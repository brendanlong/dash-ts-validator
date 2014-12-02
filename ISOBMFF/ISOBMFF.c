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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "common.h"
#include "ISOBMFF.h"
#include "log.h"

static uint8_t readUint8(uint8_t** buffer)
{
    uint8_t result;
    memcpy(&result, *buffer, sizeof(result));
    *buffer += sizeof(result);
    return result;
}

static uint16_t readUint16(uint8_t** buffer)
{
    uint16_t result;
    memcpy(&result, *buffer, sizeof(result));
    *buffer += sizeof(result);
    return ntohs(result);
}

static uint32_t readUint24(uint8_t** buffer)
{
    uint32_t result;
    memcpy(&result, *buffer, 3);
    *buffer += 3;
    return ntohl(result >> 8);
}

static uint32_t readUint32(uint8_t** buffer)
{
    uint32_t result;
    memcpy(&result, *buffer, sizeof(result));
    *buffer += sizeof(result);
    return ntohl(result);
}

static uint64_t readUint64(uint8_t** buffer)
{
    uint64_t result;
    memcpy(&result, *buffer, sizeof(result));
    *buffer += sizeof(result);
    return (((uint64_t)ntohl(result)) << 32) + (uint64_t)ntohl(
                        result >> 32);
}

static char* readString(uint8_t** buffer, uint8_t* end)
{
    for(size_t i = 0; *buffer < end; ++i, ++(*buffer)) {
        if ((*buffer)[i] == 0) {
            char* result = malloc(i);
            memcpy(result, *buffer, i);
            return result;
        }
    }
    return NULL;
}

void freeBoxes(size_t numBoxes, box_type_t* box_types, void** box_data)
{
    for(size_t i = 0; i < numBoxes; i++) {
        switch(box_types[i]) {
        case BOX_TYPE_STYP: {
            data_styp_t* styp = (data_styp_t*) box_data[i];
            freeStyp(styp);
            break;
        }
        case BOX_TYPE_SIDX: {
            data_sidx_t* sidx = (data_sidx_t*) box_data[i];
            freeSidx(sidx);
            break;
        }
        case BOX_TYPE_PCRB: {
            data_pcrb_t* pcrb = (data_pcrb_t*) box_data[i];
            freePcrb(pcrb);
            break;
        }
        case BOX_TYPE_SSIX: {
            data_ssix_t* ssix = (data_ssix_t*) box_data[i];
            freeSsix(ssix);
            break;
        }
        case BOX_TYPE_EMSG: {
            data_emsg_t* emsg = (data_emsg_t*) box_data[i];
            freeEmsg(emsg);
            break;
        }
        }
    }
}

int validateRepresentationIndexSegmentBoxes(size_t numSegments, size_t numBoxes, box_type_t* box_types,
        void** box_data,
        int* box_sizes, int* segmentDurations, data_segment_iframes_t* pIFrames, int presentationTimeOffset,
        int videoPID,
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
    if(numBoxes == 0) {
        LOG_ERROR("ERROR validating Representation Index Segment: no boxes in segment\n");
        returnCode = -1;
    }

    // first box must be a styp
    if(box_types[boxIndex] != BOX_TYPE_STYP) {
        LOG_ERROR("ERROR validating Representation Index Segment: first box not a styp\n");
        returnCode = -1;
    }

    // check brand
    data_styp_t* styp = (data_styp_t*)box_data[boxIndex];
    bool foundRisx = false;
    bool foundSsss = false;
    for(size_t i = 0; i < styp->num_compatible_brands; ++i) {
        uint32_t brand = styp->compatible_brands[i];
        if(brand == BRAND_RISX) {
            foundRisx = true;
        } else if (brand == BRAND_SSSS) {
            foundSsss = true;
        }
        if (foundRisx && foundSsss) {
            break;
        }
    }
    if(!foundRisx) {
        LOG_ERROR("ERROR validating Representation Index Segment: styp compatible brands does not contain \"risx\"\n");
        LOG_INFO("Brands found are:");
        LOG_INFO_ARGS("styp major brand = %x", styp->major_brand);
        for(size_t i = 0; i < styp->num_compatible_brands; ++i) {
            LOG_INFO_ARGS("styp compatible brand = %x", styp->compatible_brands[i]);
        }
        returnCode = -1;
    }

    boxIndex++;

    // second box must be a sidx that references other sidx boxes
    if(box_types[boxIndex] != BOX_TYPE_SIDX) {
        LOG_ERROR("ERROR validating Representation Index Segment: second box not a sidx\n");
        returnCode = -1;
    }

    // walk all references: they should all be of type 1 and should point to sidx boxes
    data_sidx_t* masterSidx = (data_sidx_t*)box_data[boxIndex];
    unsigned int masterReferenceID = masterSidx->reference_ID;
    if(masterReferenceID != videoPID) {
        LOG_ERROR_ARGS("ERROR validating Representation Index Segment: master ref ID does not equal \
video PID.  Expected %d, actual %d\n", videoPID, masterReferenceID);
        returnCode = -1;
    }
    for(size_t i = 0; i < masterSidx->reference_count; i++) {
        data_sidx_reference_t ref = masterSidx->references[i];
        if(ref.reference_type != 1) {
            LOG_ERROR("ERROR validating Representation Index Segment: reference type not 1\n");
            returnCode = -1;
        }

        // validate duration
        if(segmentDurations[i] != ref.subsegment_duration) {
            LOG_ERROR_ARGS("ERROR validating Representation Index Segment: master ref segment duration does not equal \
segment duration.  Expected %d, actual %d\n", segmentDurations[i], ref.subsegment_duration);
            returnCode = -1;
        }
    }
    boxIndex++;

    int segmentIndex = -1;
    bool ssixPresent = false;
    bool pcrbPresent = false;
    int numNestedSidx = 0;
    unsigned int referenced_size = 0;

    uint64_t segmentStartTime = presentationTimeOffset;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    while(boxIndex < numBoxes) {
        switch(box_types[boxIndex]) {
        case BOX_TYPE_SIDX: {
            ssixPresent = false;
            pcrbPresent = false;

            data_sidx_t* sidx = (data_sidx_t*)box_data[boxIndex];
            if(numNestedSidx > 0) {
                numNestedSidx--;
                // GORP: check earliest presentation time
            } else {
                // check size:
                LOG_INFO_ARGS("Validating referenced_size for reference %d.", segmentIndex);
                if(segmentIndex >= 0 && referenced_size != masterSidx->references[segmentIndex].referenced_size) {
                    LOG_ERROR_ARGS("ERROR validating Representation Index Segment: referenced_size for reference %d. \
Expected %d, actual %d\n", segmentIndex, masterSidx->references[segmentIndex].referenced_size,
                                   referenced_size);
                    returnCode = -1;
                }

                referenced_size = 0;
                segmentIndex++;
                if(segmentIndex > 0) {
                    segmentStartTime += segmentDurations[segmentIndex - 1];
                }

                LOG_INFO_ARGS("Validating earliest_presentation_time for reference %d.", segmentIndex);
                if(segmentStartTime != sidx->earliest_presentation_time) {
                    LOG_ERROR_ARGS("ERROR validating Representation Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64"\n", segmentStartTime, sidx->earliest_presentation_time);
                    returnCode = -1;
                }
            }
            referenced_size += sidx->size;

            LOG_INFO("Validating reference_id");
            if(masterReferenceID != sidx->reference_ID) {
                LOG_ERROR_ARGS("ERROR validating Representation Index Segment: invalid reference id in sidx box. \
Expected %d, actual %d\n", masterReferenceID, sidx->reference_ID);
                returnCode = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if(analyzeSidxReferences(sidx, &(pIFrames[segmentIndex].numIFrames), &numNestedSidx,
                                     isSimpleProfile) != 0) {
                returnCode = -1;
            }
            break;
        }
        case BOX_TYPE_SSIX: {
            data_ssix_t* ssix = (data_ssix_t*)box_data[boxIndex];
            referenced_size += ssix->size;
            LOG_INFO("Validating ssix box");
            if(ssixPresent) {
                LOG_ERROR("ERROR validating Representation Index Segment: More than one ssix box following sidx box\n");
                returnCode = -1;
            } else {
                ssixPresent = true;
            }
            if(pcrbPresent) {
                LOG_ERROR("ERROR validating Representation Index Segment: pcrb occurred before ssix. 6.4.6.4 says "
                        "\"The Subsegment Index box (‘ssix’) [...] shall follow immediately after the ‘sidx’ box that "
                        "documents the same Subsegment. [...] If the 'pcrb' box is present, it shall follow 'ssix'.\"\n");
                returnCode = -1;
            }
            if(!foundSsss) {
                LOG_ERROR("ERROR validating Representation Index Segment: Saw ssix box, but 'ssss' is not in compatible brands. See 6.4.6.4.\n");
                returnCode = -1;
            }
            break;
        }
        case BOX_TYPE_PCRB: {
            data_pcrb_t* pcrb = (data_pcrb_t*)box_data[boxIndex];
            referenced_size += pcrb->size;
            LOG_INFO("Validating pcrb box");
            if(pcrbPresent) {
                LOG_ERROR("ERROR validating Representation Index Segment: More than one pcrb box following sidx box\n");
                returnCode = -1;
            } else {
                pcrbPresent = true;
            }
            break;
        }
        default:
            LOG_ERROR_ARGS("Invalid box type: %x\n", box_types[boxIndex]);
            break;
        }
        boxIndex++;
    }

    // check the last reference size -- the last one is not checked in the above loop
    LOG_INFO_ARGS("Validating referenced_size for reference %d. \
Expected %d, actual %d\n", segmentIndex, masterSidx->references[segmentIndex].referenced_size,
                  referenced_size);
    if(segmentIndex >= 0 && referenced_size != masterSidx->references[segmentIndex].referenced_size) {
        LOG_ERROR_ARGS("ERROR validating Representation Index Segment: referenced_size for reference %d. \
Expected %d, actual %d\n", segmentIndex, masterSidx->references[segmentIndex].referenced_size,
                       referenced_size);
        returnCode = -1;
    }

    if(numNestedSidx != 0) {
        LOG_ERROR_ARGS("ERROR validating Representation Index Segment: Incorrect number of nested sidx boxes: %d\n",
                       numNestedSidx);
        returnCode = -1;
    }

    if((segmentIndex + 1) != numSegments) {
        LOG_ERROR_ARGS("ERROR validating Representation Index Segment: Invalid number of segment sidx boxes following master sidx box: \
expected %zu, found %d\n", numSegments, segmentIndex);
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
    while(boxIndex < numBoxes) {
        if(box_types[boxIndex] == BOX_TYPE_SIDX) {
            data_sidx_t* sidx = (data_sidx_t*)box_data[boxIndex];

            if(numNestedSidx > 0) {
                numNestedSidx--;
                nextIFrameByteLocation += sidx->first_offset;  // convert from 64-bit t0 32 bit
            } else {
                segmentIndex++;
                if(segmentIndex > 0) {
                    segmentStartTime += segmentDurations[segmentIndex - 1];
                }

                nIFrameCntr = 0;
                nextIFrameByteLocation = sidx->first_offset;
                if(segmentIndex < numSegments) {
                    pIFrames[segmentIndex].doIFrameValidation = 1;
                    pIFrames[segmentIndex].pIFrameLocations_Time = (unsigned int*)calloc(
                                pIFrames[segmentIndex].numIFrames, sizeof(unsigned int));
                    pIFrames[segmentIndex].pIFrameLocations_Byte = (uint64_t*)calloc(pIFrames[segmentIndex].numIFrames,
                            sizeof(uint64_t));
                    pIFrames[segmentIndex].pStartsWithSAP = (unsigned char*)calloc(pIFrames[segmentIndex].numIFrames,
                                                            sizeof(unsigned char));
                    pIFrames[segmentIndex].pSAPType = (unsigned char*)calloc(pIFrames[segmentIndex].numIFrames,
                                                      sizeof(unsigned char));
                }
            }

            // fill in Iframe locations here
            for(int i = 0; i < sidx->reference_count; i++) {
                data_sidx_reference_t ref = sidx->references[i];
                if(ref.reference_type == 0) {
                    (pIFrames[segmentIndex]).pStartsWithSAP[nIFrameCntr] = ref.starts_with_SAP;
                    (pIFrames[segmentIndex]).pSAPType[nIFrameCntr] = ref.SAP_type;
                    (pIFrames[segmentIndex]).pIFrameLocations_Byte[nIFrameCntr] = nextIFrameByteLocation;


                    if(nIFrameCntr == 0) {
                        (pIFrames[segmentIndex]).pIFrameLocations_Time[nIFrameCntr] = segmentStartTime + ref.SAP_delta_time;
                    } else {
                        (pIFrames[segmentIndex]).pIFrameLocations_Time[nIFrameCntr] =
                            (pIFrames[segmentIndex]).pIFrameLocations_Time[nIFrameCntr - 1] + lastIFrameDuration +
                            ref.SAP_delta_time;
                    }
                    nIFrameCntr++;
                    lastIFrameDuration = ref.subsegment_duration;
                    nextIFrameByteLocation += ref.referenced_size;
                } else {
                    numNestedSidx++;
                }
            }
        }

        boxIndex++;
    }


    return returnCode;
}

int validateSingleIndexSegmentBoxes(int numBoxes, box_type_t* box_types, void** box_data,
                                    int* box_sizes,
                                    int segmentDuration, data_segment_iframes_t* pIFrames, int presentationTimeOffset, int videoPID,
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
    if(numBoxes == 0) {
        LOG_ERROR("ERROR validating Single Index Segment: no boxes in segment\n");
        returnCode = -1;
    }

    // first box must be a styp
    if(box_types[boxIndex] != BOX_TYPE_STYP) {
        LOG_ERROR("ERROR validating Single Index Segment: first box not a styp\n");
        returnCode = -1;
    }

    // check brand
    data_styp_t* styp = (data_styp_t*)box_data[boxIndex];
    if(styp->major_brand != BRAND_SISX) {
        LOG_INFO_ARGS("styp brand = %x", styp->major_brand);
        LOG_ERROR("ERROR validating Single Index Segment: styp brand not risx\n");
        returnCode = -1;
    }

    boxIndex++;

    int ssixPresent = 0;
    int pcrbPresent = 0;
    int numNestedSidx = 0;
    unsigned int referenced_size = 0;

    uint64_t segmentStartTime = presentationTimeOffset;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    while(boxIndex < numBoxes) {
        if(box_types[boxIndex] == BOX_TYPE_SIDX) {
            ssixPresent = 0;
            pcrbPresent = 0;

            data_sidx_t* sidx = (data_sidx_t*)box_data[boxIndex];
            if(numNestedSidx > 0) {
                numNestedSidx--;
                // GORP: check earliest presentation time
            } else {
                referenced_size = 0;

                LOG_INFO("Validating earliest_presentation_time");
                if(segmentStartTime != sidx->earliest_presentation_time) {
                    LOG_ERROR_ARGS("ERROR validating Single Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64"\n", segmentStartTime, sidx->earliest_presentation_time);
                    returnCode = -1;
                }
            }
            referenced_size += sidx->size;

            LOG_INFO("Validating reference_id");
            if(videoPID != sidx->reference_ID) {
                LOG_ERROR_ARGS("ERROR validating Single Index Segment: invalid reference id in sidx box. \
Expected %d, actual %d\n", videoPID, sidx->reference_ID);
                returnCode = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if(analyzeSidxReferences(sidx, &(pIFrames->numIFrames), &numNestedSidx, isSimpleProfile) != 0) {
                returnCode = -1;
            }
        } else {
            // must be a ssix or pcrb box
            if(box_types[boxIndex] == BOX_TYPE_SSIX) {
                data_ssix_t* ssix = (data_ssix_t*)box_data[boxIndex];
                referenced_size += ssix->size;
                LOG_INFO("Validating ssix box");
                if(ssixPresent) {
                    LOG_ERROR("ERROR validating Single Index Segment: More than one ssix box following sidx box\n");
                    returnCode = -1;
                } else {
                    ssixPresent = 1;
                }
            } else if(box_types[boxIndex] == BOX_TYPE_PCRB) {
                data_pcrb_t* pcrb = (data_pcrb_t*)box_data[boxIndex];
                referenced_size += pcrb->size;
                LOG_INFO("Validating pcrb box");
                if(pcrbPresent) {
                    LOG_ERROR("ERROR validating Single Index Segment: More than one pcrb box following sidx box\n");
                    returnCode = -1;
                } else {
                    pcrbPresent = 1;
                }
            }
        }

        boxIndex++;
    }

    if(numNestedSidx != 0) {
        LOG_ERROR_ARGS("ERROR validating Single Index Segment: Incorrect number of nested sidx boxes: %d\n",
                       numNestedSidx);
        returnCode = -1;
    }

    // fill in iFrame locations by walking the list of sidx's again, startng from box 1

    pIFrames->doIFrameValidation = 1;
    pIFrames->pIFrameLocations_Time = (unsigned int*)calloc(pIFrames->numIFrames, sizeof(unsigned int));
    pIFrames->pIFrameLocations_Byte = (uint64_t*)calloc(pIFrames->numIFrames, sizeof(uint64_t));
    pIFrames->pStartsWithSAP = (unsigned char*)calloc(pIFrames->numIFrames, sizeof(unsigned char));
    pIFrames->pSAPType = (unsigned char*)calloc(pIFrames->numIFrames, sizeof(unsigned char));

    boxIndex = 1;
    numNestedSidx = 0;
    int nIFrameCntr = 0;
    unsigned int lastIFrameDuration = 0;
    uint64_t nextIFrameByteLocation = 0;
    segmentStartTime = presentationTimeOffset;
    while(boxIndex < numBoxes) {
        if(box_types[boxIndex] == BOX_TYPE_SIDX) {
            data_sidx_t* sidx = (data_sidx_t*)box_data[boxIndex];

            if(numNestedSidx > 0) {
                numNestedSidx--;
                nextIFrameByteLocation += sidx->first_offset;  // convert from 64-bit t0 32 bit
            }

            // fill in Iframe locations here
            for(int i = 0; i < sidx->reference_count; i++) {
                data_sidx_reference_t ref = sidx->references[i];
                if(ref.reference_type == 0) {
                    pIFrames->pStartsWithSAP[nIFrameCntr] = ref.starts_with_SAP;
                    pIFrames->pSAPType[nIFrameCntr] = ref.SAP_type;
                    pIFrames->pIFrameLocations_Byte[nIFrameCntr] = nextIFrameByteLocation;

                    if(nIFrameCntr == 0) {
                        pIFrames->pIFrameLocations_Time[nIFrameCntr] = segmentStartTime + ref.SAP_delta_time;
                    } else {
                        pIFrames->pIFrameLocations_Time[nIFrameCntr] =
                            pIFrames->pIFrameLocations_Time[nIFrameCntr - 1] + lastIFrameDuration + ref.SAP_delta_time;
                    }
                    nIFrameCntr++;
                    lastIFrameDuration = ref.subsegment_duration;
                    nextIFrameByteLocation += ref.referenced_size;
                } else {
                    numNestedSidx++;
                }
            }
        }

        boxIndex++;
    }

    return returnCode;
}

int analyzeSidxReferences(data_sidx_t* sidx, int* pNumIFrames, int* pNumNestedSidx,
                          unsigned char isSimpleProfile)
{
    int originalNumNestedSidx = *pNumNestedSidx;
    int originalNumIFrames = *pNumIFrames;

    for(int i = 0; i < sidx->reference_count; i++) {
        data_sidx_reference_t ref = sidx->references[i];
        if(ref.reference_type == 1) {
            (*pNumNestedSidx)++;
        } else {
            (*pNumIFrames)++;
        }
    }

    if(isSimpleProfile) {
        if(originalNumNestedSidx != *pNumNestedSidx && originalNumIFrames != *pNumIFrames) {
            // failure -- references contain references to both media and nested sidx boxes
            LOG_ERROR("ERROR validating Representation Index Segment: Section 8.7.3: Simple profile requires that \
sidx boxes have either media references or sidx references, but not both.");
            return -1;
        }
    }

    return 0;
}

void printBoxes(size_t numBoxes, box_type_t* box_types, void** box_data)
{
    for(size_t i = 0; i < numBoxes; i++) {
        switch(box_types[i]) {
        case BOX_TYPE_STYP: {
            printStyp((data_styp_t*)box_data[i]);
            break;
        }
        case BOX_TYPE_SIDX: {
            printSidx((data_sidx_t*)box_data[i]);
            break;
        }
        case BOX_TYPE_PCRB: {
            printPcrb((data_pcrb_t*)box_data[i]);
            break;
        }
        case BOX_TYPE_SSIX: {
            printSsix((data_ssix_t*)box_data[i]);
            break;
        }
        case BOX_TYPE_EMSG: {
            printEmsg((data_emsg_t*)box_data[i]);
            break;
        }
        }
    }
}

int validateIndexSegment(char* fname, size_t numSegments, int* segmentDurations,
                         data_segment_iframes_t* pIFrames,
                         int presentationTimeOffset, int videoPID, unsigned char isSimpleProfile)
{
    LOG_INFO_ARGS("validateIndexSegment: %s", fname);
    size_t numBoxes;
    box_type_t* box_types;
    void** box_data;
    int* box_sizes;

    int returnCode = readBoxes(fname, &numBoxes, &box_types, &box_data, &box_sizes);
    if(returnCode != 0) {
        LOG_ERROR("ERROR validating Index Segment: Error reading boxes from file\n");
        return -1;
    }

    printBoxes(numBoxes, box_types, box_data);

    if(numSegments <= 0) {
        freeBoxes(numBoxes, box_types, box_data);
        LOG_ERROR("ERROR validating Index Segment: Invalid number of segments");
        return -1;
    } else if(numSegments == 1) {
        returnCode = validateSingleIndexSegmentBoxes(numBoxes, box_types, box_data, box_sizes,
                      segmentDurations[0], pIFrames, presentationTimeOffset, videoPID, isSimpleProfile);
    } else {
        returnCode = validateRepresentationIndexSegmentBoxes(numSegments, numBoxes, box_types, box_data,
                      box_sizes,
                      segmentDurations, pIFrames, presentationTimeOffset, videoPID, isSimpleProfile);
    }

    freeBoxes(numBoxes, box_types, box_data);

    printf("\n\n");
    for(size_t i = 0; i < numSegments; i++) {
        printf("data_segment_iframes %zu: doIFrameValidation = %d, numIFrames = %d\n",
               i, pIFrames[i].doIFrameValidation, pIFrames[i].numIFrames);
        for(int j = 0; j < pIFrames[i].numIFrames; j++) {
            printf("   pIFrameLocations_Time[%d] = %d, \tpIFrameLocations_Byte[%d] = %"PRId64"\n", j,
                   pIFrames[i].pIFrameLocations_Time[j], j, pIFrames[i].pIFrameLocations_Byte[j]);
        }
    }

    return returnCode;
}


int readBoxes(char* fname, size_t* numBoxes, box_type_t** box_types_in, void** * box_data_in,
              int** box_sizes_in)
{
    struct stat st;
    if(stat(fname, &st) != 0) {
        LOG_ERROR_ARGS("ERROR validating Index Segment: Error getting file size for %s\n", fname);
        return -1;
    }

    FILE* indexFile = fopen(fname, "r");
    if(!indexFile) {
        LOG_ERROR_ARGS("ERROR validating Index Segment: Couldn't open indexFile %s\n", fname);
        return -1;
    }

    int bufferSize = st.st_size;
    unsigned char* buffer = malloc(bufferSize);

    if(fread(buffer, 1, bufferSize, indexFile) != bufferSize) {
        free(buffer);
        fclose(indexFile);
        return -1;
    }

    fclose(indexFile);

    int returnCode = readBoxes2(buffer, bufferSize, numBoxes, box_types_in, box_data_in, box_sizes_in);
    free(buffer);
    return returnCode;
}

int readBoxes2(unsigned char* buffer, int bufferSize, size_t* numBoxes, box_type_t** box_types_in,
               void** * box_data_in, int** box_sizes_in)
{
    if(getNumBoxes(buffer, bufferSize, numBoxes) != 0) {
        LOG_ERROR("ERROR validating Index Segment: Error reading number of boxes in buffer\n");
        return -1;
    }

    box_type_t* box_types = malloc((*numBoxes) * sizeof(box_type_t));
    void** box_data = malloc((*numBoxes) * sizeof(void*));
    int* box_sizes = malloc((*numBoxes) * sizeof(int));
    *box_types_in = box_types;
    *box_data_in = box_data;
    *box_sizes_in = box_sizes;

    for(int i = 0; i < *numBoxes; i++) {
        uint32_t size = readUint32(&buffer);
        box_types[i] = readUint32(&buffer);

        unsigned int boxBufferSize = size - 8;

        bool invalid = false;
        int returnCode = -1;
        switch(box_types[i]) {
        case BOX_TYPE_STYP:
            box_data[i] = malloc(sizeof(data_styp_t));
            returnCode = parseStyp(buffer, boxBufferSize, (data_styp_t*)box_data[i]);
            break;
        case BOX_TYPE_SIDX:
            box_data[i] = malloc(sizeof(data_sidx_t));
            returnCode = parseSidx(buffer, boxBufferSize, (data_sidx_t*)box_data[i]);
            break;
        case BOX_TYPE_PCRB:
            box_data[i] = malloc(sizeof(data_pcrb_t));
            returnCode = parsePcrb(buffer, boxBufferSize, (data_pcrb_t*)box_data[i]);
            break;
        case BOX_TYPE_SSIX:
            box_data[i] = malloc(sizeof(data_ssix_t));
            returnCode = parseSsix(buffer, boxBufferSize, (data_ssix_t*)box_data[i]);
            break;
        case BOX_TYPE_EMSG:
            box_data[i] = malloc(sizeof(data_emsg_t));
            returnCode = parseEmsg(buffer, boxBufferSize, (data_emsg_t*)box_data[i]);
            break;
        default:
            invalid = true;
            break;
        }
        if(returnCode != 0) {
            char strType[] = {0, 0, 0, 0, 0};
            convertUintToString(strType, box_types[i]);
            LOG_ERROR_ARGS("ERROR validating Index Segment: ERROR parsing %s box%s\n", strType, invalid ? " (unknown box type)" : "");
            return -1;
        }
        buffer += boxBufferSize;
    }

    return 0;
}

int getNumBoxes(unsigned char* buffer, int bufferSize, size_t* numBoxes)
{
    uint8_t* bufferEnd = buffer + bufferSize;
    *numBoxes = 0;
    while(buffer < bufferEnd) {
        uint32_t size = readUint32(&buffer);
        buffer += size - 4;
        (*numBoxes)++;
    }
    return 0;
}

typedef struct {
    uint32_t size;
} box_t;

int parseBox(uint8_t** buffer, int bufferSize, box_t* box)
{
    /*
    aligned(8) class Box (unsigned int(32) boxtype, optional unsigned int(8)[16] extended_type) {
        unsigned int(32) size;
        unsigned int(32) type = boxtype;
        if (size == 1) {
            unsigned int(64) largesize;
        } else if (size == 0) {
            // box extends to end of file
        }
        if (boxtype == "uuid") {
            unsigned int(8)[16] usertype = extended_type;
        }
    }
    */
    box->size = bufferSize + 8;
    return 0;
}

typedef struct {
    box_t box;
    uint8_t version;
    uint32_t flags;
} fullbox_t;

int parseFullBox(uint8_t** buffer, int bufferSize, fullbox_t* box)
{
    /*
    aligned(8) class FullBox(unsigned int(32) boxtype, unsigned int(8) v, bit(24) f) extends Box(boxtype) {
        unsigned int(8) version = v;
        bit(24) flags = f;
    }
    */
    int result = parseBox(buffer, bufferSize, (box_t*)box);
    if (result != 0) {
        return result;
    }
    box->version = readUint8(buffer);
    box->flags = readUint24(buffer);
    return 0;
}

void freeStyp(data_styp_t* styp)
{
    free(styp->compatible_brands);
    free(styp);
}

int parseStyp(unsigned char* buffer, int bufferSize, data_styp_t* styp)
{
    /*
    "A segment type has the same format as an 'ftyp' box [4.3], except that it takes the box type 'styp'."
    aligned(8) class FileTypeBox extends Box(‘ftyp’) {
        unsigned int(32) major_brand;
        unsigned int(32) minor_version;
        unsigned int(32) compatible_brands[];
    }
    */
    styp->size = bufferSize + 8;
    styp->major_brand = readUint32(&buffer);
    styp->minor_version = readUint32(&buffer);
    styp->num_compatible_brands = (bufferSize - 8) / 4;

    if(styp->num_compatible_brands != 0) {
        styp->compatible_brands = malloc(styp->num_compatible_brands * sizeof(uint32_t));
        for(int i = 0; i < styp->num_compatible_brands; i++) {
            styp->compatible_brands[i] = readUint32(&buffer);
        }
    }
    return 0;
}

void freeSidx(data_sidx_t* sidx)
{
    free(sidx->references);
    free(sidx);
}

int parseSidx(unsigned char* buffer, int bufferSize, data_sidx_t* sidx)
{
    /*
    aligned(8) class SegmentIndexBox extends FullBox("sidx", version, 0) {
        unsigned int(32) reference_ID;
        unsigned int(32) timescale;
        if (version == 0) {
            unsigned int(32) earliest_presentation_time;
            unsigned int(32) first_offset;
        } else {
            unsigned int(64) earliest_presentation_time;
            unsigned int(64) first_offset;
        }
        unsigned int(16) reserved = 0;
        unsigned int(16) reference_count;
        for(i = 1; i <= reference_count; i++) {
            bit (1) reference_type;
            unsigned int(31) referenced_size;
            unsigned int(32) subsegment_duration;
            bit(1) starts_with_SAP;
            unsigned int(3) SAP_type;
            unsigned int(28) SAP_delta_time;
        }
    }
    */
    int result = parseFullBox(&buffer, bufferSize, (fullbox_t*)sidx);
    if(result != 0) {
        return result;
    }

    sidx->reference_ID = readUint32(&buffer);
    sidx->timescale = readUint32(&buffer);

    if(sidx->version == 0) {
        sidx->earliest_presentation_time = readUint32(&buffer);
        sidx->first_offset = readUint32(&buffer);
    } else {
        sidx->earliest_presentation_time = readUint64(&buffer);
        sidx->first_offset = readUint64(&buffer);
    }

    sidx->reserved = readUint16(&buffer);
    sidx->reference_count = readUint16(&buffer);

    sidx->references = malloc(sidx->reference_count * sizeof(
                           data_sidx_reference_t));
    for(size_t i = 0; i < sidx->reference_count; i++) {
        sidx->references[i].reference_type = (*buffer) >> 7;
        sidx->references[i].referenced_size = readUint32(&buffer) & 0x7fffffff;
        sidx->references[i].subsegment_duration = readUint32(&buffer);
        sidx->references[i].starts_with_SAP = ((*buffer) >> 7);
        sidx->references[i].SAP_type = ((*buffer) >> 4) & 0x7;
        sidx->references[i].SAP_delta_time = readUint32(&buffer) & 0x0fffffff;
    }
    return 0;
}

void freePcrb(data_pcrb_t* pcrb)
{
    free(pcrb);
}

int parsePcrb(unsigned char* buffer, int bufferSize, data_pcrb_t* pcrb)
{
    /*
    aligned(8) class ProducerReferenceTimeBox extends FullBox("prft", version, 0)
    {
        unsigned int(32) reference_track_ID;
        unsigned int(64) ntp_timestamp;
        if (version == 0) {
            unsigned int(32) media_time;
        } else {
            unsigned int(64) media_time;
        }
    }
    */
    int result = parseFullBox(&buffer, bufferSize, (fullbox_t*)pcrb);
    if (result != 0) {
        return result;
    }

    pcrb->reference_track_ID = readUint32(&buffer);
    pcrb->ntp_timestamp = readUint64(&buffer);

    if(pcrb->version == 0) {
        pcrb->media_time = readUint32(&buffer);
    } else {
        pcrb->media_time = readUint64(&buffer);
    }
    return 0;
}

void freeSsix(data_ssix_t* ssix)
{
    for(int i = 0; i < ssix->subsegment_count; i++) {
        free(ssix->subsegments[i].ranges);
    }
    free(ssix->subsegments);
    free(ssix);
}

void freeEmsg(data_emsg_t* emsg)
{
    free(emsg->scheme_id_uri);
    free(emsg->value);
    free(emsg->message_data);

    free(emsg);
}

int parseSsix(unsigned char* buffer, int bufferSize, data_ssix_t* ssix)
{
    /*
    aligned(8) class SubsegmentIndexBox extends FullBox("ssix", 0, 0) {
       unsigned int(32) subsegment_count;
       for(i = 1; i <= subsegment_count; i++)
       {
          unsigned int(32) ranges_count;
          for (j = 1; j <= range_count; j++)
          {
             unsigned int(8) level;
             unsigned int(24) range_size;
          }
       }
    }
    */
    int result = parseFullBox(&buffer, bufferSize, (fullbox_t*)ssix);
    if(result != 0) {
        return result;
    }

    ssix->subsegment_count = readUint32(&buffer);
    ssix->subsegments = malloc(ssix->subsegment_count * sizeof(
                            data_ssix_subsegment_t));

    for(size_t i = 0; i < ssix->subsegment_count; i++) {
        ssix->subsegments[i].ranges_count = readUint32(&buffer);
        ssix->subsegments[i].ranges = malloc(
                                          ssix->subsegments[i].ranges_count *
                                          sizeof(data_ssix_subsegment_range_t));
        for(size_t ii = 0; ii < ssix->subsegments[i].ranges_count; ii++) {
            ssix->subsegments[i].ranges[ii].level = readUint8(&buffer);
            ssix->subsegments[i].ranges[ii].range_size = readUint24(&buffer);
        }
    }
    return 0;
}

int parseEmsg(unsigned char* buffer, int bufferSize, data_emsg_t* emsg)
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
    uint8_t* bufferEnd = buffer + bufferSize;
    int result = parseFullBox(&buffer, bufferSize, (fullbox_t*)emsg);
    if(result != 0) {
        return result;
    }

    emsg->scheme_id_uri = readString(&buffer, bufferEnd);
    if(emsg->scheme_id_uri == NULL) {
        LOG_ERROR("ERROR validating EMSG: Null terminator for scheme_id_uri not found in parseEmsg\n");
        return -1;
    }

    emsg->value = readString(&buffer, bufferEnd);
    if(emsg->scheme_id_uri == NULL) {
        LOG_ERROR("ERROR validating EMSG: Null terminator for value not found in parseEmsg\n");
        return -1;
    }

    emsg->timescale = readUint32(&buffer);
    emsg->presentation_time_delta = readUint32(&buffer);
    emsg->event_duration = readUint32(&buffer);
    emsg->id = readUint32(&buffer);

    emsg->message_data_size = bufferEnd - buffer;
    if(emsg->message_data_size != 0) {
        emsg->message_data = malloc(emsg->message_data_size);
        memcpy(emsg->message_data, buffer, emsg->message_data_size);
        buffer += emsg->message_data_size;
    }

    return 0;
}

void printEmsg(data_emsg_t* emsg)
{
    printf("\n####### EMSG ######\n");

    printf("scheme_id_uri = %s\n", emsg->scheme_id_uri);
    printf("value = %s\n", emsg->value);
    printf("timescale = %d\n", emsg->timescale);
    printf("presentation_time_delta = %d\n", emsg->presentation_time_delta);
    printf("event_duration = %d\n", emsg->event_duration);
    printf("id = %d\n", emsg->id);

    printf("message_data:\n");
    size_t i = 0;
    for(i = 0; i < emsg->message_data_size; i++) {
        printf("0x%x ", emsg->message_data[i]);
        if(i % 8 == 7) {
            printf("\n");
        }
    }

    if(i % 8 != 7) {
        printf("\n");
    }

    printf("###################\n\n");
}

void printStyp(data_styp_t* styp)
{
    char strTemp[] = {0, 0, 0, 0, 0};

    printf("\n####### STYP ######\n");

    convertUintToString(strTemp, styp->major_brand);
    printf("major_brand = %s\n", strTemp);

    printf("minor_version = %u\n", styp->minor_version);

    printf("num_compatible_brands = %zu\n", styp->num_compatible_brands);
    for(size_t i = 0; i < styp->num_compatible_brands; i++) {
        convertUintToString(strTemp, styp->compatible_brands[i]);
        printf("    %zu: %s\n", i, strTemp);
    }
    printf("###################\n\n");
}

void printSidx(data_sidx_t* sidx)
{
    printf("\n####### SIDX ######\n");

    printf("size = %u\n", sidx->size);
    printf("version = %u\n", sidx->version);
    printf("flags = %u\n", sidx->flags);
    printf("reference_ID = %u\n", sidx->reference_ID);
    printf("timescale = %u\n", sidx->timescale);

    printf("earliest_presentation_time = %"PRId64"\n", sidx->earliest_presentation_time);
    printf("first_offset = %"PRId64"\n", sidx->first_offset);

    printf("reserved = %u\n", sidx->reserved);
    printf("reference_count = %u\n", sidx->reference_count);

    for(size_t i = 0; i < sidx->reference_count; i++) {
        printSidxReference(&(sidx->references[i]));
    }

    printf("###################\n\n");
}

void printSidxReference(data_sidx_reference_t* reference)
{
    printf("    SidxReference:\n");

    printf("        reference_type = %u\n", reference->reference_type);
    printf("        referenced_size = %u\n", reference->referenced_size);
    printf("        subsegment_duration = %u\n", reference->subsegment_duration);
    printf("        starts_with_SAP = %u\n", reference->starts_with_SAP);
    printf("        SAP_type = %u\n", reference->SAP_type);
    printf("        SAP_delta_time = %u\n", reference->SAP_delta_time);
}

void printSsixSubsegment(data_ssix_subsegment_t* subsegment)
{
    printf("    SsixSubsegment:\n");

    printf("        ranges_count = %u\n", subsegment->ranges_count);
    for(size_t i = 0; i < subsegment->ranges_count; i++) {
        printf("            level = %u, range_size = %u\n", subsegment->ranges[i].level,
               subsegment->ranges[i].range_size);
    }
}

void printPcrb(data_pcrb_t* pcrb)
{
    printf("\n####### PCRB ######\n");

    printf("version = %u\n", pcrb->version);
    printf("flags = %u\n", pcrb->flags);
    printf("reference_track_ID = %u\n", pcrb->reference_track_ID);
    printf("ntp_timestamp = %"PRId64"\n", pcrb->ntp_timestamp);
    printf("media_time = %"PRId64"\n", pcrb->media_time);

    printf("###################\n\n");
}

void printSsix(data_ssix_t* ssix)
{
    printf("\n####### SSIX ######\n");

    printf("version = %u\n", ssix->version);
    printf("flags = %u\n", ssix->flags);
    printf("subsegment_count = %u\n", ssix->subsegment_count);

    for(int i = 0; i < ssix->subsegment_count; i++) {
        printSsixSubsegment(&(ssix->subsegments[i]));
    }

    printf("###################\n\n");
}

void convertUintToString(char* str, unsigned int uintStr)
{
    str[0] = (uintStr >> 24) & 0xff;
    str[1] = (uintStr >> 16) & 0xff;
    str[2] = (uintStr >>  8) & 0xff;
    str[3] = (uintStr >>  0) & 0xff;
}

int validateEmsgMsg(unsigned char* buffer, int bufferSize, unsigned int segmentDuration)
{
    LOG_INFO("validateEmsgMsg\n");

    size_t numBoxes;
    box_type_t* box_types;
    void** box_data;
    int* box_sizes;

    int nReturnCode = readBoxes2(buffer, bufferSize, &numBoxes, &box_types, &box_data, &box_sizes);
    if(nReturnCode != 0) {
        LOG_ERROR("ERROR validating EMSG: Error reading boxes\n");
        return -1;
    }

    printBoxes(numBoxes, box_types, box_data);

    for(int i = 0; i < numBoxes; i++) {
        if(box_types[i] != BOX_TYPE_EMSG) {
            LOG_ERROR("ERROR validating EMSG: Invalid box type found\n");
            freeBoxes(numBoxes, box_types, box_data);
            return -1;
        }

        // GORP: anything else to verify here??

        data_emsg_t* emsg = (data_emsg_t*)box_data[i];
        if(emsg->presentation_time_delta + emsg->event_duration > segmentDuration) {
            LOG_ERROR("ERROR validating EMSG: event lasts longer tha segment duration\n");
            freeBoxes(numBoxes, box_types, box_data);
            return -1;
        }
    }

    freeBoxes(numBoxes, box_types, box_data);
    return nReturnCode;
}

void freeIFrames(data_segment_iframes_t* pIFrames, int numSegments)
{
    if(pIFrames == NULL) {
        return;
    }

    for(int i = 0; i < numSegments; i++) {
        free(pIFrames[i].pIFrameLocations_Time);
        free(pIFrames[i].pIFrameLocations_Byte);
        free(pIFrames[i].pStartsWithSAP);
        free(pIFrames[i].pSAPType);
    }
}
