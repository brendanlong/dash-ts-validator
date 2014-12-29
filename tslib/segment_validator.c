
#include "segment_validator.h"

#include <glib.h>
#include <inttypes.h>
#include <errno.h>
#include "mpeg2ts_demux.h"
#include "h264_stream.h"
#include "ISOBMFF.h"

static dash_validator_t* global_dash_validator;
int global_iframe_counter;
data_segment_iframes_t* global_iframe_data;
unsigned int global_segment_duration;

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
    if (global_dash_validator->conformance_level & TS_TEST_DASH && m2s->programs->len != 1) {
        g_critical("DASH Conformance: 6.4.4.2  media segments shall contain exactly one program (%u found)",
                       m2s->programs->len);
        global_dash_validator->status = 0;
        return 0;
    }

    if (global_dash_validator->use_initialization_segment) {
        g_critical("DASH Conformance: No PAT allowed if initialization segment is used");
        global_dash_validator->status = 0;
        return 0;
    }

    for (gsize i = 0; i < m2s->programs->len; i++) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);
        m2p->pmt_processor =  pmt_processor;
    }

    global_dash_validator->psi_tables_seen |= 1 << m2s->pat->table_id;
    return 1;
}

int cat_processor(mpeg2ts_stream_t* m2s, void* arg)
{
    conditional_access_section_print(m2s->cat);

    // TODO: Register an EMM processor here

    return 1;
}

pid_validator_t* dash_validator_find_pid(int pid)
{
    for (gsize i = 0; i < global_dash_validator->pids->len; ++i) {
        pid_validator_t* pv = g_ptr_array_index(global_dash_validator->pids, i);
        if (pv->pid == pid) {
            return pv;
        }
    }
    return NULL;
}

// TODO: fix tpes to try creating last PES packet
int pmt_processor(mpeg2ts_program_t* m2p, void* arg)
{
    if (m2p->pmt == NULL) {  // if we don't have any PSI, there's nothing we can do
        return 0;
    }

    if (global_dash_validator->use_initialization_segment) {
        g_critical("DASH Conformance: No PMT allowed if initialization segment is used");
        global_dash_validator->status = 0;
        return 0;
    }

    global_dash_validator->pcr_pid = m2p->pmt->pcr_pid;
    global_dash_validator->psi_tables_seen |= 1 << m2p->pmt->table_id;
    global_dash_validator->pmt_program_number = m2p->pmt->program_number;
    global_dash_validator->pmt_version_number = m2p->pmt->version_number;

    GHashTableIter i;
    g_hash_table_iter_init(&i, m2p->pids);
    pid_info_t* pi;
    while (g_hash_table_iter_next(&i, NULL, (void**)&pi)) {
        int process_pid = 0;
        int content_component = 0;
        int pid = pi->es_info->elementary_pid;

        pid_validator_t* pid_validator = dash_validator_find_pid(pid);

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
            global_dash_validator->video_pid = pid;
            break;
        case  STREAM_TYPE_MPEG1_AUDIO:
        case  STREAM_TYPE_MPEG2_AUDIO:
        case  STREAM_TYPE_MPEG4_AAC_RAW:
        case  STREAM_TYPE_MPEG2_AAC:
        case  STREAM_TYPE_MPEG4_AAC:
            process_pid = 1;
            content_component = AUDIO_CONTENT_COMPONENT;
            global_dash_validator->audio_pid = pid;
            break;
        default:
            process_pid = 0;
        }

        if (process_pid) {
            // hook PES validation to PES demuxer
            pes_demux_t* pd = pes_demux_new(validate_pes_packet);
            pd->pes_arg = NULL;
            pd->pes_arg_destructor = NULL;
            pd->process_pes_packet = validate_pes_packet;

            // hook PES demuxer to the PID processor
            demux_pid_handler_t* demux_handler = calloc(1, sizeof(*demux_handler));
            demux_handler->process_ts_packet = pes_demux_process_ts_packet;
            demux_handler->arg = pd;
            demux_handler->arg_destructor = (arg_destructor_t)pes_demux_free;

            // hook PES demuxer to the PID processor
            demux_pid_handler_t* demux_validator = calloc(1, sizeof(*demux_validator));
            demux_validator->process_ts_packet = validate_ts_packet;
            demux_validator->arg = NULL;
            demux_validator->arg_destructor = NULL;

            // hook PID processor to PID
            mpeg2ts_program_register_pid_processor(m2p, pi->es_info->elementary_pid,
                    demux_handler, demux_validator);

            pid_validator = pid_validator_new(pid, content_component);
            g_ptr_array_add(global_dash_validator->pids, pid_validator);
            // TODO:
            // parse CA descriptors, add ca system and ecm_pid if they don't exist yet
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
        pd->pes_arg = NULL;
        pd->pes_arg_destructor = NULL;
        pd->process_pes_packet = validate_pes_packet;

        // hook PES demuxer to the PID processor
        demux_pid_handler_t* demux_handler = calloc(1, sizeof(*demux_handler));
        demux_handler->process_ts_packet = pes_demux_process_ts_packet;
        demux_handler->arg = pd;
        demux_handler->arg_destructor = (arg_destructor_t)pes_demux_free;

        // hook PES demuxer to the PID processor
        demux_pid_handler_t* demux_validator = calloc(1, sizeof(*demux_validator));
        demux_validator->process_ts_packet = validate_ts_packet;
        demux_validator->arg = NULL;
        demux_validator->arg_destructor = NULL;

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
    if (ts == NULL || es_info == NULL) {
        return 0;
    }

    pid_validator_t* pid_validator = dash_validator_find_pid(ts->header.pid);
    assert(pid_validator != NULL);

    if (global_dash_validator->pcr_pid == ts->header.pid) {
        int64_t pcr = ts_read_pcr(ts);
        if (PCR_IS_VALID(pcr)) {
            global_dash_validator->last_pcr = pcr;
        }
    }

    // This is the first TS packet from this PID
    if (pid_validator->ts_count == 0) {
        pid_validator->continuity_counter = ts->header.continuity_counter;

        // we ignore non-payload and non-media packets
        if ((ts->header.adaptation_field_control & TS_PAYLOAD)
                && (pid_validator->content_component != UNKNOWN_CONTENT_COMPONENT)) {
            // if we only have complete PES packets, we must start with PUSI=1 followed by PES header in the first payload-bearing packet
            if ((global_dash_validator->conformance_level & TS_TEST_MAIN)
                    && (ts->header.payload_unit_start_indicator == 0)) {
                g_critical("DASH Conformance: media segments shall contain only complete PES packets");
                global_dash_validator->status = 0;
            }

            // by the time we get to the start of the first PES, we need to have seen at least one PCR.
            if (global_dash_validator->conformance_level & TS_TEST_SIMPLE) {
                if(!PCR_IS_VALID(global_dash_validator->last_pcr)) {
                    g_critical("DASH Conformance: PCR must be present before first bytes of media data");
                    global_dash_validator->status = 0;
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

int validate_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GQueue* ts_queue,
                        void* arg)
{
    if (esi == NULL) {
        return 0;
    }

    if (pes == NULL) {
        // we have a queue that didn't appear to be a valid TS packet (e.g., because it didn't start from PUSI=1)
        if (global_dash_validator->conformance_level & TS_TEST_MAIN) {
            g_critical("DASH Conformance: media segments shall contain only complete PES packets");
            global_dash_validator->status = 0;
            return 0;
        }

        g_critical("NULL PES packet!");
        global_dash_validator->status = 0;
        return 0;
    }

    if (global_dash_validator->segment_type == INITIALIZATION_SEGMENT) {
        g_critical("DASH Conformance: initialization segment cannot contain program stream");
        global_dash_validator->status = 0;
        return 0;
    }

    ts_packet_t* first_ts = g_queue_peek_head(ts_queue);
    pid_validator_t* pid_validator = dash_validator_find_pid(first_ts->header.pid);

    if (first_ts->header.pid == PID_EMSG) {
        uint8_t* buf = pes->payload;
        int len = pes->payload_len;
        if(validate_emsg_msg(buf, len, global_segment_duration) != 0) {
            g_critical("DASH Conformance: validation of EMSG failed");
            global_dash_validator->status = 0;
        }
    }

    assert(pid_validator != NULL);
    if (pes->status > 0) {
        if (global_dash_validator->conformance_level & TS_TEST_MAIN) {
            g_critical("DASH Conformance: media segments shall contain only complete PES packets");
            global_dash_validator->status = 0;
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
            if(global_dash_validator->conformance_level & TS_TEST_MAIN) {
                g_critical("DASH Conformance: first PES packet must have PTS");
                global_dash_validator->status = 0;
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
        // FIXME: account for rollovers and discontinuities
        // frames can come in out of PTS order
        pid_validator->earliest_playout_time = (pid_validator->earliest_playout_time < pes->header.pts) ? pid_validator->earliest_playout_time : pes->header.pts;
        pid_validator->latest_playout_time = (pid_validator->latest_playout_time > pes->header.pts) ? pid_validator->latest_playout_time : pes->header.pts;
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
                    global_dash_validator->status = 0;
                }

                // check frame byte location
                uint64_t expectedFrameByteLocation = global_iframe_data->iframe_locations_byte[global_iframe_counter];
                uint64_t actualFrameByteLocation = pes->payload_pos_in_stream;
                g_debug("expectedIFrameByteLocation = %"PRId64", actualIFrameByteLocation = %"PRId64"",
                              expectedFrameByteLocation, actualFrameByteLocation);
                if (expectedFrameByteLocation != actualFrameByteLocation) {
                    g_critical("DASH Conformance: expected IFrame Byte Locaton does not match actual.  Expected: %"PRId64", Actual: %"PRId64"",
                                   expectedFrameByteLocation, actualFrameByteLocation);
                    global_dash_validator->status = 0;
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
                    global_dash_validator->status = 0;
                }

                global_iframe_counter++;
            } else {
                g_critical("DASH Conformance: Stream has more IFrames than index file");
                global_dash_validator->status = 0;
            }
        }
    }

    if (pid_validator->content_component == AUDIO_CONTENT_COMPONENT) {
        int index = 0;
        int frame_counter = 0;
        while (index < pes->payload_len) {
            unsigned int frame_length = ((pes->payload[index + 3] & 0x0003) << 11) +
                                        ((pes->payload[index + 4]) << 3) + ((pes->payload[index + 5] & 0x00E0) >> 5);

            index += frame_length;
            frame_counter++;
        }

        pid_validator->duration = 1920 /* 21.3 msec for 90kHz clock */ * frame_counter;
    }

    pid_validator->pes_count++;
    pes_free(pes);
    return 1;
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

// GORP:    initialization segment shall not contain any media data with an assigned presentation time
// GORP:    initialization segment shall contain PAT and PMT and PCR
// GORP: self-initializing segment shall contain PAT and PMT and PCR
// GORP:     check for complete transport stream packets
// GORP:         check for complete PES packets

    FILE* infile = fopen(fname, "rb");
    if (infile == NULL) {
        g_critical("Cannot open file %s - %s", fname, strerror(errno));
        global_dash_validator->status = 0;
        return 1;
    }

    if (global_dash_validator->segment_start > 0) {
        if (!fseek(infile, global_dash_validator->segment_start, SEEK_SET)) {
            g_critical("Error seeking to offset %ld - %s", global_dash_validator->segment_start,
                           strerror(errno));
            global_dash_validator->status = 0;
            return 1;
        }
    }

    mpeg2ts_stream_t* m2s = mpeg2ts_stream_new();
    if (m2s == NULL) {
        g_critical("Error creating MPEG-2 STREAM object");
        global_dash_validator->status = 0;
        return 1;
    }

    global_dash_validator->last_pcr = PCR_INVALID;
    global_dash_validator->status = 1;
    if (global_dash_validator->pids->len != 0) {
        g_error("Re-using DASH validator pids!");
        return 1;
    }

    // if had intialization segment, then copy program info and setup PES callbacks
    if (dash_validator_init != NULL) {
        mpeg2ts_program_t* prog = mpeg2ts_program_new(
                                      200 /* GORP */,  // can I just use dummy values here?
                                      201 /* GORP */);
        prog->pmt = dash_validator_init->initializaion_segment_pmt;

        g_debug("Adding initialization PSI info...program = %"PRIXPTR, (uintptr_t)prog);
        g_ptr_array_add(m2s->programs, prog);

        int return_code = copy_pmt_info(prog, dash_validator_init, global_dash_validator);
        if (return_code != 0) {
            g_critical("Error copying PMT info");
            global_dash_validator->status = 0;
            return 1;
        }
    }

    m2s->pat_processor = pat_processor;

    long packet_buf_size = 4096;
    uint64_t packets_read = 0;
    long num_packets;
    uint64_t packets_to_read =  UINT64_MAX;
    uint8_t* ts_buf = malloc(TS_SIZE * packet_buf_size);

    if (global_dash_validator->segment_end > 0) {
        packets_to_read = (global_dash_validator->segment_end - global_dash_validator->segment_start) / (uint64_t)TS_SIZE;
    }

    while ((num_packets = fread(ts_buf, TS_SIZE, packet_buf_size, infile)) > 0) {
        for (int i = 0; i < num_packets; i++) {
            ts_packet_t* ts = ts_new();
            int res = ts_read(ts, ts_buf + i * TS_SIZE, TS_SIZE, packets_read);
            if (res < TS_SIZE) {
                g_critical("Error parsing TS packet %"PRIo64" (%d)", packets_read, res);
                global_dash_validator->status = 0;
                break;
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

    if (global_dash_validator->segment_type == INITIALIZATION_SEGMENT) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, 0);  // should be only one program
        printf("m2p = %"PRIxPTR"\n", (uintptr_t)m2p);
        global_dash_validator->initializaion_segment_pmt = m2p->pmt;
    }

    mpeg2ts_stream_free(m2s);
    fclose(infile);
    return tslib_errno;
}

void validate_dash_events(uint8_t* buf, int len)
{
    if (validate_emsg_msg(buf, len, global_segment_duration) != 0) {
        g_critical("DASH Conformance: validation of EMSG failed");
        global_dash_validator->status = 0;
    }
}