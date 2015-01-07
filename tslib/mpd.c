
#include "mpd.h"

#include <glib.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <libxml/tree.h>
#include <pcre.h>
#include "log.h"


/* Ignore warnings for implicitly casting const char* to const xmlChar* (i.e. const unsigned char*) */
#pragma GCC diagnostic ignored "-Wpointer-sign"

#define DUMP_PROPERTY(indent, printf_str, ...) \
g_debug("%.*s"printf_str, \
        indent < sizeof(INDENT_BUFFER) ? indent : (int)sizeof(INDENT_BUFFER), \
        INDENT_BUFFER, __VA_ARGS__)


typedef struct {
    uint64_t start;
    uint64_t duration;
} segment_timeline_s_t;


bool read_period(xmlNode*, mpd_t*, char* base_url);
bool read_adaptation_set(xmlNode*, period_t*, char* base_url);
bool read_representation(xmlNode*, adaptation_set_t*, bool saw_mime_type, char* base_url);
bool read_segment_list(xmlNode*, representation_t*, char* base_url);
GPtrArray* read_segment_timeline(xmlNode*);
bool read_segment_url(xmlNode*, representation_t*, uint64_t start, uint64_t duration, char* base_url);
char* find_base_url(xmlNode*, char* parent_base_url);
uint32_t read_optional_uint32(xmlNode*, const char* property_name);
uint32_t read_uint64(xmlNode*, const char* property_name);
bool read_bool(xmlNode*, const char* property_name);
char* read_filename(xmlNode*, const char* property_name, const char* base_url);
uint64_t str_to_uint64(const char*, size_t length);

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

void mpd_dump(const mpd_t* mpd)
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
        DUMP_PROPERTY(indent, "presentation_type: %s", PRINT_STR(mpd_type));
    } else {
        DUMP_PROPERTY(indent, "presentation_type: %d", mpd->presentation_type);
    }

    for (size_t i = 0; i < mpd->periods->len; ++i) {
        DUMP_PROPERTY(indent, "periods[%zu]:", i);
        period_dump(g_ptr_array_index(mpd->periods, i), indent);
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

void period_dump(const period_t* period, unsigned indent)
{
    ++indent;
    for (size_t i = 0; i < period->adaptation_sets->len; ++i) {
        DUMP_PROPERTY(indent, "adaptation_sets[%zu]:", i);
        adaptation_set_dump(g_ptr_array_index(period->adaptation_sets, i), indent);
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

    g_ptr_array_free(obj->representations, true);

    free(obj);
}

void adaptation_set_dump(const adaptation_set_t* adaptation_set, unsigned indent)
{
    ++indent;
    DUMP_PROPERTY(indent, "audio_pid: %"PRIu32, adaptation_set->audio_pid);
    DUMP_PROPERTY(indent, "video_pid: %"PRIu32, adaptation_set->video_pid);
    DUMP_PROPERTY(indent, "segment_alignment: %"PRIu32, adaptation_set->segment_alignment);
    DUMP_PROPERTY(indent, "subsegment_alignment: %"PRIu32, adaptation_set->subsegment_alignment);
    DUMP_PROPERTY(indent, "bitstream_switching: %s", PRINT_BOOL(adaptation_set->bitstream_switching));
    for (size_t i = 0; i < adaptation_set->representations->len; ++i) {
        DUMP_PROPERTY(indent, "representations[%zu]:", i);
        representation_dump(g_ptr_array_index(adaptation_set->representations, i), indent);
    }
}

representation_t* representation_new(void)
{
    representation_t* obj = calloc(1, sizeof(*obj));
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
    g_ptr_array_free(obj->segments, true);

    free(obj);
}

void representation_dump(const representation_t* representation, unsigned indent)
{
    ++indent;
    DUMP_PROPERTY(indent, "id: %s", PRINT_STR(representation->id));
    DUMP_PROPERTY(indent, "index_file_name: %s", PRINT_STR(representation->index_file_name));
    DUMP_PROPERTY(indent, "start_with_sap: %u", representation->start_with_sap);
    DUMP_PROPERTY(indent, "presentation_time_offset: %"PRIu64, representation->presentation_time_offset);
    for (size_t i = 0; i < representation->segments->len; ++i) {
        DUMP_PROPERTY(indent, "segments[%zu]:", i);
        segment_dump(g_ptr_array_index(representation->segments, i), indent + 1);
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
    xmlFree(obj->media_range);
    g_free(obj->index_file_name);
    xmlFree(obj->index_range);

    dash_validator_destroy(&obj->validator);

    free(obj);
}

void segment_dump(const segment_t* segment, unsigned indent)
{
    DUMP_PROPERTY(indent, "file_name: %s", PRINT_STR(segment->file_name));
    DUMP_PROPERTY(indent, "media_range: %s", PRINT_STR(segment->media_range));
    DUMP_PROPERTY(indent, "start: %"PRIu64, segment->start);
    DUMP_PROPERTY(indent, "duration: %"PRIu64, segment->duration);
    DUMP_PROPERTY(indent, "index_file_name: %s", PRINT_STR(segment->index_file_name));
    DUMP_PROPERTY(indent, "index_range: %s", PRINT_STR(segment->index_range));
}

mpd_t* read_mpd(char* file_name)
{
    mpd_t* mpd = NULL;
    char* base_url = NULL;
    xmlDoc* doc = xmlReadFile(file_name, NULL, 0);
    if (doc == NULL) {
        g_critical("Could not parse MPD %s.", file_name);
        goto fail;
    }

    mpd = mpd_new();
    xmlNode* root = xmlDocGetRootElement(doc);
    if (root->type != XML_ELEMENT_NODE) {
        g_critical("MPD error, toplevel element is not an element.");
        goto fail;
    }
    if (!xmlStrEqual (root->name, "MPD")) {
        g_critical("MPD error, top level element is not an <MPD>, got <%s> instead.", root->name);
        goto fail;
    }

    xmlChar* type = xmlGetProp(root, "type");
    if (xmlStrEqual(type, "static")) {
        mpd->presentation_type = MPD_PRESENTATION_STATIC;
    } else if (xmlStrEqual(type, "dynamic")) {
        mpd->presentation_type = MPD_PRESENTATION_DYNAMIC;
    }
    xmlFree(type);

    /* Ignore BaseURL here because we want a local path */
    //char* base_url = find_base_url(root, NULL);
    base_url = g_path_get_dirname(file_name);
    puts(base_url);

    for (xmlNode* cur_node = root->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "Period")) {
            if(!read_period(cur_node, mpd, base_url)) {
                goto fail;
            }
        } else if (xmlStrEqual(cur_node->name, "BaseURL")) {
            /* Ignore */
        } else {
            g_debug("Ignoring element <%s> in <MPD>.", cur_node->name);
        }
    }

cleanup:
    g_free(base_url);
    xmlFreeDoc(doc);
    return mpd;
fail:
    mpd_free(mpd);
    mpd = NULL;
    goto cleanup;
}

bool read_period(xmlNode* node, mpd_t* mpd, char* parent_base_url)
{
    bool return_code = true;

    period_t* period = period_new();
    g_ptr_array_add(mpd->periods, period);

    char* base_url = find_base_url(node, parent_base_url);

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "AdaptationSet")) {
            if(!read_adaptation_set(cur_node, period, base_url)) {
                goto fail;
            }
        } else if (xmlStrEqual(cur_node->name, "BaseURL")) {
            /* Ignore */
        } else {
            g_debug("Ignoring element <%s> in <Period>.", cur_node->name);
        }
    }

cleanup:
    g_free(base_url);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_adaptation_set(xmlNode* node, period_t* period, char* parent_base_url)
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

    adaptation_set->segment_alignment = read_optional_uint32(node, "segmentAlignment");
    adaptation_set->subsegment_alignment = read_optional_uint32(node, "subsegmentAlignment");
    adaptation_set->bitstream_switching = read_bool(node, "bitstreamSwitching");

    char* base_url = find_base_url(node, parent_base_url);

    /* read segmentAlignment, subsegmentAlignment, subsegmentStartsWithSAP attributes? */
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "Representation")) {
            if(!read_representation(cur_node, adaptation_set, saw_mime_type, base_url)) {
                goto fail;
            }
        } else if (xmlStrEqual(cur_node->name, "BaseURL")) {
            /* Ignore */
        } else if (xmlStrEqual(cur_node->name, "ContentComponent")) {
            xmlChar* type = xmlGetProp(cur_node, "contentType");
            if (xmlStrEqual(type, "video")) {
                adaptation_set->video_pid = read_uint64(cur_node, "id");
            } else if (xmlStrEqual(type, "audio")) {
                adaptation_set->audio_pid = read_uint64(cur_node, "id");
            }
            xmlFree(type);
        } else {
            g_debug("Ignoring element <%s> in <AdaptationSet>.", cur_node->name);
        }
    }

cleanup:
    g_free(base_url);
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

bool read_representation(xmlNode* node, adaptation_set_t* adaptation_set, bool saw_mime_type, char* parent_base_url)
{
    char* start_with_sap = NULL;
    char* mime_type = NULL;
    bool return_code = true;

    mime_type = xmlGetProp(node, "mimeType");
    if (!saw_mime_type && (!mime_type || !xmlStrEqual (mime_type, "video/mp2t"))) {
        g_warning("Ignoring <Representation> with mimeType=\"%s\".", mime_type ? mime_type : "(null)");
        goto cleanup;
    }

    representation_t* representation = representation_new();
    g_ptr_array_add(adaptation_set->representations, representation);

    representation->id = xmlGetProp(node, "id");

    start_with_sap = xmlGetProp(node, "startWithSAP");
    if (start_with_sap) {
        int tmp = atoi(start_with_sap);
        if (tmp < 0 || tmp > 6) {
            g_critical("Invalid startWithSap value of %s. Must be in the range [0-6].", start_with_sap);
            goto fail;
        }
        representation->start_with_sap = tmp;
    }

    char* base_url = find_base_url(node, parent_base_url);

    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(cur_node->name, "SegmentList")) {
            if(!read_segment_list(cur_node, representation, base_url)) {
                goto fail;
            }
        } else if (xmlStrEqual(cur_node->name, "BaseURL")) {
            /* Ignore */
        } else {
            g_debug("Ignoring element <%s> in <Representation>.", cur_node->name);
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

bool read_segment_list(xmlNode* node, representation_t* representation, char* parent_base_url)
{
    bool return_code = true;

    /* Get SegmentTimeline first, since we need it to create segments */
    GPtrArray* segment_timeline = NULL;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE && xmlStrEqual(cur_node->name, "SegmentTimeline")) {
            if (segment_timeline != NULL) {
                g_critical("Saw multiple <SegmentTimeline> children for one <SegmentList>.");
                goto fail;
            }
            segment_timeline = read_segment_timeline(cur_node);
        }
    }

    char* base_url = find_base_url(node, parent_base_url);

    char* duration_str = xmlGetProp(node, "duration");
    uint64_t duration = 0;
    if (duration_str) {
        if (segment_timeline != NULL) {
            g_critical("<SegmentList> cannot have both duration attribute and <SegmentTimeline>. Pick one or the other.");
            goto fail;
        }
        /* todo: Use something safer than strtoull */
        duration = strtoull(duration_str, NULL, 10);
        xmlFree(duration_str);
    } else if (segment_timeline == NULL) {
        g_critical("No duration or <SegmentList> found for <SegmentList>.");
        goto fail;
    }

    representation->presentation_time_offset = read_uint64(node, "presentationTimeOffset");

    size_t timeline_i = 0;
    uint64_t start = 0;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }
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
        } else if (xmlStrEqual(cur_node->name, "RepresentationIndex")) {
            if (representation->index_file_name != NULL) {
                g_warning("Ignoring duplicate index file in <Representation>.");
                continue;
            }
            representation->index_file_name = read_filename(cur_node, "sourceURL", base_url);
        } else if (xmlStrEqual(cur_node->name, "SegmentTimeline")) {
            /* Ignore */
        } else {
            g_debug("Ignoring element <%s> in <SegmentList>.", cur_node->name);
        }
    }

cleanup:
    g_free(base_url);
    if (segment_timeline != NULL) {
        g_ptr_array_free(segment_timeline, true);
    }
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

GPtrArray* read_segment_timeline(xmlNode* node)
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
            uint64_t start = strtoull(t, NULL, 10);
            uint64_t duration = strtoull(d, NULL, 10);
            uint64_t repeat = strtoull(r, NULL, 10);
            xmlFree(t);
            xmlFree(d);
            xmlFree(r);

            for (uint64_t i = 0; i < repeat; ++i) {
                segment_timeline_s_t* s = malloc(sizeof(*s));
                s->start = start;
                s->duration = duration;
                start += duration;
                g_ptr_array_add(timeline, s);
            }
        } else {
            g_debug("Ignoring element <%s> in <SegmentTimeline>.", cur_node->name);
        }
    }
    return timeline;
}

bool read_segment_url(xmlNode* node, representation_t* representation, uint64_t start, uint64_t duration, char* base_url)
{
    segment_t* segment = segment_new();
    g_ptr_array_add(representation->segments, segment);

    segment->start = start + representation->presentation_time_offset;
    segment->duration = duration;
    /* Might as well calculate this once */
    segment->end = segment->start + duration;

    segment->file_name = read_filename(node, "media", base_url);
    segment->media_range = xmlGetProp(node, "mediaRange");
    segment->index_file_name = read_filename(node, "index", base_url);
    segment->index_range = xmlGetProp(node, "indexRange");
    return true;
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

uint32_t read_optional_uint32(xmlNode* node, const char* property_name)
{
    char* value = xmlGetProp(node, property_name);
    if (value == 0) {
        return 0;
    }
    uint32_t result;
    if (xmlStrEqual(value, "false")) {
        result = 0;
    } else if (xmlStrEqual(value, "true")) {
        result = 1;
    } else {
        result = strtoull(value, NULL, 10);
        if (value && result == 0 && !xmlStrEqual(value, "0")) {
            g_warning("Got invalid ConditionalUintType for property %s: %s", property_name, value);
        }
    }
    xmlFree(value);
    return result;
}

uint32_t read_uint64(xmlNode* node, const char* property_name)
{
    char* value = xmlGetProp(node, property_name);
    if (value == NULL) {
        return 0;
    }
    uint64_t result = strtoull(value, NULL, 10);
    if (value && result == 0 && !xmlStrEqual(value, "0")) {
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
    if (property == NULL) {
        return NULL;
    }

    char* filename;
    if (base_url) {
        filename = g_build_filename(base_url, property, NULL);
    } else {
        /* Make a duplicate so we always return strings allocated by glib. */
        filename = g_strdup(property);
    }
    xmlFree(property);
    return filename;
}

uint64_t str_to_uint64(const char* str, size_t length)
{
    uint64_t result = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = str[i];
        if (c < '0' || c > '9') {
            g_warning("Invalid non-digit in string to parse: %s.", str);
            return 0;
        }
        result = result * 10 + c - '0';
    }
    return result;
}