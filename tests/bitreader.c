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

#include "bitreader.h"
#include "test_common.h"

START_TEST(test_bitreader_aligned)
    uint8_t bytes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 170};
    bitreader_t* b = bitreader_new(bytes, sizeof(bytes));

    ck_assert_int_eq(bitreader_read_uint8(b), 1);
    ck_assert_int_eq(bitreader_read_uint16(b), 515);
    ck_assert_int_eq(bitreader_read_uint24(b), 263430);
    ck_assert_int_eq(bitreader_read_uint32(b), 117967114);
    ck_assert_int_eq(bitreader_read_uint64(b), 72623859790382856);
    ck_assert(!bitreader_eof(b));
    ck_assert(bitreader_read_bit(b));
    ck_assert(!bitreader_eof(b));
    ck_assert(!bitreader_read_bit(b));
    ck_assert_int_eq(bitreader_read_bits(b, 2), 2);
    ck_assert_int_eq(bitreader_read_bits(b, 4), 10);

    ck_assert(bitreader_eof(b));
    ck_assert(!b->error);
    bitreader_skip_bits(b, 1);
    ck_assert(b->error);

    bitreader_free(b);
END_TEST

START_TEST(test_bitreader_unaligned)
    uint8_t bytes[] = {255, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8};
    bitreader_t* b = bitreader_new(bytes, sizeof(bytes));

    ck_assert(bitreader_read_bit(b));
    ck_assert_int_eq(bitreader_read_uint8(b), 254);
    ck_assert_int_eq(bitreader_read_uint16(b), 516);
    ck_assert_int_eq(bitreader_read_uint24(b), 395274);
    ck_assert_int_eq(bitreader_read_uint32(b), 202248210);
    ck_assert_int_eq(bitreader_read_uint64(b), 1441719254663171086);
    ck_assert(!bitreader_eof(b));
    ck_assert(!bitreader_read_bit(b));
    ck_assert_int_eq(bitreader_read_bits(b, 6), 8);
    ck_assert(!b->error);

    ck_assert(bitreader_eof(b));
    ck_assert(!b->error);
    bitreader_skip_bits(b, 1);
    ck_assert(b->error);

    bitreader_free(b);
END_TEST

static Suite *bitreader_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Bit Reader");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_bitreader_aligned);
    tcase_add_test(tc_core, test_bitreader_unaligned);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = bitreader_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}