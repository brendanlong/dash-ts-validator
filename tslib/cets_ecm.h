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
#ifndef TSLIB_CETS_ECM_H
#define TSLIB_CETS_ECM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct {
    bool key_id_flag;
    uint8_t key_id[16];
    uint8_t byte_offset_size;
    uint8_t byte_offset[16];
    uint8_t initialization_vector[256];
} cets_ecm_au_t;

typedef struct {
    uint8_t transport_scrambling_control;
    uint8_t num_au;
    cets_ecm_au_t* au;
} cets_ecm_state_t;

typedef struct {
    bool next_key_id_flag;
    uint8_t iv_size;
    uint8_t default_key_id[16];
    cets_ecm_state_t states[4];
    uint8_t num_states;
    uint8_t countdown_sec;
    uint8_t next_key_id[16];
} cets_ecm_t;

cets_ecm_t* cets_ecm_read(uint8_t* data, size_t len);
void cets_ecm_free(cets_ecm_t*);
void cets_ecm_print(const cets_ecm_t*);

#endif
