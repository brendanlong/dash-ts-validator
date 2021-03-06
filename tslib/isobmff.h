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
#ifndef ISOBMFF_CONFORMANCE_H
#define ISOBMFF_CONFORMANCE_H

#include <stdint.h>
#include <stdbool.h>

#include "bitreader.h"


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
} styp_t;

typedef struct {
    uint8_t reference_type;
    uint32_t referenced_size;
    uint32_t subsegment_duration;
    bool starts_with_sap;
    uint8_t sap_type;
    uint32_t sap_delta_time;
} sidx_reference_t;

typedef struct {
    uint64_t size;
    uint32_t type;
    uint8_t version;
    uint32_t flags;
    uint32_t reference_id;
    uint32_t timescale;
    uint64_t earliest_presentation_time;
    uint64_t first_offset;

    uint16_t reference_count;
    sidx_reference_t* references;
} sidx_t;

typedef struct {
    uint8_t level;
    uint32_t range_size;
} ssix_subsegment_range_t;

typedef struct {
    uint32_t ranges_count;
    ssix_subsegment_range_t* ranges;
} ssix_subsegment_t;

typedef struct {
    uint64_t size;
    uint32_t type;
    uint8_t version;
    uint32_t flags;
    uint32_t subsegment_count;
    ssix_subsegment_t* subsegments;
} ssix_t;

typedef struct {
    uint64_t size;
    uint32_t type;
    uint32_t subsegment_count;
    uint64_t* pcr;
} pcrb_t;

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
    size_t message_size;
} emsg_t;

typedef enum {
    BOX_TYPE_EMSG = 0x656d7367,
    BOX_TYPE_PCRB = 0x70637262,
    BOX_TYPE_SIDX = 0x73696478,
    BOX_TYPE_SSIX = 0x73736978,
    BOX_TYPE_STYP = 0x73747970
} box_type_t;

box_t** read_boxes_from_file(const char* file_name, size_t* num_boxes, int* error);
box_t** read_boxes_from_stream(bitreader_t*, size_t* num_boxes, int* error);

box_t* read_box(bitreader_t*, int* error);
void print_box(const box_t*);
void free_box(box_t* box);

void print_boxes(box_t* const* boxes, size_t num_boxes);
void free_boxes(box_t** boxes, size_t num_boxes);

void uint32_to_string(char* str_out, uint32_t num);

#endif
