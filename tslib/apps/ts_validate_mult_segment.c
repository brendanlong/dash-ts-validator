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


#define SEGMENT_FILE_NAME_MAX_LENGTH    512 


void printAudioGapMatrix(int numRepresentations, int numSegments, int64_t* lastVideoEndTime,
                         int64_t* actualVideoStartTime,
                         int segNum, char* segFileNames);
void printVideoGapMatrix(int numRepresentations, int numSegments, int64_t* lastVideoEndTime,
                         int64_t* actualVideoStartTime,
                         int segNum, char* segFileNames);

void printVideoTimingMatrix(char* segFileNames, int64_t* actualVideoStartTime,
                            int64_t* actualVideoEndTime,
                            int64_t* expectedStartTime, int64_t* expectedEndTime, int numSegments, int numRepresentations,
                            int repNum);
void printAudioTimingMatrix(char* segFileNames, int64_t* actualAudioStartTime,
                            int64_t* actualAudioEndTime,
                            int64_t* expectedStartTime, int64_t* expectedEndTime, int numSegments, int numRepresentations,
                            int repNum);

int checkPSIIdentical(dash_validator_t* dash_validator, int numDashValidators);
int getArrayIndex(int repNum, int segNum, int numSegments);

static struct option long_options[] = {
    { "verbose",	   no_argument,        NULL, 'v' },
    { "dash",	   optional_argument,  NULL, 'd' },
    { "help",       no_argument,        NULL, 'h' },
};

static char options[] =
    "\t-d, --dash\n"
    "\t-v, --verbose\n"
    "\t-h, --help\n";

static void usage(char* name)
{
    fprintf(stderr, "\n%s\n", name);
    fprintf(stderr, "\nUsage: \n%s [options] <input file with segment info>\n\nOptions:\n%s\n", name,
            options);
}

int main(int argc, char* argv[])
{
    int c, long_options_index;
    extern char* optarg;
    extern int optind;

    uint32_t conformance_level;

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

    // read segment_info file (tab-delimited) which lists segment file paths
    char* file_name = argv[optind];
    if(file_name == NULL || file_name[0] == 0) {
        g_critical("No segment_info file provided");
        usage(argv[0]);
        return 1;
    }

    // for all the 2D arrays in the following, see getArrayIndex for array ordering
    int overallStatus = 1;    // overall pass/fail, with 1=PASS, 0=FAIL

    int returnCode = 0;
    mpd_t* mpd = read_mpd(file_name);
    if (mpd == NULL) {
        goto cleanup;
    }
    mpd_dump(mpd);

    dash_validator_t* dash_validator_init_segment = calloc(1, sizeof(dash_validator_t));
    dash_validator_init_segment->segment_type = INITIALIZATION_SEGMENT;

    /*
    char* content_component_table[NUM_CONTENT_COMPONENTS] =
    { "<unknown>", "video", "audio" };
    */

    // if there is an initialization segment, process it first in order to get the PAT and PMT tables
    for (size_t p_i = 0; p_i < mpd->periods->len; ++p_i) {
        period_t* period = g_ptr_array_index(mpd->periods, p_i);
        for (size_t a_i = 0; a_i < period->adaptation_sets->len; ++a_i) {
            adaptation_set_t* adaptation_set = g_ptr_array_index(period->adaptation_sets, a_i);
            for (size_t r_i = 0; r_i < adaptation_set->representations->len; ++r_i) {
                representation_t* representation = g_ptr_array_index(adaptation_set->representations, r_i);
                if (representation->initialization_file_name == NULL) {
                    continue;
                }
                if(doSegmentValidation(dash_validator_init_segment, representation->initialization_file_name,
                                       NULL, NULL, 0) != 0) {
                    g_critical("Validation of initialization segment %s FAILED.", representation->initialization_file_name);
                    dash_validator_init_segment->status = 0;
                }
                if(dash_validator_init_segment->status == 0) {
                    overallStatus = 0;
                }
            }
        }
    }

    // next process index files, either representation index files or single segment index files
    // the index files contain iFrame locations which are stored here and then validated when the actual
    // media segments are processed later
    for (size_t p_i = 0; p_i < mpd->periods->len; ++p_i) {
        period_t* period = g_ptr_array_index(mpd->periods, p_i);
        for (size_t a_i = 0; a_i < period->adaptation_sets->len; ++a_i) {
            adaptation_set_t* adaptation_set = g_ptr_array_index(period->adaptation_sets, a_i);
            for (size_t r_i = 0; r_i < adaptation_set->representations->len; ++r_i) {
                representation_t* representation = g_ptr_array_index(adaptation_set->representations, r_i);
                if (representation->index_file_name == NULL) {
                    continue;
                }
                int* segment_durations = malloc(sizeof(int) * representation->segments->len);
                uint64_t first_segment_start = 0;
                for (size_t s_i = 0; s_i < representation->segments->len; ++s_i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, s_i);
                    if (s_i == 0) {
                        first_segment_start = segment->start;
                    }
                    segment_durations[s_i] = segment->duration;

                    if(segment->index_file_name) {
                        int duration = segment->duration;
                        if (validateIndexSegment(segment->index_file_name, 1, &duration,
                                &representation->segment_iframes[s_i],
                                representation->presentation_time_offset + first_segment_start,
                                adaptation_set->video_pid, conformance_level & TS_TEST_SIMPLE) != 0) {
                            g_critical("Validation of SegmentIndexFile %s FAILED", segment->index_file_name);
                            overallStatus = 0;
                        }
                    }
                }
                if (validateIndexSegment(representation->index_file_name,
                        representation->segments->len, segment_durations,
                        representation->segment_iframes, representation->presentation_time_offset + first_segment_start,
                        adaptation_set->video_pid,
                        conformance_level & TS_TEST_SIMPLE) != 0) {
                    g_critical("Validation of RepresentationIndex %s FAILED", PRINT_STR(representation->index_file_name));
                    overallStatus = 0;
                }
                free(segment_durations);
            }
        }
    }

    /*
    if(strlen(&(representationIndexFileNames[0])) != 0) {
        for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
            data_segment_iframes_t* pIFramesTemp = pIFrames + getArrayIndex(repIndex, 0 ,
                                                   numSegments);
            if(validateIndexSegment(representationIndexFileNames + repIndex * SEGMENT_FILE_NAME_MAX_LENGTH,
                                    numSegments, segDurations, pIFramesTemp, presentationTimeOffset, videoPID,
                                    conformance_level & TS_TEST_SIMPLE) != 0) {
                g_critical("Validation of RepresentationIndexFile %s FAILED",
                               representationIndexFileNames + repIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
                overallStatus = 0;
            }
        }
    } else if(strlen(segmentIndexFileNames) != 0) {
        // read single-segment index files and fill in pIFrames
        for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
            for(int segIndex = 0; segIndex < numSegments; segIndex++) {
                int arrayIndex = getArrayIndex(repIndex, segIndex, numSegments);
                data_segment_iframes_t* pIFramesTemp = pIFrames + arrayIndex;

                char* segmentIndexFileName = segmentIndexFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH;
                if(validateIndexSegment(segmentIndexFileName, 1, &(segDurations[segIndex]), pIFramesTemp,
                                        presentationTimeOffset, videoPID, conformance_level & TS_TEST_SIMPLE) != 0) {
                    g_critical("Validation of SegmentIndexFile %s FAILED", segmentIndexFileName);
                    overallStatus = 0;
                }
            }
        }
    }

    // Next, validate media files for each segment
    // store timing info for each segment so that segment-segment timing can be tested
    for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
        expectedStartTime[getArrayIndex(repIndex, 0, numSegments + 1)] = presentationTimeOffset;
    }

    char* segFileName = NULL;
    for(int segIndex = 0; segIndex < numSegments; segIndex++) {
        for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
            int arrayIndex = getArrayIndex(repIndex, segIndex, numSegments);
            if(segIndex == 0) {
                expectedStartTime[arrayIndex] = presentationTimeOffset;
            }

            dash_validator[arrayIndex].conformance_level = conformance_level;

            segFileName = segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH;
//          g_info("\nsegFileName = %s", segFileName);

            data_segment_iframes_t* pIFramesTemp = pIFrames + arrayIndex;

            if(strlen(initializationSegment) != 0) {
                dash_validator[arrayIndex].use_initialization_segment = true;

                returnCode = doSegmentValidation(&(dash_validator[arrayIndex]), segFileName,
                                                 dash_validator_init_segment, pIFramesTemp, segDurations[segIndex]);
            } else {
                returnCode = doSegmentValidation(&(dash_validator[arrayIndex]), segFileName,
                                                 NULL, pIFramesTemp, segDurations[segIndex]);
            }

            if(returnCode != 0) {
                goto cleanup;
            }

            // GORP: what if there is no video in the segment??
            pid_validator_t* pv = NULL;

            expectedDuration[arrayIndex] = segDurations[segIndex];
            expectedEndTime[arrayIndex] = expectedStartTime[arrayIndex] + expectedDuration[arrayIndex];

            // per PID
            for(int pidIndex = 0; pidIndex < vqarray_length(dash_validator[arrayIndex].pids); pidIndex++) {
                pv = (pid_validator_t*)vqarray_get(dash_validator[arrayIndex].pids, pidIndex);
                if(pv == NULL) {
                    continue;
                }

                // refine duration by including duration of last frame (audio and video are different rates)
                // start time is relative to the start time of the first segment

                // units of 90kHz ticks
                int64_t actualStartTime = pv->EPT;
                int64_t actualDuration = (pv->LPT - pv->EPT) + pv->duration;
                int64_t actualEndTime = actualStartTime + actualDuration;

                g_info("%s: %04X: %s STARTTIME=%"PRId64", ENDTIME=%"PRId64", DURATION=%"PRId64"",
                              segFileName,
                              pv->PID, content_component_table[pv->content_component], actualStartTime, actualEndTime,
                              actualDuration);

                if(pv->content_component == VIDEO_CONTENT_COMPONENT) {
                    g_info("%s: VIDEO: Last end time: %"PRId64", Current start time: %"PRId64", Delta: %"PRId64"",
                                  segFileName, lastVideoEndTime[arrayIndex], actualStartTime,
                                  actualStartTime - lastVideoEndTime[arrayIndex]);

                    actualVideoStartTime[arrayIndex] = actualStartTime;
                    actualVideoEndTime[arrayIndex] = actualEndTime;

                    // check SAP and SAP Type
                    if(expectedSAPType != 0) {
                        if(pv->SAP == 0) {
                            g_info("%s: FAIL: SAP not set", segFileName);
                            dash_validator[arrayIndex].status = 0;
                        } else if(pv->SAP_type != expectedSAPType) {
                            LOG_g_info_ARGS("%s: FAIL: Invalid SAP Type: expected %d, actual %d",
                                          segFileName, expectedSAPType, pv->SAP_type);
                            dash_validator[arrayIndex].status = 0;
                        }
                    } else {
                        g_info("startWithSAP not set -- skipping SAP check");
                    }
                } else if(pv->content_component == AUDIO_CONTENT_COMPONENT) {
                    g_info("%s: AUDIO: Last end time: %"PRId64", Current start time: %"PRId64", Delta: %"PRId64"",
                                  segFileName, lastAudioEndTime[arrayIndex], actualStartTime,
                                  actualStartTime - lastAudioEndTime[arrayIndex]);

                    actualAudioStartTime[arrayIndex] = actualStartTime;
                    actualAudioEndTime[arrayIndex] = actualEndTime;
                }


                if(actualStartTime != expectedStartTime[arrayIndex]) {
                    g_info("%s: %s: Invalid start time: expected = %"PRId64", actual = %"PRId64", delta = %"PRId64"",
                                  segFileName, content_component_table[pv->content_component],
                                  expectedStartTime[arrayIndex], actualStartTime, actualStartTime - expectedStartTime[arrayIndex]);

                    if(pv->content_component == VIDEO_CONTENT_COMPONENT) {
                        dash_validator[arrayIndex].status = 0;
                    } else if(pv->content_component == AUDIO_CONTENT_COMPONENT) {
                    }
                }

                if(actualEndTime != expectedEndTime[arrayIndex]) {
                    g_info("%s: %s: Invalid end time: expected %"PRId64", actual %"PRId64", delta = %"PRId64"",
                                  segFileName, content_component_table[pv->content_component],
                                  expectedEndTime[arrayIndex], actualEndTime, actualEndTime - expectedEndTime[arrayIndex]);

                    if(pv->content_component == VIDEO_CONTENT_COMPONENT) {
                        dash_validator[arrayIndex].status = 0;
                    } else if(pv->content_component == AUDIO_CONTENT_COMPONENT) {
                    }
                }
            }

            // fill in expected start time for next segment by setting it to the end time of this segment
            if(segIndex != numSegments - 1) {
                expectedStartTime[getArrayIndex(repIndex, segIndex + 1, numSegments)] = expectedEndTime[arrayIndex];
            }
        }

        // segment cross checking: check that the gap between all adjacent segments is acceptably small

        printAudioGapMatrix(numRepresentations, numSegments, lastAudioEndTime, actualAudioStartTime,
                            segIndex, segFileNames);
        printVideoGapMatrix(numRepresentations, numSegments, lastVideoEndTime, actualVideoStartTime,
                            segIndex, segFileNames);
        printf("\n");

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
                    g_info("FAIL: Audio gap between (%d, %d) is %"PRId64" and exceeds limit %d",
                                  repIndex1, repIndex2, audioPTSDelta, maxAudioGapPTSTicks);
                    dash_validator[arrayIndex1].status = 0;
                    dash_validator[arrayIndex2].status = 0;
                }

                if(videoPTSDelta > maxVideoGapPTSTicks) {
                    g_info("FAIL: Video gap between (%d, %d) is %"PRId64" and exceeds limit %d",
                                  repIndex1, repIndex2, videoPTSDelta, maxVideoGapPTSTicks);
                    dash_validator[arrayIndex1].status = 0;
                    dash_validator[arrayIndex2].status = 0;
                }
            }
        }

        for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
            int arrayIndex0 = getArrayIndex(repIndex, segIndex, numSegments);
            int arrayIndex1 = getArrayIndex(repIndex, segIndex + 1, numSegments);
            lastAudioEndTime[arrayIndex1] = actualAudioEndTime[arrayIndex0];
            lastVideoEndTime[arrayIndex1] = actualVideoEndTime[arrayIndex0];

            g_info("SEGMENT TEST RESULT: %s: %s",
                          segFileNames + arrayIndex0 * SEGMENT_FILE_NAME_MAX_LENGTH,
                          dash_validator[arrayIndex0].status ? "PASS" : "FAIL");
        }

        printf("\n");
    }

    if(conformance_level & TS_TEST_SIMPLE) {
        // for a simple profile, the PSI info must be the same for all segments
        if(checkPSIIdentical(dash_validator, numRepresentations * numSegments) != 0) {
            g_critical("Validation FAILED: PSI info not identical for all segments");
            overallStatus = 0;
        }
    }


    // print out results
    for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
        printAudioTimingMatrix(segFileNames, actualAudioStartTime, actualAudioEndTime,
                               expectedStartTime, expectedEndTime, numSegments, numRepresentations, repIndex);
    }
    for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
        printVideoTimingMatrix(segFileNames, actualVideoStartTime, actualVideoEndTime,
                               expectedStartTime, expectedEndTime, numSegments, numRepresentations, repIndex);
    }

    // get overall pass/fail status: 1=PASS, 0=FAIL
    for(int i = 0; i < numRepresentations * numSegments; i++) {
        overallStatus = overallStatus && dash_validator[i].status;
    }*/
    printf("\nOVERALL TEST RESULT: %s\n", overallStatus ? "PASS" : "FAIL");
cleanup:
    free(dash_validator_init_segment);
    mpd_free(mpd);
    xmlCleanupParser();
    return returnCode;
}

int getArrayIndex(int repNum, int segNum, int numSegments)
{
    return (segNum + repNum * numSegments);
}

void printAudioGapMatrix(int numRepresentations, int numSegments, int64_t* lastAudioEndTime,
                         int64_t* actualAudioStartTime,
                         int segNum, char* segFileNames)
{
    if(segNum == 0) {
        return;
    }

    printf("\nAudioGapMatrix\n");
    printf("    \t");
    for(int repNum = 0; repNum < numRepresentations; repNum++) {
        int arrayIndex = getArrayIndex(repNum, segNum, numSegments);
        printf("%s\t", segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
    }
    printf("\n");

    for(int repNum1 = 0; repNum1 < numRepresentations; repNum1++) {
        int arrayIndex0 = getArrayIndex(repNum1, segNum - 1, numSegments);
        int arrayIndex1 = getArrayIndex(repNum1, segNum, numSegments);

        printf("%s\t", segFileNames + arrayIndex0 * SEGMENT_FILE_NAME_MAX_LENGTH);

        for(int repNum2 = 0; repNum2 < numRepresentations; repNum2++) {
            int arrayIndex2 = getArrayIndex(repNum2, segNum, numSegments);
            if(lastAudioEndTime[arrayIndex1] == 0 || lastAudioEndTime[arrayIndex2] == 0) {
                continue;
            }

            int64_t audioPTSDelta = actualAudioStartTime[arrayIndex2] - lastAudioEndTime[arrayIndex1];
            printf("%"PRId64"\t", audioPTSDelta);
        }

        printf("\n");
    }
}

void printVideoGapMatrix(int numRepresentations, int numSegments, int64_t* lastVideoEndTime,
                         int64_t* actualVideoStartTime,
                         int segNum, char* segFileNames)
{
    if(segNum == 0) {
        return;
    }

    printf("\nVideoGapMatrix\n");
    printf("    \t");
    for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
        int arrayIndex = getArrayIndex(repIndex, segNum, numSegments);
        printf("%s\t", segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
    }
    printf("\n");

    for(int repIndex1 = 0; repIndex1 < numRepresentations; repIndex1++) {
        int arrayIndex0 = getArrayIndex(repIndex1, segNum - 1, numSegments);
        int arrayIndex1 = getArrayIndex(repIndex1, segNum, numSegments);

        printf("%s\t", segFileNames + arrayIndex0 * SEGMENT_FILE_NAME_MAX_LENGTH);

        for(int repIndex2 = 0; repIndex2 < numRepresentations; repIndex2++) {
            int arrayIndex2 = getArrayIndex(repIndex2, segNum, numSegments);

            if(lastVideoEndTime[arrayIndex1] == 0 || lastVideoEndTime[arrayIndex2] == 0) {
                continue;
            }

            int64_t videoPTSDelta = actualVideoStartTime[arrayIndex2] - lastVideoEndTime[arrayIndex1];
            printf("%"PRId64"\t", videoPTSDelta);
        }

        printf("\n");
    }
}

void printVideoTimingMatrix(char* segFileNames, int64_t* actualVideoStartTime,
                            int64_t* actualVideoEndTime,
                            int64_t* expectedStartTime, int64_t* expectedEndTime, int numSegments, int numRepresentations,
                            int repNum)
{
    printf("\nVideoTiming\n");
    printf("segmentFile\texpectedStart\texpectedEnd\tvideoStart\tvideoEnd\tdeltaStart\tdeltaEnd\n");

    for(int segIndex = 0; segIndex < numSegments; segIndex++) {
        int arrayIndex = getArrayIndex(repNum, segIndex, numSegments);
        printf("%s\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\n",
               segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH,
               expectedStartTime[arrayIndex], expectedEndTime[arrayIndex], actualVideoStartTime[arrayIndex],
               actualVideoEndTime[arrayIndex],
               actualVideoStartTime[arrayIndex] - expectedStartTime[arrayIndex],
               actualVideoEndTime[arrayIndex] - expectedEndTime[arrayIndex]);
    }
}

void printAudioTimingMatrix(char* segFileNames, int64_t* actualAudioStartTime,
                            int64_t* actualAudioEndTime,
                            int64_t* expectedStartTime, int64_t* expectedEndTime, int numSegments, int numRepresentations,
                            int repNum)
{
    printf("\nAudioTiming\n");
    printf("segmentFile\texpectedStart\texpectedEnd\taudioStart\taudioEnd\tdeltaStart\tdeltaEnd\n");

    for(int segIndex = 0; segIndex < numSegments; segIndex++) {
        int arrayIndex = getArrayIndex(repNum, segIndex, numSegments);
        printf("%s\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\n",
               segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH,
               expectedStartTime[arrayIndex], expectedEndTime[arrayIndex], actualAudioStartTime[arrayIndex],
               actualAudioEndTime[arrayIndex],
               actualAudioStartTime[arrayIndex] - expectedStartTime[arrayIndex],
               actualAudioEndTime[arrayIndex] - expectedEndTime[arrayIndex]);
    }
}

int checkPSIIdentical(dash_validator_t* dash_validator, int numDashValidators)
{
    g_info("Validating that PSI info is identical in each segment\n");

    // check that each segment has the same videoPID, audioPID, PMT version, PMT program num, and PCR PID

    int videoPID;
    int audioPID;
    int PCR_PID;
    uint32_t pmt_program_number;
    uint32_t pmt_version_number;

    unsigned char initialized = 0;
    unsigned char status = 0; // 0=PASS, 1-FAIL

    for(int i = 0; i < numDashValidators; i++) {
        int PCR_PID_Temp = dash_validator[i].PCR_PID;
        int videoPID_Temp = dash_validator[i].videoPID;
        int audioPID_Temp = dash_validator[i].audioPID;
        uint32_t pmt_program_number_Temp = dash_validator[i].pmt_program_number;
        uint32_t pmt_version_number_Temp = dash_validator[i].pmt_version_number;

        /*
        g_info ("");
        g_info ("PSI Table Validation: num %d", i);
        g_info ("PSI Table Validation: PCR_PID = %d", PCR_PID_Temp);
        g_info ("PSI Table Validation: videoPID = %d", videoPID_Temp);
        g_info ("PSI Table Validation: audioPID = %d", audioPID_Temp);
        g_info ("PSI Table Validation: pmt_program_number = %d", pmt_program_number_Temp);
        g_info ("PSI Table Validation: pmt_version_number = %d", pmt_version_number_Temp);
        g_info ("");
        */

        if(initialized) {
            if(PCR_PID != PCR_PID_Temp) {
                g_critical("PSI Table Validation FAILED: Incorrect PCR_PID: Expected = %d, Actual = %d",
                               PCR_PID, PCR_PID_Temp);
                status = -1;
            }
            if(videoPID != videoPID_Temp) {
                g_critical("PSI Table Validation FAILED: Incorrect videoPID: Expected = %d, Actual = %d",
                               videoPID, videoPID_Temp);
                status = -1;
            }
            if(audioPID != audioPID_Temp) {
                g_critical("PSI Table Validation FAILED: Incorrect audioPID: Expected = %d, Actual = %d",
                               audioPID, audioPID_Temp);
                status = -1;
            }
            if(pmt_program_number != pmt_program_number_Temp) {
                g_critical("PSI Table Validation FAILED: Incorrect pmt_program_number: Expected = %d, Actual = %d",
                               pmt_program_number, pmt_program_number_Temp);
                status = -1;
            }
            if(pmt_version_number != pmt_version_number_Temp) {
                g_critical("PSI Table Validation FAILED: Incorrect pmt_version_number: Expected = %d, Actual = %d",
                               pmt_version_number, pmt_version_number_Temp);
                status = -1;
            }
        } else {
//            g_info ("PSI Table Validation: INITIALIZING");

            PCR_PID = PCR_PID_Temp;
            videoPID = videoPID_Temp;
            audioPID = audioPID_Temp;
            pmt_program_number = pmt_program_number_Temp;
            pmt_version_number = pmt_version_number_Temp;

            initialized = 1;
        }
    }

    return status;
}

