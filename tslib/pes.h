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
#ifndef PES_H
#define PES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// PES stream ID's
#define PES_STREAM_ID_PROGRAM_STREAM_MAP       0xBC
#define PES_STREAM_ID_PROGRAM_STREAM_DIRECTORY 0xFF

#define PES_STREAM_ID_PRIVATE_1                0xBD
#define PES_STREAM_ID_PADDING                  0xBE
#define PES_STREAM_ID_PRIVATE_2                0xBF
#define PES_STREAM_ID_AUDIO_MIN                0xC0
#define PES_STREAM_ID_AUDIO_MAX                0xDF
#define PES_STREAM_ID_VIDEO_MIN                0xEF
#define PES_STREAM_ID_VIDEO_MAX                0xEF
#define PES_STREAM_ID_ECM                      0xF0
#define PES_STREAM_ID_EMM                      0xF1
#define PES_STREAM_ID_DSMCC                    0xF2
#define PES_STREAM_ID_MHEG                     0xF3
#define PES_STREAM_ID_H222_1_TYPE_A            0xF4
#define PES_STREAM_ID_H222_1_TYPE_B            0xF5
#define PES_STREAM_ID_H222_1_TYPE_C            0xF6
#define PES_STREAM_ID_H222_1_TYPE_D            0xF7
#define PES_STREAM_ID_H222_1_TYPE_E            0xF8
#define PES_STREAM_ID_ANCILLARY                0xF9
#define PES_STREAM_ID_MPEG4_SYS_SL             0xFA
#define PES_STREAM_ID_MP4_SYS_FLEXMUX          0xFB
#define PES_STREAM_ID_METADATA                 0xFC
#define PES_STREAM_ID_EXTENDED                 0xFD
#define PES_STREAM_ID_RESERVED                 0xFE

#define PES_STREAM_ID_EXT_IPMP_CONTROL         0x00
#define PES_STREAM_ID_EXT_IPMP                 0x01
#define PES_STREAM_ID_EXT_MPEG4_TIMED_TEXT_MIN 0x02
#define PES_STREAM_ID_EXT_MPEG4_TIMED_TEXT_MAX 0x0F
#define PES_STREAM_ID_EXT_AVSI_VIDEO_MIN       0x10
#define PES_STREAM_ID_EXT_AVSI_VIDEO_MAX       0x1F

#define HAS_PES_HEADER(SID)  (((SID) != PES_STREAM_ID_PROGRAM_STREAM_MAP )	\
         && ((SID) != PES_STREAM_ID_PADDING )                               \
         && ((SID) != PES_STREAM_ID_PRIVATE_2 )                             \
         && ((SID) != PES_STREAM_ID_ECM )                                   \
         && ((SID) != PES_STREAM_ID_EMM )                                   \
         && ((SID) != PES_STREAM_ID_PROGRAM_STREAM_DIRECTORY )              \
         && ((SID) != PES_STREAM_ID_DSMCC )                                 \
         && ((SID) != PES_STREAM_ID_H222_1_TYPE_E ) )

// Trick Mode
#define PES_DSM_TRICK_MODE_CTL_FAST_FORWARD    0x00
#define PES_DSM_TRICK_MODE_CTL_SLOW_MOTION     0x01
#define PES_DSM_TRICK_MODE_CTL_FREEZE_FRAME    0x02
#define PES_DSM_TRICK_MODE_CTL_FAST_REVERSE    0x03
#define PES_DSM_TRICK_MODE_CTL_SLOW_REVERSE    0x04

#define PES_PACKET_START_CODE_PREFIX           0x000001

typedef struct {
    uint8_t stream_id;
    uint16_t packet_length;
    uint8_t scrambling_control;
    uint8_t priority;
    bool data_alignment_indicator;
    bool copyright;
    bool original_or_copy;
    bool pts_flag;
    bool dts_flag;
    bool escr_flag;
    bool es_rate_flag;
    bool dsm_trick_mode_flag;
    bool additional_copy_info_flag;
    bool crc_flag;
    bool extension_flag;
    uint64_t pts;
    uint64_t dts;
    uint64_t escr_base;
    uint8_t escr_extension;
    uint32_t es_rate;
    uint8_t trick_mode_control;
    uint8_t field_id;
    bool intra_slice_refresh;
    uint8_t frequency_truncation;
    uint8_t rep_cntrl;
    uint8_t additional_copy_info;
    uint16_t previous_pes_packet_crc;
    bool private_data_flag;
    bool pack_header_field_flag;
    bool program_packet_sequence_counter_flag;
    bool pstd_buffer_flag;
    bool extension_flag_2;
    uint8_t private_data[16];
    uint8_t pack_field_length;
    uint8_t program_packet_sequence_counter;
    bool mpeg1_mpeg2_identifier;
    uint8_t original_stuff_length;
    bool pstd_buffer_scale;
    uint16_t pstd_buffer_size;
    uint8_t extension_field_length;
    bool stream_id_extension_flag;
    uint8_t stream_id_extension;
    bool tref_extension_flag;
    uint64_t tref;

    uint8_t* payload;
    size_t payload_len;

    uint64_t payload_pos_in_stream;
} pes_packet_t;

void pes_free(pes_packet_t*);
pes_packet_t* pes_read(const uint8_t* buf, size_t len);
void pes_print(const pes_packet_t*);

#endif
