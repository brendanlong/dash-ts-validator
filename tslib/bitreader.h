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
#ifndef TSLIB_BITREADER_H
#define TSLIB_BITREADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t* data;
    size_t len;
    size_t bytes_read;
    uint8_t bits_read;
    bool error;
} bitreader_t;

bitreader_t* bitreader_new(uint8_t* data, size_t len);
void bitreader_init(bitreader_t*, uint8_t* data, size_t len);
void bitreader_free(bitreader_t*);
bool bitreader_eof(const bitreader_t*);
size_t bitreader_bytes_left(const bitreader_t*);
uint8_t bitreader_read_bit(bitreader_t*);
void bitreader_skip_bits(bitreader_t*, size_t bits);
uint64_t bitreader_read_bits(bitreader_t*, size_t bits);
uint64_t bitreader_read_uint(bitreader_t*, size_t bytes);
uint8_t bitreader_read_uint8(bitreader_t*);
uint16_t bitreader_read_uint16(bitreader_t* b);
uint32_t bitreader_read_uint24(bitreader_t* b);
uint32_t bitreader_read_uint32(bitreader_t* b);
uint64_t bitreader_read_uint64(bitreader_t* b);

#endif