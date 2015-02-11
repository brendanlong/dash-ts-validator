/*
 Copyright (c) 2012-, ISO/IEC JTC1/SC29/WG11
 Written by Alex Giladi <alex.giladi@gmail.com>
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
#include "pes.h"

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "bs.h"
#include "libts_common.h"
#include "log.h"


static int pes_read_header(pes_header_t*, bs_t*);
static void pes_print_header(pes_header_t*);

pes_packet_t* pes_new(void)
{
    pes_packet_t* pes = g_new0(pes_packet_t, 1);
    return pes;
}

void pes_free(pes_packet_t* pes)
{
    if (pes == NULL) {
        return;
    }
    free(pes->buf);
    free(pes);
}

int pes_read(pes_packet_t* pes, const uint8_t* buf, size_t len)
{
    free(pes->buf);
    pes->buf = g_memdup(buf, len);
    pes->buf_len = len;

    bs_t b;
    bs_init(&b, pes->buf, pes->buf_len);
    int header_bytes = pes_read_header(&pes->header, &b);

    // PES header broken -- bail out.
    if (header_bytes < 0) {
        pes->status = header_bytes;
        return 0;
    }

    pes->payload = pes->buf + header_bytes;
    pes->payload_len =  pes->buf_len - header_bytes;

    if (pes->header.pes_packet_length > 0 && pes->header.pes_packet_length + 6 > pes->buf_len) {
        pes->status = PES_ERROR_NOT_ENOUGH_DATA;
        g_critical("PES packet header promises %u bytes, only %ld found in buffer",
                       pes->header.pes_packet_length + 6, pes->buf_len);
    }

    return bs_pos(&b);
}

static int pes_read_header(pes_header_t* ph, bs_t* b)
{
    int pes_packet_start = bs_pos(b);

    if (bs_bytes_left(b) < 6) {
        g_critical("%d bytes in PES packet header, at least 6 bytes expected", bs_bytes_left(b));
        return PES_ERROR_NOT_ENOUGH_DATA;
    }

    // bytes 0..2
    uint32_t pes_packet_start_code = bs_read_u24(b);
    if (pes_packet_start_code != PES_PACKET_START_CODE_PREFIX) {
        int actually_read = bs_pos(b) - pes_packet_start;
        b->p -= actually_read; // undo the read
        g_critical("PES packet starts with 0x%06X instead of expected start code 0x%06X, skipping it",
                       pes_packet_start_code, PES_PACKET_START_CODE_PREFIX);

        return PES_ERROR_WRONG_START_CODE; // bail out! something is fishy!
    }

    // bytes 3..5
    ph->stream_id = bs_read_u8(b);
    ph->pes_packet_length = bs_read_u16(b);

    if (HAS_PES_HEADER(ph->stream_id)) {
        if (bs_bytes_left(b) < 3) {
            g_critical("Not enough data to complete reading PES header");
            return PES_ERROR_NOT_ENOUGH_DATA;
        }
        // byte 6
        bs_skip_u(b, 2);
        ph->pes_scrambling_control = bs_read_u(b, 2);
        ph->pes_priority = bs_read_u1(b);
        ph->data_alignment_indicator = bs_read_u1(b);
        ph->copyright = bs_read_u1(b);
        ph->original_or_copy = bs_read_u1(b);

        // byte 7
        ph->pts_dts_flags = bs_read_u(b, 2);
        ph->escr_flag = bs_read_u1(b);
        ph->es_rate_flag = bs_read_u1(b);
        ph->dsm_trick_mode_flag = bs_read_u1(b);
        ph->additional_copy_info_flag = bs_read_u1(b);
        ph->pes_crc_flag = bs_read_u1(b);
        ph->pes_extension_flag = bs_read_u1(b);

        // byte 8
        ph->pes_header_data_length = bs_read_u8(b);

        if (bs_bytes_left(b) < ph->pes_header_data_length) {
            g_critical("Not enough data to complete reading PES header");
            return PES_ERROR_NOT_ENOUGH_DATA;
        }

        int pes_packet_optional_start = bs_pos(b);

        // byte 9..14
        if (ph->pts_dts_flags & PES_PTS_FLAG) {
            bs_skip_u(b, 4);
            ph->pts = bs_read_90khz_timestamp(b);
        }
        // byte 15..19
        if (ph->pts_dts_flags & PES_DTS_FLAG) {
            bs_skip_u(b, 4);
            ph->dts = bs_read_90khz_timestamp(b);
        }

        if (ph->escr_flag) {
            bs_skip_u(b, 2);
            ph->escr_base = bs_read_90khz_timestamp(b);
            ph->escr_extension = bs_read_u(b, 9);
            bs_skip_u1(b);
        }
        if (ph->es_rate_flag) {
            bs_skip_u1(b);
            ph->es_rate = bs_read_u(b, 22);
            bs_skip_u1(b);
        }
        if (ph->dsm_trick_mode_flag) {
            ph->trick_mode_control = bs_read_u(b, 3);
            switch (ph->trick_mode_control) {
            case PES_DSM_TRICK_MODE_CTL_FAST_FORWARD:
            case PES_DSM_TRICK_MODE_CTL_FAST_REVERSE:
                ph->field_id = bs_read_u(b, 2);
                ph->intra_slice_refresh = bs_read_u1(b);
                ph->frequency_truncation = bs_read_u(b, 2);
                break;
            case PES_DSM_TRICK_MODE_CTL_SLOW_MOTION:
            case PES_DSM_TRICK_MODE_CTL_SLOW_REVERSE:
                ph->rep_cntrl = bs_read_u(b, 5);
                break;
            case PES_DSM_TRICK_MODE_CTL_FREEZE_FRAME:
                ph->field_id = bs_read_u(b, 2);
                bs_skip_u(b, 3);
                break;
            default:
                bs_skip_u(b, 5);
                break;
            }
        }
        if (ph->additional_copy_info_flag) {
            bs_skip_u1(b);
            ph->additional_copy_info = bs_read_u(b, 7);
        }
        if (ph->pes_crc_flag) {
            ph->previous_pes_packet_crc = bs_read_u16(b);
        }
        if (ph->pes_extension_flag) {
            ph->pes_private_data_flag = bs_read_u1(b);
            ph->pack_header_field_flag = bs_read_u1(b);
            ph->program_packet_sequence_counter_flag = bs_read_u1(b);
            ph->pstd_buffer_flag = bs_read_u1(b);
            bs_skip_u(b, 3);
            ph->pes_extension_flag_2 = bs_read_u1(b);

            if (ph->pes_private_data_flag) {
                bs_read_bytes(b, ph->pes_private_data, 16);
            }

            if (ph->pack_header_field_flag) {
                // whoever discovers the need for pack_header() is welcome to implement it.
                // I haven't.
                ph->pack_field_length = bs_read_u8(b);
                bs_skip_bytes(b, ph->pack_field_length);
            }
            if (ph->program_packet_sequence_counter_flag) {
                bs_skip_u1(b);
                ph->program_packet_sequence_counter = bs_read_u(b, 7);
                bs_skip_u1(b);
                ph->mpeg1_mpeg2_identifier = bs_read_u1(b);
                ph->original_stuff_length = bs_read_u(b, 6);
            }
            if (ph->pstd_buffer_flag) {
                bs_skip_u(b, 2);
                ph->pstd_buffer_scale = bs_read_u1(b);
                ph->pstd_buffer_size = bs_read_u(b, 13);
            }
            if (ph->pes_extension_flag_2) {
                int pes_extension_field_start = bs_pos(b);
                bs_skip_u1(b);
                ph->pes_extension_field_length = bs_read_u(b, 7);
                ph->stream_id_extension_flag = bs_read_u1(b);
                if (!ph->stream_id_extension_flag) {
                    ph->stream_id_extension = bs_read_u(b, 7);
                } else {
                    bs_skip_u(b, 6);
                    ph->tref_extension_flag = bs_read_u1(b);
                    if(ph->tref_extension_flag) {
                        bs_skip_u(b, 4);
                        ph->tref = bs_read_90khz_timestamp(b);
                    }
                }
                int pes_extension_bytes_left = bs_pos(b) - pes_extension_field_start;
                if (pes_extension_bytes_left > 0) {
                    bs_skip_bytes(b, pes_extension_bytes_left);
                }
            }
        }

        int pes_optional_bytes_read = bs_pos(b) - pes_packet_optional_start;
        int stuffing_bytes_len = ph->pes_header_data_length - pes_optional_bytes_read; // if any
        if (stuffing_bytes_len > 0) {
            bs_skip_bytes(b, stuffing_bytes_len);
        }
    }
    return bs_pos(b) - pes_packet_start;
}

static void pes_print_header(pes_header_t* pes_header)
{
    SKIT_LOG_UINT32_DBG("", pes_header->stream_id);
    SKIT_LOG_UINT32_DBG("", pes_header->pes_packet_length);

    if(HAS_PES_HEADER(pes_header->stream_id)) {
        SKIT_LOG_UINT32_DBG("", pes_header->pes_scrambling_control);
        SKIT_LOG_UINT32_DBG("", pes_header->pes_priority);
        SKIT_LOG_UINT32_DBG("", pes_header->data_alignment_indicator);
        SKIT_LOG_UINT32_DBG("", pes_header->copyright);
        SKIT_LOG_UINT32_DBG("", pes_header->original_or_copy);

        SKIT_LOG_UINT32_DBG("", pes_header->pts_dts_flags);
        SKIT_LOG_UINT32_DBG("", pes_header->escr_flag);
        SKIT_LOG_UINT32_DBG("", pes_header->es_rate_flag);
        SKIT_LOG_UINT32_DBG("", pes_header->dsm_trick_mode_flag);
        SKIT_LOG_UINT32_DBG("", pes_header->additional_copy_info_flag);
        SKIT_LOG_UINT32_DBG("", pes_header->pes_crc_flag);
        SKIT_LOG_UINT32_DBG("", pes_header->pes_extension_flag);

        // byte 8
        SKIT_LOG_UINT32_DBG("", pes_header->pes_header_data_length);

        // byte 9..14
        if (pes_header->pts_dts_flags & PES_PTS_FLAG) {
            SKIT_LOG_UINT64_DBG("   ", pes_header->pts);
        }

        // byte 15..19
        if (pes_header->pts_dts_flags & PES_DTS_FLAG) {
            SKIT_LOG_UINT64_DBG("   ", pes_header->dts);
        }

        if (pes_header->escr_flag) {
            SKIT_LOG_UINT64_DBG("   ", pes_header->escr_base);
            SKIT_LOG_UINT32_DBG("   ", pes_header->escr_extension);

        }
        if (pes_header->es_rate_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes_header->es_rate);
        }

        if (pes_header->dsm_trick_mode_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes_header->trick_mode_control);
            switch (pes_header->trick_mode_control) {
            case PES_DSM_TRICK_MODE_CTL_FAST_FORWARD:
            case PES_DSM_TRICK_MODE_CTL_FAST_REVERSE:
                SKIT_LOG_UINT32_DBG("   ", pes_header->field_id);
                SKIT_LOG_UINT32_DBG("   ", pes_header->intra_slice_refresh);
                SKIT_LOG_UINT32_DBG("   ", pes_header->frequency_truncation);
                break;

            case PES_DSM_TRICK_MODE_CTL_SLOW_MOTION:
            case PES_DSM_TRICK_MODE_CTL_SLOW_REVERSE:
                SKIT_LOG_UINT32_DBG("   ", pes_header->rep_cntrl);
                break;

            case PES_DSM_TRICK_MODE_CTL_FREEZE_FRAME:
                SKIT_LOG_UINT32_DBG("   ", pes_header->field_id);
                break;

            default:
                break;
            }
        }
        if (pes_header->additional_copy_info_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes_header->additional_copy_info);
        }
        if (pes_header->pes_crc_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes_header->previous_pes_packet_crc);
        }
        if (pes_header->pes_extension_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes_header->pes_private_data_flag);
            SKIT_LOG_UINT32_DBG("   ", pes_header->pack_header_field_flag);
            SKIT_LOG_UINT32_DBG("   ", pes_header->program_packet_sequence_counter_flag);
            SKIT_LOG_UINT32_DBG("   ", pes_header->pstd_buffer_flag);
            SKIT_LOG_UINT32_DBG("   ", pes_header->pes_extension_flag_2);

            // add ph->pes_private_data_flag

            if (pes_header->pack_header_field_flag) {
                SKIT_LOG_UINT32_DBG("       ", pes_header->pack_field_length);
            }
            if (pes_header->program_packet_sequence_counter_flag) {
                SKIT_LOG_UINT32_DBG("       ", pes_header->program_packet_sequence_counter);
                SKIT_LOG_UINT32_DBG("       ", pes_header->mpeg1_mpeg2_identifier);
                SKIT_LOG_UINT32_DBG("       ", pes_header->original_stuff_length);
            }
            if (pes_header->pstd_buffer_flag) {
                SKIT_LOG_UINT32_DBG("       ", pes_header->pstd_buffer_scale);
                SKIT_LOG_UINT32_DBG("       ", pes_header->pstd_buffer_size);
            }
            if (pes_header->pes_extension_flag_2) {
                SKIT_LOG_UINT32_DBG("       ", pes_header->pes_extension_field_length);
                SKIT_LOG_UINT32_DBG("       ", pes_header->stream_id_extension_flag);
                if (!pes_header->stream_id_extension_flag) {
                    SKIT_LOG_UINT32_DBG("           ", pes_header->stream_id_extension);
                } else {
                    SKIT_LOG_UINT32_DBG("           ", pes_header->tref_extension_flag);
                    if (pes_header->tref_extension_flag) {
                        SKIT_LOG_UINT64_DBG("           ", pes_header->tref);
                    }
                }
            }
        }
    }
}

void pes_print(pes_packet_t* pes)
{
    if (tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) {
        return;
    }
    pes_print_header(&pes->header);
    SKIT_LOG_UINT64_DBG("", (uint64_t)pes->payload_len);
}
