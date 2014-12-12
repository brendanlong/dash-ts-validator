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

#ifndef __H_ISOBMFF_CONFORMANCE
#define __H_ISOBMFF_CONFORMANCE

#include <stdlib.h>
#include <stdint.h>


typedef enum {
    BRAND_RISX = 0x72697378,
    BRAND_SISX = 0x73697378,
    BRAND_SSSS = 0x73737373
} brand_t;

// styp, sidx, pcrb, ssix

typedef struct {
    uint32_t size;
    uint32_t major_brand;
    uint32_t minor_version;
    size_t num_compatible_brands;
    uint32_t* compatible_brands;
} data_styp_t;

typedef struct {
    uint8_t reference_type;
    uint32_t referenced_size;
    uint32_t subsegment_duration;
    uint8_t starts_with_SAP;
    uint8_t SAP_type;
    uint32_t SAP_delta_time;

} data_sidx_reference_t;

typedef struct {
    uint32_t size;
    uint8_t version;
    uint32_t flags;
    uint32_t reference_ID;
    uint32_t timescale;
    uint64_t earliest_presentation_time;
    uint64_t first_offset;

    uint16_t reserved;
    uint16_t reference_count;
    data_sidx_reference_t* references;

} data_sidx_t;

typedef struct {
    uint8_t level;
    uint32_t range_size;

} data_ssix_subsegment_range_t;

typedef struct {
    uint32_t ranges_count;
    data_ssix_subsegment_range_t* ranges;

} data_ssix_subsegment_t;

typedef struct {
    uint32_t size;
    uint8_t version;
    uint32_t flags;
    uint32_t subsegment_count;
    data_ssix_subsegment_t* subsegments;

} data_ssix_t;

typedef struct {
    uint32_t size;
    uint8_t version;
    uint32_t flags;
    uint32_t reference_track_ID;
    uint64_t ntp_timestamp;
    uint64_t media_time;

} data_pcrb_t;

typedef struct {
    uint32_t size;
    uint8_t version;
    uint32_t flags;
    char* scheme_id_uri;
    char* value;
    uint32_t timescale;
    uint32_t presentation_time_delta;
    uint32_t event_duration;
    uint32_t id;
    uint8_t* message_data;
    size_t message_data_size;

} data_emsg_t;

typedef struct {
    unsigned int doIFrameValidation;  // 0 = FALSE, 1 = TRUE

    int numIFrames;
    unsigned int* pIFrameLocations_Time;
    uint64_t* pIFrameLocations_Byte;
    unsigned char* pStartsWithSAP;
    unsigned char* pSAPType;

} data_segment_iframes_t;

typedef enum {
    BOX_TYPE_EMSG = 0x656d7367,
    BOX_TYPE_PCRB = 0x70637262,
    BOX_TYPE_SIDX = 0x73696478,
    BOX_TYPE_SSIX = 0x73736978,
    BOX_TYPE_STYP = 0x73747970
} box_type_t;


int parseStyp(unsigned char* buffer, int bufferSize, data_styp_t* styp);
int parseSidx(unsigned char* buffer, int bufferSize, data_sidx_t* sidx);
int parsePcrb(unsigned char* buffer, int bufferSize, data_pcrb_t* pcrb);
int parseSsix(unsigned char* buffer, int bufferSize, data_ssix_t* ssix);
int parseEmsg(unsigned char* buffer, int bufferSize, data_emsg_t* emsg);

void printStyp(data_styp_t* styp);
void printSidx(data_sidx_t* sidx);
void printPcrb(data_pcrb_t* pcrb);
void printSsix(data_ssix_t* ssix);
void printEmsg(data_emsg_t* emsg);

void freeStyp(data_styp_t* styp);
void freeSidx(data_sidx_t* sidx);
void freePcrb(data_pcrb_t* pcrb);
void freeSsix(data_ssix_t* ssix);
void freeEmsg(data_emsg_t* emsg);

void printSidxReference(data_sidx_reference_t* reference);
void printSsixSubsegment(data_ssix_subsegment_t* subsegment);

void convertUintToString(char* str, unsigned int uintStr);

int getNumBoxes(unsigned char* buffer, int bufferSize, size_t* numBoxes);
void printBoxes(size_t numBoxes, box_type_t* box_types, void** box_data);
void freeBoxes(size_t numBoxes, box_type_t* box_types, void** box_data);
int readBoxes(char* fname, size_t* numBoxes, box_type_t** box_types_in, void** * box_data_in,
              int** box_sizes_in);
int readBoxes2(unsigned char* buffer, int bufferSize, size_t* numBoxes, box_type_t** box_types_in,
               void** * box_data_in, int** box_sizes_in);

int validateIndexSegment(char* fname, size_t numSegments, int* segmentDurations,
                         data_segment_iframes_t* pIFrames,
                         int presentationTimeOffset, int videoPID, unsigned char isSimpleProfile);
int validateRepresentationIndexSegmentBoxes(size_t numSegments, size_t numBoxes, box_type_t* box_types,
        void** box_data,
        int* box_sizes, int* segmentDurations, data_segment_iframes_t* pIFrames, int presentationTimeOffset,
        int videoPID,
        unsigned char isSimpleProfile);
int validateSingleIndexSegmentBoxes(int numBoxes, box_type_t* box_types, void** box_data,
                                    int* box_sizes, int segmentDuration,
                                    data_segment_iframes_t* pIFrames, int presentationTimeOffset, int videoPID,
                                    unsigned char isSimpleProfile);

int validateEmsgMsg(unsigned char* buffer, int bufferSize, unsigned int segmentDuration);

int analyzeSidxReferences(data_sidx_t* sidx, int* pNumIFrames, int* pNumNestedSidx,
                          unsigned char isSimpleProfile);

void freeIFrames(data_segment_iframes_t* pIFrames, int numSegments);


#endif  // __H_ISOBMFF_CONFORMANCE
