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

#include "psi.h"
#include "test_common.h"

START_TEST(test_read_pat)
    uint8_t bytes[] = {0, 0, 176, 13, 0, 1, 193, 0, 0, 0, 1, 240, 0, 42, 177, 4, 178};
    program_association_section_t* pas = program_association_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(pas, NULL);
    ck_assert_uint_eq(pas->table_id, 0);
    ck_assert(pas->section_syntax_indicator);
    ck_assert_uint_eq(pas->section_length, 13);
    ck_assert_uint_eq(pas->transport_stream_id, 1);
    ck_assert_uint_eq(pas->version_number, 0);
    ck_assert(pas->current_next_indicator);
    ck_assert_uint_eq(pas->section_number, 0);
    ck_assert_uint_eq(pas->last_section_number, 0);
    ck_assert_uint_eq(pas->num_programs, 1);
    ck_assert_uint_eq(pas->crc_32, 716244146);

    program_info_t* program = &pas->programs[0];
    ck_assert_uint_eq(program->program_number, 1);
    ck_assert_uint_eq(program->program_map_pid, 4096);

    program_association_section_free(pas);
END_TEST

START_TEST(test_read_cat)
    /* TODO 
    uint8_t bytes[] = {};
    conditional_access_section_t* cas = conditional_access_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(cas, NULL);

    conditional_access_section_free(cas);
    */
END_TEST

START_TEST(test_read_pmt)
    uint8_t bytes[] = {0, 2, 128, 120, 0, 1, 1, 0, 0, 1, 0, 0, 17, 37, 15, 255, 255, 73, 68, 51, 32, 255, 73, 68, 51, 32,
            0, 31, 0, 1, 27, 1, 0, 0, 18, 9, 16, 99, 101, 1, 44, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 1, 1, 0, 24,
            10, 4, 101, 110, 103, 0, 9, 16, 99, 101, 1, 45, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 21, 1, 2, 0, 33, 38,
            13, 255, 255, 73, 68, 51, 32, 255, 73, 68, 51, 32, 0, 15, 9, 16, 99, 101, 1, 46, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 56, 19, 253, 173};
    program_map_section_t* pms = program_map_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(pms, NULL);
    ck_assert_uint_eq(pms->table_id, 2);
    ck_assert(pms->section_syntax_indicator);
    ck_assert_uint_eq(pms->section_length, 120);
    ck_assert_uint_eq(pms->program_number, 1);
    ck_assert_uint_eq(pms->version_number, 0);
    ck_assert(pms->current_next_indicator);
    ck_assert_uint_eq(pms->section_number, 0);
    ck_assert_uint_eq(pms->last_section_number, 0);
    ck_assert_uint_eq(pms->pcr_pid, 256);
    ck_assert_uint_eq(pms->descriptors_len, 1);
    ck_assert_uint_eq(pms->es_info_len, 3);
    ck_assert_uint_eq(pms->crc_32, 940834221);

    descriptor_t* desc = pms->descriptors[0];
    ck_assert_uint_eq(desc->tag, 37);

    program_map_section_free(pms);
END_TEST

START_TEST(test_read_pmt_extra_data)
    uint8_t bytes[] = {0, 2, 176, 66, 0, 1, 193, 0, 0, 225, 0, 240, 17, 37, 15, 255, 255, 73, 68, 51, 32, 255, 73, 68, 51,
            32, 0, 31, 0, 1, 27, 225, 0, 240, 0, 15, 225, 1, 240, 6, 10, 4, 101, 110, 103, 0, 21, 225, 2, 240, 15, 38,
            13, 255, 255, 73, 68, 51, 32, 255, 73, 68, 51, 32, 0, 15, 145, 29, 89, 30, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    program_map_section_t* pms = program_map_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(pms, NULL);
    ck_assert_uint_eq(pms->table_id, 2);
    ck_assert(pms->section_syntax_indicator);
    ck_assert_uint_eq(pms->section_length, 66);
    ck_assert_uint_eq(pms->program_number, 1);
    ck_assert_uint_eq(pms->version_number, 0);
    ck_assert(pms->current_next_indicator);
    ck_assert_uint_eq(pms->section_number, 0);
    ck_assert_uint_eq(pms->last_section_number, 0);
    ck_assert_uint_eq(pms->pcr_pid, 256);
    ck_assert_uint_eq(pms->descriptors_len, 1);
    ck_assert_uint_eq(pms->es_info_len, 3);
    ck_assert_uint_eq(pms->crc_32, 2434619678);

    descriptor_t* desc = pms->descriptors[0];
    ck_assert_uint_eq(desc->tag, 37);

    program_map_section_free(pms);
END_TEST

static Suite *suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Program Specific Information");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_read_pat);
    tcase_add_test(tc_core, test_read_cat);
    tcase_add_test(tc_core, test_read_pmt);
    tcase_add_test(tc_core, test_read_pmt_extra_data);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}