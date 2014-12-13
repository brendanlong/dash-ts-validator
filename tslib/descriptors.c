/*
 Copyright (c) 2014-, ISO/IEC JTC1/SC29/WG11
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
#include "descriptors.h"

#include "libts_common.h"
#include "log.h"
#include "ts.h"


// "factory methods"
int read_descriptor_loop(GPtrArray* desc_list, bs_t* b, int length)
{
    int desc_start = bs_pos(b);

    while(length > bs_pos(b) - desc_start) {
        descriptor_t* desc = descriptor_new();
        desc = descriptor_read(desc, b);
        g_ptr_array_add(desc_list, desc);
    }

    return bs_pos(b) - desc_start;
}

int write_descriptor_loop(GPtrArray* desc_list, bs_t* b)
{
    // TODO actually implement descriptor loop writing
    return 0;
}

void print_descriptor_loop(GPtrArray* desc_list, int level)
{
    for(gsize i = 0; i < desc_list->len; ++i) {
        descriptor_t* desc = g_ptr_array_index(desc_list, i);
        if(desc != NULL) {
            descriptor_print(desc, level);
        }
    }
}

descriptor_t* descriptor_new()
{
    descriptor_t* desc = malloc(sizeof(descriptor_t));
    return desc;
}

void descriptor_free(descriptor_t* desc)
{
    if(!desc) {
        return;
    }
    switch(desc->tag) {
    case ISO_639_LANGUAGE_DESCRIPTOR:
        language_descriptor_free(desc);
        break;
    default:
        free(desc);
        break;
    }
}

descriptor_t* descriptor_read(descriptor_t* desc, bs_t* b)
{
    desc->tag = bs_read_u8(b);
    desc->length = bs_read_u8(b);

    switch(desc->tag) {
    case ISO_639_LANGUAGE_DESCRIPTOR:
        desc = language_descriptor_read(desc, b);
        break;
    case CA_DESCRIPTOR:
        desc = ca_descriptor_read(desc, b);
        break;
    default:
        bs_skip_bytes(b, desc->length);
    }

    return desc;
}

void descriptor_print(const descriptor_t* desc, int level)
{
    if(desc == NULL || tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }
    switch(desc->tag) {
    case ISO_639_LANGUAGE_DESCRIPTOR:
        language_descriptor_print(desc, level);
        break;
    case CA_DESCRIPTOR:
        ca_descriptor_print(desc, level);
        break;
    default:
        SKIT_LOG_UINT(level, desc->tag);
        SKIT_LOG_UINT(level, desc->length);
    }
}

descriptor_t* language_descriptor_new(descriptor_t* desc)
{
    language_descriptor_t* ld = NULL;
    if(desc == NULL) {
        ld = (language_descriptor_t*)calloc(1, sizeof(language_descriptor_t));
        ld->descriptor.tag = ISO_639_LANGUAGE_DESCRIPTOR;
    } else {
        ld = realloc(desc, sizeof(language_descriptor_t));
        ld->languages = NULL;
        ld->_num_languages = 0;
    }
    return (descriptor_t*)ld;
}

int language_descriptor_free(descriptor_t* desc)
{
    if(desc == NULL) {
        return 0;
    }
    if(desc->tag != ISO_639_LANGUAGE_DESCRIPTOR) {
        return 0;
    }

    language_descriptor_t* ld = (language_descriptor_t*)desc;
    if(ld->languages != NULL) {
        free(ld->languages);
    }
    free(ld);
    return 1;
}

descriptor_t* language_descriptor_read(descriptor_t* desc, bs_t* b)
{
    if((desc == NULL) || (b == NULL)) {
        SAFE_REPORT_TS_ERR(-1);
        return NULL;
    }
    if(desc->tag != ISO_639_LANGUAGE_DESCRIPTOR) {
        g_critical("Language descriptor has tag 0x%02X instead of expected 0x%02X",
                       desc->tag, ISO_639_LANGUAGE_DESCRIPTOR);
        SAFE_REPORT_TS_ERR(-50);
        return NULL;
    }

    language_descriptor_t* ld = (language_descriptor_t*)calloc(1, sizeof(language_descriptor_t));

    ld->descriptor.tag = desc->tag;
    ld->descriptor.length = desc->length;

    ld->_num_languages = ld->descriptor.length / 4; // language takes 4 bytes

    if(ld->_num_languages > 0) {
        ld->languages = malloc(ld->_num_languages * sizeof(language_descriptor_t));
        for(int i = 0; i < ld->_num_languages; i++) {
            ld->languages[i].ISO_639_language_code[0] = bs_read_u8(b);
            ld->languages[i].ISO_639_language_code[1] = bs_read_u8(b);
            ld->languages[i].ISO_639_language_code[2] = bs_read_u8(b);
            ld->languages[i].ISO_639_language_code[3] = 0;
            ld->languages[i].audio_type = bs_read_u8(b);
        }
    }

    free(desc);
    return (descriptor_t*)ld;
}

void language_descriptor_print(const descriptor_t* desc, int level)
{
    if(desc == NULL) {
        return;
    }
    if(desc->tag != ISO_639_LANGUAGE_DESCRIPTOR) {
        return;
    }

    language_descriptor_t* ld = (language_descriptor_t*)desc;

    SKIT_LOG_UINT_VERBOSE(level, desc->tag, "ISO_639_language_descriptor");
    SKIT_LOG_UINT(level, desc->length);

    if(ld->_num_languages > 0) {
        for(int i = 0; i < ld->_num_languages; i++) {
            SKIT_LOG_STR(level, strtol(ld->languages[i].ISO_639_language_code, NULL, 10));
            SKIT_LOG_UINT(level, ld->languages[i].audio_type);
        }
    }
}


descriptor_t* ca_descriptor_new(descriptor_t* desc)
{
    ca_descriptor_t* cad = NULL;
    cad = (ca_descriptor_t*)calloc(1, sizeof(ca_descriptor_t));
    cad->descriptor.tag = CA_DESCRIPTOR;
    if(desc != NULL) {
        cad->descriptor.tag = desc->length;
    }
    free(desc);
    return (descriptor_t*)cad;
}

int ca_descriptor_free(descriptor_t* desc)
{
    if(desc == NULL) {
        return 0;
    }
    if(desc->tag != CA_DESCRIPTOR) {
        return 0;
    }

    ca_descriptor_t* cad = (ca_descriptor_t*)desc;
    if(cad->private_data_bytes != NULL) {
        free(cad->private_data_bytes);
    }

    free(cad);
    return 1;
}

descriptor_t* ca_descriptor_read(descriptor_t* desc, bs_t* b)
{
    if((desc == NULL) || (b == NULL)) {
        return NULL;
    }

    ca_descriptor_t* cad = (ca_descriptor_t*)ca_descriptor_new(desc);

    cad->CA_system_ID = bs_read_u16(b);
    bs_skip_u(b, 3);
    cad->CA_PID = bs_read_u(b, 13);
    cad->_private_data_bytes_buf_len = cad->descriptor.length - 4; // we just read 4 bytes
    cad->private_data_bytes = malloc(cad->_private_data_bytes_buf_len);
    bs_read_bytes(b, cad->private_data_bytes, cad->_private_data_bytes_buf_len);

    return (descriptor_t*)cad;
}

void ca_descriptor_print(const descriptor_t* desc, int level)
{
    if(desc == NULL) {
        return;
    }
    if(desc->tag != CA_DESCRIPTOR) {
        return;
    }

    ca_descriptor_t* cad = (ca_descriptor_t*)desc;

    SKIT_LOG_UINT_VERBOSE(level, desc->tag, "CA_descriptor");
    SKIT_LOG_UINT(level, desc->length);

    SKIT_LOG_UINT(level, cad->CA_PID);
    SKIT_LOG_UINT(level, cad->CA_system_ID);
}

/*
 2, video_stream_descriptor
 3, audio_stream_descriptor
 4, hierarchy_descriptor
 5, registration_descriptor
 6, data_stream_alignment_descriptor
 7, target_background_grid_descriptor
 8, video_window_descriptor
 9, CA_descriptor
 10, ISO_639_language_descriptor
 11, system_clock_descriptor
 12, multiplex_buffer_utilization_descriptor
 13, copyright_descriptor
 14,maximum_bitrate_descriptor
 15,private_data_indicator_descriptor
 16,smoothing_buffer_descriptor
 17,STD_descriptor
 18,IBP_descriptor
 // 19-26 Defined in ISO/IEC 13818-6
 27,MPEG-4_video_descriptor
 28,MPEG-4_audio_descriptor
 29,IOD_descriptor
 30,SL_descriptor
 31,FMC_descriptor
 32,external_ES_ID_descriptor
 33,MuxCode_descriptor
 34,FmxBufferSize_descriptor
 35,multiplexbuffer_descriptor
 36,content_labeling_descriptor
 37,metadata_pointer_descriptor
 38,metadata_descriptor
 39,metadata_STD_descriptor
 40,AVC video descriptor
 41,IPMP_descriptor
 42,AVC timing and HRD descriptor
 43,MPEG-2_AAC_audio_descriptor
 44,FlexMuxTiming_descriptor
 45,MPEG-4_text_descriptor
 46,MPEG-4_audio_extension_descriptor
 47,auxiliary_video_stream_descriptor
 48,SVC extension descriptor
 49,MVC extension descriptor
 50,J2K video descriptor
 51,MVC operation point descriptor
 52,MPEG2_stereoscopic_video_format_descriptor
 53,Stereoscopic_program_info_descriptor
 54,Stereoscopic_video_info_descriptor
 */
