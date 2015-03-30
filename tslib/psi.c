/*
 Copyright (c) 2012, Alex Giladi <alex.giladi@gmail.com>
 Copyright (c) 2013- Alex Giladi <alex.giladi@gmail.com> and Vlad Zbarsky <zbarsky@cornell.edu>
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of the ISO/IEC nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "psi.h"

#include <glib.h>
#include <stdbool.h>
#include "bs.h"
#include "libts_common.h"
#include "ts.h"
#include "descriptors.h"
#include "log.h"
#include "crc32m.h"


#define ARRAYSIZE(x)   ((sizeof(x))/(sizeof((x)[0])))


static char* first_stream_types[] = { // 0x00 - 0x23
    "ITU-T | ISO/IEC Reserved",
    "ISO/IEC 11172-2 Video",
    "ISO/IEC 13818-2 Video",
    "ISO/IEC 11172-3 Audio",
    "ISO/IEC 13818-3 Audio",
    "ISO/IEC 13818-1 private_sections",
    "ISO/IEC 13818-1 PES packets containing private data",
    "ISO/IEC 13522 MHEG",
    "ISO/IEC 13818-1 Annex A DSM-CC",
    "ITU-T H.222.1",
    "ISO/IEC 13818-6 type A",
    "ISO/IEC 13818-6 type B",
    "ISO/IEC 13818-6 type C",
    "ISO/IEC 13818-6 type D",
    "ISO/IEC 13818-1 auxiliary",
    "ISO/IEC 13818-7 Audio with ADTS transport syntax",
    "ISO/IEC 14496-2 Visual",
    "ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3",
    "ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets",
    "ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC 14496_sections",
    "ISO/IEC 13818-6 Synchronized Download Protocol",
    "Metadata carried in PES packets",
    "Metadata carried in metadata_sections",
    "Metadata carried in ISO/IEC 13818-6 Data Carousel",
    "Metadata carried in ISO/IEC 13818-6 Object Carousel",
    "Metadata carried in ISO/IEC 13818-6 Synchronized Download Protocol",
    "IPMP stream (defined in ISO/IEC 13818-11, MPEG-2 IPMP)",
    "ISO/IEC 14496-10 AVC",
    "ISO/IEC 14496-3 Audio, without using any additional transport syntax, such as DST, ALS and SLS",
    "ISO/IEC 14496-17 Text",
    "Auxiliary video stream as defined in ISO/IEC 23002-3",
    "SVC video sub-bitstream of an AVC video stream conforming to one or more profiles defined in Annex G of Rec. ITU-T H.264 | ISO/IEC 14496-10",
    "MVC video sub-bitstream of an AVC video stream conforming to one or more profiles defined in Annex H of Rec. ITU-T H.264 | ISO/IEC 14496-10",
    "Video stream conforming to one or more profiles as defined in Rec. ITU-T T.800 | ISO/IEC 15444-1",
    "Additional view Rec. ITU-T H.262 | ISO/IEC 13818-2 video stream for service-compatible stereoscopic 3D services (see note 3 and 4)",
    "Additional view Rec. ITU-T H.264 | ISO/IEC 14496-10 video stream conforming to one or more profiles defined in Annex A for service-compatible stereoscopic 3D services (see note 3 and 4)"
};

static char* STREAM_DESC_RESERVED  = "ISO/IEC 13818-1 Reserved"; // 0x24-0x7E
static char* STREAM_DESC_IPMP = "IPMP Stream"; // 0x7F
static char* STREAM_DESC_USER_PRIVATE = "User Private"; // 0x80-0xFF

static bool read_descriptors(GPtrArray* descriptors, bs_t* b, size_t len)
{
    g_return_val_if_fail(descriptors, false);
    g_return_val_if_fail(b, false);

    bool ret = true;
    uint8_t* descriptor_bytes = malloc(len);
    bs_read_bytes(b, descriptor_bytes, len);

    size_t start = 0;
    while (start < len) {
        descriptor_t* desc = descriptor_read(descriptor_bytes + start, len - start);
        if (!desc) {
            goto fail;
        }
        g_ptr_array_add(descriptors, desc);
        start += desc->data_len + 2;
    }
    if (start > len) {
        g_critical("descriptors have invalid length");
        goto fail;
    }

cleanup:
    free(descriptor_bytes);
    return ret;
fail:
    ret = false;
    goto cleanup;
}

char* stream_desc(uint8_t stream_id)
{
    if(stream_id < ARRAYSIZE(first_stream_types)) {
        return first_stream_types[stream_id];
    } else if(stream_id < STREAM_TYPE_IPMP) {
        return STREAM_DESC_RESERVED;
    } else if(stream_id == STREAM_TYPE_IPMP) {
        return STREAM_DESC_IPMP;
    } else {
        return STREAM_DESC_USER_PRIVATE;
    }
}

static int section_header_read(mpeg2ts_section_t* section, bs_t* b)
{
    section->table_id = bs_read_u8(b);
    section->section_syntax_indicator = bs_read_u1(b);
    section->private_indicator = bs_read_u1(b);
    if ((section->table_id < 0x40 || section->table_id > 0xFE) && section->private_indicator) {
        g_critical("Private indicator set to 0x%x in table 0x%02x, but this is not in the private range 0x40-0xFE.",
                section->private_indicator, section->table_id);
        return 0;
    }

    // reserved
    bs_skip_u(b, 2);
    section->section_length = bs_read_u(b, 12);

    switch (section->table_id) {
    case TABLE_ID_PROGRAM_ASSOCIATION_SECTION:
    case TABLE_ID_CONDITIONAL_ACCESS_SECTION:
    case TABLE_ID_PROGRAM_MAP_SECTION:
        if (!section->section_syntax_indicator) {
            g_critical("section_syntax_indicator not set in table with table_id 0x%02x.", section->table_id);
            return 0;
        }
        if (section->section_length > MAX_SECTION_LEN) {
            g_critical("section length is 0x%02X, larger than maximum allowed 0x%02X",
                    section->section_length, MAX_SECTION_LEN);
            return 0;
        }
        break;
    }
    return 1;
}

static program_association_section_t* program_association_section_new(void)
{
    program_association_section_t* pas = calloc(1, sizeof(*pas));
    return pas;
}

void program_association_section_free(program_association_section_t* pas)
{
    if (pas == NULL) {
        return;
    }

    free(pas->programs);
    free(pas);
}

program_association_section_t* program_association_section_read(uint8_t* buf, size_t buf_len)
{
    program_association_section_t* pas = program_association_section_new();
    memcpy(pas->bytes, buf, buf_len);
    bs_t* b = bs_new(buf, buf_len);

    if (!section_header_read((mpeg2ts_section_t*)pas, b)) {
        goto fail;
    }

    if (pas->table_id != TABLE_ID_PROGRAM_ASSOCIATION_SECTION) {
        g_critical("Table ID in PAT is 0x%02X instead of expected 0x%02X", pas->table_id,
                TABLE_ID_PROGRAM_ASSOCIATION_SECTION);
        goto fail;
    }

    pas->transport_stream_id = bs_read_u16(b);

    // Reserved bits
    bs_skip_u(b, 2);

    pas->version_number = bs_read_u(b, 5);
    pas->current_next_indicator = bs_read_u1(b);
    if (!pas->current_next_indicator) {
        g_warning("This PAT is not yet applicable");
    }

    pas->section_number = bs_read_u8(b);
    pas->last_section_number = bs_read_u8(b);
    if(pas->section_number != 0 || pas->last_section_number != 0) {
        g_warning("Multi-section PAT is not supported yet");
        goto fail;
    }

    // section_length gives us the length from the end of section_length
    // we used 5 bytes for the mandatory section fields, and will use another 4 bytes for CRC
    // the remaining bytes contain program information, which is 4 bytes per iteration
    pas->num_programs = (pas->section_length - 5 - 4) / 4;

    if (pas->num_programs > 1) {
        g_warning("%zd programs found, but only SPTS is fully supported. Patches are welcome.",
                pas->num_programs);
        goto fail;
    }

    pas->programs = malloc(pas->num_programs * sizeof(program_info_t));
    for (size_t i = 0; i < pas->num_programs; ++i) {
        pas->programs[i].program_number = bs_read_u16(b);
        bs_skip_u(b, 3); // reserved
        pas->programs[i].program_map_pid = bs_read_u(b, 13);
    }

    pas->crc_32 = bs_read_u32(b);

    // check CRC
    crc_t pas_crc = crc_init();
    pas_crc = crc_update(pas_crc, buf, bs_pos(b) - 4);
    pas_crc = crc_finalize(pas_crc);
    if (pas_crc != pas->crc_32) {
        g_critical("PAT CRC_32 specified as 0x%08X, but calculated as 0x%08X", pas->crc_32, pas_crc);
        goto fail;
    }

cleanup:
    bs_free(b);
    return pas;
fail:
    program_association_section_free(pas);
    pas = NULL;
    goto cleanup;
}

void program_association_section_print(const program_association_section_t* pas)
{
    if (pas == NULL || tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }

    g_info("Program Association Section");
    SKIT_LOG_UINT(0, pas->table_id);
    SKIT_LOG_UINT(0, pas->section_length);
    SKIT_LOG_UINT_HEX(0, pas->transport_stream_id);
    SKIT_LOG_UINT(0, pas->version_number);
    SKIT_LOG_UINT(0, pas->current_next_indicator);
    SKIT_LOG_UINT(0, pas->section_number);
    SKIT_LOG_UINT(0, pas->last_section_number);

    for (size_t i = 0; i < pas->num_programs; i++) {
        SKIT_LOG_UINT(1, pas->programs[i].program_number);
        SKIT_LOG_UINT_HEX(1, pas->programs[i].program_map_pid);
    }
    SKIT_LOG_UINT_HEX(0, pas->crc_32);
}

static elementary_stream_info_t* es_info_new(void)
{
    elementary_stream_info_t* es = calloc(1, sizeof(*es));
    es->descriptors = g_ptr_array_new_with_free_func((GDestroyNotify)descriptor_free);
    return es;
}

static void es_info_free(elementary_stream_info_t* es)
{
    if (es == NULL) {
        return;
    }

    g_ptr_array_free(es->descriptors, true);
    free(es);
}

static elementary_stream_info_t* es_info_read(bs_t* b)
{
    elementary_stream_info_t* es = es_info_new();

    es->stream_type = bs_read_u8(b);
    bs_skip_u(b, 3);
    es->elementary_pid = bs_read_u(b, 13);
    bs_skip_u(b, 4);
    es->es_info_length = bs_read_u(b, 12);

    if (es->es_info_length > MAX_ES_INFO_LEN) {
        g_critical("ES info length is 0x%02X, larger than maximum allowed 0x%02X",
                       es->es_info_length, MAX_ES_INFO_LEN);
        goto fail;
    }

    if (!read_descriptors(es->descriptors, b, es->es_info_length)) {
        goto fail;
    }

    return es;
fail:
    es_info_free(es);
    return NULL;
}

static void es_info_print(elementary_stream_info_t* es, int level)
{
    if (tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }

    SKIT_LOG_UINT_VERBOSE(level, es->stream_type, stream_desc(es->stream_type));
    SKIT_LOG_UINT_HEX(level, es->elementary_pid);
    SKIT_LOG_UINT(level, es->es_info_length);

    for (size_t i = 0; i < es->descriptors->len; ++i) {
        descriptor_print(g_ptr_array_index(es->descriptors, i), level + 1);
    }
}

static program_map_section_t* program_map_section_new(void)
{
    program_map_section_t* pms = calloc(1, sizeof(*pms));
    pms->descriptors = g_ptr_array_new_with_free_func((GDestroyNotify)descriptor_free);
    pms->es_info = g_ptr_array_new_with_free_func((GDestroyNotify)es_info_free);
    return pms;
}

void program_map_section_free(program_map_section_t* pms)
{
    if (pms == NULL) {
        return;
    }

    g_ptr_array_free(pms->descriptors, true);
    g_ptr_array_free(pms->es_info, true);

    free(pms);
}

program_map_section_t* program_map_section_read(uint8_t* buf, size_t buf_len)
{
    program_map_section_t* pms = program_map_section_new();
    memcpy(pms->bytes, buf, buf_len);
    bs_t* b = bs_new(buf, buf_len);

    if (!section_header_read((mpeg2ts_section_t*)pms, b)) {
        goto fail;
    }

    if (pms->table_id != TABLE_ID_PROGRAM_MAP_SECTION) {
        g_critical("Table ID in PMT is 0x%02X instead of expected 0x%02X", pms->table_id,
                       TABLE_ID_PROGRAM_MAP_SECTION);
        goto fail;
    }

    int section_start = bs_pos(b);

    pms->program_number = bs_read_u16(b);

    // reserved
    bs_skip_u(b, 2);

    pms->version_number = bs_read_u(b, 5);
    pms->current_next_indicator = bs_read_u1(b);
    if (!pms->current_next_indicator) {
        g_warning("This PMT is not yet applicable");
    }

    pms->section_number = bs_read_u8(b);
    pms->last_section_number = bs_read_u8(b);
    if (pms->section_number != 0 || pms->last_section_number != 0) {
        g_critical("Multi-section PMT is not allowed");
        goto fail;
    }

    // reserved
    bs_skip_u(b, 3);

    pms->pcr_pid = bs_read_u(b, 13);
    if (pms->pcr_pid < GENERAL_PURPOSE_PID_MIN || pms->pcr_pid > GENERAL_PURPOSE_PID_MAX) {
        g_critical("PCR PID has invalid value 0x%02X", pms->pcr_pid);
        goto fail;
    }

    // reserved
    bs_skip_u(b, 4);

    pms->program_info_length = bs_read_u(b, 12);
    if (pms->program_info_length > MAX_PROGRAM_INFO_LEN) {
        g_critical("PMT program info length is 0x%02X, larger than maximum allowed 0x%02X",
                       pms->program_info_length, MAX_PROGRAM_INFO_LEN);
        goto fail;
    }

    read_descriptors(pms->descriptors, b, pms->program_info_length);

    while (pms->section_length - (bs_pos(b) - section_start) > 4) {  // account for CRC
        elementary_stream_info_t* es = es_info_read(b);
        if (es == NULL) {
            goto fail;
        }
        g_ptr_array_add(pms->es_info, es);
    }

    pms->crc_32 = bs_read_u32(b);

    // check CRC
    crc_t pas_crc = crc_init();
    pas_crc = crc_update(pas_crc, buf, bs_pos(b) - 4);
    pas_crc = crc_finalize(pas_crc);
    if(pas_crc != pms->crc_32) {
        g_critical("PMT CRC_32 specified as 0x%08X, but calculated as 0x%08X", pms->crc_32, pas_crc);
        goto fail;
    } else {
        g_debug("PMT CRC_32 checked successfully");
    }

cleanup:
    bs_free(b);
    return pms;
fail:
    program_map_section_free(pms);
    pms = NULL;
    goto cleanup;
}

void program_map_section_print(program_map_section_t* pms)
{
    if (tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }

    g_info("Program Map Section");
    SKIT_LOG_UINT(1, pms->table_id);

    SKIT_LOG_UINT(1, pms->section_length);
    SKIT_LOG_UINT(1, pms->program_number);

    SKIT_LOG_UINT(1, pms->version_number);
    SKIT_LOG_UINT(1, pms->current_next_indicator);

    SKIT_LOG_UINT(1, pms->section_number);
    SKIT_LOG_UINT(1, pms->last_section_number);

    SKIT_LOG_UINT_HEX(1, pms->pcr_pid);

    SKIT_LOG_UINT(1, pms->program_info_length);

    for (size_t i = 0; i < pms->descriptors->len; ++i) {
        descriptor_print(g_ptr_array_index(pms->descriptors, i), 2);
    }

    for (gsize i = 0; i < pms->es_info->len; ++i) {
        elementary_stream_info_t* es = g_ptr_array_index(pms->es_info, i);
        es_info_print(es, 2);
    }

    SKIT_LOG_UINT_HEX(1, pms->crc_32);
}

static conditional_access_section_t* conditional_access_section_new(void)
{
    conditional_access_section_t* cas = calloc(1, sizeof(*cas));
    cas->descriptors = g_ptr_array_new_with_free_func((GDestroyNotify)descriptor_free);
    return cas;
}

void conditional_access_section_free(conditional_access_section_t* cas)
{
    if (cas == NULL) {
        return;
    }

    g_ptr_array_free(cas->descriptors, true);
    free(cas);
}

conditional_access_section_t* conditional_access_section_read(uint8_t* buf, size_t buf_len)
{
    conditional_access_section_t* cas = conditional_access_section_new();
    memcpy(cas->bytes, buf, buf_len);
    bs_t* b = bs_new(buf, buf_len);

    if (!section_header_read((mpeg2ts_section_t*)cas, b)) {
        goto fail;
    }

    if (cas->table_id != TABLE_ID_CONDITIONAL_ACCESS_SECTION) {
        g_critical("Table ID in CAT is 0x%02X instead of expected 0x%02X",
                       cas->table_id, TABLE_ID_CONDITIONAL_ACCESS_SECTION);
        goto fail;
    }

    // 18-bits of reserved value
    bs_read_u16(b);
    bs_skip_u(b, 2);

    cas->version_number = bs_read_u(b, 5);
    cas->current_next_indicator = bs_read_u1(b);
    if (!cas->current_next_indicator) {
        g_warning("This CAT is not yet applicable");
    }

    cas->section_number = bs_read_u8(b);
    cas->last_section_number = bs_read_u8(b);
    if (cas->section_number != 0 || cas->last_section_number != 0) {
        g_warning("Multi-section CAT is not supported yet");
        goto fail;
    }

    read_descriptors(cas->descriptors, b, cas->section_length - 5 - 4);

    // explanation: section_length gives us the length from the end of section_length
    // we used 5 bytes for the mandatory section fields, and will use another 4 bytes for CRC
    // the remaining bytes contain descriptors, most probably only one
    cas->crc_32 = bs_read_u32(b);

    // check CRC
    crc_t cas_crc = crc_init();
    cas_crc = crc_update(cas_crc, buf, bs_pos(b) - 4);
    cas_crc = crc_finalize(cas_crc);
    if (cas_crc != cas->crc_32) {
        g_critical("CAT CRC_32 specified as 0x%08X, but calculated as 0x%08X", cas->crc_32, cas_crc);
        goto fail;
    }

cleanup:
    bs_free(b);
    return cas;
fail:
    conditional_access_section_free(cas);
    cas = NULL;
    goto cleanup;
}

void conditional_access_section_print(const conditional_access_section_t* cas)
{
    if (tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }

    g_info("Conditional Access Section");
    SKIT_LOG_UINT(0, cas->table_id);
    SKIT_LOG_UINT(0, cas->section_length);

    SKIT_LOG_UINT(0, cas->version_number);
    SKIT_LOG_UINT(0, cas->current_next_indicator);
    SKIT_LOG_UINT(0, cas->section_number);
    SKIT_LOG_UINT(0, cas->last_section_number);

    for (size_t i = 0; i < cas->descriptors->len; ++i) {
        descriptor_print(g_ptr_array_index(cas->descriptors, i), 2);
    }

    SKIT_LOG_UINT_HEX(0, cas->crc_32);
}
