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
#include <glib.h>
#include <getopt.h>
#include <errno.h>
#include <strings.h>
#include <stdio.h>

#include "libts_common.h"
#include "log.h"
#include "mpeg2ts_demux.h"
#include "pes.h"
#include "psi.h"
#include "tpes.h"
#include "ts.h"


char* prefix = NULL;

static struct option long_options[] = {
    { "verbose",	no_argument,        NULL, 'v' },
    { "prefix",	required_argument,  NULL, 'p' },
    { "help",    no_argument,        NULL, 'h' },
};

static char options[] =
    "\t-v, --verbose\n"
    "\t-h, --help\n";

static void usage(char* name)
{
    fprintf(stderr, "\n%s\n", name);
    fprintf(stderr, "\nUsage: \n%s [options] <input bitstream>\n\nOptions:\n%s\n", name, options);
}

int pes_processor(pes_packet_t* pes, elementary_stream_info_t* esi, GQueue* ts_queue, void* arg)
{
    if(pes == NULL || esi == NULL) {
        return 0;
    }

    FILE* fout = (FILE*)arg;
#if 0
    if(tslib_loglevel > 0) {
        pes_print(pes);
        fprintf(stderr, "\n%s\n", pes_str);
    }
#else
    //pes_print(pes);
#endif

    if(fout != NULL) {
        fwrite(pes->payload, pes->payload_len, 1, fout);
    }

    pes_free(pes);
    return 1;
}

int pmt_processor_split(mpeg2ts_program_t* m2p, void* arg)
{
    if(m2p == NULL || m2p->pmt == NULL) {  // if we don't have any PSI, there's nothing we can do
        return 0;
    }

    program_map_section_print(m2p->pmt);

    GHashTableIter i;
    g_hash_table_iter_init(&i, m2p->pids);
    pid_info_t* pi;
    while (g_hash_table_iter_next(&i, NULL, (void**)&pi)) {
        char filename[0x100];
        int process_pid = 0;

        switch(pi->es_info->stream_type) {
        case STREAM_TYPE_MPEG2_VIDEO:
            process_pid = 1;
            snprintf(filename, 0x100, "%s_video_%04X.%s", prefix == NULL ? "track" : prefix,
                     pi->es_info->elementary_pid, "mpg");
            break;
        case STREAM_TYPE_AVC:
            process_pid = 1;
            snprintf(filename, 0x100, "%s_video_%04X.%s", prefix == NULL ? "track" : prefix,
                     pi->es_info->elementary_pid, "264");
            break;
        default:
            process_pid = 0;
        }

        if(process_pid) {
            FILE* fout = fopen(filename, "wb");

            // hook PES writing to PES demuxer
            pes_demux_t* pd = pes_demux_new(pes_processor);
            pd->pes_arg = fout;
            pd->pes_arg_destructor = (pes_arg_destructor_t)fclose;
            //pd->process_pes_packet = pes_processor;

            // hook PES demuxer to the PID processor
            demux_pid_handler_t* demux_handler = calloc(1, sizeof(demux_pid_handler_t));
            demux_handler->process_ts_packet = pes_demux_process_ts_packet;
            demux_handler->arg = pd;
            demux_handler->arg_destructor = (arg_destructor_t)pes_demux_free;

            // hook PID processor to PID
            mpeg2ts_program_register_pid_processor(m2p, pi->es_info->elementary_pid, demux_handler, NULL);
        }
    }
    return 1;
}

int pat_processor_split(mpeg2ts_stream_t* m2s, void* arg)
{
    for(gsize i = 0; i < m2s->programs->len; ++i) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);
        m2p->pmt_processor = pmt_processor_split;
    }
    return 1;
}

int main(int argc, char* argv[])
{
    int c, long_options_index;
    extern char* optarg;
    extern int optind;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    while((c = getopt_long(argc, argv, "vph", long_options, &long_options_index)) != -1) {
        switch(c) {
        case 'p':
            prefix = optarg;
            break;
        case 'v':
            if(tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) {
                tslib_loglevel++;
            }
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 1;
        }
    }

    g_log_set_default_handler(log_handler, NULL);

    char* fname = argv[optind];
    if (fname == NULL || fname[0] == 0) {
        g_critical("No input file provided");
        usage(argv[0]);
        return 1;
    }

    FILE* infile = fopen(fname, "rb");
    if (infile == NULL) {
        g_critical("Cannot open file %s - %s", fname, strerror(errno));
        return 1;
    }

    mpeg2ts_stream_t* m2s = mpeg2ts_stream_new();
    if (m2s == NULL) {
        g_critical("Error creating MPEG-2 STREAM object");
        return 1;
    }

    m2s->pat_processor = pat_processor_split;

    int num_packets = 4096;
    uint8_t* ts_buf = malloc(TS_SIZE * 4096);
    uint64_t packets_read = 0;

    while ((num_packets = fread(ts_buf, TS_SIZE, 4096, infile)) > 0) {
        for (int i = 0; i < num_packets; i++) {
            ts_packet_t* ts = ts_new();
            ts_read(ts, ts_buf + i * TS_SIZE, TS_SIZE, packets_read);
            mpeg2ts_stream_read_ts_packet(m2s, ts);
            packets_read++;
        }
    }

    mpeg2ts_stream_free(m2s);
    fclose(infile);
    return tslib_errno;
}
