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
    char* file_name;
    char* media_range;
    uint64_t start;
    uint64_t duration;
    uint64_t end;
    char* index_file_name;
    char* index_range;

    /* These don't really belong here, but they make things much easier */
    uint64_t actual_start[NUM_CONTENT_COMPONENTS];
    uint64_t actual_end[NUM_CONTENT_COMPONENTS];

    void* arg;
    free_func_t arg_free;
} segment_t;

typedef struct {
    dash_profile_t profile;
    char* id;
    char* index_file_name;
    char* initialization_file_name;
    uint8_t start_with_sap;
    uint64_t presentation_time_offset;
    GPtrArray* segments;
} representation_t;

typedef struct {
    dash_profile_t profile;
    uint32_t audio_pid;
    uint32_t video_pid;
    uint32_t segment_alignment;
    uint32_t subsegment_alignment;
    bool bitstream_switching;
    GPtrArray* representations;
} adaptation_set_t;

typedef struct {
    GPtrArray* adaptation_sets;
} period_t;

typedef enum {
    MPD_PRESENTATION_UNKNOWN = 0,
    MPD_PRESENTATION_STATIC,
    MPD_PRESENTATION_DYNAMIC
} mpd_presentation_t;

typedef struct {
    dash_profile_t profile;
    mpd_presentation_t presentation_type;
    int max_video_gap_pts_ticks;
    int max_audio_gap_pts_ticks;
    char* initialization_segment;
    int presentation_time_offset;
    GPtrArray* periods;
} mpd_t;

mpd_t* mpd_new(void);
void mpd_free(mpd_t*);
mpd_t* read_mpd(char* file_name);
void mpd_print(const mpd_t*);

period_t* period_new(void);
void period_free(period_t*);
void period_print(const period_t*, unsigned indent);

adaptation_set_t* adaptation_set_new(void);
void adaptation_set_free(adaptation_set_t*);
void adaptation_set_print(const adaptation_set_t*, unsigned indent);

representation_t* representation_new(void);
void representation_free(representation_t*);
void representation_print(const representation_t*, unsigned indent);

segment_t* segment_new(void);
void segment_free(segment_t*);
void segment_print(const segment_t*, unsigned indent);

#endif