/*
 Copyright (c) 2014-, ISO/IEC JTC1/SC29/WG11
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
#include "mpd.h"

#include <gio/gio.h>
#include <inttypes.h>
#include <libxml/tree.h>
#include <pcre.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"


/* Ignore warnings for implicitly casting const char* to const xmlChar* (i.e. const unsigned char*) */
#pragma GCC diagnostic ignored "-Wpointer-sign"

#define LOG_RANGE(indent, owner, property_name) \
if (owner->property_name##_start != 0 || owner->property_name##_end != 0) { \
    LOG_DEBUG(indent, #property_name ": %"PRIu64"-%"PRIu64, owner->property_name##_start, \
            owner->property_name##_end); \
}

#define MPEG_TS_TIMESCALE 90000

typedef struct {
    uint64_t start;
    uint64_t duration;
} segment_timeline_s_t;


static bool read_period(xmlNode*, mpd_t*, char* base_url);
static bool read_adaptation_set(xmlNode*, period_t*, char* base_url, GPtrArray* segment_bases);
static bool read_representation(xmlNode*, adaptation_set_t*, char* base_url, GPtrArray* segment_bases);
static bool read_subrepresentation(xmlNode*, representation_t*);
static bool read_segment_base(xmlNode*, representation_t*, char* base_url, GPtrArray* segment_bases);
static bool read_segment_list(xmlNode*, representation_t*, char* base_url, GPtrArray* segment_bases);
static GPtrArray* read_segment_timeline(xmlNode*, representation_t*);
static bool read_segment_url(xmlNode*, representation_t*, uint64_t start, uint64_t duration, char* base_url,
        GPtrArray* segment_bases);
static bool read_segment_template(xmlNode*, representation_t*, char* base_url, GPtrArray* segment_bases);
static char* find_base_url(xmlNode*, char* parent_base_url);
static optional_uint32_t read_optional_uint32(xmlNode*, const char* property_name);
static void print_optional_uint32(int indent, const char* name, optional_uint32_t value);
static uint32_t read_uint64(xmlNode*, const char* property_name);
static bool read_bool(xmlNode*, const char* property_name);
static char* read_filename(xmlNode*, const char* property_name, const char* base_url);
static uint64_t str_to_uint64(const char*, size_t length, int* error);
static uint64_t str_to_int64(const char*, size_t length, int* error);
static dash_profile_t read_profile(xmlNode*, dash_profile_t parent_profile);
static const char* dash_profile_to_string(dash_profile_t);
static bool read_range(xmlNode*, const char* property_name, uint64_t* start_out, uint64_t* end_out);
static uint64_t convert_timescale(uint64_t time, uint64_t timescale);
static uint64_t convert_timescale_to(uint64_t time, uint64_t from_timescale, uint64_t to_timescale);
static uint64_t read_duration(xmlNode*, const char* property_name);
static xmlNode* find_segment_base(xmlNode*);

const char INDENT_BUFFER[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";


mpd_t* mpd_new(void)
{
    mpd_t* obj = calloc(1, sizeof(*obj));
    obj->periods = g_ptr_array_new_with_free_func((GDestroyNotify)period_free);
    return obj;
}

void mpd_free(mpd_t* obj)
{
    if (obj == NULL) {
        return;
    }

    g_ptr_array_free(obj->periods, true);

    free(obj);
}

void mpd_print(const mpd_t* mpd)
{
    g_return_if_fail(mpd);

    g_debug("MPD:");
    unsigned indent = 1;

    const char* mpd_type = NULL;
    switch (mpd->presentation_type) {
    case MPD_PRESENTATION_STATIC:
        mpd_type = "static";
        break;
    case MPD_PRESENTATION_DYNAMIC:
        mpd_type = "dynamic";
        break;
    default:
        /* nothing */
        break;
    }
    if (mpd_type != NULL) {
        LOG_DEBUG(indent, "presentation_type: %s", mpd_type);
    } else {
        LOG_DEBUG(indent, "presentation_type: %d", mpd->presentation_type);
    }
    LOG_DEBUG(indent, "profile: %s", dash_profile_to_string(mpd->profile));
    LOG_DEBUG(indent, "duration: %"PRIu64, mpd->duration);

    for (size_t i = 0; i < mpd->periods->len; ++i) {
        LOG_DEBUG(indent, "periods[%zu]:", i);
        period_print(g_ptr_array_index(mpd->periods, i), indent);
    }
}

period_t* period_new(mpd_t* mpd)
{
    period_t* obj = calloc(1, sizeof(*obj));
    obj->mpd = mpd;
    obj->adaptation_sets = g_ptr_array_new_with_free_func((GDestroyNotify)adaptation_set_free);
    return obj;
}

void period_free(period_t* obj)
{
    if(obj == NULL) {
        return;
    }

    g_ptr_array_free(obj->adaptation_sets, true);

    free(obj);
}

void period_print(const period_t* period, unsigned indent)
{
    g_return_if_fail(period);

    ++indent;
    LOG_DEBUG(indent, "bitstream_switching: %s", BOOL_TO_STR(period->bitstream_switching));
    LOG_DEBUG(indent, "duration: %"PRIu64, period->duration);
    for (size_t i = 0; i < period->adaptation_sets->len; ++i) {
        LOG_DEBUG(indent, "adaptation_sets[%zu]:", i);
        adaptation_set_print(g_ptr_array_index(period->adaptation_sets, i), indent + 1);
    }
}

adaptation_set_t* adaptation_set_new(period_t* period)
{
    adaptation_set_t* obj = calloc(1, sizeof(*obj));
    obj->period = period;
    obj->representations = g_ptr_array_new_with_free_func((GDestroyNotify)representation_free);
    return obj;
}

void adaptation_set_free(adaptation_set_t* obj)
{
    if(obj == NULL) {
        return;
    }

    xmlFree(obj->mime_type);
    g_ptr_array_free(obj->representations, true);

    free(obj);
}

void adaptation_set_print(const adaptation_set_t* adaptation_set, unsigned indent)
{
    g_return_if_fail(adaptation_set);

    LOG_DEBUG(indent, "id: %"PRIu32, adaptation_set->id);
    LOG_DEBUG(indent, "mime_type: %s", adaptation_set->mime_type);
    LOG_DEBUG(indent, "profile: %s", dash_profile_to_string(adaptation_set->profile));
    LOG_DEBUG(indent, "audio_pid: %"PRIu32, adaptation_set->audio_pid);
    LOG_DEBUG(indent, "video_pid: %"PRIu32, adaptation_set->video_pid);
    print_optional_uint32(indent, "segment_alignment", adaptation_set->segment_alignment);
    print_optional_uint32(indent, "subsegment_alignment", adaptation_set->subsegment_alignment);
    LOG_DEBUG(indent, "bitstream_switching: %s", BOOL_TO_STR(adaptation_set->bitstream_switching));
    for (size_t i = 0; i < adaptation_set->representations->len; ++i) {
        LOG_DEBUG(indent, "representations[%zu]:", i);
        representation_print(g_ptr_array_index(adaptation_set->representations, i), indent + 1);
    }
}

representation_t* representation_new(adaptation_set_t* adaptation_set)
{
    representation_t* obj = calloc(1, sizeof(*obj));
    obj->timescale = 1;
    obj->start_number = 1;
    obj->adaptation_set = adaptation_set;
    obj->subrepresentations = g_ptr_array_new_with_free_func((GDestroyNotify)subrepresentation_free);
    obj->segments = g_ptr_array_new_with_free_func((GDestroyNotify)segment_free);
    return obj;
}

void representation_free(representation_t* obj)
{
    if(obj == NULL) {
        return;
    }

    xmlFree(obj->id);
    xmlFree(obj->mime_type);
    g_free(obj->index_file_name);
    g_free(obj->initialization_file_name);
    g_free(obj->bitstream_switching_file_name);
    g_ptr_array_free(obj->subrepresentations, true);
    g_ptr_array_free(obj->segments, true);

    free(obj);
}

void representation_print(const representation_t* representation, unsigned indent)
{
    g_return_if_fail(representation);

    LOG_DEBUG(indent, "profile: %s", dash_profile_to_string(representation->profile));
    LOG_DEBUG(indent, "id: %s", representation->id);
    LOG_DEBUG(indent, "mime_type: %s", representation->mime_type);
    LOG_DEBUG(indent, "index_file_name: %s", representation->index_file_name);
    LOG_RANGE(indent, representation, index_range);
    LOG_DEBUG(indent, "initialization_file_name: %s", representation->initialization_file_name);
    LOG_RANGE(indent, representation, initialization_range);
    LOG_DEBUG(indent, "bitstream_switching_file_name: %s", representation->bitstream_switching_file_name);
    LOG_RANGE(indent, representation, bitstream_switching_range);
    LOG_DEBUG(indent, "start_with_sap: %u", representation->start_with_sap);
    LOG_DEBUG(indent, "presentation_time_offset: %"PRIu64, representation->presentation_time_offset);
    LOG_DEBUG(indent, "timescale: %"PRIu32, representation->timescale);
    for (size_t i = 0; i < representation->subrepresentations->len; ++i) {
        LOG_DEBUG(indent, "subrepresentation[%zu]:", i);
        subrepresentation_print(g_ptr_array_index(representation->subrepresentations, i), indent + 1);
    }
    for (size_t i = 0; i < representation->segments->len; ++i) {
        LOG_DEBUG(indent, "segments[%zu]:", i);
        segment_print(g_ptr_array_index(representation->segments, i), indent + 1);
    }
}

subrepresentation_t* subrepresentation_new(representation_t* representation)
{
    subrepresentation_t* obj = calloc(1, sizeof(*obj));
    obj->representation = representation;
    obj->dependency_level = g_array_new(false, false, sizeof(uint32_t));
    obj->content_component = g_ptr_array_new_with_free_func(g_free);
    return obj;
}

void subrepresentation_free(subrepresentation_t* obj)
{
    if(obj == NULL) {
        return;
    }
    g_array_free(obj->dependency_level, true);
    g_ptr_array_free(obj->content_component, true);
    free(obj);
}

void subrepresentation_print(const subrepresentation_t* obj, unsigned indent)
{
    g_return_if_fail(obj);

    LOG_DEBUG(indent, "profile: %s", dash_profile_to_string(obj->profile));
    LOG_DEBUG(indent, "start_with_sap: %"PRIu8, obj->start_with_sap);
    if (obj->has_level) {
        LOG_DEBUG(indent, "level: %"PRIu32, obj->level);
    }
    LOG_DEBUG(indent, "bandwidth: %"PRIu32, obj->bandwidth);
    for (size_t i = 0; i < obj->dependency_level->len; ++i) {
        LOG_DEBUG(indent, "dependency_level[%zu]: %"PRIu32, i, g_array_index(obj->dependency_level, uint32_t, i));
    }
    for (size_t i = 0; i < obj->content_component->len; ++i) {
        LOG_DEBUG(indent, "content_component[%zu]: %s", i, (char*)g_ptr_array_index(obj->content_component, i));
    }
}

segment_t* segment_new(representation_t* representation)
{
    segment_t* obj = calloc(1, sizeof(*obj));
    obj->representation = representation;
    return obj;
}

void segment_free(segment_t* obj)
{
    if(obj == NULL) {
        return;
    }

    g_free(obj->file_name);
    if (obj->file_name != obj->index_file_name) {
        g_free(obj->index_file_name);
    }

    if (obj->arg_free && obj->arg) {
        obj->arg_free(obj->arg);
    }
    free(obj);
}

void segment_print(const segment_t* segment, unsigned indent)
{
    g_return_if_fail(segment);

    LOG_DEBUG(indent, "file_name: %s", segment->file_name);
    LOG_RANGE(indent, segment, media_range);
    LOG_DEBUG(indent, "start: %"PRIu64, segment->start);
    LOG_DEBUG(indent, "duration: %"PRIu64, segment->duration);
    LOG_DEBUG(indent, "index_file_name: %s", segment->index_file_name);
    LOG_RANGE(indent, segment, index_range);
}

static mpd_t* mpd_read(xmlDoc* doc, char* base_url)
{
    g_return_val_if_fail(doc, NULL);
    g_return_val_if_fail(base_url, NULL);

    mpd_t* mpd = mpd_new();
    xmlNode* root = xmlDocGetRootElement(doc);
    if (!root) {
        g_critical("No root element in MPD!");
        goto fail;
    }
    if (root->type != XML_ELEMENT_NODE) {
        g_critical("MPD error, toplevel element is not an element.");
        goto fail;
    }
    if (!xmlStrEqual (root->name, "MPD")) {
        g_critical("MPD error, top level element is not an <MPD>, got <%s> instead.", root->name);
        goto fail;
    }

    mpd->profile = read_profile(root, DASH_PROFILE_FULL);

    xmlChar* type = xmlGetProp(root, "type");
    if (xmlStrEqual(type, "static")) {
        mpd->presentation_type = MPD_PRESENTATION_STATIC;
    } else if (xmlStrEqual(type, "dynamic")) {
        mpd->presentation_type = MPD_PRESENTATION_DYNAMIC;
    }
    xmlFree(type);

    mpd->duration = read_duration(root, "mediaPresentationDuration");

    for (xmlNode* cur_node = root->children; cur_node; cur_node = cur_node->next) {
        if (xmlStrEqual(cur_node->name, "Period")) {
            if(!read_period(cur_node, mpd, base_url)) {
                goto fail;
            }
        }
    }

cleanup:
    return mpd;
fail:
    mpd_free(mpd);
    mpd = NULL;
    goto cleanup;
}

mpd_t* mpd_read_file(char* file_name)
{
    g_return_val_if_fail(file_name, NULL);

    mpd_t* mpd = NULL;
    xmlDoc* doc = xmlReadFile(file_name, NULL, 0);
    if (doc == NULL) {
        g_critical("Could not parse MPD file: %s.", file_name);
        goto cleanup;
    }

    mpd = mpd_read(doc, file_name);

cleanup:
    xmlFreeDoc(doc);
    return mpd;
}

mpd_t* mpd_read_doc(char* xml_doc, char* base_url)
{
    g_return_val_if_fail(xml_doc, NULL);
    g_return_val_if_fail(base_url, NULL);

    mpd_t* mpd = NULL;
    xmlDoc* doc = xmlReadDoc(xml_doc, base_url, NULL, 0);
    if (doc == NULL) {
        g_critical("Could not parse MPD document: %s.", xml_doc);
        goto cleanup;
    }

    mpd = mpd_read(doc, base_url);

cleanup:
    xmlFreeDoc(doc);
    return mpd;
}

bool read_period(xmlNode* node, mpd_t* mpd, char* parent_base_url)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(mpd, false);
    g_return_val_if_fail(parent_base_url, false);

    bool return_code = true;

    period_t* period = period_new(mpd);
    g_ptr_array_add(mpd->periods, period);

    char* base_url = find_base_url(node, parent_base_url);
    period->bitstream_switching = read_bool(node, "bitstreamSwitching");
    period->duration = read_duration(node, "duration");
    if (period->duration == 0) {
        period->duration = mpd->duration;
    }

    GPtrArray* segment_bases = g_ptr_array_new();
    xmlNode* segment_base = find_segment_base(node);
    if (segment_base) {
        g_ptr_array_add(segment_bases, segment_base);
    }

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (xmlStrEqual(cur_node->name, "AdaptationSet")) {
            if(!read_adaptation_set(cur_node, period, base_url, segment_bases)) {
                goto fail;
            }
        }
    }

cleanup:
    g_ptr_array_free(segment_bases, true);
    g_free(base_url);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_adaptation_set(xmlNode* node, period_t* period, char* parent_base_url, GPtrArray* segment_bases)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(period, false);
    g_return_val_if_fail(parent_base_url, false);
    g_return_val_if_fail(segment_bases, false);

    bool return_code = true;

    adaptation_set_t* adaptation_set = adaptation_set_new(period);
    g_ptr_array_add(period->adaptation_sets, adaptation_set);

    adaptation_set->id = read_uint64(node, "id");
    adaptation_set->mime_type = xmlGetProp(node, "mimeType");
    adaptation_set->profile = read_profile(node, period->mpd->profile);
    adaptation_set->segment_alignment = read_optional_uint32(node, "segmentAlignment");
    adaptation_set->subsegment_alignment = read_optional_uint32(node, "subsegmentAlignment");
    adaptation_set->bitstream_switching = period->bitstream_switching || read_bool(node, "bitstreamSwitching");

    char* base_url = find_base_url(node, parent_base_url);

    xmlNode* segment_base = find_segment_base(node);
    if (segment_base) {
        g_ptr_array_add(segment_bases, segment_base);
    }

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "Representation")) {
            if(!read_representation(cur_node, adaptation_set, base_url, segment_bases)) {
                goto fail;
            }
        } else if (xmlStrEqual(cur_node->name, "ContentComponent")) {
            xmlChar* type = xmlGetProp(cur_node, "contentType");
            if (xmlStrEqual(type, "video")) {
                adaptation_set->video_pid = read_uint64(cur_node, "id");
            } else if (xmlStrEqual(type, "audio")) {
                adaptation_set->audio_pid = read_uint64(cur_node, "id");
            }
            xmlFree(type);
        }
    }

cleanup:
    if (segment_base) {
        g_ptr_array_remove_index(segment_bases, segment_bases->len - 1);
    }
    g_free(base_url);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_representation(xmlNode* node, adaptation_set_t* adaptation_set, char* parent_base_url,
        GPtrArray* segment_bases)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(adaptation_set, false);
    g_return_val_if_fail(parent_base_url, false);
    g_return_val_if_fail(segment_bases, false);

    char* start_with_sap = NULL;
    bool return_code = true;
    char* base_url =  NULL;

    representation_t* representation = representation_new(adaptation_set);
    g_ptr_array_add(adaptation_set->representations, representation);

    representation->profile = read_profile(node, adaptation_set->profile);
    representation->id = xmlGetProp(node, "id");
    representation->mime_type = xmlGetProp(node, "mimeType");
    representation->bandwidth = read_uint64(node, "bandwidth");

    xmlNode* segment_base = find_segment_base(node);
    if (segment_base) {
        g_ptr_array_add(segment_bases, segment_base);
    }

    start_with_sap = xmlGetProp(node, "startWithSAP");
    if (start_with_sap) {
        int tmp = atoi(start_with_sap);
        if (tmp < 0 || tmp > 6) {
            g_critical("Invalid startWithSap value of %s. Must be in the range [0-6].", start_with_sap);
            goto fail;
        }
        representation->start_with_sap = tmp;
    }

    base_url = find_base_url(node, parent_base_url);

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (xmlStrEqual(cur_node->name, "SubRepresentation")) {
            if (!read_subrepresentation(cur_node, representation)) {
                goto fail;
            }
        }
    }

    for (int i = segment_bases->len - 1; i >= 0; --i) {
        xmlNode* cur_node = g_ptr_array_index(segment_bases, i);
        if (xmlStrEqual(cur_node->name, "SegmentList")) {
            if(!read_segment_list(cur_node, representation, base_url, segment_bases)) {
                goto fail;
            }
            break;
        } else if (xmlStrEqual(cur_node->name, "SegmentBase")) {
            if (!read_segment_base(cur_node, representation, base_url, segment_bases)) {
                goto fail;
            }
            segment_t* segment = segment_new(representation);
            segment->file_name = g_strdup(base_url);
            segment->start = representation->presentation_time_offset;
            segment->duration = representation->adaptation_set->period->duration * MPEG_TS_TIMESCALE;
            segment->end = segment->start + segment->duration;
            if (representation->have_segment_index_range) {
                segment->index_range_start = representation->segment_index_range_start;
                segment->index_range_end = representation->segment_index_range_end;
                segment->index_file_name = segment->file_name;
            }
            g_ptr_array_add(representation->segments, segment);
            break;
        }  else if (xmlStrEqual(cur_node->name, "SegmentTemplate")) {
            if (!read_segment_template(cur_node, representation, base_url, segment_bases)) {
                goto fail;
            }
            break;
        }
    }

cleanup:
    if (segment_base) {
        g_ptr_array_remove_index(segment_bases, segment_bases->len - 1);
    }
    g_free(base_url);
    xmlFree(start_with_sap);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_subrepresentation(xmlNode* node, representation_t* representation)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(representation, false);

    bool return_code = true;
    char* start_with_sap = NULL;

    subrepresentation_t* subrepresentation = subrepresentation_new(representation);
    g_ptr_array_add(representation->subrepresentations, subrepresentation);

    if (xmlHasProp(node, "level")) {
        subrepresentation->has_level = true;
        subrepresentation->level = read_uint64(node, "level");
    }
    subrepresentation->profile = read_profile(node, representation->profile);
    subrepresentation->bandwidth = read_uint64(node, "bandwidth");

    char* content_component = xmlGetProp(node, "contentComponent");
    if (content_component) {
        char** cc_split = g_strsplit_set(content_component, " \t", -1);
        for (size_t i = 0; cc_split[i]; ++i) {
            if (strlen(cc_split[i]) == 0) {
                continue;
            }
            g_ptr_array_add(subrepresentation->content_component, g_strdup(cc_split[i]));
        }
        g_strfreev(cc_split);
    }

    char* dependency_level = xmlGetProp(node, "dependencyLevel");
    char** dependency_split = NULL;
    if (dependency_level) {
        dependency_split = g_strsplit_set(dependency_level, " \t", -1);
        for (size_t i = 0; dependency_split[i]; ++i) {
            if (strlen(dependency_split[i]) == 0) {
                continue;
            }
            int error = 0;
            uint64_t cc_int = str_to_uint64(dependency_split[i], 0, &error);
            if (error || cc_int > UINT32_MAX) {
                g_critical("SubRepresentation@dependencyLevel %s is not an xs:unsignedInt.", dependency_split[i]);
                goto fail;
            }
            g_array_append_val(subrepresentation->dependency_level, cc_int);
        }
    }

    start_with_sap = xmlGetProp(node, "startWithSAP");
    if (start_with_sap) {
        int tmp = atoi(start_with_sap);
        if (tmp < 0 || tmp > 6) {
            g_critical("Invalid startWithSap value of %s. Must be in the range [0-6].", start_with_sap);
            goto fail;
        }
        subrepresentation->start_with_sap = tmp;
    }

cleanup:
    g_strfreev(dependency_split);
    xmlFree(start_with_sap);
    xmlFree(dependency_level);
    xmlFree(content_component);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_segment_base(xmlNode* node, representation_t* representation, char* base_url, GPtrArray* segment_bases)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(representation, false);
    g_return_val_if_fail(base_url, false);
    g_return_val_if_fail(segment_bases, false);

    bool return_code = true;
    g_ptr_array_add(segment_bases, node);

    /* Need to read timescale before presentationTimeOffset */
    for (int i = segment_bases->len - 1; i >= 0; --i) {
        xmlNode* cur_node = g_ptr_array_index(segment_bases, i);
        if (xmlHasProp(cur_node, "timescale")) {
            representation->timescale = read_uint64(cur_node, "timescale");
            if (representation->timescale == 0) {
                char* value = xmlGetProp(cur_node, "timescale");
                g_critical("Invalid %s@timescale: %s", cur_node->name, value);
                g_free(value);
                goto fail;
            }
            break;
        }
    }

    bool have_index_range = false;
    bool have_presentation_time_offset = false;
    bool have_representation_index = false;
    bool have_initialization = false;
    bool have_bitstream_switching = false;
    bool have_start_number = false;
    for (int i = segment_bases->len - 1; i >= 0; --i) {
        xmlNode* base = g_ptr_array_index(segment_bases, i);
        if (!have_presentation_time_offset && xmlHasProp(base, "presentationTimeOffset")) {
            representation->presentation_time_offset = convert_timescale(
                read_uint64(base, "presentationTimeOffset"), representation->timescale);
            have_presentation_time_offset = true;
        }

        if (!have_index_range && xmlHasProp(base, "indexRange")) {
            representation->have_segment_index_range = true;
            if (!read_range(base, "indexRange", &representation->segment_index_range_start,
                &representation->segment_index_range_end)) {
                goto fail;
            }
            have_index_range = true;
        }

        if (!have_start_number && xmlHasProp(base, "startNumber")) {
            representation->start_number = read_uint64(base, "startNumber");
            have_start_number = true;
        }

        for (xmlNode* cur_node = base->children; cur_node; cur_node = cur_node->next) {
            if (cur_node->type != XML_ELEMENT_NODE) {
                continue;
            }
            if (!have_representation_index && xmlStrEqual(cur_node->name, "RepresentationIndex")) {
                if (representation->index_file_name != NULL) {
                    g_critical("Ignoring duplicate index file in <%s>.", node->name);
                    goto fail;
                }
                representation->index_file_name = read_filename(cur_node, "sourceURL", base_url);
                if(!read_range(cur_node, "range", &representation->index_range_start,
                        &representation->index_range_end)) {
                    goto fail;
                }
                have_representation_index = true;
            } else if (!have_initialization && xmlStrEqual(cur_node->name, "Initialization")) {
                if (representation->initialization_file_name != NULL) {
                    g_critical("Ignoring duplicate initialization segment in <%s>.", node->name);
                    goto fail;
                }
                representation->initialization_file_name = read_filename(cur_node, "sourceURL", base_url);
                if(!read_range(cur_node, "range", &representation->initialization_range_start,
                        &representation->initialization_range_end)) {
                    goto fail;
                }
                have_initialization = true;
            } else if (!have_bitstream_switching && xmlStrEqual(cur_node->name, "BitstreamSwitching")) {
                if (representation->bitstream_switching_file_name != NULL) {
                    g_critical("Duplicate <BitstreamSwitching> segment in <%s>.", node->name);
                    goto fail;
                }
                representation->bitstream_switching_file_name = read_filename(cur_node, "sourceURL", base_url);
                if(!read_range(cur_node, "range", &representation->bitstream_switching_range_start,
                        &representation->bitstream_switching_range_end)) {
                    goto fail;
                }
                have_bitstream_switching = true;
            }
        }
    }
cleanup:
    g_ptr_array_remove_index(segment_bases, segment_bases->len - 1);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_segment_list(xmlNode* node, representation_t* representation, char* base_url, GPtrArray* segment_bases)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(representation, false);
    g_return_val_if_fail(base_url, false);
    g_return_val_if_fail(segment_bases, false);

    bool return_code = read_segment_base(node, representation, base_url, segment_bases);
    if (!return_code) {
        return false;
    }
    g_ptr_array_add(segment_bases, node);

    GPtrArray* segment_timeline = NULL;
    uint64_t duration = 0;
    for (int i = segment_bases->len - 1; i >= 0; --i) {
        xmlNode* base_node = g_ptr_array_index(segment_bases, i);
        if (!duration && !segment_timeline && xmlHasProp(base_node, "duration")) {
            duration = read_uint64(base_node, "duration");
            if (duration == 0) {
                g_critical("<%s> has invalid duration.", base_node->name);
                goto fail;
            }
            duration = convert_timescale(duration, representation->timescale);
        }

        for (xmlNode* cur_node = base_node->children; cur_node; cur_node = cur_node->next) {
            if (!duration && !segment_timeline && xmlStrEqual(cur_node->name, "SegmentTimeline")) {
                segment_timeline = read_segment_timeline(cur_node, representation);
                if (!segment_timeline) {
                    goto fail;
                }
            }
        }
    }

    size_t timeline_i = 0;
    uint64_t start = representation->presentation_time_offset;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (xmlStrEqual(cur_node->name, "SegmentURL")) {
            if (segment_timeline != NULL) {
                if (timeline_i >= segment_timeline->len) {
                    g_critical("<SegmentTimeline> does not have enough elements for the given segments!");
                    goto fail;
                }
                segment_timeline_s_t* s = g_ptr_array_index(segment_timeline, timeline_i);
                start = s->start;
                duration = s->duration;
            }
            if(!read_segment_url(cur_node, representation, start, duration, base_url, segment_bases)) {
                goto fail;
            }
            ++timeline_i;
            start += duration;
        }
    }

cleanup:
    g_ptr_array_remove_index(segment_bases, segment_bases->len - 1);
    if (segment_timeline != NULL) {
        g_ptr_array_free(segment_timeline, true);
    }
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

GPtrArray* read_segment_timeline(xmlNode* node, representation_t* representation)
{
    g_return_val_if_fail(node, NULL);
    g_return_val_if_fail(representation, NULL);

    GPtrArray* timeline = g_ptr_array_new_with_free_func(free);
    uint64_t start = representation->presentation_time_offset;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "S")) {
            int error = 0;
            char* d = NULL;
            char* r = NULL;
            char* t = xmlGetProp(cur_node, "t");
            if (t) {
                start = convert_timescale(str_to_uint64(t, 0, &error), representation->timescale);
                if (error) {
                    g_critical("<S>'s @t value (%s) is not a number.", t);
                    goto loop_cleanup;
                }
            }
            d = xmlGetProp(cur_node, "d");
            uint64_t duration = d ? str_to_uint64(d, 0, &error) : 0;
            if (error || !d) {
                g_critical("<S>'s @d value (%s) is not a valid duration.", d);
                goto loop_cleanup;
            }
            duration = convert_timescale(duration, representation->timescale);
            r = xmlGetProp(cur_node, "r");
            int64_t repeat = r ? str_to_int64(r, 0, &error) : 0;
            if (error) {
                g_critical("<S>'s @r value (%s) is not a number.", r);
                goto loop_cleanup;
            }

            for (int64_t i = 0; i < repeat + 1; ++i) {
                segment_timeline_s_t* s = malloc(sizeof(*s));
                s->start = start;
                s->duration = duration;
                g_ptr_array_add(timeline, s);
                start += duration;
            }
loop_cleanup:
            xmlFree(d);
            xmlFree(r);
            xmlFree(t);
            if (error) {
                goto fail;
            }
        }
    }
    return timeline;
fail:
    g_ptr_array_free(timeline, true);
    return NULL;
}

bool read_segment_url(xmlNode* node, representation_t* representation, uint64_t start, uint64_t duration,
        char* base_url, GPtrArray* segment_bases)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(representation, false);
    g_return_val_if_fail(base_url, false);
    g_return_val_if_fail(segment_bases, false);

    segment_t* segment = segment_new(representation);

    /* Don't use convert_timescale here since we already converted in read_segment_timeline and read_segment_list */
    segment->start = start;
    segment->duration = duration;
    /* Might as well calculate this once */
    segment->end = segment->start + duration;

    segment->file_name = read_filename(node, "media", base_url);
    if(!read_range(node, "mediaRange", &segment->media_range_start, &segment->media_range_end)) {
        goto fail;
    }
    if (xmlHasProp(node, "index")) {
        segment->index_file_name = read_filename(node, "index", base_url);
    }
    bool have_range = xmlHasProp(node, "indexRange");
    if (have_range) {
        if (!read_range(node, "indexRange", &segment->index_range_start, &segment->index_range_end)) {
            goto fail;
        }
    } else if (representation->have_segment_index_range) {
        segment->index_range_start = representation->segment_index_range_start;
        segment->index_range_end = representation->segment_index_range_end;
        have_range = true;
    }
    if (have_range && !segment->index_file_name) {
        segment->index_file_name = segment->file_name;
    }
    g_ptr_array_add(representation->segments, segment);
cleanup:
    return segment != NULL;
fail:
    segment_free(segment);
    segment = NULL;
    goto cleanup;
}

static char* segment_template_replace(char* pattern, uint64_t segment_number, representation_t* representation,
        uint64_t start_time, const char* base_url)
{
    g_return_val_if_fail(pattern, NULL);
    g_return_val_if_fail(representation, NULL);
    g_return_val_if_fail(base_url, NULL);

    /* $Time$ is in timescale units */
    start_time = convert_timescale_to(start_time, MPEG_TS_TIMESCALE, representation->timescale);

    char* with_base = NULL;

    size_t pattern_len = strlen(pattern);
    GString* result = g_string_sized_new(pattern_len);
    for (size_t i = 0; i < pattern_len; ++i) {
        if (pattern[i] != '$') {
            g_string_append_c(result, pattern[i]);
            continue;
        }

        ++i;
        if (i >= pattern_len) {
            g_critical("Error: <SegmentTemplate> has unclosed $ template in pattern: \"%s\".", pattern);
            goto fail;
        }
        if (pattern[i] == '$') {
            g_string_append_c(result, '$');
            continue;
        }

        char* start = pattern + i;
        if (g_str_has_prefix(start, "RepresentationID$")) {
            g_string_append(result, representation->id);
            i += strlen("RepresentationID$") - 1;
            continue;
        }

        uint64_t print_num;
        if (g_str_has_prefix(start, "Bandwidth")) {
            print_num = representation->bandwidth;
            i += strlen("Bandwidth");
        } else if (g_str_has_prefix(start, "Number")) {
            print_num = segment_number;
            i += strlen("Number");
        } else if (g_str_has_prefix(start, "Time")) {
            print_num = start_time;
            i += strlen("Time");
        } else {
            g_critical("Unknown template substitution in template \"%s\" at position %zu.", pattern, i);
            goto fail;
        }

        /* Width specifier */
        int padding = 0;
        if (i < pattern_len && pattern[i] == '%') {
            ++i;
            if (i >= pattern_len || pattern[i] != '0') {
                g_critical("Unknown template substitution in template \"%s\" at position %zu.", pattern, i);
                goto fail;
            }
            ++i;
            for (; i < pattern_len && pattern[i] != 'd'; ++i) {
                padding = padding * 10 + pattern[i] - '0';
            }
            if (i >= pattern_len || pattern[i] != 'd') {
                g_critical("Unknown template substitution in template \"%s\" at position %zu.", pattern, i);
                goto fail;
            }
            ++i;
        }
        if (pattern[i] != '$') {
            g_critical("Unknown template substitution in template \"%s\" at position %zu.", pattern, i);
            goto fail;
        }
        g_string_append_printf(result, "%0*"PRIu64, padding, print_num);
    }

    char* directory = g_path_get_dirname(base_url);
    with_base = g_build_filename(directory, result->str, NULL);
    g_free(directory);
fail:
    g_string_free(result, true);
    return with_base;
}

static bool read_segment_template(xmlNode* node, representation_t* representation, char* base_url,
        GPtrArray* segment_bases)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(representation, false);
    g_return_val_if_fail(base_url, false);
    g_return_val_if_fail(segment_bases, false);

    char* media_template = NULL;
    char* index_template = NULL;
    char* initialization_template = NULL;
    char* bitstream_switching_template = NULL;
    GPtrArray* segment_timeline = NULL;

    bool return_code = read_segment_base(node, representation, base_url, segment_bases);
    if (!return_code) {
        return false;
    }

    g_ptr_array_add(segment_bases, node);

    uint64_t duration = 0;
    for (int i = segment_bases->len - 1; i >= 0; --i) {
        xmlNode* base_node = g_ptr_array_index(segment_bases, i);
        if (!media_template) {
            media_template = xmlGetProp(base_node, "media");
        }
        if (!index_template) {
            index_template = xmlGetProp(base_node, "index");
        }
        if (!initialization_template) {
            initialization_template = xmlGetProp(base_node, "initialization");
        }
        if (!bitstream_switching_template) {
            bitstream_switching_template = xmlGetProp(base_node, "bitstreamSwitching");
        }
        if (!duration && !segment_timeline && xmlHasProp(base_node, "duration")) {
            duration = read_uint64(base_node, "duration");
            if (duration == 0) {
                g_critical("<%s> has invalid duration.", base_node->name);
                goto fail;
            }
            duration = convert_timescale(duration, representation->timescale);
        }

        for (xmlNode* cur_node = base_node->children; cur_node; cur_node = cur_node->next) {
            if (!duration && !segment_timeline && xmlStrEqual(cur_node->name, "SegmentTimeline")) {
                segment_timeline = read_segment_timeline(cur_node, representation);
                if (!segment_timeline) {
                    goto fail;
                }
            }
        }
    }

    if (!media_template) {
        g_critical("<SegmentTemplate> has no @media attribute.");
        goto fail;
    }

    if (initialization_template) {
        representation->initialization_file_name = segment_template_replace(initialization_template, 0, representation,
                0, base_url);
        if (!representation->initialization_file_name) {
            goto fail;
        }
    }

    if (bitstream_switching_template) {
        if (representation->bitstream_switching_file_name) {
            g_critical("<SegmentTemplate> has both <BitstreamSwitching> and @bitstreamSwitching. Pick one or the "
                    "other.");
            goto fail;
        }
        representation->bitstream_switching_file_name = segment_template_replace(bitstream_switching_template, 0,
                representation, 0, base_url);
        if (!representation->bitstream_switching_file_name) {
            goto fail;
        }
    }

    uint64_t start_number = representation->start_number;
    size_t timeline_i = 0;
    uint64_t start_time = representation->presentation_time_offset;
    uint64_t period_end = (representation->adaptation_set->period->duration * MPEG_TS_TIMESCALE) \
            + start_time;
    for (size_t i = 0;
            start_time < period_end && (segment_timeline == NULL || timeline_i < segment_timeline->len);
            ++i, ++timeline_i) {

        segment_t* segment = segment_new(representation);
        if (segment_timeline) {
            segment_timeline_s_t* s = g_ptr_array_index(segment_timeline, timeline_i);
            segment->start = s->start;
            segment->duration = s->duration;
            segment->end = segment->start + segment->duration;
        } else {
            segment->start = start_time;
            segment->duration = duration;
            segment->end = start_time + duration;
        }
        segment->file_name = segment_template_replace(media_template, i + start_number, representation, segment->start,
                base_url);
        if (!segment->file_name) {
            goto fail;
        }
        if (index_template) {
            segment->index_file_name = segment_template_replace(index_template, i + start_number, representation,
                    segment->start, base_url);
        }
        if (representation->have_segment_index_range) {
            segment->index_range_start = representation->segment_index_range_start;
            segment->index_range_end = representation->segment_index_range_end;
            if (!segment->index_file_name) {
                segment->index_file_name = segment->file_name;
            }
        }
        g_ptr_array_add(representation->segments, segment);

        start_time += duration;
    }

cleanup:
    g_ptr_array_remove_index(segment_bases, segment_bases->len - 1);
    if (segment_timeline) {
        g_ptr_array_free(segment_timeline, true);
    }
    xmlFree(media_template);
    xmlFree(index_template);
    xmlFree(initialization_template);
    xmlFree(bitstream_switching_template);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

char* find_base_url(xmlNode* node, char* parent_url)
{
    g_return_val_if_fail(node, NULL);
    g_return_val_if_fail(parent_url, NULL);

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE && xmlStrEqual(cur_node->name, "BaseURL")) {
            char* base_url;
            char* content = xmlNodeGetContent(cur_node);
            if (parent_url) {
                char* directory = g_path_get_dirname(parent_url);
                base_url = g_build_filename(directory, content, NULL);
                g_free(directory);
            } else {
                /* Make a duplicate so all of our strings will use the same allocator. */
                base_url = g_strdup(content);
            }
            xmlFree(content);
            return base_url;
        }
    }
    /* Make a copy here because it makes memory management easier if this
     * function always returns a new string. */
    return g_strdup(parent_url);
}

optional_uint32_t read_optional_uint32(xmlNode* node, const char* property_name)
{
    optional_uint32_t result = {0};
    g_return_val_if_fail(node, result);
    g_return_val_if_fail(property_name, result);

    char* value = xmlGetProp(node, property_name);
    if (value == 0 || xmlStrEqual(value, "false")) {
        result.has_int = false;
        result.b = false;
    } else if (xmlStrEqual(value, "true")) {
        result.has_int = false;
        result.b = true;
    } else {
        int error;
        uint32_t tmp = str_to_uint64(value, 0, &error);
        if (error) {
            g_warning("Got invalid ConditionalUintType for property %s: %s", property_name, value);
        } else {
            result.has_int = true;
            result.i = tmp;
        }
    }
    xmlFree(value);
    return result;
}

static void print_optional_uint32(int indent, const char* name, optional_uint32_t value)
{
    g_return_if_fail(name);

    if (value.has_int) {
        LOG_DEBUG(indent, "%s: %"PRIu32, name, value.i);
    } else {
        LOG_DEBUG(indent, "%s: %s", name, BOOL_TO_STR(value.b));
    }
}

uint32_t read_uint64(xmlNode* node, const char* property_name)
{
    g_return_val_if_fail(node, 0);
    g_return_val_if_fail(property_name, 0);

    char* value = xmlGetProp(node, property_name);
    if (value == NULL) {
        return 0;
    }
    int error;
    uint64_t result = str_to_uint64(value, 0, &error);
    if (error) {
        g_warning("Got invalid unsignedLong for property %s: %s", property_name, value);
    }
    xmlFree(value);
    return result;
}

bool read_bool(xmlNode* node, const char* property_name)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(property_name, false);

    char* value = xmlGetProp(node, property_name);
    bool result = xmlStrEqual(value, "true");
    if (value && !result && !xmlStrEqual(value, "false")) {
        g_warning("Got invalid value for boolean property %s: %s", property_name, value);
    }
    xmlFree(value);
    return result;
}

char* read_filename(xmlNode* node, const char* property_name, const char* base_url)
{
    g_return_val_if_fail(node, NULL);
    g_return_val_if_fail(property_name, NULL);
    g_return_val_if_fail(base_url, NULL);

    char* property = xmlGetProp(node, property_name);
    char* filename;
    if (property == NULL) {
        /* Make a duplicate so we always return strings allocated by glib. */
        filename = g_strdup(base_url);
    } else if (base_url == NULL) {
        filename = g_strdup(property);
    } else {
        char* directory = g_path_get_dirname(base_url);
        filename = g_build_filename(directory, property, NULL);
        g_free(directory);
    }
    xmlFree(property);
    return filename;
}

uint64_t str_to_uint64(const char* str, size_t length, int* error)
{
    g_return_val_if_fail(str, 0);

    uint64_t result = 0;
    for (size_t i = 0; (!length || i < length) && str[i]; ++i) {
        char c = str[i];
        if (c < '0' || c > '9') {
            g_warning("Invalid non-digit in string to parse: %s.", str);
            if (error) {
                *error = 1;
            }
            return 0;
        }
        result = result * 10 + c - '0';
    }
    if (error) {
        *error = 0;
    }
    return result;
}

uint64_t str_to_int64(const char* str, size_t length, int* error)
{
    g_return_val_if_fail(str, 0);

    bool negative = str[0] == '-';
    int64_t result = 0;
    for (size_t i = negative ? 1 : 0; (!length || i < length) && str[i]; ++i) {
        char c = str[i];
        if (c < '0' || c > '9') {
            g_warning("Invalid non-digit '%c' in string to parse: %s.", c, str);
            if (error) {
                *error = 1;
            }
            return 0;
        }
        result = result * 10 + c - '0';
    }
    if (error) {
        *error = 0;
    }
    return (negative ? -1 : 1) * result;
}

static dash_profile_t read_profile(xmlNode* node, dash_profile_t parent_profile)
{
    g_return_val_if_fail(node, DASH_PROFILE_UNKNOWN);

    dash_profile_t profile = DASH_PROFILE_UNKNOWN;
    char* property = xmlGetProp(node, "profiles");
    if (property == NULL) {
        goto cleanup;
    }

    gchar** profiles = g_strsplit_set(property, ", \t", -1);
    gchar* profile_str;
    for (size_t i = 0; (profile_str = profiles[i]); ++i) {
        dash_profile_t new_profile = DASH_PROFILE_UNKNOWN;
        if (!strcmp(profile_str, DASH_PROFILE_URN_FULL)) {
            new_profile = DASH_PROFILE_FULL;
        } else if (!strcmp(profile_str, DASH_PROFILE_URN_MPEG2TS_MAIN)) {
            new_profile = DASH_PROFILE_MPEG2TS_MAIN;
        } else if (!strcmp(profile_str, DASH_PROFILE_URN_MPEG2TS_SIMPLE)) {
            new_profile = DASH_PROFILE_MPEG2TS_SIMPLE;
        }
        /* Pick the strictest listed profile */
        if (new_profile > profile) {
            profile = new_profile;
        }
    }
    g_strfreev(profiles);

cleanup:
    /* Only inherit parent profile if we didn't find one. This is because child profiles aren't necessarily a subset
     * of their parent profiles (i.e., an MPD with simple profile must contain at least one representation of simple
     * profile, but it can also contain representations with main and full profile. */
    if (profile == DASH_PROFILE_UNKNOWN) {
        profile = parent_profile;
    }
    xmlFree(property);
    return profile;
}

static const char* dash_profile_to_string(dash_profile_t profile) {
    switch (profile) {
    case DASH_PROFILE_FULL:
        return DASH_PROFILE_URN_FULL;
    case DASH_PROFILE_MPEG2TS_MAIN:
        return DASH_PROFILE_URN_MPEG2TS_MAIN;
    case DASH_PROFILE_MPEG2TS_SIMPLE:
        return DASH_PROFILE_URN_MPEG2TS_SIMPLE;
    default:
        g_error("Print unknown dash_profile_t: %d", profile);
        return "UNKNOWN DASH PROFILE";
    }
}

static bool read_range(xmlNode* node, const char* property_name, uint64_t* start_out, uint64_t* end_out)
{
    g_return_val_if_fail(node, false);
    g_return_val_if_fail(property_name, false);
    g_return_val_if_fail(start_out, false);
    g_return_val_if_fail(end_out, false);

    bool ret = true;
    char** split = NULL;
    char* property = xmlGetProp(node, property_name);
    if (property == NULL) {
        goto cleanup;
    }

    split = g_strsplit(property, "-", 0);
    /* Length of the split string should be exactly 2 */
    if (split[0] == NULL || split[1] == NULL || split[2] != NULL) {
        goto fail;
    }

    int error;
    *start_out = str_to_uint64(split[0], 0, &error);
    if (error) {
        goto fail;
    }
    *end_out = str_to_uint64(split[1], 0, &error);
    if (error) {
        goto fail;
    }
cleanup:
    g_strfreev(split);
    xmlFree(property);
    return ret;
fail:
    g_critical("Range %s is not a valid byte range.", property);
    ret = false;
    goto cleanup;
}

uint64_t convert_timescale(uint64_t time, uint64_t timescale)
{
    g_return_val_if_fail(timescale != 0, 0);
    return convert_timescale_to(time, timescale, MPEG_TS_TIMESCALE);
}

uint64_t convert_timescale_to(uint64_t time, uint64_t from_timescale, uint64_t to_timescale)
{
    g_return_val_if_fail(from_timescale != 0, 0);
    g_return_val_if_fail(to_timescale != 0, 0);

    // This function is complicated because we want to avoid rounding if an exact result is possible
    if (time == 0 || from_timescale == to_timescale) {
        return time;
    } else if (from_timescale > to_timescale) {
        return time / (from_timescale / to_timescale);
    } else {
        return (to_timescale / from_timescale) * time;
    }
}

uint64_t read_duration(xmlNode* node, const char* property_name)
{
    g_return_val_if_fail(node, 0);
    g_return_val_if_fail(property_name, 0);

    uint64_t result = 0;
    pcre* re = NULL;
    char* value = xmlGetProp(node, property_name);
    if (value == NULL) {
        goto fail;
    }

    const char* error;
    int error_offset;
    re = pcre_compile("P((?<year>[0-9]+)Y)?((?<month>[0-9]+)M)?((?<day>[0-9]+)D)?(T((?<hour>[0-9]+)H)?((?<minute>[0-9]+)M)?((?<second>[0-9]+)(\\.[0-9]+)?S)?)?", 0, &error, &error_offset, NULL);
    if (re == NULL) {
        g_critical("PCRE compilation error %s at offset %d.", error, error_offset);
        goto fail;
    }

    int output_vector[15 * 3];
    int output_length = 15 * 3;

    int return_code = pcre_exec(re, NULL, value, xmlStrlen(value), 0, 0, output_vector, output_length);
    if (return_code < 0) {
        switch(return_code) {
        case PCRE_ERROR_NOMATCH:
            g_warning("Duration %s does not match duration regex.", value);
            goto fail;
        default:
            g_critical("Duration %s caused PCRE matching error: %d.", value, return_code);
            goto fail;
        }
    }
    if (return_code == 0) {
        g_critical("PCRE output vector size of %d is not big enough.", output_length / 3);
        goto fail;
    }

    int namecount;
    int name_entry_size;
    unsigned char* name_table;
    pcre_fullinfo(re, NULL, PCRE_INFO_NAMECOUNT, &namecount);

    if (namecount <= 0) {
        g_critical("No named substrings. PCRE might be broken?");
        goto fail;
    }

    unsigned char *tabptr;
    pcre_fullinfo( re, NULL, PCRE_INFO_NAMETABLE, &name_table);
    pcre_fullinfo(re, NULL, PCRE_INFO_NAMEENTRYSIZE, &name_entry_size);

    /* Now we can scan the table and, for each entry, print the number, the name,
    and the substring itself. */

    tabptr = name_table;
    for (int i = 0; i < namecount; i++) {
        int n = (tabptr[0] << 8) | tabptr[1];

        char* field_name = tabptr + 2;
        int field_name_length = name_entry_size - 3;

        char* field_value = value + output_vector[2*n];
        int field_value_length = output_vector[2*n+1] - output_vector[2*n];
        if (field_value_length != 0) {
            int errorNum = 0;
            uint64_t field_value_int = str_to_uint64(field_value, field_value_length, &errorNum);
            if (errorNum) {
                g_critical("Failed to convert %.*s in xs:duration to an integer.", field_value_length, field_value);
                goto fail;
            }
            if (strncmp(field_name, "second", field_name_length) == 0) {
                result += field_value_int;
            } else if (strncmp(field_name, "minute", field_name_length) == 0) {
                result += field_value_int * 60;
            } else if (strncmp(field_name, "hour", field_name_length) == 0) {
                result += field_value_int * 3600;
            } else if (strncmp(field_name, "day", field_name_length) == 0) {
                result += field_value_int * 86400;
            } else if (strncmp(field_name, "month", field_name_length) == 0) {
                g_warning("xs:duration in property %s uses months field, but the number of seconds in a month is "
                        "undefined. Using an approximation of 30.6 days per month.", property_name);
                result += field_value_int * 2643840;
            } else if (strncmp(field_name, "year", field_name_length) == 0) {
                g_warning("xs:duration in property %s uses years field, but the number of seconds in a year is "
                        "undefined. Using an approximation of 365.25 days per year.", property_name);
                result += field_value_int * 31557600;
            } else {
                g_critical("Unknown field: %.*s = %.*s.", field_name_length,
                        field_name, field_value_length, field_value);
                goto fail;
            }
        }
        tabptr += name_entry_size;
    }

cleanup:
    pcre_free(re);
    xmlFree(value);
    return result;
fail:
    result = 0;
    goto cleanup;
}

xmlNode* find_segment_base(xmlNode* node)
{
    g_return_val_if_fail(node, NULL);

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "SegmentBase") || xmlStrEqual(cur_node->name, "SegmentList")
                || xmlStrEqual(cur_node->name, "SegmentTemplate")) {
            return cur_node;
        }
    }
    return NULL;
}