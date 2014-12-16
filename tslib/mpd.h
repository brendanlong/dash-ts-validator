#ifndef TSLIB_MPD_H
#define TSLIB_MPD_H

#include <glib.h>
#include <stdbool.h>

#include "ISOBMFF.h"
#include "segment_validator.h"


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
} segment_t;

typedef struct {
    char* index_file_name;
    char* initialization_file_name;
    uint8_t start_with_sap;
    uint64_t presentation_time_offset;
    GPtrArray* segments;
} representation_t;

typedef struct {
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
void mpd_dump(const mpd_t*);

period_t* period_new(void);
void period_free(period_t*);
void period_dump(const period_t*, unsigned indent);

adaptation_set_t* adaptation_set_new(void);
void adaptation_set_free(adaptation_set_t*);
void adaptation_set_dump(const adaptation_set_t*, unsigned indent);

representation_t* representation_new(void);
void representation_free(representation_t*);
void representation_dump(const representation_t*, unsigned indent);

segment_t* segment_new(void);
void segment_free(segment_t*);
void segment_dump(const segment_t*, unsigned indent);

/*
int getNumRepresentations(char* fname, int* numRepresentations, int* numSegments);
void parseSegInfoFileLine(char* line, char* segFileNames, int segNum, int numSegments);
void parseRepresentationIndexFileLine(char* line, char* representationIndexFileNames,
                                      int numRepresentations);
int readIntFromSegInfoFile(FILE* segInfoFile, char* paramName, int* paramValue);
int readStringFromSegInfoFile(FILE* segInfoFile, char* paramName, char* paramValue);
char* trimWhitespace(char* string);
*/
#endif