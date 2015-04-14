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

#include "isobmff.h"
#include "test_common.h"

START_TEST(test_read_representation_index_with_subsegment_index)
    int error = 0;
    size_t boxes_len;
    box_t** boxes = read_boxes_from_file("tests/subsegment-example.six", &boxes_len, &error);

    ck_assert(!error);
    ck_assert_ptr_ne(boxes, NULL);
    ck_assert_uint_eq(boxes_len, 54);

    box_t* box = boxes[0];
    char type[5] = {0};
    ck_assert_ptr_ne(box, NULL);
    uint32_to_string(type, box->type);
    ck_assert_str_eq(type, "styp");
    ck_assert_uint_eq(box->size, 24);

    styp_t* styp = (styp_t*)box;
    char major_brand[5] = {0};
    uint32_to_string(major_brand, styp->major_brand);
    ck_assert_str_eq(major_brand, "risx");
    ck_assert_uint_eq(styp->minor_version, 0);
    ck_assert_uint_eq(styp->num_compatible_brands, 2);
    char* expected_compatible_brands[2] = {"risx", "ssss"};
    for (size_t i = 0; i < styp->num_compatible_brands; ++i) {
        char compatible_brand[5] = {0};
        uint32_to_string(compatible_brand, styp->compatible_brands[i]);
        ck_assert_str_eq(compatible_brand, expected_compatible_brands[i]);
    }

    box = boxes[1];
    ck_assert_ptr_ne(box, NULL);
    uint32_to_string(type, box->type);
    ck_assert_str_eq(type, "sidx");
    ck_assert_uint_eq(box->size, 344);

    sidx_t* sidx = (sidx_t*)box;
    ck_assert_uint_eq(sidx->version, 0);
    ck_assert_uint_eq(sidx->flags, 0);
    ck_assert_uint_eq(sidx->reference_id, 256);
    ck_assert_uint_eq(sidx->timescale, 90000);
    ck_assert_uint_eq(sidx->earliest_presentation_time, 0);
    ck_assert_uint_eq(sidx->first_offset, 0);
    ck_assert_uint_eq(sidx->reference_count, 26);
    {
        uint32_t expected_referenced_size[26] = {948, 944, 944, 944, 944, 944, 944, 944, 944, 944, 944, 944, 944, 944,
                944, 944, 944, 944, 944, 944, 944, 944, 944, 944, 944, 696};
        for (size_t i = 0; i < sidx->reference_count; ++i) {
            sidx_reference_t* reference = &sidx->references[i];
            ck_assert_uint_eq(reference->reference_type, 1);
            ck_assert_uint_eq(reference->referenced_size, expected_referenced_size[i]);
            ck_assert_uint_eq(reference->subsegment_duration, (i == sidx->reference_count - 1) ? 645000 : 900000);
            ck_assert(!reference->starts_with_sap);
            ck_assert_uint_eq(reference->sap_type, 0);
            ck_assert_uint_eq(reference->sap_delta_time, 0);
        }
    }

    for (size_t box_i = 0; box_i < 26; ++box_i) {
        box = boxes[box_i * 2 + 2];
        ck_assert_ptr_ne(box, NULL);
        uint32_to_string(type, box->type);
        ck_assert_str_eq(type, "sidx");
        ck_assert_uint_eq(box->size, (box_i == 25) ? 80 : 92);

        sidx = (sidx_t*)box;
        ck_assert_uint_eq(sidx->version, 0);
        ck_assert_uint_eq(sidx->flags, 0);
        ck_assert_uint_eq(sidx->reference_id, 256);
        ck_assert_uint_eq(sidx->timescale, 90000);
        ck_assert_uint_eq(sidx->earliest_presentation_time, 6000 + 900000 * box_i);
        ck_assert_uint_eq(sidx->first_offset, 376);
        ck_assert_uint_eq(sidx->reference_count, (box_i == 25) ? 4 : 5);

        uint32_t expected_referenced_size[26][5] = {
                {144948, 179164, 225788, 228796, 220712},
                {207176, 205108, 219960, 214320, 226916},
                {224096, 162808, 173524, 191384, 185556},
                {214884, 222968, 204544, 209996, 221652},
                {214320, 214132, 219584, 200220, 185744},
                {194580, 209432, 216200, 225788, 194392},
                {194204, 203604, 218832, 214884, 239888},
                {180480, 218268, 218080, 224660, 215072},
                {203416, 203416, 222216, 221840, 220148},
                {215448, 215072, 204732, 187060, 205296},
                {209996, 214132, 223720, 228420, 227104},
                {209432, 219772, 230112, 206612, 189880},
                {192136, 229736, 179352, 203416, 238948},
                {173712, 167508, 231428, 219772, 228420},
                {221840, 221276, 227104, 221464, 222028},
                {216576, 199092, 213004, 199656, 211500},
                {212252, 220524, 207176, 221652, 240076},
                {221840, 190632, 177284, 193828, 212440},
                {218644, 214696, 203416, 209808, 221088},
                {228044, 202852, 195708, 203792, 206612},
                {203792, 205484, 225788, 216576, 227856},
                {222592, 225412, 222780, 221840, 218080},
                {216952, 222592, 247220, 134044, 131036},
                {58844, 59220, 62416, 59972, 190256},
                {58280, 57152, 60536, 57528, 157920},
                {41736, 17860, 18800, 38352}
        };

        for (size_t i = 0; i < sidx->reference_count; ++i) {
            sidx_reference_t* reference = &sidx->references[i];
            ck_assert_uint_eq(reference->reference_type, 0);
            ck_assert_uint_eq(reference->referenced_size, expected_referenced_size[box_i][i]);
            ck_assert_uint_eq(reference->subsegment_duration, (box_i == 25 && i == sidx->reference_count - 1) ? 105000 : 180000);
            ck_assert(reference->starts_with_sap);
            ck_assert_uint_eq(reference->sap_type, 1);
            ck_assert_uint_eq(reference->sap_delta_time, 0);
        }
    }

    free_boxes(boxes, boxes_len);
END_TEST
#include <stdio.h>
START_TEST(test_read_representation_index_with_pcrb)
    int error = 0;
    size_t boxes_len;
    box_t** boxes = read_boxes_from_file("tests/pcrb-example.six", &boxes_len, &error);

    ck_assert(!error);
    ck_assert_ptr_ne(boxes, NULL);
    ck_assert_uint_eq(boxes_len, 3);

    box_t* box = boxes[0];
    char type[5] = {0};
    ck_assert_ptr_ne(box, NULL);
    uint32_to_string(type, box->type);
    ck_assert_str_eq(type, "styp");
    ck_assert_uint_eq(box->size, 20);

    styp_t* styp = (styp_t*)box;
    char major_brand[5] = {0};
    uint32_to_string(major_brand, styp->major_brand);
    ck_assert_str_eq(major_brand, "risx");
    ck_assert_uint_eq(styp->minor_version, 0);
    ck_assert_uint_eq(styp->num_compatible_brands, 1);
    char* expected_compatible_brands[2] = {"risx"};
    for (size_t i = 0; i < styp->num_compatible_brands; ++i) {
        char compatible_brand[5] = {0};
        uint32_to_string(compatible_brand, styp->compatible_brands[i]);
        ck_assert_str_eq(compatible_brand, expected_compatible_brands[i]);
    }

    box = boxes[1];
    ck_assert_ptr_ne(box, NULL);
    uint32_to_string(type, box->type);
    ck_assert_str_eq(type, "sidx");
    ck_assert_uint_eq(box->size, 284);

    sidx_t* sidx = (sidx_t*)box;
    ck_assert_uint_eq(sidx->version, 0);
    ck_assert_uint_eq(sidx->flags, 0);
    ck_assert_uint_eq(sidx->reference_id, 256);
    ck_assert_uint_eq(sidx->timescale, 90000);
    ck_assert_uint_eq(sidx->earliest_presentation_time, 133500);
    ck_assert_uint_eq(sidx->first_offset, 0);
    ck_assert_uint_eq(sidx->reference_count, 21);
    uint32_t expected_referenced_size[21] = {6356468, 6530556, 4718236, 2835040, 5667824, 7061092, 5634172, 4378332,
            5582472, 5656168, 4278128, 5221324, 4775764, 6072588, 5702604, 6025400, 5961856, 4444696, 2919452, 134044,
            8648};
    for (size_t i = 0; i < sidx->reference_count; ++i) {
        sidx_reference_t* reference = &sidx->references[i];
        ck_assert_uint_eq(reference->reference_type, 0);
        ck_assert_uint_eq(reference->referenced_size, expected_referenced_size[i]);
        ck_assert_uint_eq(reference->subsegment_duration, (i == sidx->reference_count - 1) ? 105000 : 900000);
        ck_assert(reference->starts_with_sap);
        ck_assert_uint_eq(reference->sap_type, 1);
        ck_assert_uint_eq(reference->sap_delta_time, 0);
    }

    box = boxes[2];
    ck_assert_ptr_ne(box, NULL);
    uint32_to_string(type, box->type);
    ck_assert_str_eq(type, "pcrb");
    ck_assert_uint_eq(box->size, 138);

    pcrb_t* pcrb = (pcrb_t*)box;
    ck_assert_uint_eq(pcrb->subsegment_count, 21);
    uint64_t expected_pcr[21] = {304545000, 561554191, 852267469, 1135508108, 1390208823, 1671181250, 1963474468,
            2212015384, 2456513207, 2723122340, 3021708139, 3317735106, 3563989285, 3834697752, 4107719620, 4377003260,
            4629697297, 4941900000, 5321025000, 5454900000, 5450400000};
    assert_arrays_eq(ck_assert_uint_eq, pcrb->pcr, pcrb->subsegment_count, expected_pcr, 21);

    free_boxes(boxes, boxes_len);
END_TEST

Suite *suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("ISO Base Media File Format");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_read_representation_index_with_subsegment_index);
    tcase_add_test(tc_core, test_read_representation_index_with_pcrb);

    suite_add_tcase(s, tc_core);

    return s;
}