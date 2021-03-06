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
bool check_segment_psi_identical(const char* file_name1, dash_validator_t*, const char* file_name2, dash_validator_t*);
bool check_psi_identical(GPtrArray* representations);

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
    mpd_t* mpd = mpd_read_file(file_name);
    if (mpd == NULL) {
        g_critical("Error: Failed to read MPD.");
        goto cleanup;
    }
    mpd_print(mpd);

    for (size_t p_i = 0; p_i < mpd->periods->len; ++p_i) {
        period_t* period = g_ptr_array_index(mpd->periods, p_i);
        for (size_t a_i = 0; a_i < period->adaptation_sets->len; ++a_i) {
            adaptation_set_t* adaptation_set = g_ptr_array_index(period->adaptation_sets, a_i);
            if (adaptation_set->mime_type && strcmp(adaptation_set->mime_type, "video/mp2t")
                    && strcmp(adaptation_set->mime_type, "audio/mp2t")) {
                g_warning("Ignoring Adaptation Set %"PRIu32" because MIME type \"%s\" does not match \"video/mp2t\" "
                        "or \"audio/mp2t\".",
                        adaptation_set->id, adaptation_set->mime_type);
                continue;
            }
            bool adaptation_set_valid = true;

            g_print("VALIDATING ADAPTATION SET: %"PRIu32"\n", adaptation_set->id);
            GPtrArray* validated_representations = g_ptr_array_new();
            for (size_t r_i = 0; r_i < adaptation_set->representations->len; ++r_i) {
                representation_t* representation = g_ptr_array_index(adaptation_set->representations, r_i);
                if (representation->mime_type && strcmp(representation->mime_type, "video/mp2t")
                        && strcmp(representation->mime_type, "audio/mp2t")) {
                    g_warning("Ignoring Representation %s because because MIME type \"%s\" does not match "
                            "\"video/mp2t\" or \"audio/mp2t\".",
                            representation->id, representation->mime_type);
                    continue;
                }
                g_ptr_array_add(validated_representations, representation);

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
                    if (validate_segment(validator_init_segment, representation->initialization_file_name,
                            representation->initialization_range_start,
                            representation->initialization_range_end, NULL) != 0) {
                        validator_init_segment->status = 0;
                    }
                    g_print("INITIALIZATION SEGMENT TEST RESULT: %s: %s\n", representation->initialization_file_name,
                            validator_init_segment->status ? "SUCCESS" : "FAIL");
                    representation_valid &= validator_init_segment->status;
                }

                /* Validate Bitstream Switching Segment */
                if (representation->bitstream_switching_file_name) {
                    dash_validator_t* validator = dash_validator_new(BITSTREAM_SWITCHING_SEGMENT,
                            representation->profile);
                    if (validate_segment(validator, representation->bitstream_switching_file_name,
                            representation->bitstream_switching_range_start,
                            representation->bitstream_switching_range_end, validator_init_segment) != 0) {
                        validator->status = 0;
                    }
                    if (!check_segment_psi_identical(representation->initialization_file_name, validator_init_segment,
                            representation->bitstream_switching_file_name, validator)) {
                        g_critical("DASH Conformance: PSI in bitstream switching segment does not match PSI in "
                                "initialization segment. 6.4.5 Bitstream Switching Segment: If initialization "
                                "information is carried within a Bitstream Switching Segment, it shall be identical "
                                "to the one in the Initialization Segment, if present, of the Representation.");
                        validator->status = 0;
                    }
                    g_print("BITSTREAM SWITCHING SEGMENT TEST RESULT: %s: %s\n",
                            representation->bitstream_switching_file_name, validator->status ? "SUCCESS" : "FAIL");
                    representation_valid &= validator->status;
                    dash_validator_free(validator);
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
                                "%u segments and %u segment_subsegments", representation->segments->len,
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
                        g_print("SINGLE SEGMENT INDEX TEST RESULT: %s: %s\n", segment->index_file_name,
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

                    if (!segment->index_file_name && !representation->index_file_name
                            && representation->subrepresentations->len > 0) {
                        g_critical("DASH Conformance: Segment %s has no index segment, but there is a "
                                "SubRepresentation present. 7.4.4 Sub-Representations: The Subsegment Index box shall contain "
                                "at least one entry for the value of SubRepresentation@level and for each value provided in "
                                "the SubRepresentation@dependencyLevel.", segment->file_name);
                        representation_valid = false;
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

                            uint8_t expected_sap = representation->start_with_sap;
                            if (adaptation_set->bitstream_switching && (expected_sap == 0 || expected_sap > 2)) {
                                expected_sap = 3;
                            }
                            if (expected_sap != 0 && pv->content_component == VIDEO_CONTENT_COMPONENT) {
                                bool fail = false;
                                if (pv->sap == 0) {
                                    g_critical("DASH Conformance: Missing SAP in segment %s PID %"PRIu16". "
                                            "Expected SAP_type <= %d, actual (none). Table 9 - Common Adaptation Set, "
                                            "Representation and Sub-Representation attributes and elements: "
                                            "@startWithSAP: when present and greater than 0, specifies that in the "
                                            "associated Representations, each Media Segment starts with a SAP of "
                                            "type less than or equal to the value of this attribute value in each "
                                            "media stream.",
                                            segment->file_name, pv->pid, expected_sap);
                                    fail = true;
                                } else if (pv->sap > expected_sap) {
                                    g_critical("DASH Conformance: Invalid SAP Type in segment %s PID %"PRIu16". "
                                            "Expected SAP_type <= %d, actual %d. Table 9 — Common Adaptation Set, "
                                            "Representation and Sub-Representation attributes and elements: "
                                            "@startWithSAP: when present and greater than 0, specifies that in the "
                                            "associated Representations, each Media Segment starts with a SAP of "
                                            "type less than or equal to the value of this attribute value in each "
                                            "media stream.",
                                            segment->file_name, pv->pid, expected_sap, pv->sap_type);
                                    fail = true;
                                }
                                if (fail) {
                                    if (adaptation_set->bitstream_switching) {
                                        g_critical("7.3.3.2 Bitstream switching: The conditions required for setting "
                                                "(i) the @startWithSAP attribute to 2 for the Adaptation Set, or (ii) "
                                                "the conditions required for all Representations within the "
                                                "Adaptation Set to share the same value of @mediaStreamStructureId "
                                                "and setting the @startWithSAP attribute to 3 for the Adaptation Set, "
                                                "are fulfilled.");
                                    }
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

            if (adaptation_set->bitstream_switching) {
                for (gsize x = 0; x < validated_representations->len; ++x) {
                    representation_t* representation_x = g_ptr_array_index(validated_representations, x);
                    const char* file_names[4];
                    uint64_t byte_starts[4];
                    uint64_t byte_ends[4];
                    size_t f_i = 0;
                    if (representation_x->initialization_file_name) {
                        file_names[f_i] = representation_x->initialization_file_name;
                        byte_starts[f_i] = representation_x->initialization_range_start;
                        byte_ends[f_i] = representation_x->initialization_range_end;
                        f_i++;
                    }
                    for (gsize s_i = 0; s_i + 1 < representation_x->segments->len; ++s_i) {
                        segment_t* segment_x = g_ptr_array_index(representation_x->segments, s_i);
                        file_names[f_i] = segment_x->file_name;
                        byte_starts[f_i] = segment_x->media_range_start;
                        byte_ends[f_i] =  segment_x->media_range_end;
                        for (gsize y = 0; y < validated_representations->len; ++y) {
                            if (x == y) {
                                continue;
                            }
                            representation_t* representation_y = g_ptr_array_index(validated_representations, y);
                            if (representation_y->segments->len != representation_x->segments->len) {
                                g_critical("Representations %s and %s are in the same adaptation set and have "
                                        "bitstream switching set, but don't have the same number of segments.",
                                        representation_x->id, representation_y->id);
                                adaptation_set_valid = false;
                                break;
                            }
                            g_info("Testing bitstream switching from representation %s segment %"G_GSIZE_FORMAT
                                    " to %s segment %"G_GSIZE_FORMAT".",
                                    representation_x->id, s_i, representation_y->id, s_i + 1);
                            size_t f_j = f_i + 1;
                            if (representation_y->bitstream_switching_file_name) {
                                file_names[f_j] = representation_y->bitstream_switching_file_name;
                                byte_starts[f_j] = representation_y->bitstream_switching_range_start;
                                byte_ends[f_j] = representation_y->bitstream_switching_range_end;
                                f_j++;
                            }
                            segment_t* segment_y = g_ptr_array_index(representation_y->segments, s_i + 1);
                            file_names[f_j] = segment_y->file_name;
                            byte_starts[f_j] = segment_y->media_range_start;
                            byte_ends[f_j] =  segment_y->media_range_end;
                            if (!validate_bitstream_switching(file_names, byte_starts, byte_ends, f_j + 1)) {
                                g_critical("DASH Conformance: Error parsing TS packet in segments. 7.4.3.4 Bitstream "
                                        "switching: If @bitstreamSwitching flag is set to 'true' the Bitstream Switching Segment "
                                        "may be present, indicated by BitstreamSwitching in the Segment Information. In this "
                                        "case, for any two Representations, X and Y, within the same Adaptation Set, "
                                        "concatenation of Media Segment i of X, Bitstream Switching Segment of Representation Y, "
                                        "and Media Segment i+1 of Representation Y shall be a MPEG-2 TS conforming to ISO/IEC "
                                        "13818-1.");
                                g_critical("Segments concatenated for this test:");
                                for (size_t i = 0; i < f_j + 1; ++i) {
                                    g_critical("%s", file_names[i]);
                                }
                            }
                        }
                    }
                }
            }

            // segment cross checking: check that the gap between all adjacent segments is acceptably small
            if (validated_representations->len) {
                adaptation_set_valid &= check_representation_gaps(validated_representations,
                        AUDIO_CONTENT_COMPONENT, max_gap_pts_ticks[AUDIO_CONTENT_COMPONENT]);
                adaptation_set_valid &= check_representation_gaps(validated_representations,
                        VIDEO_CONTENT_COMPONENT, max_gap_pts_ticks[VIDEO_CONTENT_COMPONENT]);
            }

            /* For the simple profile, the PSI must be the same for all Representations in an
             * AdaptationSet */
            if(adaptation_set->profile >= DASH_PROFILE_MPEG2TS_SIMPLE
                    && !check_psi_identical(validated_representations)) {
                g_critical("DASH Conformance: PSI info not identical for all segments in AdaptationSet with "
                        "profile=\"urn:mpeg:dash:profile:mp2t-simple:2011\". 8.7.3 Segment format constraints: PSI "
                        "information, including versions, shall be identical within all Representations contained in an "
                        "AdaptationSet;\n");
                adaptation_set_valid = false;
            }
            g_ptr_array_free(validated_representations, true);

            g_print("ADAPTATION SET TEST RESULT: %"PRIu32": %s\n", adaptation_set->id,
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
    g_return_val_if_fail(representations, 0);

    if (representations->len == 0) {
        g_warning("Can't print gap matrix for empty set of representations.");
        return 1;
    }
    int status = 1;
    representation_t* first_representation = g_ptr_array_index(representations, 0);

    /* First figure out if there are any gaps, so we can be quieter if there are none */
    GLogLevelFlags flags = G_LOG_LEVEL_INFO;
    for (gsize s_i = 1; s_i < first_representation->segments->len; ++s_i) {
        for (gsize r_i = 0; r_i < representations->len; ++r_i) {
            representation_t* representation1 = g_ptr_array_index(representations, r_i);
            segment_t* segment1 = g_ptr_array_index(representation1->segments, s_i - 1);
            dash_validator_t* dv1 = segment1->arg;
            if (!dv1) {
                g_critical("Attempting to check representation gaps on representations that haven't be validated!");
                return 0;
            }
            if (dv1->is_encrypted) {
                continue;
            }

            for(gsize r_i2 = 0; r_i2 < representations->len; ++r_i2) {
                representation_t* representation2 = g_ptr_array_index(representations, r_i2);
                segment_t* segment2 = g_ptr_array_index(representation2->segments, s_i);
                dash_validator_t* dv2 = segment1->arg;
                if (!dv2) {
                    g_critical("Attempting to check representation gaps on representations that haven't be validated!");
                    return 0;
                }
                if (dv2->is_encrypted) {
                    continue;
                }

                int64_t pts_delta = segment2->actual_start[content_component] - \
                        (int64_t)segment1->actual_end[content_component];
                if (pts_delta) {
                    flags = G_LOG_LEVEL_WARNING;
                    if (pts_delta > max_delta) {
                        g_critical("FAIL: %s gap between for segment %zu for representations %s and %s is %"PRId64" "
                                "and exceeds limit %"PRId64,
                                content_component_to_string(content_component), s_i, representation1->id,
                                representation2->id, pts_delta, max_delta);
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

bool check_segment_psi_identical(const char* f1, dash_validator_t* v1, const char* f2, dash_validator_t* v2)
{
    g_return_val_if_fail(v1 != NULL, false);
    g_return_val_if_fail(v2 != NULL, false);

    bool identical = true;
    if (!program_association_section_equal(v1->pat, v2->pat)) {
        g_warning("PAT in segments %s and %s are not identical.", f1, f2);
        identical = false;
    }
    if (!program_map_section_equal(v1->pmt, v2->pmt)) {
        g_warning("PMT in segments %s and %s are not identical.", f1, f2);
        identical = false;
    }
    if (!conditional_access_section_equal(v1->cat, v2->cat)) {
        g_warning("CAT in segments %s and %s are not identical.", f1, f2);
        identical = false;
    }
    return identical;
}

bool check_psi_identical(GPtrArray* representations)
{
    g_return_val_if_fail(representations->len != 0, false);

    g_info("Validating that PSI info is identical in each segment\n");
    bool identical = true;
    segment_t* reference = g_ptr_array_index(((representation_t*)g_ptr_array_index(representations, 0))->segments, 0);
    for (gsize r_i = 0; r_i < representations->len; ++r_i) {
        representation_t* representation = g_ptr_array_index(representations, r_i);
        for (gsize s_i = 0; s_i < representation->segments->len; ++s_i) {
            if (r_i == 0 && s_i == 0) {
                continue;
            }
            segment_t* current = g_ptr_array_index(representation->segments, s_i);

            if (!check_segment_psi_identical(reference->file_name, reference->arg, current->file_name, current->arg)) {
                identical = false;
            }
        }
    }
    return identical;
}
