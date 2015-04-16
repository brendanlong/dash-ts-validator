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


static bool ts_read_adaptation_field(ts_adaptation_field_t*, bitreader_t*);
static void ts_print_adaptation_field(const ts_adaptation_field_t*);

static bool ts_init(ts_packet_t* ts)
{
    g_return_val_if_fail(ts, false);

    memset(ts, 0, sizeof(*ts));
    ts->pcr_int = PCR_INVALID; // invalidate interpolated PCR
    ts->adaptation_field.program_clock_reference = PCR_INVALID;
    ts->adaptation_field.original_program_clock_reference = PCR_INVALID;
    return true;
}

void ts_copy(ts_packet_t* new_ts, const ts_packet_t* original)
{
    if (!original) {
        ts_init(new_ts);
        return;
    }
    memcpy(new_ts, original, sizeof(*new_ts));
}

bool ts_read_adaptation_field(ts_adaptation_field_t* af, bitreader_t* b)
{
    g_return_val_if_fail(af, NULL);
    g_return_val_if_fail(b, NULL);

    af->length = bitreader_read_uint8(b);
    size_t start_pos = b->bytes_read;

    if (af->length > 0) {
        uint8_t tmp = bitreader_read_uint8(b);
        af->discontinuity_indicator = tmp & 128;
        af->random_access_indicator = tmp & 64;
        af->elementary_stream_priority_indicator = tmp & 32;
        af->pcr_flag = tmp & 16;
        af->opcr_flag = tmp & 8;
        af->splicing_point_flag = tmp & 4;
        af->private_data_flag = tmp & 2;
        af->extension_flag = tmp & 1;

        if (af->length > 1) {
            if(af->pcr_flag) {
                af->program_clock_reference = bitreader_read_pcr(b);
            }
            if (af->opcr_flag) {
                af->original_program_clock_reference = bitreader_read_pcr(b);
            }
            if (af->splicing_point_flag) {
                af->splice_countdown = bitreader_read_uint8(b); //TODO: it's signed, two's compliment #
            }

            if (af->private_data_flag) {
                af->private_data_len = bitreader_read_uint8(b);

                if(af->private_data_len > 0) {
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
                    af->dts_next_au = bitreader_read_90khz_timestamp(b, 4);
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

bool ts_read(ts_packet_t* ts, uint8_t* buf, size_t buf_size, uint64_t packet_num)
{
    g_return_val_if_fail(buf, false);
    if (buf_size < TS_SIZE) {
        g_critical("TS packet buffer should be %d bytes, but is %zu bytes.", TS_SIZE, buf_size);
        return false;
    }

    ts_init(ts);
    ts->pos_in_stream = packet_num * TS_SIZE;
    bitreader_new_stack(b, buf, TS_SIZE);

    uint8_t sync_byte = bitreader_read_uint8(b);
    if (sync_byte != TS_SYNC_BYTE) {
        g_critical("Got 0x%02X instead of expected sync byte 0x%02X", sync_byte, TS_SYNC_BYTE);
        goto fail;
    }

    /* micro-optimized because 25% of execution time was in the next few lines */
    uint16_t tmp = bitreader_read_uint16(b);
    ts->transport_error_indicator = tmp & (1 << 15);
    if (ts->transport_error_indicator) {
        g_warning("At least one uncorrectable bit error exists in this TS packet");
    }

    ts->payload_unit_start_indicator = tmp & (1 << 14);
    ts->transport_priority = tmp & (1 << 13);
    ts->pid = tmp & ((1 << 13) - 1);

    tmp = bitreader_read_uint8(b);
    ts->transport_scrambling_control = tmp >> 6;
    ts->has_adaptation_field = tmp & 32;
    ts->has_payload = tmp & 16;
    ts->continuity_counter = tmp & 15;
    /* end micro-optimization */

    if (ts->has_adaptation_field && !ts_read_adaptation_field(&(ts->adaptation_field), b)) {
        goto fail;
    }

    if (ts->has_payload) {
        ts->payload_len = TS_SIZE - b->bytes_read;
        bitreader_read_bytes(b, ts->payload, ts->payload_len);
    }

    if (b->error) {
        g_critical("Something is wrong with this TS packet's length.");
        goto fail;
    }

    return true;
fail:
    return false;
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
                SKIT_LOG_UINT_DBG(2, af->program_clock_reference);
            }
            if (af->opcr_flag) {
                SKIT_LOG_UINT_DBG(2, af->original_program_clock_reference);
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