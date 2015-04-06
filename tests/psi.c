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

    program_association_section_unref(pas);
END_TEST

START_TEST(test_read_pat_two_programs)
    uint8_t bytes[] = {0, 0, 176, 17, 1, 1, 193, 0, 0, 0, 1, 240, 0, 5, 5, 240, 2, 0x60, 0xF6, 0x9B, 0xB6};
    program_association_section_t* pas = program_association_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(pas, NULL);
    ck_assert_uint_eq(pas->table_id, 0);
    ck_assert(pas->section_syntax_indicator);
    ck_assert_uint_eq(pas->section_length, 17);
    ck_assert_uint_eq(pas->transport_stream_id, 257);
    ck_assert_uint_eq(pas->version_number, 0);
    ck_assert(pas->current_next_indicator);
    ck_assert_uint_eq(pas->section_number, 0);
    ck_assert_uint_eq(pas->last_section_number, 0);
    ck_assert_uint_eq(pas->num_programs, 2);
    ck_assert_uint_eq(pas->crc_32, 0x60F69BB6);

    program_info_t* program = &pas->programs[0];
    ck_assert_uint_eq(program->program_number, 1);
    ck_assert_uint_eq(program->program_map_pid, 4096);

    program = &pas->programs[1];
    ck_assert_uint_eq(program->program_number, 0x0505);
    ck_assert_uint_eq(program->program_map_pid, 4098);

    program_association_section_unref(pas);
END_TEST

START_TEST(test_read_pat_no_programs)
    uint8_t bytes[] = {0, 0, 176, 9, 0, 1, 193, 0, 0, 0xEF, 0x22, 0x62, 0x17};
    program_association_section_t* pas = program_association_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(pas, NULL);
    ck_assert_uint_eq(pas->table_id, 0);
    ck_assert(pas->section_syntax_indicator);
    ck_assert_uint_eq(pas->section_length, 9);
    ck_assert_uint_eq(pas->transport_stream_id, 1);
    ck_assert_uint_eq(pas->version_number, 0);
    ck_assert(pas->current_next_indicator);
    ck_assert_uint_eq(pas->section_number, 0);
    ck_assert_uint_eq(pas->last_section_number, 0);
    ck_assert_uint_eq(pas->num_programs, 0);
    ck_assert_uint_eq(pas->crc_32, 0xEF226217);

    program_association_section_unref(pas);
END_TEST

START_TEST(test_read_pat_no_programs_bad_length)
    uint8_t bytes[] = {0, 0, 176, 13, 0, 1, 193, 0, 0, 0xEF, 0x22, 0x62, 0x17};
    program_association_section_t* pas = program_association_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(pas, NULL);

    program_association_section_unref(pas);
END_TEST

START_TEST(test_read_pat_no_programs_bad_crc)
    uint8_t bytes[] = {0, 0, 176, 9, 0, 2, 193, 0, 0, 0xEF, 0x22, 0x62, 0x17};
    program_association_section_t* pas = program_association_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(pas, NULL);

    program_association_section_unref(pas);
END_TEST

START_TEST(test_read_pat_extra_data)
    uint8_t bytes[] = {0, 0, 176, 13, 0, 1, 193, 0, 0, 0, 1, 240, 0, 42, 177, 4, 178, 255, 255, 255, 255, 255};
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

    program_association_section_unref(pas);
END_TEST

START_TEST(test_read_cat_no_descriptors)
    uint8_t bytes[] = {0, 1, 128, 9, 0, 0, 0, 0, 0, 221, 29, 239, 78};
    conditional_access_section_t* cas = conditional_access_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(cas, NULL);
    ck_assert_uint_eq(cas->table_id, 1);
    ck_assert_uint_eq(cas->version_number, 0);
    ck_assert_uint_eq(cas->current_next_indicator, 0);
    ck_assert_uint_eq(cas->section_number, 0);
    ck_assert_uint_eq(cas->last_section_number, 0);
    ck_assert_uint_eq(cas->crc_32, 3709726542);
    ck_assert_uint_eq(cas->descriptors_len, 0);

    conditional_access_section_unref(cas);
END_TEST

START_TEST(test_read_cat_complex)
    uint8_t bytes[] = {0, 1, 128, 27, 0, 0, 17, 5, 129, 9, 16, 99, 101, 1, 46, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0x15, 0xB7, 0xB5, 0x08};
    conditional_access_section_t* cas = conditional_access_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(cas, NULL);
    ck_assert_uint_eq(cas->table_id, 1);
    ck_assert_uint_eq(cas->version_number, 8);
    ck_assert_uint_eq(cas->current_next_indicator, 1);
    ck_assert_uint_eq(cas->section_number, 5);
    ck_assert_uint_eq(cas->last_section_number, 129);
    ck_assert_uint_eq(cas->crc_32, 0x15B7B508);
    ck_assert_uint_eq(cas->descriptors_len, 1);

    descriptor_t* desc = cas->descriptors[0];
    ck_assert_ptr_ne(desc, NULL);
    ck_assert_uint_eq(desc->tag, CA_DESCRIPTOR);

    ca_descriptor_t* ca_desc = (ca_descriptor_t*)desc;
    ck_assert_uint_eq(ca_desc->ca_system_id, 0x6365);
    ck_assert_uint_eq(ca_desc->ca_pid, 302);

    conditional_access_section_unref(cas);
END_TEST

START_TEST(test_read_cat_extra_data)
    uint8_t bytes[] = {0, 1, 128, 9, 0, 0, 0, 0, 0, 221, 29, 239, 78, 255, 255, 255};
    conditional_access_section_t* cas = conditional_access_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(cas, NULL);
    ck_assert_uint_eq(cas->table_id, 1);
    ck_assert_uint_eq(cas->version_number, 0);
    ck_assert_uint_eq(cas->current_next_indicator, 0);
    ck_assert_uint_eq(cas->section_number, 0);
    ck_assert_uint_eq(cas->last_section_number, 0);
    ck_assert_uint_eq(cas->crc_32, 3709726542);

    conditional_access_section_unref(cas);
END_TEST

START_TEST(test_read_cat_not_enough_data)
    uint8_t bytes[] = {0, 1, 128, 9, 0, 0, 0, 0, 0, 221, 29, 239};
    conditional_access_section_t* cas = conditional_access_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(cas, NULL);
END_TEST

START_TEST(test_read_cat_bad_section_length)
    uint8_t bytes[] = {0, 1, 128, 5, 0, 0, 0, 0, 0, 104, 253, 177, 110};
    conditional_access_section_t* cas = conditional_access_section_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(cas, NULL);
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

    elementary_stream_info_t* es = pms->es_info[0];
    ck_assert_ptr_ne(es, NULL);
    ck_assert_uint_eq(es->stream_type, 27);
    ck_assert_uint_eq(es->elementary_pid, 256);
    ck_assert_uint_eq(es->descriptors_len, 1);

    desc = es->descriptors[0];
    ck_assert_ptr_ne(desc, NULL);
    ck_assert_uint_eq(desc->tag, 9);

    ca_descriptor_t* ca_desc = (ca_descriptor_t*)desc;
    ck_assert_uint_eq(ca_desc->ca_system_id, 0x6365);
    ck_assert_uint_eq(ca_desc->ca_pid, 300);

    es = pms->es_info[1];
    ck_assert_ptr_ne(es, NULL);
    ck_assert_uint_eq(es->stream_type, 15);
    ck_assert_uint_eq(es->elementary_pid, 257);
    ck_assert_uint_eq(es->descriptors_len, 2);

    desc = es->descriptors[0];
    ck_assert_ptr_ne(desc, NULL);
    ck_assert_uint_eq(desc->tag, 10);

    desc = es->descriptors[1];
    ck_assert_ptr_ne(desc, NULL);

    ck_assert_uint_eq(desc->tag, 9);ca_desc = (ca_descriptor_t*)desc;
    ck_assert_uint_eq(ca_desc->ca_system_id, 0x6365);
    ck_assert_uint_eq(ca_desc->ca_pid, 301);

    es = pms->es_info[2];
    ck_assert_ptr_ne(es, NULL);
    ck_assert_uint_eq(es->stream_type, 21);
    ck_assert_uint_eq(es->elementary_pid, 258);
    ck_assert_uint_eq(es->descriptors_len, 2);

    desc = es->descriptors[0];
    ck_assert_ptr_ne(desc, NULL);
    ck_assert_uint_eq(desc->tag, 38);

    desc = es->descriptors[1];
    ck_assert_ptr_ne(desc, NULL);
    ck_assert_uint_eq(desc->tag, 9);

    ck_assert_uint_eq(desc->tag, 9);ca_desc = (ca_descriptor_t*)desc;
    ck_assert_uint_eq(ca_desc->ca_system_id, 0x6365);
    ck_assert_uint_eq(ca_desc->ca_pid, 302);

    program_map_section_unref(pms);
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

    program_map_section_unref(pms);
END_TEST

Suite *suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Program Specific Information");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_read_pat);
    tcase_add_test(tc_core, test_read_pat_two_programs);
    tcase_add_test(tc_core, test_read_pat_no_programs);
    tcase_add_test(tc_core, test_read_pat_no_programs_bad_length);
    tcase_add_test(tc_core, test_read_pat_no_programs_bad_crc);
    tcase_add_test(tc_core, test_read_pat_extra_data);
    tcase_add_test(tc_core, test_read_cat_no_descriptors);
    tcase_add_test(tc_core, test_read_cat_complex);
    tcase_add_test(tc_core, test_read_cat_extra_data);
    tcase_add_test(tc_core, test_read_cat_not_enough_data);
    tcase_add_test(tc_core, test_read_cat_bad_section_length);
    tcase_add_test(tc_core, test_read_pmt);
    tcase_add_test(tc_core, test_read_pmt_extra_data);

    suite_add_tcase(s, tc_core);

    return s;
}