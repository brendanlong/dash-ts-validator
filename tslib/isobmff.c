/*
 Copyright (c) 2014-, ISO/IEC JTC1/SC29/WG11
 All rights reserved.

 See AUTHORS for a full list of authors.

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
#include "isobmff.h"

#include <glib.h>
#include "log.h"


static bool read_full_box(bitreader_t*, fullbox_t*);
static box_t* read_styp(bitreader_t*, int* error);
static box_t* read_sidx(bitreader_t*, int* error);
static box_t* read_pcrb(bitreader_t*, int* error);
static box_t* read_ssix(bitreader_t*, int* error);
static box_t* read_emsg(bitreader_t*, int* error);

static void print_fullbox(const fullbox_t*);
static void print_styp(const styp_t*);
static void print_sidx(const sidx_t*);
static void print_pcrb(const pcrb_t*);
static void print_ssix(const ssix_t*);
static void print_emsg(const emsg_t*);

static void print_sidx_reference(const sidx_reference_t*);
static void print_ssix_subsegment(const ssix_subsegment_t*);

static void free_styp(styp_t*);
static void free_sidx(sidx_t*);
static void free_pcrb(pcrb_t*);
static void free_ssix(ssix_t*);
static void free_emsg(emsg_t*);


void uint32_to_string(char* str, uint32_t num)
{
    g_return_if_fail(str);

    str[0] = (num >> 24) & 0xff;
    str[1] = (num >> 16) & 0xff;
    str[2] = (num >>  8) & 0xff;
    str[3] = (num >>  0) & 0xff;
}

void free_box(box_t* box)
{
    if (box == NULL) {
        return;
    }
    switch (box->type) {
    case BOX_TYPE_STYP: {
        free_styp((styp_t*)box);
        break;
    }
    case BOX_TYPE_SIDX: {
        free_sidx((sidx_t*)box);
        break;
    }
    case BOX_TYPE_PCRB: {
        free_pcrb((pcrb_t*)box);
        break;
    }
    case BOX_TYPE_SSIX: {
        free_ssix((ssix_t*)box);
        break;
    }
    case BOX_TYPE_EMSG: {
        free_emsg((emsg_t*)box);
        break;
    }
    default:
        g_slice_free(box_t, box);
        break;
    }
}

void free_boxes(box_t** boxes, size_t num_boxes)
{
    if (boxes == NULL) {
        return;
    }
    for (size_t i = 0; i < num_boxes; i++) {
        free_box(boxes[i]);
    }
    g_free(boxes);
}

box_t** read_boxes_from_file(const char* file_name, size_t* num_boxes, int* error_out)
{
    g_return_val_if_fail(file_name, NULL);
    g_return_val_if_fail(num_boxes, NULL);

    box_t** boxes = NULL;
    *num_boxes = 0;
    bitreader_t* b = NULL;

    gchar* file_contents = NULL;
    size_t file_len;
    GError* error = NULL;
    if (!g_file_get_contents(file_name, &file_contents, &file_len, &error)) {
        g_critical("While looking for ISOBMFF boxes, failed to open file %s. Error is: %s.", file_name, error->message);
        goto fail;
    }

    b = bitreader_new((uint8_t*)file_contents, file_len);
    boxes = read_boxes_from_stream(b, num_boxes, error_out);
cleanup:
    bitreader_free(b);
    g_free(file_contents);
    if (error) {
        g_error_free(error);
    }
    return boxes;
fail:
    if (error_out) {
        *error_out = 1;
    }
    goto cleanup;
}

box_t** read_boxes_from_stream(bitreader_t* b, size_t* num_boxes, int* error_out)
{
    g_return_val_if_fail(b, NULL);
    g_return_val_if_fail(num_boxes, NULL);

    GPtrArray* boxes = g_ptr_array_new();
    box_t** boxes_out = NULL;
    while (!bitreader_eof(b)) {
        int error = 0;
        box_t* box = read_box(b, &error);
        if (error) {
            goto fail;
        }
        g_ptr_array_add(boxes, box);
    }
    *num_boxes = boxes->len;
    boxes_out = (box_t**)g_ptr_array_free(boxes, false);
cleanup:
    return boxes_out;
fail:
    if (error_out) {
        *error_out = 1;
    }
    g_ptr_array_set_free_func(boxes, (GDestroyNotify)free_box);
    g_ptr_array_free(boxes, true);
    goto cleanup;
}

box_t* read_box(bitreader_t* b, int* error_out)
{
    g_return_val_if_fail(b, NULL);

    /*
    aligned(8) class Box (unsigned int(32) boxtype, optional unsigned int(8)[16] extended_type) {
        unsigned int(32) size;
        unsigned int(32) type = boxtype;
        if (size == 1) {
            unsigned int(64) largesize;
        } else if (size == 0) {
            // box extends to end of file
        }
        if (boxtype == "uuid") {
            unsigned int(8)[16] usertype = extended_type;
        }
    }
    */
    box_t* box = NULL;
    bitreader_t* box_reader = NULL;
    uint64_t size = bitreader_read_uint32(b);
    uint32_t type = bitreader_read_uint32(b);
    char type_str[5] = {0};
    uint32_to_string(type_str, type);

    if (size == 1) {
        size = bitreader_read_uint64(b);
    } else if (size == 0) {
        size = bitreader_bytes_left(b); // box extends to the end of the file
    }

    if (b->error) {
        g_critical("Error reading size or type for ISOBMFF box. Size: %"PRIu64", Type: 0x%"PRIx32" (%s)",
                size, type, type_str);
        goto fail;
    }

    if (size < 8) {
        g_critical("ISOBMFF box with type 0x%"PRIx32" (%s) has size %"PRIu64", but should be >= 8.",
                type, type_str, size);
    }

    /* Ignore size for Box itself */
    box_reader = bitreader_read_bytes_as_bitreader(b, size - 8);
    if (!box_reader) {
        g_critical("Failed to read box with type 0x%"PRIx32" (%s), not data to read.", type, type_str);
        goto fail;
    }
    int error = 0;
    switch (type) {
    case BOX_TYPE_STYP:
        box = read_styp(box_reader, &error);
        break;
    case BOX_TYPE_SIDX:
        box = read_sidx(box_reader, &error);
        break;
    case BOX_TYPE_PCRB:
        box = read_pcrb(box_reader, &error);
        break;
    case BOX_TYPE_SSIX:
        box = read_ssix(box_reader, &error);
        break;
    case BOX_TYPE_EMSG:
        box = read_emsg(box_reader, &error);
        break;
    default: {
        char tmp[5] = {0};
        uint32_to_string(tmp, type);
        g_debug("Unknown box type: %s.", tmp);
        box = g_slice_new(box_t);
    }
    }
    if (error) {
        goto fail;
    }
    if (box) {
        box->type = type;
        box->size = size;
    }
    if (box_reader->error) {
        g_critical("Input error reading box with type 0x%"PRIx32" (%s) and size %"PRIu64". Buffer had size %zu.",
                type, type_str, size, box_reader->len + 8);
        goto fail;
    }
    if (!bitreader_eof(box_reader)) {
        g_critical("Box with type 0x%"PRIx32" (%s) had extra data that was ignored. Size was %"PRIu64", buffer had "
                "size %zu. Reader has %zu bits left.",
                type, type_str, size, box_reader->len + 8, bitreader_bits_left(box_reader));
        goto fail;
    }

cleanup:
    bitreader_free(box_reader);
    return box;
fail:
    if (error_out) {
        *error_out = 1;
    }
    free_box(box);
    box = NULL;
    goto cleanup;
}

bool read_full_box(bitreader_t* b, fullbox_t* box)
{
    g_return_val_if_fail(b, false);
    g_return_val_if_fail(box, false);

    /*
    aligned(8) class FullBox(unsigned int(32) boxtype, unsigned int(8) v, bit(24) f) extends Box(boxtype) {
        unsigned int(8) version = v;
        bit(24) flags = f;
    }
    */
    box->version = bitreader_read_uint8(b);
    box->flags = bitreader_read_uint24(b);
    return !b->error;
}

void free_styp(styp_t* box)
{
    if (box == NULL) {
        return;
    }
    g_free(box->compatible_brands);
    g_slice_free(styp_t, box);
}

box_t* read_styp(bitreader_t* b, int* error_out)
{
    g_return_val_if_fail(b, NULL);

    /*
    "A segment type has the same format as an 'ftyp' box [4.3], except that it takes the box type 'styp'."
    aligned(8) class FileTypeBox extends Box(‘ftyp’) {
        unsigned int(32) major_brand;
        unsigned int(32) minor_version;
        unsigned int(32) compatible_brands[];
    }
    */
    styp_t* box = g_slice_new0(styp_t);

    box->major_brand = bitreader_read_uint32(b);
    box->minor_version = bitreader_read_uint32(b);
    box->num_compatible_brands = bitreader_bytes_left(b) / 4;
    if (box->num_compatible_brands != 0) {
        box->compatible_brands = g_try_new(uint32_t, box->num_compatible_brands);
        if (!box->compatible_brands) {
            g_critical("Failed to allocate 'styp' box's compatible brands. num_compatible_brands = %zu.",
                    box->num_compatible_brands);
            goto fail;
        }
        for(size_t i = 0; i < box->num_compatible_brands; ++i) {
            box->compatible_brands[i] = bitreader_read_uint32(b);
        }
    }
    return (box_t*)box;
fail:
    if (error_out) {
        *error_out = 1;
    }
    free_styp(box);
    return NULL;
}

void free_sidx(sidx_t* box)
{
    if (box == NULL) {
        return;
    }
    g_free(box->references);
    g_slice_free(sidx_t, box);
}

box_t* read_sidx(bitreader_t* b, int* error_out)
{
    g_return_val_if_fail(b, NULL);

    /*
    aligned(8) class segment_indexBox extends FullBox("sidx", version, 0) {
        unsigned int(32) reference_id;
        unsigned int(32) timescale;
        if (version == 0) {
            unsigned int(32) earliest_presentation_time;
            unsigned int(32) first_offset;
        } else {
            unsigned int(64) earliest_presentation_time;
            unsigned int(64) first_offset;
        }
        unsigned int(16) reserved = 0;
        unsigned int(16) reference_count;
        for(i = 1; i <= reference_count; i++) {
            bit (1) reference_type;
            unsigned int(31) referenced_size;
            unsigned int(32) subsegment_duration;
            bit(1) starts_with_sap;
            unsigned int(3) sap_type;
            unsigned int(28) sap_delta_time;
        }
    }
    */
    sidx_t* box = g_slice_new0(sidx_t);
    if (!read_full_box(b, (fullbox_t*)box)) {
        goto fail;
    }

    box->reference_id = bitreader_read_uint32(b);
    box->timescale = bitreader_read_uint32(b);

    if (box->version == 0) {
        box->earliest_presentation_time = bitreader_read_uint32(b);
        box->first_offset = bitreader_read_uint32(b);
    } else {
        box->earliest_presentation_time = bitreader_read_uint64(b);
        box->first_offset = bitreader_read_uint64(b);
    }

    bitreader_skip_bytes(b, 2); // reserved
    box->reference_count = bitreader_read_uint16(b);

    if (box->reference_count > 0) {
        box->references = g_try_new(sidx_reference_t, box->reference_count);
        if (!box->references) {
            g_critical("Failed to allocate %"PRIu16" sidx_reference_t's.", box->reference_count);
            goto fail;
        }
        for (size_t i = 0; i < box->reference_count; ++i) {
            sidx_reference_t* reference = &box->references[i];
            uint32_t tmp = bitreader_read_uint32(b);
            reference->reference_type = tmp >> 31;
            reference->referenced_size = tmp & 0x7fffffff;
            reference->subsegment_duration = bitreader_read_uint32(b);
            tmp = bitreader_read_uint32(b);
            reference->starts_with_sap = tmp >> 31;
            reference->sap_type = (tmp >> 28) & 0x7;
            reference->sap_delta_time = tmp & 0x0fffffff;
        }
    }
    return (box_t*)box;
fail:
    if (error_out) {
        *error_out = 1;
    }
    free_sidx(box);
    return NULL;
}

void free_pcrb(pcrb_t* box)
{
    if (box == NULL) {
        return;
    }
    g_free(box->pcr);
    g_slice_free(pcrb_t, box);
}

box_t* read_pcrb(bitreader_t* b, int* error_out)
{
    g_return_val_if_fail(b, NULL);

    /* 6.4.7.2 MPEG-2 TS PCR information box
    aligned(8) class MPEG2TSPCRInfoBox extends Box(‘pcrb’, 0) {
        unsigned int(32) subsegment_count;
        for (i = 1; i <= subsegment_count; i++) {
            unsigned int(42) pcr;
            unsigned int(6) pad = 0;
        }
    }
    */
    pcrb_t* box = g_slice_new0(pcrb_t);

    box->subsegment_count = bitreader_read_uint32(b);

    uint64_t pcr_size = box->subsegment_count * (48 / 8);
    if (pcr_size != bitreader_bytes_left(b)) {
        g_critical("pcrb box has subsegment_count %"PRIu32", indicating the remaining size should be %"PRIu64" bytes, "
                "but the box has %"PRIu64" bytes left.",
                box->subsegment_count, pcr_size, bitreader_bytes_left(b));
        if (bitreader_bytes_left(b) == box->subsegment_count * 8) {
            g_critical("Note: Your encoder appears to be writing 64-bit pcrb entries instead of 48-bit. See "
                    "https://github.com/gpac/gpac/issues/34 for details.");
        }
        goto fail;
    }
    if (box->subsegment_count) {
        box->pcr = g_try_new(uint64_t, box->subsegment_count);
        if (!box->pcr) {
            g_critical("Failed to allocate %"PRIu32" uint64_t's for 'pcrb' box's 'pcr'.", box->subsegment_count);
            goto fail;
        }
        for (size_t i = 0; i < box->subsegment_count; ++i) {
            box->pcr[i] = bitreader_read_bits(b, 42);
            bitreader_skip_bits(b, 6);
        }
    }

    return (box_t*)box;
fail:
    if (error_out) {
        *error_out = 1;
    }
    free_pcrb(box);
    return NULL;
}

void free_ssix(ssix_t* box)
{
    if (box == NULL) {
        return;
    }
    for (size_t i = 0; i < box->subsegment_count; i++) {
        g_free(box->subsegments[i].ranges);
    }
    g_free(box->subsegments);
    g_slice_free(ssix_t, box);
}

box_t* read_ssix(bitreader_t* b, int* error_out)
{
    g_return_val_if_fail(b, NULL);

    /*
    aligned(8) class Subsegment_indexBox extends FullBox("ssix", 0, 0) {
       unsigned int(32) subsegment_count;
       for(i = 1; i <= subsegment_count; i++)
       {
          unsigned int(32) ranges_count;
          for (j = 1; j <= range_count; j++)
          {
             unsigned int(8) level;
             unsigned int(24) range_size;
          }
       }
    }
    */
    ssix_t* box = g_slice_new0(ssix_t);
    if (!read_full_box(b, (fullbox_t*)box)) {
        goto fail;
    }

    box->subsegment_count = bitreader_read_uint32(b);
    if (box->subsegment_count * 4 > bitreader_bytes_left(b)) {
        g_critical("Not enough bytes left in 'ssix' box to read the required %"PRIu32" subsegments.",
                box->subsegment_count);
        goto fail;
    }
    if (box->subsegment_count > 0) {
        box->subsegments = g_try_new0(ssix_subsegment_t, box->subsegment_count);
        if (!box->subsegments) {
            g_critical("Failed to allocate %"PRIu32" ssix_subsegment_t for 'ssix' box.", box->subsegment_count);
            goto fail;
        }
        for (size_t i = 0; i < box->subsegment_count; i++) {
            ssix_subsegment_t* subsegment = &box->subsegments[i];
            subsegment->ranges_count = bitreader_read_uint32(b);
            if (subsegment->ranges_count * 4 > bitreader_bytes_left(b)) {
                g_critical("Not enough bytes left in 'ssix' box to read the required %"PRIu32" ranges.",
                        subsegment->ranges_count);
                goto fail;
            }
            if (subsegment->ranges_count > 0) {
                subsegment->ranges = g_try_new0(ssix_subsegment_range_t, subsegment->ranges_count);
                if (!subsegment->ranges) {
                    g_critical("Failed to allocate %"PRIu32" ranges for 'ssix' box.", subsegment->ranges_count);
                    goto fail;
                }
                for (size_t j = 0; j < subsegment->ranges_count; ++j) {
                    ssix_subsegment_range_t* range = &subsegment->ranges[j];
                    range->level = bitreader_read_uint8(b);
                    range->range_size = bitreader_read_uint24(b);
                }
            }
        }
    }
    return (box_t*)box;
fail:
    if (error_out) {
        *error_out = 1;
    }
    free_ssix(box);
    return NULL;
}

void free_emsg(emsg_t* box)
{
    if (box == NULL) {
        return;
    }
    g_free(box->scheme_id_uri);
    g_free(box->value);
    g_free(box->message_data);

    g_slice_free(emsg_t, box);
}

box_t* read_emsg(bitreader_t* b, int* error_out)
{
    g_return_val_if_fail(b, NULL);

    /*
    aligned(8) class DASHEventMessageBox extends FullBox("emsg", version = 0, flags = 0)
    {
        string scheme_id_uri;
        string value;
        unsigned int(32) timescale;
        unsigned int(32) presentation_time_delta;
        unsigned int(32) event_duration;
        unsigned int(32) id;
        unsigned int(8) message_data[];
    }
    */
    emsg_t* box = g_slice_new0(emsg_t);
    if (!read_full_box(b, (fullbox_t*)box)) {
        goto fail;
    }

    size_t length;
    box->scheme_id_uri = bitreader_read_string(b, &length);

    box->value = bitreader_read_string(b, &length);

    box->timescale = bitreader_read_uint32(b);
    box->presentation_time_delta = bitreader_read_uint32(b);
    box->event_duration = bitreader_read_uint32(b);
    box->id = bitreader_read_uint32(b);

    box->message_size = bitreader_bytes_left(b);
    if (box->message_size != 0) {
        box->message_data = g_try_new0(uint8_t, box->message_size);
        if (!box->message_data) {
            g_critical("Failed to allocate %zu bytes for 'emsg' box's message data.", box->message_size);
            goto fail;
        }
        bitreader_read_bytes(b, box->message_data, box->message_size);
    }

    return (box_t*)box;
fail:
    free_emsg(box);
    return NULL;
}

void print_boxes(box_t* const* boxes, size_t num_boxes)
{
    g_return_if_fail(boxes);
    if (tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) {
        return;
    }
    for (size_t i = 0; i < num_boxes; i++) {
        print_box(boxes[i]);
    }
}

void print_box(const box_t* box)
{
    g_return_if_fail(box);

    char tmp[5] = {0};
    uint32_to_string(tmp, box->type);
    g_debug("####### %s ######", tmp);
    g_debug("size = %"PRIu64, box->size);

    switch (box->type) {
    case BOX_TYPE_STYP: {
        print_styp((const styp_t*)box);
        break;
    }
    case BOX_TYPE_SIDX: {
        print_sidx((const sidx_t*)box);
        break;
    }
    case BOX_TYPE_PCRB: {
        print_pcrb((const pcrb_t*)box);
        break;
    }
    case BOX_TYPE_SSIX: {
        print_ssix((const ssix_t*)box);
        break;
    }
    case BOX_TYPE_EMSG: {
        print_emsg((const emsg_t*)box);
        break;
    }
    }
    g_debug("###################\n");
}

void print_fullbox(const fullbox_t* box)
{
    g_return_if_fail(box);

    g_debug("version = %u", box->version);
    g_debug("flags = 0x%04x", box->flags);
}

void print_emsg(const emsg_t* emsg)
{
    g_return_if_fail(emsg);

    print_fullbox((const fullbox_t*)emsg);

    g_debug("scheme_id_uri = %s", emsg->scheme_id_uri);
    g_debug("value = %s", emsg->value);
    g_debug("timescale = %d", emsg->timescale);
    g_debug("presentation_time_delta = %d", emsg->presentation_time_delta);
    g_debug("event_duration = %d", emsg->event_duration);
    g_debug("id = %d", emsg->id);

    g_debug("message_data:");
    GString* line = g_string_new(NULL);
    for (size_t i = 0; i < emsg->message_size; i++) {
        g_string_append_printf(line, "0x%x ", emsg->message_data[i]);
        if (i % 8 == 7) {
            g_debug("%s", line->str);
            g_string_truncate(line, 0);
        }
    }

    if (line->len) {
        g_debug("%s", line->str);
    }
    g_string_free(line, true);
}

void print_styp(const styp_t* styp)
{
    g_return_if_fail(styp);

    char str_tmp[5] = {0};
    uint32_to_string(str_tmp, styp->major_brand);
    g_debug("major_brand = %s", str_tmp);
    g_debug("minor_version = %u", styp->minor_version);
    g_debug("num_compatible_brands = %zu", styp->num_compatible_brands);
    g_debug("compatible_brands:");
    for (size_t i = 0; i < styp->num_compatible_brands; i++) {
        uint32_to_string(str_tmp, styp->compatible_brands[i]);
        g_debug("    %zu: %s", i, str_tmp);
    }
}

void print_sidx(const sidx_t* sidx)
{
    g_return_if_fail(sidx);

    print_fullbox((const fullbox_t*)sidx);

    g_debug("reference_id = 0x%04x", sidx->reference_id);
    g_debug("timescale = %u", sidx->timescale);

    g_debug("earliest_presentation_time = %"PRId64, sidx->earliest_presentation_time);
    g_debug("first_offset = %"PRId64, sidx->first_offset);

    g_debug("reference_count = %u", sidx->reference_count);

    for(size_t i = 0; i < sidx->reference_count; i++) {
        print_sidx_reference(&(sidx->references[i]));
    }
}

void print_sidx_reference(const sidx_reference_t* reference)
{
    g_return_if_fail(reference);

    g_debug("    SidxReference:");

    g_debug("        reference_type = %u", reference->reference_type);
    g_debug("        referenced_size = %u", reference->referenced_size);
    g_debug("        subsegment_duration = %u", reference->subsegment_duration);
    g_debug("        starts_with_sap = %s", BOOL_TO_STR(reference->starts_with_sap));
    g_debug("        sap_type = %u", reference->sap_type);
    g_debug("        sap_delta_time = %u", reference->sap_delta_time);
}

void print_ssix_subsegment(const ssix_subsegment_t* subsegment)
{
    g_return_if_fail(subsegment);

    g_debug("    SsixSubsegment:");

    g_debug("        ranges_count = %u", subsegment->ranges_count);
    for(size_t i = 0; i < subsegment->ranges_count; i++) {
        g_debug("            level = %u, range_size = %u", subsegment->ranges[i].level,
               subsegment->ranges[i].range_size);
    }
}

void print_pcrb(const pcrb_t* pcrb)
{
    g_return_if_fail(pcrb);

    g_debug("subsegment_count = %"PRIu32, pcrb->subsegment_count);
    for (size_t i = 0; i < pcrb->subsegment_count; ++i) {
        g_debug("    pcr = %"PRIu64, pcrb->pcr[i]);
    }
}

void print_ssix(const ssix_t* ssix)
{
    g_return_if_fail(ssix);

    print_fullbox((const fullbox_t*)ssix);

    g_debug("subsegment_count = %u", ssix->subsegment_count);

    for (size_t i = 0; i < ssix->subsegment_count; i++) {
        print_ssix_subsegment(&(ssix->subsegments[i]));
    }
}
