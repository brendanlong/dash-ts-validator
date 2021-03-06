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

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t bytes_read;
    uint8_t bits_read;
    bool error;
} bitreader_t;

#define bitreader_new_stack(name, data, len) \
bitreader_t _##name; \
bitreader_t *name = &_##name; \
bitreader_init(name, data, len);

static inline void bitreader_init(bitreader_t*, const uint8_t* data, size_t len);
static inline void bitreader_free(bitreader_t*);
static inline bool bitreader_eof(const bitreader_t*);
static inline size_t bitreader_bits_left(const bitreader_t*);
static inline size_t bitreader_bytes_left(const bitreader_t*);
static inline bool bitreader_read_bit(bitreader_t*);
static inline void bitreader_rewind_bytes(bitreader_t*, size_t bytes);
static inline void bitreader_skip_bit(bitreader_t*);
static inline void bitreader_skip_bits(bitreader_t*, size_t bits_len);
static inline void bitreader_skip_bytes(bitreader_t*, size_t bytes_len);
static inline void bitreader_read_bytes(bitreader_t*, uint8_t* bytes_out, size_t bytes_len);
static inline bitreader_t* bitreader_read_bytes_as_bitreader(bitreader_t*, size_t bytes_len);
static inline uint64_t bitreader_read_bits(bitreader_t*, uint8_t bits_len);
static inline uint8_t bitreader_read_uint8(bitreader_t*);
static inline uint16_t bitreader_read_uint16(bitreader_t*);
static inline uint32_t bitreader_read_uint24(bitreader_t*);
static inline uint32_t bitreader_read_uint32(bitreader_t*);
static inline uint64_t bitreader_read_uint48(bitreader_t*);
static inline uint64_t bitreader_read_uint64(bitreader_t*);

static inline uint64_t bitreader_read_90khz_timestamp(bitreader_t*, uint8_t skip_bits);
static inline uint64_t bitreader_read_pcr(bitreader_t*);
static inline char* bitreader_read_string(bitreader_t*, size_t* length);

/* private */

#define bitreader_check_overflow(b, bits) \
if (bitreader_would_overflow(b, bits)) { \
    bitreader_set_error(b); \
    return 0; \
}

#define bitreader_check_overflow_void(b, bits) \
if (bitreader_would_overflow(b, bits)) { \
    bitreader_set_error(b); \
    return; \
}

static inline bitreader_t* bitreader_new(const uint8_t* data, size_t len);

static inline void bitreader_set_error(bitreader_t*);
static inline bool bitreader_would_overflow(bitreader_t*, size_t bits);

static inline bitreader_t* bitreader_new(const uint8_t* data, size_t len)
{
    bitreader_t* b = g_slice_new(bitreader_t);
    bitreader_init(b, data, len);
    return b;
}

static inline void bitreader_init(bitreader_t* b, const uint8_t* data, size_t len)
{
    b->data = data;
    b->len = len;
    b->bytes_read = 0;
    b->bits_read = 0;
    b->error = false;
}

static inline void bitreader_free(bitreader_t* b)
{
    g_slice_free(bitreader_t, b);
}

static inline void bitreader_set_error(bitreader_t* b)
{
    if (b) {
        b->error = true;
    }
}

static inline bool bitreader_would_overflow(bitreader_t* b, size_t bits)
{
    return bitreader_bits_left(b) < bits;
}

static inline bool bitreader_eof(const bitreader_t* b)
{
    return !b || b->error || !b->data || b->bytes_read >= b->len;
}

static inline size_t bitreader_bits_left(const bitreader_t* b)
{
    if (bitreader_eof(b)) {
        return 0;
    }
    return (b->len - b->bytes_read) * 8 - b->bits_read;
}

static inline size_t bitreader_bytes_left(const bitreader_t* b)
{
    return bitreader_bits_left(b) / 8;
}

static inline void bitreader_skip_bits_unchecked(bitreader_t* b, size_t bits)
{
    size_t bits_read = b->bits_read + bits;
    b->bytes_read += bits_read / 8;
    b->bits_read = bits_read % 8;
}

static inline bool bitreader_read_bit(bitreader_t* b)
{
    bitreader_check_overflow(b, 1);
    uint8_t result = b->data[b->bytes_read];
    result &= 128 >> b->bits_read;
    bitreader_skip_bits_unchecked(b, 1);
    return result;
}

static inline void bitreader_skip_bit(bitreader_t* b)
{
    bitreader_check_overflow_void(b, 1);
    bitreader_skip_bits_unchecked(b, 1);
}

static inline void bitreader_skip_bits(bitreader_t* b, size_t bits)
{
    bitreader_check_overflow_void(b, bits);
    size_t bits_read = b->bits_read + bits;
    b->bytes_read += bits_read / 8;
    b->bits_read = bits_read % 8;
}

static inline void bitreader_skip_bytes(bitreader_t* b, size_t bytes)
{
    bitreader_check_overflow_void(b, bytes * 8);
    b->bytes_read += bytes;
}

static inline void bitreader_rewind_bytes(bitreader_t* b, size_t bytes)
{
    if (!b || b->bytes_read < bytes) {
        bitreader_set_error(b);
        return;
    }
    b->bytes_read -= bytes;
}

static inline uint64_t bitreader_read_bits_unaligned_unchecked(bitreader_t* b, uint8_t bits)
{
    uint8_t result = b->data[b->bytes_read];
    result >>= 8 - bits - b->bits_read;
    result &= (1 << bits) - 1;
    bitreader_skip_bits_unchecked(b, bits);
    return result;
}

static inline uint64_t bitreader_read_uint_aligned_unchecked(bitreader_t* b, uint8_t bytes)
{
    uint64_t result = b->data[b->bytes_read];
    ++b->bytes_read;
    for (size_t i = 1; i < bytes; ++i) {
        result <<= 8;
        result += b->data[b->bytes_read];
        ++b->bytes_read;
    }
    return result;
}

static inline uint64_t bitreader_read_bits_unchecked(bitreader_t* b, uint8_t bits)
{
    uint64_t result = 0;
    /* Try to turn this into an aligned read */
    if (b->bits_read && bits >= 8) {
        size_t to_read = 8 - b->bits_read;
        result = bitreader_read_bits_unaligned_unchecked(b, to_read);
        bits -= to_read;
    }

    /* Do aligned reads if we can */
    while (!b->bits_read && bits >= 8) {
        size_t to_read_bytes = bits / 8;
        result <<= to_read_bytes * 8;
        result += bitreader_read_uint_aligned_unchecked(b, to_read_bytes);
        bits -= to_read_bytes * 8;
    }

    /* Handle leftovers */
    while (bits > 0) {
        size_t to_read = MIN(bits, 8 - b->bits_read);
        result <<= to_read;
        result += bitreader_read_bits_unaligned_unchecked(b, to_read);
        bits -= to_read;
    }
    return result;
}

static inline uint64_t bitreader_read_bits(bitreader_t* b, uint8_t bits)
{
    bitreader_check_overflow(b, bits);
    if (bits > 64) {
        bitreader_set_error(b);
        return 0;
    }
    return bitreader_read_bits_unchecked(b, bits);
}

static inline void bitreader_read_bytes(bitreader_t* b, uint8_t* bytes_out, size_t bytes_len)
{
    if (bytes_len == 0) {
        return;
    }
    bitreader_check_overflow_void(b, bytes_len * 8);
    if (!b->bits_read) {
        memcpy(bytes_out, b->data + b->bytes_read, bytes_len);
        b->bytes_read += bytes_len;
    } else {
        for (size_t i = 0; i < bytes_len; ++i) {
            bytes_out[i] = bitreader_read_bits_unchecked(b, 8);
        }
    }
}

static inline bitreader_t* bitreader_read_bytes_as_bitreader(bitreader_t* b, size_t bytes_len)
{
    bitreader_check_overflow(b, bytes_len * 8);
    bitreader_t* sub = bitreader_new(b->data + b->bytes_read, bytes_len);
    sub->bits_read = b->bits_read;
    b->bytes_read += bytes_len;
    return sub;
}

static inline uint64_t bitreader_read_uint(bitreader_t* b, uint8_t bytes)
{
    bitreader_check_overflow(b, bytes * 8);
    if (!b->bits_read) {
        return bitreader_read_uint_aligned_unchecked(b, bytes);
    } else {
        return bitreader_read_bits_unchecked(b, bytes * 8);
    }
}

static inline uint8_t bitreader_read_uint8(bitreader_t* b)
{
    return (uint8_t)bitreader_read_uint(b, 1);
}

static inline uint16_t bitreader_read_uint16(bitreader_t* b)
{
    return (uint16_t)bitreader_read_uint(b, 2);
}

static inline uint32_t bitreader_read_uint24(bitreader_t* b)
{
    return (uint32_t)bitreader_read_uint(b, 3);
}

static inline uint32_t bitreader_read_uint32(bitreader_t* b)
{
    return (uint32_t)bitreader_read_uint(b, 4);
}

static inline uint64_t bitreader_read_uint48(bitreader_t* b)
{
    return bitreader_read_uint(b, 6);
}

static inline uint64_t bitreader_read_uint64(bitreader_t* b)
{
    return bitreader_read_uint(b, 8);
}

static inline uint64_t bitreader_read_90khz_timestamp(bitreader_t* b, uint8_t skip_bits)
{
    if (bitreader_would_overflow(b, 36 + skip_bits)) {
        goto fail;
    }
    if (skip_bits) {
        bitreader_skip_bits(b, skip_bits);
    }
    uint64_t v = bitreader_read_bits_unchecked(b, 3) << 30;
    bitreader_skip_bits_unchecked(b, 1);
    v |= bitreader_read_bits_unchecked(b, 15) << 15;
    bitreader_skip_bits_unchecked(b, 1);
    v |= bitreader_read_bits_unchecked(b, 15);
    bitreader_skip_bits_unchecked(b, 1);
    return v;
fail:
    bitreader_set_error(b);
    return 0;
}

static inline uint64_t bitreader_read_pcr(bitreader_t* b)
{
    if (bitreader_would_overflow(b, 48)) {
        goto fail;
    }
    uint64_t result = 300 * bitreader_read_bits_unchecked(b, 33);
    bitreader_skip_bits_unchecked(b, 6);
    result += bitreader_read_bits_unchecked(b, 9);
    return result;
fail:
    bitreader_set_error(b);
    return 0;
}

static inline char* bitreader_read_string(bitreader_t* b, size_t* length_out) {
    size_t bytes_left = bitreader_bytes_left(b);
    if (bytes_left == 0) {
        goto fail;
    }
    size_t length;
    for (length = 0; length < bytes_left && bitreader_read_bits_unchecked(b, 8); ++length);
    if (b->error || length >= bytes_left) {
        goto fail;
    }
    ++length;

    bitreader_rewind_bytes(b, length);

    char *str = malloc(length);
    bitreader_read_bytes(b, (uint8_t*)str, length);
    if (length_out) {
        /* Don't include the NUL terminator */
        *length_out = length - 1;
    }
    return str;
fail:
    if (length_out) {
        *length_out = 0;
    }
    bitreader_set_error(b);
    return NULL;
}

#endif