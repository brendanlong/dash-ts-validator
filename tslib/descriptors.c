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
#include "log.h"

ca_descriptor_t* ca_descriptor_new(descriptor_t* desc);
void ca_descriptor_free(descriptor_t* desc);
descriptor_t* ca_descriptor_read(descriptor_t* desc);

descriptor_t* descriptor_new(void)
{
    descriptor_t* desc = g_slice_new0(descriptor_t);
    return desc;
}

void descriptor_free(descriptor_t* desc)
{
    if (!desc) {
        return;
    }
    switch (desc->tag) {
    case CA_DESCRIPTOR:
        ca_descriptor_free(desc);
        break;
    }
    free(desc->data);
    g_slice_free(descriptor_t, desc);
}

descriptor_t* descriptor_read(uint8_t* data, size_t data_len)
{
    g_return_val_if_fail(data, NULL);

    bitreader_t* b = bitreader_new(data, data_len);
    descriptor_t* desc = descriptor_read_from_bitreader(b);
    bitreader_free(b);
    return desc;
}

descriptor_t* descriptor_read_from_bitreader(bitreader_t* b)
{
    g_return_val_if_fail(b, NULL);

    descriptor_t* desc = descriptor_new();

    desc->tag = bitreader_read_uint8(b);
    desc->data_len = bitreader_read_uint8(b);
    desc->data = malloc(desc->data_len);
    bitreader_read_bytes(b, desc->data, desc->data_len);

    if (b->error) {
        g_critical("Descriptor length is invalid.");
        desc->tag = 0;
        goto fail;
    }

    switch (desc->tag) {
    case CA_DESCRIPTOR:
        desc = ca_descriptor_read(desc);
        break;
    }

cleanup:
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
        LOG_DEBUG(level, "tag: %"PRIu8, desc->tag);
        LOG_DEBUG(level, "length: %"PRIu8, desc->data_len);
    }
}

ca_descriptor_t* ca_descriptor_new(descriptor_t* desc)
{
    ca_descriptor_t* cad = g_slice_new0(ca_descriptor_t);
    cad->descriptor.tag = CA_DESCRIPTOR;
    if (desc != NULL) {
        cad->descriptor.data = desc->data;
        desc->data = NULL;
        cad->descriptor.data_len = desc->data_len;
    }
    g_slice_free(descriptor_t, desc);
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
}

descriptor_t* ca_descriptor_read(descriptor_t* desc)
{
    g_return_val_if_fail(desc, NULL);
    g_return_val_if_fail(desc->data, NULL);

    ca_descriptor_t* cad = ca_descriptor_new(desc);
    bitreader_t* b = bitreader_new(cad->descriptor.data, cad->descriptor.data_len);

    cad->ca_system_id = bitreader_read_uint16(b);
    bitreader_skip_bits(b, 3);
    cad->ca_pid = bitreader_read_bits(b, 13);
    if (b->error) {
        g_critical("CA descriptor invalid");
        goto fail;
    }
    cad->private_data_len = cad->descriptor.data_len - 4; // we just read 4 bytes
    cad->private_data = malloc(cad->private_data_len);
    bitreader_read_bytes(b, cad->private_data, cad->private_data_len);
    if (b->error) {
        g_critical("CA descriptor invalid");
        goto fail;
    }

cleanup:
    bitreader_free(b);
    return (descriptor_t*)cad;
fail:
    descriptor_free((descriptor_t*)cad);
    cad = NULL;
    goto cleanup;
}

void ca_descriptor_print(const descriptor_t* desc, int level)
{
    g_return_if_fail(desc);
    g_return_if_fail(desc->tag == CA_DESCRIPTOR);

    const ca_descriptor_t* cad = (const ca_descriptor_t*)desc;

    descriptor_print(desc, level);

    LOG_DEBUG(level, "ca_pid: %"PRIu16, cad->ca_pid);
    LOG_DEBUG(level, "ca_system_id: %"PRIu16, cad->ca_system_id);
}