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
#include <glib.h>

#include "pes_demux.h"
#include "test_common.h"


bool pes_arg_called = false;

static void processor(pes_packet_t* pes, elementary_stream_info_t* esi, GPtrArray* ts_packets, void* arg)
{
    pes_packet_t* expected = arg;

    if (expected == NULL) {
        ck_assert_ptr_eq(pes, NULL);
        goto cleanup;
    }
    size_t pes_size = sizeof(*pes) - sizeof(uint8_t*);
    assert_bytes_eq((uint8_t*)pes, pes_size, (uint8_t*)expected, pes_size);
    assert_bytes_eq((uint8_t*)pes->payload, pes->payload_len, (uint8_t*)expected->payload, expected->payload_len);

cleanup:
    pes_arg_called = true;
    pes_free(pes);
}

START_TEST(test_pes_demux_no_empty_pes)
    pes_arg_called = false;
    pes_demux_t* pes_demux = pes_demux_new(processor);
    ck_assert_ptr_ne(pes_demux, NULL);

    pes_demux_process_ts_packet(NULL, NULL, pes_demux);
    ck_assert(!pes_arg_called);

    pes_demux_free(pes_demux);
END_TEST

START_TEST(test_pes_single_ts)
    pes_arg_called = false;
    uint8_t tspacket_bytes[] = {71, 65, 0, 48, 7, 80, 0, 0, 123, 12, 0, 0, 0, 0, 1, 224, 0, 0, 128, 192, 10, 49, 0, 9,
            18, 249, 17, 0, 7, 216, 97, 0, 0, 0, 1, 9, 240, 0, 0, 0, 1, 103, 100, 0, 30, 172, 217, 64, 160, 47, 249,
            112, 17, 0, 0, 3, 0, 1, 0, 0, 3, 0, 48, 15, 22, 45, 150, 0, 0, 0, 1, 104, 235, 236, 178, 44, 0, 0, 1, 6, 5,
            255, 255, 187, 220, 69, 233, 189, 230, 217, 72, 183, 150, 44, 216, 32, 217, 35, 238, 239, 120, 50, 54, 52,
            32, 45, 32, 99, 111, 114, 101, 32, 49, 52, 50, 32, 114, 50, 52, 53, 53, 32, 48, 50, 49, 99, 48, 100, 99,
            32, 45, 32, 72, 46, 50, 54, 52, 47, 77, 80, 69, 71, 45, 52, 32, 65, 86, 67, 32, 99, 111, 100, 101, 99, 32,
            45, 32, 67, 111, 112, 121, 108, 101, 102, 116, 32, 50, 48, 48, 51, 45, 50, 48, 49, 52, 32, 45, 32, 104,
            116, 116, 112, 58, 47, 47, 119, 119, 119};
    uint8_t* pes_bytes = tspacket_bytes + 12;
    size_t pes_len = sizeof(tspacket_bytes) - 12;
    ts_packet_t* ts = ts_read(tspacket_bytes, sizeof(tspacket_bytes), 0);
    ck_assert_ptr_ne(ts, NULL);
    pes_packet_t* pes = pes_read(pes_bytes, pes_len);
    ck_assert_ptr_ne(pes, NULL);

    pes_demux_t* pes_demux = pes_demux_new(processor);
    pes_demux->arg = pes;
    pes_demux->arg_destructor = (pes_arg_destructor_t)pes_free;
    ck_assert_ptr_ne(pes_demux, NULL);

    pes_demux_process_ts_packet(ts, NULL, pes_demux);
    ck_assert(!pes_arg_called);
    pes_demux_process_ts_packet(NULL, NULL, pes_demux);
    ck_assert(pes_arg_called);

    pes_demux_free(pes_demux);
END_TEST

START_TEST(test_pes_multi_ts)
    pes_arg_called = false;
    uint8_t ts_bytes[6][TS_SIZE] = {
        {71, 65, 0, 48, 7, 80, 0, 0, 123, 12, 0, 0, 0, 0, 1, 224, 0, 0, 128, 192, 10, 49, 0, 9,
            18, 249, 17, 0, 7, 216, 97, 0, 0, 0, 1, 9, 240, 0, 0, 0, 1, 103, 100, 0, 30, 172, 217, 64, 160, 47, 249,
            112, 17, 0, 0, 3, 0, 1, 0, 0, 3, 0, 48, 15, 22, 45, 150, 0, 0, 0, 1, 104, 235, 236, 178, 44, 0, 0, 1, 6, 5,
            255, 255, 187, 220, 69, 233, 189, 230, 217, 72, 183, 150, 44, 216, 32, 217, 35, 238, 239, 120, 50, 54, 52,
            32, 45, 32, 99, 111, 114, 101, 32, 49, 52, 50, 32, 114, 50, 52, 53, 53, 32, 48, 50, 49, 99, 48, 100, 99,
            32, 45, 32, 72, 46, 50, 54, 52, 47, 77, 80, 69, 71, 45, 52, 32, 65, 86, 67, 32, 99, 111, 100, 101, 99, 32,
            45, 32, 67, 111, 112, 121, 108, 101, 102, 116, 32, 50, 48, 48, 51, 45, 50, 48, 49, 52, 32, 45, 32, 104,
            116, 116, 112, 58, 47, 47, 119, 119, 119},
        {71, 1, 0, 17, 46, 118, 105, 100, 101, 111, 108, 97, 110, 46, 111, 114, 103, 47, 120,
            50, 54, 52, 46, 104, 116, 109, 108, 32, 45, 32, 111, 112, 116, 105, 111, 110, 115, 58, 32, 99, 97, 98, 97,
            99, 61, 49, 32, 114, 101, 102, 61, 51, 32, 100, 101, 98, 108, 111, 99, 107, 61, 49, 58, 48, 58, 48, 32, 97,
            110, 97, 108, 121, 115, 101, 61, 48, 120, 51, 58, 48, 120, 49, 49, 51, 32, 109, 101, 61, 104, 101, 120, 32,
            115, 117, 98, 109, 101, 61, 55, 32, 112, 115, 121, 61, 49, 32, 112, 115, 121, 95, 114, 100, 61, 49, 46, 48,
            48, 58, 48, 46, 48, 48, 32, 109, 105, 120, 101, 100, 95, 114, 101, 102, 61, 49, 32, 109, 101, 95, 114, 97,
            110, 103, 101, 61, 49, 54, 32, 99, 104, 114, 111, 109, 97, 95, 109, 101, 61, 49, 32, 116, 114, 101, 108,
            108, 105, 115, 61, 49, 32, 56, 120, 56, 100, 99, 116, 61, 49, 32, 99, 113, 109, 61, 48, 32, 100, 101, 97,
            100},
        {71, 1, 0, 18, 122, 111, 110, 101, 61, 50, 49, 44, 49, 49, 32, 102, 97, 115, 116, 95,
            112, 115, 107, 105, 112, 61, 49, 32, 99, 104, 114, 111, 109, 97, 95, 113, 112, 95, 111, 102, 102, 115, 101,
            116, 61, 45, 50, 32, 116, 104, 114, 101, 97, 100, 115, 61, 49, 50, 32, 108, 111, 111, 107, 97, 104, 101,
            97, 100, 95, 116, 104, 114, 101, 97, 100, 115, 61, 50, 32, 115, 108, 105, 99, 101, 100, 95, 116, 104, 114,
            101, 97, 100, 115, 61, 48, 32, 110, 114, 61, 48, 32, 100, 101, 99, 105, 109, 97, 116, 101, 61, 49, 32, 105,
            110, 116, 101, 114, 108, 97, 99, 101, 100, 61, 48, 32, 98, 108, 117, 114, 97, 121, 95, 99, 111, 109, 112,
            97, 116, 61, 48, 32, 99, 111, 110, 115, 116, 114, 97, 105, 110, 101, 100, 95, 105, 110, 116, 114, 97, 61,
            48, 32, 98, 102, 114, 97, 109, 101, 115, 61, 51, 32, 98, 95, 112, 121, 114, 97, 109, 105, 100, 61, 50, 32,
            98, 95, 97, 100, 97},
        {71, 1, 0, 19, 112, 116, 61, 49, 32, 98, 95, 98, 105, 97, 115, 61, 48, 32, 100, 105,
            114, 101, 99, 116, 61, 49, 32, 119, 101, 105, 103, 104, 116, 98, 61, 49, 32, 111, 112, 101, 110, 95, 103,
            111, 112, 61, 48, 32, 119, 101, 105, 103, 104, 116, 112, 61, 50, 32, 107, 101, 121, 105, 110, 116, 61, 50,
            52, 48, 32, 107, 101, 121, 105, 110, 116, 95, 109, 105, 110, 61, 49, 50, 49, 32, 115, 99, 101, 110, 101,
            99, 117, 116, 61, 48, 32, 105, 110, 116, 114, 97, 95, 114, 101, 102, 114, 101, 115, 104, 61, 48, 32, 114,
            99, 95, 108, 111, 111, 107, 97, 104, 101, 97, 100, 61, 52, 48, 32, 114, 99, 61, 97, 98, 114, 32, 109, 98,
            116, 114, 101, 101, 61, 49, 32, 98, 105, 116, 114, 97, 116, 101, 61, 49, 48, 48, 48, 32, 114, 97, 116, 101,
            116, 111, 108, 61, 49, 46, 48, 32, 113, 99, 111, 109, 112, 61, 48, 46, 54, 48, 32, 113, 112, 109, 105, 110,
            61, 48, 32, 113},
        {71, 1, 0, 52, 23, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 112, 109, 97, 120, 61, 54, 57, 32, 113, 112, 115, 116, 101,
            112, 61, 52, 32, 105, 112, 95, 114, 97, 116, 105, 111, 61, 49, 46, 52, 48, 32, 97, 113, 61, 49, 58, 49, 46,
            48, 48, 0, 128, 0, 0, 1, 101, 136, 132, 0, 43, 255, 254, 244, 163, 248, 20, 210, 108, 68, 48, 223, 151, 31,
            243, 200, 163, 68, 40, 201, 100, 128, 204, 41, 144, 0, 0, 3, 0, 0, 3, 0, 0, 27, 63, 193, 254, 32, 88, 134,
            27, 126, 4, 200, 229, 188, 160, 0, 0, 3, 0, 0, 8, 224, 0, 1, 21, 0, 102, 64, 51, 128, 0, 0, 3, 0, 0, 3, 0,
            0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0,
            0, 3, 0, 0, 64, 65},
        {71, 65, 0, 53, 138, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,
            1, 224, 0, 0, 128, 192, 10, 49, 0, 9, 48, 69, 17, 0, 7, 245, 173, 0, 0, 0, 1, 9, 240, 0, 0, 0, 1, 65, 154,
            33, 108, 66, 127, 250, 88, 0, 0, 3, 0, 0, 3, 3, 46}
        };

    uint8_t pespacket_bytes[] = {0, 0, 1, 224, 0, 0, 128, 192, 10, 49, 0, 9, 18, 249, 17, 0, 7, 216, 97, 0, 0, 0, 1, 9,
            240, 0, 0, 0, 1, 103, 100, 0, 30, 172, 217, 64, 160, 47, 249, 112, 17, 0, 0, 3, 0, 1, 0, 0, 3, 0, 48, 15,
            22, 45, 150, 0, 0, 0, 1, 104, 235, 236, 178, 44, 0, 0, 1, 6, 5, 255, 255, 187, 220, 69, 233, 189, 230, 217,
            72, 183, 150, 44, 216, 32, 217, 35, 238, 239, 120, 50, 54, 52, 32, 45, 32, 99, 111, 114, 101, 32, 49, 52,
            50, 32, 114, 50, 52, 53, 53, 32, 48, 50, 49, 99, 48, 100, 99, 32, 45, 32, 72, 46, 50, 54, 52, 47, 77, 80,
            69, 71, 45, 52, 32, 65, 86, 67, 32, 99, 111, 100, 101, 99, 32, 45, 32, 67, 111, 112, 121, 108, 101, 102,
            116, 32, 50, 48, 48, 51, 45, 50, 48, 49, 52, 32, 45, 32, 104, 116, 116, 112, 58, 47, 47, 119, 119, 119, 46,
            118, 105, 100, 101, 111, 108, 97, 110, 46, 111, 114, 103, 47, 120, 50, 54, 52, 46, 104, 116, 109, 108, 32,
            45, 32, 111, 112, 116, 105, 111, 110, 115, 58, 32, 99, 97, 98, 97, 99, 61, 49, 32, 114, 101, 102, 61, 51,
            32, 100, 101, 98, 108, 111, 99, 107, 61, 49, 58, 48, 58, 48, 32, 97, 110, 97, 108, 121, 115, 101, 61, 48,
            120, 51, 58, 48, 120, 49, 49, 51, 32, 109, 101, 61, 104, 101, 120, 32, 115, 117, 98, 109, 101, 61, 55, 32,
            112, 115, 121, 61, 49, 32, 112, 115, 121, 95, 114, 100, 61, 49, 46, 48, 48, 58, 48, 46, 48, 48, 32, 109,
            105, 120, 101, 100, 95, 114, 101, 102, 61, 49, 32, 109, 101, 95, 114, 97, 110, 103, 101, 61, 49, 54, 32,
            99, 104, 114, 111, 109, 97, 95, 109, 101, 61, 49, 32, 116, 114, 101, 108, 108, 105, 115, 61, 49, 32, 56,
            120, 56, 100, 99, 116, 61, 49, 32, 99, 113, 109, 61, 48, 32, 100, 101, 97, 100, 122, 111, 110, 101, 61,
            50, 49, 44, 49, 49, 32, 102, 97, 115, 116, 95, 112, 115, 107, 105, 112, 61, 49, 32, 99, 104, 114, 111, 109,
            97, 95, 113, 112, 95, 111, 102, 102, 115, 101, 116, 61, 45, 50, 32, 116, 104, 114, 101, 97, 100, 115, 61,
            49, 50, 32, 108, 111, 111, 107, 97, 104, 101, 97, 100, 95, 116, 104, 114, 101, 97, 100, 115, 61, 50, 32,
            115, 108, 105, 99, 101, 100, 95, 116, 104, 114, 101, 97, 100, 115, 61, 48, 32, 110, 114, 61, 48, 32, 100,
            101, 99, 105, 109, 97, 116, 101, 61, 49, 32, 105, 110, 116, 101, 114, 108, 97, 99, 101, 100, 61, 48, 32,
            98, 108, 117, 114, 97, 121, 95, 99, 111, 109, 112, 97, 116, 61, 48, 32, 99, 111, 110, 115, 116, 114, 97,
            105, 110, 101, 100, 95, 105, 110, 116, 114, 97, 61, 48, 32, 98, 102, 114, 97, 109, 101, 115, 61, 51, 32,
            98, 95, 112, 121, 114, 97, 109, 105, 100, 61, 50, 32, 98, 95, 97, 100, 97, 112, 116, 61, 49, 32, 98, 95,
            98, 105, 97, 115, 61, 48, 32, 100, 105, 114, 101, 99, 116, 61, 49, 32, 119, 101, 105, 103, 104, 116, 98,
            61, 49, 32, 111, 112, 101, 110, 95, 103, 111, 112, 61, 48, 32, 119, 101, 105, 103, 104, 116, 112, 61, 50,
            32, 107, 101, 121, 105, 110, 116, 61, 50, 52, 48, 32, 107, 101, 121, 105, 110, 116, 95, 109, 105, 110, 61,
            49, 50, 49, 32, 115, 99, 101, 110, 101, 99, 117, 116, 61, 48, 32, 105, 110, 116, 114, 97, 95, 114, 101,
            102, 114, 101, 115, 104, 61, 48, 32, 114, 99, 95, 108, 111, 111, 107, 97, 104, 101, 97, 100, 61, 52, 48,
            32, 114, 99, 61, 97, 98, 114, 32, 109, 98, 116, 114, 101, 101, 61, 49, 32, 98, 105, 116, 114, 97, 116, 101,
            61, 49, 48, 48, 48, 32, 114, 97, 116, 101, 116, 111, 108, 61, 49, 46, 48, 32, 113, 99, 111, 109, 112, 61,
            48, 46, 54, 48, 32, 113, 112, 109, 105, 110, 61, 48, 32, 113, 112, 109, 97, 120, 61, 54, 57, 32, 113, 112,
            115, 116, 101, 112, 61, 52, 32, 105, 112, 95, 114, 97, 116, 105, 111, 61, 49, 46, 52, 48, 32, 97, 113, 61,
            49, 58, 49, 46, 48, 48, 0, 128, 0, 0, 1, 101, 136, 132, 0, 43, 255, 254, 244, 163, 248, 20, 210, 108, 68,
            48, 223, 151, 31, 243, 200, 163, 68, 40, 201, 100, 128, 204, 41, 144, 0, 0, 3, 0, 0, 3, 0, 0, 27, 63, 193,
            254, 32, 88, 134, 27, 126, 4, 200, 229, 188, 160, 0, 0, 3, 0, 0, 8, 224, 0, 1, 21, 0, 102, 64, 51, 128, 0,
            0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0,
            0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 64, 65};

    pes_packet_t* pes = pes_read(pespacket_bytes, sizeof(pespacket_bytes));
    ck_assert_ptr_ne(pes, NULL);

    pes_demux_t* pes_demux = pes_demux_new(processor);
    pes_demux->arg = pes;
    pes_demux->arg_destructor = (pes_arg_destructor_t)pes_free;
    ck_assert_ptr_ne(pes_demux, NULL);

    for (size_t i = 0; i < 6; ++i) {
        ts_packet_t* ts = ts_read(ts_bytes[i], TS_SIZE, i);
        pes_demux_process_ts_packet(ts, NULL, pes_demux);
        ck_assert(pes_arg_called == (i == 5));
    }

    pes_free(pes);
    pes = pes_read(ts_bytes[5] + 143, TS_SIZE - 143);
    pes->payload_pos_in_stream = 940;
    pes_demux->arg = pes;

    pes_demux_process_ts_packet(NULL, NULL, pes_demux);
    ck_assert(pes_arg_called);

    pes_demux_free(pes_demux);
END_TEST

START_TEST(test_bad_pes)
    pes_arg_called = false;
    uint8_t tspacket_bytes[] = {71, 65, 0, 48, 7, 80, 0, 0, 123, 12, 0, 0, 0, 0, 42, 224, 0, 0, 128, 192, 10, 49, 0, 9,
            18, 249, 17, 0, 7, 216, 97, 0, 0, 0, 1, 9, 240, 0, 0, 0, 1, 103, 100, 0, 30, 172, 217, 64, 160, 47, 249,
            112, 17, 0, 0, 3, 0, 1, 0, 0, 3, 0, 48, 15, 22, 45, 150, 0, 0, 0, 1, 104, 235, 236, 178, 44, 0, 0, 1, 6, 5,
            255, 255, 187, 220, 69, 233, 189, 230, 217, 72, 183, 150, 44, 216, 32, 217, 35, 238, 239, 120, 50, 54, 52,
            32, 45, 32, 99, 111, 114, 101, 32, 49, 52, 50, 32, 114, 50, 52, 53, 53, 32, 48, 50, 49, 99, 48, 100, 99,
            32, 45, 32, 72, 46, 50, 54, 52, 47, 77, 80, 69, 71, 45, 52, 32, 65, 86, 67, 32, 99, 111, 100, 101, 99, 32,
            45, 32, 67, 111, 112, 121, 108, 101, 102, 116, 32, 50, 48, 48, 51, 45, 50, 48, 49, 52, 32, 45, 32, 104,
            116, 116, 112, 58, 47, 47, 119, 119, 119};
    ts_packet_t* ts = ts_read(tspacket_bytes, sizeof(tspacket_bytes), 0);
    ck_assert_ptr_ne(ts, NULL);

    pes_demux_t* pes_demux = pes_demux_new(processor);
    ck_assert_ptr_ne(pes_demux, NULL);

    pes_demux_process_ts_packet(ts, NULL, pes_demux);
    ck_assert(!pes_arg_called);
    pes_demux_process_ts_packet(NULL, NULL, pes_demux);
    ck_assert(pes_arg_called);

    pes_demux_free(pes_demux);
END_TEST

Suite *suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("PES Packet Demuxer");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_pes_demux_no_empty_pes);
    tcase_add_test(tc_core, test_pes_single_ts);
    tcase_add_test(tc_core, test_pes_multi_ts);
    tcase_add_test(tc_core, test_bad_pes);

    suite_add_tcase(s, tc_core);

    return s;
}