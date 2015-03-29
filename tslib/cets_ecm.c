/*
 Copyright (c) 2014-, ISO/IEC JTC1/SC29/WG11
 All rights reserved.

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
#include "cets_ecm.h"

#include <glib.h>
#include "bs.h"


static cets_ecm_t* cets_ecm_new(void)
{
    cets_ecm_t* obj = g_new0(cets_ecm_t, 1);
    return obj;
}

void cets_ecm_free(cets_ecm_t* obj)
{
    if (obj == NULL) {
        return;
    }
    for (size_t i = 0; i < obj->num_states; ++i) {
        g_free(obj->states[i].au);
    }
    g_free(obj);
}

cets_ecm_t* cets_ecm_read(uint8_t* data, size_t len)
{
    g_return_val_if_fail(data, NULL);
    g_return_val_if_fail(len > 0, NULL);

    cets_ecm_t* ecm = cets_ecm_new();

    bs_t* b = bs_new(data, len);
    ecm->num_states = bs_read_u(b, 2);
    ecm->next_key_id_flag = bs_read_u1(b);
    bs_skip_u(b, 3); // reserved
    /* Note: Everything after this isn't byte aligned (off by 2 bits). Will notify MPEG since this is presumably a bug */
    ecm->iv_size = bs_read_u8(b);
    for (size_t i = 0; i < 16; ++i) {
        ecm->default_key_id[i] = bs_read_u8(b);
    }
    for (size_t i = 0; i < ecm->num_states; ++i) {
        cets_ecm_state_t* state = &ecm->states[i];
        state->transport_scrambling_control = bs_read_u(b, 2);
        state->num_au = bs_read_u(b, 6);
        state->au = g_new0(cets_ecm_au_t, state->num_au);
        for (size_t j = 0; j < state->num_au; ++j) {
            cets_ecm_au_t* au = &state->au[j];
            au->key_id_flag = bs_read_u1(b);
            bs_skip_u(b, 3); // reserved
            au->byte_offset_size = bs_read_u(b, 4);
            for (size_t k = 0; k < 16 && au->key_id_flag; ++k) {
                au->key_id[k] = bs_read_u8(b);
            }
            for (size_t k = 0; k < au->byte_offset_size; ++k) {
                au->byte_offset[k] = bs_read_u8(b);
            }
            for (size_t k = 0; k < ecm->iv_size; ++k) {
                au->initialization_vector[k] = bs_read_u8(b);
            }
        }
    }
    if (ecm->next_key_id_flag) {
        ecm->countdown_sec = bs_read_u(b, 4);
        bs_skip_u(b, 4); // reserved
        for (size_t i = 0; i < 16; ++i) {
            ecm->next_key_id[i] = bs_read_u8(b);
        }
    }

    if (bs_overrun(b)) {
        cets_ecm_free(ecm);
        ecm = NULL;
    }
    bs_free(b);
    return ecm;
}

void cets_ecm_print(const cets_ecm_t* obj)
{
    // TODO
}