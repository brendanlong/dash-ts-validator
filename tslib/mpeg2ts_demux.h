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
#ifndef MPEG2TS_DEMUX_H
#define MPEG2TS_DEMUX_H

#include <glib.h>
#include <stdbool.h>

#include "bs.h"
#include "ts.h"
#include "pes.h"
#include "psi.h"
#include "descriptors.h"

struct _mpeg2ts_stream_;
struct _mpeg2ts_program_;

typedef int (*ts_pid_processor_t)(ts_packet_t*, elementary_stream_info_t*, void*);
typedef int (*pat_processor_t)(struct _mpeg2ts_stream_*, void*);
typedef int (*cat_processor_t)(struct _mpeg2ts_stream_*, void*);
typedef int (*pmt_processor_t)(struct _mpeg2ts_program_*, void*);

typedef void (*arg_destructor_t)(void*);

typedef struct {
    void* arg;                            // argument for ts packet processor
    arg_destructor_t arg_destructor;      // destructor for arg
    ts_pid_processor_t process_ts_packet; // ts packet processor, needs to be registered with mpeg2ts_program
} demux_pid_handler_t;

struct _mpeg2ts_program_ {
    uint16_t pid; // PMT PID
    uint16_t program_number;

    GHashTable* pids; // PIDs belonging to this program
    // each element is of type pid_info_t

    struct {
        int64_t first_pcr;
        int32_t num_rollovers;
        int64_t pcr[2];
        int32_t packets_from_last_pcr;
        double pcr_rate;
    } pcr_info; // information on STC clock state

    program_map_section_t* pmt;      // parsed PMT
    uint8_t* pmt_bytes;
    size_t pmt_len;
    pmt_processor_t pmt_processor;   // callback called after PMT was processed
    void* arg;                       // argument for PMT callback
    arg_destructor_t arg_destructor; // destructor for the callback argument
};

struct _mpeg2ts_stream_ {
    program_association_section_t* pat; // PAT
    uint8_t* pat_bytes;
    size_t pat_len;
    conditional_access_section_t* cat;  // CAT
    uint8_t* cat_bytes;
    size_t cat_len;
    pat_processor_t pat_processor;      // callback called after PAT was processed
    cat_processor_t cat_processor;      // callback called after CAT was processed
    demux_pid_handler_t* emsg_processor; // handler for 'emsg' packets
    demux_pid_handler_t* ts_processor;  // handler for all TS packets
    GPtrArray* programs;                // list of programs in this multiplex
    GPtrArray* ca_systems;              // list of conditional access systems in this multiplex
    void* arg;                          // argument for PAT/CAT callbacks
    arg_destructor_t arg_destructor;    // destructor for the callback argument
};

typedef struct _mpeg2ts_stream_  mpeg2ts_stream_t;
typedef struct _mpeg2ts_program_ mpeg2ts_program_t;

typedef struct {
    demux_pid_handler_t* demux_handler;   /// demux handler
    demux_pid_handler_t* demux_validator; /// demux validator
    // TODO: mux_pid_handler_t*
    elementary_stream_info_t* es_info;  /// ES-level information (type, descriptors)
    int continuity_counter;             /// running continuity counter
    uint64_t num_packets;
} pid_info_t;

mpeg2ts_stream_t* mpeg2ts_stream_new(void);
void mpeg2ts_stream_free(mpeg2ts_stream_t* m2s);
int mpeg2ts_stream_read_ts_packet(mpeg2ts_stream_t* m2s, ts_packet_t* ts);

mpeg2ts_program_t* mpeg2ts_program_new(uint16_t program_number, uint16_t pid);
void mpeg2ts_program_free(mpeg2ts_program_t* m2p);
int mpeg2ts_program_register_pid_processor(mpeg2ts_program_t* m2p, uint16_t pid,
        demux_pid_handler_t* handler, demux_pid_handler_t* validator);
int mpeg2ts_program_unregister_pid_processor(mpeg2ts_program_t* m2p, uint16_t pid);
int mpeg2ts_program_replace_pid_processor(mpeg2ts_program_t* m2p, pid_info_t* piNew);
void mpeg2ts_stream_reset(mpeg2ts_stream_t* m2s);

demux_pid_handler_t* demux_pid_handler_new(ts_pid_processor_t);

#endif
