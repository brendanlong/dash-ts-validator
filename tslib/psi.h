/*
 Written by Alex Giladi <alex.giladi@gmail.com> and Vlad Zbarsky <zbarsky@cornell.edu>
 All rights reserved.
 Copyright (c) 2014-, ISO/IEC JTC1/SC29/WG11

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
#ifndef PSI_H
#define PSI_H

#include <descriptors.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TABLE_ID_PROGRAM_ASSOCIATION_SECTION = 0,
    TABLE_ID_CONDITIONAL_ACCESS_SECTION, // CA section
    TABLE_ID_PROGRAM_MAP_SECTION,
    TABLE_ID_DESCRIPTION_SECTION,
    TABLE_ID_SCENE_DESCRIPTION_SECTION,
    TABLE_ID_OBJECT_DESCRIPTOR_SECTION,
    TABLE_ID_METADATA_SECTION,
    TABLE_ID_IPMP_CONTROL_INFORMATION_SECTION, // (defined in ISO/IEC 13818-11)
    // 0x08-0x3F ISO/IEC 13818-1 reserved
    // 0x40-0xFE User private
    TABLE_ID_FORBIDDEN = 0xFF
} table_id_t;

#define	MAX_SECTION_LEN 0x03FD
#define MAX_PROGRAM_INFO_LEN 0x03FF
#define MAX_ES_INFO_LEN 0x03FF

enum {
    STREAM_TYPE_MPEG1_VIDEO = 0x01,
    STREAM_TYPE_MPEG2_VIDEO = 0x02,
    STREAM_TYPE_MPEG1_AUDIO = 0x03,
    STREAM_TYPE_MPEG2_AUDIO = 0x04,
    STREAM_TYPE_MPEG2_PRIVATE_SECTIONS = 0x05,
    STREAM_TYPE_MPEG2_PRIVATE_PES = 0x06,
    STREAM_TYPE_MHEG = 0x07,
    STREAM_TYPE_MPEG2_DSMCC = 0x08,
    STREAM_TYPE_ATM_MUX = 0x09,
    STREAM_TYPE_DSMCC_A = 0x0A,
    STREAM_TYPE_DSMCC_B = 0x0B,
    STREAM_TYPE_DSMCC_C = 0x0C,
    STREAM_TYPE_DSMCC_D = 0x0D,
    STREAM_TYPE_MPEG2_AUX = 0x0E,
    STREAM_TYPE_MPEG2_AAC = 0x0F, // ADTS
    STREAM_TYPE_MPEG4_VIDEO = 0x10,
    STREAM_TYPE_MPEG4_AAC = 0x11, // LATM
    STREAM_TYPE_MPEG4_SYS_PES = 0x12,
    STREAM_TYPE_MPEG2_SYS_SECTION = 0x13,
    STREAM_TYPE_DSMCC_SDP = 0x14,
    STREAM_TYPE_METADATA_PES = 0x15,
    STREAM_TYPE_METADATA_SECTIONS = 0x16,
    STREAM_TYPE_METADATA_DSMCC_DATA = 0x17,
    STREAM_TYPE_METADATA_DSMCC_OBJ = 0x18,
    STREAM_TYPE_METADATA_DSMCC_SDP = 0x19,
    STREAM_TYPE_MPEG2_IPMP = 0x1A,
    STREAM_TYPE_AVC = 0x1B,
    STREAM_TYPE_MPEG4_AAC_RAW = 0x1C,
    STREAM_TYPE_MPEG4_TIMED_TEXT = 0x1D,
    STREAM_TYPE_AVSI = 0x1E,
    STREAM_TYPE_SVC = 0x1F,
    STREAM_TYPE_MVC = 0x20,
    STREAM_TYPE_JPEG2000 = 0x21,
    STREAM_TYPE_S3D_SC_MPEG2 = 0x22,
    STREAM_TYPE_S3D_SC_AVC = 0x23,
    STREAM_TYPE_HEVC = 0x24,
    STREAM_TYPE_IPMP = 0x7F,
//TODO: handle registration descriptor
    STREAM_TYPE_AC3_AUDIO = 0x81 // ATSC A/52B, A3.1 AC3 Stream Type
} ts_stream_type_t;

#define GENERAL_PURPOSE_PID_MIN		0x0010
#define GENERAL_PURPOSE_PID_MAX		0x1FFE

typedef struct {
    uint8_t table_id;
    bool section_syntax_indicator;
    bool private_indicator;
    uint16_t section_length;
    uint8_t* bytes;
    size_t bytes_len;
} mpeg2ts_section_t;

typedef struct {
    uint16_t program_number;
    uint16_t program_map_pid; // a.k.a. network pid for prog 0
} program_info_t;

typedef struct {
    uint8_t table_id;
    bool section_syntax_indicator;
    bool private_indicator;
    uint16_t section_length;
    uint8_t* bytes;
    size_t bytes_len;

    uint16_t transport_stream_id;
    uint8_t version_number;
    bool current_next_indicator;
    uint8_t section_number;
    uint8_t last_section_number;

    program_info_t* programs;
    size_t num_programs;
    uint32_t crc_32;
} program_association_section_t;

typedef struct {
    uint8_t table_id;
    bool section_syntax_indicator;
    bool private_indicator;
    uint16_t section_length;
    uint8_t* bytes;
    size_t bytes_len;

    uint8_t version_number;
    bool current_next_indicator;
    uint8_t section_number;
    uint8_t last_section_number;

    descriptor_t** descriptors;
    size_t descriptors_len;

    uint32_t crc_32;
} conditional_access_section_t;

// PMT
typedef struct {
    uint8_t stream_type;
    uint16_t elementary_pid;

    descriptor_t** descriptors;
    size_t descriptors_len;
} elementary_stream_info_t;

typedef struct {
    uint8_t table_id;
    bool section_syntax_indicator;
    bool private_indicator;
    uint16_t section_length;
    uint8_t* bytes;
    size_t bytes_len;

    uint16_t program_number;
    uint8_t version_number;
    bool current_next_indicator;
    uint8_t section_number;
    uint8_t last_section_number;
    uint16_t pcr_pid;

    descriptor_t** descriptors;
    size_t descriptors_len;

    elementary_stream_info_t** es_info;
    size_t es_info_len;

    uint32_t crc_32;
} program_map_section_t;

bool mpeg2ts_sections_equal(const mpeg2ts_section_t*, const mpeg2ts_section_t*);

void program_association_section_free(program_association_section_t*);
program_association_section_t* program_association_section_read(uint8_t* buf, size_t buf_len);
void program_association_section_print(const program_association_section_t*);
static inline bool program_association_sections_equal(const program_association_section_t* a,
        const program_association_section_t* b)
{
    return mpeg2ts_sections_equal((const mpeg2ts_section_t*)a, (const mpeg2ts_section_t*)b);
}

void conditional_access_section_free(conditional_access_section_t* );
conditional_access_section_t* conditional_access_section_read(uint8_t* buf, size_t buf_len);
void conditional_access_section_print(const conditional_access_section_t*);
static inline bool conditional_access_sections_equal(const conditional_access_section_t* a,
        const conditional_access_section_t* b)
{
    return mpeg2ts_sections_equal((const mpeg2ts_section_t*)a, (const mpeg2ts_section_t*)b);
}

void program_map_section_free(program_map_section_t*);
program_map_section_t* program_map_section_read(uint8_t* buf, size_t buf_size);
void program_map_section_print(program_map_section_t*);
static inline bool program_map_sections_equal(const program_map_section_t* a,
        const program_map_section_t* b)
{
    return mpeg2ts_sections_equal((const mpeg2ts_section_t*)a, (const mpeg2ts_section_t*)b);
}

const char* stream_desc(uint8_t stream_id);

#endif