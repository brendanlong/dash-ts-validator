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

#include "descriptors.h"
#include "test_common.h"

START_TEST(test_descriptor_read_no_data)
    uint8_t bytes[] = {64, 0};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(desc, NULL);
    ck_assert_int_eq(desc->tag, bytes[0]);
    ck_assert_int_eq(desc->data_len, 0);

    descriptor_free(desc);
END_TEST

START_TEST(test_descriptor_read_no_data_extra_data)
    uint8_t bytes[] = {64, 0, 1, 2, 3};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(desc, NULL);
    ck_assert_int_eq(desc->tag, bytes[0]);
    ck_assert_int_eq(desc->data_len, 0);

    descriptor_free(desc);
END_TEST

START_TEST(test_descriptor_read_data)
    uint8_t bytes[] = {69, 4, 1, 2, 3, 4};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(desc, NULL);
    ck_assert_int_eq(desc->tag, bytes[0]);
    assert_bytes_eq(desc->data, desc->data_len, bytes + 2, sizeof(bytes) - 2);

    descriptor_free(desc);
END_TEST

START_TEST(test_descriptor_read_data_length_too_long)
    uint8_t bytes[] = {69, 5, 1, 2, 3, 4};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(desc, NULL);
END_TEST

START_TEST(test_descriptor_read_not_enough_data)
    uint8_t bytes[] = {65, 4, 1};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(desc, NULL);
END_TEST

START_TEST(test_descriptor_read_too_much_data)
    uint8_t bytes[] = {66, 4, 1, 2, 3, 4, 5, 6};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(desc, NULL);
    ck_assert_int_eq(desc->tag, bytes[0]);
    assert_bytes_eq(desc->data, desc->data_len, bytes + 2, bytes[1]);

    descriptor_free(desc);
END_TEST

START_TEST(test_ca_descriptor_read_no_systems)
    uint8_t bytes[] = {9, 16, 99, 101, 1, 44, 99, 101, 110, 99, 0, 0, 0, 1, 0, 1, 2, 3};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(desc, NULL);
    ck_assert_int_eq(desc->tag, 9);
    assert_bytes_eq(desc->data, desc->data_len, bytes + 2, sizeof(bytes) - 2);

    ca_descriptor_t* cad = (ca_descriptor_t*)desc;
    ck_assert_int_eq(cad->ca_system_id, 25445);
    ck_assert_int_eq(cad->ca_pid, 300);

    assert_bytes_eq(cad->private_data, cad->private_data_len, bytes + 6, sizeof(bytes) - 6);

    descriptor_free(desc);
END_TEST

START_TEST(test_ca_descriptor_read_not_enough_data)
    uint8_t bytes[] = {9, 3, 99, 101, 1};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_eq(desc, NULL);
END_TEST

START_TEST(test_ca_descriptor_read_too_much_data)
    uint8_t bytes[] = {9, 16, 99, 101, 1, 44, 99, 101, 110, 99, 0, 0, 0, 1, 0, 1, 2, 3};

    descriptor_t* desc = descriptor_read(bytes, sizeof(bytes));

    ck_assert_ptr_ne(desc, NULL);
    ck_assert_int_eq(desc->tag, 9);
    assert_bytes_eq(desc->data, desc->data_len, bytes + 2, sizeof(bytes) - 2);

    ca_descriptor_t* cad = (ca_descriptor_t*)desc;
    ck_assert_int_eq(cad->ca_system_id, 25445);
    ck_assert_int_eq(cad->ca_pid, 300);

    assert_bytes_eq(cad->private_data, cad->private_data_len, bytes + 6, sizeof(bytes) - 6);

    descriptor_free(desc);
END_TEST

Suite *suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Descriptors");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_descriptor_read_no_data);
    tcase_add_test(tc_core, test_descriptor_read_no_data_extra_data);
    tcase_add_test(tc_core, test_descriptor_read_data);
    tcase_add_test(tc_core, test_descriptor_read_data_length_too_long);
    tcase_add_test(tc_core, test_descriptor_read_not_enough_data);
    tcase_add_test(tc_core, test_descriptor_read_too_much_data);
    tcase_add_test(tc_core, test_ca_descriptor_read_no_systems);
    tcase_add_test(tc_core, test_ca_descriptor_read_not_enough_data);
    tcase_add_test(tc_core, test_ca_descriptor_read_too_much_data);
    suite_add_tcase(s, tc_core);

    return s;
}