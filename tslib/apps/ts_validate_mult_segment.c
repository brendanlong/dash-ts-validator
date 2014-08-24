
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

#define SEGMENT_FILE_NAME_MAX_LENGTH    512
#define SEGMENT_FILE_NUM_HEADER_LINES    8

int getNumRepresentations (char * fname, int *numRepresentations, int *numSegments);
void parseSegInfoFileLine (char *line, char *segFileNames, int segmentCounter, int numRepresentations, int *duration);
void parseRepresentationIndexFileLine (char *line, char *representationIndexFileNames, int numRepresentations);

void printAudioGapMatrix(int numRepresentations, int64_t *lastVideoEndTime, int64_t *actualVideoStartTime,
                         int segmentCounter, char * segFileNames);
void printVideoGapMatrix(int numRepresentations, int64_t *lastVideoEndTime, int64_t *actualVideoStartTime,
                         int segmentCounter, char * segFileNames);

void printVideoTimingMatrix(char *segFileNames, int64_t *actualVideoStartTime, int64_t *actualVideoEndTime,
    int64_t *expectedStartTime, int64_t *expectedEndTime, int numSegments, int numRepresentations,
    int representationNum);
void printAudioTimingMatrix(char * segFileNames, int64_t *actualAudioStartTime, int64_t *actualAudioEndTime,
    int64_t *expectedStartTime, int64_t *expectedEndTime, int numSegments, int numRepresentations,
    int representationNum);

int readIntFromSegInfoFile (FILE *segInfoFile, char * paramName, int *paramValue);
int readStringFromSegInfoFile (FILE *segInfoFile, char * paramName, char *paramValue);
int readSegInfoFile(char * fname, int *numRepresentations, int *numSegments, char **segFileNames, int **segDurations,
    int *expectedSAPType, int *maxVideoGapPTSTicks, int *maxAudioGapPTSTicks,
    int *segmentAlignment, int *subsegmentAlignment, int *bitstreamSwitching,
    char *initializationSegment, char **representationIndexFileNames);



static struct option long_options[] = { 
   { "verbose",	   no_argument,        NULL, 'v' }, 
   { "dash",	   optional_argument,  NULL, 'd' }, 
   { "byte-range", required_argument,  NULL, 'b' }, 
   { "help",       no_argument,        NULL, 'h' }, 
}; 

static char options[] = 
   "\t-d, --dash\n"
   "\t-v, --verbose\n"
   "\t-h, --help\n"; 

static void usage(char *name) 
{ 
   fprintf(stderr, "\n%s\n", name); 
   fprintf(stderr, "\nUsage: \n%s [options] <input file with segment info>\n\nOptions:\n%s\n", name, options);
}

int main(int argc, char *argv[]) 
{ 
   int c, long_options_index; 
   extern char *optarg; 
   extern int optind; 

   uint32_t conformance_level; 
  
   if (argc < 2) 
   {
      usage(argv[0]); 
      return 1;
   }
   

   while ((c = getopt_long(argc, argv, "vdbh", long_options, &long_options_index)) != -1) 
   {
      switch (c) 
      {
      case 'd':
         conformance_level = TS_TEST_DASH; 
         if (optarg != NULL) 
         {
            if (!strcmp(optarg, "simple")) 
            {
               conformance_level |= TS_TEST_SIMPLE; 
               conformance_level |= TS_TEST_MAIN; 
            }
            if (!strcmp(optarg, "main")) 
            {
               conformance_level |= TS_TEST_MAIN; 
            }
         }
         break; 
      case 'v':
         if (tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) tslib_loglevel++; 
         break; 
      case 'h':
      default:
         usage(argv[0]); 
         return 1;
      }
   }
   
   // read segment_info file (tab-delimited) which lists segment file paths
   char *fname = argv[optind]; 
   if (fname == NULL || fname[0] == 0) 
   {
      LOG_ERROR("No segment_info file provided"); 
      usage(argv[0]); 
      return 1;
   }

   int numRepresentations;
   int numSegments;
   char *segFileNames;
   int *segDurations;
   int expectedSAPType = 0;
   int maxVideoGapPTSTicks = 0;
   int maxAudioGapPTSTicks = 0;
   int segmentAlignment = 0;
   int subsegmentAlignment = 0;
   int bitstreamSwitching = 0;
   char initializationSegment[SEGMENT_FILE_NAME_MAX_LENGTH];
   initializationSegment[0] = 0;
   char * representationIndexFileNames;

   if (readSegInfoFile(fname, &numRepresentations, &numSegments, &segFileNames, &segDurations,
     &expectedSAPType, &maxVideoGapPTSTicks, &maxAudioGapPTSTicks,
     &segmentAlignment, &subsegmentAlignment, &bitstreamSwitching,
     initializationSegment, &representationIndexFileNames) != 0)
   {
       return 1;
   }


   dash_validator_t *dash_validator = calloc (numRepresentations * numSegments, sizeof (dash_validator_t));
   dash_validator_t *dash_validator_init_segment = calloc (1, sizeof (dash_validator_t));
   dash_validator_init_segment->segment_type = INITIALIZAION_SEGMENT;
   dash_validator_t *dash_validator_rep_index_segment = calloc (1, sizeof (dash_validator_t));
   dash_validator_rep_index_segment->segment_type = REPRESENTATION_INDEX_SEGMENT;
      
   int64_t *expectedStartTime = calloc (numRepresentations * (numSegments + 1), sizeof (int64_t));
   int64_t *expectedEndTime = calloc (numRepresentations * numSegments, sizeof (int64_t));
   int64_t *expectedDuration = calloc (numRepresentations * numSegments, sizeof (int64_t));

   int64_t *actualAudioStartTime = calloc (numRepresentations * numSegments, sizeof (int64_t));
   int64_t *actualVideoStartTime = calloc (numRepresentations * numSegments, sizeof (int64_t));
   int64_t *actualAudioEndTime = calloc (numRepresentations * numSegments, sizeof (int64_t));
   int64_t *actualVideoEndTime = calloc (numRepresentations * numSegments, sizeof (int64_t));

   int64_t *lastAudioEndTime = calloc (numRepresentations * (numSegments + 1), sizeof (int64_t));
   int64_t *lastVideoEndTime = calloc (numRepresentations * (numSegments + 1), sizeof (int64_t));
 
   char *content_component_table[NUM_CONTENT_COMPONENTS] = 
        { "<unknown>", "video", "audio" }; 

   ///////////////////////////////////

   int returnCode;

   if (strlen(initializationSegment) != 0)
   {
      returnCode = doSegmentValidation(dash_validator_init_segment, initializationSegment, NULL);
      if (returnCode != 0)
      {
         return returnCode;
      }
   }


   if (strlen(representationIndexFileNames) != 0)
   {
       for (int i=0; i<numRepresentations; i++)
       {
          returnCode = doRepresentationIndexSegmentValidation(dash_validator_rep_index_segment, 
              representationIndexFileNames + i*SEGMENT_FILE_NAME_MAX_LENGTH);
          if (returnCode != 0)
          {
             return returnCode;
          }
       }
   }

   char *segFileName = NULL;
   for (int segmentCounter=0; segmentCounter<numSegments; segmentCounter++)
   {
      for (int i=0; i<numRepresentations; i++)
      {
          int arrayIndex = i + segmentCounter * numRepresentations;
          dash_validator[arrayIndex].conformance_level = conformance_level; 
          if (strlen(initializationSegment) != 0)
          {
            dash_validator[arrayIndex].use_initializaion_segment = 1;
          }

          printf ("\n");
          segFileName = segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH;
          LOG_INFO_ARGS("segFileName = %s", segFileName);

          if (strlen(initializationSegment) != 0)
          {
             returnCode = doSegmentValidation(&(dash_validator[arrayIndex]), segFileName, dash_validator_init_segment);
          }
          else
          {
             returnCode = doSegmentValidation(&(dash_validator[arrayIndex]), segFileName, NULL);
          }
          if (returnCode != 0)
          {
             return returnCode;
          }

          // GORP: what if there is no video in the segment??
          pid_validator_t *pv = NULL;
          if (expectedStartTime[arrayIndex] == 0)
          {
              for (int ii = 0; ii < vqarray_length(dash_validator[arrayIndex].pids); ii++) 
              {
                 pv = (pid_validator_t *)vqarray_get(dash_validator[arrayIndex].pids, ii); 
                 if (pv == NULL) continue; 
                          
                 if (pv->content_component == VIDEO_CONTENT_COMPONENT)
                 {
                     expectedStartTime[arrayIndex] = pv->EPT;
                 }
              }
          }

          expectedDuration[arrayIndex] = segDurations[segmentCounter];
          expectedEndTime[arrayIndex] = expectedStartTime[arrayIndex] + expectedDuration[arrayIndex];

          // per PID
          for (int ii = 0; ii < vqarray_length(dash_validator[arrayIndex].pids); ii++) 
          {
             pv = (pid_validator_t *)vqarray_get(dash_validator[arrayIndex].pids, ii); 
             if (pv == NULL) continue; 
              
             // refine duration by including duration of last frame (audio and video are different rates)
             // start time is relative to the start time of the first segment

             // units of 90kHz ticks
             int64_t actualStartTime = pv->EPT;
             int64_t actualDuration = (pv->LPT - pv->EPT) + pv->duration;
             int64_t actualEndTime = actualStartTime + actualDuration;

             LOG_INFO_ARGS("%s: %04X: %s STARTTIME=%"PRId64", ENDTIME=%"PRId64", DURATION=%"PRId64"", segFileName,
                     pv->PID, content_component_table[pv->content_component], actualStartTime, actualEndTime, 
                     actualDuration);

             if (pv->content_component == VIDEO_CONTENT_COMPONENT)
             {
                 LOG_INFO_ARGS ("%s: VIDEO: Last end time: %"PRId64", Current start time: %"PRId64", Delta: %"PRId64"", 
                     segFileName, 
                     lastVideoEndTime[arrayIndex], actualStartTime, 
                     actualStartTime - lastVideoEndTime[arrayIndex]);

                 actualVideoStartTime[arrayIndex] = actualStartTime;
                 actualVideoEndTime[arrayIndex] = actualEndTime;

                // check SAP and SAP Type
                if (expectedSAPType != 0)
                {
                    if (pv->SAP == 0)
                    {
                        LOG_INFO_ARGS ("%s: FAIL: SAP not set", segFileName);
                        dash_validator[arrayIndex].status = 0;
                    }
                    else if (pv->SAP_type != expectedSAPType)
                    {
                        LOG_INFO_ARGS ("%s: FAIL: Invalid SAP Type: expected %d, actual %d", 
                            segFileName, expectedSAPType, pv->SAP_type);
                        dash_validator[arrayIndex].status = 0;
                    } 
                }
                else
                {
                    LOG_INFO ("startWithSAP not set -- skipping SAP check");
                }
             }
             else if (pv->content_component == AUDIO_CONTENT_COMPONENT)
             {
                 LOG_INFO_ARGS ("%s: AUDIO: Last end time: %"PRId64", Current start time: %"PRId64", Delta: %"PRId64"", 
                     segFileName, lastAudioEndTime[arrayIndex], actualStartTime,
                     actualStartTime - lastAudioEndTime[arrayIndex]);

                 actualAudioStartTime[arrayIndex] = actualStartTime;
                 actualAudioEndTime[arrayIndex] = actualEndTime;
             }


             if (actualStartTime != expectedStartTime[arrayIndex])
             {
                 LOG_INFO_ARGS ("%s: %s: Invalid start time: expected = %"PRId64", actual = %"PRId64", delta = %"PRId64"", 
                     segFileName, content_component_table[pv->content_component], 
                     expectedStartTime[arrayIndex], actualStartTime, actualStartTime - expectedStartTime[arrayIndex]);
                 
                 if (pv->content_component == VIDEO_CONTENT_COMPONENT)
                 {
                    dash_validator[arrayIndex].status = 0;
                 }
                 else if (pv->content_component == AUDIO_CONTENT_COMPONENT)
                 {
                     float numDeltaFrames = (actualStartTime - expectedStartTime[arrayIndex])/1917.0;
                     if (numDeltaFrames >= 4.0 || numDeltaFrames <= -4.0)
                     {
                         LOG_INFO_ARGS ("%s: Num Audio Frames in Delta (%f) >= 4: FAIL", segFileName, numDeltaFrames);
                         dash_validator[arrayIndex].status = 0;
                     }
                     else
                     {
                         LOG_INFO_ARGS ("%s: Num Audio Frames in Delta (%f) < 4: OK", segFileName, numDeltaFrames);
                     }
                 }
             }

             if (actualEndTime != expectedEndTime[arrayIndex])
             {
                 LOG_INFO_ARGS ("%s: %s: Invalid end time: expected %"PRId64", actual %"PRId64", delta = %"PRId64"", 
                     segFileName, content_component_table[pv->content_component], 
                     expectedEndTime[arrayIndex], actualEndTime, actualEndTime - expectedEndTime[arrayIndex]);
                 
                 if (pv->content_component == VIDEO_CONTENT_COMPONENT)
                 {
                    dash_validator[arrayIndex].status = 0;
                 }
                 else if (pv->content_component == AUDIO_CONTENT_COMPONENT)
                 {
                     float numDeltaFrames = (actualEndTime - expectedEndTime[arrayIndex])/1917.0;
                     if (numDeltaFrames >= 4.0 || numDeltaFrames <= -4.0)
                     {
                         LOG_INFO_ARGS ("%s: FAIL: Num Audio Frames in Delta (%f) >= 4", segFileName, numDeltaFrames);
                         dash_validator[arrayIndex].status = 0;
                     }
                     else
                     {
                         LOG_INFO_ARGS ("%s: Num Audio Frames in Delta (%f) < 4: OK", segFileName, numDeltaFrames);
                     }
                 }
             }
          }

          expectedStartTime[arrayIndex + numRepresentations] = expectedEndTime[arrayIndex];

      }

      // segment cross checking: check that the gap between all adjacent segments is
      // acceptably small

      printAudioGapMatrix(numRepresentations, lastAudioEndTime, actualAudioStartTime, segmentCounter, segFileNames);
      printVideoGapMatrix(numRepresentations, lastVideoEndTime, actualVideoStartTime, segmentCounter, segFileNames);
      printf ("\n");

      for (int j1 = 0; j1<numRepresentations; j1++)
      {
          int arrayIndex1 = j1 + segmentCounter * numRepresentations;
          for (int j2 = 0; j2<numRepresentations; j2++)
          {
              int arrayIndex2 = j2 + segmentCounter * numRepresentations;
                
              if (lastAudioEndTime[arrayIndex1] == 0 || lastAudioEndTime[arrayIndex2] == 0 ||
                  lastVideoEndTime[arrayIndex1] == 0 || lastVideoEndTime[arrayIndex2] == 0)
              {
                  continue;
              }

              int64_t audioPTSDelta = actualAudioStartTime[arrayIndex2] - lastAudioEndTime[arrayIndex1];
              int64_t videoPTSDelta = actualVideoStartTime[arrayIndex2] - lastVideoEndTime[arrayIndex1];

              if (audioPTSDelta > maxAudioGapPTSTicks)
              {
                 LOG_INFO_ARGS ("FAIL: Audio gap between (%d, %d) is %"PRId64" and exceeds limit %d", 
                     j1, j2, audioPTSDelta, maxAudioGapPTSTicks);
                 dash_validator[arrayIndex1].status = 0;
                 dash_validator[arrayIndex2].status = 0;
              }

              if (videoPTSDelta > maxVideoGapPTSTicks)
              {
                 LOG_INFO_ARGS ("FAIL: Video gap between (%d, %d) is %"PRId64" and exceeds limit %d", 
                     j1, j2, videoPTSDelta, maxVideoGapPTSTicks);
                 dash_validator[arrayIndex1].status = 0;
                 dash_validator[arrayIndex2].status = 0;
              }
          }
      }

      for (int i=0; i<numRepresentations; i++)
      {
         int arrayIndex = i + segmentCounter * numRepresentations;
         lastAudioEndTime[arrayIndex + numRepresentations] = actualAudioEndTime[arrayIndex];
         lastVideoEndTime[arrayIndex + numRepresentations] = actualVideoEndTime[arrayIndex];

         LOG_INFO_ARGS("%s: RESULT: %s", segFileNames + arrayIndex*SEGMENT_FILE_NAME_MAX_LENGTH, 
             dash_validator[arrayIndex].status ? "PASS" : "FAIL"); 
      }

      printf ("\n");
   }

   for (int i=0; i<numRepresentations; i++)
   {
      printAudioTimingMatrix(segFileNames, actualAudioStartTime, actualAudioEndTime,
        expectedStartTime, expectedEndTime, numSegments, numRepresentations, i);
   }
   for (int i=0; i<numRepresentations; i++)
   {
      printVideoTimingMatrix(segFileNames, actualVideoStartTime, actualVideoEndTime,
        expectedStartTime, expectedEndTime, numSegments, numRepresentations, i);
   }
}

void parseSegInfoFileLine (char *line, char *segFileNames, int segmentCounter, int numRepresentations, int *duration)
{
   char *pch = strtok (line,"\t\r\n");
   sscanf (pch, "%d", duration);

   int i = 0;

   pch = strtok (NULL, "\t\r\n");
   while (pch != NULL)
   {
      int arrayIndex = i + segmentCounter * numRepresentations;

      sscanf (pch, "%s", segFileNames + arrayIndex*SEGMENT_FILE_NAME_MAX_LENGTH);
      LOG_INFO_ARGS ("segFileNames[%d][%d] = %s",segmentCounter, i, 
          segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
      pch = strtok (NULL, "\t\r\n");
      i++;
   }
}

void parseRepresentationIndexFileLine (char *line, char *representationIndexFileNames, int numRepresentations)
{
   int i = 0;
   printf ("parseRepresentationIndexFileLine\n");
   printf ("parseRepresentationIndexFileLine: %s\n", line);

   if (strlen(line) == 0)
   {
       return;
   }

   char *pch = strtok (NULL, "\t\r\n");
   while (pch != NULL)
   {
      sscanf (pch, "%s", representationIndexFileNames + i * SEGMENT_FILE_NAME_MAX_LENGTH);
      LOG_INFO_ARGS ("representationIndexFileNames[%d] = %s", i, 
          representationIndexFileNames + i * SEGMENT_FILE_NAME_MAX_LENGTH);
      pch = strtok (NULL, "\t\r\n");
      i++;
   }
}

int getNumRepresentations (char * fname, int *numRepresentations, int *numSegments)
{
   char line[SEGMENT_FILE_NAME_MAX_LENGTH];
   FILE *segInfoFile = fopen(fname, "r");
   if (!segInfoFile) 
   {
      LOG_ERROR_ARGS("Couldn't open segInfoFile %s\n", fname); 
      return -1;
   }

   // discard header lines and read first line with segment filenames in it
   for (int i=0; i<SEGMENT_FILE_NUM_HEADER_LINES; i++)
   {
       if (fgets(line, sizeof(line), segInfoFile) == NULL )
       {
          LOG_ERROR_ARGS("Couldn't read segInfoFile %s\n", fname); 
          return -1;
       }
   }

   if (fgets(line, sizeof(line), segInfoFile) == NULL )
   {
      LOG_ERROR_ARGS("Couldn't read segInfoFile %s\n", fname); 
      return -1;
   }


   int representationCounter = 0;
   char *pch = strtok (line,"\t\r\n");
   while (pch != NULL)
   {
//      printf ("%s\n",pch);
      representationCounter ++;
      pch = strtok (NULL, "\t\r\n");
   }

   *numRepresentations = representationCounter-1;

   int segmentCounter = 1;
   while (fgets(line, sizeof(line), segInfoFile) != NULL )
   {
       segmentCounter++;
   }

   *numSegments = segmentCounter;

   fclose (segInfoFile);

   LOG_INFO_ARGS ("NumRepresentations = %d", *numRepresentations);
   LOG_INFO_ARGS ("NumSegments = %d", *numSegments);

   return 0;
}

void printAudioGapMatrix(int numRepresentations, int64_t *lastAudioEndTime, int64_t *actualAudioStartTime,
                         int segmentCounter, char * segFileNames)
{
    if (segmentCounter == 0)
    {
        return;
    }

    printf ("\nAudioGapMatrix\n");
    printf ("    \t");
    for (int i=0; i<numRepresentations; i++)
    {
       int arrayIndex = i + segmentCounter * numRepresentations;
       printf ("%s\t", segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
    }
    printf ("\n");

    for (int j1 = 0; j1<numRepresentations; j1++)
      {
          int arrayIndex0 = j1 + (segmentCounter - 1) * numRepresentations;
          int arrayIndex1 = j1 + segmentCounter * numRepresentations;

          printf ("%s\t", segFileNames + arrayIndex0 * SEGMENT_FILE_NAME_MAX_LENGTH);

          for (int j2 = 0; j2<numRepresentations; j2++)
          {
              int arrayIndex2 = j2 + segmentCounter * numRepresentations;
              if (lastAudioEndTime[arrayIndex1] == 0 || lastAudioEndTime[arrayIndex2] == 0)
              {
                  continue;
              }

              int64_t audioPTSDelta = actualAudioStartTime[arrayIndex2] - lastAudioEndTime[arrayIndex1];
              printf ("%"PRId64"\t", audioPTSDelta);
          }

          printf ("\n");
      }
}

void printVideoGapMatrix(int numRepresentations, int64_t *lastVideoEndTime, int64_t *actualVideoStartTime,
                         int segmentCounter, char * segFileNames)
{
    if (segmentCounter == 0)
    {
        return;
    }

    printf ("\nVideoGapMatrix\n");
    printf ("    \t");
    for (int i=0; i<numRepresentations; i++)
    {
       int arrayIndex = i + segmentCounter * numRepresentations;
       printf ("%s\t", segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
    }
    printf ("\n");

      for (int j1 = 0; j1<numRepresentations; j1++)
      {
          int arrayIndex0 = j1 + (segmentCounter - 1) * numRepresentations;
          int arrayIndex1 = j1 + segmentCounter * numRepresentations;

          printf ("%s\t", segFileNames + arrayIndex0 * SEGMENT_FILE_NAME_MAX_LENGTH);

          for (int j2 = 0; j2<numRepresentations; j2++)
          {
              int arrayIndex2 = j2 + segmentCounter * numRepresentations;

              if (lastVideoEndTime[arrayIndex1] == 0 || lastVideoEndTime[arrayIndex2] == 0)
              {
                  continue;
              }

              int64_t videoPTSDelta = actualVideoStartTime[arrayIndex2] - lastVideoEndTime[arrayIndex1];
              printf ("%"PRId64"\t", videoPTSDelta);
          }

          printf ("\n");
      }
}

void printVideoTimingMatrix(char * segFileNames, int64_t *actualVideoStartTime, int64_t *actualVideoEndTime,
    int64_t *expectedStartTime, int64_t *expectedEndTime, int numSegments, int numRepresentations,
    int representationNum)
{
    printf ("\nVideoTiming\n");
    printf ("segmentFile\texpectedStart\texpectedEnd\tvideoStart\tvideoEnd\tdeltaStart\tdeltaEnd\n");

      for (int i = 0; i<numSegments; i++)
      {
          int arrayIndex = representationNum + i * numRepresentations;
          printf ("%s\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\n", 
              segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH,
              expectedStartTime[arrayIndex], expectedEndTime[arrayIndex], actualVideoStartTime[arrayIndex], 
              actualVideoEndTime[arrayIndex], 
              actualVideoStartTime[arrayIndex] - expectedStartTime[arrayIndex],
              actualVideoEndTime[arrayIndex] - expectedEndTime[arrayIndex]);
      }
}

void printAudioTimingMatrix(char* segFileNames, int64_t *actualAudioStartTime, int64_t *actualAudioEndTime,
    int64_t *expectedStartTime, int64_t *expectedEndTime, int numSegments, int numRepresentations,
    int representationNum)
{
    printf ("\nAudioTiming\n");
    printf ("segmentFile\texpectedStart\texpectedEnd\taudioStart\taudioEnd\tdeltaStart\tdeltaEnd\n");
          
      for (int i = 0; i<numSegments; i++)
      {
          int arrayIndex = representationNum + i * numRepresentations;
          printf ("%s\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\t%"PRId64"\n", 
              segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH,
              expectedStartTime[arrayIndex], expectedEndTime[arrayIndex], actualAudioStartTime[arrayIndex], actualAudioEndTime[arrayIndex], 
              actualAudioStartTime[arrayIndex] - expectedStartTime[arrayIndex],
              actualAudioEndTime[arrayIndex] - expectedEndTime[arrayIndex]);
      }
}

int readSegInfoFile(char * fname, int *numRepresentations, int *numSegments, char **segFileNames, int **segDurations,
    int *expectedSAPType, int *maxVideoGapPTSTicks, int *maxAudioGapPTSTicks,
    int *segmentAlignment, int *subsegmentAlignment, int *bitstreamSwitching,
    char *initializationSegment, char **representationIndexFileNames)
{

   if (getNumRepresentations (fname, numRepresentations, numSegments) < 0)
   {
      LOG_ERROR("Error reading segment_info file"); 
      return -1;
   }

   FILE *segInfoFile = fopen(fname, "r");
   if (!segInfoFile) 
   {
      LOG_ERROR_ARGS("Couldn't open segInfoFile %s\n", fname); 
      return 0;
   }

   char line[SEGMENT_FILE_NAME_MAX_LENGTH];
   *segFileNames = (char *)calloc ((*numRepresentations) * (*numSegments), SEGMENT_FILE_NAME_MAX_LENGTH);
   *segDurations = (int *)calloc (*numSegments, sizeof (int));

   char paramName[256];
   strcpy (paramName, "startWithSAP");
   if (readIntFromSegInfoFile (segInfoFile, paramName, expectedSAPType) != 0)
   {
       return -1;
   }

   strcpy (paramName, "MaxAudioGap (PTS ticks)");
   if (readIntFromSegInfoFile (segInfoFile, paramName, maxAudioGapPTSTicks) != 0)
   {
       return -1;
   }

   strcpy (paramName, "MaxVideoGap (PTS ticks)");
   if (readIntFromSegInfoFile (segInfoFile, paramName, maxVideoGapPTSTicks) != 0)
   {
       return -1;
   }

   strcpy (paramName, "segmentAlignment");
   if (readIntFromSegInfoFile (segInfoFile, paramName, segmentAlignment) != 0)
   {
       return -1;
   }

   strcpy (paramName, "subsegmentAlignment");
   if (readIntFromSegInfoFile (segInfoFile, paramName, subsegmentAlignment) != 0)
   {
       return -1;
   }

   strcpy (paramName, "bitstreamSwitching");
   if (readIntFromSegInfoFile (segInfoFile, paramName, bitstreamSwitching) != 0)
   {
       return -1;
   }

   strcpy (paramName, "InitializationSegment");
   if (readStringFromSegInfoFile (segInfoFile, paramName, initializationSegment) != 0)
   {
       return -1;
   }

   char representationIndexSegmentString[1024];
   strcpy (paramName, "RepresentationIndexSegment");
   if (readStringFromSegInfoFile (segInfoFile, paramName, representationIndexSegmentString) != 0)
   {
       return -1;
   }
   *representationIndexFileNames = calloc (*numRepresentations, SEGMENT_FILE_NAME_MAX_LENGTH);
   parseRepresentationIndexFileLine (representationIndexSegmentString, *representationIndexFileNames, *numRepresentations);


   for (int i=0; i<*numSegments; i++)
   {
      if (fgets(line, sizeof(line), segInfoFile) == NULL)
      {
          LOG_ERROR ("ERROR: error reading SegInfoFile\n");
          return -1;
      }

      parseSegInfoFileLine (line, *segFileNames, i, *numRepresentations, &((*segDurations)[i]));
      LOG_INFO_ARGS("segDuration = %d", (*segDurations)[i]);
   }

    return 0;
}

int readIntFromSegInfoFile (FILE *segInfoFile, char * paramName, int *paramValue)
{
    char line[1024];
    char temp[1024];

    strcpy (temp, paramName);
    strcat (temp, " = %d\n");

   if (fgets(line, sizeof(line), segInfoFile) == NULL)
   {
       LOG_ERROR_ARGS ("ERROR: cannot read %s from SegInfoFile\n", paramName);
       return -1;
   }
   if (sscanf(line, temp, paramValue) != 1)
   {
       LOG_ERROR_ARGS ("ERROR: cannot parse %s from SegInfoFile\n", paramName);
       return -1;
   }
   LOG_INFO_ARGS ("%s = %d", paramName, *paramValue);

   return 0;
}

int readStringFromSegInfoFile (FILE *segInfoFile, char * paramName, char *paramValue)
{
    char line[1024];
    char temp[1024];

    strcpy (temp, paramName);
    strcat (temp, " = %s\n");

   if (fgets(line, sizeof(line), segInfoFile) == NULL)
   {
       LOG_ERROR_ARGS ("ERROR: cannot read %s from SegInfoFile\n", paramName);
       return -1;
   }
   if (sscanf(line, temp, paramValue) != 1)
   {
       strcpy (temp, paramName);
       strcat (temp, " =");
       line [strlen(temp)] = 0;

       if (strcmp(line, temp) == 0)
       {
           paramValue[0] = 0;
           return 0;
       }

       LOG_ERROR_ARGS ("ERROR: cannot parse %s from SegInfoFile\n", paramName);
       return -1;
   }
   LOG_INFO_ARGS ("%s = %s", paramName, paramValue);


   return 0;
}
