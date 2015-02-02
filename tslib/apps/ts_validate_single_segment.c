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
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include "log.h"

#include "segment_validator.h"


static struct option long_options[] = {
    { "verbose",	   no_argument,        NULL, 'v' },
    { "dash",	   optional_argument,  NULL, 'd' },
    { "byte-range", required_argument,  NULL, 'b' },
    { "help",       no_argument,        NULL, 'h' },
};

static char options[] =
    "\t-d, --dash\n"
    "\t-b, --byte-range\n"
    "\t-v, --verbose\n"
    "\t-h, --help\n";

static void usage(char* name)
{
    fprintf(stderr, "\n%s\n", name);
    fprintf(stderr, "\nUsage: \n%s [options] <input bitstream>\n\nOptions:\n%s\n", name, options);
}

int main(int argc, char* argv[])
{
    int c, long_options_index;
    extern char* optarg;
    extern int optind;

    dash_validator_t dash_validator;
    dash_validator_init(&dash_validator, MEDIA_SEGMENT, DASH_PROFILE_FULL);

    if(argc < 2) {
        usage(argv[0]);
        return 1;
    }

    while((c = getopt_long(argc, argv, "vdbh", long_options, &long_options_index)) != -1) {
        switch(c) {
        case 'd':
            if(optarg != NULL) {
                if (!strcmp(optarg, "simple")) {
                    dash_validator.profile = DASH_PROFILE_MPEG2TS_SIMPLE;
                } else if(!strcmp(optarg, "main")) {
                    dash_validator.profile = DASH_PROFILE_MPEG2TS_MAIN;
                }
            }
            break;
        case 'b':
            if(sscanf(optarg, "%ld-%ld", &dash_validator.segment_start, &dash_validator.segment_end) == 2) {
                if(dash_validator.segment_end < dash_validator.segment_start + TS_SIZE) {
                    g_critical("Invalid byte range %s", optarg);
                    return 1;
                }
            } else {
                g_critical("Invalid byte range %s", optarg);
                return 1;
            }
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

    data_segment_iframes_t* iframe_data = data_segment_iframes_new(1);
    int return_code = validate_segment(&dash_validator, fname, NULL, iframe_data,
                                         0 /* GORP: segment duration */);
    if (return_code != 0) {
        goto cleanup;
    }

    fprintf(stdout, "RESULT: %s\n", dash_validator.status ? "PASS" : "FAIL");

    // per PID

    pid_validator_t* pv = NULL;
    char* content_component_table[NUM_CONTENT_COMPONENTS] =
    { "<unknown>", "video", "audio" };

    for (gsize i = 0; i < dash_validator.pids->len; ++i) {
        pv = g_ptr_array_index(dash_validator.pids, i);
        g_info("%04X: %s EPT=%"PRId64" SAP=%d SAP Type=%d DURATION=%"PRId64"\n",
                      pv->pid, content_component_table[pv->content_component], pv->earliest_playout_time, pv->sap, pv->sap_type,
                      pv->latest_playout_time - pv->earliest_playout_time);

    }
cleanup:
    g_ptr_array_free(dash_validator.pids, true);
    data_segment_iframes_free(iframe_data, 1);
    return return_code;
}
