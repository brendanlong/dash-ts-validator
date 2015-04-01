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
#ifndef TSLIB_MPD_H
#define TSLIB_MPD_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

#define DASH_PROFILE_URN_FULL "urn:mpeg:dash:profile:full:2011"
#define DASH_PROFILE_URN_MPEG2TS_MAIN "urn:mpeg:dash:profile:mp2t-main:2011"
#define DASH_PROFILE_URN_MPEG2TS_SIMPLE "urn:mpeg:dash:profile:mp2t-simple:2011"

typedef enum {
    DASH_PROFILE_UNKNOWN = -1,
    DASH_PROFILE_FULL = 0,
    DASH_PROFILE_MPEG2TS_MAIN,
    DASH_PROFILE_MPEG2TS_SIMPLE
} dash_profile_t;

typedef enum {
    UNKNOWN_CONTENT_COMPONENT = 0x00,
    VIDEO_CONTENT_COMPONENT,
    AUDIO_CONTENT_COMPONENT,
    NUM_CONTENT_COMPONENTS
} content_component_t;

typedef void (*free_func_t)(void*);

typedef struct {
    bool has_int;
    bool b;
    uint32_t i;
} optional_uint32_t;

struct _representation_t;
struct _adaptation_set_t;
struct _period_t;
struct _mpd_t;

typedef struct {
    struct _representation_t* representation;
    char* file_name;
    uint64_t media_range_start;
    uint64_t media_range_end;
    uint64_t start;
    uint64_t duration;
    uint64_t end;
    char* index_file_name;
    uint64_t index_range_start;
    uint64_t index_range_end;

    /* These don't really belong here, but they make things much easier */
    uint64_t actual_start[NUM_CONTENT_COMPONENTS];
    uint64_t actual_end[NUM_CONTENT_COMPONENTS];

    void* arg;
    free_func_t arg_free;
} segment_t;

typedef struct {
    struct _representation_t* representation;
    dash_profile_t profile;
    uint8_t start_with_sap;
    bool has_level;
    uint32_t level;
    uint32_t bandwidth;
    GArray* dependency_level;
    GPtrArray* content_component;
} subrepresentation_t;

typedef struct _representation_t {
    struct _adaptation_set_t* adaptation_set;
    dash_profile_t profile;
    char* id;
    char* mime_type;
    char* index_file_name;
    uint64_t index_range_start;
    uint64_t index_range_end;
    char* initialization_file_name;
    uint64_t initialization_range_start;
    uint64_t initialization_range_end;
    char* bitstream_switching_file_name;
    uint64_t bitstream_switching_range_start;
    uint64_t bitstream_switching_range_end;
    uint8_t start_with_sap;
    uint64_t presentation_time_offset;
    uint32_t bandwidth;
    uint32_t timescale;
    uint64_t segment_index_range_start;
    uint64_t segment_index_range_end;
    GPtrArray* subrepresentations;
    GPtrArray* segments;
} representation_t;

typedef struct _adaptation_set_t {
    struct _period_t* period;
    uint32_t id;
    char* mime_type;
    dash_profile_t profile;
    uint32_t audio_pid;
    uint32_t video_pid;
    optional_uint32_t segment_alignment;
    optional_uint32_t subsegment_alignment;
    bool bitstream_switching;
    GPtrArray* representations;
} adaptation_set_t;

typedef struct _period_t {
    struct _mpd_t* mpd;
    bool bitstream_switching;
    uint64_t duration;
    GPtrArray* adaptation_sets;
} period_t;

typedef enum {
    MPD_PRESENTATION_STATIC = 0,
    MPD_PRESENTATION_DYNAMIC
} mpd_presentation_t;

typedef struct _mpd_t {
    dash_profile_t profile;
    mpd_presentation_t presentation_type;
    uint64_t duration;
    GPtrArray* periods;
} mpd_t;

mpd_t* mpd_new(void);
void mpd_free(mpd_t*);
mpd_t* mpd_read_file(char* file_name);
mpd_t* mpd_read_doc(char* mpd_xml, char* base_url);
void mpd_print(const mpd_t*);

period_t* period_new(mpd_t*);
void period_free(period_t*);
void period_print(const period_t*, unsigned indent);

adaptation_set_t* adaptation_set_new(period_t*);
void adaptation_set_free(adaptation_set_t*);
void adaptation_set_print(const adaptation_set_t*, unsigned indent);

representation_t* representation_new(adaptation_set_t*);
void representation_free(representation_t*);
void representation_print(const representation_t*, unsigned indent);

subrepresentation_t* subrepresentation_new(representation_t*);
void subrepresentation_free(subrepresentation_t*);
void subrepresentation_print(const subrepresentation_t*, unsigned indent);

segment_t* segment_new(representation_t*);
void segment_free(segment_t*);
void segment_print(const segment_t*, unsigned indent);

#endif