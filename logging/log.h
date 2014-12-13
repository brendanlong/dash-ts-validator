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
#ifndef STREAMKIT_LOG_H
#define STREAMKIT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <glib.h>

// we need those for inttypes.h in C++
#if defined __cplusplus && !defined __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif

#define SKIT_LOG_TYPE_UINT			0x01
#define SKIT_LOG_TYPE_UINT_HEX		0x02
#define SKIT_LOG_TYPE_STR			0x03

#define SKIT_LOG_TYPE_UINT_DBG		0x04
#define SKIT_LOG_TYPE_UINT_HEX_DBG	0x05
#define SKIT_LOG_TYPE_STR_DBG		0x06

#define SKIT_LOG_UINT32_DBG(prefix, arg)  fprintf(stdout, "DEBUG: %s%s=%"PRIu32"\n", prefix, #arg, (arg));
#define SKIT_LOG_UINT32_HEX_DBG(prefix, arg)  fprintf(stdout, "DEBUG: %s%s=%"PRIX32"\n", prefix, #arg, (arg));
#define SKIT_LOG_UINT64_DBG(prefix, arg)  fprintf(stdout, "DEBUG: %s%s=%"PRIu64"\n", prefix, #arg, (arg));

#define SKIT_LOG_UINT(level, arg) skit_log_struct((level), #arg,  (arg), SKIT_LOG_TYPE_UINT, NULL);
#define SKIT_LOG_UINT_DBG(level, arg) skit_log_struct((level), #arg,  (arg), SKIT_LOG_TYPE_UINT_DBG, NULL);
#define SKIT_LOG_UINT_HEX(level, arg) skit_log_struct((level), #arg,  (arg), SKIT_LOG_TYPE_UINT_HEX, NULL);
#define SKIT_LOG_UINT_HEX_DBG(level, arg) skit_log_struct((level), #arg,  (arg), SKIT_LOG_TYPE_UINT_HEX_DBG, NULL);
#define SKIT_LOG_UINT_VERBOSE(level, arg, explain) skit_log_struct((level), #arg,  (arg), SKIT_LOG_TYPE_UINT, explain);
#define SKIT_LOG_STR(level, arg) skit_log_struct((level), #arg, arg, SKIT_LOG_TYPE_STR, NULL);
#define SKIT_LOG_STR_DBG(level, arg) skit_log_struct((level), #arg, arg, SKIT_LOG_TYPE_STR_DBG, NULL);

void skit_log_struct(int level, char* name, uint64_t value, int type, char* str);

// More traditional debug logging
// tslib-global loglevel: error > warn (default) > info > debug
typedef enum {
    TSLIB_LOG_LEVEL_ERROR = 1,
    TSLIB_LOG_LEVEL_WARN,
    TSLIB_LOG_LEVEL_INFO,
    TSLIB_LOG_LEVEL_DEBUG
} tslib_log_level_t;

#define TSLIB_LOG_LEVEL_DEFAULT TSLIB_LOG_LEVEL_WARN

extern int tslib_loglevel;

#define PRINT_STR(str) (str ? (const char*)str : "(null)")
#define PRINT_BOOL(b) (b ? "true" : "false")

void log_handler(const char* domain, GLogLevelFlags log_level, const char* message, void*);

#ifdef __cplusplus
}
#endif

#endif