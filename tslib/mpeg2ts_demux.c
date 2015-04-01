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
#include "mpeg2ts_demux.h"

#include <glib.h>
#include "libts_common.h"
#include "psi.h"
#include "segment_validator.h"


demux_pid_handler_t* demux_pid_handler_new(ts_pid_processor_t process_ts_packet)
{
    demux_pid_handler_t* obj = g_new0(demux_pid_handler_t, 1);
    obj->process_ts_packet = process_ts_packet;
    return obj;
}

static void demux_pid_handler_free(demux_pid_handler_t* obj)
{
    if (obj == NULL) {
        return;
    }
    if (obj->arg_destructor && obj->arg) {
        obj->arg_destructor(obj->arg);
    }
    g_free(obj);
}

static pid_info_t* pid_info_new(void)
{
    pid_info_t* obj = g_new0(pid_info_t, 1);
    return obj;
}

static void pid_info_free(pid_info_t* obj)
{
    if (obj == NULL) {
        return;
    }
    demux_pid_handler_free(obj->demux_handler);
    demux_pid_handler_free(obj->demux_validator);

    // note: es_info is a reference, don't free it!
    //       the real thing is in pmt

    g_free(obj);
}

mpeg2ts_program_t* mpeg2ts_program_new(uint16_t program_number, uint16_t pid)
{
    mpeg2ts_program_t* m2p = g_new0(mpeg2ts_program_t, 1);
    m2p->pids = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)pid_info_free);
    m2p->pid = pid;
    m2p->program_number = program_number;

    // initialize PCR state
    m2p->pcr_info.first_pcr = m2p->pcr_info.pcr[0] =  m2p->pcr_info.pcr[1] = INT64_MAX;
    m2p->pcr_info.pcr_rate = 0.0;

    return m2p;
}

void mpeg2ts_program_free(mpeg2ts_program_t* m2p)
{
    if (m2p == NULL) {
        return;
    }

    g_hash_table_destroy(m2p->pids);

    // TODO: if this is a test with an initialization segment, then dont want to free the pmt
    program_map_section_free(m2p->pmt);
    g_free(m2p->pmt_bytes);

    if (m2p->arg_destructor && m2p->arg) {
        m2p->arg_destructor(m2p->arg);
    }
    g_free(m2p);
}

int mpeg2ts_program_register_pid_processor(mpeg2ts_program_t* m2p, uint16_t pid,
        demux_pid_handler_t* handler, demux_pid_handler_t* validator)
{
    g_return_val_if_fail(m2p, 0);
    g_return_val_if_fail(m2p->pmt, 0);
    g_return_val_if_fail(handler, 0);

    pid_info_t* pi = pid_info_new();
    elementary_stream_info_t* esi = NULL;
    for (size_t i = 0; i < m2p->pmt->es_info_len; i++) {
        elementary_stream_info_t* tmp = m2p->pmt->es_info[i];
        if (tmp && tmp->elementary_pid == pid) {
            esi = tmp;
            break;
        }
    }
    if (esi == NULL) {
        g_critical("Elementary stream with PID 0x%02X not found in PMT of program %d", pid,
                       m2p->program_number);
        free(pi);
        return 0;
    }
    pi->es_info = esi;
    pi->demux_handler = handler;
    if (validator != NULL) {
        pi->demux_validator = validator;
    }

    mpeg2ts_program_replace_pid_processor(m2p, pi);
    return 1;
}

int mpeg2ts_program_unregister_pid_processor(mpeg2ts_program_t* m2p, uint16_t pid)
{
    g_hash_table_remove(m2p->pids, GINT_TO_POINTER(pid));
    return 0;
}

int mpeg2ts_program_replace_pid_processor(mpeg2ts_program_t* m2p, pid_info_t* pi_new)
{
    if (m2p == NULL) {
        return 0;
    }

    g_hash_table_replace(m2p->pids, GINT_TO_POINTER(pi_new->es_info->elementary_pid), pi_new);
    return 0;
}

static pid_info_t* mpeg2ts_program_get_pid_info(mpeg2ts_program_t* m2p, uint32_t pid)
{
    return g_hash_table_lookup(m2p->pids, GINT_TO_POINTER(pid));
}

mpeg2ts_stream_t* mpeg2ts_stream_new(void)
{
    mpeg2ts_stream_t* m2s = g_new0(mpeg2ts_stream_t, 1);
    m2s->programs = g_ptr_array_new_with_free_func((GDestroyNotify)mpeg2ts_program_free);
    return m2s;
}

void mpeg2ts_stream_free(mpeg2ts_stream_t* m2s)
{
    if (m2s == NULL) {
        return;
    }
    g_ptr_array_free(m2s->programs, true);
    conditional_access_section_free(m2s->cat);
    g_free(m2s->cat_bytes);
    program_association_section_free(m2s->pat);
    g_free(m2s->pat_bytes);
    demux_pid_handler_free(m2s->emsg_processor);
    demux_pid_handler_free(m2s->ts_processor);
    if (m2s->arg_destructor && m2s->arg) {
        m2s->arg_destructor(m2s->arg);
    }
    g_free(m2s);
}

static int mpeg2ts_stream_read_cat(mpeg2ts_stream_t* m2s, ts_packet_t* ts)
{
    g_return_val_if_fail(m2s, 1);
    g_return_val_if_fail(ts, 1);

    if (!ts->payload.bytes || !ts->payload.len) {
        g_critical("mpeg2ts_program_read_pat called with an empty TS payload.");
        return 1;
    }

    int ret = 0;
    conditional_access_section_t* new_cas = conditional_access_section_read(ts->payload.bytes + 1,
            ts->payload.len - 1);
    if (new_cas == NULL) {
        ret = 1;
        goto cleanup;
    }

    // TODO: allow >1 packet cat
    if (!conditional_access_sections_equal(m2s->cat, new_cas)) {
        if (m2s->cat != NULL) {
            g_info("New cat section in force, discarding the old one");
            conditional_access_section_free(m2s->cat);
        }

        m2s->cat = new_cas;

        if (m2s->cat_processor != NULL) {
            m2s->cat_processor(m2s, m2s->arg);
        }
    }

cleanup:
    ts_free(ts);
    return ret;
}

static int mpeg2ts_stream_read_pat(mpeg2ts_stream_t* m2s, ts_packet_t* ts)
{
    g_return_val_if_fail(m2s, 1);
    g_return_val_if_fail(ts, 1);

    if (!ts->payload.bytes || !ts->payload.len) {
        g_critical("mpeg2ts_program_read_pat called with an empty TS payload.");
        return 1;
    }

    int ret = 0;
    program_association_section_t* new_pas = program_association_section_read(ts->payload.bytes, ts->payload.len);
    if (new_pas == NULL) {
        ret = 1;
        goto cleanup;
    }

    // TODO: allow >1 packet PAT
    if (!program_association_sections_equal(m2s->pat, new_pas)) {
        if (m2s->pat != NULL) {
            g_warning("New PAT section in force, discarding the old one");
            program_association_section_free(m2s->pat);
        }

        m2s->pat = new_pas;
        for (gsize i = 0; i < m2s->pat->num_programs; i++) {
            mpeg2ts_program_t* prog = mpeg2ts_program_new(
                    m2s->pat->programs[i].program_number,
                    m2s->pat->programs[i].program_map_pid);
            g_ptr_array_add(m2s->programs, prog);
        }

        if (m2s->pat_processor) {
            m2s->pat_processor(m2s, m2s->arg);
        }
    } else {
        program_association_section_free(new_pas);
    }

cleanup:
    ts_free(ts);
    return ret;
}

static int mpeg2ts_stream_read_dash_event_msg(mpeg2ts_stream_t* m2s, ts_packet_t* ts)
{
    if (m2s->emsg_processor && m2s->emsg_processor->process_ts_packet) {
        m2s->emsg_processor->process_ts_packet(ts, NULL, m2s->emsg_processor->arg);
    }
    ts_free(ts);
    return 0;
}

static int mpeg2ts_program_read_pmt(mpeg2ts_program_t* m2p, ts_packet_t* ts)
{
    g_return_val_if_fail(m2p, 1);
    g_return_val_if_fail(ts, 1);

    if (!ts->payload.bytes || !ts->payload.len) {
        g_critical("mpeg2ts_program_read_pmt called with an empty TS payload.");
        return 1;
    }

    int ret = 0;
    program_map_section_t* new_pms = program_map_section_read(ts->payload.bytes, ts->payload.len);
    if (new_pms == NULL) {
        ret = 1;
        goto cleanup;
    }

    // TODO: allow >1 packet PAT
    if (!program_map_sections_equal(m2p->pmt, new_pms)) {
        if (m2p->pmt != NULL) {
            g_info("New PMT in force, discarding the old one");
            g_hash_table_remove_all(m2p->pids);
            program_map_section_free(m2p->pmt);
        }
        m2p->pmt = new_pms;

        for (size_t es_idx = 0; es_idx < m2p->pmt->es_info_len; es_idx++) {
            elementary_stream_info_t* es = m2p->pmt->es_info[es_idx];
            pid_info_t* pi = pid_info_new();
            pi->es_info = es;
            g_hash_table_insert(m2p->pids, GINT_TO_POINTER(pi->es_info->elementary_pid), pi);
        }

        if (m2p->pmt_processor != NULL) {
            m2p->pmt_processor(m2p, m2p->arg);
        }
    } else {
        program_map_section_free(new_pms);
    }

cleanup:
    ts_free(ts);
    return ret;
}

void mpeg2ts_stream_reset(mpeg2ts_stream_t* m2s)
{
    for (gsize i = 0; i < m2s->programs->len; ++i) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);

        GHashTableIter j;
        g_hash_table_iter_init(&j, m2p->pids);
        pid_info_t* pi;
        while (g_hash_table_iter_next (&j, NULL, (void**)&pi)) {
            if (pi->demux_validator && pi->demux_validator->process_ts_packet) {
                pi->demux_validator->process_ts_packet(NULL, pi->es_info, pi->demux_handler->arg);
            }
            if (pi->demux_handler && pi->demux_handler->process_ts_packet) {
                pi->demux_handler->process_ts_packet(NULL, pi->es_info, pi->demux_handler->arg);
            }
        }
    }
}

int mpeg2ts_stream_read_ts_packet(mpeg2ts_stream_t* m2s, ts_packet_t* ts)
{
    if (ts == NULL) {
        mpeg2ts_stream_reset(m2s);
        return 0;
    }
    if (m2s->ts_processor && m2s->ts_processor->process_ts_packet) {
        m2s->ts_processor->process_ts_packet(ts, NULL, m2s->ts_processor->arg);
    }

    if (ts->header.pid == PID_PAT) {
        return mpeg2ts_stream_read_pat(m2s, ts);
    }
    if (ts->header.pid == PID_CAT) {
        return mpeg2ts_stream_read_cat(m2s, ts);
    }
    if (ts->header.pid == PID_DASH_EMSG) {
        return mpeg2ts_stream_read_dash_event_msg(m2s, ts);
    }
    if (ts->header.pid == PID_NULL) {
        ts_free(ts);
        return 0;
    }

    if (m2s->pat == NULL) {
        g_info("PAT missing -- unknown PID 0x%02X", ts->header.pid);
        ts_free(ts);
        return 0;
    }

    for (gsize i = 0; i < m2s->programs->len; ++i) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);

        if (m2p->pid == ts->header.pid) {
            return mpeg2ts_program_read_pmt(m2p, ts);    // got a PMT
        }

        pid_info_t* pi = mpeg2ts_program_get_pid_info(m2p, ts->header.pid);

        // pi == NULL => this PID does not belong to this program
        if (pi == NULL) {
            continue;
        }

        // TODO: check for discontinuity

        pi->num_packets++;

        // TODO: this can misfire if we have an MPTS and same PID is "owned" by more than one program
        // this is an *extremely unlikely* case
        if (pi->demux_validator != NULL && pi->demux_validator->process_ts_packet != NULL) {
            // TODO: check return value and do something intelligent
            pi->demux_validator->process_ts_packet(ts, pi->es_info, pi->demux_validator->arg);
        }

        if(pi->demux_handler != NULL && pi->demux_handler->process_ts_packet != NULL) {
            return pi->demux_handler->process_ts_packet(ts, pi->es_info, pi->demux_handler->arg);
        }
        break;
    }

    // if we are here, we have no clue what this PID is
    g_debug("Unknown PID 0x%02X", ts->header.pid);
    ts_free(ts);
    return 0;
}