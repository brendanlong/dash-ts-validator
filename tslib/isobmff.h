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
#ifndef ISOBMFF_CONFORMANCE_H
#define ISOBMFF_CONFORMANCE_H

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>
#include <stdbool.h>


typedef enum {
    ISOBMFF_ERROR_BAD_BOX_SIZE
} isobmff_error_t;

typedef enum {
    BRAND_RISX = 0x72697378,
    BRAND_SISX = 0x73697378,
    BRAND_SSSS = 0x73737373
} brand_t;

typedef struct {
    uint64_t size;
    uint32_t type;
} box_t;

typedef struct {
    box_t box;
    uint8_t version;
    uint32_t flags;
} fullbox_t;

typedef struct {
    uint64_t size;
    uint32_t type;
    uint32_t major_brand;
    uint32_t minor_version;
    size_t num_compatible_brands;
    uint32_t* compatible_brands;
} data_styp_t;

typedef struct {
    uint8_t reference_type;
    uint32_t referenced_size;
    uint32_t subsegment_duration;
    uint8_t starts_with_sap;
    uint8_t sap_type;
    uint32_t sap_delta_time;
} data_sidx_reference_t;

typedef struct {
    uint64_t size;
    uint32_t type;
    uint8_t version;
    uint32_t flags;
    uint32_t reference_id;
    uint32_t timescale;
    uint64_t earliest_presentation_time;
    uint64_t first_offset;

    uint16_t reserved;
    uint16_t reference_count;
    data_sidx_reference_t* references;
} data_sidx_t;

typedef struct {
    uint8_t level;
    uint32_t range_size;
} data_ssix_subsegment_range_t;

typedef struct {
    uint32_t ranges_count;
    data_ssix_subsegment_range_t* ranges;
} data_ssix_subsegment_t;

typedef struct {
    uint64_t size;
    uint32_t type;
    uint8_t version;
    uint32_t flags;
    uint32_t subsegment_count;
    data_ssix_subsegment_t* subsegments;
} data_ssix_t;

typedef struct {
    uint64_t size;
    uint32_t type;
    uint32_t subsegment_count;
    uint64_t* pcr;
} data_pcrb_t;

typedef struct {
    uint64_t size;
    uint32_t type;
    uint8_t version;
    uint32_t flags;
    char* scheme_id_uri;
    char* value;
    uint32_t timescale;
    uint32_t presentation_time_delta;
    uint32_t event_duration;
    uint32_t id;
    uint8_t* message_data;
    size_t message_data_size;
} data_emsg_t;

typedef struct {
    bool do_iframe_validation;
    int num_iframes;
    unsigned* iframe_locations_time;
    uint64_t* iframe_locations_byte;
    uint8_t* starts_with_sap;
    uint8_t* sap_type;
} data_segment_iframes_t;

typedef enum {
    BOX_TYPE_EMSG = 0x656d7367,
    BOX_TYPE_PCRB = 0x70637262,
    BOX_TYPE_SIDX = 0x73696478,
    BOX_TYPE_SSIX = 0x73736978,
    BOX_TYPE_STYP = 0x73747970
} box_type_t;

int read_boxes_from_file(char* file_name, box_t*** boxes_out, size_t* num_boxes);
int read_boxes_from_stream(GDataInputStream* input, box_t*** boxes_out, size_t* num_boxes);

box_t* parse_box(GDataInputStream*, GError**);
void print_box(box_t*);
void free_box(box_t* box);

void print_boxes(box_t** boxes, size_t num_boxes);
void free_boxes(box_t** boxes, size_t num_boxes);

data_segment_iframes_t* data_segment_iframes_new(size_t num_segments);
void data_segment_iframes_free(data_segment_iframes_t*, size_t num_segments);

void uint32_to_string(char* str_out, uint32_t num);

#endif
