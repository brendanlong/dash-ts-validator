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
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <libxml/parser.h>
#include "log.h"

#include "segment_validator.h"
#include "mpd.h"


int check_representation_gaps(GPtrArray* representations, content_component_t, int64_t max_delta);
int check_segment_timing(GPtrArray* segments, content_component_t);
int check_psi_identical(GPtrArray* representations);

static struct option long_options[] = {
    { "verbose", no_argument, NULL, 'v' },
    { "help", no_argument, NULL, 'h' },
};

static char options[] =
    "\t-v, --verbose\n"
    "\t-h, --help\n";

static void usage(char* name)
{
    fprintf(stderr, "Usage: \n%s [options] MPD_file\n\nOptions:\n%s\n", name,
            options);
}

int main(int argc, char* argv[])
{
    int c, long_options_index;
    extern char* optarg;
    extern int optind;

    if(argc < 2) {
        usage(argv[0]);
        return 1;
    }

    while((c = getopt_long(argc, argv, "vh", long_options, &long_options_index)) != -1) {
        switch(c) {
        case 'v':
            if(tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) {
                tslib_loglevel++;
            }
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 1;
        }
    }

    g_log_set_default_handler(log_handler, NULL);

    int overall_status = 1;    // overall pass/fail, with 1=PASS, 0=FAIL

    /* This should probably be configurable */
    int64_t max_gap_pts_ticks[NUM_CONTENT_COMPONENTS] = {0};

    /* Read MPD file */
    char* file_name = argv[optind];
    if(file_name == NULL || file_name[0] == 0) {
        g_critical("No MPD file provided");
        usage(argv[0]);
        return 1;
    }
    mpd_t* mpd = read_mpd(file_name);
    if (mpd == NULL) {
        g_critical("Error: Failed to read MPD.");
        goto cleanup;
    }
    mpd_print(mpd);

    for (size_t p_i = 0; p_i < mpd->periods->len; ++p_i) {
        period_t* period = g_ptr_array_index(mpd->periods, p_i);
        for (size_t a_i = 0; a_i < period->adaptation_sets->len; ++a_i) {
            adaptation_set_t* adaptation_set = g_ptr_array_index(period->adaptation_sets, a_i);
            bool adaptation_set_valid = true;

            g_print("VALIDATING ADAPTATION SET: %s\n", adaptation_set->id);
            for (size_t r_i = 0; r_i < adaptation_set->representations->len; ++r_i) {
                representation_t* representation = g_ptr_array_index(adaptation_set->representations, r_i);
                bool representation_valid = true;
                g_print("\nVALIDATING REPRESENTATION: %s\n", representation->id);

                if (representation->segments->len == 0) {
                    g_critical("Representation has no segments!");
                    goto cleanup;
                }

                for (size_t s_i = 0; s_i < representation->segments->len; ++s_i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, s_i);
                    dash_validator_t* validator = dash_validator_new(MEDIA_SEGMENT, representation->profile);
                    validator->adaptation_set = adaptation_set;
                    validator->segment = segment;

                    segment->arg = validator;
                    segment->arg_free = (free_func_t)dash_validator_free;
                }

                // if there is an initialization segment, process it first in order to get the PAT and PMT tables
                dash_validator_t* validator_init_segment = NULL;
                if (representation->initialization_file_name) {
                    validator_init_segment = dash_validator_new(INITIALIZATION_SEGMENT, representation->profile);
                    if (validate_segment(validator_init_segment, representation->initialization_file_name, 0, 0, NULL) != 0) {
                        validator_init_segment->status = 0;
                    }
                    g_print("INITIALIZATION SEGMENT TEST RESULT: %s: %s\n", representation->initialization_file_name,
                            validator_init_segment->status ? "SUCCESS" : "FAIL");
                    representation_valid &= validator_init_segment->status;
                }

                /* Validate Bitstream Switching Segment */
                if (representation->initialization_file_name) {
                    dash_validator_t* validator = dash_validator_new(BITSTREAM_SWITCHING_SEGMENT,
                            representation->profile);
                    if (validate_segment(validator, representation->bitstream_switching_file_name,
                            representation->bitstream_switching_range_start,
                            representation->bitstream_switching_range_end, validator_init_segment) != 0) {
                        validator->status = 0;
                    }
                    g_print("BITSTREAM SWITCHING SEGMENT TEST RESULT: %s: %s\n",
                            representation->bitstream_switching_file_name, validator->status ? "SUCCESS" : "FAIL");
                    representation_valid &= validator->status;
                }

                /* Validate Representation Index */
                if (representation->index_file_name) {
                    index_segment_validator_t* index_validator = validate_index_segment(
                            representation->index_file_name, NULL, representation, adaptation_set);
                    if (index_validator->error) {
                        representation_valid = false;
                    }
                    g_print("REPRESENTATION INDEX TEST RESULT: %s: %s\n", representation->index_file_name,
                            index_validator->error ? "FAIL" : "SUCCESS");
                    if (index_validator->segment_subsegments->len != 0 &&
                            index_validator->segment_subsegments->len != representation->segments->len) {
                        g_error("PROGRAMMING ERROR: index_segment_validator_t->segment_subsegments returned from "
                                "validate_index_segment()  should have on subsegments GPtrArray* per segment, but we have "
                                "%zu segments and %zu segment_subsegments", representation->segments->len,
                                index_validator->segment_subsegments->len);
                        /* g_error asserts */
                    }
                    for (size_t s_i = 0; s_i < index_validator->segment_subsegments->len; ++s_i) {
                        segment_t* segment = g_ptr_array_index(representation->segments, s_i);
                        dash_validator_t* validator = segment->arg;
                        validator->has_subsegments = true;
                        GPtrArray* subsegments = g_ptr_array_index(index_validator->segment_subsegments, s_i);
                        for (size_t i = 0; i < subsegments->len; ++i) {
                            g_ptr_array_add(validator->subsegments, g_ptr_array_index(subsegments, i));
                        }
                        g_ptr_array_set_size(subsegments, 0);
                        
                    }
                    index_segment_validator_free(index_validator);
                }

                for (size_t s_i = 0; s_i < representation->segments->len; ++s_i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, s_i);

                    /* Validate Segment Index */
                    if (segment->index_file_name) {
                        index_segment_validator_t* index_validator = validate_index_segment(
                                segment->index_file_name, segment, representation, adaptation_set);
                        if (index_validator->error) {
                            representation_valid = false;
                        }
                        g_print("SINGLE SEGMENT INDEX TEST RESULT: %s: %s\n", representation->index_file_name,
                                index_validator->error ? "FAIL" : "SUCCESS");
                        if (index_validator->segment_subsegments->len != 0) {
                            GPtrArray* subsegments = g_ptr_array_index(index_validator->segment_subsegments, 0);
                            dash_validator_t* validator = segment->arg;
                            if (validator->subsegments->len != 0) {
                                g_critical("DASH Conformance: Segment %s has a representation index and a single "
                                        "segment index, but should only have one or the other. 6.4.6 Index Segment: "
                                        "Index Segments may either be associated to a single Media Segment as "
                                        "specified in 6.4.6.2 or may be associated to all Media Segments in one "
                                        "Representation as specified in 6.4.6.3.", segment->file_name);
                                representation_valid = false;
                            } else {
                                validator->has_subsegments = true;
                                for (size_t i = 0; i < subsegments->len; ++i) {
                                    g_ptr_array_add(validator->subsegments, g_ptr_array_index(subsegments, i));
                                }
                                g_ptr_array_set_size(subsegments, 0);
                            }
                        }
                        index_segment_validator_free(index_validator);
                    }

                    /* Validate Segment */
                    dash_validator_t* validator = segment->arg;
                    if (!validate_segment(validator, segment->file_name, segment->media_range_start,
                            segment->media_range_end, validator_init_segment) != 0) {
                        // GORP: what if there is no video in the segment??
                        for (gsize pid_i = 0; pid_i < validator->pids->len; pid_i++) {
                            pid_validator_t* pv = g_ptr_array_index(validator->pids, pid_i);

                            // refine duration by including duration of last frame (audio and video are different rates)
                            // start time is relative to the start time of the first segment

                            // units of 90kHz ticks
                            int64_t actual_start = pv->earliest_playout_time;
                            int64_t actual_duration = (pv->latest_playout_time - pv->earliest_playout_time) + pv->duration;
                            int64_t actual_end = actual_start + actual_duration;

                            segment->actual_start[pv->content_component] = actual_start;
                            segment->actual_end[pv->content_component] = actual_end;

                            g_debug("%s: %04X: %s STARTTIME=%"PRId64", ENDTIME=%"PRId64", DURATION=%"PRId64"",
                                    segment->file_name, pv->pid, content_component_to_string(pv->content_component),
                                    actual_start, actual_end, actual_duration);

                            if (pv->content_component == VIDEO_CONTENT_COMPONENT && representation->start_with_sap != 0) {
                                if (pv->sap == 0) {
                                    g_critical("%s: FAIL: SAP not set", segment->file_name);
                                    validator->status = 0;
                                } else if (pv->sap_type != representation->start_with_sap) {
                                    g_critical("%s: FAIL: Invalid SAP Type: expected %d, actual %d",
                                            segment->file_name, representation->start_with_sap, pv->sap_type);
                                    validator->status = 0;
                                }
                            }
                        }
                    }

                    g_print("SEGMENT TEST RESULT: %s: %s\n", segment->file_name,
                            validator->status ? "SUCCESS" : "FAIL");
                    g_info("");
                    representation_valid &= validator->status;
                }

                /* Check that segments in the same representation don't have gaps between them */
                representation_valid &= check_segment_timing(representation->segments, AUDIO_CONTENT_COMPONENT);
                representation_valid &= check_segment_timing(representation->segments, VIDEO_CONTENT_COMPONENT);

                g_print("REPRESENTATION TEST RESULT: %s: %s\n", representation->id,
                        representation_valid ? "SUCCESS" : "FAIL");
                g_info("");
                adaptation_set_valid &= representation_valid;

                dash_validator_free(validator_init_segment);
            }


            // segment cross checking: check that the gap between all adjacent segments is acceptably small
            adaptation_set_valid &= check_representation_gaps(adaptation_set->representations,
                    AUDIO_CONTENT_COMPONENT, max_gap_pts_ticks[AUDIO_CONTENT_COMPONENT]);
            adaptation_set_valid &= check_representation_gaps(adaptation_set->representations,
                    VIDEO_CONTENT_COMPONENT, max_gap_pts_ticks[VIDEO_CONTENT_COMPONENT]);

            if(adaptation_set->profile >= DASH_PROFILE_MPEG2TS_SIMPLE) {
                /* For the simple profile, the PSI must be the same for all Representations in an
                 * AdaptationSet */
                adaptation_set_valid &= check_psi_identical(adaptation_set->representations);
            }

            g_print("ADAPTATION SET TEST RESULT: %s: %s\n", adaptation_set->id,
                    adaptation_set_valid ? "SUCCESS" : "FAIL");
            g_info("");
            overall_status &= adaptation_set_valid;
        }
    }

    g_print("\nOVERALL TEST RESULT: %s\n", overall_status ? "PASS" : "FAIL");
cleanup:
    mpd_free(mpd);
    xmlCleanupParser();
    return overall_status != 0;
}

int check_representation_gaps(GPtrArray* representations, content_component_t content_component, int64_t max_delta)
{
    if (representations->len == 0) {
        g_warning("Can't print gap matrix for empty set of representations.");
        return 0;
    }
    int status = 1;
    representation_t* first_representation = g_ptr_array_index(representations, 0);

    /* First figure out if there are any gaps, so we can be quieter if there are none */
    GLogLevelFlags flags = G_LOG_LEVEL_INFO;
    for (gsize s_i = 1; s_i < first_representation->segments->len; ++s_i) {
        for (gsize r_i = 0; r_i < representations->len; ++r_i) {
            representation_t* representation1 = g_ptr_array_index(representations, r_i);
            segment_t* segment1 = g_ptr_array_index(representation1->segments, s_i - 1);

            for(gsize r_i2 = 0; r_i2 < representations->len; ++r_i2) {
                representation_t* representation2 = g_ptr_array_index(representations, r_i2);
                segment_t* segment2 = g_ptr_array_index(representation2->segments, s_i);

                int64_t pts_delta = segment2->actual_start[content_component] - (int64_t)segment1->actual_end[content_component];
                if (pts_delta) {
                    flags = G_LOG_LEVEL_WARNING;
                    if (pts_delta > max_delta) {
                        g_critical("FAIL: %s gap between for segment %zu for representations %s and %s is %"PRId64" and exceeds limit %"PRId64,
                                content_component_to_string(content_component), s_i, representation1->id, representation2->id,
                                pts_delta, max_delta);
                        status = 0;
                    }
                }
            }
        }
    }

    g_log(G_LOG_DOMAIN, flags, "%sGapMatrix", content_component_to_string(content_component));
    for (gsize s_i = 1; s_i < first_representation->segments->len; ++s_i) {
        GString* line = g_string_new("    \t");
        for (gsize r_i = 0; r_i < representations->len; ++r_i) {
            representation_t* representation = g_ptr_array_index(representations, r_i);
            segment_t* segment = g_ptr_array_index(representation->segments, s_i);
            g_string_append_printf(line, "%s\t", segment->file_name);
        }
        g_log(G_LOG_DOMAIN, flags, "%s", line->str);

        for (gsize r_i = 0; r_i < representations->len; ++r_i) {
            representation_t* representation = g_ptr_array_index(representations, r_i);
            segment_t* segment1 = g_ptr_array_index(representation->segments, s_i - 1);
            g_string_printf(line, "%s\t", segment1->file_name);

            for(gsize r_i2 = 0; r_i2 < representations->len; ++r_i2) {
                representation_t* representation2 = g_ptr_array_index(representations, r_i2);
                segment_t* segment2 = g_ptr_array_index(representation2->segments, s_i);

                int64_t pts_delta = segment2->actual_start[content_component] - segment1->actual_end[content_component];
                g_string_append_printf(line, "%"PRId64"\t", pts_delta);
            }
            g_log(G_LOG_DOMAIN, flags, "%s", line->str);
        }
        g_log(G_LOG_DOMAIN, flags, " ");
        g_string_free(line, true);
    }
    return status;
}

int check_segment_timing(GPtrArray* segments, content_component_t content_component)
{
    if (segments->len == 0) {
        g_warning("Can't print timing matrix for empty set of segments.");
        return 0;
    }

    int status = 1;

    /* First figure out if there are any gaps, so we can be quieter if there are none */
    GLogLevelFlags log_level = G_LOG_LEVEL_INFO;
    for(gsize i = 0; i < segments->len; ++i) {
        segment_t* segment = g_ptr_array_index(segments, i);
        uint64_t actual_start = segment->actual_start[content_component];
        uint64_t actual_end = segment->actual_end[content_component];
        uint64_t previous_end;
        int64_t delta_start = actual_start - (int64_t)segment->start;
        int64_t delta_end = actual_end - (int64_t)segment->end;
        int64_t delta_previous = 0;
        if (i > 0) {
            segment_t* previous = g_ptr_array_index(segments, i - 1);
            previous_end = previous->actual_end[content_component];
            delta_previous = actual_start - previous_end;
        }
        if (content_component == VIDEO_CONTENT_COMPONENT && (delta_start || delta_end || delta_previous)) {
            log_level = G_LOG_LEVEL_WARNING;
            status = 0;
        }
        if (delta_start) {
            g_log(G_LOG_DOMAIN, log_level,
                    "%s: %s: Invalid start time: expected = %"PRIu64", actual = %"PRIu64", delta = %"PRId64,
                    segment->file_name, content_component_to_string(content_component),
                    segment->start, actual_start, delta_start);
        }
        if (delta_end) {
            g_log(G_LOG_DOMAIN, log_level,
                    "%s: %s: Invalid end time: expected = %"PRIu64", actual = %"PRIu64", delta = %"PRId64,
                    segment->file_name, content_component_to_string(content_component),
                    segment->end, actual_end, delta_end);
        }
        if (delta_previous) {
            g_log(G_LOG_DOMAIN, log_level,
                    "%s: %s: Last end time: %"PRIu64", Current start time: %"PRIu64", Delta: %"PRId64,
                    segment->file_name, content_component_to_string(content_component),
                    previous_end, actual_start, delta_previous);
        }
    }

    g_log(G_LOG_DOMAIN, log_level, " ");
    g_log(G_LOG_DOMAIN, log_level, "%sTiming", content_component_to_string(content_component));
    g_log(G_LOG_DOMAIN, log_level, "segmentFile\texpectedStart\texpectedEnd\tactualStart\tactualEnd\tdeltaStart\tdeltaEnd");
    for(gsize i = 0; i < segments->len; ++i) {
        segment_t* segment = g_ptr_array_index(segments, i);
        int64_t delta_start = segment->actual_start[content_component] - (int64_t)segment->start;
        int64_t delta_end = segment->actual_end[content_component] - (int64_t)segment->end;
        g_log(G_LOG_DOMAIN, log_level,
               "%s\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64,
               segment->file_name, segment->start, segment->end,
               segment->actual_start[content_component], segment->actual_end[content_component],
               delta_start, delta_end);
    }
    g_log(G_LOG_DOMAIN, log_level, " ");
    return status;
}

// check that each segment has the same video PID, audio PID, PMT version, PMT program num, and PCR PID
int check_psi_identical(GPtrArray* representations)
{
    if (representations->len == 0) {
        g_warning("Can't check PSI for empty set of representations.");
        return 0;
    }

    g_info("Validating that PSI info is identical in each segment\n");
    int status = 1; // 1=PASS, 0=FAIL

    int video_pid;
    int audio_pid;
    int pcr_pid;
    uint32_t pmt_program_number;
    uint32_t pmt_version_number;

    for (gsize r_i = 0; r_i < representations->len; ++r_i) {
        representation_t* representation = g_ptr_array_index(representations, r_i);
        for (gsize s_i = 0; s_i < representation->segments->len; ++s_i) {
            segment_t* segment = g_ptr_array_index(representation->segments, s_i);
            dash_validator_t* validator = (dash_validator_t*)segment->arg;

            if (r_i == 0 && s_i == 0) {
                video_pid = validator->video_pid;
                audio_pid = validator->audio_pid;
                pcr_pid = validator->pcr_pid;
                pmt_program_number = validator->pmt_program_number;
                pmt_version_number = validator->pmt_version_number;
            } else {
                if (video_pid != validator->video_pid) {
                    g_critical("PSI Table Validation FAILED: Incorrect video PID: Expected = %d, Actual = %d",
                            video_pid, validator->video_pid);
                    status = 0;
                }
                if (audio_pid != validator->audio_pid) {
                    g_critical("PSI Table Validation FAILED: Incorrect audio PID: Expected = %d, Actual = %d",
                            audio_pid, validator->audio_pid);
                    status = 0;
                }
                if (pcr_pid != validator->pcr_pid) {
                    g_critical("PSI Table Validation FAILED: Incorrect PCR PID: Expected = %d, Actual = %d",
                            pcr_pid, validator->pcr_pid);
                    status = 0;
                }
                if (pmt_program_number != validator->pmt_program_number) {
                    g_critical("PSI Table Validation FAILED: Incorrect pmt_program_number: Expected = %d, Actual = %d",
                            pmt_program_number, validator->pmt_program_number);
                    status = 0;
                }
                if (pmt_version_number != validator->pmt_version_number) {
                    g_critical("PSI Table Validation FAILED: Incorrect pmt_version_number: Expected = %d, Actual = %d",
                            pmt_version_number, validator->pmt_version_number);
                    status = 0;
                }
            }
        }
    }
    if (status != 1) {
        g_critical("Validation FAILED: PSI info not identical for all segments\n");
    }
    return status;
}
