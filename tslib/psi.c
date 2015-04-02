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

#include <inttypes.h>
#include <glib.h>
#include <string.h>
#include "bitreader.h"
#include "crc32m.h"
#include "log.h"

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

bool mpeg2ts_sections_equal(const mpeg2ts_section_t* a, const mpeg2ts_section_t* b)
{
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return a->bytes_len == b->bytes_len
        && !memcmp(a->bytes, b->bytes, a->bytes_len);
}

static bool read_descriptors(bitreader_t* b, size_t len, descriptor_t*** descriptors_out, size_t* descriptors_len)
{
    g_return_val_if_fail(b, false);
    g_return_val_if_fail(descriptors_out, false);
    g_return_val_if_fail(descriptors_len, false);
    g_return_val_if_fail(len < UINT16_MAX, false);

    if (len == 0) {
        *descriptors_out = NULL;
        *descriptors_len = 0;
        return true;
    }

    bool ret = true;
    GPtrArray* descriptors = g_ptr_array_new();

    uint8_t* bytes = malloc(len);
    for (size_t i = 0; i < len; ++i) {
        bytes[i] = bitreader_read_uint8(b);
    }
    if (b->error) {
        goto fail;
    }

    size_t start = 0;
    while (start < len) {
        descriptor_t* desc = descriptor_read(bytes + start, len - start);
        if (!desc) {
            goto fail;
        }
        g_ptr_array_add(descriptors, desc);
        start += desc->data_len + 2;
    }
    if (start != len) {
        g_critical("descriptors have invalid length");
        goto fail;
    }
    *descriptors_len = descriptors->len;
    *descriptors_out = (descriptor_t**)g_ptr_array_free(descriptors, false);

cleanup:
    free(bytes);
    return ret;
fail:
    g_ptr_array_set_free_func(descriptors, (GDestroyNotify)descriptor_free);
    g_ptr_array_free(descriptors, true);
    ret = false;
    goto cleanup;
}

const char* stream_desc(uint8_t stream_id)
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

static bool section_header_read(mpeg2ts_section_t* section, bitreader_t* b)
{
    g_return_val_if_fail(section, false);
    g_return_val_if_fail(b, false);

    section->table_id = bitreader_read_uint8(b);
    section->section_syntax_indicator = bitreader_read_bit(b);
    section->private_indicator = bitreader_read_bit(b);
    if ((section->table_id < 0x40 || section->table_id > 0xFE) && section->private_indicator) {
        g_critical("Private indicator set to 0x%x in table 0x%02x, but this is not in the private range 0x40-0xFE.",
                section->private_indicator, section->table_id);
        return false;
    }

    // reserved
    bitreader_skip_bits(b, 2);
    section->section_length = bitreader_read_bits(b, 12);
    if (b->error || section->section_length + 3 > section->bytes_len) {
        g_critical("Invalid section header, bad section_length or too short header!");
        return false;
    }
    section->bytes_len = section->section_length + 3;
    b->len = section->bytes_len;

    switch (section->table_id) {
    case TABLE_ID_PROGRAM_ASSOCIATION_SECTION:
    case TABLE_ID_CONDITIONAL_ACCESS_SECTION:
    case TABLE_ID_PROGRAM_MAP_SECTION:
        if (!section->section_syntax_indicator) {
            g_critical("section_syntax_indicator not set in table with table_id 0x%02x.", section->table_id);
            return false;
        }
        if (section->section_length > MAX_SECTION_LEN) {
            g_critical("section length is 0x%02X, larger than maximum allowed 0x%02X",
                    section->section_length, MAX_SECTION_LEN);
            return false;
        }
        break;
    }
    return true;
}

static program_association_section_t* program_association_section_new(uint8_t* data, size_t len)
{
    g_return_val_if_fail(data, NULL);

    program_association_section_t* pas = calloc(1, sizeof(*pas));
    pas->bytes = g_memdup(data, len);
    pas->bytes_len = len;
    return pas;
}

void program_association_section_free(program_association_section_t* pas)
{
    if (pas == NULL) {
        return;
    }

    g_free(pas->bytes);
    free(pas->programs);
    free(pas);
}

program_association_section_t* program_association_section_read(uint8_t* buf, size_t buf_len)
{
    g_return_val_if_fail(buf, NULL);
    if (!buf_len) {
        g_critical("Buffer for program association section is empty.");
        return NULL;
    }

    uint8_t offset = buf[0] + 1;
    if (offset > buf_len) {
        g_critical("Invalid pointer field %"PRIu8" in PAT", offset - 1);
        return NULL;
    }

    program_association_section_t* pas = program_association_section_new(buf + offset, buf_len - offset);
    bitreader_t* b = bitreader_new(pas->bytes, pas->bytes_len);

    if (!section_header_read((mpeg2ts_section_t*)pas, b)) {
        goto fail;
    }

    if (pas->table_id != TABLE_ID_PROGRAM_ASSOCIATION_SECTION) {
        g_critical("Table ID in PAT is 0x%02X instead of expected 0x%02X", pas->table_id,
                TABLE_ID_PROGRAM_ASSOCIATION_SECTION);
        goto fail;
    }

    pas->transport_stream_id = bitreader_read_uint16(b);

    // Reserved bits
    bitreader_skip_bits(b, 2);

    pas->version_number = bitreader_read_bits(b, 5);
    pas->current_next_indicator = bitreader_read_bit(b);

    pas->section_number = bitreader_read_uint8(b);
    pas->last_section_number = bitreader_read_uint8(b);
    if(pas->section_number != 0 || pas->last_section_number != 0) {
        g_warning("Multi-section PAT is not supported yet");
    }

    // section_length gives us the length from the end of section_length
    // we used 5 bytes for the mandatory section fields, and will use another 4 bytes for CRC
    // the remaining bytes contain program information, which is 4 bytes per iteration
    if (pas->section_length < 9) {
        g_critical("Invalid PAT, section_length of %"PRIu16" is not long enoough to hold require data.",
                pas->section_length);
        goto fail;
    }
    pas->num_programs = (pas->section_length - 5 - 4) / 4;

    pas->programs = malloc(pas->num_programs * sizeof(program_info_t));
    for (size_t i = 0; i < pas->num_programs; ++i) {
        pas->programs[i].program_number = bitreader_read_uint16(b);
        bitreader_skip_bits(b, 3); // reserved
        pas->programs[i].program_map_pid = bitreader_read_bits(b, 13);
    }

    pas->crc_32 = bitreader_read_uint32(b);

    if (b->error) {
        g_critical("Invalid Program Association Section length.");
        goto fail;
    }

    // check CRC
    crc_t pas_crc = crc_init();
    pas_crc = crc_update(pas_crc, pas->bytes, pas->section_length + 3 - 4);
    pas_crc = crc_finalize(pas_crc);
    if (pas_crc != pas->crc_32) {
        g_critical("PAT CRC_32 should be 0x%08X, but calculated as 0x%08X", pas->crc_32, pas_crc);
        goto fail;
    }

cleanup:
    bitreader_free(b);
    return pas;
fail:
    program_association_section_free(pas);
    pas = NULL;
    goto cleanup;
}

void program_association_section_print(const program_association_section_t* pas)
{
    g_return_if_fail(pas);
    if (tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }

    g_info("Program Association Section");
    SKIT_LOG_UINT8(0, pas->table_id);
    SKIT_LOG_UINT16(0, pas->section_length);
    SKIT_LOG_UINT16(0, pas->transport_stream_id);
    SKIT_LOG_UINT8(0, pas->version_number);
    SKIT_LOG_UINT8(0, pas->current_next_indicator);
    SKIT_LOG_UINT8(0, pas->section_number);
    SKIT_LOG_UINT8(0, pas->last_section_number);

    for (size_t i = 0; i < pas->num_programs; i++) {
        SKIT_LOG_UINT16(1, pas->programs[i].program_number);
        SKIT_LOG_UINT16(1, pas->programs[i].program_map_pid);
    }
    SKIT_LOG_UINT32(0, pas->crc_32);
}

static elementary_stream_info_t* es_info_new(void)
{
    elementary_stream_info_t* es = calloc(1, sizeof(*es));
    return es;
}

static void es_info_free(elementary_stream_info_t* es)
{
    if (es == NULL) {
        return;
    }

    for (size_t i = 0; i < es->descriptors_len; ++i) {
        descriptor_free(es->descriptors[i]);
    }
    g_free(es->descriptors);
    free(es);
}

static elementary_stream_info_t* es_info_read(bitreader_t* b)
{
    g_return_val_if_fail(b, NULL);

    elementary_stream_info_t* es = es_info_new();

    es->stream_type = bitreader_read_uint8(b);
    bitreader_skip_bits(b, 3);
    es->elementary_pid = bitreader_read_bits(b, 13);
    bitreader_skip_bits(b, 4);

    uint16_t es_info_length = bitreader_read_bits(b, 12);

    if (es_info_length > MAX_ES_INFO_LEN) {
        g_critical("ES info length is 0x%02X, larger than maximum allowed 0x%02X",
                       es_info_length, MAX_ES_INFO_LEN);
        goto fail;
    }

    if (!read_descriptors(b, es_info_length, &es->descriptors, &es->descriptors_len)) {
        goto fail;
    }

cleanup:
    return es;
fail:
    es_info_free(es);
    es = NULL;
    goto cleanup;
}

static void es_info_print(const elementary_stream_info_t* es, int level)
{
    g_return_if_fail(es);
    if (tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }

    SKIT_LOG_UINT16(level, es->stream_type);
    SKIT_LOG_UINT16(level, es->elementary_pid);

    for (size_t i = 0; i < es->descriptors_len; ++i) {
        descriptor_print(es->descriptors[i], level + 1);
    }
}

static program_map_section_t* program_map_section_new(uint8_t* data, size_t len)
{
    g_return_val_if_fail(data, NULL);

    program_map_section_t* pms = calloc(1, sizeof(*pms));
    pms->bytes = g_memdup(data, len);
    pms->bytes_len = len;
    return pms;
}

void program_map_section_free(program_map_section_t* pms)
{
    if (pms == NULL) {
        return;
    }

    g_free(pms->bytes);
    for (size_t i = 0; i < pms->descriptors_len; ++i) {
        descriptor_free(pms->descriptors[i]);
    }
    free(pms->descriptors);
    for (size_t i = 0; i < pms->es_info_len; ++i) {
        es_info_free(pms->es_info[i]);
    }
    free(pms->es_info);

    free(pms);
}

program_map_section_t* program_map_section_read(uint8_t* buf, size_t buf_len)
{
    g_return_val_if_fail(buf, NULL);
    if (!buf_len) {
        g_critical("Buffer for program map section is empty.");
        return NULL;
    }

    uint8_t offset = buf[0] + 1;
    if (offset > buf_len) {
        g_critical("Invalid pointer field %"PRIu8" in PMT", offset - 1);
        return NULL;
    }

    program_map_section_t* pms = program_map_section_new(buf + offset, buf_len - offset);
    bitreader_t* b = bitreader_new(pms->bytes, pms->bytes_len);
    GPtrArray* es_info = NULL;

    if (!section_header_read((mpeg2ts_section_t*)pms, b)) {
        goto fail;
    }

    if (pms->table_id != TABLE_ID_PROGRAM_MAP_SECTION) {
        g_critical("Table ID in PMT is 0x%02X instead of expected 0x%02X", pms->table_id,
                       TABLE_ID_PROGRAM_MAP_SECTION);
        goto fail;
    }

    pms->program_number = bitreader_read_uint16(b);

    // reserved
    bitreader_skip_bits(b, 2);

    pms->version_number = bitreader_read_bits(b, 5);
    pms->current_next_indicator = bitreader_read_bit(b);

    pms->section_number = bitreader_read_uint8(b);
    pms->last_section_number = bitreader_read_uint8(b);
    if (pms->section_number != 0 || pms->last_section_number != 0) {
        g_critical("Multi-section PMT is not allowed");
    }

    // reserved
    bitreader_skip_bits(b, 3);

    pms->pcr_pid = bitreader_read_bits(b, 13);
    if (pms->pcr_pid < GENERAL_PURPOSE_PID_MIN || pms->pcr_pid > GENERAL_PURPOSE_PID_MAX) {
        g_critical("PCR PID has invalid value 0x%02X", pms->pcr_pid);
        goto fail;
    }

    // reserved
    bitreader_skip_bits(b, 4);

    uint16_t program_info_length = bitreader_read_bits(b, 12);
    if (program_info_length > MAX_PROGRAM_INFO_LEN) {
        g_critical("PMT program info length is 0x%02X, larger than maximum allowed 0x%02X",
                       program_info_length, MAX_PROGRAM_INFO_LEN);
        goto fail;
    }

    if (!read_descriptors(b, program_info_length, &pms->descriptors, &pms->descriptors_len)) {
        goto fail;
    }

    es_info = g_ptr_array_new();
    while (bitreader_bytes_left(b) > 4) {  // account for CRC
        elementary_stream_info_t* es = es_info_read(b);
        if (!es) {
            goto fail;
        }
        g_ptr_array_add(es_info, es);
    }
    if (bitreader_bytes_left(b) != 4) {
        g_critical("CRC missing in PMT");
        goto fail;
    }
    pms->es_info_len = es_info->len;
    pms->es_info = (elementary_stream_info_t**)g_ptr_array_free(es_info, false);
    es_info = NULL;

    pms->crc_32 = bitreader_read_uint32(b);

    if (b->error) {
        g_critical("Invalid Program Map Section length.");
        goto fail;
    }

    // check CRC
    crc_t pms_crc = crc_init();
    pms_crc = crc_update(pms_crc, pms->bytes, pms->section_length + 3 - 4);
    pms_crc = crc_finalize(pms_crc);
    if (pms_crc != pms->crc_32) {
        g_critical("PMT CRC_32 should be 0x%08X, but calculated as 0x%08X", pms->crc_32, pms_crc);
        goto fail;
    }

cleanup:
    bitreader_free(b);
    return pms;
fail:
    program_map_section_free(pms);
    if (es_info) {
        g_ptr_array_set_free_func(es_info, (GDestroyNotify)es_info_free);
        g_ptr_array_free(es_info, true);
    }
    pms = NULL;
    goto cleanup;
}

void program_map_section_print(program_map_section_t* pms)
{
    g_return_if_fail(pms);
    if (tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }

    g_info("Program Map Section");
    SKIT_LOG_UINT8(1, pms->table_id);

    SKIT_LOG_UINT16(1, pms->section_length);
    SKIT_LOG_UINT16(1, pms->program_number);

    SKIT_LOG_UINT8(1, pms->version_number);
    SKIT_LOG_UINT8(1, pms->current_next_indicator);

    SKIT_LOG_UINT8(1, pms->section_number);
    SKIT_LOG_UINT8(1, pms->last_section_number);

    SKIT_LOG_UINT16(1, pms->pcr_pid);

    for (size_t i = 0; i < pms->descriptors_len; ++i) {
        descriptor_print(pms->descriptors[i], 2);
    }

    for (size_t i = 0; i < pms->es_info_len; ++i) {
        es_info_print(pms->es_info[i], 2);
    }

    SKIT_LOG_UINT32(1, pms->crc_32);
}

static conditional_access_section_t* conditional_access_section_new(uint8_t* data, size_t len)
{
    g_return_val_if_fail(data, NULL);

    conditional_access_section_t* cas = calloc(1, sizeof(*cas));
    cas->bytes = g_memdup(data, len);
    cas->bytes_len = len;
    return cas;
}

void conditional_access_section_free(conditional_access_section_t* cas)
{
    if (cas == NULL) {
        return;
    }

    g_free(cas->bytes);
    for (size_t i = 0; i < cas->descriptors_len; ++i) {
        descriptor_free(cas->descriptors[i]);
    }
    free(cas->descriptors);
    free(cas);
}

conditional_access_section_t* conditional_access_section_read(uint8_t* buf, size_t buf_len)
{
    g_return_val_if_fail(buf, NULL);
    if (!buf_len) {
        g_critical("Buffer for program association section is empty.");
        return NULL;
    }

    uint8_t offset = buf[0] + 1;
    if (offset > buf_len) {
        g_critical("Invalid pointer field %"PRIu8" in PAT", offset - 1);
        return NULL;
    }

    conditional_access_section_t* cas = conditional_access_section_new(buf + offset, buf_len - offset);
    bitreader_t* b = bitreader_new(cas->bytes, cas->bytes_len);

    if (!section_header_read((mpeg2ts_section_t*)cas, b)) {
        goto fail;
    }

    if (cas->table_id != TABLE_ID_CONDITIONAL_ACCESS_SECTION) {
        g_critical("Table ID in CAT is 0x%02X instead of expected 0x%02X",
                       cas->table_id, TABLE_ID_CONDITIONAL_ACCESS_SECTION);
        goto fail;
    }

    // 18-bits of reserved value
    bitreader_read_uint16(b);
    bitreader_skip_bits(b, 2);

    cas->version_number = bitreader_read_bits(b, 5);
    cas->current_next_indicator = bitreader_read_bit(b);

    cas->section_number = bitreader_read_uint8(b);
    cas->last_section_number = bitreader_read_uint8(b);
    if (cas->section_number != 0 || cas->last_section_number != 0) {
        g_warning("Multi-section CAT is not supported yet");
    }

    if (cas->section_length < 9) {
        g_critical("Invalid CAT section length, %"PRIu16" is not long enough to hold required data.",
                cas->section_length);
        goto fail;
    }
    if (!read_descriptors(b, cas->section_length - 5 - 4, &cas->descriptors, &cas->descriptors_len)) {
        goto fail;
    }

    // explanation: section_length gives us the length from the end of section_length
    // we used 5 bytes for the mandatory section fields, and will use another 4 bytes for CRC
    // the remaining bytes contain descriptors, most probably only one
    cas->crc_32 = bitreader_read_uint32(b);

    if (b->error) {
        g_critical("Invalid Program Map Section length.");
        goto fail;
    }

    // check CRC
    crc_t crc = crc_init();
    crc = crc_update(crc, cas->bytes, cas->section_length + 3 - 4);
    crc = crc_finalize(crc);
    if (crc != cas->crc_32) {
        g_critical("CAT CRC_32 should be 0x%08X, but calculated as 0x%08X", cas->crc_32, crc);
        goto fail;
    }

cleanup:
    bitreader_free(b);
    return cas;
fail:
    conditional_access_section_free(cas);
    cas = NULL;
    goto cleanup;
}

void conditional_access_section_print(const conditional_access_section_t* cas)
{
    g_return_if_fail(cas);
    if (tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }

    g_info("Conditional Access Section");
    SKIT_LOG_UINT8(0, cas->table_id);
    SKIT_LOG_UINT16(0, cas->section_length);

    SKIT_LOG_UINT8(0, cas->version_number);
    SKIT_LOG_UINT8(0, cas->current_next_indicator);
    SKIT_LOG_UINT8(0, cas->section_number);
    SKIT_LOG_UINT8(0, cas->last_section_number);

    for (size_t i = 0; i < cas->descriptors_len; ++i) {
        descriptor_print(cas->descriptors[i], 2);
    }

    SKIT_LOG_UINT32(0, cas->crc_32);
}