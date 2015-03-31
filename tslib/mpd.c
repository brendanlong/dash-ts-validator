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
#include <stdlib.h>
#include <string.h>
#include <libxml/tree.h>
#include "log.h"


/* Ignore warnings for implicitly casting const char* to const xmlChar* (i.e. const unsigned char*) */
#pragma GCC diagnostic ignored "-Wpointer-sign"

#define PRINT_PROPERTY(indent, printf_str, ...) \
g_debug("%.*s"printf_str, \
        indent < sizeof(INDENT_BUFFER) ? indent : (int)sizeof(INDENT_BUFFER), \
        INDENT_BUFFER, __VA_ARGS__)

#define PRINT_RANGE(indent, owner, property_name) \
if (owner->property_name##_start != 0 || owner->property_name##_end != 0) { \
    PRINT_PROPERTY(indent, #property_name ": %"PRIu64"-%"PRIu64, owner->property_name##_start, \
            owner->property_name##_end); \
}

typedef struct {
    uint64_t start;
    uint64_t duration;
} segment_timeline_s_t;


static bool read_period(xmlNode*, mpd_t*, char* base_url);
static bool read_adaptation_set(xmlNode*, mpd_t*, period_t*, char* base_url, xmlNode* segment_base,
        xmlNode* segment_list, xmlNode* segment_template);
static bool read_representation(xmlNode*, adaptation_set_t*, bool saw_mime_type, char* base_url, xmlNode* segment_base,
        xmlNode* segment_list, xmlNode* segment_template);
static bool read_subrepresentation(xmlNode*, representation_t*);
static bool read_segment_base(xmlNode*, representation_t*, char* base_url, xmlNode* parent_segment_base);
static bool read_segment_list(xmlNode*, representation_t*, char* base_url, xmlNode* parent_segment_list);
static GPtrArray* read_segment_timeline(xmlNode*, representation_t*);
static bool read_segment_url(xmlNode*, representation_t*, uint64_t start, uint64_t duration, char* base_url);
static bool read_segment_template(xmlNode*, representation_t*, char* base_url, xmlNode* parent_segment_template);
static char* find_base_url(xmlNode*, char* parent_base_url);
static optional_uint32_t read_optional_uint32(xmlNode*, const char* property_name);
static void print_optional_uint32(int indent, const char* name, optional_uint32_t value);
static uint32_t read_uint64(xmlNode*, const char* property_name);
static bool read_bool(xmlNode*, const char* property_name);
static char* read_filename(xmlNode*, const char* property_name, const char* base_url);
static uint64_t str_to_uint64(const char*, int* error);
static dash_profile_t read_profile(xmlNode*, dash_profile_t parent_profile);
static const char* dash_profile_to_string(dash_profile_t);
static bool read_range(xmlNode*, const char* property_name, uint64_t* start_out, uint64_t* end_out);
static uint64_t convert_timescale(uint64_t time, uint64_t timescale);

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

    free(obj->initialization_segment);
    g_ptr_array_free(obj->periods, true);

    free(obj);
}

void mpd_print(const mpd_t* mpd)
{
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
        PRINT_PROPERTY(indent, "presentation_type: %s", PRINT_STR(mpd_type));
    } else {
        PRINT_PROPERTY(indent, "presentation_type: %d", mpd->presentation_type);
    }
    PRINT_PROPERTY(indent, "profile: %s", dash_profile_to_string(mpd->profile));

    for (size_t i = 0; i < mpd->periods->len; ++i) {
        PRINT_PROPERTY(indent, "periods[%zu]:", i);
        period_print(g_ptr_array_index(mpd->periods, i), indent);
    }
}

period_t* period_new(void)
{
    period_t* obj = calloc(1, sizeof(*obj));
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
    ++indent;
    PRINT_PROPERTY(indent, "bitstream_switching: %s", PRINT_BOOL(period->bitstream_switching));
    for (size_t i = 0; i < period->adaptation_sets->len; ++i) {
        PRINT_PROPERTY(indent, "adaptation_sets[%zu]:", i);
        adaptation_set_print(g_ptr_array_index(period->adaptation_sets, i), indent + 1);
    }
}

adaptation_set_t* adaptation_set_new(void)
{
    adaptation_set_t* obj = calloc(1, sizeof(*obj));
    obj->representations = g_ptr_array_new_with_free_func((GDestroyNotify)representation_free);
    return obj;
}

void adaptation_set_free(adaptation_set_t* obj)
{
    if(obj == NULL) {
        return;
    }

    xmlFree(obj->id);
    g_ptr_array_free(obj->representations, true);

    free(obj);
}

void adaptation_set_print(const adaptation_set_t* adaptation_set, unsigned indent)
{
    PRINT_PROPERTY(indent, "id: %s", adaptation_set->id);
    PRINT_PROPERTY(indent, "profile: %s", dash_profile_to_string(adaptation_set->profile));
    PRINT_PROPERTY(indent, "audio_pid: %"PRIu32, adaptation_set->audio_pid);
    PRINT_PROPERTY(indent, "video_pid: %"PRIu32, adaptation_set->video_pid);
    print_optional_uint32(indent, "segment_alignment", adaptation_set->segment_alignment);
    print_optional_uint32(indent, "subsegment_alignment", adaptation_set->subsegment_alignment);
    PRINT_PROPERTY(indent, "bitstream_switching: %s", PRINT_BOOL(adaptation_set->bitstream_switching));
    for (size_t i = 0; i < adaptation_set->representations->len; ++i) {
        PRINT_PROPERTY(indent, "representations[%zu]:", i);
        representation_print(g_ptr_array_index(adaptation_set->representations, i), indent + 1);
    }
}

representation_t* representation_new(void)
{
    representation_t* obj = calloc(1, sizeof(*obj));
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
    g_free(obj->index_file_name);
    g_free(obj->initialization_file_name);
    g_free(obj->bitstream_switching_file_name);
    g_ptr_array_free(obj->subrepresentations, true);
    g_ptr_array_free(obj->segments, true);

    free(obj);
}

void representation_print(const representation_t* representation, unsigned indent)
{
    PRINT_PROPERTY(indent, "profile: %s", dash_profile_to_string(representation->profile));
    PRINT_PROPERTY(indent, "id: %s", representation->id);
    PRINT_PROPERTY(indent, "index_file_name: %s", representation->index_file_name);
    PRINT_RANGE(indent, representation, index_range);
    PRINT_PROPERTY(indent, "initialization_file_name: %s", representation->initialization_file_name);
    PRINT_RANGE(indent, representation, initialization_range);
    PRINT_PROPERTY(indent, "bitstream_switching_file_name: %s", representation->bitstream_switching_file_name);
    PRINT_RANGE(indent, representation, bitstream_switching_range);
    PRINT_PROPERTY(indent, "start_with_sap: %u", representation->start_with_sap);
    PRINT_PROPERTY(indent, "presentation_time_offset: %"PRIu64, representation->presentation_time_offset);
    PRINT_PROPERTY(indent, "timescale: %"PRIu32, representation->timescale);
    for (size_t i = 0; i < representation->subrepresentations->len; ++i) {
        PRINT_PROPERTY(indent, "subrepresentation[%zu]:", i);
        subrepresentation_print(g_ptr_array_index(representation->subrepresentations, i), indent + 1);
    }
    for (size_t i = 0; i < representation->segments->len; ++i) {
        PRINT_PROPERTY(indent, "segments[%zu]:", i);
        segment_print(g_ptr_array_index(representation->segments, i), indent + 1);
    }
}

subrepresentation_t* subrepresentation_new(void)
{
    subrepresentation_t* obj = calloc(1, sizeof(*obj));
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
    PRINT_PROPERTY(indent, "profile: %s", dash_profile_to_string(obj->profile));
    PRINT_PROPERTY(indent, "start_with_sap: %"PRIu8, obj->start_with_sap);
    if (obj->has_level) {
        PRINT_PROPERTY(indent, "level: %"PRIu32, obj->level);
    }
    PRINT_PROPERTY(indent, "bandwidth: %"PRIu32, obj->bandwidth);
    for (size_t i = 0; i < obj->dependency_level->len; ++i) {
        PRINT_PROPERTY(indent, "dependency_level[%zu]: %"PRIu32, i, g_array_index(obj->dependency_level, uint32_t, i));
    }
    for (size_t i = 0; i < obj->content_component->len; ++i) {
        PRINT_PROPERTY(indent, "content_component[%zu]: %s", i, (char*)g_ptr_array_index(obj->content_component, i));
    }
}

segment_t* segment_new(void)
{
    segment_t* obj = calloc(1, sizeof(*obj));
    return obj;
}

void segment_free(segment_t* obj)
{
    if(obj == NULL) {
        return;
    }

    g_free(obj->file_name);
    g_free(obj->index_file_name);

    if (obj->arg_free && obj->arg) {
        obj->arg_free(obj->arg);
    }
    free(obj);
}

void segment_print(const segment_t* segment, unsigned indent)
{
    PRINT_PROPERTY(indent, "file_name: %s", PRINT_STR(segment->file_name));
    PRINT_RANGE(indent, segment, media_range);
    PRINT_PROPERTY(indent, "start: %"PRIu64, segment->start);
    PRINT_PROPERTY(indent, "duration: %"PRIu64, segment->duration);
    PRINT_PROPERTY(indent, "index_file_name: %s", PRINT_STR(segment->index_file_name));
    PRINT_RANGE(indent, segment, index_range);
}

static mpd_t* mpd_read(xmlDoc* doc, char* base_url)
{
    g_return_val_if_fail(doc, NULL);
    g_return_val_if_fail(base_url, NULL);

    mpd_t* mpd = mpd_new();
    xmlNode* root = xmlDocGetRootElement(doc);
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
    char* base_url = NULL;
    mpd_t* mpd = NULL;
    xmlDoc* doc = xmlReadFile(file_name, NULL, 0);
    if (doc == NULL) {
        g_critical("Could not parse MPD file: %s.", file_name);
        goto cleanup;
    }
    base_url = g_path_get_dirname(file_name);

    mpd = mpd_read(doc, base_url);

cleanup:
    g_free(base_url);
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
    bool return_code = true;

    period_t* period = period_new();
    g_ptr_array_add(mpd->periods, period);

    char* base_url = find_base_url(node, parent_base_url);
    period->bitstream_switching = read_bool(node, "bitstreamSwitching");

    xmlNode* segment_base = NULL;
    xmlNode* segment_list = NULL;
    xmlNode* segment_template = NULL;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "SegmentBase")) {
            segment_base = cur_node;
        } else if (xmlStrEqual(cur_node->name, "SegmentList")) {
            segment_base = cur_node;
        } else if (xmlStrEqual(cur_node->name, "SegmentTemplate")) {
            segment_base = cur_node;
        }
    }

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (xmlStrEqual(cur_node->name, "AdaptationSet")) {
            if(!read_adaptation_set(cur_node, mpd, period, base_url, segment_base, segment_list, segment_template)) {
                goto fail;
            }
        }
    }

cleanup:
    g_free(base_url);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_adaptation_set(xmlNode* node, mpd_t* mpd, period_t* period, char* parent_base_url,
        xmlNode* parent_segment_base, xmlNode* parent_segment_list, xmlNode* parent_segment_template)
{
    bool return_code = true;

    adaptation_set_t* adaptation_set = adaptation_set_new();
    g_ptr_array_add(period->adaptation_sets, adaptation_set);

    bool saw_mime_type = false;
    char* mime_type = xmlGetProp(node, "mimeType");
    if (mime_type) {
        if (xmlStrEqual (mime_type, "video/mp2t")) {
            saw_mime_type = true;
        }
        xmlFree(mime_type);
    }

    adaptation_set->id = xmlGetProp(node, "id");
    adaptation_set->profile = read_profile(node, mpd->profile);
    adaptation_set->segment_alignment = read_optional_uint32(node, "segmentAlignment");
    adaptation_set->subsegment_alignment = read_optional_uint32(node, "subsegmentAlignment");
    adaptation_set->bitstream_switching = period->bitstream_switching || read_bool(node, "bitstreamSwitching");

    char* base_url = find_base_url(node, parent_base_url);

    /* read segmentAlignment, subsegmentAlignment, subsegmentStartsWithSAP attributes? */
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "Representation")) {
            if(!read_representation(cur_node, adaptation_set, saw_mime_type, base_url, parent_segment_base,
                    parent_segment_list, parent_segment_template)) {
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
    g_free(base_url);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_representation(xmlNode* node, adaptation_set_t* adaptation_set, bool saw_mime_type, char* parent_base_url,
        xmlNode* parent_segment_base, xmlNode* parent_segment_list, xmlNode* parent_segment_template)
{
    char* start_with_sap = NULL;
    char* mime_type = NULL;
    bool return_code = true;
    char* base_url =  NULL;

    mime_type = xmlGetProp(node, "mimeType");
    if (!saw_mime_type && (!mime_type || !xmlStrEqual (mime_type, "video/mp2t"))) {
        g_warning("Ignoring <Representation> with mimeType=\"%s\".", mime_type ? mime_type : "(null)");
        goto cleanup;
    }

    representation_t* representation = representation_new();
    g_ptr_array_add(adaptation_set->representations, representation);

    representation->profile = read_profile(node, adaptation_set->profile);
    representation->id = xmlGetProp(node, "id");
    representation->bandwidth = read_uint64(node, "bandwidth");

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
        if (xmlStrEqual(cur_node->name, "SegmentList")) {
            if(!read_segment_list(cur_node, representation, base_url, parent_segment_list)) {
                goto fail;
            }
        } else if (xmlStrEqual(cur_node->name, "SegmentBase")) {
            if (!read_segment_base(cur_node, representation, base_url, parent_segment_base)) {
                goto fail;
            }
            segment_t* segment = segment_new();
            segment->file_name = g_strdup(base_url);
            segment->start = representation->presentation_time_offset;
            g_ptr_array_add(representation->segments, segment);
        }  else if (xmlStrEqual(cur_node->name, "SegmentTemplate")) {
            if (!read_segment_template(cur_node, representation, base_url, parent_segment_template)) {
                goto fail;
            }
        } else if (xmlStrEqual(cur_node->name, "SubRepresentation")) {
            if (!read_subrepresentation(cur_node, representation)) {
                goto fail;
            }
        }
    }

cleanup:
    g_free(base_url);
    xmlFree(mime_type);
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

    subrepresentation_t* subrepresentation = subrepresentation_new();
    g_ptr_array_add(representation->subrepresentations, subrepresentation);

    if (xmlHasProp(node, "level")) {
        subrepresentation->has_level = true;
        subrepresentation->level = read_uint64(node, "level");
    }
    subrepresentation->profile = read_profile(node, representation->profile);
    subrepresentation->bandwidth = read_uint64(node, "bandwidth");

    char* content_component = xmlGetProp(node, "contentComponent");
    if (content_component) {
        size_t start = 0;
        while (content_component[start] == ' ' || content_component[start] == '\t') {
            ++start;
        }
        if (content_component[start]) {
            for (size_t end = start + 1; ; ++end) {
                if (content_component[end] == 0 || content_component[end] == ' ' || content_component[end] == '\t') {
                    g_ptr_array_add(subrepresentation->content_component,
                            g_strndup(content_component + start, end - start));
                    if (content_component[end] == 0) {
                        break;
                    }
                    while (content_component[end] == ' ' || content_component[end] == '\t') {
                        ++end;
                    }
                    start = end;
                }
            }
        }
    }

    char* dependency_level = xmlGetProp(node, "dependencyLevel");
    if (dependency_level) {
        size_t start = 0;
        while (dependency_level[start] == ' ' || dependency_level[start] == '\t') {
            ++start;
        }
        if (dependency_level[start]) {
            for (size_t end = 1; ; ++end) {
                bool last = dependency_level[end] == 0;
                if (last || dependency_level[end] == ' ' || dependency_level[end] == '\t') {
                    dependency_level[end] = 0;
                    int error = 0;
                    uint64_t cc_int = str_to_uint64(dependency_level + start, &error);
                    if (error || cc_int > UINT32_MAX) {
                        g_print("SubRepresentation@dependencyLevel %s is not an xs:unsignedInt.",
                                content_component + start);
                        goto fail;
                    }
                    g_array_append_val(subrepresentation->dependency_level, cc_int);
                    if (last) {
                        break;
                    }
                    ++end;
                    while (dependency_level[end] == ' ' || dependency_level[end] == '\t') {
                        ++end;
                    }
                    start = end;
                }
            }
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
    xmlFree(start_with_sap);
    xmlFree(dependency_level);
    xmlFree(content_component);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_segment_base(xmlNode* node, representation_t* representation, char* base_url, xmlNode* parent_segment_base)
{
    bool return_code = true;

    if (xmlHasProp(node, "timescale")) {
        representation->timescale = read_uint64(node, "timescale");
    } else if (xmlHasProp(parent_segment_base, "timescale")) {
        representation->timescale = read_uint64(parent_segment_base, "timescale");
    } else {
        representation->timescale = 1;
    }

    representation->presentation_time_offset = convert_timescale(read_uint64(node, "presentationTimeOffset"),
                                                                 representation->timescale);

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "RepresentationIndex")) {
            if (representation->index_file_name != NULL) {
                g_critical("Ignoring duplicate index file in <%s>.", node->name);
                goto fail;
            }
            representation->index_file_name = read_filename(cur_node, "sourceURL", base_url);
        } else if (xmlStrEqual(cur_node->name, "Initialization")) {
            if (representation->initialization_file_name != NULL) {
                g_critical("Ignoring duplicate initialization segment in <%s>.", node->name);
                goto fail;
            }
            representation->initialization_file_name = read_filename(cur_node, "sourceURL", base_url);
        } else if (xmlStrEqual(cur_node->name, "BitstreamSwitching")) {
            if (representation->bitstream_switching_file_name != NULL) {
                g_critical("Duplicate <BitstreamSwitching> segment in <%s>.", node->name);
                goto fail;
            }
            representation->bitstream_switching_file_name = read_filename(cur_node, "sourceURL", base_url);
            if(!read_range(node, "range", &representation->bitstream_switching_range_start,
                    &representation->bitstream_switching_range_end)) {
                goto fail;
            }
        }
    }
cleanup:
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_segment_list(xmlNode* node, representation_t* representation, char* base_url, xmlNode* parent_segment_list)
{
    bool return_code = read_segment_base(node, representation, base_url, NULL);
    char* duration_str = NULL;

    /* Get SegmentTimeline first, since we need it to create segments */
    GPtrArray* segment_timeline = NULL;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (xmlStrEqual(cur_node->name, "SegmentTimeline")) {
            if (segment_timeline != NULL) {
                g_critical("Saw multiple <SegmentTimeline> children for one <SegmentList>.");
                goto fail;
            }
            segment_timeline = read_segment_timeline(cur_node, representation);
        }
    }

    duration_str = xmlGetProp(node, "duration");
    if (duration_str == NULL && segment_timeline == NULL) {
        duration_str = xmlGetProp(parent_segment_list, "duration");
    }
    uint64_t duration = 0;
    if (duration_str) {
        if (segment_timeline != NULL) {
            g_critical("<SegmentList> cannot have both duration attribute and <SegmentTimeline>. Pick one or the other.");
            goto fail;
        }
        int error;
        duration = str_to_uint64(duration_str, &error);
        if (error) {
            g_critical("<SegmentList> duration \"%s\" is not a 64-bit number.", duration_str);
            goto fail;
        }
    } else if (segment_timeline == NULL) {
        g_critical("No duration or <SegmentTimeline> found for <SegmentList>.");
        goto fail;
    }
    duration = convert_timescale(duration, representation->timescale);

    if (!xmlHasProp(node, "presentationTimeOffset")) {
        representation->presentation_time_offset = read_uint64(parent_segment_list, "presentationTimeOffset");
    }

    size_t timeline_i = 0;
    uint64_t start = representation->presentation_time_offset;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (xmlStrEqual(cur_node->name, "SegmentURL")) {
            if (segment_timeline != NULL) {
                segment_timeline_s_t* s = g_ptr_array_index(segment_timeline, timeline_i);
                start = s->start;
                duration = s->duration;
            }
            if(!read_segment_url(cur_node, representation, start, duration, base_url)) {
                goto fail;
            }
            ++timeline_i;
            start += duration;
        }
    }

cleanup:
    xmlFree(duration_str);
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
    GPtrArray* timeline = g_ptr_array_new_with_free_func(free);
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "S")) {
            char* t = xmlGetProp(cur_node, "t");
            char* d = xmlGetProp(cur_node, "d");
            char* r = xmlGetProp(cur_node, "r");
            int error;
            bool failed = false;
            uint64_t start = str_to_uint64(t, &error);
            if (error) {
                g_critical("<S>'s @t value (%s) is not a number.", t);
                failed = true;
            }
            uint64_t duration = str_to_uint64(d, &error);
            if (error) {
                g_critical("<S>'s @d value (%s) is not a number.", d);
                failed = true;
            }
            uint64_t repeat = str_to_uint64(r, &error);
            if (error) {
                g_critical("<S>'s @r value (%s) is not a number.", r);
                failed = true;
            }
            xmlFree(t);
            xmlFree(d);
            xmlFree(r);
            if (failed) {
                goto fail;
            }

            for (uint64_t i = 0; i < repeat; ++i) {
                segment_timeline_s_t* s = malloc(sizeof(*s));
                s->start = convert_timescale(start, representation->timescale);
                s->duration = convert_timescale(duration, representation->timescale);
                start += duration;
                g_ptr_array_add(timeline, s);
            }
        } else {
            g_debug("Ignoring element <%s> in <SegmentTimeline>.", cur_node->name);
        }
    }
    return timeline;
fail:
    g_ptr_array_free(timeline, true);
    return NULL;
}

bool read_segment_url(xmlNode* node, representation_t* representation, uint64_t start, uint64_t duration, char* base_url)
{
    segment_t* segment = segment_new();

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
    if(!read_range(node, "indexRange", &segment->index_range_start, &segment->index_range_end)) {
        goto fail;
    }
    g_ptr_array_add(representation->segments, segment);
    return true;
fail:
    segment_free(segment);
    return false;
}

static char* segment_template_replace(char* pattern, uint64_t segment_number, representation_t* representation,
        uint64_t start_time, const char* base_url)
{
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

    with_base = g_build_filename(base_url, result->str, NULL);
fail:
    g_string_free(result, true);
    return with_base;
}

static bool read_segment_template(xmlNode* node, representation_t* representation, char* base_url, xmlNode* parent)
{
    char* media_template = NULL;
    char* index_template = NULL;
    char* initialization_template = NULL;
    char* bitstream_switching_template = NULL;
    char* duration_str = NULL;

    bool return_code = read_segment_base(node, representation, base_url, NULL);

    /* Get SegmentTimeline first, since we need it to create segments */
    GPtrArray* segment_timeline = NULL;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (xmlStrEqual(cur_node->name, "SegmentTimeline")) {
            if (segment_timeline != NULL) {
                g_critical("Saw multiple <SegmentTimeline> children for one <SegmentTemplate>.");
                goto fail;
            }
            segment_timeline = read_segment_timeline(cur_node, representation);
        }
    }

    media_template = xmlGetProp(node, "media");
    if (!media_template) {
        media_template = xmlGetProp(parent, "media");
    }
    if (!media_template) {
        g_critical("<SegmentTemplate> has no @media attribute.");
        goto fail;
    }

    index_template = xmlGetProp(node, "index");
    if (!index_template) {
        index_template = xmlGetProp(parent, "index");
    }

    initialization_template = xmlGetProp(node, "initialization");
    if (!initialization_template) {
        initialization_template = xmlGetProp(parent, "initialization");
    }
    if (initialization_template) {
        representation->initialization_file_name = segment_template_replace(initialization_template, 0, representation,
                0, base_url);
        if (!representation->initialization_file_name) {
            goto fail;
        }
    }

    bitstream_switching_template = xmlGetProp(node, "bitstreamSwitching");
    if (bitstream_switching_template == NULL && !representation->bitstream_switching_file_name) {
        bitstream_switching_template = xmlGetProp(parent, "bitstreamSwitching");
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

    uint64_t duration = 0;
    duration_str = xmlGetProp(node, "duration");
    if (duration_str == NULL && !segment_timeline) {
        duration_str = xmlGetProp(parent, "duration");
    }
    if (duration_str) {
        if (segment_timeline) {
            g_critical("SegmentTemplate> has both @duration and <SegmentTimeline>, but you can only use one or the "
                    "other.");
            goto fail;
        }
        int error;
        duration = str_to_uint64(duration_str, &error);
        if (error) {
            g_critical("<SegmentTemplate> duration \"%s\" is not a 64-bit number.", duration_str);
            goto fail;
        }
    }
    duration = convert_timescale(duration, representation->timescale);

    uint64_t start_number = 1;
    size_t timeline_i = 0;
    uint64_t start_time = representation->presentation_time_offset;
    for (size_t i = start_number; segment_timeline == NULL || timeline_i < segment_timeline->len; ++i, ++timeline_i) {
        char* media = segment_template_replace(media_template, i, representation, start_time, base_url);
        if (!media) {
            goto fail;
        }

        /* Figure out if this file exists */
        GFile* file = g_file_new_for_path(media);
        GError* error = NULL;
        GFileInputStream* in = g_file_read(file, NULL, &error);
        if (in) {
            g_object_unref(in);
        }
        g_object_unref(file);
        if (error) {
            break;
        }

        segment_t* segment = segment_new();
        segment->file_name = media;
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
        if (index_template) {
            segment->index_file_name = segment_template_replace(index_template, i, representation, start_time, base_url);
        }
        g_ptr_array_add(representation->segments, segment);

        start_time += duration;
    }

cleanup:
    g_ptr_array_free(segment_timeline, true);
    xmlFree(media_template);
    xmlFree(index_template);
    xmlFree(initialization_template);
    xmlFree(bitstream_switching_template);
    xmlFree(duration_str);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

char* find_base_url(xmlNode* node, char* parent_url)
{
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE && xmlStrEqual(cur_node->name, "BaseURL")) {
            char* base_url;
            char* content = xmlNodeGetContent(cur_node);
            if (parent_url) {
                base_url = g_build_filename(parent_url, content, NULL);
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
    char* value = xmlGetProp(node, property_name);
    optional_uint32_t result;
    if (value == 0 || xmlStrEqual(value, "false")) {
        result.has_int = false;
        result.b = false;
    } else if (xmlStrEqual(value, "true")) {
        result.has_int = false;
        result.b = true;
    } else {
        int error;
        uint32_t tmp = str_to_uint64(value, &error);
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
    if (value.has_int) {
        PRINT_PROPERTY(indent, "%s: %"PRIu32, name, value.i);
    } else {
        PRINT_PROPERTY(indent, "%s: %s", name, PRINT_BOOL(value.b));
    }
}

uint32_t read_uint64(xmlNode* node, const char* property_name)
{
    char* value = xmlGetProp(node, property_name);
    if (value == NULL) {
        return 0;
    }
    int error;
    uint64_t result = str_to_uint64(value, &error);
    if (error) {
        g_warning("Got invalid unsignedLong for property %s: %s", property_name, value);
    }
    xmlFree(value);
    return result;
}

bool read_bool(xmlNode* node, const char* property_name)
{
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

uint64_t str_to_uint64(const char* str, int* error)
{
    uint64_t result = 0;
    for (size_t i = 0; str[i]; ++i) {
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

static dash_profile_t read_profile(xmlNode* node, dash_profile_t parent_profile)
{
    dash_profile_t profile = DASH_PROFILE_UNKNOWN;
    char* property = xmlGetProp(node, "profiles");
    if (property == NULL) {
        goto cleanup;
    }

    gchar** profiles = g_strsplit(property, ",", 0);
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
    bool ret = true;
    char* property = xmlGetProp(node, property_name);
    if (property == NULL) {
        goto cleanup;
    }

    char** split = g_strsplit(property, "-", 0);
    /* Length of the split string should be exactly 2 */
    if (split[0] == NULL || split[1] == NULL || split[2] != NULL) {
        goto fail;
    }

    int error;
    uint64_t start = str_to_uint64(split[0], &error);
    if (error) {
        goto fail;
    }
    uint64_t end = str_to_uint64(split[1], &error);
    if (error) {
        goto fail;
    }
    if (start_out) {
        *start_out = start;
    }
    if (end_out) {
        *end_out = end;
    }
cleanup:
    xmlFree(property);
    return ret;
fail:
    g_critical("Range %s is not a valid byte range.", property);
    ret = false;
    goto cleanup;
}

#define MPEG_TS_TIMESCALE 90000

static uint64_t convert_timescale(uint64_t time, uint64_t timescale)
{
    // This function is complicated because we want to avoid rounding if an exact result is possible
    if (time == 0 || timescale == MPEG_TS_TIMESCALE) {
        return time;
    } else if (timescale > MPEG_TS_TIMESCALE) {
        return time / (timescale / MPEG_TS_TIMESCALE);
    } else {
        return (MPEG_TS_TIMESCALE / timescale) * time;
    }
}