/*
 Copyright (c) 2015-, ISO/IEC JTC1/SC29/WG11
 All rights reserved.

 See AUTHORS for a full list of authors.

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
#include <check.h>
#include <getopt.h>
#include <glib.h>
#include <libxml/parser.h>

#include "log.h"
#include "test_common.h"

static struct option long_options[] = {
    { "verbose", no_argument, NULL, 'v' },
    { "help", no_argument, NULL, 'h' },
};

static char options[] =
    "\t-v, --verbose\n"
    "\t-h, --help\n";

static void usage(char* name)
{
    fprintf(stderr, "Usage: \n%s [options]\n\nOptions:\n%s\n", name,
            options);
}

int main(int argc, char** argv)
{
    tslib_loglevel = TSLIB_LOG_LEVEL_ERROR;
    int c, long_options_index;
    while((c = getopt_long(argc, argv, "vh", long_options, &long_options_index)) != -1) {
        switch(c) {
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

    int number_failed;
    Suite *s;
    SRunner *sr;

    s = suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    xmlCleanupParser();
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}