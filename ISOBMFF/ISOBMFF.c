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
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "common.h"
#include "ISOBMFF.h"
#include "log.h"

#define ISOBMF_2BYTE_SZ 2
#define ISOBMF_4BYTE_SZ 4
#define ISOBMF_8BYTE_SZ 8


void freeBoxes(int numBoxes, box_type_t *box_types, void ** box_data)
{
 //   LOG_INFO("freeBoxes\n");

    for (int i=0; i<numBoxes; i++)
    {
        switch (box_types[i])
        {
            case BOX_STYP:
            {
                data_styp_t *styp = (data_styp_t *) box_data[i];
                freeStyp (styp);
                break;
            }
            case BOX_SIDX:
            {
                data_sidx_t *sidx = (data_sidx_t *) box_data[i];
                freeSidx (sidx);
                break;
            }
            case BOX_PCRB:
            {
                data_pcrb_t *pcrb = (data_pcrb_t *) box_data[i];
                freePcrb (pcrb);
                break;
            }
            case BOX_SSIX:
            {
                data_ssix_t *ssix = (data_ssix_t *) box_data[i];
                freeSsix (ssix);
                break;
            }
            case BOX_EMSG:
            {
                data_emsg_t *emsg = (data_emsg_t *) box_data[i];
                freeEmsg (emsg);
                break;
            }
        }
    }
}


int validateRepresentationIndexSegmentBoxes(int numSegments, int numBoxes, box_type_t *box_types, void ** box_data,
    int *box_sizes, int *segmentDurations, data_segment_iframes_t *pIFrames, int presentationTimeOffset, int videoPID,
    unsigned char isSimpleProfile)
{
    /*
    A Representation Index Segment indexes all Media Segments of one Representation and is defined as follows:

    -- Each Representation Index Segment shall begin with an "styp" box, and the brand "risx" shall be
    present in the "styp" box. The conformance requirement of the brand "risx" is defined by this subclause.

    -- Each Media Segment is indexed by one or more Segment Index box(es); the boxes for a 
    given Media Segment are contiguous;

    -- Each Segment Index box may be followed by an 'ssix' and/or 'pcrb' box;

    -- The Segment Index for each Media Segments is concatenated in order, preceded by a 
    single Segment Index box that indexes the Index Segment. This initial Segment Index box shall 
    have one entry in its loop for each Media Segment, and each entry refers to the Segment 
    Index information for a single Media Segment.
   */

    int returnCode = 0;

    int boxIndex = 0;
    if (numBoxes == 0)
    {
        LOG_ERROR ("ERROR validating Representation Index Segment: no boxes in segment\n");
        returnCode = -1;
    }

    // first box must be a styp
    if (box_types[boxIndex] != BOX_STYP)
    {
        LOG_ERROR ("ERROR validating Representation Index Segment: first box not a styp\n");
        returnCode = -1;
    }
    
    // check brand
    data_styp_t * styp = (data_styp_t *)box_data[boxIndex];
    if (styp->major_brand != 0x72697378 /* 'risx' */)
    {
        LOG_INFO_ARGS ("styp brand = %x", styp->major_brand);
        LOG_ERROR ("ERROR validating Representation Index Segment: styp brand not risx\n");
        returnCode = -1;
    }

    boxIndex++;

    // second box must be a sidx that references other sidx boxes
    if (box_types[boxIndex] != BOX_SIDX)
    {
        LOG_ERROR ("ERROR validating Representation Index Segment: second box not a sidx\n");
        returnCode = -1;
    }


    // walk all references: they should all be of type 1 and should point to sidx boxes
    data_sidx_t * masterSidx = (data_sidx_t *)box_data[boxIndex];
    unsigned int masterReferenceID = masterSidx->reference_ID;
    if (masterReferenceID != videoPID)
    {
        LOG_ERROR_ARGS ("ERROR validating Representation Index Segment: master ref ID does not equal \
video PID.  Expected %d, actual %d\n", videoPID, masterReferenceID);
        returnCode = -1;
    }
    for (int i=0; i<masterSidx->reference_count; i++)
    {
        data_sidx_reference_t ref = masterSidx->references[i];
        if (ref.reference_type != 1)
        {
            LOG_ERROR ("ERROR validating Representation Index Segment: reference type not 1\n");
            returnCode = -1;
        }

        // validate duration
        if (segmentDurations[i] != ref.subsegment_duration)
        {
            LOG_ERROR_ARGS ("ERROR validating Representation Index Segment: master ref segment duration does not equal \
segment duration.  Expected %d, actual %d\n", segmentDurations[i], ref.subsegment_duration);
            returnCode = -1;
        }
    }
    boxIndex++;

    int segmentIndex = -1;
    int ssixPresent = 0;
    int pcrbPresent = 0;
    int numNestedSidx = 0;
    unsigned int referenced_size = 0;

    uint64_t segmentStartTime = presentationTimeOffset;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    while (boxIndex < numBoxes)
    {
        if (box_types[boxIndex] == BOX_SIDX)
        {
            ssixPresent = 0;
            pcrbPresent = 0;

            data_sidx_t * sidx = (data_sidx_t *)box_data[boxIndex];
            if (numNestedSidx > 0)
            {
                numNestedSidx--;
                // GORP: check earliest presentation time
            }
            else
            {
                // check size:
                LOG_INFO_ARGS ("Validating referenced_size for reference %d.", segmentIndex);
                if (segmentIndex >= 0 && referenced_size != masterSidx->references[segmentIndex].referenced_size)
                {
                    LOG_ERROR_ARGS ("ERROR validating Representation Index Segment: referenced_size for reference %d. \
Expected %d, actual %d\n", segmentIndex, masterSidx->references[segmentIndex].referenced_size, referenced_size);
                    returnCode = -1;
                }
                        
                referenced_size = 0;
                segmentIndex++;
                if (segmentIndex > 0)
                {
                    segmentStartTime += segmentDurations[segmentIndex - 1];
                }

                LOG_INFO_ARGS ("Validating earliest_presentation_time for reference %d.", segmentIndex);
                if (segmentStartTime != sidx->earliest_presentation_time)
                {
                    LOG_ERROR_ARGS ("ERROR validating Representation Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64"\n", segmentStartTime, sidx->earliest_presentation_time);
                    returnCode = -1;
                }
            }
            referenced_size += sidx->size;

            LOG_INFO ("Validating reference_id");
            if (masterReferenceID != sidx->reference_ID)
            {
                 LOG_ERROR_ARGS ("ERROR validating Representation Index Segment: invalid reference id in sidx box. \
Expected %d, actual %d\n", masterReferenceID, sidx->reference_ID);
                 returnCode = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if (analyzeSidxReferences (sidx, &(pIFrames[segmentIndex].numIFrames), &numNestedSidx, isSimpleProfile) != 0)
            {
                 returnCode = -1;
            }
        }
        else
        {
            // must be a ssix or pcrb box
            if (box_types[boxIndex] == BOX_SSIX)
            {
                data_ssix_t * ssix = (data_ssix_t *)box_data[boxIndex];
                referenced_size += ssix->size;
                LOG_INFO ("Validating ssix box");
                if (ssixPresent)
                {
                    LOG_ERROR ("ERROR validating Representation Index Segment: More than one ssix box following sidx box\n");
                    returnCode = -1;
                }
                else
                {
                    ssixPresent = 1;
                }
            }
            else if (box_types[boxIndex] == BOX_PCRB)
            {
                data_pcrb_t * pcrb = (data_pcrb_t *)box_data[boxIndex];
                referenced_size += pcrb->size;
                LOG_INFO ("Validating pcrb box");
                if (pcrbPresent)
                {
                    LOG_ERROR ("ERROR validating Representation Index Segment: More than one pcrb box following sidx box\n");
                    returnCode = -1;
                }
                else
                {
                    pcrbPresent = 1;
                }
            }
        }

        boxIndex++;
    }

    // check the last reference size -- the last one is not checked in the above loop
    LOG_INFO_ARGS ("Validating referenced_size for reference %d. \
Expected %d, actual %d\n", segmentIndex, masterSidx->references[segmentIndex].referenced_size, referenced_size);
    if (segmentIndex >= 0 && referenced_size != masterSidx->references[segmentIndex].referenced_size)
    {
        LOG_ERROR_ARGS ("ERROR validating Representation Index Segment: referenced_size for reference %d. \
Expected %d, actual %d\n", segmentIndex, masterSidx->references[segmentIndex].referenced_size, referenced_size);
        returnCode = -1;
    }

    if (numNestedSidx != 0)
    {
        LOG_ERROR_ARGS ("ERROR validating Representation Index Segment: Incorrect number of nested sidx boxes: %d\n", numNestedSidx);
        returnCode = -1;
    }

    if ((segmentIndex+1) != numSegments)
    {
        LOG_ERROR_ARGS ("ERROR validating Representation Index Segment: Invalid number of segment sidx boxes following master sidx box: \
expected %d, found %d\n", numSegments, segmentIndex);
        returnCode = -1;
    }

    // fill in iFrame locations by walking the list of sidx's again, starting from the third box
    boxIndex = 2;
    numNestedSidx = 0;
    segmentIndex = -1;
    int nIFrameCntr = 0;
    unsigned int lastIFrameDuration = 0;
    uint64_t nextIFrameByteLocation = 0;
    segmentStartTime = presentationTimeOffset;
    while (boxIndex < numBoxes)
    {
        if (box_types[boxIndex] == BOX_SIDX)
        {
            data_sidx_t * sidx = (data_sidx_t *)box_data[boxIndex];

            if (numNestedSidx > 0)
            {
                numNestedSidx--;
                nextIFrameByteLocation += sidx->first_offset;  // convert from 64-bit t0 32 bit
            }
            else
            {
                segmentIndex++;
                if (segmentIndex > 0)
                {
                    segmentStartTime += segmentDurations[segmentIndex - 1];
                }

                nIFrameCntr = 0;
                nextIFrameByteLocation = sidx->first_offset;
                if (segmentIndex < numSegments)
                {
                    pIFrames[segmentIndex].doIFrameValidation = 1;
                    pIFrames[segmentIndex].pIFrameLocations_Time = ( unsigned int *)calloc (pIFrames[segmentIndex].numIFrames, sizeof (unsigned int));
                    pIFrames[segmentIndex].pIFrameLocations_Byte = ( uint64_t *)calloc (pIFrames[segmentIndex].numIFrames, sizeof (uint64_t));
                    pIFrames[segmentIndex].pStartsWithSAP = ( unsigned char *)calloc (pIFrames[segmentIndex].numIFrames, sizeof (unsigned char));
                    pIFrames[segmentIndex].pSAPType = ( unsigned char *)calloc (pIFrames[segmentIndex].numIFrames, sizeof (unsigned char));
                }
            }

            // fill in Iframe locations here
            for (int i=0; i<sidx->reference_count; i++)
            {
                data_sidx_reference_t ref = sidx->references[i];
                if (ref.reference_type == 0)
                {
                    (pIFrames[segmentIndex]).pStartsWithSAP[nIFrameCntr] = ref.starts_with_SAP;
                    (pIFrames[segmentIndex]).pSAPType[nIFrameCntr] = ref.SAP_type;
                    (pIFrames[segmentIndex]).pIFrameLocations_Byte[nIFrameCntr] = nextIFrameByteLocation;
                    

                    if (nIFrameCntr ==0)
                    {
                        (pIFrames[segmentIndex]).pIFrameLocations_Time[nIFrameCntr] = segmentStartTime + ref.SAP_delta_time;
                    }
                    else
                    {
                        (pIFrames[segmentIndex]).pIFrameLocations_Time[nIFrameCntr] = 
                            (pIFrames[segmentIndex]).pIFrameLocations_Time[nIFrameCntr-1] + lastIFrameDuration + ref.SAP_delta_time;
                    }
                    nIFrameCntr++;
                    lastIFrameDuration = ref.subsegment_duration;
                    nextIFrameByteLocation += ref.referenced_size;
                }
                else
                {
                    numNestedSidx++;
                }
            }
        }

        boxIndex++;
    }


    return returnCode;
}

int validateSingleIndexSegmentBoxes(int numBoxes, box_type_t *box_types, void ** box_data, int *box_sizes,
    int segmentDuration, data_segment_iframes_t *pIFrames, int presentationTimeOffset, int videoPID,
    unsigned char isSimpleProfile)
{
/*
 A Single Index Segment indexes exactly one Media Segment and is defined as follows: 

 -- Each Single Index Segment shall begin with a "styp" box, and the brand "sisx"
 shall be present in the "styp" box. The conformance requirement of the brand "sisx" is defined in this subclause.
 
 -- Each Single Index Segment shall contain one or more 'sidx' boxes which index one Media Segment. 
 
 -- A Single Index Segment may contain one or multiple "ssix" boxes. If present, the "ssix"
 shall follow the "sidx" box that documents the same Subsegment without any other "sidx" preceding the "ssix".
 
 -- A Single Index Segment may contain one or multiple "pcrb" boxes as defined in 6.4.7.2.
 If present, "pcrb" shall follow the "sidx" box that documents the same Subsegments, i.e. a "pcrb"
 box provides PCR information for every subsegment indexed in the last "sidx" box.

*/

    int returnCode = 0;

    int boxIndex = 0;
    if (numBoxes == 0)
    {
        LOG_ERROR ("ERROR validating Single Index Segment: no boxes in segment\n");
        returnCode = -1;
    }

    // first box must be a styp
    if (box_types[boxIndex] != BOX_STYP)
    {
        LOG_ERROR ("ERROR validating Single Index Segment: first box not a styp\n");
        returnCode = -1;
    }
    
    // check brand
    data_styp_t * styp = (data_styp_t *)box_data[boxIndex];
    if (styp->major_brand != 0x73697378 /* 'sisx' */)
    {
        LOG_INFO_ARGS ("styp brand = %x", styp->major_brand);
        LOG_ERROR ("ERROR validating Single Index Segment: styp brand not risx\n");
        returnCode = -1;
    }

    boxIndex++;

    int ssixPresent = 0;
    int pcrbPresent = 0;
    int numNestedSidx = 0;
    unsigned int referenced_size = 0;

    uint64_t segmentStartTime = presentationTimeOffset;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    while (boxIndex < numBoxes)
    {
        if (box_types[boxIndex] == BOX_SIDX)
        {
            ssixPresent = 0;
            pcrbPresent = 0;

            data_sidx_t * sidx = (data_sidx_t *)box_data[boxIndex];
            if (numNestedSidx > 0)
            {
                numNestedSidx--;
                // GORP: check earliest presentation time
            }
            else
            {                        
                referenced_size = 0;

                LOG_INFO ("Validating earliest_presentation_time");
                if (segmentStartTime != sidx->earliest_presentation_time)
                {
                    LOG_ERROR_ARGS ("ERROR validating Single Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64"\n", segmentStartTime, sidx->earliest_presentation_time);
                    returnCode = -1;
                }
            }
            referenced_size += sidx->size;

            LOG_INFO ("Validating reference_id");
            if (videoPID != sidx->reference_ID)
            {
                 LOG_ERROR_ARGS ("ERROR validating Single Index Segment: invalid reference id in sidx box. \
Expected %d, actual %d\n", videoPID, sidx->reference_ID);
                 returnCode = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if (analyzeSidxReferences (sidx, &(pIFrames->numIFrames), &numNestedSidx, isSimpleProfile) != 0)
            {
                 returnCode = -1;
            }
        }
        else
        {
            // must be a ssix or pcrb box
            if (box_types[boxIndex] == BOX_SSIX)
            {
                data_ssix_t * ssix = (data_ssix_t *)box_data[boxIndex];
                referenced_size += ssix->size;
                LOG_INFO ("Validating ssix box");
                if (ssixPresent)
                {
                    LOG_ERROR ("ERROR validating Single Index Segment: More than one ssix box following sidx box\n");
                    returnCode = -1;
                }
                else
                {
                    ssixPresent = 1;
                }
            }
            else if (box_types[boxIndex] == BOX_PCRB)
            {
                data_pcrb_t * pcrb = (data_pcrb_t *)box_data[boxIndex];
                referenced_size += pcrb->size;
                LOG_INFO ("Validating pcrb box");
                if (pcrbPresent)
                {
                    LOG_ERROR ("ERROR validating Single Index Segment: More than one pcrb box following sidx box\n");
                    returnCode = -1;
                }
                else
                {
                    pcrbPresent = 1;
                }
            }
        }

        boxIndex++;
    }

    if (numNestedSidx != 0)
    {
        LOG_ERROR_ARGS ("ERROR validating Single Index Segment: Incorrect number of nested sidx boxes: %d\n", numNestedSidx);
        returnCode = -1;
    }

    // fill in iFrame locations by walking the list of sidx's again, startng from box 1
                    
    pIFrames->doIFrameValidation = 1;
    pIFrames->pIFrameLocations_Time = ( unsigned int *)calloc (pIFrames->numIFrames, sizeof (unsigned int));
    pIFrames->pIFrameLocations_Byte = ( uint64_t *)calloc (pIFrames->numIFrames, sizeof (uint64_t));
    pIFrames->pStartsWithSAP = ( unsigned char *)calloc (pIFrames->numIFrames, sizeof (unsigned char));
    pIFrames->pSAPType = ( unsigned char *)calloc (pIFrames->numIFrames, sizeof (unsigned char));

    boxIndex = 1;
    numNestedSidx = 0;
    int nIFrameCntr = 0;
    unsigned int lastIFrameDuration = 0;
    uint64_t nextIFrameByteLocation = 0;
    segmentStartTime = presentationTimeOffset;
    while (boxIndex < numBoxes)
    {
        if (box_types[boxIndex] == BOX_SIDX)
        {
            data_sidx_t * sidx = (data_sidx_t *)box_data[boxIndex];

            if (numNestedSidx > 0)
            {
                numNestedSidx--;
                nextIFrameByteLocation += sidx->first_offset;  // convert from 64-bit t0 32 bit
            }

            // fill in Iframe locations here
            for (int i=0; i<sidx->reference_count; i++)
            {
                data_sidx_reference_t ref = sidx->references[i];
                if (ref.reference_type == 0)
                {
                    pIFrames->pStartsWithSAP[nIFrameCntr] = ref.starts_with_SAP;
                    pIFrames->pSAPType[nIFrameCntr] = ref.SAP_type;
                    pIFrames->pIFrameLocations_Byte[nIFrameCntr] = nextIFrameByteLocation;

                    if (nIFrameCntr ==0)
                    {
                        pIFrames->pIFrameLocations_Time[nIFrameCntr] = segmentStartTime + ref.SAP_delta_time;
                    }
                    else
                    {
                        pIFrames->pIFrameLocations_Time[nIFrameCntr] = 
                            pIFrames->pIFrameLocations_Time[nIFrameCntr-1] + lastIFrameDuration + ref.SAP_delta_time;
                    }
                    nIFrameCntr++;
                    lastIFrameDuration = ref.subsegment_duration;
                    nextIFrameByteLocation += ref.referenced_size;
                }
                else
                {
                    numNestedSidx++;
                }
           }
        }

        boxIndex++;
    }

    return returnCode;
}

int analyzeSidxReferences (data_sidx_t * sidx, int *pNumIFrames, int *pNumNestedSidx, unsigned char isSimpleProfile)
{
    int originalNumNestedSidx = *pNumNestedSidx;
    int originalNumIFrames = *pNumIFrames;

    for (int i=0; i<sidx->reference_count; i++)
    {
        data_sidx_reference_t ref = sidx->references[i];
        if (ref.reference_type == 1)
        {
            (*pNumNestedSidx)++;
        }
        else
        {
            (*pNumIFrames)++;
        }
    }

    if (isSimpleProfile)
    {
        if (originalNumNestedSidx != *pNumNestedSidx && originalNumIFrames != *pNumIFrames)
        {
            // failure -- references contain references to both media and nested sidx boxes
            LOG_ERROR ("ERROR validating Representation Index Segment: Section 8.7.3: Simple profile requires that \
sidx boxes have either media references or sidx references, but not both.");
            return -1;
        }
    }

    return 0;
}

void printBoxes(int numBoxes, box_type_t *box_types, void ** box_data)
{
    for (int i=0; i<numBoxes; i++)
    {
        switch (box_types[i])
        {
            case BOX_STYP:
            {
                data_styp_t *styp = (data_styp_t *) box_data[i];
                printStyp (styp);
                break;
            }
            case BOX_SIDX:
            {
                data_sidx_t *sidx = (data_sidx_t *) box_data[i];
                printSidx (sidx);
                break;
            }
            case BOX_PCRB:
            {
                data_pcrb_t *pcrb = (data_pcrb_t *) box_data[i];
                printPcrb (pcrb);
                break;
            }
            case BOX_SSIX:
            {
                data_ssix_t *ssix = (data_ssix_t *) box_data[i];
                printSsix (ssix);
                break;
            }
            case BOX_EMSG:
            {
                data_emsg_t *emsg = (data_emsg_t *) box_data[i];
                printEmsg (emsg);
                break;
            }
        }
    }
}


int validateIndexSegment(char *fname, int numSegments, int *segmentDurations, data_segment_iframes_t *pIFrames,
                         int presentationTimeOffset, int videoPID, unsigned char isSimpleProfile)
{
    LOG_INFO_ARGS ("validateIndexSegment: %s", fname);
    int numBoxes; 
    box_type_t *box_types;
    void ** box_data;
    int *box_sizes;

    int nReturnCode = readBoxes(fname, &numBoxes, &box_types, &box_data, &box_sizes);
    if (nReturnCode != 0)
    {
        LOG_ERROR ("ERROR validating Index Segment: Error reading boxes from file\n");
        return -1;
    }

    printBoxes(numBoxes, box_types, box_data);

    if (numSegments <= 0)
    {
        freeBoxes(numBoxes, box_types, box_data);
        LOG_ERROR ("ERROR validating Index Segment: Invalid number of segments");
        return -1;
    }
    else if (numSegments == 1)
    {
        nReturnCode = validateSingleIndexSegmentBoxes(numBoxes, box_types, box_data, box_sizes, 
            segmentDurations[0], pIFrames, presentationTimeOffset, videoPID, isSimpleProfile);
    }
    else
    {
        nReturnCode = validateRepresentationIndexSegmentBoxes(numSegments, numBoxes, box_types, box_data, box_sizes, 
            segmentDurations, pIFrames, presentationTimeOffset, videoPID, isSimpleProfile);
    }

    freeBoxes(numBoxes, box_types, box_data);

   printf ("\n\n");
   for (int i=0; i<numSegments; i++) 
   {
       printf ("data_segment_iframes %d: doIFrameValidation = %d, numIFrames = %d\n",
           i, pIFrames[i].doIFrameValidation, pIFrames[i].numIFrames);
       for (int j=0; j<pIFrames[i].numIFrames; j++)
       {
            printf ("   pIFrameLocations_Time[%d] = %d, \tpIFrameLocations_Byte[%d] = %"PRId64"\n", j, 
                pIFrames[i].pIFrameLocations_Time[j], j, pIFrames[i].pIFrameLocations_Byte[j]);
       }
   }

    return nReturnCode;
}


int readBoxes(char *fname, int *pNumBoxes, box_type_t **box_types_in, void *** box_data_in, int **box_sizes_in)
{
    struct stat st; 
    if (stat(fname, &st) != 0)
    {
        LOG_ERROR_ARGS ("ERROR validating Index Segment: Error getting file size for %s\n", fname);
        return -1;
    }
        
    FILE *indexFile = fopen(fname, "r");
    if (!indexFile) 
    {
        LOG_ERROR_ARGS("ERROR validating Index Segment: Couldn't open indexFile %s\n", fname); 
        return -1;
    }

    int bufferSz = st.st_size;
    unsigned char *buffer = (unsigned char *)malloc (bufferSz);
       
    if (fread (buffer, 1, bufferSz, indexFile) != bufferSz)
    {
        free (buffer);
        fclose (indexFile);
        return -1;
    }

    fclose (indexFile);

    int returnCode = readBoxes2(buffer, bufferSz, pNumBoxes, box_types_in, box_data_in, box_sizes_in);
    free (buffer);
    return returnCode;
}

int readBoxes2(unsigned char *buffer, int bufferSz, int *pNumBoxes, box_type_t **box_types_in, void *** box_data_in, int **box_sizes_in)
{
//    LOG_INFO ("readBoxes2\n");
   int numBoxes = 0;
   if (getNumBoxes(buffer, bufferSz, &numBoxes) != 0)
   {
       LOG_ERROR("ERROR validating Index Segment: Error reading number of boxes in buffer\n"); 
       return -1;
   }
   *pNumBoxes = numBoxes;

   box_type_t *box_types = (box_type_t *)malloc (numBoxes * sizeof (box_type_t));
   void ** box_data = (void **)malloc (numBoxes * sizeof (void *));
   int* box_sizes = (int *)malloc (numBoxes * sizeof (int));
   *box_types_in = box_types;
   *box_data_in = box_data;
   *box_sizes_in = box_sizes;

   int index = 0;

   for (int i=0; i<numBoxes; i++)
   {
//       LOG_INFO_ARGS ("Reading box %d, index = %d\n", i, index);
       unsigned int size = 0;
       unsigned int type = 0;
       memcpy(&size, &(buffer[index]), ISOBMF_4BYTE_SZ);
       index += ISOBMF_4BYTE_SZ;
       size = ntohl(size);
//       LOG_INFO_ARGS ("size: %u\n", size);
       box_sizes[i] = size;

       memcpy(&type, &(buffer[index]), ISOBMF_4BYTE_SZ);
       index += ISOBMF_4BYTE_SZ;
       type = ntohl(type);
       char strType[] = {0,0,0,0,0};
       convertUintToString(strType, type);
//       LOG_INFO_ARGS ("type: %s\n", strType);

       unsigned int boxBufferSz = size - 8;

       if (strcmp(strType, "styp") == 0)
       {
          data_styp_t *styp = (data_styp_t *)malloc(sizeof(data_styp_t));
          int nReturnCode = parseStyp(&(buffer[index]), boxBufferSz, styp);
          if (nReturnCode != 0)
          {
              LOG_ERROR ("ERROR validating Index Segment: ERROR parsing styp box\n");
              return -1;
          }
          box_types[i] = BOX_STYP;
          box_data[i] = (void *) styp;
       }
       else if (strcmp(strType, "sidx") == 0)
       {
          data_sidx_t *sidx = (data_sidx_t *)malloc(sizeof(data_sidx_t));
          int nReturnCode = parseSidx(&(buffer[index]), boxBufferSz, sidx);
          if (nReturnCode != 0)
          {
              LOG_ERROR ("ERROR validating Index Segment: ERROR parsing sidx box\n");
              return -1;
          }
          box_types[i] = BOX_SIDX;
          box_data[i] = (void *) sidx;
       }
       else if (strcmp(strType, "pcrb") == 0)
       {
          data_pcrb_t *pcrb = (data_pcrb_t *)malloc(sizeof(data_pcrb_t));
          int nReturnCode = parsePcrb(&(buffer[index]), boxBufferSz, pcrb);
          if (nReturnCode != 0)
          {
              LOG_ERROR ("ERROR validating Index Segment: ERROR parsing pcrb box\n");
              return -1;
          }
          box_types[i] = BOX_PCRB;
          box_data[i] = (void *) pcrb;
       }
       else if (strcmp(strType, "ssix") == 0)
       {
          data_ssix_t *ssix = (data_ssix_t *)malloc(sizeof(data_ssix_t));
          int nReturnCode = parseSsix(&(buffer[index]), boxBufferSz, ssix);
          if (nReturnCode != 0)
          {
              LOG_ERROR ("ERROR validating Index Segment: ERROR parsing ssix box\n");
              return -1;
          }
          box_types[i] = BOX_SSIX;
          box_data[i] = (void *) ssix;
       }
       else if (strcmp(strType, "emsg") == 0)
       {
          data_emsg_t *emsg = (data_emsg_t *)malloc(sizeof(data_emsg_t));
          int nReturnCode = parseEmsg(&(buffer[index]), boxBufferSz, emsg);
          if (nReturnCode != 0)
          {
              LOG_ERROR ("ERROR validating EMSG: ERROR parsing emsg box\n");
              return -1;
          }
          box_types[i] = BOX_EMSG;
          box_data[i] = (void *) emsg;
       }
       else
       {
           LOG_ERROR_ARGS ("ERROR validating EMSG: Invalid box type found: %s\n", strType);
           return -1;
       }

       index += boxBufferSz; 
   }

   return 0;
}


int getNumBoxes(unsigned char *buffer, int bufferSz, int *pNumBoxes)
{
//   LOG_INFO ("getNumBoxes\n");

   int index = 0;

   *pNumBoxes = 0;
   while (index < bufferSz)
   {
 //      LOG_INFO_ARGS ("Reading box: index = =% d\n", index);

       unsigned int size = 0;
       memcpy(&size, &(buffer[index]), ISOBMF_4BYTE_SZ);
       index += ISOBMF_4BYTE_SZ;

       size = ntohl(size);
//       LOG_INFO_ARGS ("size: %u\n", size);

       index += (size - 4);

       (*pNumBoxes)++;
   }

   return 0;
}

void freeStyp(data_styp_t *styp)
{
//   LOG_INFO ("freeStyp\n");
    if (styp->compatible_brands != NULL)
    {
        free (styp->compatible_brands);
    }

    free (styp);
}

int parseStyp (unsigned char *buffer, int bufferSz, data_styp_t *styp)
{
//    LOG_INFO ("parseStyp\n");
    styp->size = bufferSz + 8;

    memcpy (&(styp->major_brand), buffer, ISOBMF_4BYTE_SZ);
    styp->major_brand = ntohl(styp->major_brand);

    memcpy (&(styp->minor_version), buffer + ISOBMF_4BYTE_SZ, ISOBMF_4BYTE_SZ);
    styp->minor_version = ntohl(styp->minor_version);

    styp->num_compatible_brands = (bufferSz - 8)/ISOBMF_4BYTE_SZ;

    if (styp->num_compatible_brands != 0)
    {
        styp->compatible_brands = (unsigned int *)malloc(styp->num_compatible_brands * sizeof(unsigned int));
        for (int i=0; i<styp->num_compatible_brands; i++)
        {
            unsigned int * compatible_brand_temp = styp->compatible_brands + ISOBMF_4BYTE_SZ * i;
            memcpy (compatible_brand_temp, buffer + 8 + ISOBMF_4BYTE_SZ * i, ISOBMF_4BYTE_SZ);
            *compatible_brand_temp = ntohl(*compatible_brand_temp);
        }
    }

    return 0;
}

void freeSidx(data_sidx_t *sidx)
{
//   LOG_INFO ("freeSidx\n");
    if (sidx->references != NULL)
    {
        free (sidx->references);
    }

    free (sidx);
}

int parseSidx (unsigned char *buffer, int bufferSz, data_sidx_t *sidx)
{
    /*
aligned(8) class Box (unsigned int(32) boxtype, optional unsigned int(8)[16] extended_type) 
{ 
   unsigned int(32) size; 
   unsigned int(32) type = boxtype; 
   if (size==1) { 
      unsigned int(64) largesize; 
   } 
   else if (size==0) { 
      // box extends to end of file 
   } 
   if (boxtype=="uuid") {
      unsigned int(8)[16] usertype = extended_type; 
   } 
}


aligned(8) class FullBox(unsigned int(32) boxtype, unsigned int(8) v, bit(24) f) extends Box(boxtype) { 
   unsigned int(8) version = v; 
   bit(24) flags = f;
}


aligned(8) class SegmentIndexBox extends FullBox("sidx", version, 0) {
   unsigned int(32) reference_ID; 
   unsigned int(32) timescale; 
   if (version==0) { 
      unsigned int(32) earliest_presentation_time; 
      unsigned int(32) first_offset; 
   } 
   else { 
      unsigned int(64) earliest_presentation_time; 
      unsigned int(64) first_offset; 
   } 
   unsigned int(16) reserved = 0; 
   unsigned int(16) reference_count; 
   for(i=1; i <= reference_count; i++) 
   { 
      bit (1) reference_type; 
      unsigned int(31) referenced_size; 
      unsigned int(32) subsegment_duration; 
      bit(1) starts_with_SAP; 
      unsigned int(3) SAP_type; 
      unsigned int(28) SAP_delta_time; 
   } 
}
*/
//    LOG_INFO ("parseSidx\n");
    sidx->size = bufferSz + 8;
    int index = 0;
    sidx->version = buffer[index];
    index++;

    memcpy (&(sidx->flags), buffer + index, 3);
    sidx->flags = ntohl(sidx->flags >> 8);
    index += 3;

    memcpy (&(sidx->reference_ID), buffer + index, ISOBMF_4BYTE_SZ);
    sidx->reference_ID = ntohl(sidx->reference_ID);
    index += ISOBMF_4BYTE_SZ;

    memcpy (&(sidx->timescale), buffer + index, ISOBMF_4BYTE_SZ);
    sidx->timescale = ntohl(sidx->timescale);
    index += ISOBMF_4BYTE_SZ;

    if (sidx->version == 0)
    {
        unsigned int earliest_presentation_time;
        memcpy (&earliest_presentation_time, buffer + index, ISOBMF_4BYTE_SZ);
        sidx->earliest_presentation_time = ntohl(earliest_presentation_time);
        index += ISOBMF_4BYTE_SZ;

        unsigned int first_offset;
        memcpy (&first_offset, buffer + index, ISOBMF_4BYTE_SZ);
        sidx->first_offset = ntohl(first_offset);
        index += ISOBMF_4BYTE_SZ;
    }
    else
    {
        memcpy (&(sidx->earliest_presentation_time), buffer + index, ISOBMF_8BYTE_SZ);
        sidx->earliest_presentation_time = isobmff_ntohll(sidx->earliest_presentation_time);
        index += ISOBMF_8BYTE_SZ;

        memcpy (&(sidx->first_offset), buffer + index, ISOBMF_8BYTE_SZ);
        sidx->first_offset = isobmff_ntohll(sidx->first_offset);
        index += ISOBMF_8BYTE_SZ;
    }

    memcpy (&(sidx->reserved), buffer + index, ISOBMF_2BYTE_SZ);
    sidx->reserved = ntohs(sidx->reserved);
    index += ISOBMF_2BYTE_SZ;

    memcpy (&(sidx->reference_count), buffer + index, ISOBMF_2BYTE_SZ);
    sidx->reference_count = ntohs(sidx->reference_count);
    index += ISOBMF_2BYTE_SZ;

    sidx->references = (data_sidx_reference_t *) malloc (sidx->reference_count * sizeof (data_sidx_reference_t));

    for (int i=0; i<sidx->reference_count; i++)
    {
        sidx->references[i].reference_type = buffer[index] >> 7;
        memcpy (&(sidx->references[i].referenced_size), buffer + index, ISOBMF_4BYTE_SZ);
        index += ISOBMF_4BYTE_SZ;
        sidx->references[i].referenced_size = ntohl (sidx->references[i].referenced_size);
  //      printf ("RAW referenced_size = %x\n", sidx->references[i].referenced_size);

        sidx->references[i].referenced_size = sidx->references[i].referenced_size & 0x7fffffff;

        memcpy (&(sidx->references[i].subsegment_duration), buffer + index, ISOBMF_4BYTE_SZ);
        index += ISOBMF_4BYTE_SZ;
        sidx->references[i].subsegment_duration = ntohl (sidx->references[i].subsegment_duration);

        sidx->references[i].starts_with_SAP = ((buffer[index]) >> 7);
        sidx->references[i].SAP_type = ((buffer[index] >> 4) & 0x7);
        memcpy (&(sidx->references[i].SAP_delta_time), buffer + index, ISOBMF_4BYTE_SZ);
        index += ISOBMF_4BYTE_SZ;
        sidx->references[i].SAP_delta_time = ntohl (sidx->references[i].SAP_delta_time);
  //      printf ("RAW SAP_delta_time = %x\n", sidx->references[i].SAP_delta_time);

        sidx->references[i].SAP_delta_time = sidx->references[i].SAP_delta_time & 0x0fffffff;
    }


//    LOG_INFO_ARGS ("parseSidx: index = %d\n", index);


    return 0;
}

void freePcrb(data_pcrb_t *pcrb)
{
//    LOG_INFO ("freePcrb\n");
    free (pcrb);
}

int parsePcrb (unsigned char *buffer, int bufferSz, data_pcrb_t *pcrb)
{
    /*
    aligned(8) class ProducerReferenceTimeBox extends FullBox("prft", version, 0)
    { 
        unsigned int(32) reference_track_ID; 
        unsigned int(64) ntp_timestamp; 
        if (version==0) 
        { 
            unsigned int(32) media_time; 
        } 
        else 
        { 
            unsigned int(64) media_time; 
        } 
    }
    */

//    LOG_INFO ("parsePcrb\n");
    pcrb->size = bufferSz + 8;

    int index = 0;
    pcrb->version = buffer[index];
    index++;

    memcpy (&(pcrb->flags), buffer + index, 3);
    pcrb->flags = ntohl(pcrb->flags);
    index += 3;

    memcpy (&(pcrb->reference_track_ID), buffer + index, ISOBMF_4BYTE_SZ);
    pcrb->reference_track_ID = ntohl(pcrb->reference_track_ID);
    index += ISOBMF_4BYTE_SZ;

    memcpy (&(pcrb->ntp_timestamp), buffer + index, ISOBMF_8BYTE_SZ);
    pcrb->ntp_timestamp = isobmff_ntohll(pcrb->ntp_timestamp);
    index += ISOBMF_8BYTE_SZ;

    if (pcrb->version == 0)
    {
        unsigned int media_time;
        memcpy (&media_time, buffer + index, ISOBMF_4BYTE_SZ);
        index += ISOBMF_4BYTE_SZ;
        pcrb->media_time = ntohl(media_time);
    }
    else
    {
        memcpy (&(pcrb->media_time), buffer + index, ISOBMF_8BYTE_SZ);
        pcrb->media_time = isobmff_ntohll(pcrb->media_time);
        index += ISOBMF_8BYTE_SZ;
    }

    return 0;
}

void freeSsix(data_ssix_t *ssix)
{
//    LOG_INFO ("freeSsix\n");
    for (int i=0; i<ssix->subsegment_count; i++)
    {
        if (ssix->subsegments[i].ranges != NULL)
        {
            free (ssix->subsegments[i].ranges);
        }
    }

    if (ssix->subsegments != NULL)
    {
        free (ssix->subsegments);
    }

    free (ssix);
}

void freeEmsg(data_emsg_t *emsg)
{
//    LOG_INFO ("freeEmsg\n");

    free (emsg->scheme_id_uri); 
    free (emsg->value); 
    free (emsg->message_data); 

    free (emsg);
}

int parseSsix (unsigned char *buffer, int bufferSz, data_ssix_t *ssix)
{
/*
aligned(8) class SubsegmentIndexBox extends FullBox("ssix", 0, 0) {
   unsigned int(32) subsegment_count; 
   for( i=1; i <= subsegment_count; i++) 
   { 
      unsigned int(32) ranges_count; 
      for ( j=1; j <= range_count; j++) 
      { 
         unsigned int(8) level; 
         unsigned int(24) range_size; 
      } 
   } 
}
*/
//    LOG_INFO ("parseSsix\n");
    ssix->size = bufferSz + 8;

    int index = 0;
    ssix->version = buffer[index];
    index++;

    memcpy (&(ssix->flags), buffer + index, 3);
    ssix->flags = (ntohl(ssix->flags) >> 8);
    index += 3;

    memcpy (&(ssix->subsegment_count), buffer + index, ISOBMF_4BYTE_SZ);
    ssix->subsegment_count = ntohl(ssix->subsegment_count);
    index += ISOBMF_4BYTE_SZ;

    ssix->subsegments = (data_ssix_subsegment_t *) malloc (ssix->subsegment_count * sizeof (data_ssix_subsegment_t));

    for (int i=0; i<ssix->subsegment_count; i++)
    {
        memcpy (&(ssix->subsegments[i].ranges_count), buffer + index, ISOBMF_4BYTE_SZ);
        ssix->subsegments[i].ranges_count = ntohl(ssix->subsegments[i].ranges_count);
        index += ISOBMF_4BYTE_SZ;

        ssix->subsegments[i].ranges = (data_ssix_subsegment_range_t*) malloc(ssix->subsegments[i].ranges_count * 
            sizeof (data_ssix_subsegment_range_t));

        for (int ii=0; ii<ssix->subsegments[i].ranges_count; ii++)
        {
            ssix->subsegments[i].ranges[ii].level = buffer[index];
            index++;

            memcpy (&(ssix->subsegments[i].ranges[ii].range_size), buffer + index, 3);
            index += 3;
            ssix->subsegments[i].ranges[ii].range_size = (ntohl(ssix->subsegments[i].ranges[ii].range_size) >> 8);
        }
    }


    return 0;
}

int parseEmsg (unsigned char *buffer, int bufferSz, data_emsg_t *emsg)
{
/*
aligned(8) class DASHEventMessageBox extends FullBox("emsg", version = 0, flags = 0)
{ 
    string scheme_id_uri; 
    string value; 
    unsigned int(32) timescale; 
    unsigned int(32) presentation_time_delta; 
    unsigned int(32) event_duration; 
    unsigned int(32) id; 
    unsigned int(8) message_data[]; 
}
*/
//    LOG_INFO ("parseEmsg\n");

    int index = 0;
    emsg->size = bufferSz + 8;

    emsg->version = buffer[index];
    index++;

    memcpy (&(emsg->flags), buffer + index, 3);
    emsg->flags = ntohl(emsg->flags >> 8);
    index += 3;

    // look for null in buffer
    int i=index;
    int nullFound = 0;
    while (i<bufferSz)
    {
        if (buffer[i++] == 0)
        {
            nullFound = 1;
            break;
        }
    }
    if (!nullFound)
    {
        LOG_ERROR ("ERROR validating EMSG: Null terminator for scheme_id_uri not found in parseEmsg\n");
        return -1;
    }
    emsg->scheme_id_uri = (char *) malloc (i - index);
    memcpy (emsg->scheme_id_uri, buffer + index, i - index);
    index = i;

    // look for null in buffer
    i=index;
    nullFound = 0;
    while (i<bufferSz)
    {
        if (buffer[i++] == 0)
        {
            nullFound = 1;
            break;
        }
    }
    if (!nullFound)
    {
        LOG_ERROR ("ERROR validating EMSG: Null terminator for value not found in parseEmsg\n");
        return -1;
    }
    emsg->value = (char *) malloc (i - index);
    memcpy (emsg->value, buffer + index, i - index);
    index = i;

    memcpy (&(emsg->timescale), buffer + index, ISOBMF_4BYTE_SZ);
    emsg->timescale = ntohl(emsg->timescale);
    index += ISOBMF_4BYTE_SZ;

    memcpy (&(emsg->presentation_time_delta), buffer + index, ISOBMF_4BYTE_SZ);
    emsg->presentation_time_delta = ntohl(emsg->presentation_time_delta);
    index += ISOBMF_4BYTE_SZ;

    memcpy (&(emsg->event_duration), buffer + index, ISOBMF_4BYTE_SZ);
    emsg->event_duration = ntohl(emsg->event_duration);
    index += ISOBMF_4BYTE_SZ;

    memcpy (&(emsg->id), buffer + index, ISOBMF_4BYTE_SZ);
    emsg->id = ntohl(emsg->id);
    index += ISOBMF_4BYTE_SZ;

    unsigned char *message_data; 
    emsg->message_data_sz = bufferSz - index;
    if (emsg->message_data_sz != 0)
    {
        emsg->message_data = (unsigned char *) malloc (emsg->message_data_sz);
        memcpy (emsg->message_data, buffer + index, emsg->message_data_sz);
        index += emsg->message_data_sz;
    }

    return 0;
}

void printEmsg (data_emsg_t *emsg)
{
    printf ("\n####### EMSG ######\n");

    printf ("scheme_id_uri = %s\n", emsg->scheme_id_uri);
    printf ("value = %s\n", emsg->value);
    printf ("timescale = %d\n", emsg->timescale);
    printf ("presentation_time_delta = %d\n", emsg->presentation_time_delta);
    printf ("event_duration = %d\n", emsg->event_duration);
    printf ("id = %d\n", emsg->id);

    printf ("message_data:\n");
    int i=0;
    for (i=0; i<emsg->message_data_sz; i++)
    {
        printf ("0x%x ", emsg->message_data[i]);
        if (i%8 == 7)
        {
            printf ("\n");
        }
    }
            
    if (i%8 != 7)
    {
        printf ("\n");
    }

    printf ("###################\n\n");
}

void printStyp (data_styp_t *styp)
{
    char strTemp[] = {0,0,0,0,0};


    printf ("\n####### STYP ######\n");

    convertUintToString(strTemp, styp->major_brand);
    printf ("major_brand = %s\n", strTemp);

    printf ("minor_version = %u\n", styp->minor_version);

    printf ("num_compatible_brands = %u\n", styp->num_compatible_brands);
    for (int i=0; i<styp->num_compatible_brands; i++)
    {
        convertUintToString(strTemp, styp->compatible_brands[i]);
        printf ("    %d: %s\n", i, strTemp);
    }
    printf ("###################\n\n");
}

void printSidx (data_sidx_t *sidx)
{
    printf ("\n####### SIDX ######\n");

    printf ("size = %u\n", sidx->size);
    printf ("version = %u\n", sidx->version);
    printf ("flags = %u\n", sidx->flags);
    printf ("reference_ID = %u\n", sidx->reference_ID);
    printf ("timescale = %u\n", sidx->timescale);

    printf ("earliest_presentation_time = %"PRId64"\n", sidx->earliest_presentation_time);
    printf ("first_offset = %"PRId64"\n", sidx->first_offset);

    printf ("reserved = %u\n", sidx->reserved);
    printf ("reference_count = %u\n", sidx->reference_count);

    for (int i=0; i<sidx->reference_count; i++)
    {
        printSidxReference(&(sidx->references[i]));
    }

    printf ("###################\n\n");
}

void printSidxReference(data_sidx_reference_t *reference)
{
    printf ("    SidxReference:\n");

    printf ("        reference_type = %u\n", reference->reference_type);
    printf ("        referenced_size = %u\n", reference->referenced_size);
    printf ("        subsegment_duration = %u\n", reference->subsegment_duration);
    printf ("        starts_with_SAP = %u\n", reference->starts_with_SAP);
    printf ("        SAP_type = %u\n", reference->SAP_type);
    printf ("        SAP_delta_time = %u\n", reference->SAP_delta_time);
}

void printSsixSubsegment(data_ssix_subsegment_t *subsegment)
{
    printf ("    SsixSubsegment:\n");

    printf ("        ranges_count = %u\n", subsegment->ranges_count);
    for (int i=0; i<subsegment->ranges_count; i++)
    {
        printf ("            level = %u, range_size = %u\n", subsegment->ranges[i].level, 
            subsegment->ranges[i].range_size);
    }
}

void printPcrb (data_pcrb_t *pcrb)
{
    printf ("\n####### PCRB ######\n");

    printf ("version = %u\n", pcrb->version);
    printf ("flags = %u\n", pcrb->flags);
    printf ("reference_track_ID = %u\n", pcrb->reference_track_ID);
    printf ("ntp_timestamp = %"PRId64"\n", pcrb->ntp_timestamp);
    printf ("media_time = %"PRId64"\n", pcrb->media_time);

    printf ("###################\n\n");
}

void printSsix (data_ssix_t *ssix)
{
    printf ("\n####### SSIX ######\n");

    printf ("version = %u\n", ssix->version);
    printf ("flags = %u\n", ssix->flags);
    printf ("subsegment_count = %u\n", ssix->subsegment_count);

    for (int i=0; i<ssix->subsegment_count; i++)
    {
        printSsixSubsegment(&(ssix->subsegments[i]));
    }

    printf ("###################\n\n");
}

void convertUintToString(char *str, unsigned int uintStr)
{
   str[0] = (uintStr >> 24) & 0xff;
   str[1] = (uintStr >> 16) & 0xff;
   str[2] = (uintStr >>  8) & 0xff;
   str[3] = (uintStr >>  0) & 0xff;
}

uint64_t isobmff_ntohll(uint64_t num)
{
    uint64_t num2 = (((uint64_t)ntohl((unsigned int)num)) << 32) + (uint64_t)ntohl((unsigned int)(num >> 32));
    return num2;
}

int validateEmsgMsg(unsigned char *buffer, int bufferSz, unsigned int segmentDuration)
{
    LOG_INFO ("validateEmsgMsg\n");
              
    int numBoxes; 
    box_type_t *box_types;
    void ** box_data;
    int *box_sizes;

    int nReturnCode = readBoxes2(buffer, bufferSz, &numBoxes, &box_types, &box_data, &box_sizes);
    if (nReturnCode != 0)
    {
        LOG_ERROR ("ERROR validating EMSG: Error reading boxes\n");
        return -1;
    }

    printBoxes(numBoxes, box_types, box_data);

    for (int i=0; i<numBoxes; i++)
    {
        if (box_types[i] != BOX_EMSG)
        {
            LOG_ERROR ("ERROR validating EMSG: Invalid box type found\n");
            freeBoxes(numBoxes, box_types, box_data);
            return -1;
        }

        // GORP: anything else to verify here??

        data_emsg_t *emsg = (data_emsg_t *)box_data[i];
        if (emsg->presentation_time_delta + emsg->event_duration > segmentDuration)
        {
            LOG_ERROR ("ERROR validating EMSG: event lasts longer tha segment duration\n");
            freeBoxes(numBoxes, box_types, box_data);
            return -1;
        }
    }

    freeBoxes(numBoxes, box_types, box_data);

    return nReturnCode;
}

void freeIFrames (data_segment_iframes_t *pIFrames, int numSegments)
{
    if (pIFrames == NULL)
    {
        return;
    }

    for (int i=0; i<numSegments; i++)
    {
        if (pIFrames[i].pIFrameLocations_Time != NULL)
        {
            free (pIFrames[i].pIFrameLocations_Time);
        }
        if (pIFrames[i].pIFrameLocations_Byte != NULL)
        {
            free (pIFrames[i].pIFrameLocations_Byte);
        }
        if (pIFrames[i].pStartsWithSAP != NULL)
        {
            free (pIFrames[i].pStartsWithSAP);
        }
        if (pIFrames[i].pSAPType != NULL)
        {
            free (pIFrames[i].pSAPType);
        }
    }
}
