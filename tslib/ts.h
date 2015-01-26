/*
 Copyright (c) 2012-, ISO/IEC JTC1/SC29/WG11
 Written by Alex Giladi <alex.giladi@gmail.com> and Vlad Zbarsky <zbarsky@cornell.edu>
 All rights reserved.

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

#ifndef TSLIB_TS_H
#define TSLIB_TS_H

#include <stdbool.h>
#include <stdint.h>

#include "bs.h"
#include "libts_common.h"


#define TS_SIZE                 188
#define TS_HEADER_SIZE            4
#define TS_SYNC_BYTE           0x47

#define TS_PAYLOAD             0x01
#define TS_ADAPTATION_FIELD    0x02

#define PCR_MAX          (1LL << 42)
#define PCR_INVALID       INT64_MAX
#define PCR_IS_VALID(P)  ( ( (P) >= 0 ) && ((P) <  PCR_MAX))


/* 2.4.3.2 Transport Stream packet layer */
typedef struct {
    bool transport_error_indicator;
    bool payload_unit_start_indicator;
    bool transport_priority;
    uint16_t pid;

    uint8_t transport_scrambling_control;
    uint8_t adaptation_field_control;
    uint8_t continuity_counter;
} ts_header_t;

/* 2.4.3.4 Adaptation field */
typedef struct {
    uint8_t adaptation_field_length;

    bool discontinuity_indicator;
    bool random_access_indicator;
    bool elementary_stream_priority_indicator;
    bool pcr_flag;
    bool opcr_flag;
    bool splicing_point_flag;
    bool transport_private_data_flag;
    bool adaptation_field_extension_flag;

    /* if pcr_flag == 1 */
    uint64_t program_clock_reference_base;
    uint16_t program_clock_reference_extension;

    /* if opcr_flag == 1 */
    uint64_t original_program_clock_reference_base;
    uint16_t original_program_clock_reference_extension;

    /* if splicing_point_flag == 1 */
    uint8_t splice_countdown;

    /* if transport_private_data_flag == 1 */
    uint8_t transport_private_data_length;
    buf_t private_data_bytes;

    /* if adaptation_field_extension_flag == 1 */
    uint8_t adaptation_field_extension_length;
    bool ltw_flag;
    bool piecewise_rate_flag;
    bool seamless_splice_flag;

    /* if ltw_flag == 1 */
    bool ltw_valid_flag;
    uint16_t ltw_offset;

    /* if piecewise_rate_flag == 1 */
    uint32_t piecewise_rate;

    /* if seamless_splice_flag == 1 */
    uint8_t splice_type;
    uint64_t dts_next_au;
} ts_adaptation_field_t;

/* 2.4.3.2 Transport Stream packet layer */
typedef struct {
    ts_header_t header;
    ts_adaptation_field_t adaptation_field;
    buf_t payload;      /// start of the payload
    uint64_t pcr_int;   /// interpolated PCR
    uint64_t pos_in_stream;  // byte location of payload in transport stream
} ts_packet_t;

typedef enum {
    TS_ERROR_UNKNOWN         =  0,
    TS_ERROR_NOT_ENOUGH_DATA = -1,
    TS_ERROR_NO_SYNC_BYTE    = -2,
} ts_error_t;


ts_packet_t* ts_new();
void ts_free(ts_packet_t* ts);
int ts_read(ts_packet_t* ts, uint8_t* buf, size_t buf_size, uint64_t packet_num);
void ts_print(const ts_packet_t* const ts);

int64_t ts_read_pcr(const ts_packet_t* const ts);

#endif