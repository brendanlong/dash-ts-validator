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

START_TEST(test_cets_ecm_read)
    uint8_t example_bytes[] = {64, 64, 196, 200, 204, 208, 212, 216, 220, 224, 228, 192, 196, 200, 204, 208, 212, 218,
                               4, 1, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 24};
    cets_ecm_t* cets_ecm = cets_ecm_read(example_bytes, sizeof(example_bytes));

    ck_assert_ptr_ne(cets_ecm, NULL);
    ck_assert_int_eq(cets_ecm->next_key_id_flag, 0);
    ck_assert_int_eq(cets_ecm->iv_size, 16);
    ck_assert_int_eq(cets_ecm->num_states, 1);
    assert_array_eq(cets_ecm->default_key_id, 16, "1234567890123456", 16);
    if (cets_ecm->num_states > 0) {
        cets_ecm_state_t* state = &cets_ecm->states[0];
        ck_assert_int_eq(state->transport_scrambling_control, 2);
        ck_assert_int_eq(state->num_au, 1);
        if (state->num_au > 0) {
            cets_ecm_au_t* au = &state->au[0];
            ck_assert_int_eq(au->key_id_flag, 0);
            ck_assert_int_eq(au->byte_offset_size, 0);
            // key_id undefined if key_id_flag == 0
            // byte_offset undefined if byte_offset_size == 0
            assert_array_eq(au->initialization_vector, 16, "FFFFFFFFFFFFFFFF", 16);
        }
    }
    // countdown_sec and next_key_id undefined if countdown_sec == 0
    cets_ecm_free(cets_ecm);
END_TEST

START_TEST(test_cets_ecm_read_bad_length)
    uint8_t example_bytes[] = {64, 64, 196, 200, 204, 208, 212, 216, 220, 224, 228, 192, 196, 200, 204, 208, 212, 218,
                               4, 1};
    cets_ecm_t* cets_ecm = cets_ecm_read(example_bytes, sizeof(example_bytes));

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

    tcase_add_test(tc_core, test_cets_ecm_read);
    tcase_add_test(tc_core, test_cets_ecm_read_bad_length);
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