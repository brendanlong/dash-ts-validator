
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


#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include "log.h"

#include "segment_validator.h"
#include "ISOBMFF.h"

#define SEGMENT_FILE_NAME_MAX_LENGTH    512
#define SEGMENT_FILE_NUM_HEADER_LINES    8


int getNumRepresentations(char* fname, int* numRepresentations, int* numSegments);
void parseSegInfoFileLine(char* line, char* segFileNames, int segNum, int numSegments);
void parseRepresentationIndexFileLine(char* line, char* representationIndexFileNames,
                                      int numRepresentations);

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

int getArrayIndex(int repNum, int segNum, int numSegments);

int readIntFromSegInfoFile(FILE* segInfoFile, char* paramName, int* paramValue);
int readStringFromSegInfoFile(FILE* segInfoFile, char* paramName, char* paramValue);
int readSegInfoFile(char* fname, int* numRepresentations, int* numSegments, char** segFileNames,
                    int** segDurations,
                    int* expectedSAPType, int* maxVideoGapPTSTicks, int* maxAudioGapPTSTicks,
                    char* initializationSegment, char** representationIndexFileNames, char** segmentIndexFileNames,
                    int* presentationTimeOffset, int* videoPID);
char* trimWhitespace(char* string);

int checkPSIIdentical(dash_validator_t* dash_validator, int numDashValidators);


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
                }
                if(!strcmp(optarg, "main")) {
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

    // read segment_info file (tab-delimited) which lists segment file paths
    char* fname = argv[optind];
    if(fname == NULL || fname[0] == 0) {
        LOG_ERROR("No segment_info file provided");
        usage(argv[0]);
        return 1;
    }

    // for all the 2D arrays in the following, see getArrayIndex for array ordering
    int overallStatus = 1;    // overall pass/fail, with 1=PASS, 0=FAIL
    int numRepresentations;     // number of representations (one per bitrate) in the adaptation set
    int numSegments;            // number of segments in each representation
    char* segFileNames =
        NULL;  // contains numRepresentations*numSegments media filepaths (relative to exe)
    int* segDurations =
        NULL;   // contains numSegments durations -- each representation has same durations
    int expectedSAPType =
        0;    // the SAP type expected at start of each segment -- if 0, then not tested
    int maxVideoGapPTSTicks =
        0;    // allowable gap in video stream between segments (in units of PTS ticks)
    int maxAudioGapPTSTicks =
        0;    // allowable gap in video stream between segments (in units of PTS ticks)
    char initializationSegment[SEGMENT_FILE_NAME_MAX_LENGTH];  // intialization segment file path, if present
    initializationSegment[0] = 0;
    int presentationTimeOffset = 0; // starting time for segment 0 (in units of PTS ticks)
    char* representationIndexFileNames =
        NULL;     // if present, contains numRepresentations index-file filepaths, one per representation.
    char* segmentIndexFileNames =
        NULL;     // if present, contains numRepresentations*numSegments index-file filepaths, one per segment/representation
    int videoPID = -1;  // PID of video stream in media segments

    // first step: read input file and get all test parameters
    if(readSegInfoFile(fname, &numRepresentations, &numSegments, &segFileNames, &segDurations,
                       &expectedSAPType, &maxVideoGapPTSTicks, &maxAudioGapPTSTicks, initializationSegment,
                       &representationIndexFileNames, &segmentIndexFileNames, &presentationTimeOffset, &videoPID) != 0) {
        return 1;
    }

    // allocate memory for all the arrays -- see GetArrayIndex for array ordering

    dash_validator_t* dash_validator = calloc(numRepresentations * numSegments,
                                       sizeof(dash_validator_t));
    dash_validator_t* dash_validator_init_segment = calloc(1, sizeof(dash_validator_t));
    dash_validator_init_segment->segment_type = INITIALIZATION_SEGMENT;

    int64_t* expectedStartTime = calloc(numRepresentations * numSegments, sizeof(int64_t));
    int64_t* expectedEndTime = calloc(numRepresentations * numSegments, sizeof(int64_t));
    int64_t* expectedDuration = calloc(numRepresentations * numSegments, sizeof(int64_t));

    int64_t* actualAudioStartTime = calloc(numRepresentations * numSegments, sizeof(int64_t));
    int64_t* actualVideoStartTime = calloc(numRepresentations * numSegments, sizeof(int64_t));
    int64_t* actualAudioEndTime = calloc(numRepresentations * numSegments, sizeof(int64_t));
    int64_t* actualVideoEndTime = calloc(numRepresentations * numSegments, sizeof(int64_t));

    int64_t* lastAudioEndTime = calloc(numRepresentations * (numSegments + 1), sizeof(int64_t));
    int64_t* lastVideoEndTime = calloc(numRepresentations * (numSegments + 1), sizeof(int64_t));

    data_segment_iframes_t* pIFrames = calloc(numSegments * numRepresentations,
                                       sizeof(data_segment_iframes_t));


    char* content_component_table[NUM_CONTENT_COMPONENTS] =
    { "<unknown>", "video", "audio" };

    ///////////////////////////////////

    int returnCode;

    // if there is an initialization segment, process it first in order to get the PAT and PMT tables
    if(strlen(initializationSegment) != 0) {
        if(doSegmentValidation(dash_validator_init_segment, initializationSegment,
                               NULL, NULL, 0) != 0) {
            LOG_ERROR_ARGS("Validation of initialization segment %s FAILED.", initializationSegment);
            dash_validator_init_segment->status = 0;
        }

        if(dash_validator_init_segment->status == 0) {
            overallStatus = 0;
        }
    }

    // next process index files, either representation index files or single segment index files
    // the index files contain iFrame locations which are stored here and then validated when the actual
    // media segments are processed later

    if(strlen(&(representationIndexFileNames[0])) != 0) {
        for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
            data_segment_iframes_t* pIFramesTemp = pIFrames + getArrayIndex(repIndex, 0 /* segIndex */,
                                                   numSegments);
            if(validateIndexSegment(representationIndexFileNames + repIndex * SEGMENT_FILE_NAME_MAX_LENGTH,
                                    numSegments, segDurations, pIFramesTemp, presentationTimeOffset, videoPID,
                                    conformance_level & TS_TEST_SIMPLE) != 0) {
                LOG_ERROR_ARGS("Validation of RepresentationIndexFile %s FAILED",
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
                    LOG_ERROR_ARGS("Validation of SegmentIndexFile %s FAILED", segmentIndexFileName);
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
//           LOG_INFO_ARGS("\nsegFileName = %s", segFileName);

            data_segment_iframes_t* pIFramesTemp = pIFrames + arrayIndex;

            if(strlen(initializationSegment) != 0) {
                dash_validator[arrayIndex].use_initializaion_segment = 1;

                returnCode = doSegmentValidation(&(dash_validator[arrayIndex]), segFileName,
                                                 dash_validator_init_segment, pIFramesTemp, segDurations[segIndex]);
            } else {
                returnCode = doSegmentValidation(&(dash_validator[arrayIndex]), segFileName,
                                                 NULL, pIFramesTemp, segDurations[segIndex]);
            }

            if(returnCode != 0) {
                return returnCode;
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

                LOG_INFO_ARGS("%s: %04X: %s STARTTIME=%"PRId64", ENDTIME=%"PRId64", DURATION=%"PRId64"",
                              segFileName,
                              pv->PID, content_component_table[pv->content_component], actualStartTime, actualEndTime,
                              actualDuration);

                if(pv->content_component == VIDEO_CONTENT_COMPONENT) {
                    LOG_INFO_ARGS("%s: VIDEO: Last end time: %"PRId64", Current start time: %"PRId64", Delta: %"PRId64"",
                                  segFileName, lastVideoEndTime[arrayIndex], actualStartTime,
                                  actualStartTime - lastVideoEndTime[arrayIndex]);

                    actualVideoStartTime[arrayIndex] = actualStartTime;
                    actualVideoEndTime[arrayIndex] = actualEndTime;

                    // check SAP and SAP Type
                    if(expectedSAPType != 0) {
                        if(pv->SAP == 0) {
                            LOG_INFO_ARGS("%s: FAIL: SAP not set", segFileName);
                            dash_validator[arrayIndex].status = 0;
                        } else if(pv->SAP_type != expectedSAPType) {
                            LOG_INFO_ARGS("%s: FAIL: Invalid SAP Type: expected %d, actual %d",
                                          segFileName, expectedSAPType, pv->SAP_type);
                            dash_validator[arrayIndex].status = 0;
                        }
                    } else {
                        LOG_INFO("startWithSAP not set -- skipping SAP check");
                    }
                } else if(pv->content_component == AUDIO_CONTENT_COMPONENT) {
                    LOG_INFO_ARGS("%s: AUDIO: Last end time: %"PRId64", Current start time: %"PRId64", Delta: %"PRId64"",
                                  segFileName, lastAudioEndTime[arrayIndex], actualStartTime,
                                  actualStartTime - lastAudioEndTime[arrayIndex]);

                    actualAudioStartTime[arrayIndex] = actualStartTime;
                    actualAudioEndTime[arrayIndex] = actualEndTime;
                }


                if(actualStartTime != expectedStartTime[arrayIndex]) {
                    LOG_INFO_ARGS("%s: %s: Invalid start time: expected = %"PRId64", actual = %"PRId64", delta = %"PRId64"",
                                  segFileName, content_component_table[pv->content_component],
                                  expectedStartTime[arrayIndex], actualStartTime, actualStartTime - expectedStartTime[arrayIndex]);

                    if(pv->content_component == VIDEO_CONTENT_COMPONENT) {
                        dash_validator[arrayIndex].status = 0;
                    } else if(pv->content_component == AUDIO_CONTENT_COMPONENT) {
                    }
                }

                if(actualEndTime != expectedEndTime[arrayIndex]) {
                    LOG_INFO_ARGS("%s: %s: Invalid end time: expected %"PRId64", actual %"PRId64", delta = %"PRId64"",
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
        }

        for(int repIndex = 0; repIndex < numRepresentations; repIndex++) {
            int arrayIndex0 = getArrayIndex(repIndex, segIndex, numSegments);
            int arrayIndex1 = getArrayIndex(repIndex, segIndex + 1, numSegments);
            lastAudioEndTime[arrayIndex1] = actualAudioEndTime[arrayIndex0];
            lastVideoEndTime[arrayIndex1] = actualVideoEndTime[arrayIndex0];

            LOG_INFO_ARGS("SEGMENT TEST RESULT: %s: %s",
                          segFileNames + arrayIndex0 * SEGMENT_FILE_NAME_MAX_LENGTH,
                          dash_validator[arrayIndex0].status ? "PASS" : "FAIL");
        }

        printf("\n");
    }

    if(conformance_level & TS_TEST_SIMPLE) {
        // for a simple profile, the PSI info must be the same for all segments
        if(checkPSIIdentical(dash_validator, numRepresentations * numSegments) != 0) {
            LOG_ERROR("Validation FAILED: PSI info not identical for all segments");
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
    }
    LOG_INFO_ARGS("\nOVERALL TEST RESULT: %s", (overallStatus == 1) ? "PASS" : "FAIL");
}

int getArrayIndex(int repNum, int segNum, int numSegments)
{
    return (segNum + repNum * numSegments);
}

void parseSegInfoFileLine(char* line, char* segFileNames, int segNum, int numSegments)
{
    if(strlen(line) == 0) {
        return;
    }

    char* pch = strtok(line, ",\r\n");
    int repNum = 0;

    while(pch != NULL) {
        int arrayIndex = getArrayIndex(repNum, segNum, numSegments);

        sscanf(pch, "%s", segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
        LOG_INFO_ARGS("fileNames[%d][%d] = %s", segNum, repNum,
                      segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
        pch = strtok(NULL, ",\r\n");
        repNum++;
    }
}

void parseRepresentationIndexFileLine(char* line, char* representationIndexFileNames,
                                      int numRepresentations)
{
    int repIndex = 0;

    if(strlen(line) == 0) {
        return;
    }

    char* pch = strtok(line, ",\r\n");
    while(pch != NULL) {
        sscanf(pch, "%s", representationIndexFileNames + repIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
        pch = strtok(NULL, ",\r\n");
        repIndex++;
    }
}

int getNumRepresentations(char* fname, int* numRepresentations, int* numSegments)
{
    char line[SEGMENT_FILE_NAME_MAX_LENGTH];
    FILE* segInfoFile = fopen(fname, "r");
    if(!segInfoFile) {
        LOG_ERROR_ARGS("Couldn't open segInfoFile %s\n", fname);
        return -1;
    }

    *numSegments = 0;
    while(1) {
        if(fgets(line, sizeof(line), segInfoFile) == NULL) {
            if(feof(segInfoFile) != 0) {
                break;
            } else {
                LOG_ERROR_ARGS("ERROR: cannot read line from SegInfoFile %s\n", fname);
                return -1;
            }
        }

        if(strncmp("Segment.", line, strlen("Segment.")) == 0) {
            int segmentIndex = 0;
            char* temp1 = strchr(line, '.');
            char* temp2 = strchr(line, '=');

            // parse to get num reps
            char* temp3 = temp2;
            *numRepresentations = 1;
            while(1) {
                char* temp4 = strchr(temp3, ',');
                if(temp4 == NULL) {
                    break;
                }

                temp3 = temp4 + 1;
                (*numRepresentations)++;
            }

            temp2 = 0;  // terminate string
            if(sscanf(temp1 + 1, "%d", &segmentIndex) != 1) {
                LOG_ERROR("ERROR parsing segment index\n");
                return -1;
            }

            if(segmentIndex > (*numSegments)) {
                *numSegments = segmentIndex;
            }
        }
    }

    fclose(segInfoFile);

    LOG_INFO_ARGS("NumRepresentations = %d", *numRepresentations);
    LOG_INFO_ARGS("NumSegments = %d", *numSegments);

    return 0;
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

int readSegInfoFile(char* fname, int* numRepresentations, int* numSegments, char** segFileNames,
                    int** segDurations,
                    int* expectedSAPType, int* maxVideoGapPTSTicks, int* maxAudioGapPTSTicks,
                    char* initializationSegment, char** representationIndexFileNames, char** segmentIndexFileNames,
                    int* presentationTimeOffset, int* videoPID)
{
    char representationIndexSegmentString[1024];

    if(getNumRepresentations(fname, numRepresentations, numSegments) < 0) {
        LOG_ERROR("Error reading segment_info file");
        return -1;
    }

    FILE* segInfoFile = fopen(fname, "r");
    if(!segInfoFile) {
        LOG_ERROR_ARGS("Couldn't open segInfoFile %s\n", fname);
        return 0;
    }

    char line[SEGMENT_FILE_NAME_MAX_LENGTH];
    *segFileNames = (char*)calloc((*numRepresentations) * (*numSegments), SEGMENT_FILE_NAME_MAX_LENGTH);
    *segDurations = (int*)calloc(*numSegments, sizeof(int));
    *representationIndexFileNames = calloc(*numRepresentations, SEGMENT_FILE_NAME_MAX_LENGTH);
    *segmentIndexFileNames = calloc((*numSegments) * (*numRepresentations),
                                    SEGMENT_FILE_NAME_MAX_LENGTH);


    while(1) {
        if(fgets(line, sizeof(line), segInfoFile) == NULL) {
            if(feof(segInfoFile) != 0) {
                return 0;
            } else {
                LOG_ERROR_ARGS("ERROR: cannot read line from SegInfoFile %s\n", fname);
                return -1;
            }
        }


        char* trimmedLine = trimWhitespace(line);

        if(strncmp("#", trimmedLine, strlen("#")) == 0 || strlen(trimmedLine) == 0) {
            // comment -- skip line
            continue;
        }

        char* temp = strchr(trimmedLine, '=');
        if(temp == NULL) {
            LOG_ERROR_ARGS("Error parsing line in SegInfoFile %s", fname);
            continue;
        }

        if(strncmp("PresentationTimeOffset_PTSTicks", trimmedLine,
                   strlen("PresentationTimeOffset_PTSTicks")) == 0) {
            if(sscanf(temp + 1, "%d", presentationTimeOffset) != 1) {
                LOG_ERROR_ARGS("Error parsing PresentationTimeOffset_PTSTicks in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("PresentationTimeOffset_PTSTicks = %d", *presentationTimeOffset);
        } else if(strncmp("VideoPID", trimmedLine, strlen("VideoPID")) == 0) {
            if(sscanf(temp + 1, "%d", videoPID) != 1) {
                LOG_ERROR_ARGS("Error parsing VideoPID in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("VideoPID = %d", *videoPID);
        } else if(strncmp("ExpectedSAPType", trimmedLine, strlen("ExpectedSAPType")) == 0) {
            if(sscanf(temp + 1, "%d", expectedSAPType) != 1) {
                LOG_ERROR_ARGS("Error parsing ExpectedSAPType in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("ExpectedSAPType = %d", *expectedSAPType);
        } else if(strncmp("MaxAudioGap_PTSTicks", trimmedLine, strlen("MaxAudioGap_PTSTicks")) == 0) {
            if(sscanf(temp + 1, "%d", maxAudioGapPTSTicks) != 1) {
                LOG_ERROR_ARGS("Error parsing MaxVideoGap_PTSTicks in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("MaxAudioGap_PTSTicks = %d", *maxAudioGapPTSTicks);
        } else if(strncmp("MaxVideoGap_PTSTicks", trimmedLine, strlen("MaxVideoGap_PTSTicks")) == 0) {
            if(sscanf(temp + 1, "%d", maxVideoGapPTSTicks) != 1) {
                LOG_ERROR_ARGS("Error parsing MaxVideoGap_PTSTicks in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("MaxVideoGap_PTSTicks = %d", *maxVideoGapPTSTicks);
        } else if(strncmp("InitializationSegment", trimmedLine, strlen("InitializationSegment")) == 0) {
            char* temp2 = trimWhitespace(temp + 1);
            if(strlen(temp2) != 0) {
                if(sscanf(temp2, "%s", initializationSegment) != 1) {
                    LOG_ERROR_ARGS("Error parsing InitializationSegment in SegInfoFile %s", fname);
                }
                LOG_INFO_ARGS("InitializationSegment = %s", initializationSegment);
            } else {
                LOG_INFO("No InitializationSegment");
            }
        } else if(strncmp("RepresentationIndexSegment", trimmedLine,
                          strlen("RepresentationIndexSegment")) == 0) {
            char* temp2 = trimWhitespace(temp + 1);
            if(sscanf(temp2, "%s", representationIndexSegmentString) != 1) {
                LOG_ERROR_ARGS("Error parsing RepresentationIndexSegment in SegInfoFile %s", fname);
            } else {
                parseRepresentationIndexFileLine(representationIndexSegmentString, *representationIndexFileNames,
                                                 *numRepresentations);

                for(int i = 0; i < *numRepresentations; i++) {
                    LOG_INFO_ARGS("RepresentationIndexFile[%d] = %s", i,
                                  (*representationIndexFileNames) + i * SEGMENT_FILE_NAME_MAX_LENGTH);
                }
            }
        } else if(strncmp("Segment.", trimmedLine, strlen("Segment.")) == 0) {
            int segmentIndex = 0;
            char* temp1 = strchr(trimmedLine, '.');
            *temp = 0;
            char* temp3 = trimWhitespace(temp + 1);
            if(sscanf(temp1 + 1, "%d", &segmentIndex) != 1) {
                LOG_ERROR_ARGS("Error parsing Segment line in SegInfoFile %s", fname);
            } else {
                segmentIndex -= 1;
                parseSegInfoFileLine(temp3, *segFileNames, segmentIndex, *numSegments);
//               LOG_INFO_ARGS ("segDuration[%d] = %d", segmentIndex, (*segDurations)[segmentIndex]);
            }
        } else if(strncmp("Segment_DurationPTSTicks.", trimmedLine,
                          strlen("Segment_DurationPTSTicks.")) == 0) {
            int segmentIndex = 0;
            char* temp1 = strchr(trimmedLine, '.');
            *temp = 0;
            if(sscanf(temp1 + 1, "%d", &segmentIndex) != 1) {
                LOG_ERROR_ARGS("Error parsing Segment_DurationPTSTicks line in SegInfoFile %s", fname);
            } else {
                segmentIndex -= 1;
                if(sscanf(temp + 1, "%d", &((*segDurations)[segmentIndex])) != 1) {
                    LOG_ERROR_ARGS("Error parsing segDuration in SegInfoFile %s", fname);
                }

                LOG_INFO_ARGS("segDuration[%d] = %d", segmentIndex, (*segDurations)[segmentIndex]);
            }
        } else if(strncmp("Segment_IndexFile.", trimmedLine, strlen("Segment_IndexFile.")) == 0) {
            int segmentIndex = 0;
            char* temp1 = strchr(trimmedLine, '.');
            *temp = 0;
            char* temp2 = trimWhitespace(temp + 1);
            if(sscanf(temp1 + 1, "%d", &segmentIndex) != 1) {
                LOG_ERROR_ARGS("Error parsing Segment_IndexFile line in SegInfoFile %s", fname);
            } else {
                segmentIndex -= 1;
                parseSegInfoFileLine(temp2, *segmentIndexFileNames, segmentIndex, *numSegments);
                for(int i = 0; i < *numRepresentations; i++) {
                    int arrayIndex = getArrayIndex(i, segmentIndex, *numSegments);
                    LOG_INFO_ARGS("Segment_IndexFile[%d][%d] = %s", segmentIndex, i,
                                  (*segmentIndexFileNames) + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
                }
            }
        }
    }

    return 0;
}

int readIntFromSegInfoFile(FILE* segInfoFile, char* paramName, int* paramValue)
{
    char line[1024];
    char temp[1024];

    strcpy(temp, paramName);
    strcat(temp, " = %d\n");

    if(fgets(line, sizeof(line), segInfoFile) == NULL) {
        LOG_ERROR_ARGS("ERROR: cannot read %s from SegInfoFile\n", paramName);
        return -1;
    }
    if(sscanf(line, temp, paramValue) != 1) {
        LOG_ERROR_ARGS("ERROR: cannot parse %s from SegInfoFile\n", paramName);
        return -1;
    }
    LOG_INFO_ARGS("%s = %d", paramName, *paramValue);

    return 0;
}

int readStringFromSegInfoFile(FILE* segInfoFile, char* paramName, char* paramValue)
{
    char line[1024];
    char temp[1024];

    strcpy(temp, paramName);
    strcat(temp, " = %s\n");

    if(fgets(line, sizeof(line), segInfoFile) == NULL) {
        LOG_ERROR_ARGS("ERROR: cannot read %s from SegInfoFile\n", paramName);
        return -1;
    }
    if(sscanf(line, temp, paramValue) != 1) {
        strcpy(temp, paramName);
        strcat(temp, " =");
        line [strlen(temp)] = 0;

        if(strcmp(line, temp) == 0) {
            paramValue[0] = 0;
            return 0;
        }

        LOG_ERROR_ARGS("ERROR: cannot parse %s from SegInfoFile\n", paramName);
        return -1;
    }
    LOG_INFO_ARGS("%s = %s", paramName, paramValue);


    return 0;
}

char* trimWhitespace(char* string)
{
    int stringSz = strlen(string);
    int index = 0;
    while(index < stringSz) {
        if(string[index] != ' ' && string[index] != '\t' && string[index] != '\n'
                && string[index] != '\r') {
            break;
        }

        index++;
    }

    char* newString = string + index;
    if(index == stringSz) {
        return newString;
    }

    stringSz = strlen(newString);
    index = stringSz - 1;
    while(index >= 0) {
        if(newString[index] != ' ' && newString[index] != '\t' && newString[index] != '\n'
                && newString[index] != '\r') {
            break;
        }

        index--;
    }

    newString[index + 1] = 0;

    return newString;
}

int checkPSIIdentical(dash_validator_t* dash_validator, int numDashValidators)
{
    LOG_INFO("Validating that PSI info is identical in each segment\n");

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
        LOG_INFO ("");
        LOG_INFO_ARGS ("PSI Table Validation: num %d", i);
        LOG_INFO_ARGS ("PSI Table Validation: PCR_PID = %d", PCR_PID_Temp);
        LOG_INFO_ARGS ("PSI Table Validation: videoPID = %d", videoPID_Temp);
        LOG_INFO_ARGS ("PSI Table Validation: audioPID = %d", audioPID_Temp);
        LOG_INFO_ARGS ("PSI Table Validation: pmt_program_number = %d", pmt_program_number_Temp);
        LOG_INFO_ARGS ("PSI Table Validation: pmt_version_number = %d", pmt_version_number_Temp);
        LOG_INFO ("");
        */

        if(initialized) {
            if(PCR_PID != PCR_PID_Temp) {
                LOG_ERROR_ARGS("PSI Table Validation FAILED: Incorrect PCR_PID: Expected = %d, Actual = %d",
                               PCR_PID, PCR_PID_Temp);
                status = -1;
            }
            if(videoPID != videoPID_Temp) {
                LOG_ERROR_ARGS("PSI Table Validation FAILED: Incorrect videoPID: Expected = %d, Actual = %d",
                               videoPID, videoPID_Temp);
                status = -1;
            }
            if(audioPID != audioPID_Temp) {
                LOG_ERROR_ARGS("PSI Table Validation FAILED: Incorrect audioPID: Expected = %d, Actual = %d",
                               audioPID, audioPID_Temp);
                status = -1;
            }
            if(pmt_program_number != pmt_program_number_Temp) {
                LOG_ERROR_ARGS("PSI Table Validation FAILED: Incorrect pmt_program_number: Expected = %d, Actual = %d",
                               pmt_program_number, pmt_program_number_Temp);
                status = -1;
            }
            if(pmt_version_number != pmt_version_number_Temp) {
                LOG_ERROR_ARGS("PSI Table Validation FAILED: Incorrect pmt_version_number: Expected = %d, Actual = %d",
                               pmt_version_number, pmt_version_number_Temp);
                status = -1;
            }
        } else {
//            LOG_INFO ("PSI Table Validation: INITIALIZING");

            PCR_PID = PCR_PID_Temp;
            videoPID = videoPID_Temp;
            audioPID = audioPID_Temp;
            pmt_program_number = pmt_program_number_Temp;
            pmt_version_number = pmt_version_number_Temp;

            initialized = 1;
        }
    }

    return 0;
}

