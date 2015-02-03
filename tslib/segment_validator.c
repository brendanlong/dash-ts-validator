
#include "segment_validator.h"

#include <inttypes.h>
#include <errno.h>

#include "h264_stream.h"
#include "mpeg2ts_demux.h"
#include "pes_demux.h"


static int pat_processor(mpeg2ts_stream_t* m2s, void* arg);
static int pmt_processor(mpeg2ts_program_t* m2p, void* arg);
static int validate_ts_packet(ts_packet_t* ts, elementary_stream_info_t* es_info, void* arg);
static int validate_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GQueue* ts_queue,
        void* arg);

static void validate_representation_index_segment_boxes(index_segment_validator_t*, box_t**, size_t num_boxes,
        representation_t*, adaptation_set_t*);
static void validate_single_index_segment_boxes(index_segment_validator_t*, box_t**, size_t num_boxes,
        segment_t*, representation_t*, adaptation_set_t*);

static int validate_emsg_msg(uint8_t* buffer, size_t len, unsigned segment_duration);
static int analyze_sidx_references(data_sidx_t*, int* num_iframes, int* num_nested_sidx, dash_profile_t);


const char* content_component_to_string(content_component_t content_component)
{
    switch(content_component) {
    case UNKNOWN_CONTENT_COMPONENT:
        return "Unknown";
    case VIDEO_CONTENT_COMPONENT:
        return "Video";
    case AUDIO_CONTENT_COMPONENT:
        return "Audio";
    default:
        g_error("Bad content component: %d", content_component);
        return "Bad Content Component Value";
    }
}

static index_segment_validator_t* index_segment_validator_new(void)
{
    index_segment_validator_t* obj = g_new0(index_segment_validator_t, 1);
    obj->segment_iframes = g_ptr_array_new();
    return obj;
}

void index_segment_validator_free(index_segment_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }

    for (size_t i = 0; i < obj->segment_iframes->len; ++i) {
        GArray* iframes = g_ptr_array_index(obj->segment_iframes, i);
        g_array_free(iframes, true);
    }
    g_ptr_array_free(obj->segment_iframes, true);
    g_free(obj);
}

static pid_validator_t* pid_validator_new(uint16_t pid, content_component_t content_component)
{
    pid_validator_t* obj = calloc(1, sizeof(*obj));
    obj->pid = pid;
    obj->content_component = content_component;
    obj->ecm_pids = g_ptr_array_new();
    return obj;
}

static void pid_validator_free(pid_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }
    g_ptr_array_free(obj->ecm_pids, true);
    free(obj);
}

dash_validator_t* dash_validator_new(segment_type_t segment_type, dash_profile_t profile)
{
    dash_validator_t* obj = calloc(1, sizeof(*obj));
    dash_validator_init(obj, segment_type, profile);
    return obj;
}

void dash_validator_init(dash_validator_t* obj, segment_type_t segment_type, dash_profile_t profile)
{
    obj->profile = profile;
    obj->iframes = g_array_new(false, false, sizeof(iframe_t));
    obj->pids = g_ptr_array_new_with_free_func((GDestroyNotify)pid_validator_free);
}

void dash_validator_destroy(dash_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }
    g_array_free(obj->iframes, true);
    g_ptr_array_free(obj->pids, true);
}

void dash_validator_free(dash_validator_t* obj)
{
    dash_validator_destroy(obj);
    free(obj);
}

static int pat_processor(mpeg2ts_stream_t* m2s, void* arg)
{
    dash_validator_t* dash_validator = (dash_validator_t*)arg;
    if (m2s->programs->len != 1) {
        g_critical("DASH Conformance: 6.4.4.2  Media segments shall contain exactly one program (%u found)",
                       m2s->programs->len);
        dash_validator->status = 0;
        return 0;
    }

    if (dash_validator->use_initialization_segment) {
        g_critical("DASH Conformance: No PAT allowed if initialization segment is used");
        dash_validator->status = 0;
        return 0;
    }

    for (gsize i = 0; i < m2s->programs->len; i++) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);
        m2p->pmt_processor = pmt_processor;
        m2p->arg = dash_validator;
    }

    return 1;
}

static pid_validator_t* dash_validator_find_pid(int pid, dash_validator_t* dash_validator)
{
    for (gsize i = 0; i < dash_validator->pids->len; ++i) {
        pid_validator_t* pv = g_ptr_array_index(dash_validator->pids, i);
        if (pv->pid == pid) {
            return pv;
        }
    }
    return NULL;
}

static int pmt_processor(mpeg2ts_program_t* m2p, void* arg)
{
    dash_validator_t* dash_validator = (dash_validator_t*)arg;
    if (m2p->pmt == NULL) {  // if we don't have any PSI, there's nothing we can do
        return 0;
    }

    if (dash_validator->use_initialization_segment) {
        g_critical("DASH Conformance: No PMT allowed if initialization segment is used");
        dash_validator->status = 0;
        return 0;
    }

    dash_validator->pcr_pid = m2p->pmt->pcr_pid;
    dash_validator->pmt_program_number = m2p->pmt->program_number;
    dash_validator->pmt_version_number = m2p->pmt->version_number;

    GHashTableIter i;
    g_hash_table_iter_init(&i, m2p->pids);
    pid_info_t* pi;
    while (g_hash_table_iter_next(&i, NULL, (void**)&pi)) {
        int process_pid = 0;
        content_component_t content_component = UNKNOWN_CONTENT_COMPONENT;
        uint16_t pid = pi->es_info->elementary_pid;

        pid_validator_t* pid_validator = dash_validator_find_pid(pid, dash_validator);

// TODO: we need to figure out what we do when section versions change
// Do we need to fix something? Profile something out?
        assert(pid_validator == NULL);

        switch (pi->es_info->stream_type) {
        case STREAM_TYPE_MPEG2_VIDEO:
        case STREAM_TYPE_AVC:
        case STREAM_TYPE_HEVC:

        case  STREAM_TYPE_MPEG1_VIDEO:
        case  STREAM_TYPE_MPEG4_VIDEO:
        case  STREAM_TYPE_SVC:
        case  STREAM_TYPE_MVC:
        case  STREAM_TYPE_S3D_SC_MPEG2:
        case  STREAM_TYPE_S3D_SC_AVC:
            process_pid = 1;
            content_component = VIDEO_CONTENT_COMPONENT;
            dash_validator->video_pid = pid;
            break;
        case  STREAM_TYPE_MPEG1_AUDIO:
        case  STREAM_TYPE_MPEG2_AUDIO:
        case  STREAM_TYPE_MPEG4_AAC_RAW:
        case  STREAM_TYPE_MPEG2_AAC:
        case  STREAM_TYPE_MPEG4_AAC:
            process_pid = 1;
            content_component = AUDIO_CONTENT_COMPONENT;
            dash_validator->audio_pid = pid;
            break;
        default:
            process_pid = 0;
        }

        if (process_pid) {
            // hook PES validation to PES demuxer
            pes_demux_t* pd = pes_demux_new(validate_pes_packet);
            pd->pes_arg = dash_validator;

            // hook PES demuxer to the PID processor
            demux_pid_handler_t* demux_handler = demux_pid_handler_new(pes_demux_process_ts_packet);
            demux_handler->arg = pd;
            demux_handler->arg_destructor = (arg_destructor_t)pes_demux_free;

            // hook PES demuxer to the PID processor
            demux_pid_handler_t* demux_validator = demux_pid_handler_new(validate_ts_packet);
            demux_validator->arg = dash_validator;

            // hook PID processor to PID
            mpeg2ts_program_register_pid_processor(m2p, pi->es_info->elementary_pid,
                    demux_handler, demux_validator);

            pid_validator = pid_validator_new(pid, content_component);
            g_ptr_array_add(dash_validator->pids, pid_validator);
            // TODO: parse CA descriptors, add ca system and ecm_pid if they don't exist yet
        }
    }
    return 1;
}

static int copy_pmt_info(mpeg2ts_program_t* m2p, dash_validator_t* dash_validator_source,
        dash_validator_t* dash_validator_dest)
{
    dash_validator_dest->pcr_pid = dash_validator_source->pcr_pid;

    g_debug("copy_pmt_info: dash_validator_source->pids->len = %u",
            dash_validator_source->pids->len);
    for (gsize i = 0; i < dash_validator_source->pids->len; ++i) {
        pid_validator_t* pid_validator_src = g_ptr_array_index(dash_validator_source->pids, i);
        int content_component = 0;
        uint16_t pid = pid_validator_src->pid;

        // hook PES validation to PES demuxer
        pes_demux_t* pd = pes_demux_new(validate_pes_packet);
        pd->pes_arg = dash_validator_source;

        // hook PES demuxer to the PID processor
        demux_pid_handler_t* demux_handler = demux_pid_handler_new(pes_demux_process_ts_packet);
        demux_handler->arg = pd;
        demux_handler->arg_destructor = (arg_destructor_t)pes_demux_free;

        // hook PES demuxer to the PID processor
        demux_pid_handler_t* demux_validator = demux_pid_handler_new(validate_ts_packet);
        demux_validator->arg = dash_validator_source;

        // hook PID processor to PID
        mpeg2ts_program_register_pid_processor(m2p, pid, demux_handler, demux_validator);

        pid_validator_t* pid_validator_dest = pid_validator_new(pid, content_component);

        g_debug("copy_pmt_info: adding pid_validator %"PRIxPTR" for PID %d", (uintptr_t)pid_validator_dest,
                      pid);
        g_ptr_array_add(dash_validator_dest->pids, pid_validator_dest);
        // TODO: parse CA descriptors, add ca system and ecm_pid if they don't exist yet
    }

    return 0;
}

static int validate_ts_packet(ts_packet_t* ts, elementary_stream_info_t* esi, void* arg)
{
    dash_validator_t* dash_validator = (dash_validator_t*)arg;
    if (ts == NULL) {
        return 0;
    }

    pid_validator_t* pid_validator = dash_validator_find_pid(ts->header.pid, dash_validator);
    assert(pid_validator != NULL);

    if (dash_validator->pcr_pid == ts->header.pid) {
        int64_t pcr = ts_read_pcr(ts);
        if (PCR_IS_VALID(pcr)) {
            if (dash_validator->segment_type == INITIALIZATION_SEGMENT) {
                g_critical("DASH Conformance: 6.4.3.2: \"PCR-bearing packets shall not be present in the "
                        "Initialization Segment;\"");
                dash_validator->status = 0;
            }
            dash_validator->last_pcr = pcr;
        }
    }

    // This is the first TS packet from this PID
    if (pid_validator->ts_count == 0) {
        pid_validator->continuity_counter = ts->header.continuity_counter;

        // we ignore non-payload and non-media packets
        if ((ts->header.adaptation_field_control & TS_PAYLOAD)
                && (pid_validator->content_component != UNKNOWN_CONTENT_COMPONENT)) {
            // if we only have complete PES packets, we must start with PUSI=1 followed by PES header in the first payload-bearing packet
            if ((dash_validator->profile >= DASH_PROFILE_MPEG2TS_MAIN)
                    && (ts->header.payload_unit_start_indicator == 0)) {
                g_critical("DASH Conformance: media segments shall contain only complete PES packets");
                dash_validator->status = 0;
            }

            // by the time we get to the start of the first PES, we need to have seen at least one PCR.
            if (dash_validator->profile >= DASH_PROFILE_MPEG2TS_SIMPLE) {
                if (!PCR_IS_VALID(dash_validator->last_pcr)) {
                    g_critical("DASH Conformance: PCR must be present before first bytes of media data");
                    dash_validator->status = 0;
                }
            }
        }
    }

    // TODO: check for discontinuities
    //       -> CC errors, declared discontinuities
    //       -> PCR-PCR distances, with some arbitrary tolerance
    // we need to figure out what to do here -- we can let PES parsing fail and get a notice

    pid_validator->ts_count++;
    return 1;
}

static void validate_pes_packet_common(pes_packet_t* pes, GQueue* ts_queue, dash_validator_t* dash_validator)
{
    if (dash_validator->segment_type == INITIALIZATION_SEGMENT) {
        for (GList* current = ts_queue->head; current; current = current->next) {
            ts_packet_t* tsp = current->data;
            if (tsp->adaptation_field.pcr_flag) {
                g_critical("DASH Conformance: TS packet in initialization segment has pcr_flag = 1. 6.4.3.2 says, "
                        "\"PCR-bearing packets shall not be present in the Initialization Segment;\".");
                dash_validator->status = 0;
            }
        }
    }

    if (pes == NULL) {
        return;
    }

    if (dash_validator->segment_type == INITIALIZATION_SEGMENT && pes->header.pts_dts_flags != 0) {
        g_critical("DASH Conformance: PES packet in initialization segment has PTS_DTS_flags set to 0x%x. "
                "6.4.3.2 says, \"Time-varying initialization information shall not be present in the Initialization "
                "Segment.\"", pes->header.pts_dts_flags);
        dash_validator->status = 0;
    }
}

int validate_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GQueue* ts_queue, void* arg)
{
    dash_validator_t* dash_validator = (dash_validator_t*)arg;
    pid_validator_t* pid_validator = NULL;
    validate_pes_packet_common(pes, ts_queue, dash_validator);

    if (pes == NULL) {
        // we have a queue that didn't appear to be a valid PES packet (e.g., because it didn't start with
        // payload_unit_start_indicator = 1)
        // TODO: Where did this requirement come from?
        if (dash_validator->profile >= DASH_PROFILE_MPEG2TS_MAIN) {
            g_critical("DASH Conformance: media segments shall contain only complete PES packets");
            dash_validator->status = 0;
        }
        goto cleanup;
    }

    ts_packet_t* first_ts = g_queue_peek_head(ts_queue);
    pid_validator = dash_validator_find_pid(first_ts->header.pid, dash_validator);
    assert(pid_validator != NULL);

    if (pes->status > 0) {
        if (dash_validator->profile >= DASH_PROFILE_MPEG2TS_MAIN) {
            /* TODO: This is a 'should'. Is there somewhere else that upgrades it to 'shall'? */
            /* TODO: 7.4.3.2 says that this is a 'shall' if we're dealing with @segmentAlignment = true  and 7.4.3.2
             *       says the same for @subsegmentAlignment. */
            g_critical("DASH Conformance: 6.4.4.2 Media Segments should contain only complete PES packets and sections.");
            dash_validator->status = 0;
        }
        goto cleanup;
    }

    // we are in the first PES packet of a PID
    if (pid_validator->pes_count == 0) {
        if (pes->header.pts_dts_flags & PES_PTS_FLAG) {
            pid_validator->earliest_playout_time = pes->header.pts;
            pid_validator->latest_playout_time = pes->header.pts;
        } else if (dash_validator->profile >= DASH_PROFILE_MPEG2TS_MAIN) {
            /* TODO: This is probably only if @segmentAlignment is not 'false'
             * 7.4.3.2 Segment alignment
             * If the @segmentAlignment attribute is not set to ‘false’, [...] the first PES packet shall contain a
             * PTS timestamp. */
            g_critical("DASH Conformance: first PES packet must have PTS");
            dash_validator->status = 0;
        }

        if (first_ts->adaptation_field.random_access_indicator) {
            pid_validator->sap = 1; // we trust AF by default.
            if (pid_validator->content_component == VIDEO_CONTENT_COMPONENT) {
                int nal_start, nal_end;
                int returnCode;
                uint8_t* buf = pes->payload;
                size_t len = pes->payload_len;

                // walk the nal units in the PES payload and check to see if they are type 1 or type 5 -- these determine
                // SAP type
                size_t index = 0;
                while (len > index
                        && (returnCode = find_nal_unit(buf + index, len - index, &nal_start, &nal_end)) !=  0) {
                    h264_stream_t* h = h264_new();
                    read_nal_unit(h, &buf[nal_start + index], nal_end - nal_start);
                    int unit_type = h->nal->nal_unit_type;
                    h264_free(h);
                    if (unit_type == 5) {
                        pid_validator->sap_type = 1;
                        break;
                    } else if (unit_type == 1) {
                        pid_validator->sap_type = 2;
                        break;
                    }

                    index += nal_end;
                }
            }
        }
        // TODO: validate in case of ISO/IEC 14496-10 (?)
    }

    if (pes->header.pts_dts_flags & PES_PTS_FLAG) {
        // TODO: account for rollovers and discontinuities
        // frames can come in out of PTS order
        pid_validator->earliest_playout_time = MIN(pid_validator->earliest_playout_time, pes->header.pts);
        pid_validator->latest_playout_time = MAX(pid_validator->latest_playout_time, pes->header.pts);
        if (dash_validator->segment_type == INITIALIZATION_SEGMENT) {
            g_critical("DASH Conformance: 'PES packet in initialization segment has PTS_DTS_flags set to 0x%x. "
                    "Initialization segment packets should not contain timing information.",
                    pes->header.pts_dts_flags);
            dash_validator->status = 0;
        }
    }

    if (pid_validator->content_component == VIDEO_CONTENT_COMPONENT) {
        pid_validator->duration = 3000;

        if (dash_validator->do_iframe_validation && first_ts->adaptation_field.random_access_indicator) {
            // check iFrame location against index file
            if (dash_validator->iframe_index >= dash_validator->iframes->len) {
                g_critical("DASH Conformance: Stream has more IFrames than index file");
                dash_validator->status = 0;
            } else {
                iframe_t* iframe = &g_array_index(dash_validator->iframes, iframe_t, dash_validator->iframe_index);
                ++dash_validator->iframe_index;

                // time location
                if (iframe->location_time != pes->header.pts) {
                    g_critical("DASH Conformance: expected IFrame PTS does not match actual.  Expected: %"PRIu64", "
                            "Actual: %"PRIu64, iframe->location_time, pes->header.pts);
                    dash_validator->status = 0;
                }

                // byte location
                if (iframe->location_byte != pes->payload_pos_in_stream) {
                    g_critical("DASH Conformance: expected IFrame Byte Location does not match actual.  Expected: "
                            "%"PRIu64", Actual: %"PRIu64"", iframe->location_byte, pes->payload_pos_in_stream);
                    dash_validator->status = 0;
                }

                // SAP type
                if (iframe->starts_with_sap && iframe->sap_type != 0 && iframe->sap_type != pid_validator->sap_type) {
                    g_critical("DASH Conformance: expected IFrame SAP Type does not match actual: expected SAP_type "
                            "= %d, actual SAP_type = %d", iframe->sap_type, pid_validator->sap_type);
                    dash_validator->status = 0;
                }
            }
        }
    }

    if (pid_validator->content_component == AUDIO_CONTENT_COMPONENT) {
        uint64_t index = 0;
        int frame_counter = 0;
        while (index < pes->payload_len) {
            uint64_t frame_length = ((pes->payload[index + 3] & 0x0003) << 11) +
                                        ((pes->payload[index + 4]) << 3) + ((pes->payload[index + 5] & 0x00E0) >> 5);
            if (frame_length == 0) {
                g_critical("Error: Detected 0-length frame");
                goto fail;
            }
            index += frame_length;
            frame_counter++;
        }

        pid_validator->duration = 1920 /* 21.3 msec for 90kHz clock */ * frame_counter;
    }

cleanup:
    if (pid_validator) {
        pid_validator->pes_count++;
    }
    pes_free(pes);
    return 1;
fail:
    dash_validator->status = 0;
    goto cleanup;
}

static int validate_emsg_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GQueue* ts_queue, void* arg)
{
    dash_validator_t* dash_validator = (dash_validator_t*)arg;
    validate_pes_packet_common(pes, ts_queue, dash_validator);

    ts_packet_t* first_ts = g_queue_peek_head(ts_queue);
    if (!first_ts->header.payload_unit_start_indicator) {
        g_critical("DASH Conformance: First 'emsg' packet (PID = 0x0004) does not have "
                "payload_unit_start_indicator = 1. 5.10.3.3.5 says, \"the transport stream packet carrying the start "
                "of the `emsg` box shall have the payload_unit_start_indicator field set to `1`\".");
        dash_validator->status = 0;
    }

    if (first_ts->payload.len < 8) {
        /* Note: The first 8 bytes of a Box are the "size" and "type" fields, so if the payload size is >= 8 bytes,
         *       we can guarantee that the Box.type is in it. We check that the type (and size) are correct in
         *       validate_emsg_msg(). */
        g_critical("DASH Conformance: The first TS packet with 'emsg' data has payload size of %zu bytes, but should "
                "be at least 8 bytes. 5.10.3.3.5 says, \"The complete Box.type field shall be present in this first "
                "packet, and the payload size shall be at least 8 bytes.\".", first_ts->payload.len);
        dash_validator->status = 0;
    }

    for (GList* current = ts_queue->head; current; current = current->next) {
        ts_packet_t* tsp = current->data;
        if (tsp->header.transport_scrambling_control != 0) {
            g_critical("DASH Conformance: EMSG packet transport_scrambling_control was 0x%x but should be 0. From "
                    "\"5.10.3.3.5 Carriage of the Event Message Box in MPEG-2 TS\": \"For any packet with PID value "
                    "of 0x0004 the value of the transport_scrambling_control field shall be set to '00'\".",
                    first_ts->header.transport_scrambling_control);
            dash_validator->status = 0;
        }
    }

    if (pes->status > 0) {
        g_critical("DASH Conformance: 5.10.3.3.5 \"A segment shall contain only complete [emsg] boxes. If "
                "@bitstreamSwitching is set, and subsegments are used, a subsegment shall contain only complete "
                "`emsg` boxes.\"");
        dash_validator->status = 0;
    }

    if (pes == NULL) {
        goto cleanup;
    }

    if (validate_emsg_msg(pes->payload, pes->payload_len, dash_validator->segment->duration) != 0) {
        g_critical("DASH Conformance: validation of EMSG failed");
        dash_validator->status = 0;
    }

cleanup:
    pes_free(pes);
    return 1; // return code doesn't matter
}

int validate_segment(dash_validator_t* dash_validator, char* file_name, dash_validator_t* dash_validator_init)
{
    mpeg2ts_stream_t* m2s = NULL;

    FILE* infile = fopen(file_name, "rb");
    if (infile == NULL) {
        g_critical("Cannot open file %s - %s", file_name, strerror(errno));
        goto fail;
    }

    if (dash_validator->segment_start > 0) {
        if (!fseek(infile, dash_validator->segment_start, SEEK_SET)) {
            g_critical("Error seeking to offset %ld - %s", dash_validator->segment_start,
                           strerror(errno));
            goto fail;
        }
    }

    m2s = mpeg2ts_stream_new();

    dash_validator->last_pcr = PCR_INVALID;
    dash_validator->status = 1;
    if (dash_validator->pids->len != 0) {
        g_error("Re-using DASH validator pids!");
        goto fail;
    }

    // if had intialization segment, then copy program info and setup PES callbacks
    if (dash_validator_init != NULL) {
        mpeg2ts_program_t* prog = mpeg2ts_program_new(
                                      200 /* GORP */,  // can I just use dummy values here?
                                      201 /* GORP */);
        prog->pmt = dash_validator_init->initialization_segment_pmt;

        g_debug("Adding initialization PSI info...program = %"PRIXPTR, (uintptr_t)prog);
        g_ptr_array_add(m2s->programs, prog);

        int return_code = copy_pmt_info(prog, dash_validator_init, dash_validator);
        if (return_code != 0) {
            g_critical("Error copying PMT info");
            goto fail;
        }
    }

    m2s->pat_processor = pat_processor;
    m2s->arg = dash_validator;

    // Connect handler for DASH EMSG streams
    pes_demux_t* emsg_pd = pes_demux_new(validate_emsg_pes_packet);
    emsg_pd->pes_arg = dash_validator;
    demux_pid_handler_t* emsg_handler = demux_pid_handler_new(pes_demux_process_ts_packet);
    emsg_handler->arg = emsg_pd;
    emsg_handler->arg_destructor = (arg_destructor_t)pes_demux_free;
    m2s->emsg_processor = emsg_handler;

    long packet_buf_size = 4096;
    uint64_t packets_read = 0;
    long num_packets;
    uint64_t packets_to_read =  UINT64_MAX;
    uint8_t* ts_buf = malloc(TS_SIZE * packet_buf_size);

    if (dash_validator->segment_end > 0) {
        packets_to_read = (dash_validator->segment_end - dash_validator->segment_start) / (uint64_t)TS_SIZE;
    }

    while ((num_packets = fread(ts_buf, TS_SIZE, packet_buf_size, infile)) > 0) {
        for (int i = 0; i < num_packets; i++) {
            ts_packet_t* ts = ts_new();
            int res = ts_read(ts, ts_buf + i * TS_SIZE, TS_SIZE, packets_read);
            if (res < TS_SIZE) {
                g_critical("Error parsing TS packet %"PRIo64" (%d)", packets_read, res);
                goto fail;
            }

            mpeg2ts_stream_read_ts_packet(m2s, ts);
            packets_read++;
        }

        if (packets_to_read - packets_read < packet_buf_size) {
            packet_buf_size = packets_to_read - packets_read;
        }
    }
    free(ts_buf);

    // need to reset the mpeg stream to be sure to process the last PES packet
    mpeg2ts_stream_reset(m2s);

    g_debug("%"PRIo64" TS packets read", packets_read);

    if (dash_validator->segment_type == INITIALIZATION_SEGMENT) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, 0);  // should be only one program
        g_debug("m2p = %"PRIxPTR"\n", (uintptr_t)m2p);
        dash_validator->initialization_segment_pmt = m2p->pmt;
    }

cleanup:
    mpeg2ts_stream_free(m2s);
    fclose(infile);
    return tslib_errno;
fail:
    dash_validator->status = 0;
    tslib_errno = 1;
    goto cleanup;
}

static void validate_representation_index_segment_boxes(index_segment_validator_t* validator, box_t** boxes,
        size_t num_boxes, representation_t* representation, adaptation_set_t* adaptation_set)
{
    /*
    A Representation Index Segment indexes all Media Segments of one Representation and is defined as follows:

    -- Each Representation Index Segment shall begin with an "styp" box, and the brand "risx" shall be
    present in the "styp" box. The conformance requirement of the brand "risx" is defined by this subclause.

    -- Each Media Segment is indexed by one or more Segment Index box(es); the boxes for a
    given Media Segment are contiguous;

    -- Each Segment Index box may be followed by an 'ssix' and/or 'pcrb' box;

    -- The Segment Index for each Media Segments is concatenated in order, preceded by a
    single Segment Index box that indexes the Index Segment. This initial Segment Index box shall
    have one entry in its loop for each Media Segment, and each entry refers to the Segment
    Index information for a single Media Segment.
    */

    int return_code = 0;

    int box_index = 0;
    if (num_boxes == 0) {
        g_critical("ERROR validating Representation Index Segment: no boxes in segment.");
        goto fail;
    }

    // first box must be a styp
    if (boxes[box_index]->type != BOX_TYPE_STYP) {
        g_critical("ERROR validating Representation Index Segment: first box not a styp.");
        goto fail;
    }

    // check brand
    data_styp_t* styp = (data_styp_t*)boxes[box_index];
    bool found_risx = false;
    bool found_ssss = false;
    for(size_t i = 0; i < styp->num_compatible_brands; ++i) {
        uint32_t brand = styp->compatible_brands[i];
        if (brand == BRAND_RISX) {
            found_risx = true;
        } else if (brand == BRAND_SSSS) {
            found_ssss = true;
        }
        if (found_risx && found_ssss) {
            break;
        }
    }
    if (!found_risx) {
        g_critical("ERROR validating Representation Index Segment: styp compatible brands does not contain \"risx\".");
        g_info("Brands found are:");
        g_info("styp major brand = %x", styp->major_brand);
        for (size_t i = 0; i < styp->num_compatible_brands; ++i) {
            g_info("styp compatible brand = %x", styp->compatible_brands[i]);
        }
        goto fail;
    }

    box_index++;

    // second box must be a sidx that references other sidx boxes
    if (boxes[box_index]->type != BOX_TYPE_SIDX) {
        g_critical("ERROR validating Representation Index Segment: second box not a sidx.");
        goto fail;
    }

    // walk all references: they should all be of type 1 and should point to sidx boxes
    data_sidx_t* master_sidx = (data_sidx_t*)boxes[box_index];
    unsigned int master_reference_id = master_sidx->reference_id;
    if (master_reference_id != adaptation_set->video_pid) {
        g_critical("ERROR validating Representation Index Segment: master ref ID does not equal \
video PID.  Expected %d, actual %d.", adaptation_set->video_pid, master_reference_id);
        return_code = -1;
    }
    for (size_t i = 0; i < master_sidx->reference_count; i++) {
        data_sidx_reference_t ref = master_sidx->references[i];
        if (ref.reference_type != 1) {
            g_critical("ERROR validating Representation Index Segment: reference type not 1.");
            goto fail;
        }

        // validate duration
        if (i < representation->segments->len) {
            segment_t* segment = g_ptr_array_index(representation->segments, i);
            if (segment->duration != ref.subsegment_duration) {
                g_critical("ERROR validating Representation Index Segment: master ref segment duration does not equal "
                        "segment duration.  Expected %"PRIu64", actual %d.", segment->duration, ref.subsegment_duration);
                return_code = -1;
            }
        }
    }
    box_index++;

    size_t segment_index = 0;
    bool ssix_present = false;
    bool pcrb_present = false;
    int num_nested_sidx = 0;
    int num_iframes = 0;
    uint64_t referenced_size = 0;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    for (; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        switch(box->type) {
        case BOX_TYPE_SIDX: {
            ssix_present = false;
            pcrb_present = false;

            data_sidx_t* sidx = (data_sidx_t*)box;
            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                // GORP: check earliest presentation time
            } else {
                // check size:
                g_info("Validating referenced_size for segment %zu.", segment_index);
                if (segment_index > 1 && referenced_size != master_sidx->references[segment_index - 1].referenced_size) {
                    g_critical("ERROR validating Representation Index Segment: referenced_size for segment %zu. "
                            "Expected %"PRIu32", actual %"PRIu64"\n", segment_index,
                            master_sidx->references[segment_index - 1].referenced_size, referenced_size);
                    return_code = -1;
                }

                referenced_size = 0;
                segment_t* segment = g_ptr_array_index(representation->segments, segment_index);
                segment_index++;

                g_info("Validating earliest_presentation_time for segment %zu.", segment_index);
                if (segment->start != sidx->earliest_presentation_time) {
                    g_critical("ERROR validating Representation Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64".", segment->start, sidx->earliest_presentation_time);
                    return_code = -1;
                }
            }
            referenced_size += sidx->size;

            g_info("Validating reference_id");
            if (master_reference_id != sidx->reference_id) {
                g_critical("ERROR validating Representation Index Segment: invalid reference id in sidx box. \
Expected %d, actual %d.", master_reference_id, sidx->reference_id);
                return_code = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if (analyze_sidx_references(sidx, &num_iframes, &num_nested_sidx, representation->profile) != 0) {
                return_code = -1;
            }
            break;
        }
        case BOX_TYPE_SSIX: {
            data_ssix_t* ssix = (data_ssix_t*)box;
            referenced_size += ssix->size;
            g_info("Validating ssix box");
            if (ssix_present) {
                g_critical("ERROR validating Representation Index Segment: More than one ssix box following sidx box.");
                return_code = -1;
            } else {
                ssix_present = true;
            }
            if (pcrb_present) {
                g_critical("ERROR validating Representation Index Segment: pcrb occurred before ssix. 6.4.6.4 says "
                        "\"The Subsegment Index box (‘ssix’) [...] shall follow immediately after the ‘sidx’ box that "
                        "documents the same Subsegment. [...] If the 'pcrb' box is present, it shall follow 'ssix'.\".");
                return_code = -1;
            }
            if (!found_ssss) {
                g_critical("ERROR validating Representation Index Segment: Saw ssix box, but 'ssss' is not in compatible brands. See 6.4.6.4.");
                return_code = -1;
            }
            break;
        }
        case BOX_TYPE_PCRB: {
            data_pcrb_t* pcrb = (data_pcrb_t*)box;
            referenced_size += pcrb->size;
            g_info("Validating pcrb box");
            if (pcrb_present) {
                g_critical("ERROR validating Representation Index Segment: More than one pcrb box following sidx box.");
                return_code = -1;
            } else {
                pcrb_present = true;
            }
            break;
        }
        default:
            g_critical("Invalid box type: %x.", box->type);
            break;
        }
    }

    // check the last reference size -- the last one is not checked in the above loop
    if (segment_index > 0 && referenced_size != master_sidx->references[segment_index - 1].referenced_size) {
        g_critical("ERROR validating Representation Index Segment: referenced_size for reference %zu. Expected "
                "%"PRIu32", actual %"PRIu64".", segment_index,
                master_sidx->references[segment_index - 1].referenced_size, referenced_size);
        return_code = -1;
    }

    if (num_nested_sidx != 0) {
        g_critical("ERROR validating Representation Index Segment: Incorrect number of nested sidx boxes: %d.",
                num_nested_sidx);
        return_code = -1;
    }

    if (segment_index != representation->segments->len) {
        g_critical("ERROR validating Representation Index Segment: Invalid number of segment sidx boxes following "
                "master sidx box: expected %u, found %zu.", representation->segments->len, segment_index);
        return_code = -1;
    }

    // fill in iFrame locations by walking the list of sidx's again, starting from the third box
    num_nested_sidx = 0;
    segment_index = 0;
    size_t iframe_counter = 0;
    uint64_t next_iframe_byte_location = 0;
    uint64_t last_iframe_start_time = representation->presentation_time_offset;
    uint64_t last_iframe_duration = 0;
    for (box_index = 2; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        if (box->type == BOX_TYPE_SIDX) {
            data_sidx_t* sidx = (data_sidx_t*)box;

            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                next_iframe_byte_location += sidx->first_offset;  // convert from 64-bit t0 32 bit
            } else {
                segment_t* segment = g_ptr_array_index(representation->segments, segment_index);
                last_iframe_start_time = segment->start;
                last_iframe_duration = segment->duration;
                segment_index++;

                iframe_counter = 0;
                next_iframe_byte_location = sidx->first_offset;
            }

            // fill in Iframe locations here
            for (size_t i = 0; i < sidx->reference_count; i++) {
                data_sidx_reference_t ref = sidx->references[i];
                if (ref.reference_type == 0) {
                    iframe_t iframe;
                    iframe.starts_with_sap = ref.starts_with_sap;
                    iframe.sap_type = ref.sap_type;
                    iframe.location_byte = next_iframe_byte_location;
                    iframe.location_time = last_iframe_start_time + last_iframe_duration + ref.sap_delta_time;

                    last_iframe_start_time = iframe.location_time;
                    last_iframe_duration = ref.subsegment_duration;
                    next_iframe_byte_location += ref.referenced_size;
                    iframe_counter++;
                } else {
                    num_nested_sidx++;
                }
            }
        }
    }

cleanup:
    validator->error = return_code != 0;
    return;
fail:
    return_code = -1;
    goto cleanup;
}

static void validate_single_index_segment_boxes(index_segment_validator_t* validator, box_t** boxes, size_t num_boxes,
        segment_t* segment, representation_t* representation, adaptation_set_t* adaptation_set)
{
    /*
     A Single Index Segment indexes exactly one Media Segment and is defined as follows:

     -- Each Single Index Segment shall begin with a "styp" box, and the brand "sisx"
     shall be present in the "styp" box. The conformance requirement of the brand "sisx" is defined in this subclause.

     -- Each Single Index Segment shall contain one or more 'sidx' boxes which index one Media Segment.

     -- A Single Index Segment may contain one or multiple "ssix" boxes. If present, the "ssix"
     shall follow the "sidx" box that documents the same Subsegment without any other "sidx" preceding the "ssix".

     -- A Single Index Segment may contain one or multiple "pcrb" boxes as defined in 6.4.7.2.
     If present, "pcrb" shall follow the "sidx" box that documents the same Subsegments, i.e. a "pcrb"
     box provides PCR information for every subsegment indexed in the last "sidx" box.
    */
    int return_code = 0;

    int box_index = 0;
    if (num_boxes == 0) {
        g_critical("ERROR validating Single Index Segment: no boxes in segment.");
        return_code = -1;
    }

    // first box must be a styp
    if (boxes[box_index]->type != BOX_TYPE_STYP) {
        g_critical("ERROR validating Single Index Segment: first box not a styp.");
        return_code = -1;
    }

    // check brand
    data_styp_t* styp = (data_styp_t*)boxes[box_index];
    if (styp->major_brand != BRAND_SISX) {
        g_info("styp brand = %x", styp->major_brand);
        g_critical("ERROR validating Single Index Segment: styp brand not risx.");
        return_code = -1;
    }

    box_index++;

    int ssix_present = 0;
    int pcrb_present = 0;
    int num_nested_sidx = 0;
    unsigned int referenced_size = 0;
    int num_iframes = 0;

    uint64_t segment_start_time = representation->presentation_time_offset;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    for (; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        switch (box->type) {
        case BOX_TYPE_SIDX: {
            ssix_present = 0;
            pcrb_present = 0;

            data_sidx_t* sidx = (data_sidx_t*)box;
            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                // GORP: check earliest presentation time
            } else {
                referenced_size = 0;

                g_info("Validating earliest_presentation_time");
                if (segment_start_time != sidx->earliest_presentation_time) {
                    g_critical("ERROR validating Single Index Segment: invalid earliest_presentation_time in sidx "
                            "box. Expected %"PRId64", actual %"PRId64".", segment_start_time, sidx->earliest_presentation_time);
                    return_code = -1;
                }
            }
            referenced_size += sidx->size;

            g_info("Validating reference_id");
            if (adaptation_set->video_pid != sidx->reference_id) {
                g_critical("ERROR validating Single Index Segment: invalid reference id in sidx box. Expected %d, "
                        "actual %d.", adaptation_set->video_pid, sidx->reference_id);
                return_code = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if (analyze_sidx_references(sidx, &num_iframes, &num_nested_sidx, representation->profile) != 0) {
                return_code = -1;
            }
            break;
        }
        case BOX_TYPE_SSIX: {
            data_ssix_t* ssix = (data_ssix_t*)box;
            referenced_size += ssix->size;
            g_info("Validating ssix box");
            if (ssix_present) {
                g_critical("ERROR validating Single Index Segment: More than one ssix box following sidx box.");
                return_code = -1;
            } else {
                ssix_present = 1;
            }
            break;
        }
        case BOX_TYPE_PCRB: {
            data_pcrb_t* pcrb = (data_pcrb_t*)box;
            referenced_size += pcrb->size;
            g_info("Validating pcrb box");
            if (pcrb_present) {
                g_critical("ERROR validating Single Index Segment: More than one pcrb box following sidx box.");
                return_code = -1;
            } else {
                pcrb_present = 1;
            }
            break;
        }
        default:
            g_debug("Ignoring box: %x", box->type);
            break;
        }
    }

    if (num_nested_sidx != 0) {
        g_critical("ERROR validating Single Index Segment: Incorrect number of nested sidx boxes: %d.",
                       num_nested_sidx);
        return_code = -1;
    }

    // fill in iFrame locations by walking the list of sidx's again, startng from box 1

    num_nested_sidx = 0;
    uint64_t last_iframe_start_time = representation->presentation_time_offset;
    uint64_t last_iframe_duration = 0;
    uint64_t next_iframe_byte_location = 0;
    GArray* iframes = g_array_new(false, false, sizeof(iframe_t));
    for (box_index = 1; box_index < num_boxes; ++box_index) {
        if (boxes[box_index]->type != BOX_TYPE_SIDX) {
            continue;
        }
        data_sidx_t* sidx = (data_sidx_t*)boxes[box_index];

        if (num_nested_sidx > 0) {
            num_nested_sidx--;
            next_iframe_byte_location += sidx->first_offset;  // convert from 64-bit t0 32 bit
        }

        // fill in Iframe locations here
        for (size_t i = 0; i < sidx->reference_count; i++) {
            data_sidx_reference_t ref = sidx->references[i];
            if (ref.reference_type == 0) {
                iframe_t iframe;
                iframe.starts_with_sap = ref.starts_with_sap;
                iframe.sap_type = ref.sap_type;
                iframe.location_byte = next_iframe_byte_location;
                iframe.location_time = last_iframe_start_time + last_iframe_duration + ref.sap_delta_time;
                g_array_append_val(iframes, iframe);

                last_iframe_start_time = iframe.location_time;
                last_iframe_duration = ref.subsegment_duration;
                next_iframe_byte_location += ref.referenced_size;
            } else {
                num_nested_sidx++;
            }
        }
    }
    g_ptr_array_add(validator->segment_iframes, iframes);
    validator->error = return_code != 0;
}

int analyze_sidx_references(data_sidx_t* sidx, int* pnum_iframes, int* pnum_nested_sidx, dash_profile_t profile)
{
    int originalnum_nested_sidx = *pnum_nested_sidx;
    int originalnum_iframes = *pnum_iframes;

    for(int i = 0; i < sidx->reference_count; i++) {
        data_sidx_reference_t ref = sidx->references[i];
        if (ref.reference_type == 1) {
            (*pnum_nested_sidx)++;
        } else {
            (*pnum_iframes)++;
        }
    }

    if (profile >= DASH_PROFILE_MPEG2TS_SIMPLE) {
        if (originalnum_nested_sidx != *pnum_nested_sidx && originalnum_iframes != *pnum_iframes) {
            // failure -- references contain references to both media and nested sidx boxes
            g_critical("ERROR validating Representation Index Segment: Section 8.7.3: Simple profile requires that \
sidx boxes have either media references or sidx references, but not both.");
            return -1;
        }
    }

    return 0;
}

index_segment_validator_t* validate_index_segment(char* file_name, segment_t* segment, representation_t* representation,
        adaptation_set_t* adaptation_set)
{
    size_t num_boxes = 0;
    box_t** boxes = NULL;
    index_segment_validator_t* validator = index_segment_validator_new();

    if (representation->segments->len == 0) {
        g_critical("ERROR validating Index Segment: No segments in representation.");
        goto fail;
    }

    int return_code = read_boxes_from_file(file_name, &boxes, &num_boxes);
    if (return_code != 0) {
        g_critical("ERROR validating Index Segment: Error reading boxes from file.");
        goto fail;
    }
    print_boxes(boxes, num_boxes);

    if (segment) {
        validate_single_index_segment_boxes(validator, boxes, num_boxes, segment, representation, adaptation_set);
    } else {
        validate_representation_index_segment_boxes(validator, boxes, num_boxes, representation, adaptation_set);
    }

cleanup:
    free_boxes(boxes, num_boxes);
    return validator;
fail:
    validator->error = true;
    goto cleanup;
}

int validate_emsg_msg(uint8_t* buffer, size_t len, unsigned segment_duration)
{
    box_t** boxes = NULL;
    size_t num_boxes;

    GDataInputStream* input = g_data_input_stream_new(g_memory_input_stream_new_from_data(buffer, len, NULL));
    int return_code = read_boxes_from_stream(input, &boxes, &num_boxes);
    if (return_code != 0) {
        goto fail;
    }

    print_boxes(boxes, num_boxes);

    for (size_t i = 0; i < num_boxes; i++) {
        data_emsg_t* box = (data_emsg_t*)boxes[i];
        if (box->type != BOX_TYPE_EMSG) {
            char tmp[5] = {0};
            uint32_to_string(tmp, box->type);
            g_critical("DASH Conformance: Saw a box with type %s in a PES packet for PID 0x0004, which is reserved "
                    "for 'emsg' boxes. 5.10.3.3.5: \"[...] the packet payload will start with the `emsg` box [...].\"",
                    tmp);
            return_code = -1;
        }

        // GORP: anything else to verify here??

        if (box->presentation_time_delta + box->event_duration > segment_duration) {
            g_critical("ERROR validating EMSG: event lasts longer tha segment duration.");
            goto fail;
        }
    }

cleanup:
    g_object_unref(input);
    free_boxes(boxes, num_boxes);
    return return_code;
fail:
    return_code = -1;
    goto cleanup;
}