
#ifndef TSLIB_SEGMENT_VALIDATOR_H
#define TSLIB_SEGMENT_VALIDATOR_H

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "isobmff.h"
#include "libts_common.h"
#include "log.h"
#include "mpeg2ts_demux.h"
#include "pes.h"
#include "psi.h"
#include "ts.h"

#define TS_STATE_PAT   0x01
#define TS_STATE_PMT   0x02
#define TS_STATE_PCR   0x04
#define TS_STATE_ECM   0x08

#define TS_TEST_DASH   0x01
#define TS_TEST_MAIN   0x02
#define TS_TEST_SIMPLE 0x04


typedef enum {
    UNKNOWN_CONTENT_COMPONENT = 0x00,
    VIDEO_CONTENT_COMPONENT,
    AUDIO_CONTENT_COMPONENT,
    NUM_CONTENT_COMPONENTS
} content_component_t;

typedef enum {
    MEDIA_SEGMENT = 0x00,
    INITIALIZATION_SEGMENT,
    REPRESENTATION_INDEX_SEGMENT
} segment_type_t;

typedef struct {
    int pid;
    int sap;
    int sap_type;
    int64_t earliest_playout_time;
    int64_t latest_playout_time;
    int64_t duration; // duration of latest pes packet
    uint64_t pes_count;
    uint64_t ts_count;
    content_component_t content_component;
    int continuity_counter;
    GPtrArray* ecm_pids;
} pid_validator_t;

typedef struct {
    uint32_t conformance_level;
    int64_t  last_pcr;
    long segment_start;
    long segment_end;
    GPtrArray* pids;
    int pcr_pid;
    int video_pid;
    int audio_pid;
    uint32_t pmt_program_number;
    uint32_t pmt_version_number;
    int status; // 0 == fail
    segment_type_t segment_type;
    bool use_initialization_segment;
    program_map_section_t* initialization_segment_pmt;      /// parsed PMT
} dash_validator_t;

const char* content_component_to_string(content_component_t);

dash_validator_t* dash_validator_new(segment_type_t, uint32_t conformance_level);
void dash_validator_init(dash_validator_t*, segment_type_t, uint32_t conformance_level);
void dash_validator_destroy(dash_validator_t*);
void dash_validator_free(dash_validator_t*);

int validate_segment(dash_validator_t* dash_validator, char* fname,
        dash_validator_t* dash_validator_init,
        data_segment_iframes_t* pIFrameData, uint64_t segmentDuration);

int validate_index_segment(char* file_name, size_t num_segments, uint64_t* segment_durations,
        data_segment_iframes_t* iframes,
        int presentation_time_offset, int video_pid, bool is_simple_profile);

#endif