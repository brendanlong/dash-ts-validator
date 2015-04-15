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

    LOG_DEBUG(0, "stream_id: %"PRIu8, pes->stream_id);
    LOG_DEBUG(0, "packet_length: %"PRIu16, pes->packet_length);

    if(HAS_PES_HEADER(pes->stream_id)) {
        LOG_DEBUG(0, "scrambling_control: %"PRIu8, pes->scrambling_control);
        LOG_DEBUG(0, "priority: %"PRIu8, pes->priority);
        LOG_DEBUG(0, "data_alignment_indicator: %s", BOOL_TO_STR(pes->data_alignment_indicator));
        LOG_DEBUG(0, "copyright: %s", BOOL_TO_STR(pes->copyright));
        LOG_DEBUG(0, "original_or_copy: %s", BOOL_TO_STR(pes->original_or_copy));

        LOG_DEBUG(0, "pts_flag: %s", BOOL_TO_STR(pes->pts_flag));
        LOG_DEBUG(0, "dts_flag: %s", BOOL_TO_STR(pes->dts_flag));
        LOG_DEBUG(0, "escr_flag: %s", BOOL_TO_STR(pes->escr_flag));
        LOG_DEBUG(0, "es_rate_flag: %s", BOOL_TO_STR(pes->es_rate_flag));
        LOG_DEBUG(0, "es_rate_flag: %s", BOOL_TO_STR(pes->es_rate_flag));
        LOG_DEBUG(0, "es_rate_flag: %s", BOOL_TO_STR(pes->es_rate_flag));
        LOG_DEBUG(0, "crc_flag: %s", BOOL_TO_STR(pes->crc_flag));
        LOG_DEBUG(0, "extension_flag: %s", BOOL_TO_STR(pes->extension_flag));

        // byte 9..14
        if (pes->pts_flag) {
            LOG_DEBUG(1, "pts: %"PRIu64, pes->pts);
        }

        // byte 15..19
        if (pes->dts_flag) {
            LOG_DEBUG(1, "dts: %"PRIu64, pes->dts);
        }

        if (pes->escr_flag) {
            LOG_DEBUG(1, "escr_base: %"PRIu64, pes->escr_base);
            LOG_DEBUG(1, "escr_extension: %"PRIu8, pes->escr_extension);

        }
        if (pes->es_rate_flag) {
            LOG_DEBUG(1, "es_rate: %"PRIu32, pes->es_rate);
        }

        if (pes->dsm_trick_mode_flag) {
            LOG_DEBUG(1, "trick_mode_control: %"PRIu8, pes->trick_mode_control);
            switch (pes->trick_mode_control) {
            case PES_DSM_TRICK_MODE_CTL_FAST_FORWARD:
            case PES_DSM_TRICK_MODE_CTL_FAST_REVERSE:
                LOG_DEBUG(1, "field_id: %"PRIu8, pes->field_id);
                LOG_DEBUG(1, "intra_slice_refresh: %s", BOOL_TO_STR(pes->intra_slice_refresh));
                LOG_DEBUG(1, "frequency_truncation: %"PRIu8, pes->frequency_truncation);
                break;

            case PES_DSM_TRICK_MODE_CTL_SLOW_MOTION:
            case PES_DSM_TRICK_MODE_CTL_SLOW_REVERSE:
                LOG_DEBUG(1, "rep_cntrl: %"PRIu8, pes->rep_cntrl);
                break;

            case PES_DSM_TRICK_MODE_CTL_FREEZE_FRAME:
                LOG_DEBUG(1, "field_id: %"PRIu8, pes->field_id);
                break;

            default:
                break;
            }
        }
        if (pes->additional_copy_info_flag) {
            LOG_DEBUG(1, "additional_copy_info: %"PRIu8, pes->additional_copy_info);
        }
        if (pes->crc_flag) {
            LOG_DEBUG(1, "previous_pes_packet_crc: %"PRIu16, pes->previous_pes_packet_crc);
        }
        if (pes->extension_flag) {
            LOG_DEBUG(1, "private_data_flag: %s", BOOL_TO_STR(pes->private_data_flag));
            LOG_DEBUG(1, "pack_header_field_flag: %s", BOOL_TO_STR(pes->pack_header_field_flag));
            LOG_DEBUG(1, "program_packet_sequence_counter_flag: %s",
                    BOOL_TO_STR(pes->program_packet_sequence_counter_flag));
            LOG_DEBUG(1, "pstd_buffer_flag: %s", BOOL_TO_STR(pes->pstd_buffer_flag));
            LOG_DEBUG(1, "extension: %s", BOOL_TO_STR(pes->extension_flag_2));

            // add ph->private_data_flag

            if (pes->pack_header_field_flag) {
                LOG_DEBUG(2, "pack_field_length: %"PRIu8, pes->pack_field_length);
            }
            if (pes->program_packet_sequence_counter_flag) {
                LOG_DEBUG(2, "program_packet_sequence_counter: %"PRIu8, pes->program_packet_sequence_counter);
                LOG_DEBUG(2, "mpeg1_mpeg2_identifier: %s", BOOL_TO_STR(pes->mpeg1_mpeg2_identifier));
                LOG_DEBUG(2, "original_stuff_length: %"PRIu8, pes->original_stuff_length);
            }
            if (pes->pstd_buffer_flag) {
                LOG_DEBUG(2, "pstd_buffer_scale: %s", BOOL_TO_STR(pes->pstd_buffer_scale));
                LOG_DEBUG(2, "pstd_buffer_size: %"PRIu16, pes->pstd_buffer_size);
            }
            if (pes->extension_flag_2) {
                LOG_DEBUG(2, "extension_field_length: %"PRIu8, pes->extension_field_length);
                LOG_DEBUG(2, "stream_id_extension_flag: %s", BOOL_TO_STR(pes->stream_id_extension_flag));
                if (!pes->stream_id_extension_flag) {
                    LOG_DEBUG(2, "stream_id_extension: %"PRIu8, pes->stream_id_extension);
                } else {
                    LOG_DEBUG(2, "tref_extension_flag: %s", BOOL_TO_STR(pes->tref_extension_flag));
                    if (pes->tref_extension_flag) {
                        LOG_DEBUG(2, "tref: %"PRIu64, pes->tref);
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
    LOG_DEBUG(0, "payload_len: %zu", pes->payload_len);
}