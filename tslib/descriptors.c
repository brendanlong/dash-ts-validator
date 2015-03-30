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

#include <stdlib.h>
#include "bitreader.h"
#include "log.h"

ca_descriptor_t* ca_descriptor_new(descriptor_t* desc);
void ca_descriptor_free(descriptor_t* desc);
descriptor_t* ca_descriptor_read(descriptor_t* desc);

descriptor_t* descriptor_new(void)
{
    descriptor_t* desc = malloc(sizeof(*desc));
    desc->tag = 0;
    desc->data = NULL;
    desc->data_len = 0;
    return desc;
}

void descriptor_free(descriptor_t* desc)
{
    if (!desc) {
        return;
    }
    free(desc->data);
    switch (desc->tag) {
    case CA_DESCRIPTOR:
        ca_descriptor_free(desc);
        break;
    default:
        free(desc);
        break;
    }
}

descriptor_t* descriptor_read(uint8_t* data, size_t data_len)
{
    g_return_val_if_fail(data, NULL);

    descriptor_t* desc = descriptor_new();
    bitreader_t* b = bitreader_new(data, data_len);

    desc->tag = bitreader_read_uint8(b);
    desc->data_len = bitreader_read_uint8(b);
    desc->data = malloc(desc->data_len);
    for (size_t i = 0; i < desc->data_len; ++i) {
        desc->data[i] = bitreader_read_uint8(b);
    }

    if (b->error) {
        g_critical("descriptor is invalid");
        goto fail;
    }

    switch (desc->tag) {
    case CA_DESCRIPTOR:
        desc = ca_descriptor_read(desc);
        break;
    }

cleanup:
    bitreader_free(b);
    return desc;
fail:
    descriptor_free(desc);
    desc = NULL;
    goto cleanup;
}

void descriptor_print(const descriptor_t* desc, int level)
{
    g_return_if_fail(desc);

    if (tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    }
    switch (desc->tag) {
    case CA_DESCRIPTOR:
        ca_descriptor_print(desc, level);
        break;
    default:
        SKIT_LOG_UINT(level, desc->tag);
        SKIT_LOG_UINT(level, desc->data_len);
    }
}

ca_descriptor_t* ca_descriptor_new(descriptor_t* desc)
{
    ca_descriptor_t* cad = calloc(1, sizeof(*cad));
    cad->descriptor.tag = CA_DESCRIPTOR;
    if (desc != NULL) {
        cad->descriptor.data = desc->data;
        desc->data = NULL;
        cad->descriptor.data_len = desc->data_len;
    }
    free(desc);
    return cad;
}

void ca_descriptor_free(descriptor_t* desc)
{
    if (desc == NULL) {
        return;
    }
    g_return_if_fail(desc->tag == CA_DESCRIPTOR);

    ca_descriptor_t* cad = (ca_descriptor_t*)desc;
    free(cad->private_data);
    free(cad);
}

descriptor_t* ca_descriptor_read(descriptor_t* desc)
{
    g_return_val_if_fail(desc, NULL);
    g_return_val_if_fail(desc->data, NULL);

    ca_descriptor_t* cad = ca_descriptor_new(desc);
    bitreader_t* b = bitreader_new(cad->descriptor.data, cad->descriptor.data_len);

    cad->ca_system_id = bitreader_read_uint16(b);
    bitreader_skip_bits(b, 3);
    cad->ca_pid = bitreader_read_uint(b, 13);
    cad->private_data_len = cad->descriptor.data_len - 4; // we just read 4 bytes
    cad->private_data = malloc(cad->private_data_len);
    for (size_t i = 0; i < cad->private_data_len; ++i) {
        cad->private_data[i] = bitreader_read_uint8(b);
    }
    if (b->error) {
        ca_descriptor_print((descriptor_t*)cad, 0);
        g_critical("CA_descriptor is invalid.");
        goto fail;
    }

cleanup:
    bitreader_free(b);
    return (descriptor_t*)cad;
fail:
    ca_descriptor_free((descriptor_t*)cad);
    cad = NULL;
    goto cleanup;
}

void ca_descriptor_print(const descriptor_t* desc, int level)
{
    g_return_if_fail(desc);
    g_return_if_fail(desc->tag == CA_DESCRIPTOR);

    const ca_descriptor_t* cad = (const ca_descriptor_t*)desc;

    SKIT_LOG_UINT8(level, desc->tag);
    SKIT_LOG_UINT8(level, desc->data_len);

    SKIT_LOG_UINT16(level, cad->ca_pid);
    SKIT_LOG_UINT16(level, cad->ca_system_id);
}