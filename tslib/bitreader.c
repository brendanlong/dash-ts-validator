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

#include <glib.h>
#include <stdlib.h>
#include <string.h>

static void bitreader_set_error(bitreader_t* b)
{
    if (b) {
        b->error = true;
    }
}

bitreader_t* bitreader_new(const uint8_t* data, size_t len)
{
    bitreader_t* b = g_slice_new(bitreader_t);
    bitreader_init(b, data, len);
    return b;
}

void bitreader_init(bitreader_t* b, const uint8_t* data, size_t len)
{
    b->data = data;
    b->len = len;
    b->bytes_read = 0;
    b->bits_read = 0;
    b->error = false;
}

void bitreader_free(bitreader_t* b)
{
    g_slice_free(bitreader_t, b);
}

bool bitreader_eof(const bitreader_t* b)
{
    return bitreader_bits_left(b) == 0;
}

size_t bitreader_bits_left(const bitreader_t* b)
{
    if (!b || !b->data || b->bytes_read >= b->len) {
        return 0;
    }
    return (b->len - b->bytes_read) * 8 - b->bits_read;
}

size_t bitreader_bytes_left(const bitreader_t* b)
{
    if (!b || !b->data || b->bytes_read >= b->len) {
        return 0;
    }
    return b->len - b->bytes_read - (b->bits_read ? 1 : 0);
}

uint8_t bitreader_peek_bit(bitreader_t* b, size_t bit_offset)
{
    if (bitreader_eof(b)) {
        goto fail;
    }
    return (b->data[b->bytes_read] >> (7 - b->bits_read)) & 1;
fail:
    bitreader_set_error(b);
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
        goto fail;
    }
    return result;
fail:
    bitreader_set_error(b);
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
    bitreader_set_error(b);
}

void bitreader_skip_bytes(bitreader_t* b, size_t bytes)
{
    if (!b) {
        return;
    }
    if (bitreader_eof(b) || b->bytes_read + bytes > b->len) {
        goto fail;
    }
    b->bytes_read += bytes;
    return;
fail:
    bitreader_set_error(b);
}

uint64_t bitreader_peek_bits(bitreader_t* b, size_t bits, size_t bits_offset)
{
    if (bitreader_eof(b) || bits > 64) {
        goto fail;
    }
    uint64_t result = 0;
    while (bits >= 8) {
        result = (result << 8) + bitreader_read_uint8(b);
        bits -= 8;
    }
    for (size_t i = 0; i < bits; ++i) {
        result <<= 1;
        result += bitreader_peek_bit(b, bits_offset + i);
    }
    return result;
fail:
    bitreader_set_error(b);
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
    bitreader_set_error(b);
    return 0;
}

void bitreader_read_bytes(bitreader_t* b, uint8_t* bytes_out, size_t bytes_len)
{
    if (bytes_len == 0) {
        return;
    }
    if (bitreader_eof(b) || bytes_len > bitreader_bytes_left(b)) {
        goto fail;
    }
    if (!b->bits_read) {
        memcpy(bytes_out, b->data + b->bytes_read, bytes_len);
        b->bytes_read += bytes_len;
    } else {
        for (size_t i = 0; i < bytes_len; ++i) {
            bytes_out[i] = bitreader_read_uint8(b);
        }
    }
    return;
fail:
    bitreader_set_error(b);
}

bitreader_t* bitreader_read_bytes_as_bitreader(bitreader_t* b, size_t bytes_len)
{
    if (bitreader_eof(b) || bitreader_bytes_left(b) < bytes_len) {
        goto fail;
    }
    bitreader_t* sub = bitreader_new(b->data + b->bytes_read, bytes_len);
    sub->bits_read = b->bits_read;
    b->bytes_read += bytes_len;
    return sub;
fail:
    bitreader_set_error(b);
    return NULL;
}

uint64_t bitreader_peek_uint(bitreader_t* b, size_t bits, size_t bits_offset)
{
    if (bitreader_eof(b) || bits > 64) {
        goto fail;
    }
    uint64_t result;
    if (!b->bits_read && !(bits % 8) && !(bits_offset % 8)) {
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
    bitreader_set_error(b);
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
    bitreader_set_error(b);
    return 0;
}

uint8_t bitreader_read_uint8(bitreader_t* b)
{
    return (uint8_t)bitreader_read_uint(b, 8);
}

uint8_t bitreader_peek_uint8(bitreader_t* b, size_t bits_offset)
{
    if (bits_offset > SIZE_MAX / 8) {
        goto fail;
    }
    return (uint8_t)bitreader_peek_uint(b, 8, bits_offset * 8);
fail:
    bitreader_set_error(b);
    return 0;
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
    if (bitreader_eof(b)) {
        goto fail;
    }
    uint64_t v = bitreader_read_bits(b, 3) << 30;
    bitreader_skip_bit(b);
    v |= bitreader_read_bits(b, 15) << 15;
    bitreader_skip_bit(b);
    v |= bitreader_read_bits(b, 15);
    bitreader_skip_bit(b);

    if (b->error) {
        goto fail;
    }
    return v;
fail:
    bitreader_set_error(b);
    return 0;
}

char* bitreader_read_string(bitreader_t* b, size_t* length_out) {
    char* str = NULL;
    if (bitreader_eof(b)) {
        goto fail;
    }
    size_t length;
    for (length = 0; bitreader_peek_uint8(b, length); ++length);
    ++length;

    str = malloc(length);
    for (size_t i = 0; i < length; ++i) {
        str[i] = bitreader_read_uint8(b);
    }
    if (b->error) {
        goto fail;
    }
    if (length_out) {
        *length_out = length;
    }
    return str;
fail:
    bitreader_set_error(b);
    free(str);
    return NULL;
}