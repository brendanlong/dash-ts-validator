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

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

#include "log.h"

int tslib_loglevel = TSLIB_LOG_LEVEL_DEFAULT;

#define INDENT_LEVEL	1
const char* IDENT = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

void skit_log_struct(size_t num_indents, char* name, const void* value, skit_log_type_t type, char* str)
{
    if(name == NULL) {
        return;
    }

    // get rid of prefixes
    char* last_dot = strrchr(name, '.');
    char* last_arrow = strrchr(name, '>');
    char* real_name = NULL;

    if (last_dot == NULL) {
        real_name = (last_arrow != NULL) ? last_arrow + 1 : name;
    } else {
        if(last_arrow != NULL) {
            real_name = (last_dot > last_arrow) ? last_dot : last_arrow;
        } else {
            real_name = last_dot;
        }
        real_name++;
    }

    if (num_indents >= sizeof IDENT) {
        g_critical("Too many indents - %zu", num_indents);
        num_indents = sizeof IDENT;
    }

    switch(type) {
    case SKIT_LOG_TYPE_UINT:
        g_info("%.*s%s=%"PRId64"", (int)num_indents, IDENT, real_name, *(const uint64_t*)value);
        break;
    case SKIT_LOG_TYPE_UINT_DBG:
        g_debug("%.*s%s=%"PRId64"", (int)num_indents, IDENT, real_name, *(const uint64_t*)value);
        break;
    case SKIT_LOG_TYPE_UINT_HEX:
        g_info("%.*s%s=0x%"PRIX64"", (int)num_indents, IDENT, real_name, *(const uint64_t*)value);
        break;
    case SKIT_LOG_TYPE_UINT_HEX_DBG:
        g_debug("%.*s%s=0x%"PRIX64"", (int)num_indents, IDENT, real_name, *(const uint64_t*)value);
        break;
    case SKIT_LOG_TYPE_STR:
        g_info("%.*s%s=%s", (int)num_indents, IDENT, real_name, (const char*)value);
        break;
    case SKIT_LOG_TYPE_STR_DBG:
        g_info("%.*s%s=%s", (int)num_indents, IDENT, real_name, (const char*)value);
        break;
    default:
        break;
    }

    if(str) {
        g_info(" (%s)", str);
    }
}

void log_handler(const char* domain, GLogLevelFlags log_level, const char* message, void* unused)
{
    if ((log_level & G_LOG_LEVEL_DEBUG) && tslib_loglevel < TSLIB_LOG_LEVEL_DEBUG) {
        return;
    } else if (((log_level & G_LOG_LEVEL_INFO) || (log_level & G_LOG_LEVEL_MESSAGE)) && tslib_loglevel < TSLIB_LOG_LEVEL_INFO) {
        return;
    } else if ((log_level & G_LOG_LEVEL_WARNING) && tslib_loglevel < TSLIB_LOG_LEVEL_WARN) {
        return;
    }
    g_print("%s\n", message);
}
