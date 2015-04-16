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
#include "pes_demux.h"


pes_demux_t* pes_demux_new(pes_processor_t pes_processor)
{
    pes_demux_t* pdm = g_new0(pes_demux_t, 1);
    pdm->ts_packets = g_array_new(false, false, sizeof(ts_packet_t));
    pdm->processor = pes_processor;
    return pdm;
}

void pes_demux_free(pes_demux_t* pdm)
{
    if (pdm == NULL) {
        return;
    }

    g_array_free(pdm->ts_packets, true);
    if (pdm->arg_destructor) {
        pdm->arg_destructor(pdm->arg);
    }
    g_free(pdm);
}

void pes_demux_process_ts_packet(ts_packet_t* new_ts, elementary_stream_info_t* es_info, void* arg)
{
    g_return_if_fail(arg);

    pes_demux_t* pdm = arg;

    // If we have a new payload, handle the existing PES data in the queue first
    if ((new_ts == NULL || new_ts->payload_unit_start_indicator) && pdm->ts_packets->len > 0) {
        // we have something in the queue
        // chances are this is a PES packet
        ts_packet_t* first_ts = &g_array_index(pdm->ts_packets, ts_packet_t, 0);
        if (first_ts->payload_unit_start_indicator == 0) {
            // the queue doesn't start with a complete TS packet
            g_critical("PES queue does not start from PUSI=1");
            // we'll do nothing and just clear the queue
            if (pdm->processor != NULL) {
                // at this point we don't own the PES packet memory
                pdm->processor(NULL, es_info, pdm->ts_packets, pdm->arg);
            }
        } else {
            size_t buf_size = 0;
            for (size_t i = 0; i < pdm->ts_packets->len; ++i) {
                ts_packet_t* ts = &g_array_index(pdm->ts_packets, ts_packet_t, i);
                buf_size += ts->payload_len;
            }
            GArray* buf = g_array_sized_new(false, false, 1, buf_size);

            bool first = true;
            uint64_t pos_in_stream = 0;
            for (size_t i = 0; i < pdm->ts_packets->len; ++i) {
                ts_packet_t* ts = &g_array_index(pdm->ts_packets, ts_packet_t, i);
                if (first) {
                    pos_in_stream = ts->pos_in_stream;
                    first = false;
                }

                if (ts != NULL && ts->has_payload) {
                    g_array_append_vals(buf, ts->payload, ts->payload_len);
                }
            }
            pes_packet_t* pes = pes_read((uint8_t*)buf->data, buf->len);
            if (pes) {
                pes->payload_pos_in_stream = pos_in_stream;
            }

            if (pdm->processor != NULL) {
                // at this point we don't own the PES packet memory
                pdm->processor(pes, es_info, pdm->ts_packets, pdm->arg);
            } else {
                pes_free(pes);
            }
            g_array_free(buf, true);
        }

        // Clear the queue
        g_array_set_size(pdm->ts_packets, 0);
    }

    // Push new packet on the queue
    if (new_ts != NULL) {
        size_t i = pdm->ts_packets->len;
        g_array_set_size(pdm->ts_packets, i + 1);
        ts_copy(&g_array_index(pdm->ts_packets, ts_packet_t, i), new_ts);
    }
}
