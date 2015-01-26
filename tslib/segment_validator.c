
#include "segment_validator.h"

#include <inttypes.h>
#include <errno.h>
#include "mpeg2ts_demux.h"
#include "h264_stream.h"

static dash_validator_t* global_dash_validator;
int global_iframe_counter;
data_segment_iframes_t* global_iframe_data;
unsigned int global_segment_duration;

int pat_processor(mpeg2ts_stream_t* m2s, void* arg);
int pmt_processor(mpeg2ts_program_t* m2p, void* arg);
int validate_ts_packet(ts_packet_t* ts, elementary_stream_info_t* es_info, void* arg);
int validate_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GQueue* ts_queue,
        void* arg);
int validate_representation_index_segment_boxes(size_t num_segments, box_t** boxes, size_t num_boxes,
        uint64_t* segment_durations, data_segment_iframes_t* iframes, int presentation_time_offset,
        int video_pid, bool is_simple_profile);
int validate_single_index_segment_boxes(box_t** boxes, size_t num_boxes,
        uint64_t segment_duration, data_segment_iframes_t* iframes,
        int presentation_time_offset, int video_pid, bool is_simple_profile);

int validate_emsg_msg(uint8_t* buffer, size_t len, unsigned segment_duration);
int analyze_sidx_references(data_sidx_t*, int* num_iframes, int* num_nested_sidx,
        bool is_simple_profile);

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

pid_validator_t* pid_validator_new(int pid, int content_component)
{
    pid_validator_t* obj = calloc(1, sizeof(*obj));
    obj->pid = pid;
    obj->content_component = content_component;
    obj->ecm_pids = g_ptr_array_new();
    return obj;
}

void pid_validator_free(pid_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }
    g_ptr_array_free(obj->ecm_pids, true);
    free(obj);
}

dash_validator_t* dash_validator_new(segment_type_t segment_type, uint32_t conformance_level)
{
    dash_validator_t* obj = calloc(1, sizeof(*obj));
    dash_validator_init(obj, segment_type, conformance_level);
    return obj;
}

void dash_validator_init(dash_validator_t* obj, segment_type_t segment_type, uint32_t conformance_level)
{
    obj->pids = g_ptr_array_new_with_free_func((GDestroyNotify)pid_validator_free);
    obj->conformance_level = conformance_level;
}

void dash_validator_destroy(dash_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }
    g_ptr_array_free(obj->pids, true);
}

void dash_validator_free(dash_validator_t* obj)
{
    dash_validator_destroy(obj);
    free(obj);
}

int pat_processor(mpeg2ts_stream_t* m2s, void* arg)
{
    dash_validator_t* dash_validator = (dash_validator_t*)arg;
    if (dash_validator->conformance_level & TS_TEST_DASH && m2s->programs->len != 1) {
        g_critical("DASH Conformance: 6.4.4.2  media segments shall contain exactly one program (%u found)",
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

    dash_validator->psi_tables_seen |= 1 << m2s->pat->table_id;
    return 1;
}

int cat_processor(mpeg2ts_stream_t* m2s, void* arg)
{
    conditional_access_section_print(m2s->cat);

    // TODO: Register an EMM processor here

    return 1;
}

pid_validator_t* dash_validator_find_pid(int pid, dash_validator_t* dash_validator)
{
    for (gsize i = 0; i < dash_validator->pids->len; ++i) {
        pid_validator_t* pv = g_ptr_array_index(dash_validator->pids, i);
        if (pv->pid == pid) {
            return pv;
        }
    }
    return NULL;
}

// TODO: fix tpes to try creating last PES packet
int pmt_processor(mpeg2ts_program_t* m2p, void* arg)
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
    dash_validator->psi_tables_seen |= 1 << m2p->pmt->table_id;
    dash_validator->pmt_program_number = m2p->pmt->program_number;
    dash_validator->pmt_version_number = m2p->pmt->version_number;

    GHashTableIter i;
    g_hash_table_iter_init(&i, m2p->pids);
    pid_info_t* pi;
    while (g_hash_table_iter_next(&i, NULL, (void**)&pi)) {
        int process_pid = 0;
        int content_component = 0;
        int pid = pi->es_info->elementary_pid;

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

// TODO: fix tpes to try creating last PES packet
int copy_pmt_info(mpeg2ts_program_t* m2p, dash_validator_t* dash_validator_source,
        dash_validator_t* dash_validator_dest)
{
    pid_validator_t* pid_validator_dest = NULL;

    dash_validator_dest->pcr_pid = dash_validator_source->pcr_pid;
    dash_validator_dest->psi_tables_seen = 0;

    g_debug("copy_pmt_info: dash_validator_source->pids->len = %u",
            dash_validator_source->pids->len);
    for (gsize i = 0; i < dash_validator_source->pids->len; ++i) {
        pid_validator_t* pid_validator_src = g_ptr_array_index(dash_validator_source->pids, i);
        int content_component = 0;
        int pid = pid_validator_src->pid;

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

        pid_validator_dest = pid_validator_new(pid, content_component);

        g_debug("copy_pmt_info: adding pid_validator %"PRIxPTR" for PID %d", (uintptr_t)pid_validator_dest,
                      pid);
        g_ptr_array_add(dash_validator_dest->pids, pid_validator_dest);
        // TODO: parse CA descriptors, add ca system and ecm_pid if they don't exist yet
    }

    return 0;
}

int validate_ts_packet(ts_packet_t* ts, elementary_stream_info_t* es_info, void* arg)
{
    dash_validator_t* dash_validator = (dash_validator_t*)arg;
    if (ts == NULL || es_info == NULL) {
        return 0;
    }

    pid_validator_t* pid_validator = dash_validator_find_pid(ts->header.pid, dash_validator);
    assert(pid_validator != NULL);

    if (dash_validator->pcr_pid == ts->header.pid) {
        int64_t pcr = ts_read_pcr(ts);
        if (PCR_IS_VALID(pcr)) {
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
            if ((dash_validator->conformance_level & TS_TEST_MAIN)
                    && (ts->header.payload_unit_start_indicator == 0)) {
                g_critical("DASH Conformance: media segments shall contain only complete PES packets");
                dash_validator->status = 0;
            }

            // by the time we get to the start of the first PES, we need to have seen at least one PCR.
            if (dash_validator->conformance_level & TS_TEST_SIMPLE) {
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

int validate_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GQueue* ts_queue, void* arg)
{
    dash_validator_t* dash_validator = (dash_validator_t*)arg;
    if (esi == NULL) {
        return 0;
    }

    if (pes == NULL) {
        // we have a queue that didn't appear to be a valid TS packet (e.g., because it didn't start from PUSI=1)
        if (dash_validator->conformance_level & TS_TEST_MAIN) {
            g_critical("DASH Conformance: media segments shall contain only complete PES packets");
            dash_validator->status = 0;
            return 0;
        }

        g_critical("NULL PES packet!");
        dash_validator->status = 0;
        return 0;
    }

    if (dash_validator->segment_type == INITIALIZATION_SEGMENT) {
        g_critical("DASH Conformance: initialization segment cannot contain program stream");
        dash_validator->status = 0;
        return 0;
    }

    ts_packet_t* first_ts = g_queue_peek_head(ts_queue);
    pid_validator_t* pid_validator = dash_validator_find_pid(first_ts->header.pid, dash_validator);

    if (first_ts->header.pid == PID_EMSG) {
        g_print("EMSG\n");
        if (first_ts->header.transport_scrambling_control != 0) {
            g_critical("DASH Conformance: EMSG packet transport_scrambling_control was 0x%x but should be 0. From "
                    "\"5.10.3.3.5 Carriage of the Event Message Box in MPEG-2 TS\": \"For any packet with PID value "
                    "of 0x0004 the value of the transport_scrambling_control field shall be set to '00'\".",
                    first_ts->header.transport_scrambling_control);
            dash_validator->status = 0;
        }
        uint8_t* buf = pes->payload;
        int len = pes->payload_len;
        if (validate_emsg_msg(buf, len, global_segment_duration) != 0) {
            g_critical("DASH Conformance: validation of EMSG failed");
            dash_validator->status = 0;
        }
    }

    assert(pid_validator != NULL);
    if (pes->status > 0) {
        if (dash_validator->conformance_level & TS_TEST_MAIN) {
            g_critical("DASH Conformance: media segments shall contain only complete PES packets");
            dash_validator->status = 0;
        }
        pes_free(pes);
        return 0;
    }

    // we are in the first PES packet of a PID
    if (pid_validator->pes_count == 0) {
        if (pes->header.pts_dts_flags & PES_PTS_FLAG) {
            pid_validator->earliest_playout_time = pes->header.pts;
            pid_validator->latest_playout_time = pes->header.pts;
        } else {
            if(dash_validator->conformance_level & TS_TEST_MAIN) {
                g_critical("DASH Conformance: first PES packet must have PTS");
                dash_validator->status = 0;
            }
        }

        if (first_ts->adaptation_field.random_access_indicator) {
            pid_validator->sap = 1; // we trust AF by default.
            if (pid_validator->content_component == VIDEO_CONTENT_COMPONENT) {
                int nal_start, nal_end;
                int returnCode;
                uint8_t* buf = pes->payload;
                int len = pes->payload_len;

                // walk the nal units in the PES payload and check to see if they are type 1 or type 5 -- these determine
                // SAP type
                int index = 0;
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
    }

    if (pid_validator->content_component == VIDEO_CONTENT_COMPONENT) {
        pid_validator->duration = 3000;

        if (global_iframe_data->do_iframe_validation && first_ts->adaptation_field.random_access_indicator) {
            g_debug("Performing IFrame validation");
            // check iFrame location against index file

            if (global_iframe_counter < global_iframe_data->num_iframes) {
                unsigned int expectedIFramePTS = global_iframe_data->iframe_locations_time[global_iframe_counter];
                unsigned int actualIFramePTS = pes->header.pts;
                g_debug("expectedIFramePTS = %u, actualIFramePTS = %u", expectedIFramePTS, actualIFramePTS);
                if (expectedIFramePTS != actualIFramePTS) {
                    g_critical("DASH Conformance: expected IFrame PTS does not match actual.  Expected: %d, Actual: %d",
                                   expectedIFramePTS, actualIFramePTS);
                    dash_validator->status = 0;
                }

                // check frame byte location
                uint64_t expectedFrameByteLocation = global_iframe_data->iframe_locations_byte[global_iframe_counter];
                uint64_t actualFrameByteLocation = pes->payload_pos_in_stream;
                g_debug("expectedIFrameByteLocation = %"PRId64", actualIFrameByteLocation = %"PRId64"",
                              expectedFrameByteLocation, actualFrameByteLocation);
                if (expectedFrameByteLocation != actualFrameByteLocation) {
                    g_critical("DASH Conformance: expected IFrame Byte Locaton does not match actual.  Expected: %"PRId64", Actual: %"PRId64"",
                                   expectedFrameByteLocation, actualFrameByteLocation);
                    dash_validator->status = 0;
                }

                // check SAP type
                unsigned char expectedStartsWithSAP = global_iframe_data->starts_with_sap[global_iframe_counter];
                unsigned char expectedSAPType = global_iframe_data->sap_type[global_iframe_counter];
                unsigned char actualSAPType = pid_validator->sap_type;
                g_debug("expectedStartsWithSAP = %d, expectedSAPType = %d, actualSAPType = %d",
                              expectedStartsWithSAP, expectedSAPType, actualSAPType);
                if (expectedStartsWithSAP == 1 && expectedSAPType != 0 && expectedSAPType != actualSAPType) {
                    g_critical("DASH Conformance: expected IFrame SAP Type does not match actual: expectedStartsWithSAP = %d, \
expectedSAPType = %d, actualSAPType = %d", expectedStartsWithSAP, expectedSAPType, actualSAPType);
                    dash_validator->status = 0;
                }

                global_iframe_counter++;
            } else {
                g_critical("DASH Conformance: Stream has more IFrames than index file");
                dash_validator->status = 0;
            }
        }
    }

    if (pid_validator->content_component == AUDIO_CONTENT_COMPONENT) {
        int index = 0;
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
    pid_validator->pes_count++;
    pes_free(pes);
    return 1;
fail:
    dash_validator->status = 0;
    goto cleanup;
}

int validate_segment(dash_validator_t* dash_validator, char* fname,
        dash_validator_t* dash_validator_init,
        data_segment_iframes_t* pIFrameData, uint64_t segmentDuration)
{
    global_dash_validator = dash_validator;
    global_iframe_counter = 0;
    global_iframe_data = pIFrameData;
    global_segment_duration = segmentDuration;

    g_debug("doSegmentValidation : %s", fname);
    mpeg2ts_stream_t* m2s = NULL;

// GORP:    initialization segment shall not contain any media data with an assigned presentation time
// GORP:    initialization segment shall contain PAT and PMT and PCR
// GORP: self-initializing segment shall contain PAT and PMT and PCR
// GORP:     check for complete transport stream packets
// GORP:         check for complete PES packets

    FILE* infile = fopen(fname, "rb");
    if (infile == NULL) {
        g_critical("Cannot open file %s - %s", fname, strerror(errno));
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
    if (m2s == NULL) {
        g_critical("Error creating MPEG-2 STREAM object");
        goto fail;
    }

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

void validate_dash_events(uint8_t* buf, int len)
{
    if (validate_emsg_msg(buf, len, global_segment_duration) != 0) {
        g_critical("DASH Conformance: validation of EMSG failed");
        global_dash_validator->status = 0;
    }
}

int validate_representation_index_segment_boxes(size_t num_segments,
        box_t** boxes, size_t num_boxes, uint64_t* segment_durations,
        data_segment_iframes_t* iframes, int presentation_time_offset,
        int video_pid, bool is_simple_profile)
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
    if (master_reference_id != video_pid) {
        g_critical("ERROR validating Representation Index Segment: master ref ID does not equal \
video PID.  Expected %d, actual %d.", video_pid, master_reference_id);
        return_code = -1;
    }
    for (size_t i = 0; i < master_sidx->reference_count; i++) {
        data_sidx_reference_t ref = master_sidx->references[i];
        if (ref.reference_type != 1) {
            g_critical("ERROR validating Representation Index Segment: reference type not 1.");
            goto fail;
        }

        // validate duration
        if (segment_durations[i] != ref.subsegment_duration) {
            g_critical("ERROR validating Representation Index Segment: master ref segment duration does not equal \
segment duration.  Expected %"PRIu64", actual %d.", segment_durations[i], ref.subsegment_duration);
            return_code = -1;
        }
    }
    box_index++;

    int segment_index = -1;
    bool ssix_present = false;
    bool pcrb_present = false;
    int num_nested_sidx = 0;
    uint64_t referenced_size = 0;
    uint64_t segment_start_time = presentation_time_offset;

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
                g_info("Validating referenced_size for reference %d.", segment_index);
                if (segment_index >= 0 && referenced_size != master_sidx->references[segment_index].referenced_size) {
                    g_critical("ERROR validating Representation Index Segment: referenced_size for reference %d. \
Expected %"PRIu32", actual %"PRIu64"\n", segment_index, master_sidx->references[segment_index].referenced_size,
                                   referenced_size);
                    return_code = -1;
                }

                referenced_size = 0;
                segment_index++;
                if (segment_index > 0) {
                    segment_start_time += segment_durations[segment_index - 1];
                }

                g_info("Validating earliest_presentation_time for reference %d.", segment_index);
                if (segment_start_time != sidx->earliest_presentation_time) {
                    g_critical("ERROR validating Representation Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64".", segment_start_time, sidx->earliest_presentation_time);
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
            if (analyze_sidx_references(sidx, &(iframes[segment_index].num_iframes), &num_nested_sidx,
                                     is_simple_profile) != 0) {
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
    g_info("Validating referenced_size for reference %d. Expected %"PRIu32", actual %"PRIu64".",
           segment_index, master_sidx->references[segment_index].referenced_size, referenced_size);
    if (segment_index >= 0 && referenced_size != master_sidx->references[segment_index].referenced_size) {
        g_critical("ERROR validating Representation Index Segment: referenced_size for reference %d. \
Expected %"PRIu32", actual %"PRIu64".", segment_index, master_sidx->references[segment_index].referenced_size,
                       referenced_size);
        return_code = -1;
    }

    if (num_nested_sidx != 0) {
        g_critical("ERROR validating Representation Index Segment: Incorrect number of nested sidx boxes: %d.",
                num_nested_sidx);
        return_code = -1;
    }

    if ((segment_index + 1) != num_segments) {
        g_critical("ERROR validating Representation Index Segment: Invalid number of segment sidx boxes following master sidx box: \
expected %zu, found %d.", num_segments, segment_index);
        return_code = -1;
    }

    // fill in iFrame locations by walking the list of sidx's again, starting from the third box
    num_nested_sidx = 0;
    segment_index = -1;
    int iframe_counter = 0;
    unsigned int lastIFrameDuration = 0;
    uint64_t nextIFrameByteLocation = 0;
    segment_start_time = presentation_time_offset;
    for (box_index = 2; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        if (box->type == BOX_TYPE_SIDX) {
            data_sidx_t* sidx = (data_sidx_t*)box;

            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                nextIFrameByteLocation += sidx->first_offset;  // convert from 64-bit t0 32 bit
            } else {
                segment_index++;
                if (segment_index > 0) {
                    segment_start_time += segment_durations[segment_index - 1];
                }

                iframe_counter = 0;
                nextIFrameByteLocation = sidx->first_offset;
                if (segment_index < num_segments) {
                    iframes[segment_index].do_iframe_validation = 1;
                    iframes[segment_index].iframe_locations_time = calloc(
                                iframes[segment_index].num_iframes, sizeof(unsigned int));
                    iframes[segment_index].iframe_locations_byte = calloc(iframes[segment_index].num_iframes,
                            sizeof(uint64_t));
                    iframes[segment_index].starts_with_sap = calloc(iframes[segment_index].num_iframes,
                                                            sizeof(unsigned char));
                    iframes[segment_index].sap_type = calloc(iframes[segment_index].num_iframes,
                                                      sizeof(unsigned char));
                }
            }

            // fill in Iframe locations here
            for (int i = 0; i < sidx->reference_count; i++) {
                data_sidx_reference_t ref = sidx->references[i];
                if (ref.reference_type == 0) {
                    (iframes[segment_index]).starts_with_sap[iframe_counter] = ref.starts_with_sap;
                    (iframes[segment_index]).sap_type[iframe_counter] = ref.sap_type;
                    (iframes[segment_index]).iframe_locations_byte[iframe_counter] = nextIFrameByteLocation;


                    if (iframe_counter == 0) {
                        (iframes[segment_index]).iframe_locations_time[iframe_counter] = segment_start_time + ref.sap_delta_time;
                    } else {
                        (iframes[segment_index]).iframe_locations_time[iframe_counter] =
                            (iframes[segment_index]).iframe_locations_time[iframe_counter - 1] + lastIFrameDuration +
                            ref.sap_delta_time;
                    }
                    iframe_counter++;
                    lastIFrameDuration = ref.subsegment_duration;
                    nextIFrameByteLocation += ref.referenced_size;
                } else {
                    num_nested_sidx++;
                }
            }
        }
    }

cleanup:
    return return_code;
fail:
    return_code = -1;
    goto cleanup;
}

int validate_single_index_segment_boxes(box_t** boxes, size_t num_boxes,
        uint64_t segment_duration, data_segment_iframes_t* iframes,
        int presentation_time_offset, int video_pid, bool is_simple_profile)
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

    uint64_t segment_start_time = presentation_time_offset;

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
                    g_critical("ERROR validating Single Index Segment: invalid earliest_presentation_time in sidx box. \
Expected %"PRId64", actual %"PRId64".", segment_start_time, sidx->earliest_presentation_time);
                    return_code = -1;
                }
            }
            referenced_size += sidx->size;

            g_info("Validating reference_id");
            if (video_pid != sidx->reference_id) {
                g_critical("ERROR validating Single Index Segment: invalid reference id in sidx box. \
Expected %d, actual %d.", video_pid, sidx->reference_id);
                return_code = -1;
            }

            // count number of Iframes and number of sidx boxes in reference list of this sidx
            if (analyze_sidx_references(sidx, &(iframes->num_iframes), &num_nested_sidx, is_simple_profile) != 0) {
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

    iframes->do_iframe_validation = 1;
    iframes->iframe_locations_time = (unsigned int*)calloc(iframes->num_iframes, sizeof(unsigned int));
    iframes->iframe_locations_byte = (uint64_t*)calloc(iframes->num_iframes, sizeof(uint64_t));
    iframes->starts_with_sap = (unsigned char*)calloc(iframes->num_iframes, sizeof(unsigned char));
    iframes->sap_type = (unsigned char*)calloc(iframes->num_iframes, sizeof(unsigned char));

    num_nested_sidx = 0;
    int iframe_counter = 0;
    unsigned int lastIFrameDuration = 0;
    uint64_t nextIFrameByteLocation = 0;
    segment_start_time = presentation_time_offset;
    for (box_index = 1; box_index < num_boxes; ++box_index) {
        if (boxes[box_index]->type != BOX_TYPE_SIDX) {
                continue;
        }
        data_sidx_t* sidx = (data_sidx_t*)boxes[box_index];

        if (num_nested_sidx > 0) {
            num_nested_sidx--;
            nextIFrameByteLocation += sidx->first_offset;  // convert from 64-bit t0 32 bit
        }

        // fill in Iframe locations here
        for (size_t i = 0; i < sidx->reference_count; i++) {
            data_sidx_reference_t ref = sidx->references[i];
            if (ref.reference_type == 0) {
                iframes->starts_with_sap[iframe_counter] = ref.starts_with_sap;
                iframes->sap_type[iframe_counter] = ref.sap_type;
                iframes->iframe_locations_byte[iframe_counter] = nextIFrameByteLocation;

                if (iframe_counter == 0) {
                    iframes->iframe_locations_time[iframe_counter] = segment_start_time + ref.sap_delta_time;
                } else {
                    iframes->iframe_locations_time[iframe_counter] =
                        iframes->iframe_locations_time[iframe_counter - 1] + lastIFrameDuration + ref.sap_delta_time;
                }
                iframe_counter++;
                lastIFrameDuration = ref.subsegment_duration;
                nextIFrameByteLocation += ref.referenced_size;
            } else {
                num_nested_sidx++;
            }
        }
    }

    return return_code;
}

int analyze_sidx_references(data_sidx_t* sidx, int* pnum_iframes, int* pnum_nested_sidx,
                          bool is_simple_profile)
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

    if (is_simple_profile) {
        if (originalnum_nested_sidx != *pnum_nested_sidx && originalnum_iframes != *pnum_iframes) {
            // failure -- references contain references to both media and nested sidx boxes
            g_critical("ERROR validating Representation Index Segment: Section 8.7.3: Simple profile requires that \
sidx boxes have either media references or sidx references, but not both.");
            return -1;
        }
    }

    return 0;
}

int validate_index_segment(char* file_name, size_t num_segments, uint64_t* segment_durations,
                         data_segment_iframes_t* iframes,
                         int presentation_time_offset, int video_pid, bool is_simple_profile)
{
    g_debug("validate_index_segment: %s", file_name);
    size_t num_boxes = 0;
    box_t** boxes = NULL;

    int return_code = read_boxes_from_file(file_name, &boxes, &num_boxes);
    if (return_code != 0) {
        g_critical("ERROR validating Index Segment: Error reading boxes from file.");
        goto fail;
    }

    print_boxes(boxes, num_boxes);

    if (num_segments <= 0) {
        g_critical("ERROR validating Index Segment: Invalid number of segments.");
        goto fail;
    } else if (num_segments == 1) {
        return_code = validate_single_index_segment_boxes(boxes, num_boxes,
                segment_durations[0], iframes, presentation_time_offset,
                video_pid, is_simple_profile);
    } else {
        return_code = validate_representation_index_segment_boxes(num_segments,
                boxes, num_boxes, segment_durations, iframes,
                presentation_time_offset, video_pid, is_simple_profile);
    }
    g_info(" ");

    /* What is the purpose of this? */
    for(size_t i = 0; i < num_segments; i++) {
        g_info("data_segment_iframes %zu: do_iframe_validation = %d, num_iframes = %d",
               i, iframes[i].do_iframe_validation, iframes[i].num_iframes);
        for(int j = 0; j < iframes[i].num_iframes; j++) {
            g_info("   iframe_locations_time[%d] = %d, \tiframe_locations_byte[%d] = %"PRId64, j,
                   iframes[i].iframe_locations_time[j], j, iframes[i].iframe_locations_byte[j]);
        }
    }
    g_info(" ");

cleanup:
    free_boxes(boxes, num_boxes);
    return return_code;
fail:
    return_code = -1;
    goto cleanup;
}

int validate_emsg_msg(uint8_t* buffer, size_t len, unsigned segment_duration)
{
    g_debug("validate_emsg_msg");

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
            g_critical("ERROR validating EMSG: Invalid box type found.");
            goto fail;
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