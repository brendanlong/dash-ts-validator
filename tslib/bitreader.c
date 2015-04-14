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
#include "bitreader.h"

#include <assert.h>
#include <stdlib.h>

bitreader_t* bitreader_new(const uint8_t* data, size_t len)
{
    if (!data) {
        return NULL;
    }
    bitreader_t* b = malloc(sizeof(*b));
    bitreader_init(b, data, len);
    return b;
}

void bitreader_init(bitreader_t* b, const uint8_t* data, size_t len)
{
    assert(data);
    b->data = data;
    b->len = len;
    b->bytes_read = 0;
    b->bits_read = 0;
    b->error = false;
}

void bitreader_free(bitreader_t* b)
{
    free(b);
}

bool bitreader_eof(const bitreader_t* b)
{
    return bitreader_bytes_left(b) == 0;
}

size_t bitreader_bytes_left(const bitreader_t* b)
{
    if (!b || !b->data || b->bytes_read > b->len) {
        return 0;
    }
    return b->len - b->bytes_read;
}

uint8_t bitreader_peek_bit(bitreader_t* b, size_t bit_offset)
{
    if (bitreader_eof(b)) {
        goto fail;
    }
    return (b->data[b->bytes_read] >> (7 - b->bits_read)) & 1;
fail:
    b->error = true;
    return 0;
}

uint8_t bitreader_read_bit(bitreader_t* b)
{
    if (bitreader_eof(b)) {
        goto fail;
    }
    bool result = bitreader_peek_bit(b, 0);
    ++b->bits_read;
    if (b->bits_read == 8) {
        ++b->bytes_read;
        b->bits_read = 0;
    }
    if (b->bytes_read > b->len || (b->bits_read > 0 && b->bytes_read >= b->len)) {
        b->error = true;
    }
    return result;
fail:
    b->error = true;
    return 0;
}

void bitreader_skip_bit(bitreader_t* b)
{
    bitreader_skip_bits(b, 1);
}

void bitreader_skip_bits(bitreader_t* b, size_t bits)
{
    if (bitreader_eof(b)) {
        goto fail;
    }
    b->bytes_read += bits / 8;
    b->bits_read += bits % 8;
    if (b->bits_read >= 8) {
        ++b->bytes_read;
        b->bits_read -= 8;
    }
    if (b->bytes_read > b->len || (b->bits_read > 0 && b->bytes_read >= b->len)) {
        goto fail;
    }
    return;
fail:
    b->error = true;
}

void bitreader_skip_bytes(bitreader_t* b, size_t bytes)
{
    /* TODO: Optimize */
    bitreader_skip_bits(b, bytes * 8);
}

uint64_t bitreader_peek_bits(bitreader_t* b, size_t bits, size_t bits_offset)
{
    if (bitreader_eof(b) || bits > 64) {
        goto fail;
    }
    uint64_t result = 0;
    for (size_t i = 0; i < bits; ++i) {
        result <<= 1;
        result += bitreader_peek_bit(b, bits_offset + i);
    }
    return result;
fail:
    b->error = true;
    return 0;
}

uint64_t bitreader_read_bits(bitreader_t* b, size_t bits)
{
    if (bitreader_eof(b) || bits > 64) {
        goto fail;
    }
    uint64_t result = 0;
    for (size_t i = 0; i < bits; ++i) {
        result <<= 1;
        result += bitreader_read_bit(b);
    }
    return result;
fail:
    b->error = true;
    return 0;
}

void bitreader_read_bytes(bitreader_t* b, uint8_t* bytes_out, size_t bytes_len)
{
    /* TODO: Optimize */
    for (size_t i = 0; i < bytes_len; ++i) {
        bytes_out[i] = bitreader_read_uint8(b);
    }
}

uint64_t bitreader_peek_uint(bitreader_t* b, size_t bits, size_t bits_offset)
{
    if (bitreader_eof(b) || bits > 64) {
        goto fail;
    }
    uint64_t result;
    if (!b->bits_read && !(bits % 8) && !(bits_offset %8)) {
        size_t bytes = bits / 8;
        size_t bytes_offset = bits_offset / 8;
        if (b->bytes_read + bytes + bytes_offset > b->len) {
            goto fail;
        }
        result = 0;
        for (size_t i = 0; i < bytes; ++i) {
            result <<= 8;
            result += b->data[bytes_offset + b->bytes_read + i];
        }
    } else {
        result = bitreader_peek_bits(b, bits, bits_offset);
    }
    return result;
fail:
    b->error = true;
    return 0;
}

uint64_t bitreader_read_uint(bitreader_t* b, size_t bits)
{
    if (bitreader_eof(b) || bits > 64) {
        goto fail;
    }
    uint64_t result;
    if (!b->bits_read && !(bits % 8)) {
        result = bitreader_peek_uint(b, bits, 0);
        b->bytes_read += bits / 8;
    } else {
        result = bitreader_read_bits(b, bits);
    }
    return result;
fail:
    b->error = true;
    return 0;
}

uint8_t bitreader_read_uint8(bitreader_t* b)
{
    return (uint8_t)bitreader_read_uint(b, 8);
}

uint8_t bitreader_peek_uint8(bitreader_t* b, size_t bits_offset)
{
    if (bits_offset > SIZE_MAX / 8) {
        b->error = true;
        return 0;
    }
    return (uint8_t)bitreader_peek_uint(b, 8, bits_offset * 8);
}

uint16_t bitreader_read_uint16(bitreader_t* b)
{
    return (uint16_t)bitreader_read_uint(b, 16);
}

uint32_t bitreader_read_uint24(bitreader_t* b)
{
    return (uint32_t)bitreader_read_uint(b, 24);
}

uint32_t bitreader_read_uint32(bitreader_t* b)
{
    return (uint32_t)bitreader_read_uint(b, 32);
}

uint64_t bitreader_read_uint48(bitreader_t* b)
{
    return bitreader_read_uint(b, 48);
}

uint64_t bitreader_read_uint64(bitreader_t* b)
{
    return bitreader_read_uint(b, 64);
}

uint64_t bitreader_read_90khz_timestamp(bitreader_t* b)
{
    uint64_t v = bitreader_read_bits(b, 3) << 30;
    bitreader_skip_bit(b);
    v |= bitreader_read_bits(b, 15) << 15;
    bitreader_skip_bit(b);
    v |= bitreader_read_bits(b, 15);
    bitreader_skip_bit(b);

    return v;
}

char* bitreader_read_string(bitreader_t* b, size_t* length_out) {
    size_t length;
    for (length = 0; bitreader_peek_uint8(b, length); ++length);
    ++length;

    char* str = malloc(length);
    for (size_t i = 0; i < length; ++i) {
        str[i] = bitreader_read_uint8(b);
    }
    if (length_out) {
        *length_out = length;
    }
    return str;
}