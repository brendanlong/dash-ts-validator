/*
 Copyright (c) 2012-, ISO/IEC JTC1/SC29/WG11
 Written by Alex Giladi <alex.giladi@gmail.com> and Vlad Zbarsky <zbarsky@cornell.edu>
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
#include "ts.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "bitreader.h"
#include "log.h"


static ts_packet_t* ts_new(uint8_t* buf, size_t buf_len);
static bool ts_read_adaptation_field(ts_adaptation_field_t*, bitreader_t*);

static void ts_print_adaptation_field(const ts_adaptation_field_t*);


ts_packet_t* ts_new(uint8_t* buf, size_t buf_size)
{
    g_return_val_if_fail(buf, NULL);
    g_return_val_if_fail(buf_size >= TS_SIZE, NULL);

    ts_packet_t* ts = g_slice_new0(ts_packet_t);
    ts->pcr_int = PCR_INVALID; // invalidate interpolated PCR
    memcpy(ts->bytes, buf, TS_SIZE);
    return ts;
}

ts_packet_t* ts_copy(const ts_packet_t* original)
{
    if (!original) {
        return NULL;
    }
    ts_packet_t* ts = g_slice_dup(ts_packet_t, original);
    ts->payload = g_slice_copy(original->payload_len, original->payload);
    ts->adaptation_field.private_data = g_slice_copy(ts->adaptation_field.private_data_len,
            original->adaptation_field.private_data);
    return ts;
}

void ts_free(ts_packet_t* ts)
{
    if (ts == NULL) {
        return;
    }
    g_slice_free1(ts->payload_len, ts->payload);
    g_slice_free1(ts->adaptation_field.private_data_len, ts->adaptation_field.private_data);
    g_slice_free(ts_packet_t, ts);
}

bool ts_read_adaptation_field(ts_adaptation_field_t* af, bitreader_t* b)
{
    g_return_val_if_fail(af, NULL);
    g_return_val_if_fail(b, NULL);

    af->length = bitreader_read_uint8(b);
    size_t start_pos = b->bytes_read;

    if (af->length > 0) {
        af->discontinuity_indicator = bitreader_read_bit(b);
        af->random_access_indicator = bitreader_read_bit(b);
        af->elementary_stream_priority_indicator = bitreader_read_bit(b);
        af->pcr_flag = bitreader_read_bit(b);
        af->opcr_flag = bitreader_read_bit(b);
        af->splicing_point_flag = bitreader_read_bit(b);
        af->private_data_flag = bitreader_read_bit(b);
        af->extension_flag = bitreader_read_bit(b);

        if (af->length > 1) {
            if(af->pcr_flag) {
                af->program_clock_reference_base = bitreader_read_bits(b, 33);
                bitreader_skip_bits(b, 6);
                af->program_clock_reference_extension = bitreader_read_bits(b, 9);
            }
            if (af->opcr_flag) {
                af->original_program_clock_reference_base = bitreader_read_bits(b, 33);
                bitreader_skip_bits(b, 6);
                af->original_program_clock_reference_extension = bitreader_read_bits(b, 9);
            }
            if (af->splicing_point_flag) {
                af->splice_countdown = bitreader_read_uint8(b); //TODO: it's signed, two's compliment #
            }

            if (af->private_data_flag) {
                af->private_data_len = bitreader_read_uint8(b);

                if(af->private_data_len > 0) {
                    af->private_data = g_slice_alloc(af->private_data_len);
                    bitreader_read_bytes(b, af->private_data, af->private_data_len);
                }
            }

            if (af->extension_flag) {
                af->extension_length = bitreader_read_uint8(b);
                size_t afe_start_pos = b->bytes_read;

                af->ltw_flag = bitreader_read_bit(b);
                af->piecewise_rate_flag = bitreader_read_bit(b);
                af->seamless_splice_flag = bitreader_read_bit(b);
                bitreader_skip_bits(b, 5);

                if (af->ltw_flag) {
                    af->ltw_valid_flag = bitreader_read_bit(b);
                    af->ltw_offset = bitreader_read_bits(b, 15);
                }
                if (af->piecewise_rate_flag) {
                    bitreader_skip_bits(b, 2);
                    af->piecewise_rate = bitreader_read_bits(b, 22);
                }
                if (af->seamless_splice_flag) {
                    bitreader_skip_bits(b, 4);
                    af->dts_next_au = bitreader_read_90khz_timestamp(b);
                }

                size_t res_len = af->extension_length + b->bytes_read - afe_start_pos;
                if (res_len > 0) {
                    bitreader_skip_bytes(b, res_len);
                }
            }
        }

        while (b->bytes_read < (start_pos + af->length)) {
            uint8_t stuffing_byte = bitreader_read_uint8(b);
            if (stuffing_byte != 0xFF) {
                g_critical("In adaptation field, read stuffing byte with value 0x%02x, but stuffing bytes should "
                        "have the value 0xFF.", stuffing_byte);
                return false;
            }
        }
    }

    if (b->error) {
        g_critical("Something is wrong with this TS packet's length.");
    }
    return !b->error;
}

ts_packet_t* ts_read(uint8_t* buf, size_t buf_size, uint64_t packet_num)
{
    g_return_val_if_fail(buf, NULL);
    if (buf_size < TS_SIZE) {
        g_critical("TS packet buffer should be %d bytes, but is %zu bytes.", TS_SIZE, buf_size);
        return NULL;
    }

    ts_packet_t* ts = ts_new(buf, buf_size);
    ts->pos_in_stream = packet_num * TS_SIZE;
    bitreader_t* b = bitreader_new(ts->bytes, TS_SIZE);

    uint8_t sync_byte = bitreader_read_uint8(b);
    if (sync_byte != TS_SYNC_BYTE) {
        g_critical("Got 0x%02X instead of expected sync byte 0x%02X", sync_byte, TS_SYNC_BYTE);
        goto fail;
    }

    ts->transport_error_indicator = bitreader_read_bit(b);
    if (ts->transport_error_indicator) {
        g_warning("At least one uncorrectable bit error exists in this TS packet");
    }

    ts->payload_unit_start_indicator = bitreader_read_bit(b);
    ts->transport_priority = bitreader_read_bit(b);
    ts->pid = bitreader_read_bits(b, 13);

    ts->transport_scrambling_control = bitreader_read_bits(b, 2);
    ts->has_adaptation_field = bitreader_read_bit(b);
    ts->has_payload = bitreader_read_bit(b);
    ts->continuity_counter = bitreader_read_bits(b, 4);

    if (ts->has_adaptation_field) {
        memset(&(ts->adaptation_field), 0, sizeof(ts->adaptation_field));
        if (!ts_read_adaptation_field(&(ts->adaptation_field), b)) {
            goto fail;
        }
    }

    if (ts->has_payload) {
        ts->payload_len = TS_SIZE - b->bytes_read;
        ts->payload = g_slice_alloc(ts->payload_len);
        bitreader_read_bytes(b, ts->payload, ts->payload_len);
    }

    if (b->error) {
        g_critical("Something is wrong with this TS packet's length.");
        goto fail;
    }

cleanup:
    bitreader_free(b);
    return ts;
fail:
    ts_free(ts);
    ts = NULL;
    goto cleanup;
}

void ts_print_adaptation_field(const ts_adaptation_field_t* af)
{
    g_return_if_fail(af);

    SKIT_LOG_UINT_DBG(1, af->length);

    if (af->length > 0) {
        SKIT_LOG_UINT_DBG(1, af->discontinuity_indicator);
        SKIT_LOG_UINT_DBG(1, af->random_access_indicator);
        SKIT_LOG_UINT_DBG(1, af->elementary_stream_priority_indicator);
        SKIT_LOG_UINT_DBG(1, af->pcr_flag);
        SKIT_LOG_UINT_DBG(1, af->opcr_flag);
        SKIT_LOG_UINT_DBG(1, af->splicing_point_flag);
        SKIT_LOG_UINT_DBG(1, af->private_data_flag);
        SKIT_LOG_UINT_DBG(1, af->extension_flag);

        if (af->length > 1) {
            if (af->pcr_flag) {
                SKIT_LOG_UINT_DBG(2, af->program_clock_reference_base);
                SKIT_LOG_UINT_DBG(2, af->program_clock_reference_extension);
            }
            if (af->opcr_flag) {
                SKIT_LOG_UINT_DBG(2, af->original_program_clock_reference_base);
                SKIT_LOG_UINT_DBG(2, af->original_program_clock_reference_extension);
            }
            if (af->splicing_point_flag) {
                SKIT_LOG_UINT_DBG(2, af->splice_countdown);
            }

            if (af->private_data_flag) {
                SKIT_LOG_UINT_DBG(2, af->private_data_len);
                // TODO print transport_private_data, if any
            }

            if (af->extension_flag) {
                SKIT_LOG_UINT_DBG(2, af->extension_length);
                SKIT_LOG_UINT_DBG(2, af->ltw_flag);
                SKIT_LOG_UINT_DBG(2, af->piecewise_rate_flag);
                SKIT_LOG_UINT_DBG(2, af->seamless_splice_flag);

                if (af->ltw_flag) {
                    SKIT_LOG_UINT_DBG(3, af->ltw_valid_flag);
                    SKIT_LOG_UINT_DBG(3, af->ltw_offset);
                }
                if (af->piecewise_rate_flag) {
                    // here go reserved 2 bits
                    SKIT_LOG_UINT_DBG(3, af->piecewise_rate);
                }
                if (af->seamless_splice_flag) {
                    SKIT_LOG_UINT_DBG(3, af->splice_type);
                    SKIT_LOG_UINT_DBG(3, af->dts_next_au);
                }
            }
        }
    }
}

void ts_print(const ts_packet_t* ts)
{
    g_return_if_fail(ts);
    if (tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) {
        return;
    }

    SKIT_LOG_UINT_DBG(0, ts->transport_error_indicator);
    SKIT_LOG_UINT_DBG(0, ts->payload_unit_start_indicator);
    SKIT_LOG_UINT_DBG(0, ts->transport_priority);
    SKIT_LOG_UINT_HEX_DBG(0, ts->pid);

    SKIT_LOG_UINT_DBG(0, ts->transport_scrambling_control);
    SKIT_LOG_UINT_DBG(0, ts->continuity_counter);

    if (ts->has_adaptation_field) {
        ts_print_adaptation_field(&ts->adaptation_field);
    }
    SKIT_LOG_UINT64_DBG("", (uint64_t)ts->payload_len);
}

int64_t ts_read_pcr(const ts_packet_t* ts)
{
    g_return_val_if_fail(ts, 0);

    if (ts->has_adaptation_field && ts->adaptation_field.pcr_flag) {
        uint64_t pcr = 300 * ts->adaptation_field.program_clock_reference_base;
        pcr += ts->adaptation_field.program_clock_reference_extension;
        return pcr;
    } else {
        return PCR_INVALID;
    }
}