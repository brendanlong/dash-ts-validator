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


// styp, sidx, pcrb, ssix

typedef struct
{
    unsigned int size;
    unsigned int major_brand;
    unsigned int minor_version;
    unsigned int num_compatible_brands;
    unsigned int *compatible_brands;

} data_styp_t; 

typedef struct
{
    unsigned char reference_type; 
    unsigned int referenced_size; 
    unsigned int subsegment_duration; 
    unsigned char starts_with_SAP; 
    unsigned char SAP_type; 
    unsigned int SAP_delta_time; 

} data_sidx_reference_t;

typedef struct
{
    unsigned int size;
    unsigned char version;
    unsigned int flags;
    unsigned int reference_ID; 
    unsigned int timescale; 
    uint64_t earliest_presentation_time; 
    uint64_t first_offset; 

    unsigned short reserved;
    unsigned short reference_count; 
    data_sidx_reference_t *references;

} data_sidx_t; 

typedef struct
{
    unsigned char level;
    unsigned int range_size;

} data_ssix_subsegment_range_t; 

typedef struct
{
    unsigned int ranges_count;
    data_ssix_subsegment_range_t* ranges;

} data_ssix_subsegment_t; 

typedef struct
{
    unsigned int size;
    unsigned char version;
    unsigned int flags;
    unsigned int subsegment_count;

    data_ssix_subsegment_t* subsegments;

} data_ssix_t; 

typedef struct
{
    unsigned int size;
    unsigned char version;
    unsigned int flags;
    unsigned int reference_track_ID;
    uint64_t ntp_timestamp;
    uint64_t media_time;

} data_pcrb_t; 

typedef struct
{
    unsigned int size;
    unsigned char version;
    unsigned int flags;
    char* scheme_id_uri; 
    char* value; 
    unsigned int timescale; 
    unsigned int presentation_time_delta; 
    unsigned int event_duration; 
    unsigned int id; 
    unsigned char *message_data; 
    int message_data_sz;

} data_emsg_t;

typedef struct
{
    unsigned int doIFrameValidation;  // 0 = FALSE, 1 = TRUE

    int numIFrames;
    unsigned int *pIFrameLocations_Time;
    uint64_t *pIFrameLocations_Byte;
    unsigned char *pStartsWithSAP; 
    unsigned char *pSAPType;

} data_segment_iframes_t;

typedef enum
{
    BOX_STYP = 0,
    BOX_SIDX,
    BOX_PCRB,
    BOX_SSIX,
    BOX_EMSG
} box_type_t;


int parseStyp (unsigned char *buffer, int bufferSz, data_styp_t *styp);
int parseSidx (unsigned char *buffer, int bufferSz, data_sidx_t *sidx);
int parsePcrb (unsigned char *buffer, int bufferSz, data_pcrb_t *pcrb);
int parseSsix (unsigned char *buffer, int bufferSz, data_ssix_t *ssix);
int parseEmsg (unsigned char *buffer, int bufferSz, data_emsg_t *emsg);

void printStyp (data_styp_t *styp);
void printSidx (data_sidx_t *sidx);
void printPcrb (data_pcrb_t *pcrb);
void printSsix (data_ssix_t *ssix);
void printEmsg (data_emsg_t *emsg);

void freeStyp (data_styp_t *styp);
void freeSidx (data_sidx_t *sidx);
void freePcrb (data_pcrb_t *pcrb);
void freeSsix (data_ssix_t *ssix);
void freeEmsg (data_emsg_t *emsg);

void printSidxReference(data_sidx_reference_t *reference);
void printSsixSubsegment(data_ssix_subsegment_t *subsegment);

void convertUintToString(char *str, unsigned int uintStr);

/* don't name this "ntohll" or it will cause build failures on platforms where
 * ntohll is a standard function */
uint64_t isobmff_ntohll (uint64_t num);

int getNumBoxes(unsigned char *buffer, int bufferSz, int *pNumBoxes);
void printBoxes(int numBoxes, box_type_t *box_types, void ** box_data);
void freeBoxes(int numBoxes, box_type_t *box_types, void ** box_data);
int readBoxes(char *fname, int *pNumBoxes, box_type_t **box_types_in, void *** box_data_in, int **box_sizes_in);
int readBoxes2(unsigned char *buffer, int buuferSz, int *pNumBoxes, box_type_t **box_types_in, void *** box_data_in, int **box_sizes_in);

int validateIndexSegment(char *fname, int numSegments, int *segmentDurations, data_segment_iframes_t *pIFrames, 
                         int presentationTimeOffset, int videoPID, unsigned char isSimpleProfile);
int validateRepresentationIndexSegmentBoxes(int numSegments, int numBoxes, box_type_t *box_types, void ** box_data, 
    int *box_sizes, int *segmentDurations, data_segment_iframes_t *pIFrames, int presentationTimeOffset, int videoPID,
    unsigned char isSimpleProfile);
int validateSingleIndexSegmentBoxes(int numBoxes, box_type_t *box_types, void ** box_data, int *box_sizes, int segmentDuration,
    data_segment_iframes_t *pIFrames, int presentationTimeOffset, int videoPID, unsigned char isSimpleProfile);

int validateEmsgMsg(unsigned char *buffer, int bufferSz, unsigned int segmentDuration);

int analyzeSidxReferences (data_sidx_t * sidx, int *pNumIFrames, int *pNumNestedSidx, unsigned char isSimpleProfile);

void freeIFrames (data_segment_iframes_t *pIFrames, int numSegments);


#endif  // __H_ISOBMFF_CONFORMANCE
