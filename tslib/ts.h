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
#include <stddef.h>
#include <stdint.h>


#define TS_SIZE                 188
#define TS_HEADER_SIZE            4
#define TS_SYNC_BYTE           0x47

#define TS_PAYLOAD             0x01
#define TS_ADAPTATION_FIELD    0x02

#define PCR_MAX          (1LL << 42)
#define PCR_INVALID       UINT64_MAX
#define PCR_IS_VALID(P)  ( ( (P) >= 0 ) && ((P) <  PCR_MAX))

/* 2.4.3.4 Adaptation field */
typedef struct {
    uint8_t length;

    bool discontinuity_indicator;
    bool random_access_indicator;
    bool elementary_stream_priority_indicator;
    bool pcr_flag;
    bool opcr_flag;
    bool splicing_point_flag;
    bool private_data_flag;
    bool extension_flag;

    /* if pcr_flag == 1 */
    uint64_t program_clock_reference_base;
    uint16_t program_clock_reference_extension;

    /* if opcr_flag == 1 */
    uint64_t original_program_clock_reference_base;
    uint16_t original_program_clock_reference_extension;

    /* if splicing_point_flag == 1 */
    uint8_t splice_countdown;

    /* if transport_private_data_flag == 1 */
    uint8_t private_data[TS_SIZE];
    size_t private_data_len;

    /* if adaptation_field_extension_flag == 1 */
    uint8_t extension_length;
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
    bool transport_error_indicator;
    bool payload_unit_start_indicator;
    bool transport_priority;
    uint16_t pid;

    uint8_t transport_scrambling_control;
    bool has_adaptation_field;
    bool has_payload;
    uint8_t continuity_counter;

    ts_adaptation_field_t adaptation_field;

    uint8_t payload[TS_SIZE];
    size_t payload_len;
    uint64_t pcr_int;   /// interpolated PCR
    uint64_t pos_in_stream;  // byte location of payload in transport stream
} ts_packet_t;

enum {
    PID_PAT = 0,
    PID_CAT = 1,
    PID_TSDT = 2,
    PID_IPMP_CIT = 3,
    PID_DASH_EMSG = 4,
    PID_NULL = 0x1FFF
} ts_pid_t;

void ts_copy(ts_packet_t*, const ts_packet_t*);
bool ts_read(ts_packet_t*, uint8_t* buf, size_t buf_size, uint64_t packet_num);
void ts_print(const ts_packet_t* ts);

int64_t ts_read_pcr(const ts_packet_t* ts);

#endif