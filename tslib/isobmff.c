/*
** Copyright (C) 2014  Cable Television Laboratories, Inc.
** Contact: http://www.cablelabs.com/

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>


GQuark isobmff_error_quark(void); // Silence -Wmissing-prototypes warning
G_DEFINE_QUARK(ISOBMFF_ERROR, isobmff_error);

static void parse_full_box(GDataInputStream*, fullbox_t*, uint64_t box_size, GError**);
static box_t* parse_styp(GDataInputStream*, uint64_t box_size, GError**);
static box_t* parse_sidx(GDataInputStream*, uint64_t box_size, GError**);
static box_t* parse_pcrb(GDataInputStream*, uint64_t box_size, GError**);
static box_t* parse_ssix(GDataInputStream*, uint64_t box_size, GError**);
static box_t* parse_emsg(GDataInputStream*, uint64_t box_size, GError**);

static void print_fullbox(fullbox_t*);
static void print_styp(data_styp_t*);
static void print_sidx(data_sidx_t*);
static void print_pcrb(data_pcrb_t*);
static void print_ssix(data_ssix_t*);
static void print_emsg(data_emsg_t*);

static void print_sidx_reference(data_sidx_reference_t*);
static void print_ssix_subsegment(data_ssix_subsegment_t*);

static void free_styp(data_styp_t*);
static void free_sidx(data_sidx_t*);
static void free_pcrb(data_pcrb_t*);
static void free_ssix(data_ssix_t*);
static void free_emsg(data_emsg_t*);


void uint32_to_string(char* str, uint32_t num)
{
    str[0] = (num >> 24) & 0xff;
    str[1] = (num >> 16) & 0xff;
    str[2] = (num >>  8) & 0xff;
    str[3] = (num >>  0) & 0xff;
}

static uint32_t read_uint24(GDataInputStream* input, GError** error)
{
    uint32_t value = g_data_input_stream_read_uint16(input, NULL, error);
    if (*error) {
        return 0;
    }
    return (value << 8) + g_data_input_stream_read_byte(input, NULL, error);
}

static uint32_t read_uint48(GDataInputStream* input, GError** error)
{
    uint64_t value = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        return 0;
    }
    return (value << 16) + g_data_input_stream_read_uint16(input, NULL, error);
}

void free_box(box_t* box)
{
    if (box == NULL) {
        return;
    }
    switch (box->type) {
    case BOX_TYPE_STYP: {
        free_styp((data_styp_t*)box);
        break;
    }
    case BOX_TYPE_SIDX: {
        free_sidx((data_sidx_t*)box);
        break;
    }
    case BOX_TYPE_PCRB: {
        free_pcrb((data_pcrb_t*)box);
        break;
    }
    case BOX_TYPE_SSIX: {
        free_ssix((data_ssix_t*)box);
        break;
    }
    case BOX_TYPE_EMSG: {
        free_emsg((data_emsg_t*)box);
        break;
    }
    default:
        g_free(box);
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

int read_boxes_from_file(char* file_name, box_t*** boxes_out, size_t* num_boxes)
{
    int return_code = 0;
    GFile* file = g_file_new_for_path(file_name);
    GError* error = NULL;
    GFileInputStream* file_input = g_file_read(file, NULL, &error);
    GDataInputStream* input = g_data_input_stream_new(G_INPUT_STREAM(file_input));
    if (error != NULL) {
        g_critical("Error validating index segment, unable to read file %s. Error is: %s.", file_name, error->message);
        g_error_free(error);
        goto fail;
    }

    return_code = read_boxes_from_stream(input, boxes_out, num_boxes);
cleanup:
    g_object_unref(input);
    g_object_unref(file_input);
    g_object_unref(file);
    return return_code;
fail:
    return_code = -1;
    goto cleanup;
}

int read_boxes_from_stream(GDataInputStream* input, box_t*** boxes_out, size_t* num_boxes)
{
    int return_code = 0;
    GPtrArray* boxes = g_ptr_array_new();
    while (true) {
        GError* error = NULL;
        box_t* box = parse_box(input, &error);
        if (error) {
            g_critical("Failed to read box: %s.", error->message);
            g_error_free(error);
            goto fail;
        } else if (box == NULL) {
            break;
        }
        g_ptr_array_add(boxes, box);
    }
    *num_boxes = boxes->len;
    *boxes_out = (box_t**)g_ptr_array_free(boxes, false);
cleanup:
    return return_code;
fail:
    return_code = -1;
    g_ptr_array_set_free_func(boxes, (GDestroyNotify)free_box);
    g_ptr_array_free(boxes, true);
    goto cleanup;
}

box_t* parse_box(GDataInputStream* input, GError** error)
{
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
    uint64_t size = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        /* No more data */
        g_error_free(*error);
        *error = NULL;
        return NULL;
    }

    if (size == 1) {
        size = g_data_input_stream_read_uint64(input, NULL, error);
        if (*error) {
            goto fail;
        }
    }

    uint32_t type = g_data_input_stream_read_uint32(input, NULL, error);
    /* TODO: Handle usertype */
    if (*error) {
        goto fail;
    }

    /* Ignore size for Box itself */
    uint64_t inner_size = size - 8;

    switch (type) {
    case BOX_TYPE_STYP:
        box = parse_styp(input, inner_size, error);
        break;
    case BOX_TYPE_SIDX:
        box = parse_sidx(input, inner_size, error);
        break;
    case BOX_TYPE_PCRB:
        box = parse_pcrb(input, inner_size, error);
        break;
    case BOX_TYPE_SSIX:
        box = parse_ssix(input, inner_size, error);
        break;
    case BOX_TYPE_EMSG:
        box = parse_emsg(input, inner_size, error);
        break;
    default: {
        char tmp[5] = {0};
        uint32_to_string(tmp, type);
        g_warning("Unknown box type: %s.", tmp);
        g_input_stream_skip(G_INPUT_STREAM(input), size, NULL, error);
        if (*error) {
            goto fail;
        }
        box = g_new(box_t, 1);
    }
    }
    if (box) {
        box->type = type;
        box->size = size;
    }
    if (*error) {
        goto fail;
    }

    return box;
fail:
    free_box(box);
    return NULL;
}

void parse_full_box(GDataInputStream* input, fullbox_t* box, uint64_t box_size, GError** error)
{
    /*
    aligned(8) class FullBox(unsigned int(32) boxtype, unsigned int(8) v, bit(24) f) extends Box(boxtype) {
        unsigned int(8) version = v;
        bit(24) flags = f;
    }
    */
    if (box_size < 4) {
        *error = g_error_new(isobmff_error_quark(), ISOBMFF_ERROR_BAD_BOX_SIZE, "FullBox is 4 bytes, but this box size only has %zu bytes remaining.", box_size);
        return;
    }
    box->version = g_data_input_stream_read_byte(input, NULL, error);
    if (*error) {
        return;
    }

    box->flags = read_uint24(input, error);
}

void free_styp(data_styp_t* box)
{
    g_free(box->compatible_brands);
    g_free(box);
}

box_t* parse_styp(GDataInputStream* input, uint64_t box_size, GError** error)
{
    /*
    "A segment type has the same format as an 'ftyp' box [4.3], except that it takes the box type 'styp'."
    aligned(8) class FileTypeBox extends Box(‘ftyp’) {
        unsigned int(32) major_brand;
        unsigned int(32) minor_version;
        unsigned int(32) compatible_brands[];
    }
    */
    data_styp_t* box = g_new0(data_styp_t, 1);

    box->major_brand = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box->minor_version = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }

    box->num_compatible_brands = (box_size - 8) / 4;
    if (box->num_compatible_brands != 0) {
        box->compatible_brands = g_new(uint32_t, box->num_compatible_brands);
        for(size_t i = 0; i < box->num_compatible_brands; ++i) {
            box->compatible_brands[i] = g_data_input_stream_read_uint32(input, NULL, error);
            if (*error) {
                goto fail;
            }
        }
    }
    return (box_t*)box;
fail:
    free_styp(box);
    return NULL;
}

void free_sidx(data_sidx_t* box)
{
    g_free(box->references);
    g_free(box);
}

box_t* parse_sidx(GDataInputStream* input, uint64_t box_size, GError** error)
{
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
    data_sidx_t* box = g_new0(data_sidx_t, 1);
    parse_full_box(input, (fullbox_t*)box, box_size, error);
    if (*error) {
        goto fail;
    }
    box_size -= 4;

    box->reference_id = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box->timescale = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }

    if (box->version == 0) {
        box->earliest_presentation_time = g_data_input_stream_read_uint32(input, NULL, error);
        if (*error) {
            goto fail;
        }
        box->first_offset = g_data_input_stream_read_uint32(input, NULL, error);
        if (*error) {
            goto fail;
        }
    } else {
        box->earliest_presentation_time = g_data_input_stream_read_uint64(input, NULL, error);
        if (*error) {
            goto fail;
        }
        box->first_offset = g_data_input_stream_read_uint64(input, NULL, error);
        if (*error) {
            goto fail;
        }
    }

    box->reserved = g_data_input_stream_read_uint16(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box->reference_count = g_data_input_stream_read_uint16(input, NULL, error);
    if (*error) {
        goto fail;
    }

    box->references = g_new(data_sidx_reference_t, box->reference_count);
    for (size_t i = 0; i < box->reference_count; ++i) {
        data_sidx_reference_t* reference = &box->references[i];
        uint32_t tmp = g_data_input_stream_read_uint32(input, NULL, error);
        if (*error) {
            goto fail;
        }
        reference->reference_type = tmp >> 31;
        reference->referenced_size = tmp & 0x7fffffff;
        reference->subsegment_duration = g_data_input_stream_read_uint32(input, NULL, error);
        if (*error) {
            goto fail;
        }
        tmp = g_data_input_stream_read_uint32(input, NULL, error);
        if (*error) {
            goto fail;
        }
        reference->starts_with_sap = tmp >> 31;
        reference->sap_type = (tmp >> 28) & 0x7;
        reference->sap_delta_time = tmp & 0x0fffffff;
    }
    return (box_t*)box;
fail:
    free_sidx(box);
    return NULL;
}

void free_pcrb(data_pcrb_t* box)
{
    g_free(box->pcr);
    g_free(box);
}

box_t* parse_pcrb(GDataInputStream* input, uint64_t box_size, GError** error)
{
    /* 6.4.7.2 MPEG-2 TS PCR information box
    aligned(8) class MPEG2TSPCRInfoBox extends Box(‘pcrb’, 0) {
        unsigned int(32) subsegment_count;
        for (i = 1; i <= subsegment_count; i++) {
            unsigned int(42) pcr;
            unsigned int(6) pad = 0;
        }
    }
    */
    data_pcrb_t* box = g_new0(data_pcrb_t, 1);

    box->subsegment_count = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box_size -= 4;

    uint64_t pcr_size = box->subsegment_count * (48 / 8);
    if (pcr_size != box_size) {
        *error = g_error_new(isobmff_error_quark(), ISOBMFF_ERROR_BAD_BOX_SIZE,
                "pcrb box has subsegment_count %"PRIu32", indicating the remaining size should be %zu bytes, but the box has %zu bytes left.",
                box->subsegment_count, pcr_size, box_size);
        if (box_size == box->subsegment_count * 8) {
            g_critical("Note: Your encoder appears to be writing 64-bit pcrb entries instead of 48-bit. See https://github.com/gpac/gpac/issues/34 for details.");
        }
        goto fail;
    }
    box->pcr = g_new(uint64_t, box->subsegment_count);
    for (size_t i = 0; i < box->subsegment_count; ++i) {
        box->pcr[i] = read_uint48(input, error) >> 6;
        if (*error) {
            goto fail;
        }
    }

    return (box_t*)box;
fail:
    free_pcrb(box);
    return NULL;
}

void free_ssix(data_ssix_t* box)
{
    for (size_t i = 0; i < box->subsegment_count; i++) {
        g_free(box->subsegments[i].ranges);
    }
    g_free(box->subsegments);
    g_free(box);
}

box_t* parse_ssix(GDataInputStream* input, uint64_t box_size, GError** error)
{
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
    data_ssix_t* box = g_new0(data_ssix_t, 1);
    parse_full_box(input, (fullbox_t*)box, box_size, error);
    if (*error) {
        goto fail;
    }
    box_size -= 4;

    box->subsegment_count = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box->subsegments = g_new0(data_ssix_subsegment_t, box->subsegment_count);

    for (size_t i = 0; i < box->subsegment_count; i++) {
        data_ssix_subsegment_t* subsegment = &box->subsegments[i];
        subsegment->ranges_count = g_data_input_stream_read_uint32(input, NULL, error);
        if (*error) {
            goto fail;
        }
        subsegment->ranges = g_new0(data_ssix_subsegment_range_t, subsegment->ranges_count);
        for (size_t j = 0; j < subsegment->ranges_count; ++j) {
            data_ssix_subsegment_range_t* range = &subsegment->ranges[j];
            range->level = g_data_input_stream_read_byte(input, NULL, error);
            if (*error) {
                goto fail;
            }
            range->range_size = read_uint24(input, error);
            if (*error) {
                goto fail;
            }
        }
    }
    return (box_t*)box;
fail:
    free_ssix(box);
    return NULL;
}

void free_emsg(data_emsg_t* box)
{
    g_free(box->scheme_id_uri);
    g_free(box->value);
    g_free(box->message_data);

    g_free(box);
}

box_t* parse_emsg(GDataInputStream* input, uint64_t box_size, GError** error)
{
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
    data_emsg_t* box = g_new0(data_emsg_t, 1);
    parse_full_box(input, (fullbox_t*)box, box_size, error);
    if (*error) {
        goto fail;
    }
    box_size -= 4;

    gsize length;
    box->scheme_id_uri = g_data_input_stream_read_upto(input, "\0", 1, &length, NULL, error);
    if (*error) {
        goto fail;
    }
    /* Skip NUL terminator */
    g_data_input_stream_read_byte(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box_size -= length + 1;

    box->value = g_data_input_stream_read_upto(input, "\0", 1, &length, NULL, error);
    if (*error) {
        goto fail;
    }
    /* Skip NUL terminator */
    g_data_input_stream_read_byte(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box_size -= length + 1;

    box->timescale = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box->presentation_time_delta = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box->event_duration = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }
    box->id = g_data_input_stream_read_uint32(input, NULL, error);
    if (*error) {
        goto fail;
    }

    box->message_data_size = box_size - 17;
    if (box->message_data_size != 0) {
        box->message_data = g_new0(uint8_t, box->message_data_size);
        g_input_stream_read(G_INPUT_STREAM(input), box->message_data, box->message_data_size, NULL, error);
        if (*error) {
            goto fail;
        }
    }

    return (box_t*)box;
fail:
    free_emsg(box);
    return NULL;
}

void print_boxes(box_t** boxes, size_t num_boxes)
{
    for (size_t i = 0; i < num_boxes; i++) {
        print_box(boxes[i]);
    }
}

void print_box(box_t* box)
{
    char tmp[5] = {0};
    uint32_to_string(tmp, box->type);
    g_debug("####### %s ######", tmp);
    g_debug("size = %"PRIu64, box->size);

    switch (box->type) {
    case BOX_TYPE_STYP: {
        print_styp((data_styp_t*)box);
        break;
    }
    case BOX_TYPE_SIDX: {
        print_sidx((data_sidx_t*)box);
        break;
    }
    case BOX_TYPE_PCRB: {
        print_pcrb((data_pcrb_t*)box);
        break;
    }
    case BOX_TYPE_SSIX: {
        print_ssix((data_ssix_t*)box);
        break;
    }
    case BOX_TYPE_EMSG: {
        print_emsg((data_emsg_t*)box);
        break;
    }
    }
    g_debug("###################\n");
}

void print_fullbox(fullbox_t* box)
{
    g_debug("version = %u", box->version);
    g_debug("flags = 0x%04x", box->flags);
}

void print_emsg(data_emsg_t* emsg)
{
    print_fullbox((fullbox_t*)emsg);

    g_debug("scheme_id_uri = %s", emsg->scheme_id_uri);
    g_debug("value = %s", emsg->value);
    g_debug("timescale = %d", emsg->timescale);
    g_debug("presentation_time_delta = %d", emsg->presentation_time_delta);
    g_debug("event_duration = %d", emsg->event_duration);
    g_debug("id = %d", emsg->id);

    g_debug("message_data:");
    GString* line = g_string_new(NULL);
    for (size_t i = 0; i < emsg->message_data_size; i++) {
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

void print_styp(data_styp_t* styp)
{
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

void print_sidx(data_sidx_t* sidx)
{
    print_fullbox((fullbox_t*)sidx);

    g_debug("reference_id = 0x%04x", sidx->reference_id);
    g_debug("timescale = %u", sidx->timescale);

    g_debug("earliest_presentation_time = %"PRId64, sidx->earliest_presentation_time);
    g_debug("first_offset = %"PRId64, sidx->first_offset);

    g_debug("reserved = %u", sidx->reserved);
    g_debug("reference_count = %u", sidx->reference_count);

    for(size_t i = 0; i < sidx->reference_count; i++) {
        print_sidx_reference(&(sidx->references[i]));
    }
}

void print_sidx_reference(data_sidx_reference_t* reference)
{
    g_debug("    SidxReference:");

    g_debug("        reference_type = %u", reference->reference_type);
    g_debug("        referenced_size = %u", reference->referenced_size);
    g_debug("        subsegment_duration = %u", reference->subsegment_duration);
    g_debug("        starts_with_sap = %u", reference->starts_with_sap);
    g_debug("        sap_type = %u", reference->sap_type);
    g_debug("        sap_delta_time = %u", reference->sap_delta_time);
}

void print_ssix_subsegment(data_ssix_subsegment_t* subsegment)
{
    g_debug("    SsixSubsegment:");

    g_debug("        ranges_count = %u", subsegment->ranges_count);
    for(size_t i = 0; i < subsegment->ranges_count; i++) {
        g_debug("            level = %u, range_size = %u", subsegment->ranges[i].level,
               subsegment->ranges[i].range_size);
    }
}

void print_pcrb(data_pcrb_t* pcrb)
{
    g_debug("subsegment_count = %"PRIu32, pcrb->subsegment_count);
    for (size_t i = 0; i < pcrb->subsegment_count; ++i) {
        g_debug("    pcr = %"PRIu64, pcrb->pcr[i]);
    }
}

void print_ssix(data_ssix_t* ssix)
{
    print_fullbox((fullbox_t*)ssix);

    g_debug("subsegment_count = %u", ssix->subsegment_count);

    for (size_t i = 0; i < ssix->subsegment_count; i++) {
        print_ssix_subsegment(&(ssix->subsegments[i]));
    }
}
