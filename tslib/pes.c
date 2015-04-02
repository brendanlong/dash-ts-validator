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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "bitreader.h"
#include "log.h"


static bool pes_read_header(pes_packet_t*, bitreader_t*);
static void pes_print_header(const pes_packet_t*);

static pes_packet_t* pes_new(void)
{
    pes_packet_t* pes = g_new0(pes_packet_t, 1);
    return pes;
}

void pes_free(pes_packet_t* pes)
{
    if (pes == NULL) {
        return;
    }
    free(pes->payload);
    free(pes);
}

pes_packet_t* pes_read(const uint8_t* buf, size_t len)
{
    g_return_val_if_fail(buf, NULL);

    pes_packet_t* pes = pes_new();
    bitreader_t* b = bitreader_new(buf, len);

    if (!pes_read_header(pes, b)) {
        goto fail;
    }

    if (pes->packet_length == 0) {
        pes->payload_len = bitreader_bytes_left(b);
    } else {
        pes->payload_len = pes->packet_length + 3 - b->bytes_read;
    }
    if (pes->payload_len) {
        pes->payload = malloc(pes->payload_len);
        bitreader_read_bytes(b, pes->payload, pes->payload_len);
    }

    if (b->error) {
        g_critical("PES packet has invalid length.");
        goto fail;
    }

cleanup:
    bitreader_free(b);
    return pes;
fail:
    pes_free(pes);
    pes = NULL;
    goto cleanup;
}

static bool pes_read_header(pes_packet_t* ph, bitreader_t* b)
{
    g_return_val_if_fail(ph, false);
    g_return_val_if_fail(b, false);

    // bytes 0..2
    uint32_t pes_packet_start_code = bitreader_read_uint24(b);
    if (pes_packet_start_code != PES_PACKET_START_CODE_PREFIX) {
        g_critical("PES packet starts with 0x%06X instead of expected start code 0x%06X.",
                pes_packet_start_code, PES_PACKET_START_CODE_PREFIX);
        return false;
    }

    // bytes 3..5
    ph->stream_id = bitreader_read_uint8(b);
    ph->packet_length = bitreader_read_uint16(b);

    if (HAS_PES_HEADER(ph->stream_id)) {
        // byte 6
        bitreader_skip_bits(b, 2);
        ph->scrambling_control = bitreader_read_bits(b, 2);
        ph->priority = bitreader_read_bit(b);
        ph->data_alignment_indicator = bitreader_read_bit(b);
        ph->copyright = bitreader_read_bit(b);
        ph->original_or_copy = bitreader_read_bit(b);

        // byte 7
        ph->pts_flag = bitreader_read_bit(b);
        ph->dts_flag = bitreader_read_bit(b);
        ph->escr_flag = bitreader_read_bit(b);
        ph->es_rate_flag = bitreader_read_bit(b);
        ph->dsm_trick_mode_flag = bitreader_read_bit(b);
        ph->additional_copy_info_flag = bitreader_read_bit(b);
        ph->crc_flag = bitreader_read_bit(b);
        ph->extension_flag = bitreader_read_bit(b);

        // byte 8
        uint8_t header_data_length = bitreader_read_uint8(b);

        int pes_packet_optional_start = b->bytes_read;

        // byte 9..14
        if (ph->pts_flag) {
            bitreader_skip_bits(b, 4);
            ph->pts = bitreader_read_90khz_timestamp(b);
        }
        // byte 15..19
        if (ph->dts_flag) {
            if (!ph->pts_flag) {
                g_critical("PTS_DTS_flags set to '01' in PES header. ISO 13818-1 section 2.4.3.7 says: PTS_DTS_flags "
                        "- [...] The value '01' is forbidden.");
                return false;
            }
            bitreader_skip_bits(b, 4);
            ph->dts = bitreader_read_90khz_timestamp(b);
        }

        if (ph->escr_flag) {
            bitreader_skip_bits(b, 2);
            ph->escr_base = bitreader_read_90khz_timestamp(b);
            ph->escr_extension = bitreader_read_bits(b, 9);
            bitreader_skip_bit(b);
        }
        if (ph->es_rate_flag) {
            bitreader_skip_bit(b);
            ph->es_rate = bitreader_read_bits(b, 22);
            bitreader_skip_bit(b);
        }
        if (ph->dsm_trick_mode_flag) {
            ph->trick_mode_control = bitreader_read_bits(b, 3);
            switch (ph->trick_mode_control) {
            case PES_DSM_TRICK_MODE_CTL_FAST_FORWARD:
            case PES_DSM_TRICK_MODE_CTL_FAST_REVERSE:
                ph->field_id = bitreader_read_bits(b, 2);
                ph->intra_slice_refresh = bitreader_read_bit(b);
                ph->frequency_truncation = bitreader_read_bits(b, 2);
                break;
            case PES_DSM_TRICK_MODE_CTL_SLOW_MOTION:
            case PES_DSM_TRICK_MODE_CTL_SLOW_REVERSE:
                ph->rep_cntrl = bitreader_read_bits(b, 5);
                break;
            case PES_DSM_TRICK_MODE_CTL_FREEZE_FRAME:
                ph->field_id = bitreader_read_bits(b, 2);
                bitreader_skip_bits(b, 3);
                break;
            default:
                bitreader_skip_bits(b, 5);
                break;
            }
        }
        if (ph->additional_copy_info_flag) {
            bitreader_skip_bit(b);
            ph->additional_copy_info = bitreader_read_bits(b, 7);
        }
        if (ph->crc_flag) {
            ph->previous_pes_packet_crc = bitreader_read_uint16(b);
        }
        if (ph->extension_flag) {
            ph->private_data_flag = bitreader_read_bit(b);
            ph->pack_header_field_flag = bitreader_read_bit(b);
            ph->program_packet_sequence_counter_flag = bitreader_read_bit(b);
            ph->pstd_buffer_flag = bitreader_read_bit(b);
            bitreader_skip_bits(b, 3);
            ph->extension_flag_2 = bitreader_read_bit(b);

            if (ph->private_data_flag) {
                bitreader_read_bytes(b, ph->private_data, 16);
            }

            if (ph->pack_header_field_flag) {
                // whoever discovers the need for pack_header() is welcome to implement it.
                // I haven't.
                ph->pack_field_length = bitreader_read_uint8(b);
                bitreader_skip_bytes(b, ph->pack_field_length);
            }
            if (ph->program_packet_sequence_counter_flag) {
                bitreader_skip_bit(b);
                ph->program_packet_sequence_counter = bitreader_read_bits(b, 7);
                bitreader_skip_bit(b);
                ph->mpeg1_mpeg2_identifier = bitreader_read_bit(b);
                ph->original_stuff_length = bitreader_read_bits(b, 6);
            }
            if (ph->pstd_buffer_flag) {
                bitreader_skip_bits(b, 2);
                ph->pstd_buffer_scale = bitreader_read_bit(b);
                ph->pstd_buffer_size = bitreader_read_bits(b, 13);
            }
            if (ph->extension_flag_2) {
                size_t pes_extension_field_start = b->bytes_read;
                bitreader_skip_bit(b);
                ph->extension_field_length = bitreader_read_bits(b, 7);
                ph->stream_id_extension_flag = bitreader_read_bit(b);
                if (!ph->stream_id_extension_flag) {
                    ph->stream_id_extension = bitreader_read_bits(b, 7);
                } else {
                    bitreader_skip_bits(b, 6);
                    ph->tref_extension_flag = bitreader_read_bit(b);
                    if(ph->tref_extension_flag) {
                        bitreader_skip_bits(b, 4);
                        ph->tref = bitreader_read_90khz_timestamp(b);
                    }
                }
                size_t pes_extension_bytes_left = b->bytes_read - pes_extension_field_start;
                if (pes_extension_bytes_left > 0) {
                    bitreader_skip_bytes(b, pes_extension_bytes_left);
                }
            }
        }

        int pes_optional_bytes_read = b->bytes_read - pes_packet_optional_start;
        int stuffing_bytes_len = header_data_length - pes_optional_bytes_read; // if any
        if (stuffing_bytes_len > 0) {
            bitreader_skip_bytes(b, stuffing_bytes_len);
        }
    }

    if (b->error) {
        g_critical("PES packet header has invalid length.");
    }
    return !b->error;
}

static void pes_print_header(const pes_packet_t* pes)
{
    g_return_if_fail(pes);

    SKIT_LOG_UINT32_DBG("", pes->stream_id);
    SKIT_LOG_UINT32_DBG("", pes->packet_length);

    if(HAS_PES_HEADER(pes->stream_id)) {
        SKIT_LOG_UINT32_DBG("", pes->scrambling_control);
        SKIT_LOG_UINT32_DBG("", pes->priority);
        SKIT_LOG_UINT32_DBG("", pes->data_alignment_indicator);
        SKIT_LOG_UINT32_DBG("", pes->copyright);
        SKIT_LOG_UINT32_DBG("", pes->original_or_copy);

        SKIT_LOG_UINT32_DBG("", pes->pts_flag);
        SKIT_LOG_UINT32_DBG("", pes->dts_flag);
        SKIT_LOG_UINT32_DBG("", pes->escr_flag);
        SKIT_LOG_UINT32_DBG("", pes->es_rate_flag);
        SKIT_LOG_UINT32_DBG("", pes->dsm_trick_mode_flag);
        SKIT_LOG_UINT32_DBG("", pes->additional_copy_info_flag);
        SKIT_LOG_UINT32_DBG("", pes->crc_flag);
        SKIT_LOG_UINT32_DBG("", pes->extension_flag);

        // byte 9..14
        if (pes->pts_flag) {
            SKIT_LOG_UINT64_DBG("   ", pes->pts);
        }

        // byte 15..19
        if (pes->dts_flag) {
            SKIT_LOG_UINT64_DBG("   ", pes->dts);
        }

        if (pes->escr_flag) {
            SKIT_LOG_UINT64_DBG("   ", pes->escr_base);
            SKIT_LOG_UINT32_DBG("   ", pes->escr_extension);

        }
        if (pes->es_rate_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes->es_rate);
        }

        if (pes->dsm_trick_mode_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes->trick_mode_control);
            switch (pes->trick_mode_control) {
            case PES_DSM_TRICK_MODE_CTL_FAST_FORWARD:
            case PES_DSM_TRICK_MODE_CTL_FAST_REVERSE:
                SKIT_LOG_UINT32_DBG("   ", pes->field_id);
                SKIT_LOG_UINT32_DBG("   ", pes->intra_slice_refresh);
                SKIT_LOG_UINT32_DBG("   ", pes->frequency_truncation);
                break;

            case PES_DSM_TRICK_MODE_CTL_SLOW_MOTION:
            case PES_DSM_TRICK_MODE_CTL_SLOW_REVERSE:
                SKIT_LOG_UINT32_DBG("   ", pes->rep_cntrl);
                break;

            case PES_DSM_TRICK_MODE_CTL_FREEZE_FRAME:
                SKIT_LOG_UINT32_DBG("   ", pes->field_id);
                break;

            default:
                break;
            }
        }
        if (pes->additional_copy_info_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes->additional_copy_info);
        }
        if (pes->crc_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes->previous_pes_packet_crc);
        }
        if (pes->extension_flag) {
            SKIT_LOG_UINT32_DBG("   ", pes->private_data_flag);
            SKIT_LOG_UINT32_DBG("   ", pes->pack_header_field_flag);
            SKIT_LOG_UINT32_DBG("   ", pes->program_packet_sequence_counter_flag);
            SKIT_LOG_UINT32_DBG("   ", pes->pstd_buffer_flag);
            SKIT_LOG_UINT32_DBG("   ", pes->extension_flag_2);

            // add ph->private_data_flag

            if (pes->pack_header_field_flag) {
                SKIT_LOG_UINT32_DBG("       ", pes->pack_field_length);
            }
            if (pes->program_packet_sequence_counter_flag) {
                SKIT_LOG_UINT32_DBG("       ", pes->program_packet_sequence_counter);
                SKIT_LOG_UINT32_DBG("       ", pes->mpeg1_mpeg2_identifier);
                SKIT_LOG_UINT32_DBG("       ", pes->original_stuff_length);
            }
            if (pes->pstd_buffer_flag) {
                SKIT_LOG_UINT32_DBG("       ", pes->pstd_buffer_scale);
                SKIT_LOG_UINT32_DBG("       ", pes->pstd_buffer_size);
            }
            if (pes->extension_flag_2) {
                SKIT_LOG_UINT32_DBG("       ", pes->extension_field_length);
                SKIT_LOG_UINT32_DBG("       ", pes->stream_id_extension_flag);
                if (!pes->stream_id_extension_flag) {
                    SKIT_LOG_UINT32_DBG("           ", pes->stream_id_extension);
                } else {
                    SKIT_LOG_UINT32_DBG("           ", pes->tref_extension_flag);
                    if (pes->tref_extension_flag) {
                        SKIT_LOG_UINT64_DBG("           ", pes->tref);
                    }
                }
            }
        }
    }
}

void pes_print(const pes_packet_t* pes)
{
    g_return_if_fail(pes);
    if (tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) {
        return;
    }
    pes_print_header(pes);
    SKIT_LOG_UINT64_DBG("", (uint64_t)pes->payload_len);
}
