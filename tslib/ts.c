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
#include <stddef.h>
#include <string.h>
#include <inttypes.h>

#include "libts_common.h"
#include "log.h"


static int ts_read_header(ts_header_t* tsh, bs_t* b);
static int ts_read_adaptation_field(ts_adaptation_field_t* af, bs_t* b);

static void ts_print_adaptation_field(const ts_adaptation_field_t* const af);
static void ts_print_header(const ts_header_t* const tsh);


volatile int tslib_errno = 0;


ts_packet_t* ts_new(void)
{
    ts_packet_t* ts = calloc(1, sizeof(ts_packet_t));
    ts->pcr_int = UINT64_MAX; // invalidate interpolated PCR
    return ts;
}

ts_packet_t* ts_copy(ts_packet_t* original)
{
    ts_packet_t* ts = g_memdup(original, sizeof(*original));
    ts->payload.bytes = g_memdup(original->payload.bytes, original->payload.len);
    ts->adaptation_field.private_data_bytes.bytes = g_memdup(original->adaptation_field.private_data_bytes.bytes,
            original->adaptation_field.private_data_bytes.len);
    return ts;
}

void ts_free(ts_packet_t* ts)
{
    if (ts == NULL) {
        return;
    }
    free(ts->payload.bytes);
    free(ts->adaptation_field.private_data_bytes.bytes);
    free(ts);
}

int ts_read_header(ts_header_t* tsh, bs_t* b)
{
    int start_pos = bs_pos(b);

    uint8_t sync_byte = bs_read_u8(b);
    if (sync_byte != TS_SYNC_BYTE) {
        g_critical("Got 0x%02X instead of expected sync byte 0x%02X", sync_byte, TS_SYNC_BYTE);
        return TS_ERROR_NO_SYNC_BYTE;
    }

    tsh->transport_error_indicator = bs_read_u1(b);
    if (tsh->transport_error_indicator) {
        g_warning("At least one uncorrectable bit error exists in this TS packet");
    }

    tsh->payload_unit_start_indicator = bs_read_u1(b);
    tsh->transport_priority = bs_read_u1(b);
    tsh->pid = bs_read_u(b, 13);

    tsh->transport_scrambling_control = bs_read_u(b, 2);
    tsh->adaptation_field_control = bs_read_u(b, 2);
    tsh->continuity_counter = bs_read_u(b, 4);

    return bs_pos(b) - start_pos;
}

int ts_read_adaptation_field(ts_adaptation_field_t* af, bs_t* b)
{
    af->adaptation_field_length = bs_read_u8(b);
    int start_pos = bs_pos(b);

    if (af->adaptation_field_length > 0) {
        af->discontinuity_indicator = bs_read_u1(b);
        af->random_access_indicator = bs_read_u1(b);
        af->elementary_stream_priority_indicator = bs_read_u1(b);
        af->pcr_flag = bs_read_u1(b);
        af->opcr_flag = bs_read_u1(b);
        af->splicing_point_flag = bs_read_u1(b);
        af->transport_private_data_flag = bs_read_u1(b);
        af->adaptation_field_extension_flag = bs_read_u1(b);

        if (af->adaptation_field_length > 1) {
            if(af->pcr_flag) {
                af->program_clock_reference_base = bs_read_ull(b, 33);
                bs_skip_u(b, 6);
                af->program_clock_reference_extension = bs_read_u(b, 9);
            }
            if (af->opcr_flag) {
                af->original_program_clock_reference_base = bs_read_ull(b, 33);
                bs_skip_u(b, 6);
                af->original_program_clock_reference_extension = bs_read_u(b, 9);
            }
            if (af->splicing_point_flag) {
                af->splice_countdown = bs_read_u8(b); //TODO: it's signed, two's compliment #
            }

            if (af->transport_private_data_flag) {
                af->transport_private_data_length = bs_read_u8(b);

                if(af->transport_private_data_length > 0) {
                    af->private_data_bytes.len = af->transport_private_data_length;
                    af->private_data_bytes.bytes = malloc(af->private_data_bytes.len);
                    bs_read_bytes(b, af->private_data_bytes.bytes, af->transport_private_data_length);
                }
            }

            if (af->adaptation_field_extension_flag) {
                af->adaptation_field_extension_length = bs_read_u8(b);
                int afe_start_pos = bs_pos(b);

                af->ltw_flag = bs_read_u1(b);
                af->piecewise_rate_flag = bs_read_u1(b);
                af->seamless_splice_flag = bs_read_u1(b);
                bs_skip_u(b, 5);

                if (af->ltw_flag) {
                    af->ltw_valid_flag = bs_read_u1(b);
                    af->ltw_offset = bs_read_u(b, 15);
                }
                if (af->piecewise_rate_flag) {
                    bs_skip_u(b, 2);
                    af->piecewise_rate = bs_read_u(b, 22);
                }
                if (af->seamless_splice_flag) {
                    af->splice_type = bs_read_u(b, 4);
                    af->dts_next_au = bs_read_90khz_timestamp(b);

                }

                int res_len = af->adaptation_field_extension_length + bs_pos(b) - afe_start_pos;
                if (res_len > 0) {
                    bs_skip_bytes(b, res_len);
                }

            }
        }

        int stuffing_bytes_len = af->adaptation_field_length - (bs_pos(b) - start_pos);
        while (stuffing_bytes_len > 0) {
            uint8_t stuffing_byte = bs_read_u8(b);
            if (stuffing_byte != 0xFF) {
                g_critical("In adaptation field, read stuffing byte with value 0x%02x, but stuffing bytes should "
                        "have the value 0xFF.", stuffing_byte);
                return -1;
            }
            --stuffing_bytes_len;
        }
    }

    return 1 + bs_pos(b) - start_pos;
}

int ts_read(ts_packet_t* ts, uint8_t* buf, size_t buf_size, uint64_t packet_num)
{
    if (buf_size < TS_SIZE) {
        SAFE_REPORT_TS_ERR(-1);
        return TS_ERROR_NOT_ENOUGH_DATA;
    }

    memcpy(ts->bytes, buf, TS_SIZE);

    bs_t b;
    bs_init(&b, buf, TS_SIZE);
    memset(&(ts->header), 0, sizeof(ts->header));

    int res = ts_read_header(&(ts->header), &b);
    if (res < 1) {
        SAFE_REPORT_TS_ERR(-2);
        return res;
    }

    if (ts->header.adaptation_field_control & TS_ADAPTATION_FIELD) {
        memset(&(ts->adaptation_field), 0, sizeof(ts->adaptation_field));
        res = ts_read_adaptation_field(&(ts->adaptation_field), &b);
        if (res < 1) {
            SAFE_REPORT_TS_ERR(-3);
            return res;
        }
    }

    if (ts->header.adaptation_field_control & TS_PAYLOAD) {
        ts->pos_in_stream = packet_num * TS_SIZE;
        ts->payload.len = TS_SIZE - bs_pos(&b);
        ts->payload.bytes = malloc(ts->payload.len);
        bs_read_bytes(&b, ts->payload.bytes, ts->payload.len);
    }

    // TODO read and interpret pointer field

    return bs_pos(&b);
}

void ts_print_header(const ts_header_t* const tsh)
{
    SKIT_LOG_UINT_DBG(0, tsh->transport_error_indicator);
    SKIT_LOG_UINT_DBG(0, tsh->payload_unit_start_indicator);
    SKIT_LOG_UINT_DBG(0, tsh->transport_priority);
    SKIT_LOG_UINT_HEX_DBG(0, tsh->pid);

    SKIT_LOG_UINT_DBG(0, tsh->transport_scrambling_control);
    SKIT_LOG_UINT_DBG(0, tsh->adaptation_field_control);
    SKIT_LOG_UINT_DBG(0, tsh->continuity_counter);
}

void ts_print_adaptation_field(const ts_adaptation_field_t* const af)
{
    SKIT_LOG_UINT_DBG(1, af->adaptation_field_length);

    if (af->adaptation_field_length > 0) {
        SKIT_LOG_UINT_DBG(1, af->discontinuity_indicator);
        SKIT_LOG_UINT_DBG(1, af->random_access_indicator);
        SKIT_LOG_UINT_DBG(1, af->elementary_stream_priority_indicator);
        SKIT_LOG_UINT_DBG(1, af->pcr_flag);
        SKIT_LOG_UINT_DBG(1, af->opcr_flag);
        SKIT_LOG_UINT_DBG(1, af->splicing_point_flag);
        SKIT_LOG_UINT_DBG(1, af->transport_private_data_flag);
        SKIT_LOG_UINT_DBG(1, af->adaptation_field_extension_flag);

        if (af->adaptation_field_length > 1) {
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

            if (af->transport_private_data_flag) {
                SKIT_LOG_UINT_DBG(2, af->transport_private_data_length);
                // TODO print transport_private_data, if any
            }

            if (af->adaptation_field_extension_flag) {
                SKIT_LOG_UINT_DBG(2, af->adaptation_field_extension_length);
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

void ts_print(const ts_packet_t* const ts)
{
    if (tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) {
        return;
    }

    ts_print_header(&ts->header);

    if (ts->header.adaptation_field_control & TS_ADAPTATION_FIELD) {
        ts_print_adaptation_field(&ts->adaptation_field);
    }
    SKIT_LOG_UINT64_DBG("", (uint64_t)ts->payload.len);
}

int64_t ts_read_pcr(const ts_packet_t* const ts)
{
    int64_t pcr = INT64_MAX;
    if (ts->header.adaptation_field_control & TS_ADAPTATION_FIELD) {
        if (ts->adaptation_field.pcr_flag) {
            pcr = 300 * ts->adaptation_field.program_clock_reference_base;
            pcr += ts->adaptation_field.program_clock_reference_extension;
        }
    }
    return pcr;
}