/*
 Copyright (c) 2015-, ISO/IEC JTC1/SC29/WG11
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
#include <check.h>
#include <stdlib.h>

#include "ts.h"
#include "test_common.h"

START_TEST(test_read_ts)
    uint8_t bytes[] = {71, 64, 0, 22, 0, 0, 176, 13, 0, 1, 193, 0, 0, 0, 1, 240, 0, 42, 177, 4, 178, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

    ts_packet_t ts;
    ck_assert(ts_read(&ts, bytes, sizeof(bytes), 5));

    ck_assert(!ts.transport_error_indicator);
    ck_assert(ts.payload_unit_start_indicator);
    ck_assert(!ts.transport_priority);
    ck_assert_uint_eq(ts.pid, 0);
    ck_assert_uint_eq(ts.transport_scrambling_control, 0);
    ck_assert(!ts.has_adaptation_field);
    ck_assert(ts.has_payload);
    ck_assert_uint_eq(ts.continuity_counter, 6);
    assert_bytes_eq(ts.payload, ts.payload_len, bytes + 4, sizeof(bytes) - 4);
    ck_assert_uint_eq(ts.pcr_int, PCR_INVALID);
    ck_assert_uint_eq(ts.pos_in_stream, 5 * TS_SIZE);

    ts_adaptation_field_t* af = &ts.adaptation_field;
    ck_assert_uint_eq(af->length, 0);
    ck_assert(!af->discontinuity_indicator);
    ck_assert(!af->random_access_indicator);
    ck_assert(!af->elementary_stream_priority_indicator);
    ck_assert(!af->pcr_flag);
    ck_assert(!af->opcr_flag);
    ck_assert(!af->splicing_point_flag);
    ck_assert(!af->private_data_flag);
    ck_assert(!af->extension_flag);

    ck_assert_uint_eq(af->program_clock_reference, PCR_INVALID);
    ck_assert_uint_eq(af->original_program_clock_reference, PCR_INVALID);
    ck_assert_uint_eq(af->splice_countdown, 0);
    ck_assert_uint_eq(af->private_data_len, 0);
    ck_assert_uint_eq(af->extension_length, 0);
    ck_assert(!af->ltw_flag);
    ck_assert(!af->piecewise_rate_flag);
    ck_assert(!af->seamless_splice_flag);
    ck_assert(!af->ltw_valid_flag);
    ck_assert_uint_eq(af->ltw_offset, 0);
    ck_assert_uint_eq(af->piecewise_rate, 0);
    ck_assert_uint_eq(af->splice_type, 0);
    ck_assert_uint_eq(af->dts_next_au, 0);
END_TEST

START_TEST(test_read_ts_too_short)
    uint8_t bytes[] = {71, 64, 0, 22, 0, 0, 176, 13, 0, 1, 193, 0, 0, 0, 1, 240, 0, 42, 177, 4, 178, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

    ts_packet_t ts;
    ck_assert(!ts_read(&ts, bytes, sizeof(bytes), 0));
END_TEST

START_TEST(test_read_ts_too_long)
    uint8_t bytes[] = {71, 64, 0, 22, 0, 0, 176, 13, 0, 1, 193, 0, 0, 0, 1, 240, 0, 42, 177, 4, 178, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

    ts_packet_t ts;
    ck_assert(ts_read(&ts, bytes, sizeof(bytes), 0));

    ck_assert(!ts.transport_error_indicator);
    ck_assert(ts.payload_unit_start_indicator);
    ck_assert(!ts.transport_priority);
    ck_assert_uint_eq(ts.pid, PID_PAT);
    ck_assert_uint_eq(ts.transport_scrambling_control, 0);
    ck_assert(!ts.has_adaptation_field);
    ck_assert(ts.has_payload);
    ck_assert_uint_eq(ts.continuity_counter, 6);
    assert_bytes_eq(ts.payload, ts.payload_len, bytes + 4, TS_SIZE - 4);
    ck_assert_uint_eq(ts.pcr_int, PCR_INVALID);
    ck_assert_uint_eq(ts.pos_in_stream, 0);
END_TEST

START_TEST(test_read_ts_with_adaptation_field)
    uint8_t bytes[] = {71, 65, 0, 53, 7, 80, 0, 20, 153, 112, 0, 0, 0, 0, 1, 224, 120, 72, 128, 192, 10, 49, 0, 165,
            148, 161, 17, 0, 165, 101, 193, 0, 0, 0, 1, 9, 240, 0, 0, 0, 1, 103, 77, 64, 31, 236, 160, 80, 23, 252,
            184, 8, 128, 0, 0, 3, 0, 128, 0, 0, 30, 7, 140, 24, 203, 0, 0, 0, 1, 104, 235, 140, 178, 0, 0, 1, 101,
            136, 130, 0, 27, 255, 225, 210, 204, 91, 94, 146, 109, 151, 204, 174, 159, 212, 179, 159, 230, 180, 104,
            192, 121, 73, 106, 224, 3, 239, 249, 84, 165, 75, 31, 48, 113, 249, 121, 167, 102, 187, 240, 81, 39, 140,
            33, 71, 234, 225, 236, 168, 4, 146, 88, 49, 202, 114, 127, 53, 77, 192, 197, 82, 4, 196, 37, 139, 234, 85,
            150, 90, 216, 159, 191, 107, 134, 217, 75, 229, 251, 68, 72, 58, 58, 245, 61, 110, 212, 87, 185, 78, 143,
            129, 36, 110, 165, 126, 181, 27, 16, 153, 23, 142, 144, 163, 127, 35, 200, 73, 96, 225};

    ts_packet_t ts;
    ck_assert(ts_read(&ts, bytes, sizeof(bytes), 6));

    ck_assert(!ts.transport_error_indicator);
    ck_assert(ts.payload_unit_start_indicator);
    ck_assert(!ts.transport_priority);
    ck_assert_uint_eq(ts.pid, 256);
    ck_assert_uint_eq(ts.transport_scrambling_control, 0);
    ck_assert(ts.has_adaptation_field);
    ck_assert(ts.has_payload);
    ck_assert_uint_eq(ts.continuity_counter, 5);
    assert_bytes_eq(ts.payload, ts.payload_len, bytes + 12, ts.payload_len);
    ck_assert_uint_eq(ts.pcr_int, PCR_INVALID);
    ck_assert_uint_eq(ts.pos_in_stream, 1128);

    ts_adaptation_field_t* af = &ts.adaptation_field;
    ck_assert_uint_eq(af->length, 7);
    ck_assert(!af->discontinuity_indicator);
    ck_assert(af->random_access_indicator);
    ck_assert(!af->elementary_stream_priority_indicator);
    ck_assert(af->pcr_flag);
    ck_assert(!af->opcr_flag);
    ck_assert(!af->splicing_point_flag);
    ck_assert(!af->private_data_flag);
    ck_assert(!af->extension_flag);

    ck_assert_uint_eq(af->program_clock_reference, 810000000);
END_TEST

Suite *suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Transport Packets");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_read_ts);
    tcase_add_test(tc_core, test_read_ts_too_short);
    tcase_add_test(tc_core, test_read_ts_too_long);
    tcase_add_test(tc_core, test_read_ts_with_adaptation_field);

    suite_add_tcase(s, tc_core);

    return s;
}