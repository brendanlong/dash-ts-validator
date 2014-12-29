/*
 Copyright (c) 2014-, ISO/IEC JTC1/SC29/WG11

 Written by Alex Giladi <alex.giladi@gmail.com>

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of ISO/IEC nor the
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

#include <assert.h>
#include <stdbool.h>

#include "cas.h"
#include "libts_common.h"


ecm_pid_t* ecm_pid_new(int pid, elementary_stream_info_t* esi)
{
    ecm_pid_t* ep = malloc(sizeof(*ep));
    ep->pid = pid;
    ep->elementary_pids = g_ptr_array_new();
    g_ptr_array_add(ep->elementary_pids, esi);
    ep->ecm = NULL;
    return ep;
}

void ecm_pid_free(ecm_pid_t* ep)
{
    if (ep == NULL) {
        return;
    }
    ts_free(ep->ecm);
    g_ptr_array_free(ep->elementary_pids, true);
    free(ep);
}

ca_system_t* ca_system_new(int ca_system_id)
{
    ca_system_t* cas = calloc(1, sizeof(*cas));
    cas->id = ca_system_id;
    cas->ecm_pids = g_ptr_array_new_with_free_func((GDestroyNotify)ecm_pid_free);
    return cas;
}

void ca_system_free(ca_system_t* cas)
{
    if (cas == NULL) {
        return;
    }
    g_ptr_array_free(cas->ecm_pids, true);
    free(cas);
}

// FIXME: for now, we ignore EMMs
int ca_system_process_ts_packet(ts_packet_t* ts, elementary_stream_info_t* es_info, GPtrArray* cas_list)
{
    if (ts == NULL || cas_list == NULL) {
        return 0;
    }

    for (gsize i = 0; i < cas_list->len; ++i) {
        ca_system_t* cas = g_ptr_array_index(cas_list, i);
        for (gsize j  = 0; j < cas->ecm_pids->len; j++) {
            ecm_pid_t* ep = g_ptr_array_index(cas->ecm_pids, j);
            if(ep->pid == ts->header.pid) {
                ts_free(ep->ecm);
            }
            ep->ecm = ts;
            return 1;
        }
    }

    ts_free(ts);
    g_warning("PID 0x%04X does not appear to carry CAS-related information.", ts->header.pid);
    return 0;
}

int ca_system_process_ca_descriptor(GPtrArray* cas_list, elementary_stream_info_t* esi,
        ca_descriptor_t* cad)
{
    if (cas_list == NULL) {
        return 0;
    }

    ca_system_t* cas = NULL;
    for (gsize i = 0; i < cas_list->len; ++i) {
        cas = g_ptr_array_index(cas->ecm_pids, i);
        if (cas && cas->id == cad->ca_system_id) {
            break;
        }
    }

    if (cas == NULL) {
        // new CAS
        cas = ca_system_new(cad->ca_system_id) ;
    }

    if(esi == NULL) {
        // ca_descriptor is from CAT
        cas->emm_pid = cad->ca_pid;
        return 1;
    }

    // from here onwards this descriptor is per single ES

    ecm_pid_t* ep = NULL;
    for (gsize i = 0; i < cas->ecm_pids->len; i++) {
        ep = g_ptr_array_index(cas->ecm_pids, i);
        if (ep->pid == cad->ca_pid) {
            for (gsize j = 0; j < ep->elementary_pids->len; j++) {
                elementary_stream_info_t* tmp = g_ptr_array_index(ep->elementary_pids, i);
                if (tmp->elementary_pid == esi->elementary_pid) {
                    return 1;
                }
            }
        }
    }

    // we have a new ECM PID
    if (ep == NULL) {
        ep = ecm_pid_new(cad->ca_pid, esi);
        g_ptr_array_add(cas->ecm_pids, ep);
    }

    return 1;
}

ts_packet_t* ca_system_get_ecm(ca_system_t* cas, uint32_t ecm_pid)
{
    for(gsize i  = 0; i < cas->ecm_pids->len; ++i) {
        ecm_pid_t* ep = g_ptr_array_index(cas->ecm_pids, i);
        if(ep->pid == ecm_pid) {
            return ep->ecm;
        }
    }
    return NULL;
}