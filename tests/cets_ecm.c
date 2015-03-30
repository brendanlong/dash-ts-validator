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

#include "cets_ecm.h"
#include "test_common.h"

START_TEST(test_cets_ecm_read_no_states_no_next_key_id)
    uint8_t ecm_bytes[] = {0, 1, 56, 158, 174, 34, 247, 204, 197, 249, 24, 174, 193, 182, 68, 91, 66, 160};
    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_ne(cets_ecm, NULL);
    ck_assert_int_eq(cets_ecm->next_key_id_flag, 0);
    ck_assert_int_eq(cets_ecm->iv_size, 0);
    ck_assert_int_eq(cets_ecm->num_states, 0);

    uint8_t default_key_id[] = {78, 39, 171, 136, 189, 243, 49, 126, 70, 43, 176, 109, 145, 22, 208, 168};
    assert_array_eq(cets_ecm->default_key_id, 16, default_key_id, 16);
END_TEST

START_TEST(test_cets_ecm_read_one_state_one_au)
    uint8_t default_key_id[] = {50, 222, 158, 99, 217, 105, 113, 85, 37, 99, 16, 134, 251, 24, 132, 53};
    uint8_t iv[] = {57, 103, 110, 255, 88, 173, 25, 23, 1, 187, 244, 209, 229, 127, 142, 60};
    uint8_t ecm_bytes[] = {64, 64, 203, 122, 121, 143, 101, 165, 197, 84, 149, 140, 66, 27, 236, 98, 16, 213, 4, 0, 229,
                           157, 187, 253, 98, 180, 100, 92, 6, 239, 211, 71, 149, 254, 56, 240};

    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_ne(cets_ecm, NULL);
    ck_assert_int_eq(cets_ecm->next_key_id_flag, 0);
    ck_assert_int_eq(cets_ecm->iv_size, 16);
    ck_assert_int_eq(cets_ecm->num_states, 1);
    assert_array_eq(cets_ecm->default_key_id, 16, default_key_id, sizeof(default_key_id));
    if (cets_ecm->num_states > 0) {
        cets_ecm_state_t* state = &cets_ecm->states[0];
        ck_assert_int_eq(state->transport_scrambling_control, 1);
        ck_assert_int_eq(state->num_au, 1);
        if (state->num_au > 0) {
            cets_ecm_au_t* au = &state->au[0];
            ck_assert_int_eq(au->key_id_flag, 0);
            ck_assert_int_eq(au->byte_offset_size, 0);
            // key_id undefined if key_id_flag == 0
            // byte_offset undefined if byte_offset_size == 0
            assert_array_eq(au->initialization_vector, cets_ecm->iv_size, iv, sizeof(iv));
        }
    }
    // countdown_sec and next_key_id undefined if countdown_sec == 0
    cets_ecm_free(cets_ecm);
END_TEST

START_TEST(test_cets_ecm_read_two_states_no_au)
    uint8_t default_key_id[] = {163, 77, 13, 36, 35, 135, 214, 199, 185, 52, 51, 127, 89, 76, 37, 155};
    uint8_t ecm_bytes[] = {128, 2, 141, 52, 52, 144, 142, 31, 91, 30, 228, 208, 205, 253, 101, 48, 150, 109, 3, 0};

    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_ne(cets_ecm, NULL);
    ck_assert_int_eq(cets_ecm->next_key_id_flag, 0);
    ck_assert_int_eq(cets_ecm->iv_size, 0);
    ck_assert_int_eq(cets_ecm->num_states, 2);
    assert_array_eq(cets_ecm->default_key_id, 16, default_key_id, sizeof(default_key_id));
    if (cets_ecm->num_states > 0) {
        cets_ecm_state_t* state = &cets_ecm->states[0];
        ck_assert_int_eq(state->transport_scrambling_control, 1);
        ck_assert_int_eq(state->num_au, 0);
    }
    if (cets_ecm->num_states > 1) {
        cets_ecm_state_t* state = &cets_ecm->states[1];
        ck_assert_int_eq(state->transport_scrambling_control, 3);
        ck_assert_int_eq(state->num_au, 0);
    }
    // countdown_sec and next_key_id undefined if countdown_sec == 0
    cets_ecm_free(cets_ecm);
END_TEST

START_TEST(test_cets_ecm_read_one_state_two_au)
    uint8_t iv_au_1[] = {250, 225, 219, 188, 238, 166, 60, 118, 196, 10, 52, 185, 215, 217, 113, 19};
    uint8_t iv_au_2[] = {84, 185, 125, 74, 121, 96, 145, 68, 33, 37, 1, 156, 96, 16, 179, 126};
    uint8_t byte_offset_au_2[] = {210, 6, 172};
    uint8_t key_id_au_2[] = {238, 223, 224, 101, 105, 100, 223, 224, 18, 0, 105, 111, 13, 166, 166, 167};
    uint8_t default_key_id[] = {174, 201, 221, 78, 234, 15, 195, 28, 219, 200, 112, 58, 25, 218, 14, 16};
    uint8_t next_key_id[] = {115, 65, 117, 100, 197, 251, 171, 66, 220, 92, 219, 242, 165, 227, 24, 228};
    uint8_t ecm_bytes[] = {96, 66, 187, 39, 117, 59, 168, 63, 12, 115, 111, 33, 192, 232, 103, 104, 56, 66, 8, 3, 235,
        135, 110, 243, 186, 152, 241, 219, 16, 40, 210, 231, 95, 101, 196, 78, 15, 187, 127, 129, 149, 165, 147,
        127, 128, 72, 1, 165, 188, 54, 154, 154, 159, 72, 26, 177, 82, 229, 245, 41, 229, 130, 69, 16, 132, 148,
        6, 113, 128, 66, 205, 249, 65, 205, 5, 213, 147, 23, 238, 173, 11, 113, 115, 111, 202, 151, 140, 99, 144};

    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_ne(cets_ecm, NULL);
    ck_assert_int_eq(cets_ecm->next_key_id_flag, 1);
    ck_assert_int_eq(cets_ecm->iv_size, 16);
    ck_assert_int_eq(cets_ecm->num_states, 1);
    assert_array_eq(cets_ecm->default_key_id, 16, default_key_id, sizeof(default_key_id));
    if (cets_ecm->num_states > 0) {
        cets_ecm_state_t* state = &cets_ecm->states[0];
        ck_assert_int_eq(state->transport_scrambling_control, 2);
        ck_assert_int_eq(state->num_au, 2);
        if (state->num_au > 0) {
            cets_ecm_au_t* au = &state->au[0];
            ck_assert_int_eq(au->key_id_flag, 0);
            ck_assert_int_eq(au->byte_offset_size, 0);
            // key_id undefined if key_id_flag == 0
            // byte_offset undefined if byte_offset_size == 0
            assert_array_eq(au->initialization_vector, cets_ecm->iv_size, iv_au_1, sizeof(iv_au_1));
        }
        if (state->num_au > 1) {
            cets_ecm_au_t* au = &state->au[1];
            ck_assert_int_eq(au->key_id_flag, 1);
            assert_array_eq(au->key_id, 16, key_id_au_2, sizeof(key_id_au_2));
            assert_array_eq(au->byte_offset, au->byte_offset_size, byte_offset_au_2, sizeof(byte_offset_au_2));
            assert_array_eq(au->initialization_vector, cets_ecm->iv_size, iv_au_2, sizeof(iv_au_2));
        }
    }
    ck_assert_int_eq(cets_ecm->countdown_sec, 5);
    assert_array_eq(cets_ecm->next_key_id, 16, next_key_id, sizeof(next_key_id));
    cets_ecm_free(cets_ecm);
END_TEST

START_TEST(test_cets_ecm_header_too_short)
    uint8_t ecm_bytes[] = {0, 64};
    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_eq(cets_ecm, NULL);
    cets_ecm_free(cets_ecm);
END_TEST

START_TEST(test_cets_ecm_read_too_few_states)
    uint8_t ecm_bytes[] = {128, 64, 196, 200, 204, 208, 212, 216, 220, 224, 228, 192, 196, 200, 204, 208, 212, 218,
                               4, 1, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 24};
    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_eq(cets_ecm, NULL);
    cets_ecm_free(cets_ecm);
END_TEST

START_TEST(test_cets_ecm_read_too_many_states)
    uint8_t ecm_bytes[] = {0, 64, 196, 200, 204, 208, 212, 216, 220, 224, 228, 192, 196, 200, 204, 208, 212, 218,
                               4, 1, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 24};
    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_eq(cets_ecm, NULL);
    cets_ecm_free(cets_ecm);
END_TEST

START_TEST(test_cets_ecm_too_few_au)
    uint8_t ecm_bytes[] = {96, 66, 128, 197, 85, 89, 71, 236, 87, 106, 19, 145, 147, 178, 245, 141, 223, 62, 0, 3, 238,
        113, 72, 66, 174, 163, 130, 214, 100, 117, 13, 139, 10, 204, 82, 29, 65, 22, 150, 70, 130, 77, 221, 223, 184,
        246, 146, 204, 112, 173, 213, 133, 112};

    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_eq(cets_ecm, NULL);
    cets_ecm_free(cets_ecm);
END_TEST

START_TEST(test_cets_ecm_too_many_au)
    uint8_t ecm_bytes[] = {96, 66, 123, 168, 59, 156, 114, 245, 120, 168, 227, 42, 0, 177, 159, 210, 221, 158, 8, 0,
        144, 20, 150, 184, 218, 77, 185, 241, 183, 39, 21, 221, 205, 26, 89, 45, 66, 38, 28, 34, 131, 127, 164, 18, 36,
        255, 230, 90, 62, 20, 70, 44, 204};

    cets_ecm_t* cets_ecm = cets_ecm_read(ecm_bytes, sizeof(ecm_bytes));

    ck_assert_ptr_eq(cets_ecm, NULL);
    cets_ecm_free(cets_ecm);
END_TEST

Suite *cets_ecm_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("CETS ECM");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_cets_ecm_read_no_states_no_next_key_id);
    tcase_add_test(tc_core, test_cets_ecm_read_one_state_one_au);
    tcase_add_test(tc_core, test_cets_ecm_read_two_states_no_au);
    tcase_add_test(tc_core, test_cets_ecm_read_one_state_two_au);
    tcase_add_test(tc_core, test_cets_ecm_header_too_short);
    tcase_add_test(tc_core, test_cets_ecm_read_too_few_states);
    tcase_add_test(tc_core, test_cets_ecm_read_too_many_states);
    tcase_add_test(tc_core, test_cets_ecm_too_few_au);
    tcase_add_test(tc_core, test_cets_ecm_too_many_au);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = cets_ecm_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}