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
#include "ISOBMFF.h"

#include <stdlib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>


G_DEFINE_QUARK(ISOBMFF_ERROR, isobmff_error);

int read_boxes_from_file(char* file_name, box_t*** boxes_out, size_t* num_boxes_out);
int read_boxes_from_stream(GDataInputStream*, box_t*** boxes_out, size_t* num_boxes_out);

box_t* parse_box(GDataInputStream*, GError**);
void parse_full_box(GDataInputStream*, fullbox_t*, uint64_t box_size, GError**);
box_t* parse_styp(GDataInputStream*, uint64_t box_size, GError**);
box_t* parse_sidx(GDataInputStream*, uint64_t box_size, GError**);
box_t* parse_pcrb(GDataInputStream*, uint64_t box_size, GError**);
box_t* parse_ssix(GDataInputStream*, uint64_t box_size, GError**);
box_t* parse_emsg(GDataInputStream*, uint64_t box_size, GError**);

void print_boxes(box_t** boxes, size_t num_boxes);
void print_box(box_t*);

void print_fullbox(fullbox_t*);
void print_styp(data_styp_t*);
void print_sidx(data_sidx_t*);
void print_pcrb(data_pcrb_t*);
void print_ssix(data_ssix_t*);
void print_emsg(data_emsg_t*);

void print_sidx_reference(data_sidx_reference_t*);
void print_ssix_subsegment(data_ssix_subsegment_t*);

void free_boxes(box_t** boxes, size_t num_boxes);
void free_styp(data_styp_t*);
void free_sidx(data_sidx_t*);
void free_pcrb(data_pcrb_t*);
void free_ssix(data_ssix_t*);
void free_emsg(data_emsg_t*);

static void uint32_to_string(char* str, uint32_t num)
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

int validate_representation_index_segment_boxes(size_t num_segments,
        box_t** boxes, size_t num_boxes, uint64_t* segment_durations,
        data_segment_iframes_t* iframes, int presentation_time_offset,
        int video_pid, bool is_simple_profile)
{
    /*
    A Representation Index Segment indexes all Media Segments of one Representation and is defined as follows:

    -- Each Representation Index Segment shall begin with an "styp" box, and the brand "risx" shall be
    present in the "styp" box. The conformance requirement of the brand "risx" is defined by this subclause.

    -- Each Media Segment is indexed by one or more Segment Index box(es); the boxes for a
    given Media Segment are contiguous;

    -- Each Segment Index box may be followed by an 'ssix' and/or 'pcrb' box;

    -- The Segment Index for each Media Segments is concatenated in order, preceded by a
    single Segment Index box that indexes the Index Segment. This initial Segment Index box shall
    have one entry in its loop for each Media Segment, and each entry refers to the Segment
    Index information for a single Media Segment.
    */

    int return_code = 0;

    int box_index = 0;
    if (num_boxes == 0) {
        g_critical("ERROR validating Representation Index Segment: no boxes in segment.");
        goto fail;
    }

    // first box must be a styp
    if (boxes[box_index]->type != BOX_TYPE_STYP) {
        g_critical("ERROR validating Representation Index Segment: first box not a styp.");
        goto fail;
    }

    // check brand
    data_styp_t* styp = (data_styp_t*)boxes[box_index];
    bool found_risx = false;
    bool found_ssss = false;
    for(size_t i = 0; i < styp->num_compatible_brands; ++i) {
        uint32_t brand = styp->compatible_brands[i];
        if (brand == BRAND_RISX) {
            found_risx = true;
        } else if (brand == BRAND_SSSS) {
            found_ssss = true;
        }
        if (found_risx && found_ssss) {
            break;
        }
    }
    if (!found_risx) {
        g_critical("ERROR validating Representation Index Segment: styp compatible brands does not contain \"risx\".");
        g_info("Brands found are:");
        g_info("styp major brand = %x", styp->major_brand);
        for (size_t i = 0; i < styp->num_compatible_brands; ++i) {
            g_info("styp compatible brand = %x", styp->compatible_brands[i]);
        }
        goto fail;
    }

    box_index++;

    // second box must be a sidx that references other sidx boxes
    if (boxes[box_index]->type != BOX_TYPE_SIDX) {
        g_critical("ERROR validating Representation Index Segment: second box not a sidx.");
        goto fail;
    }

    // walk all references: they should all be of type 1 and should point to sidx boxes
    data_sidx_t* master_sidx = (data_sidx_t*)boxes[box_index];
    unsigned int master_reference_id = master_sidx->reference_id;
    if (master_reference_id != video_pid) {
        g_critical("ERROR validating Representation Index Segment: master ref ID does not equal \
video PID.  Expected %d, actual %d.", video_pid, master_reference_id);
        return_code = -1;
    }
    for (size_t i = 0; i < master_sidx->reference_count; i++) {
        data_sidx_reference_t ref = master_sidx->references[i];
        if (ref.reference_type != 1) {
            g_critical("ERROR validating Representation Index Segment: reference type not 1.");
            goto fail;
        }

        // validate duration
        if (segment_durations[i] != ref.subsegment_duration) {
            g_critical("ERROR validating Representation Index Segment: master ref segment duration does not equal \
segment duration.  Expected %"PRIu64", actual %d.", segment_durations[i], ref.subsegment_duration);
            return_code = -1;
        }
    }
    box_index++;

    int segment_index = -1;
    bool ssix_present = false;
    bool pcrb_present = false;
    int num_nested_sidx = 0;
    uint64_t referenced_size = 0;
    uint64_t segment_start_time = presentation_time_offset;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    for (; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        switch(box->type) {
        case BOX_TYPE_SIDX: {
            ssix_present = false;
            pcrb_present = false;

            data_sidx_t* sidx = (data_sidx_t*)box;
            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                // GORP: check earliest presentation time
            } else {
                // check size:
                g_info("Validating referenced_size for reference %d.", segment_index);
                if (segment_index >= 0 && referenced_size != master_sidx->references[segment_index].referenced_size) {
                    g_critical("ERROR validating Representation Index Segment: referenced_size for reference %d. \
Expected %"PRIu32", actual %"PRIu64"\n", segment_index, master_sidx->references[segment_index].referenced_size,
                                   referenced_size);
                    return_code = -1;
                }

                referenced_size = 0;
                segment_index++;
                if (segment_index > 0) {
                    segment_start_time += segment_durations[segment_index - 1];
                }

                g_info("Validating earliest_presentation_time for reference %d.", segment_index);
                if (segment_start_time != sidx->earliest_presentation_time) {
                    g_critical("ERROR validating Representation Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64".", segment_start_time, sidx->earliest_presentation_time);
                    return_code = -1;
                }
            }
            referenced_size += sidx->size;

            g_info("Validating reference_id");
            if (master_reference_id != sidx->reference_id) {
                g_critical("ERROR validating Representation Index Segment: invalid reference id in sidx box. \
Expected %d, actual %d.", master_reference_id, sidx->reference_id);
                return_code = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if (analyze_sidx_references(sidx, &(iframes[segment_index].num_iframes), &num_nested_sidx,
                                     is_simple_profile) != 0) {
                return_code = -1;
            }
            break;
        }
        case BOX_TYPE_SSIX: {
            data_ssix_t* ssix = (data_ssix_t*)box;
            referenced_size += ssix->size;
            g_info("Validating ssix box");
            if (ssix_present) {
                g_critical("ERROR validating Representation Index Segment: More than one ssix box following sidx box.");
                return_code = -1;
            } else {
                ssix_present = true;
            }
            if (pcrb_present) {
                g_critical("ERROR validating Representation Index Segment: pcrb occurred before ssix. 6.4.6.4 says "
                        "\"The Subsegment Index box (‘ssix’) [...] shall follow immediately after the ‘sidx’ box that "
                        "documents the same Subsegment. [...] If the 'pcrb' box is present, it shall follow 'ssix'.\".");
                return_code = -1;
            }
            if (!found_ssss) {
                g_critical("ERROR validating Representation Index Segment: Saw ssix box, but 'ssss' is not in compatible brands. See 6.4.6.4.");
                return_code = -1;
            }
            break;
        }
        case BOX_TYPE_PCRB: {
            data_pcrb_t* pcrb = (data_pcrb_t*)box;
            referenced_size += pcrb->size;
            g_info("Validating pcrb box");
            if (pcrb_present) {
                g_critical("ERROR validating Representation Index Segment: More than one pcrb box following sidx box.");
                return_code = -1;
            } else {
                pcrb_present = true;
            }
            break;
        }
        default:
            g_critical("Invalid box type: %x.", box->type);
            break;
        }
    }

    // check the last reference size -- the last one is not checked in the above loop
    g_info("Validating referenced_size for reference %d. Expected %"PRIu32", actual %"PRIu64".",
           segment_index, master_sidx->references[segment_index].referenced_size, referenced_size);
    if (segment_index >= 0 && referenced_size != master_sidx->references[segment_index].referenced_size) {
        g_critical("ERROR validating Representation Index Segment: referenced_size for reference %d. \
Expected %"PRIu32", actual %"PRIu64".", segment_index, master_sidx->references[segment_index].referenced_size,
                       referenced_size);
        return_code = -1;
    }

    if (num_nested_sidx != 0) {
        g_critical("ERROR validating Representation Index Segment: Incorrect number of nested sidx boxes: %d.",
                num_nested_sidx);
        return_code = -1;
    }

    if ((segment_index + 1) != num_segments) {
        g_critical("ERROR validating Representation Index Segment: Invalid number of segment sidx boxes following master sidx box: \
expected %zu, found %d.", num_segments, segment_index);
        return_code = -1;
    }

    // fill in iFrame locations by walking the list of sidx's again, starting from the third box
    num_nested_sidx = 0;
    segment_index = -1;
    int iframe_counter = 0;
    unsigned int lastIFrameDuration = 0;
    uint64_t nextIFrameByteLocation = 0;
    segment_start_time = presentation_time_offset;
    for (box_index = 2; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        if (box->type == BOX_TYPE_SIDX) {
            data_sidx_t* sidx = (data_sidx_t*)box;

            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                nextIFrameByteLocation += sidx->first_offset;  // convert from 64-bit t0 32 bit
            } else {
                segment_index++;
                if (segment_index > 0) {
                    segment_start_time += segment_durations[segment_index - 1];
                }

                iframe_counter = 0;
                nextIFrameByteLocation = sidx->first_offset;
                if (segment_index < num_segments) {
                    iframes[segment_index].do_iframe_validation = 1;
                    iframes[segment_index].iframe_locations_time = calloc(
                                iframes[segment_index].num_iframes, sizeof(unsigned int));
                    iframes[segment_index].iframe_locations_byte = calloc(iframes[segment_index].num_iframes,
                            sizeof(uint64_t));
                    iframes[segment_index].starts_with_sap = calloc(iframes[segment_index].num_iframes,
                                                            sizeof(unsigned char));
                    iframes[segment_index].sap_type = calloc(iframes[segment_index].num_iframes,
                                                      sizeof(unsigned char));
                }
            }

            // fill in Iframe locations here
            for (int i = 0; i < sidx->reference_count; i++) {
                data_sidx_reference_t ref = sidx->references[i];
                if (ref.reference_type == 0) {
                    (iframes[segment_index]).starts_with_sap[iframe_counter] = ref.starts_with_sap;
                    (iframes[segment_index]).sap_type[iframe_counter] = ref.sap_type;
                    (iframes[segment_index]).iframe_locations_byte[iframe_counter] = nextIFrameByteLocation;


                    if (iframe_counter == 0) {
                        (iframes[segment_index]).iframe_locations_time[iframe_counter] = segment_start_time + ref.sap_delta_time;
                    } else {
                        (iframes[segment_index]).iframe_locations_time[iframe_counter] =
                            (iframes[segment_index]).iframe_locations_time[iframe_counter - 1] + lastIFrameDuration +
                            ref.sap_delta_time;
                    }
                    iframe_counter++;
                    lastIFrameDuration = ref.subsegment_duration;
                    nextIFrameByteLocation += ref.referenced_size;
                } else {
                    num_nested_sidx++;
                }
            }
        }
    }

cleanup:
    return return_code;
fail:
    return_code = -1;
    goto cleanup;
}

int validate_single_index_segment_boxes(box_t** boxes, size_t num_boxes,
        uint64_t segment_duration, data_segment_iframes_t* iframes,
        int presentation_time_offset, int video_pid, bool is_simple_profile)
{
    /*
     A Single Index Segment indexes exactly one Media Segment and is defined as follows:

     -- Each Single Index Segment shall begin with a "styp" box, and the brand "sisx"
     shall be present in the "styp" box. The conformance requirement of the brand "sisx" is defined in this subclause.

     -- Each Single Index Segment shall contain one or more 'sidx' boxes which index one Media Segment.

     -- A Single Index Segment may contain one or multiple "ssix" boxes. If present, the "ssix"
     shall follow the "sidx" box that documents the same Subsegment without any other "sidx" preceding the "ssix".

     -- A Single Index Segment may contain one or multiple "pcrb" boxes as defined in 6.4.7.2.
     If present, "pcrb" shall follow the "sidx" box that documents the same Subsegments, i.e. a "pcrb"
     box provides PCR information for every subsegment indexed in the last "sidx" box.
    */
    int return_code = 0;

    int box_index = 0;
    if (num_boxes == 0) {
        g_critical("ERROR validating Single Index Segment: no boxes in segment.");
        return_code = -1;
    }

    // first box must be a styp
    if (boxes[box_index]->type != BOX_TYPE_STYP) {
        g_critical("ERROR validating Single Index Segment: first box not a styp.");
        return_code = -1;
    }

    // check brand
    data_styp_t* styp = (data_styp_t*)boxes[box_index];
    if (styp->major_brand != BRAND_SISX) {
        g_info("styp brand = %x", styp->major_brand);
        g_critical("ERROR validating Single Index Segment: styp brand not risx.");
        return_code = -1;
    }

    box_index++;

    int ssix_present = 0;
    int pcrb_present = 0;
    int num_nested_sidx = 0;
    unsigned int referenced_size = 0;

    uint64_t segment_start_time = presentation_time_offset;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    for (; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        switch (box->type) {
        case BOX_TYPE_SIDX: {
            ssix_present = 0;
            pcrb_present = 0;

            data_sidx_t* sidx = (data_sidx_t*)box;
            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                // GORP: check earliest presentation time
            } else {
                referenced_size = 0;

                g_info("Validating earliest_presentation_time");
                if (segment_start_time != sidx->earliest_presentation_time) {
                    g_critical("ERROR validating Single Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64".", segment_start_time, sidx->earliest_presentation_time);
                    return_code = -1;
                }
            }
            referenced_size += sidx->size;

            g_info("Validating reference_id");
            if (video_pid != sidx->reference_id) {
                g_critical("ERROR validating Single Index Segment: invalid reference id in sidx box. \
Expected %d, actual %d.", video_pid, sidx->reference_id);
                return_code = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if (analyze_sidx_references(sidx, &(iframes->num_iframes), &num_nested_sidx, is_simple_profile) != 0) {
                return_code = -1;
            }
            break;
        }
        case BOX_TYPE_SSIX: {
            data_ssix_t* ssix = (data_ssix_t*)box;
            referenced_size += ssix->size;
            g_info("Validating ssix box");
            if (ssix_present) {
                g_critical("ERROR validating Single Index Segment: More than one ssix box following sidx box.");
                return_code = -1;
            } else {
                ssix_present = 1;
            }
            break;
        }
        case BOX_TYPE_PCRB: {
            data_pcrb_t* pcrb = (data_pcrb_t*)box;
            referenced_size += pcrb->size;
            g_info("Validating pcrb box");
            if (pcrb_present) {
                g_critical("ERROR validating Single Index Segment: More than one pcrb box following sidx box.");
                return_code = -1;
            } else {
                pcrb_present = 1;
            }
            break;
        }
        default:
            g_debug("Ignoring box: %x", box->type);
            break;
        }
    }

    if (num_nested_sidx != 0) {
        g_critical("ERROR validating Single Index Segment: Incorrect number of nested sidx boxes: %d.",
                       num_nested_sidx);
        return_code = -1;
    }

    // fill in iFrame locations by walking the list of sidx's again, startng from box 1

    iframes->do_iframe_validation = 1;
    iframes->iframe_locations_time = (unsigned int*)calloc(iframes->num_iframes, sizeof(unsigned int));
    iframes->iframe_locations_byte = (uint64_t*)calloc(iframes->num_iframes, sizeof(uint64_t));
    iframes->starts_with_sap = (unsigned char*)calloc(iframes->num_iframes, sizeof(unsigned char));
    iframes->sap_type = (unsigned char*)calloc(iframes->num_iframes, sizeof(unsigned char));

    num_nested_sidx = 0;
    int iframe_counter = 0;
    unsigned int lastIFrameDuration = 0;
    uint64_t nextIFrameByteLocation = 0;
    segment_start_time = presentation_time_offset;
    for (box_index = 1; box_index < num_boxes; ++box_index) {
        if (boxes[box_index]->type != BOX_TYPE_SIDX) {
                continue;
        }
        data_sidx_t* sidx = (data_sidx_t*)boxes[box_index];

        if (num_nested_sidx > 0) {
            num_nested_sidx--;
            nextIFrameByteLocation += sidx->first_offset;  // convert from 64-bit t0 32 bit
        }

        // fill in Iframe locations here
        for (size_t i = 0; i < sidx->reference_count; i++) {
            data_sidx_reference_t ref = sidx->references[i];
            if (ref.reference_type == 0) {
                iframes->starts_with_sap[iframe_counter] = ref.starts_with_sap;
                iframes->sap_type[iframe_counter] = ref.sap_type;
                iframes->iframe_locations_byte[iframe_counter] = nextIFrameByteLocation;

                if (iframe_counter == 0) {
                    iframes->iframe_locations_time[iframe_counter] = segment_start_time + ref.sap_delta_time;
                } else {
                    iframes->iframe_locations_time[iframe_counter] =
                        iframes->iframe_locations_time[iframe_counter - 1] + lastIFrameDuration + ref.sap_delta_time;
                }
                iframe_counter++;
                lastIFrameDuration = ref.subsegment_duration;
                nextIFrameByteLocation += ref.referenced_size;
            } else {
                num_nested_sidx++;
            }
        }
    }

    return return_code;
}

int analyze_sidx_references(data_sidx_t* sidx, int* pnum_iframes, int* pnum_nested_sidx,
                          bool is_simple_profile)
{
    int originalnum_nested_sidx = *pnum_nested_sidx;
    int originalnum_iframes = *pnum_iframes;

    for(int i = 0; i < sidx->reference_count; i++) {
        data_sidx_reference_t ref = sidx->references[i];
        if (ref.reference_type == 1) {
            (*pnum_nested_sidx)++;
        } else {
            (*pnum_iframes)++;
        }
    }

    if (is_simple_profile) {
        if (originalnum_nested_sidx != *pnum_nested_sidx && originalnum_iframes != *pnum_iframes) {
            // failure -- references contain references to both media and nested sidx boxes
            g_critical("ERROR validating Representation Index Segment: Section 8.7.3: Simple profile requires that \
sidx boxes have either media references or sidx references, but not both.");
            return -1;
        }
    }

    return 0;
}

int validate_index_segment(char* file_name, size_t num_segments, uint64_t* segment_durations,
                         data_segment_iframes_t* iframes,
                         int presentation_time_offset, int video_pid, bool is_simple_profile)
{
    g_debug("validate_index_segment: %s", file_name);
    size_t num_boxes = 0;
    box_t** boxes = NULL;

    int return_code = read_boxes_from_file(file_name, &boxes, &num_boxes);
    if (return_code != 0) {
        g_critical("ERROR validating Index Segment: Error reading boxes from file.");
        goto fail;
    }

    print_boxes(boxes, num_boxes);

    if (num_segments <= 0) {
        g_critical("ERROR validating Index Segment: Invalid number of segments.");
        goto fail;
    } else if (num_segments == 1) {
        return_code = validate_single_index_segment_boxes(boxes, num_boxes,
                segment_durations[0], iframes, presentation_time_offset,
                video_pid, is_simple_profile);
    } else {
        return_code = validate_representation_index_segment_boxes(num_segments,
                boxes, num_boxes, segment_durations, iframes,
                presentation_time_offset, video_pid, is_simple_profile);
    }
    g_info(" ");

    /* What is the purpose of this? */
    for(size_t i = 0; i < num_segments; i++) {
        g_info("data_segment_iframes %zu: do_iframe_validation = %d, num_iframes = %d",
               i, iframes[i].do_iframe_validation, iframes[i].num_iframes);
        for(int j = 0; j < iframes[i].num_iframes; j++) {
            g_info("   iframe_locations_time[%d] = %d, \tiframe_locations_byte[%d] = %"PRId64, j,
                   iframes[i].iframe_locations_time[j], j, iframes[i].iframe_locations_byte[j]);
        }
    }
    g_info(" ");

cleanup:
    free_boxes(boxes, num_boxes);
    return return_code;
fail:
    return_code = -1;
    goto cleanup;
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
    uint32_t inner_size = size - 8;

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
    box->subsegments = g_new(data_ssix_subsegment_t, box->subsegment_count);

    for (size_t i = 0; i < box->subsegment_count; i++) {
        data_ssix_subsegment_t* subsegment = &box->subsegments[i];
        subsegment->ranges_count = g_data_input_stream_read_uint32(input, NULL, error);
        if (*error) {
            goto fail;
        }
        subsegment->ranges = g_new(data_ssix_subsegment_range_t, subsegment->ranges_count);
        for (size_t j = 0; j < subsegment->ranges_count; ++j) {
            data_ssix_subsegment_range_t* range = &subsegment->ranges[i];
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

    for(int i = 0; i < ssix->subsegment_count; i++) {
        print_ssix_subsegment(&(ssix->subsegments[i]));
    }
}

int validate_emsg_msg(uint8_t* buffer, size_t len, unsigned segment_duration)
{
    g_debug("validate_emsg_msg");

    box_t** boxes = NULL;
    size_t num_boxes;

    GDataInputStream* input = g_data_input_stream_new(g_memory_input_stream_new_from_data(buffer, len, NULL));
    int return_code = read_boxes_from_stream(input, &boxes, &num_boxes);
    if (return_code != 0) {
        goto fail;
    }

    print_boxes(boxes, num_boxes);

    for (size_t i = 0; i < num_boxes; i++) {
        data_emsg_t* box = (data_emsg_t*)boxes[i];
        if (box->type != BOX_TYPE_EMSG) {
            g_critical("ERROR validating EMSG: Invalid box type found.");
            goto fail;
        }

        // GORP: anything else to verify here??

        if (box->presentation_time_delta + box->event_duration > segment_duration) {
            g_critical("ERROR validating EMSG: event lasts longer tha segment duration.");
            goto fail;
        }
    }

cleanup:
    g_object_unref(input);
    free_boxes(boxes, num_boxes);
    return return_code;
fail:
    return_code = -1;
    goto cleanup;
}

void free_segment_iframes(data_segment_iframes_t* iframes, size_t num_segments)
{
    if (iframes == NULL) {
        return;
    }

    for(size_t i = 0; i < num_segments; i++) {
        free(iframes[i].iframe_locations_time);
        free(iframes[i].iframe_locations_byte);
        free(iframes[i].starts_with_sap);
        free(iframes[i].sap_type);
    }
    free(iframes);
}
