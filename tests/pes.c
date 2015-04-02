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

#include "pes.h"
#include "test_common.h"

#include "pes_data.c"

START_TEST(test_read_pes)
    uint8_t bytes[] = {
      0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x80, 0xc0, 0x0a, 0x31, 0x00, 0x09,
      0x12, 0xf9, 0x11, 0x00, 0x07, 0xd8, 0x61, 0x00, 0x00, 0x00, 0x01, 0x09,
      0xf0, 0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x1e, 0xac, 0xd9, 0x40,
      0xa0, 0x2f, 0xf9, 0x70, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00,
      0x03, 0x00, 0x30, 0x0f, 0x16, 0x2d, 0x96, 0x00, 0x00, 0x00, 0x01, 0x68,
      0xeb, 0xec, 0xb2, 0x2c, 0x00, 0x00, 0x01, 0x06, 0x05, 0xff, 0xff, 0xbb,
      0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7, 0x96, 0x2c, 0xd8, 0x20,
      0xd9, 0x23, 0xee, 0xef, 0x78, 0x32, 0x36, 0x34, 0x20, 0x2d, 0x20, 0x63,
      0x6f, 0x72, 0x65, 0x20, 0x31, 0x34, 0x32, 0x20, 0x72, 0x32, 0x34, 0x35,
      0x35, 0x20, 0x30, 0x32, 0x31, 0x63, 0x30, 0x64, 0x63, 0x20, 0x2d, 0x20,
      0x48, 0x2e, 0x32, 0x36, 0x34, 0x2f, 0x4d, 0x50, 0x45, 0x47, 0x2d, 0x34,
      0x20, 0x41, 0x56, 0x43, 0x20, 0x63, 0x6f, 0x64, 0x65, 0x63, 0x20, 0x2d,
      0x20, 0x43, 0x6f, 0x70, 0x79, 0x6c, 0x65, 0x66, 0x74, 0x20, 0x32, 0x30,
      0x30, 0x33, 0x2d, 0x32, 0x30, 0x31, 0x34, 0x20, 0x2d, 0x20, 0x68, 0x74,
      0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77
    };

    pes_packet_t* pes = pes_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(pes, NULL);
    ck_assert_uint_eq(pes->packet_length, 0);
    ck_assert_uint_eq(pes->stream_id, 224);
    ck_assert_uint_eq(pes->scrambling_control, 0);
    ck_assert(!pes->priority);
    ck_assert(!pes->data_alignment_indicator);
    ck_assert(!pes->copyright);
    ck_assert(!pes->original_or_copy);
    ck_assert(pes->pts_flag);
    ck_assert(pes->dts_flag);
    ck_assert(!pes->escr_flag);
    ck_assert(!pes->es_rate_flag);
    ck_assert(!pes->dsm_trick_mode_flag);
    ck_assert(!pes->additional_copy_info_flag);
    ck_assert(!pes->crc_flag);
    ck_assert(!pes->extension_flag);
    ck_assert_uint_eq(pes->pts, 133500);
    ck_assert_uint_eq(pes->dts, 126000);
    ck_assert_uint_eq(pes->escr_base, 0);
    ck_assert_uint_eq(pes->escr_extension, 0);
    ck_assert_uint_eq(pes->es_rate, 0);
    ck_assert_uint_eq(pes->trick_mode_control, 0);
    ck_assert_uint_eq(pes->field_id, 0);
    ck_assert(!pes->intra_slice_refresh);
    ck_assert_uint_eq(pes->frequency_truncation, 0);
    ck_assert_uint_eq(pes->rep_cntrl, 0);
    ck_assert_uint_eq(pes->additional_copy_info, 0);
    ck_assert_uint_eq(pes->previous_pes_packet_crc, 0);
    ck_assert(!pes->private_data_flag);
    ck_assert(!pes->pack_header_field_flag);
    ck_assert(!pes->program_packet_sequence_counter_flag);
    ck_assert(!pes->pstd_buffer_flag);
    ck_assert(!pes->extension_flag_2);
    uint8_t zero_bytes[16] = {0};
    assert_bytes_eq(pes->private_data, 16, zero_bytes, 16);
    ck_assert_uint_eq(pes->pack_field_length, 0);
    ck_assert_uint_eq(pes->program_packet_sequence_counter, 0);
    ck_assert(!pes->mpeg1_mpeg2_identifier);
    ck_assert_uint_eq(pes->original_stuff_length, 0);
    ck_assert(!pes->pstd_buffer_scale);
    ck_assert_uint_eq(pes->pstd_buffer_size, 0);
    ck_assert_uint_eq(pes->extension_field_length, 0);
    ck_assert(!pes->stream_id_extension_flag);
    ck_assert_uint_eq(pes->stream_id_extension, 0);
    ck_assert(!pes->tref_extension_flag);
    ck_assert_uint_eq(pes->tref, 0);

    ck_assert_ptr_ne(pes->payload, NULL);
    ck_assert_uint_eq(pes->payload_len, 157);

    pes_free(pes);
END_TEST

START_TEST(test_read_pes_too_short)
    uint8_t bytes[] = {
      0x00, 0x00, 0x01, 0xe0, 0x00, 173, 0x80, 0xc0, 0x0a, 0x31, 0x00, 0x09,
      0x12, 0xf9, 0x11, 0x00, 0x07, 0xd8, 0x61, 0x00, 0x00, 0x00, 0x01, 0x09,
      0xf0, 0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x1e, 0xac, 0xd9, 0x40,
      0xa0, 0x2f, 0xf9, 0x70, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00,
      0x03, 0x00, 0x30, 0x0f, 0x16, 0x2d, 0x96, 0x00, 0x00, 0x00, 0x01, 0x68,
      0xeb, 0xec, 0xb2, 0x2c, 0x00, 0x00, 0x01, 0x06, 0x05, 0xff, 0xff, 0xbb,
      0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7, 0x96, 0x2c, 0xd8, 0x20,
      0xd9, 0x23, 0xee, 0xef, 0x78, 0x32, 0x36, 0x34, 0x20, 0x2d, 0x20, 0x63,
      0x6f, 0x72, 0x65, 0x20, 0x31, 0x34, 0x32, 0x20, 0x72, 0x32, 0x34, 0x35,
      0x35, 0x20, 0x30, 0x32, 0x31, 0x63, 0x30, 0x64, 0x63, 0x20, 0x2d, 0x20,
      0x48, 0x2e, 0x32, 0x36, 0x34, 0x2f, 0x4d, 0x50, 0x45, 0x47, 0x2d, 0x34,
      0x20, 0x41, 0x56, 0x43, 0x20, 0x63, 0x6f, 0x64, 0x65, 0x63, 0x20, 0x2d,
      0x20, 0x43, 0x6f, 0x70, 0x79, 0x6c, 0x65, 0x66, 0x74, 0x20, 0x32, 0x30,
      0x30, 0x33, 0x2d, 0x32, 0x30, 0x31, 0x34, 0x20, 0x2d, 0x20, 0x68, 0x74,
      0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x77, 0x77,
    };

    pes_packet_t* pes = pes_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(pes, NULL);
END_TEST

START_TEST(test_read_pes_too_long)
    uint8_t bytes[] = {
      0x00, 0x00, 0x01, 0xe0, 0x00, 173, 0x80, 0xc0, 0x0a, 0x31, 0x00, 0x09,
      0x12, 0xf9, 0x11, 0x00, 0x07, 0xd8, 0x61, 0x00, 0x00, 0x00, 0x01, 0x09,
      0xf0, 0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x1e, 0xac, 0xd9, 0x40,
      0xa0, 0x2f, 0xf9, 0x70, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00,
      0x03, 0x00, 0x30, 0x0f, 0x16, 0x2d, 0x96, 0x00, 0x00, 0x00, 0x01, 0x68,
      0xeb, 0xec, 0xb2, 0x2c, 0x00, 0x00, 0x01, 0x06, 0x05, 0xff, 0xff, 0xbb,
      0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7, 0x96, 0x2c, 0xd8, 0x20,
      0xd9, 0x23, 0xee, 0xef, 0x78, 0x32, 0x36, 0x34, 0x20, 0x2d, 0x20, 0x63,
      0x6f, 0x72, 0x65, 0x20, 0x31, 0x34, 0x32, 0x20, 0x72, 0x32, 0x34, 0x35,
      0x35, 0x20, 0x30, 0x32, 0x31, 0x63, 0x30, 0x64, 0x63, 0x20, 0x2d, 0x20,
      0x48, 0x2e, 0x32, 0x36, 0x34, 0x2f, 0x4d, 0x50, 0x45, 0x47, 0x2d, 0x34,
      0x20, 0x41, 0x56, 0x43, 0x20, 0x63, 0x6f, 0x64, 0x65, 0x63, 0x20, 0x2d,
      0x20, 0x43, 0x6f, 0x70, 0x79, 0x6c, 0x65, 0x66, 0x74, 0x20, 0x32, 0x30,
      0x30, 0x33, 0x2d, 0x32, 0x30, 0x31, 0x34, 0x20, 0x2d, 0x20, 0x68, 0x74,
      0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0xFF, 0xFF
    };

    pes_packet_t* pes = pes_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(pes, NULL);
    ck_assert_uint_eq(pes->packet_length, 173);
    ck_assert_uint_eq(pes->stream_id, 224);
    ck_assert_uint_eq(pes->scrambling_control, 0);
    ck_assert(!pes->priority);
    ck_assert(!pes->data_alignment_indicator);
    ck_assert(!pes->copyright);
    ck_assert(!pes->original_or_copy);
    ck_assert(pes->pts_flag);
    ck_assert(pes->dts_flag);
    ck_assert(!pes->escr_flag);
    ck_assert(!pes->es_rate_flag);
    ck_assert(!pes->dsm_trick_mode_flag);
    ck_assert(!pes->additional_copy_info_flag);
    ck_assert(!pes->crc_flag);
    ck_assert(!pes->extension_flag);
    ck_assert_uint_eq(pes->pts, 133500);
    ck_assert_uint_eq(pes->dts, 126000);
    ck_assert_uint_eq(pes->escr_base, 0);
    ck_assert_uint_eq(pes->escr_extension, 0);
    ck_assert_uint_eq(pes->es_rate, 0);
    ck_assert_uint_eq(pes->trick_mode_control, 0);
    ck_assert_uint_eq(pes->field_id, 0);
    ck_assert(!pes->intra_slice_refresh);
    ck_assert_uint_eq(pes->frequency_truncation, 0);
    ck_assert_uint_eq(pes->rep_cntrl, 0);
    ck_assert_uint_eq(pes->additional_copy_info, 0);
    ck_assert_uint_eq(pes->previous_pes_packet_crc, 0);
    ck_assert(!pes->private_data_flag);
    ck_assert(!pes->pack_header_field_flag);
    ck_assert(!pes->program_packet_sequence_counter_flag);
    ck_assert(!pes->pstd_buffer_flag);
    ck_assert(!pes->extension_flag_2);
    uint8_t zero_bytes[16] = {0};
    assert_bytes_eq(pes->private_data, 16, zero_bytes, 16);
    ck_assert_uint_eq(pes->pack_field_length, 0);
    ck_assert_uint_eq(pes->program_packet_sequence_counter, 0);
    ck_assert(!pes->mpeg1_mpeg2_identifier);
    ck_assert_uint_eq(pes->original_stuff_length, 0);
    ck_assert(!pes->pstd_buffer_scale);
    ck_assert_uint_eq(pes->pstd_buffer_size, 0);
    ck_assert_uint_eq(pes->extension_field_length, 0);
    ck_assert(!pes->stream_id_extension_flag);
    ck_assert_uint_eq(pes->stream_id_extension, 0);
    ck_assert(!pes->tref_extension_flag);
    ck_assert_uint_eq(pes->tref, 0);

    ck_assert_ptr_ne(pes->payload, NULL);
    ck_assert_uint_eq(pes->payload_len, 157);

    pes_free(pes);
END_TEST

START_TEST(test_read_pes_no_payload)
    uint8_t bytes[] = {
      0x00, 0x00, 0x01, 0xe0, 0x00, 0, 0x80, 0xc0, 0x0a, 0x31, 0x00, 0x09,
      0x12, 0xf9, 0x11, 0x00, 0x07, 0xd8, 0x61,
    };

    pes_packet_t* pes = pes_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(pes, NULL);
    ck_assert_uint_eq(pes->stream_id, 224);
    ck_assert_uint_eq(pes->scrambling_control, 0);

    ck_assert_ptr_eq(pes->payload, NULL);
    ck_assert_uint_eq(pes->payload_len, 0);

    pes_free(pes);
END_TEST

START_TEST(test_read_pes_long)
    pes_packet_t* pes = pes_read(TEST_PES_DATA, sizeof(TEST_PES_DATA));

    ck_assert_ptr_ne(pes, NULL);
    ck_assert_uint_eq(pes->packet_length, 14880);
    ck_assert_uint_eq(pes->stream_id, 224);
    ck_assert_uint_eq(pes->scrambling_control, 0);
    ck_assert(!pes->priority);
    ck_assert(!pes->data_alignment_indicator);
    ck_assert(!pes->copyright);
    ck_assert(!pes->original_or_copy);
    ck_assert(pes->pts_flag);
    ck_assert(pes->dts_flag);
    ck_assert(!pes->escr_flag);
    ck_assert(!pes->es_rate_flag);
    ck_assert(!pes->dsm_trick_mode_flag);
    ck_assert(!pes->additional_copy_info_flag);
    ck_assert(!pes->crc_flag);
    ck_assert(!pes->extension_flag);
    ck_assert_uint_eq(pes->pts, 3606000);
    ck_assert_uint_eq(pes->dts, 3600000);
    ck_assert_uint_eq(pes->escr_base, 0);
    ck_assert_uint_eq(pes->escr_extension, 0);
    ck_assert_uint_eq(pes->es_rate, 0);
    ck_assert_uint_eq(pes->trick_mode_control, 0);
    ck_assert_uint_eq(pes->field_id, 0);
    ck_assert(!pes->intra_slice_refresh);
    ck_assert_uint_eq(pes->frequency_truncation, 0);
    ck_assert_uint_eq(pes->rep_cntrl, 0);
    ck_assert_uint_eq(pes->additional_copy_info, 0);
    ck_assert_uint_eq(pes->previous_pes_packet_crc, 0);
    ck_assert(!pes->private_data_flag);
    ck_assert(!pes->pack_header_field_flag);
    ck_assert(!pes->program_packet_sequence_counter_flag);
    ck_assert(!pes->pstd_buffer_flag);
    ck_assert(!pes->extension_flag_2);
    uint8_t zero_bytes[16] = {0};
    assert_bytes_eq(pes->private_data, 16, zero_bytes, 16);
    ck_assert_uint_eq(pes->pack_field_length, 0);
    ck_assert_uint_eq(pes->program_packet_sequence_counter, 0);
    ck_assert(!pes->mpeg1_mpeg2_identifier);
    ck_assert_uint_eq(pes->original_stuff_length, 0);
    ck_assert(!pes->pstd_buffer_scale);
    ck_assert_uint_eq(pes->pstd_buffer_size, 0);
    ck_assert_uint_eq(pes->extension_field_length, 0);
    ck_assert(!pes->stream_id_extension_flag);
    ck_assert_uint_eq(pes->stream_id_extension, 0);
    ck_assert(!pes->tref_extension_flag);
    ck_assert_uint_eq(pes->tref, 0);

    ck_assert_ptr_ne(pes->payload, NULL);
    ck_assert_uint_eq(pes->payload_len, 14864);

    pes_free(pes);
END_TEST

/* TODO: Find a PES packet with a more interesting header to test */

Suite *suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("PES Packets");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_read_pes);
    tcase_add_test(tc_core, test_read_pes_too_short);
    tcase_add_test(tc_core, test_read_pes_too_long);
    tcase_add_test(tc_core, test_read_pes_no_payload);
    tcase_add_test(tc_core, test_read_pes_long);

    suite_add_tcase(s, tc_core);

    return s;
}
