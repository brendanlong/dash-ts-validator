
#ifndef TSLIB_SEGMENT_VALIDATOR_H
#define TSLIB_SEGMENT_VALIDATOR_H

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "isobmff.h"
#include "libts_common.h"
#include "log.h"
#include "mpd.h"
#include "mpeg2ts_demux.h"
#include "pes.h"
#include "psi.h"
#include "ts.h"


#define TS_STATE_PAT   0x01
#define TS_STATE_PMT   0x02
#define TS_STATE_PCR   0x04
#define TS_STATE_ECM   0x08

typedef enum {
    MEDIA_SEGMENT = 0x00,
    INITIALIZATION_SEGMENT,
    REPRESENTATION_INDEX_SEGMENT
} segment_type_t;

typedef struct {
    uint16_t pid;
    uint8_t sap;
    uint8_t sap_type;
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
    uint64_t start_time;
    uint64_t start_byte;
    uint64_t end_byte;
    bool starts_with_sap;
    uint8_t sap_type;
    bool saw_random_access;
    size_t ts_count;
    size_t pes_count;
    GArray* ssix_offsets; /* uint64_t offsets from ssix boxes */
} subsegment_t;

typedef struct {
    dash_profile_t profile;
    int64_t  last_pcr;
    long segment_start;
    long segment_end;
    GPtrArray* pids;
    uint16_t pcr_pid;
    uint16_t video_pid;
    uint16_t audio_pid;
    uint32_t pmt_program_number;
    uint32_t pmt_version_number;
    int status; // 0 == fail
    segment_type_t segment_type;
    bool use_initialization_segment;
    program_map_section_t* initialization_segment_pmt;      /// parsed PMT

    bool has_subsegments;
    size_t subsegment_index;
    GPtrArray* subsegments;
    subsegment_t* current_subsegment;

    segment_t* segment;
    adaptation_set_t* adaptation_set;
} dash_validator_t;

typedef struct {
    bool error; // false = success

    /* Each entry is a GPtrArray* of subsegment_t, one for each segment */
    GPtrArray* segment_subsegments;
} index_segment_validator_t;

const char* content_component_to_string(content_component_t);

dash_validator_t* dash_validator_new(segment_type_t, dash_profile_t);
void dash_validator_init(dash_validator_t*, segment_type_t, dash_profile_t);
void dash_validator_destroy(dash_validator_t*);
void dash_validator_free(dash_validator_t*);

void index_segment_validator_free(index_segment_validator_t*);

int validate_segment(dash_validator_t* dash_validator, char* file_name, dash_validator_t* dash_validator_init);

index_segment_validator_t* validate_index_segment(char* file_name, segment_t*, representation_t*, adaptation_set_t*);

#endif