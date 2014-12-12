
#include "mpd.h"

#include <glib.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <libxml/tree.h>
#include <pcre.h>
#include "log.h"


/* Ignore warnings for implicitly casting const char* to const xmlChar* (i.e. const unsigned char*) */
#pragma GCC diagnostic ignored "-Wpointer-sign"

#define DUMP_PROPERTY(indent, printf_str, ...) \
printf("%.*s"printf_str"\n", \
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
varray_t* read_segment_timeline(xmlNode*);
bool read_segment_url(xmlNode*, representation_t*, uint64_t start, uint64_t duration, char* base_url);
char* find_base_url(xmlNode*, char* parent_base_url);
uint32_t read_optional_uint32(xmlNode*, const char* property_name);
uint32_t read_uint64(xmlNode*, const char* property_name);
uint64_t read_duration(xmlNode*, const char* property_name);
bool read_bool(xmlNode*, const char* property_name);
uint64_t str_to_uint64(const char*, size_t length);

const char INDENT_BUFFER[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

mpd_t* mpd_new(void)
{
    mpd_t* obj = calloc(1, sizeof(*obj));
    obj->periods = varray_new();
    return obj;
}

void mpd_free(mpd_t* obj)
{
    if (obj == NULL) {
        return;
    }

    free(obj->initialization_segment);
    for (size_t i = 0; i < obj->periods->length; ++i) {
        period_free(varray_get(obj->periods, i));
    }
    varray_free(obj->periods);

    free(obj);
}

void mpd_dump(const mpd_t* mpd)
{
    printf("MPD:\n");
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

    for (size_t i = 0; i < mpd->periods->length; ++i) {
        DUMP_PROPERTY(indent, "periods[%zu]:", i);
        period_dump(varray_get(mpd->periods, i), indent);
    }
}

period_t* period_new(void)
{
    period_t* obj = calloc(1, sizeof(*obj));
    obj->adaptation_sets = varray_new();
    return obj;
}

void period_free(period_t* obj)
{
    if(obj == NULL) {
        return;
    }

    for (size_t i = 0; i < obj->adaptation_sets->length; ++i) {
        adaptation_set_free(varray_get(obj->adaptation_sets, i));
    }
    varray_free(obj->adaptation_sets);

    free(obj);
}

void period_dump(const period_t* period, unsigned indent)
{
    if (indent == 0) {
        printf("Period:\n");
    }
    ++indent;
    DUMP_PROPERTY(indent, "start: %"PRIu64, period->start);
    DUMP_PROPERTY(indent, "duration: %"PRIu64, period->duration);
    for (size_t i = 0; i < period->adaptation_sets->length; ++i) {
        DUMP_PROPERTY(indent, "adaptation_sets[%zu]:", i);
        adaptation_set_dump(varray_get(period->adaptation_sets, i), indent);
    }
}

adaptation_set_t* adaptation_set_new(void)
{
    adaptation_set_t* obj = calloc(1, sizeof(*obj));
    obj->representations = varray_new();
    return obj;
}

void adaptation_set_free(adaptation_set_t* obj)
{
    if(obj == NULL) {
        return;
    }

    for(size_t i = 0; i < obj->representations->length; ++i) {
        representation_free(varray_get(obj->representations, i));
    }
    varray_free(obj->representations);

    free(obj);
}

void adaptation_set_dump(const adaptation_set_t* adaptation_set, unsigned indent)
{
    if (indent == 0) {
        printf("AdaptationSet:\n");
    }
    ++indent;
    DUMP_PROPERTY(indent, "segment_alignment: %"PRIu32, adaptation_set->segment_alignment);
    DUMP_PROPERTY(indent, "subsegment_alignment: %"PRIu32, adaptation_set->subsegment_alignment);
    DUMP_PROPERTY(indent, "bitstream_switching: %s", PRINT_BOOL(adaptation_set->bitstream_switching));
    for (size_t i = 0; i < adaptation_set->representations->length; ++i) {
        DUMP_PROPERTY(indent, "representations[%zu]:", i);
        representation_dump(varray_get(adaptation_set->representations, i), indent);
    }
}

representation_t* representation_new(void)
{
    representation_t* obj = calloc(1, sizeof(*obj));
    obj->segments = varray_new();
    return obj;
}

void representation_free(representation_t* obj)
{
    if(obj == NULL) {
        return;
    }

    g_free(obj->index_file_name);
    freeIFrames(obj->segment_iframes, obj->segments->length);
    for(size_t i = 0; i < obj->segments->length; ++i) {
        segment_free(varray_get(obj->segments, i));
    }
    varray_free(obj->segments);

    free(obj);
}

void representation_dump(const representation_t* representation, unsigned indent)
{
    if (indent == 0) {
        printf("Representation:\n");
    }
    ++indent;
    DUMP_PROPERTY(indent, "index_file_name: %s", PRINT_STR(representation->index_file_name));
    DUMP_PROPERTY(indent, "start_with_sap: %u", representation->start_with_sap);
    DUMP_PROPERTY(indent, "presentation_time_offset: %"PRIu64, representation->presentation_time_offset);
    for (size_t i = 0; i < representation->segments->length; ++i) {
        DUMP_PROPERTY(indent, "segments[%zu]:", i);
        segment_dump(varray_get(representation->segments, i), indent + 1);
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

    free(obj);
}

void segment_dump(const segment_t* segment, unsigned indent)
{
    if (indent == 0) {
        printf("Segment:\n");
    }
    DUMP_PROPERTY(indent, "file_name: %s", PRINT_STR(segment->file_name));
    DUMP_PROPERTY(indent, "start: %"PRIu64, segment->start);
    DUMP_PROPERTY(indent, "duration: %"PRIu64, segment->duration);
    DUMP_PROPERTY(indent, "index_file_name: %s", PRINT_STR(segment->index_file_name));
}

mpd_t* read_mpd(char* file_name)
{
    mpd_t* mpd = NULL;
    xmlDoc* doc = xmlReadFile(file_name, NULL, 0);
    if (doc == NULL) {
        LOG_ERROR_ARGS("Could not parse MPD %s.", file_name);
        goto fail;
    }

    mpd = mpd_new();
    xmlNode* root = xmlDocGetRootElement(doc);
    if (root->type != XML_ELEMENT_NODE) {
        LOG_ERROR("MPD error, toplevel element is not an element.");
        goto fail;
    }
    if (!xmlStrEqual (root->name, "MPD")) {
        LOG_ERROR_ARGS("MPD error, top level element is not an <MPD>, got <%s> instead.", root->name);
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
    char* base_url = g_path_get_dirname(file_name);
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
            LOG_DEBUG_ARGS("Ignoring element <%s> in <MPD>.", cur_node->name);
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
    varray_add(mpd->periods, period);

    period->start = read_duration(node, "start");
    period->duration = read_duration(node, "duration");

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
            LOG_DEBUG_ARGS("Ignoring element <%s> in <Period>.", cur_node->name);
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
    varray_add(period->adaptation_sets, adaptation_set);

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
            }
            xmlFree(type);
        } else {
            LOG_DEBUG_ARGS("Ignoring element <%s> in <AdaptationSet>.", cur_node->name);
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
        LOG_WARN_ARGS("Ignoring <Representation> with mimeType=\"%s\".", mime_type ? mime_type : "(null)");
        goto cleanup;
    }

    representation_t* representation = representation_new();
    varray_add(adaptation_set->representations, representation);

    start_with_sap = xmlGetProp(node, "startWithSAP");
    if (start_with_sap) {
        int tmp = atoi(start_with_sap);
        if (tmp < 0 || tmp > 6) {
            LOG_ERROR_ARGS("Invalid startWithSap value of %s. Must be in the range [0-6].", start_with_sap);
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
            LOG_DEBUG_ARGS("Ignoring element <%s> in <Representation>.", cur_node->name);
        }
    }

    representation->segment_iframes = calloc(representation->segments->length,
            sizeof(data_segment_iframes_t));

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
    varray_t* segment_timeline = NULL;
    for (xmlNode* cur_node = node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE && xmlStrEqual(cur_node->name, "SegmentTimeline")) {
            if (segment_timeline != NULL) {
                LOG_ERROR("Saw multiple <SegmentTimeline> children for one <SegmentList>.");
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
            LOG_ERROR("<SegmentList> cannot have both duration attribute and <SegmentTimeline>. Pick one or the other.");
            goto fail;
        }
        /* todo: Use something safer than strtoull */
        duration = strtoull(duration_str, NULL, 10);
        xmlFree(duration_str);
    } else if (segment_timeline == NULL) {
        LOG_ERROR("No duration or <SegmentList> found for <SegmentList>.");
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
                segment_timeline_s_t* s = varray_get(segment_timeline, timeline_i);
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
                LOG_WARN("Ignoring duplicate index file in <Representation>.");
                continue;
            }
            xmlChar* index_file_name = xmlGetProp(cur_node, "sourceURL");
            if (base_url) {
                representation->index_file_name = g_build_filename(base_url, index_file_name, NULL);
            } else {
                representation->index_file_name = g_strdup(index_file_name);
            }
            xmlFree(index_file_name);
        } else if (xmlStrEqual(cur_node->name, "SegmentTimeline")) {
            /* Ignore */
        } else {
            LOG_DEBUG_ARGS("Ignoring element <%s> in <SegmentList>.", cur_node->name);
        }
    }

cleanup:
    g_free(base_url);
    if (segment_timeline != NULL) {
        for (size_t i = 0; i < segment_timeline->length; ++i) {
            free(varray_get(segment_timeline, i));
        }
        varray_free(segment_timeline);
    }
    return return_code;
fail:
    return_code = false;
    goto cleanup;
}

varray_t* read_segment_timeline(xmlNode* node)
{
    varray_t* timeline = varray_new();
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
                varray_add(timeline, s);
            }
        } else {
            LOG_DEBUG_ARGS("Ignoring element <%s> in <SegmentTimeline>.", cur_node->name);
        }
    }
    return timeline;
}

bool read_segment_url(xmlNode* node, representation_t* representation, uint64_t start, uint64_t duration, char* base_url)
{
    segment_t* segment = segment_new();
    varray_add(representation->segments, segment);

    segment->start = start;
    segment->duration = duration;
    xmlChar* media = xmlGetProp(node, "media");
    if (base_url) {
        segment->file_name = g_build_filename(base_url, media, NULL);
    } else {
        /* Make a duplicate so all of our strings will use the same allocator. */
        segment->file_name = g_strdup(media);
    }
    xmlFree(media);
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
            LOG_WARN_ARGS("Got invalid ConditionalUintType for property %s: %s", property_name, value);
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
        LOG_WARN_ARGS("Got invalid unsignedLong for property %s: %s", property_name, value);
    }
    xmlFree(value);
    return result;
}

uint64_t read_duration(xmlNode* node, const char* property_name)
{
    uint64_t result = 0;
    char* value = xmlGetProp(node, property_name);
    if (value == NULL) {
        goto cleanup;
    }

    const char* error;
    int error_offset;
    pcre* re = pcre_compile("P((?<year>[0-9]+)Y)?((?<month>[0-9]+)M)?((?<day>[0-9]+)D)?(T((?<hour>[0-9]+)H)?((?<minute>[0-9]+)M)?((?<second>[0-9]+(\\.[0-9]+)?)S)?)?", 0, &error, &error_offset, NULL);
    if (re == NULL) {
        LOG_WARN_ARGS("PCRE compilation error %s at offset %d.", error, error_offset);
        goto cleanup;
    }

    int output_vector[14 * 3];
    int output_length = 14 * 3;

    int return_code = pcre_exec(re, NULL, value, xmlStrlen(value), 0, 0, output_vector, output_length);
    if (return_code < 0) {
        switch(return_code) {
        case PCRE_ERROR_NOMATCH:
            LOG_WARN_ARGS("Duration %s does not match duration regex.", value);
            goto cleanup;
        default:
            LOG_WARN_ARGS("Duration %s caused PCRE matching error: %d.", value, return_code);
            goto cleanup;
        }
    }
    if (return_code == 0) {
        LOG_WARN_ARGS("PCRE output vector size of %d is not big enough.", output_length / 3);
        goto cleanup;
    }

    int namecount;
    int name_entry_size;
    unsigned char* name_table;
    pcre_fullinfo(re, NULL, PCRE_INFO_NAMECOUNT, &namecount);

    if (namecount <= 0) {
        LOG_WARN("No named substrings. PCRE might be broken?");
        goto cleanup;
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
            uint64_t field_value_int = str_to_uint64(field_value, field_value_length);
            if (strncmp(field_name, "second", field_name_length) == 0) {
                result += field_value_int;
            } else if (strncmp(field_name, "minute", field_name_length) == 0) {
                result += field_value_int * 60;
            } else if (strncmp(field_name, "hour", field_name_length) == 0) {
                result += field_value_int * 3600;
            } else if (strncmp(field_name, "day", field_name_length) == 0) {
                result += field_value_int * 86400;
            } else if (strncmp(field_name, "month", field_name_length) == 0) {
                /* note: number of seconds in a month is undefined by ISO 6801.
                   This is an approximation, assuming 30.6 days per month. */
                result += field_value_int * 2643840;
            } else if (strncmp(field_name, "year", field_name_length) == 0) {
                /* note: number of seconds in a year is undefined by ISO 6801.
                   This is an approximation, assuming 365.25 days per year. */
                result += field_value_int * 31557600;
            } else {
                LOG_WARN_ARGS("Unknown field: %.*s = %.*s.", field_name_length,
                        field_name, field_value_length, field_value);
            }
        }
        tabptr += name_entry_size;
    }

cleanup:
    pcre_free(re);
    xmlFree(value);
    return result;
}

bool read_bool(xmlNode* node, const char* property_name)
{
    char* value = xmlGetProp(node, property_name);
    bool result = xmlStrEqual(value, "true");
    if (value && !result && !xmlStrEqual(value, "false")) {
        LOG_WARN_ARGS("Got invalid value for boolean property %s: %s", property_name, value);
    }
    xmlFree(value);
    return result;
}

uint64_t str_to_uint64(const char* str, size_t length)
{
    uint64_t result = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = str[i];
        if (c < '0' || c > '9') {
            LOG_WARN_ARGS("Invalid non-digit in string to parse: %s.", str);
            return 0;
        }
        result = result * 10 + c - '0';
    }
    return result;
}

/*
void parseSegInfoFileLine(char* line, char* segFileNames, int segNum, int numSegments)
{
    if(strlen(line) == 0) {
        return;
    }

    char* pch = strtok(line, ",\r\n");
    int repNum = 0;

    while(pch != NULL) {
        int arrayIndex = getArrayIndex(repNum, segNum, numSegments);

        sscanf(pch, "%s", segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
        LOG_INFO_ARGS("fileNames[%d][%d] = %s", segNum, repNum,
                      segFileNames + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
        pch = strtok(NULL, ",\r\n");
        repNum++;
    }
}

void parseRepresentationIndexFileLine(char* line, char* representationIndexFileNames,
                                      int numRepresentations)
{
    int repIndex = 0;

    if(strlen(line) == 0) {
        return;
    }

    char* pch = strtok(line, ",\r\n");
    while(pch != NULL) {
        sscanf(pch, "%s", representationIndexFileNames + repIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
        pch = strtok(NULL, ",\r\n");
        repIndex++;
    }
}

int getNumRepresentations(char* fname, int* numRepresentations, int* numSegments)
{
    char line[SEGMENT_FILE_NAME_MAX_LENGTH];
    FILE* segInfoFile = fopen(fname, "r");
    if(!segInfoFile) {
        LOG_ERROR_ARGS("Couldn't open segInfoFile %s\n", fname);
        return -1;
    }

    *numSegments = 0;
    while(1) {
        if(fgets(line, sizeof(line), segInfoFile) == NULL) {
            if(feof(segInfoFile) != 0) {
                break;
            } else {
                LOG_ERROR_ARGS("ERROR: cannot read line from SegInfoFile %s\n", fname);
                return -1;
            }
        }

        if(strncmp("Segment.", line, strlen("Segment.")) == 0) {
            int segmentIndex = 0;
            char* temp1 = strchr(line, '.');
            char* temp2 = strchr(line, '=');

            // parse to get num reps
            char* temp3 = temp2;
            *numRepresentations = 1;
            while(1) {
                char* temp4 = strchr(temp3, ',');
                if(temp4 == NULL) {
                    break;
                }

                temp3 = temp4 + 1;
                (*numRepresentations)++;
            }

            temp2 = 0;  // terminate string
            if(sscanf(temp1 + 1, "%d", &segmentIndex) != 1) {
                LOG_ERROR("ERROR parsing segment index\n");
                return -1;
            }

            if(segmentIndex > (*numSegments)) {
                *numSegments = segmentIndex;
            }
        }
    }

    fclose(segInfoFile);

    LOG_INFO_ARGS("NumRepresentations = %d", *numRepresentations);
    LOG_INFO_ARGS("NumSegments = %d", *numSegments);

    return 0;
}

mpd_t* read_mpd(char* file_name, int* error_code)
{
    char representationIndexSegmentString[1024];

    if(getNumRepresentations(fname, numRepresentations, numSegments) < 0) {
        LOG_ERROR("Error reading segment_info file");
        return -1;
    }

    FILE* segInfoFile = fopen(fname, "r");
    if(!segInfoFile) {
        LOG_ERROR_ARGS("Couldn't open segInfoFile %s\n", fname);
        return 0;
    }

    char line[SEGMENT_FILE_NAME_MAX_LENGTH];
    *segFileNames = (char*)calloc((*numRepresentations) * (*numSegments), SEGMENT_FILE_NAME_MAX_LENGTH);
    *segDurations = (int*)calloc(*numSegments, sizeof(int));
    *representationIndexFileNames = calloc(*numRepresentations, SEGMENT_FILE_NAME_MAX_LENGTH);
    *segmentIndexFileNames = calloc((*numSegments) * (*numRepresentations),
                                    SEGMENT_FILE_NAME_MAX_LENGTH);


    while(1) {
        if(fgets(line, sizeof(line), segInfoFile) == NULL) {
            if(feof(segInfoFile) != 0) {
                return 0;
            } else {
                LOG_ERROR_ARGS("ERROR: cannot read line from SegInfoFile %s\n", fname);
                return -1;
            }
        }


        char* trimmedLine = trimWhitespace(line);

        if(strncmp("#", trimmedLine, strlen("#")) == 0 || strlen(trimmedLine) == 0) {
            // comment -- skip line
            continue;
        }

        char* temp = strchr(trimmedLine, '=');
        if(temp == NULL) {
            LOG_ERROR_ARGS("Error parsing line in SegInfoFile %s", fname);
            continue;
        }

        if(strncmp("PresentationTimeOffset_PTSTicks", trimmedLine,
                   strlen("PresentationTimeOffset_PTSTicks")) == 0) {
            if(sscanf(temp + 1, "%d", presentationTimeOffset) != 1) {
                LOG_ERROR_ARGS("Error parsing PresentationTimeOffset_PTSTicks in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("PresentationTimeOffset_PTSTicks = %d", *presentationTimeOffset);
        } else if(strncmp("VideoPID", trimmedLine, strlen("VideoPID")) == 0) {
            if(sscanf(temp + 1, "%d", videoPID) != 1) {
                LOG_ERROR_ARGS("Error parsing VideoPID in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("VideoPID = %d", *videoPID);
        } else if(strncmp("ExpectedSAPType", trimmedLine, strlen("ExpectedSAPType")) == 0) {
            if(sscanf(temp + 1, "%d", expectedSAPType) != 1) {
                LOG_ERROR_ARGS("Error parsing ExpectedSAPType in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("ExpectedSAPType = %d", *expectedSAPType);
        } else if(strncmp("MaxAudioGap_PTSTicks", trimmedLine, strlen("MaxAudioGap_PTSTicks")) == 0) {
            if(sscanf(temp + 1, "%d", maxAudioGapPTSTicks) != 1) {
                LOG_ERROR_ARGS("Error parsing MaxVideoGap_PTSTicks in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("MaxAudioGap_PTSTicks = %d", *maxAudioGapPTSTicks);
        } else if(strncmp("MaxVideoGap_PTSTicks", trimmedLine, strlen("MaxVideoGap_PTSTicks")) == 0) {
            if(sscanf(temp + 1, "%d", maxVideoGapPTSTicks) != 1) {
                LOG_ERROR_ARGS("Error parsing MaxVideoGap_PTSTicks in SegInfoFile %s", fname);
            }
            LOG_INFO_ARGS("MaxVideoGap_PTSTicks = %d", *maxVideoGapPTSTicks);
        } else if(strncmp("InitializationSegment", trimmedLine, strlen("InitializationSegment")) == 0) {
            char* temp2 = trimWhitespace(temp + 1);
            if(strlen(temp2) != 0) {
                if(sscanf(temp2, "%s", initializationSegment) != 1) {
                    LOG_ERROR_ARGS("Error parsing InitializationSegment in SegInfoFile %s", fname);
                }
                LOG_INFO_ARGS("InitializationSegment = %s", initializationSegment);
            } else {
                LOG_INFO("No InitializationSegment");
            }
        } else if(strncmp("RepresentationIndexSegment", trimmedLine,
                          strlen("RepresentationIndexSegment")) == 0) {
            char* temp2 = trimWhitespace(temp + 1);
            if(sscanf(temp2, "%s", representationIndexSegmentString) != 1) {
                LOG_ERROR_ARGS("Error parsing RepresentationIndexSegment in SegInfoFile %s", fname);
            } else {
                parseRepresentationIndexFileLine(representationIndexSegmentString, *representationIndexFileNames,
                                                 *numRepresentations);

                for(int i = 0; i < *numRepresentations; i++) {
                    LOG_INFO_ARGS("RepresentationIndexFile[%d] = %s", i,
                                  (*representationIndexFileNames) + i * SEGMENT_FILE_NAME_MAX_LENGTH);
                }
            }
        } else if(strncmp("Segment.", trimmedLine, strlen("Segment.")) == 0) {
            int segmentIndex = 0;
            char* temp1 = strchr(trimmedLine, '.');
            *temp = 0;
            char* temp3 = trimWhitespace(temp + 1);
            if(sscanf(temp1 + 1, "%d", &segmentIndex) != 1) {
                LOG_ERROR_ARGS("Error parsing Segment line in SegInfoFile %s", fname);
            } else {
                segmentIndex -= 1;
                parseSegInfoFileLine(temp3, *segFileNames, segmentIndex, *numSegments);
//               LOG_INFO_ARGS ("segDuration[%d] = %d", segmentIndex, (*segDurations)[segmentIndex]);
            }
        } else if(strncmp("Segment_DurationPTSTicks.", trimmedLine,
                          strlen("Segment_DurationPTSTicks.")) == 0) {
            int segmentIndex = 0;
            char* temp1 = strchr(trimmedLine, '.');
            *temp = 0;
            if(sscanf(temp1 + 1, "%d", &segmentIndex) != 1) {
                LOG_ERROR_ARGS("Error parsing Segment_DurationPTSTicks line in SegInfoFile %s", fname);
            } else {
                segmentIndex -= 1;
                if(sscanf(temp + 1, "%d", &((*segDurations)[segmentIndex])) != 1) {
                    LOG_ERROR_ARGS("Error parsing segDuration in SegInfoFile %s", fname);
                }

                LOG_INFO_ARGS("segDuration[%d] = %d", segmentIndex, (*segDurations)[segmentIndex]);
            }
        } else if(strncmp("Segment_IndexFile.", trimmedLine, strlen("Segment_IndexFile.")) == 0) {
            int segmentIndex = 0;
            char* temp1 = strchr(trimmedLine, '.');
            *temp = 0;
            char* temp2 = trimWhitespace(temp + 1);
            if(sscanf(temp1 + 1, "%d", &segmentIndex) != 1) {
                LOG_ERROR_ARGS("Error parsing Segment_IndexFile line in SegInfoFile %s", fname);
            } else {
                segmentIndex -= 1;
                parseSegInfoFileLine(temp2, *segmentIndexFileNames, segmentIndex, *numSegments);
                for(int i = 0; i < *numRepresentations; i++) {
                    int arrayIndex = getArrayIndex(i, segmentIndex, *numSegments);
                    LOG_INFO_ARGS("Segment_IndexFile[%d][%d] = %s", segmentIndex, i,
                                  (*segmentIndexFileNames) + arrayIndex * SEGMENT_FILE_NAME_MAX_LENGTH);
                }
            }
        }
    }

    return 0;
}

int readIntFromSegInfoFile(FILE* segInfoFile, char* paramName, int* paramValue)
{
    char line[1024];
    char temp[1024];

    strcpy(temp, paramName);
    strcat(temp, " = %d\n");

    if(fgets(line, sizeof(line), segInfoFile) == NULL) {
        LOG_ERROR_ARGS("ERROR: cannot read %s from SegInfoFile\n", paramName);
        return -1;
    }
    if(sscanf(line, temp, paramValue) != 1) {
        LOG_ERROR_ARGS("ERROR: cannot parse %s from SegInfoFile\n", paramName);
        return -1;
    }
    LOG_INFO_ARGS("%s = %d", paramName, *paramValue);

    return 0;
}

int readStringFromSegInfoFile(FILE* segInfoFile, char* paramName, char* paramValue)
{
    char line[1024];
    char temp[1024];

    strcpy(temp, paramName);
    strcat(temp, " = %s\n");

    if(fgets(line, sizeof(line), segInfoFile) == NULL) {
        LOG_ERROR_ARGS("ERROR: cannot read %s from SegInfoFile\n", paramName);
        return -1;
    }
    if(sscanf(line, temp, paramValue) != 1) {
        strcpy(temp, paramName);
        strcat(temp, " =");
        line [strlen(temp)] = 0;

        if(strcmp(line, temp) == 0) {
            paramValue[0] = 0;
            return 0;
        }

        LOG_ERROR_ARGS("ERROR: cannot parse %s from SegInfoFile\n", paramName);
        return -1;
    }
    LOG_INFO_ARGS("%s = %s", paramName, paramValue);


    return 0;
}

char* trimWhitespace(char* string)
{
    int stringSz = strlen(string);
    int index = 0;
    while(index < stringSz) {
        if(string[index] != ' ' && string[index] != '\t' && string[index] != '\n'
                && string[index] != '\r') {
            break;
        }

        index++;
    }

    char* newString = string + index;
    if(index == stringSz) {
        return newString;
    }

    stringSz = strlen(newString);
    index = stringSz - 1;
    while(index >= 0) {
        if(newString[index] != ' ' && newString[index] != '\t' && newString[index] != '\n'
                && newString[index] != '\r') {
            break;
        }

        index--;
    }

    newString[index + 1] = 0;

    return newString;
}
*/