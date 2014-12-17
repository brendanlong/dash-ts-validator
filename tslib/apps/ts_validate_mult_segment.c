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
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <libxml/parser.h>
#include "log.h"

#include "segment_validator.h"
#include "ISOBMFF.h"
#include "mpd.h"


void print_gap_matrix(GPtrArray* representations, content_component_t);
void print_timing_matrix(GPtrArray* segments, content_component_t);
int check_psi_identical(GPtrArray* representations);

static struct option long_options[] = {
    { "verbose",	   no_argument,        NULL, 'v' },
    { "dash",	   optional_argument,  NULL, 'd' },
    { "help",       no_argument,        NULL, 'h' },
};

static char options[] =
    "\t-d(main|simple), --dash(main|simple)\n"
    "\t-v, --verbose\n"
    "\t-h, --help\n";

static void usage(char* name)
{
    fprintf(stderr, "\n%s\n", name);
    fprintf(stderr, "\nUsage: \n%s [options] MPD_file\n\nOptions:\n%s\n", name,
            options);
}

int main(int argc, char* argv[])
{
    int c, long_options_index;
    extern char* optarg;
    extern int optind;

    uint32_t conformance_level = 0;

    if(argc < 2) {
        usage(argv[0]);
        return 1;
    }

    while((c = getopt_long(argc, argv, "vd::h", long_options, &long_options_index)) != -1) {
        switch(c) {
        case 'd':
            conformance_level = TS_TEST_DASH;
            if(optarg != NULL) {
                if(!strcmp(optarg, "simple")) {
                    conformance_level |= TS_TEST_SIMPLE;
                    conformance_level |= TS_TEST_MAIN;
                } else if(!strcmp(optarg, "main")) {
                    conformance_level |= TS_TEST_MAIN;
                }
            }
            break;
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

    int overallStatus = 1;    // overall pass/fail, with 1=PASS, 0=FAIL

    /* Read MPD file */
    char* file_name = argv[optind];
    if(file_name == NULL || file_name[0] == 0) {
        g_critical("No MPD file provided");
        usage(argv[0]);
        return 1;
    }
    mpd_t* mpd = read_mpd(file_name);
    if (mpd == NULL) {
        goto cleanup;
    }
    mpd_dump(mpd);

    // if there is an initialization segment, process it first in order to get the PAT and PMT tables
    for (size_t p_i = 0; p_i < mpd->periods->len; ++p_i) {
        period_t* period = g_ptr_array_index(mpd->periods, p_i);
        for (size_t a_i = 0; a_i < period->adaptation_sets->len; ++a_i) {
            adaptation_set_t* adaptation_set = g_ptr_array_index(period->adaptation_sets, a_i);
            for (size_t r_i = 0; r_i < adaptation_set->representations->len; ++r_i) {
                representation_t* representation = g_ptr_array_index(adaptation_set->representations, r_i);
                if (representation->segments->len == 0) {
                    g_critical("Representation has no segments!");
                    goto cleanup;
                }

                /* At some point we should replace these gigantic arrays with more reasonable data structures */
                dash_validator_t* validator_init_segment = dash_validator_new(INITIALIZATION_SEGMENT, conformance_level);

                data_segment_iframes_t* iframe_data = calloc(representation->segments->len, sizeof(data_segment_iframes_t));

                for (size_t s_i = 0; s_i < representation->segments->len; ++s_i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, s_i);
                    dash_validator_init(&segment->validator, MEDIA_SEGMENT, conformance_level);
                    segment->validator.use_initialization_segment = representation->initialization_file_name != NULL;
                }

                if (representation->initialization_file_name) {
                    if(doSegmentValidation(validator_init_segment, representation->initialization_file_name,
                                           NULL, NULL, 0) != 0) {
                        g_critical("Validation of initialization segment %s FAILED.", representation->initialization_file_name);
                        validator_init_segment->status = 0;
                    }
                    overallStatus &= validator_init_segment->status;
                }

                // next process index files, either representation index files or single segment index files
                // the index files contain iFrame locations which are stored here and then validated when the actual
                // media segments are processed later
                segment_t* first_segment = g_ptr_array_index(representation->segments, 0);
                for (size_t s_i = 0; s_i < representation->segments->len; ++s_i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, s_i);

                    /* Validate Segment Index */
                    if(segment->index_file_name && validateIndexSegment(segment->index_file_name,
                                1, &segment->duration, &iframe_data[s_i],
                                first_segment->start,
                                adaptation_set->video_pid, conformance_level & TS_TEST_SIMPLE) != 0) {
                        g_critical("Validation of SegmentIndexFile %s FAILED", segment->index_file_name);
                        overallStatus = 0;
                    }
                }

                /* Validate Representation Index */
                if (representation->index_file_name) {

                    uint64_t* expected_durations = malloc(representation->segments->len * sizeof(uint64_t));
                    for (gsize s_i = 0; s_i < representation->segments->len; ++s_i) {
                        segment_t* segment = g_ptr_array_index(representation->segments, s_i);
                        expected_durations[s_i] = segment->duration;
                    }
                    if (validateIndexSegment(representation->index_file_name,
                            representation->segments->len, expected_durations,
                            iframe_data, first_segment->start,
                            adaptation_set->video_pid,
                            conformance_level & TS_TEST_SIMPLE) != 0) {
                        g_critical("Validation of RepresentationIndex %s FAILED", PRINT_STR(representation->index_file_name));
                        overallStatus = 0;
                    }
                    free(expected_durations);
                }

                // Next, validate media files for each segment
                for (size_t s_i = 0; s_i < representation->segments->len; ++s_i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, s_i);
                    segment_t* previous_segment = s_i > 0 ? g_ptr_array_index(representation->segments, s_i - 1) : NULL;
                    int return_code = doSegmentValidation(&segment->validator, segment->file_name,
                            validator_init_segment,
                            &iframe_data[s_i], segment->duration);
                    if (return_code != 0) {
                        overallStatus = 0;
                        goto cleanup;
                    }

                    // GORP: what if there is no video in the segment??

                    // per PID
                    for(gsize pidIndex = 0; pidIndex < segment->validator.pids->len; pidIndex++) {
                        pid_validator_t* pv = g_ptr_array_index(segment->validator.pids, pidIndex);

                        // refine duration by including duration of last frame (audio and video are different rates)
                        // start time is relative to the start time of the first segment

                        // units of 90kHz ticks
                        int64_t actual_start = pv->EPT;
                        int64_t actual_duration = (pv->LPT - pv->EPT) + pv->duration;
                        int64_t actual_end = actual_start + actual_duration;

                        segment->actual_start[pv->content_component] = actual_start;
                        segment->actual_end[pv->content_component] = actual_end;

                        g_debug("%s: %04X: %s STARTTIME=%"PRId64", ENDTIME=%"PRId64", DURATION=%"PRId64"",
                                segment->file_name, pv->PID, content_component_to_string(pv->content_component),
                                actual_start, actual_end, actual_duration);

                        if(pv->content_component == VIDEO_CONTENT_COMPONENT) {
                            uint64_t previous_video_end = previous_segment ? previous_segment->actual_end[VIDEO_CONTENT_COMPONENT] : 0;
                            g_debug("%s: VIDEO: Last end time: %"PRId64", Current start time: %"PRId64", Delta: %"PRId64"",
                                          segment->file_name, previous_video_end, actual_start,
                                          actual_start - (int64_t)previous_video_end);

                            // check SAP and SAP Type
                            if(representation->start_with_sap != 0) {
                                if(pv->SAP == 0) {
                                    g_critical("%s: FAIL: SAP not set", segment->file_name);
                                    segment->validator.status = 0;
                                } else if(pv->SAP_type != representation->start_with_sap) {
                                    g_critical("%s: FAIL: Invalid SAP Type: expected %d, actual %d",
                                            segment->file_name, representation->start_with_sap, pv->SAP_type);
                                    segment->validator.status = 0;
                                }
                            } else {
                                g_debug("startWithSAP not set -- skipping SAP check");
                            }
                        } else if(pv->content_component == AUDIO_CONTENT_COMPONENT) {
                            uint64_t previous_audio_end = previous_segment ? previous_segment->actual_end[AUDIO_CONTENT_COMPONENT] : 0;
                            g_debug("%s: AUDIO: Last end time: %"PRId64", Current start time: %"PRId64", Delta: %"PRId64"",
                                    segment->file_name, previous_audio_end, actual_start,
                                    actual_start - (int64_t)previous_audio_end);
                        }

                        GLogLevelFlags log_level;
                        if (pv->content_component == VIDEO_CONTENT_COMPONENT) {
                            log_level = G_LOG_LEVEL_CRITICAL;
                        } else {
                            log_level = G_LOG_LEVEL_INFO;
                        }

                        if(actual_start != segment->start) {
                            if (pv->content_component == VIDEO_CONTENT_COMPONENT) {
                                segment->validator.status = 0;
                            } else if(pv->content_component == AUDIO_CONTENT_COMPONENT) {
                                /* Audio is allowed to be off? */
                            }
                            g_log(G_LOG_DOMAIN, log_level,
                                    "%s: %s: Invalid start time: expected = %"PRIu64", actual = %"PRIu64", delta = %"PRId64"",
                                    segment->file_name, content_component_to_string(pv->content_component),
                                    segment->start, actual_start, actual_start - (int64_t)segment->start);
                        }

                        if(actual_end != segment->end) {
                            if (pv->content_component == VIDEO_CONTENT_COMPONENT) {
                                segment->validator.status = 0;
                            } else if(pv->content_component == AUDIO_CONTENT_COMPONENT) {
                                /* Audio is allowed to be off? */
                            }
                            g_log(G_LOG_DOMAIN, log_level,
                                    "%s: %s: Invalid end time: expected %"PRId64", actual %"PRId64", delta = %"PRId64"",
                                    segment->file_name, content_component_to_string(pv->content_component),
                                    segment->end, actual_end, actual_end - (int64_t)segment->end);
                        }
                    }
                }
                g_critical(" ");

                /*
                for(int repIndex1 = 0; repIndex1 < numRepresentations; repIndex1++) {
                    int arrayIndex1 = getArrayIndex(repIndex1, segIndex, numSegments);
                    for(int repIndex2 = 0; repIndex2 < numRepresentations; repIndex2++) {
                        int arrayIndex2 = getArrayIndex(repIndex2, segIndex, numSegments);

                        if(lastAudioEndTime[arrayIndex1] == 0 || lastAudioEndTime[arrayIndex2] == 0 ||
                                lastVideoEndTime[arrayIndex1] == 0 || lastVideoEndTime[arrayIndex2] == 0) {
                            continue;
                        }

                        int64_t audioPTSDelta = actualAudioStartTime[arrayIndex2] - lastAudioEndTime[arrayIndex1];
                        int64_t videoPTSDelta = actualVideoStartTime[arrayIndex2] - lastVideoEndTime[arrayIndex1];

                        if(audioPTSDelta > maxAudioGapPTSTicks) {
                            LOG_INFO_ARGS("FAIL: Audio gap between (%d, %d) is %"PRId64" and exceeds limit %d",
                                          repIndex1, repIndex2, audioPTSDelta, maxAudioGapPTSTicks);
                            dash_validator[arrayIndex1].status = 0;
                            dash_validator[arrayIndex2].status = 0;
                        }

                        if(videoPTSDelta > maxVideoGapPTSTicks) {
                            LOG_INFO_ARGS("FAIL: Video gap between (%d, %d) is %"PRId64" and exceeds limit %d",
                                          repIndex1, repIndex2, videoPTSDelta, maxVideoGapPTSTicks);
                            dash_validator[arrayIndex1].status = 0;
                            dash_validator[arrayIndex2].status = 0;
                        }
                    }
                }*/

                for(gsize s_i = 0; s_i < representation->segments->len; ++s_i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, s_i);

                    if (segment->validator.status) {
                        g_info("SEGMENT TEST RESULT: %s: SUCCESS", segment->file_name);
                    } else {
                        g_critical("SEGMENT TEST RESULT: %s: FAIL", segment->file_name);
                        overallStatus = 0;
                    }
                }
                g_critical(" ");

                if(conformance_level & TS_TEST_SIMPLE) {
                    /* For the simple profile, the PSI must be the same for all Representations in an
                     * AdaptationSet */
                    /* TODO: Make this work correctly */
                    if(check_psi_identical(adaptation_set->representations) != 0) {
                        g_critical("Validation FAILED: PSI info not identical for all segments");
                        overallStatus = 0;
                    }
                }

                // print out results
                print_timing_matrix(representation->segments, AUDIO_CONTENT_COMPONENT);
                print_timing_matrix(representation->segments, VIDEO_CONTENT_COMPONENT);

                dash_validator_free(validator_init_segment);
                freeIFrames(iframe_data, representation->segments->len);
            }

            // segment cross checking: check that the gap between all adjacent segments is acceptably small
            print_gap_matrix(adaptation_set->representations, AUDIO_CONTENT_COMPONENT);
            print_gap_matrix(adaptation_set->representations, VIDEO_CONTENT_COMPONENT);
        }
    }

    printf("OVERALL TEST RESULT: %s\n", overallStatus ? "PASS" : "FAIL");
cleanup:
    mpd_free(mpd);
    xmlCleanupParser();
    return overallStatus != 0;
}

void print_gap_matrix(GPtrArray* representations, content_component_t content_component)
{
    if (representations->len == 0) {
        g_warning("Can't print gap matrix for empty set of representations.");
        return;
    }
    representation_t* first_representation = g_ptr_array_index(representations, 0);

    /* First figure out if there are any gaps, so we can be quieter if there are none */
    GLogLevelFlags flags = G_LOG_LEVEL_INFO;
    for (gsize s_i = 1; s_i < first_representation->segments->len; ++s_i) {
        for (gsize r_i = 0; r_i < representations->len; ++r_i) {
            representation_t* representation = g_ptr_array_index(representations, r_i);
            segment_t* segment1 = g_ptr_array_index(representation->segments, s_i - 1);

            for(gsize r_i2 = 0; r_i2 < representations->len; ++r_i2) {
                representation_t* representation2 = g_ptr_array_index(representations, r_i2);
                segment_t* segment2 = g_ptr_array_index(representation2->segments, s_i);

                int64_t pts_delta = segment2->actual_start[content_component] - (int64_t)segment1->actual_end[content_component];
                if (pts_delta) {
                    flags = G_LOG_LEVEL_WARNING;
                    goto found_delta;
                }
            }
        }
    }
found_delta:

    g_log(G_LOG_DOMAIN, flags, "%sGapMatrix", content_component_to_string(content_component));
    for (gsize s_i = 1; s_i < first_representation->segments->len; ++s_i) {
        GString* line = g_string_new("    \t");
        for (gsize r_i = 0; r_i < representations->len; ++r_i) {
            representation_t* representation = g_ptr_array_index(representations, r_i);
            segment_t* segment = g_ptr_array_index(representation->segments, s_i);
            g_string_append_printf(line, "%s\t", segment->file_name);
        }
        g_log(G_LOG_DOMAIN, flags, line->str);

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
            g_log(G_LOG_DOMAIN, flags, line->str);
        }
        g_log(G_LOG_DOMAIN, flags, " ");
        g_string_free(line, true);
    }
}

void print_timing_matrix(GPtrArray* segments, content_component_t content_component)
{
    if (segments->len == 0) {
        g_warning("Can't print timing matrix for empty set of segments.");
        return;
    }

    /* First figure out if there are any gaps, so we can be quieter if there are none */
    GLogLevelFlags flags = G_LOG_LEVEL_INFO;
    for(gsize i = 0; i < segments->len; ++i) {
        segment_t* segment = g_ptr_array_index(segments, i);
        int64_t delta_start = segment->actual_start[content_component] - (int64_t)segment->start;
        int64_t delta_end = segment->actual_end[content_component] - (int64_t)segment->end;
        if (delta_start || delta_end) {
            flags = G_LOG_LEVEL_WARNING;
            break;
        }
    }

    g_log(G_LOG_DOMAIN, flags, "%sTiming", content_component_to_string(content_component));
    g_log(G_LOG_DOMAIN, flags, "segmentFile\texpectedStart\texpectedEnd\tactualStart\tactualEnd\tdeltaStart\tdeltaEnd");
    for(gsize i = 0; i < segments->len; ++i) {
        segment_t* segment = g_ptr_array_index(segments, i);
        g_log(G_LOG_DOMAIN, flags,
               "%s\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64,
               segment->file_name, segment->start, segment->end,
               segment->actual_start[content_component], segment->actual_end[content_component],
               segment->actual_start[content_component] - segment->start,
               segment->actual_end[content_component] - segment->end);
    }
    g_log(G_LOG_DOMAIN, flags, " ");
}

// check that each segment has the same videoPID, audioPID, PMT version, PMT program num, and PCR PID
int check_psi_identical(GPtrArray* representations)
{
    if (representations->len == 0) {
        g_warning("Can't check PSI for empty set of representations.");
        return 1;
    }

    g_info("Validating that PSI info is identical in each segment\n");
    int status = 0; // 0=PASS, 1-FAIL

    int video_pid;
    int audio_pid;
    int pcr_pid;
    uint32_t pmt_program_number;
    uint32_t pmt_version_number;

    for (gsize r_i = 0; r_i < representations->len; ++r_i) {
        representation_t* representation = g_ptr_array_index(representations, r_i);
        for (gsize s_i = 0; s_i < representation->segments->len; ++s_i) {
            segment_t* segment = g_ptr_array_index(representation->segments, s_i);
            dash_validator_t* validator = &segment->validator;

            if (r_i == 0 && s_i == 0) {
                video_pid = validator->videoPID;
                audio_pid = validator->audioPID;
                pcr_pid = validator->PCR_PID;
                pmt_program_number = validator->pmt_program_number;
                pmt_version_number = validator->pmt_version_number;
            } else {
                if (video_pid != validator->videoPID) {
                    g_critical("PSI Table Validation FAILED: Incorrect videoPID: Expected = %d, Actual = %d",
                            video_pid, validator->videoPID);
                    status = -1;
                }
                if (audio_pid != validator->audioPID) {
                    g_critical("PSI Table Validation FAILED: Incorrect audioPID: Expected = %d, Actual = %d",
                            audio_pid, validator->audioPID);
                    status = -1;
                }
                if (pcr_pid != validator->PCR_PID) {
                    g_critical("PSI Table Validation FAILED: Incorrect PCR_PID: Expected = %d, Actual = %d",
                            pcr_pid, validator->PCR_PID);
                    status = -1;
                }
                if (pmt_program_number != validator->pmt_program_number) {
                    g_critical("PSI Table Validation FAILED: Incorrect pmt_program_number: Expected = %d, Actual = %d",
                            pmt_program_number, validator->pmt_program_number);
                    status = -1;
                }
                if (pmt_version_number != validator->pmt_version_number) {
                    g_critical("PSI Table Validation FAILED: Incorrect pmt_version_number: Expected = %d, Actual = %d",
                            pmt_version_number, validator->pmt_version_number);
                    status = -1;
                }
            }
        }
    }
    return status;
}
