/*
 Copyright (c) 2012-, ISO/IEC JTC1/SC29/WG11
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
#include "libts_common.h"
#include "mpeg2ts_demux.h"
#include "cas.h"
#include "psi.h"
#include "segment_validator.h"


pid_info_t* pid_info_new()
{
    pid_info_t* pi = calloc(1, sizeof(pid_info_t));
    return pi;
}

void demux_pid_handler_free(demux_pid_handler_t* dph)
{
    if(dph == NULL) {
        return;
    }
    if(dph->arg && dph->arg_destructor) {
        dph->arg_destructor(dph->arg);
    }
    free(dph);
}

void pid_info_free(pid_info_t* pi)
{
    if(pi == NULL) {
        return;
    }
    demux_pid_handler_free(pi->demux_handler);
    demux_pid_handler_free(pi->demux_validator);

    // note: es_info is a reference, don't free it!
    //       the real thing is in pmt

    free(pi);
}

mpeg2ts_program_t* mpeg2ts_program_new(int program_number, int PID)
{
    mpeg2ts_program_t* m2p = calloc(1, sizeof(mpeg2ts_program_t));
    m2p->pids = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)pid_info_free);
    m2p->PID = PID;
    m2p->program_number = program_number;

    // initialize PCR state
    m2p->pcr_info.first_pcr = m2p->pcr_info.pcr[0] =  m2p->pcr_info.pcr[1] = INT64_MAX;
    m2p->pcr_info.pcr_rate = 0.0;

    return m2p;
}

void mpeg2ts_program_free(mpeg2ts_program_t* m2p)
{
    if(m2p == NULL) {
        return;
    }

    g_hash_table_destroy(m2p->pids);

    // GORP: if this is a test with an initialization segment, then dont want to free the pmt
    program_map_section_free(m2p->pmt);

    if(m2p->arg_destructor && m2p->arg) {
        m2p->arg_destructor(m2p->arg);
    }

    free(m2p);
}

int mpeg2ts_program_register_pid_processor(mpeg2ts_program_t* m2p, uint32_t PID,
        demux_pid_handler_t* handler, demux_pid_handler_t* validator)
{
    if(m2p->pmt == NULL || handler == NULL) {
        return 0;
    }

    pid_info_t* pid = pid_info_new();
    elementary_stream_info_t* esi = NULL;
    for(gsize i = 0; i < m2p->pmt->es_info->len; i++) {
        elementary_stream_info_t* tmp = g_ptr_array_index(m2p->pmt->es_info, i);
        if(tmp && tmp->elementary_PID == PID) {
            esi = tmp;
            break;
        }
    }
    if(esi == NULL) {
        LOG_ERROR_ARGS("Elementary stream with PID 0x%02X not found in PMT of program %d", PID,
                       m2p->program_number);
        free(pid);
        return 0;
    }
    pid->es_info = esi;
    pid->demux_handler = handler;
    if(validator != NULL) {
        pid->demux_validator = validator;
    }

    mpeg2ts_program_replace_pid_processor(m2p, pid);
    return 1;
}

int mpeg2ts_program_unregister_pid_processor(mpeg2ts_program_t* m2p, uint32_t PID)
{
    g_hash_table_remove(m2p->pids, GINT_TO_POINTER(PID));
    return 0;
}

int mpeg2ts_program_replace_pid_processor(mpeg2ts_program_t* m2p, pid_info_t* piNew)
{
    if(m2p == NULL) {
        return 0;
    }

    g_hash_table_replace(m2p->pids, GINT_TO_POINTER(piNew->es_info->elementary_PID), piNew);
    return 0;
}

pid_info_t* mpeg2ts_program_get_pid_info(mpeg2ts_program_t* m2p, uint32_t pid)
{
    return g_hash_table_lookup(m2p->pids, GINT_TO_POINTER(pid));
}

mpeg2ts_stream_t* mpeg2ts_stream_new()
{
    mpeg2ts_stream_t* m2s = calloc(1, sizeof(mpeg2ts_stream_t));
    m2s->programs = g_ptr_array_new_with_free_func((GDestroyNotify)mpeg2ts_program_free);
    return m2s;
}

void mpeg2ts_stream_free(mpeg2ts_stream_t* m2s)
{
    if(m2s == NULL) {
        return;
    }
    g_ptr_array_free(m2s->programs, true);
    if(m2s->pat != NULL) {
        program_association_section_free(m2s->pat);
    }
    if(m2s->arg_destructor && m2s->arg) {
        m2s->arg_destructor(m2s->arg);
    }
    free(m2s);
}

int mpeg2ts_stream_read_cat(mpeg2ts_stream_t* m2s, ts_packet_t* ts)
{
    int ret = 0;
    conditional_access_section_t* new_cas = conditional_access_section_new();

    if(new_cas == NULL) {
        goto cleanup;
    }

    if(conditional_access_section_read(new_cas, ts->payload.bytes + 1, ts->payload.len - 1) == 0) {
        conditional_access_section_free(new_cas);
        goto cleanup;
    }

    // FIXME: allow >1 packet cat
    if (!m2s->cat || (m2s->cat->version_number != new_cas->version_number
            && new_cas->current_next_indicator == 1)) {
        if(m2s->cat != NULL) {
            LOG_WARN("New cat section in force, discarding the old one");
            conditional_access_section_free(m2s->cat);
        }

        m2s->cat = new_cas;

        for(gsize i = 0; i < m2s->cat->descriptors->len; ++i) {
            ca_descriptor_t* cad = g_ptr_array_index(m2s->cat->descriptors, i);

            // we think it's a ca_descriptor, but we don't really know
            if(cad->descriptor.tag != CA_DESCRIPTOR) {
                continue;
            }

            ca_system_process_ca_descriptor(m2s->ca_systems, NULL, cad);

            // TODO: do something intelligent for EMM PIDs
        }
        if(m2s->cat_processor != NULL) {
            m2s->cat_processor(m2s, m2s->arg);
        }
    }

cleanup:
    ts_free(ts);
    return ret;
}

int mpeg2ts_stream_read_pat(mpeg2ts_stream_t* m2s, ts_packet_t* ts)
{
    int ret = 0;
    program_association_section_t* new_pas = program_association_section_new();

    if(new_pas == NULL) {
        goto cleanup;
    }

    if(program_association_section_read(new_pas, ts->payload.bytes + 1, ts->payload.len - 1) == 0) {
        program_association_section_free(new_pas);
        goto cleanup;
    }

    // FIXME: allow >1 packet PAT
    if(!m2s->pat || (m2s->pat->version_number != new_pas->version_number
            && new_pas->current_next_indicator == 1)) {
        if(m2s->pat != NULL) {
            LOG_WARN("New PAT section in force, discarding the old one");
            program_association_section_free(m2s->pat);
        }

        m2s->pat = new_pas;
        for(int i = 0; i < m2s->pat->_num_programs; i++) {
            mpeg2ts_program_t* prog = mpeg2ts_program_new(
                                          m2s->pat->programs[i].program_number,
                                          m2s->pat->programs[i].program_map_PID);
            g_ptr_array_add(m2s->programs, prog);
        }

        if(m2s->pat_processor) {
            m2s->pat_processor(m2s, m2s->arg);
        }
    } else {
        program_association_section_free(new_pas);
    }

cleanup:
    ts_free(ts);
    return ret;
}

int mpeg2ts_stream_read_dash_event_msg(mpeg2ts_stream_t* m2s, ts_packet_t* ts)
{
    doDASHEventValidation(ts->payload.bytes, ts->payload.len);

    // GORP: allow >1 packet event
    ts_free(ts);
    return 0;
}

int mpeg2ts_program_read_pmt(mpeg2ts_program_t* m2p, ts_packet_t* ts)
{
    int ret = 0;
    program_map_section_t* new_pms = program_map_section_new();

    if(new_pms == NULL) {
        goto cleanup;
    }

    if(program_map_section_read(new_pms, ts->payload.bytes + 1, ts->payload.len - 1) == 0) {
        program_map_section_free(new_pms);
        goto cleanup;
    }

    // FIXME: allow >1 packet PAT
    if(m2p->pmt == NULL || (m2p->pmt->version_number != new_pms->version_number
            && new_pms->current_next_indicator == 1)) {
        if(m2p->pmt != NULL) {
            LOG_WARN("New PMT in force, discarding the old one");
            program_map_section_free(m2p->pmt);
        }

        m2p->pmt = new_pms;

        for(int es_idx = 0; es_idx < m2p->pmt->es_info->len; es_idx++) {
            elementary_stream_info_t* es = g_ptr_array_index(m2p->pmt->es_info, es_idx);
            pid_info_t* pi = pid_info_new();
            pi->es_info = es;
            g_hash_table_insert(m2p->pids, GINT_TO_POINTER(pi->es_info->elementary_PID), pi);
        }

        if(m2p->pmt_processor != NULL) {
            m2p->pmt_processor(m2p, m2p->arg);
        }
    } else {
        program_map_section_free(new_pms);
    }

cleanup:
    ts_free(ts);
    return ret;
}

int mpeg2ts_stream_reset(mpeg2ts_stream_t* m2s)
{
    int pid_cnt = 0;
    for(gsize i = 0; i < m2s->programs->len; ++i) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);

        GHashTableIter j;
        g_hash_table_iter_init(&j, m2p->pids);
        pid_info_t* pi;
        while (g_hash_table_iter_next (&j, NULL, (void**)&pi)) {
            ++pid_cnt;
            if(pi->demux_validator && pi->demux_validator->process_ts_packet) {
                pi->demux_validator->process_ts_packet(NULL, pi->es_info, pi->demux_handler->arg);
            }
            if(pi->demux_handler && pi->demux_handler->process_ts_packet) {
                pi->demux_handler->process_ts_packet(NULL, pi->es_info, pi->demux_handler->arg);
            }
        }
    }
    return pid_cnt;
}

int mpeg2ts_stream_read_ts_packet(mpeg2ts_stream_t* m2s, ts_packet_t* ts)
{
    if(ts == NULL) {
        mpeg2ts_stream_reset(m2s);
        return 0;
    }

    if(ts->header.PID == PAT_PID) {
        return mpeg2ts_stream_read_pat(m2s, ts);
    }
    if(ts->header.PID == CAT_PID) {
        return mpeg2ts_stream_read_cat(m2s, ts);
    }
    if(ts->header.PID == DASH_PID) {
        return mpeg2ts_stream_read_dash_event_msg(m2s, ts);
    }
    if(ts->header.PID == NULL_PID) {
        ts_free(ts);
        return 0;
    }

    if(m2s->pat == NULL) {
        LOG_ERROR_ARGS("PAT missing -- unknown PID 0x%02X", ts->header.PID);
        ts_free(ts);
        return 0;
    }

    for(gsize i = 0; i < m2s->programs->len; ++i) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);

        if(m2p->PID == ts->header.PID) {
            return mpeg2ts_program_read_pmt(m2p, ts);    // got a PMT
        }

        // pi == NULL => this PID does not belong to this program
        pid_info_t* pi = mpeg2ts_program_get_pid_info(m2p, ts->header.PID);
        if (pi == NULL) {
            continue;
        }

        // check for discontinuity
        pi->num_packets++;

        // FIXME: this can misfire if we have an MPTS and same PID is "owned" by more than one program
        // this is an *extremely unlikely* case
        if((pi->demux_validator != NULL) && (pi->demux_validator->process_ts_packet != NULL)) {
            // TODO: check return value and do something intelligent
            pi->demux_validator->process_ts_packet(ts, pi->es_info, pi->demux_handler->arg);
        }

        if((pi->demux_handler != NULL) && (pi->demux_handler->process_ts_packet != NULL)) {
            return pi->demux_handler->process_ts_packet(ts, pi->es_info, pi->demux_handler->arg);
        }
        break;
    }

    // if we are here, we have no clue what this PID is
    LOG_WARN_ARGS("Unknown PID 0x%02X", ts->header.PID);
    ts_free(ts);
    return 0;
}