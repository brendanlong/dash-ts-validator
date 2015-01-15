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
    unsigned int* iframe_locations_time;
    uint64_t* iframe_locations_byte;
    unsigned char* starts_with_sap;
    unsigned char* sap_type;
} data_segment_iframes_t;

typedef enum {
    BOX_TYPE_EMSG = 0x656d7367,
    BOX_TYPE_PCRB = 0x70637262,
    BOX_TYPE_SIDX = 0x73696478,
    BOX_TYPE_SSIX = 0x73736978,
    BOX_TYPE_STYP = 0x73747970
} box_type_t;

void print_styp(data_styp_t*);
void print_sidx(data_sidx_t*);
void print_pcrb(data_pcrb_t*);
void print_ssix(data_ssix_t*);
void print_emsg(data_emsg_t*);

void print_sidx_reference(data_sidx_reference_t*);
void print_ssix_subsegment(data_ssix_subsegment_t*);

void free_styp(data_styp_t*);
void free_sidx(data_sidx_t*);
void free_pcrb(data_pcrb_t*);
void free_ssix(data_ssix_t*);
void free_emsg(data_emsg_t*);

void uint_to_string(char*, unsigned);

void print_boxes(box_t** boxes, size_t num_boxes);
void free_boxes(box_t** boxes, size_t num_boxes);
int read_boxes_from_file(char* file_name, box_t*** boxes_out, size_t* num_boxes_out);

int validate_index_segment(char* file_name, size_t num_segments, uint64_t* segment_durations,
        data_segment_iframes_t* iframes,
        int presentation_time_offset, int video_pid, bool is_simple_profile);
int validate_representation_index_segment_boxes(size_t num_segments, box_t** boxes, size_t num_boxes,
        uint64_t* segment_durations, data_segment_iframes_t* iframes, int presentation_time_offset,
        int video_pid, bool is_simple_profile);
int validate_single_index_segment_boxes(box_t** boxes, size_t num_boxes,
        uint64_t segment_duration, data_segment_iframes_t* iframes,
        int presentation_time_offset, int video_pid, bool is_simple_profile);

int validate_emsg_msg(uint8_t* buffer, size_t len, unsigned segment_duration);

int analyze_sidx_references(data_sidx_t*, int* num_iframes, int* num_nested_sidx,
        bool is_simple_profile);

void free_segment_iframes(data_segment_iframes_t*, size_t num_segments);

#endif
