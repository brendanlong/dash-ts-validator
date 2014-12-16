
#include "segment_validator.h"

#include <glib.h>
#include <inttypes.h>
#include <errno.h>
#include "mpeg2ts_demux.h"
#include "h264_stream.h"
#include "ISOBMFF.h"

static dash_validator_t* g_p_dash_validator;
int g_nIFrameCntr;
data_segment_iframes_t* g_pIFrameData;
unsigned int g_segmentDuration;

pid_validator_t* pid_validator_new(int pid, int content_component)
{
    pid_validator_t* obj = calloc(1, sizeof(pid_validator_t));
    obj->PID = pid;
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

dash_validator_t* dash_validator_new(segment_type_t segment_type)
{
    dash_validator_t* obj = calloc(1, sizeof *obj);
    obj->pids = g_ptr_array_new_with_free_func((GDestroyNotify)pid_validator_free);
    return obj;
}

void dash_validator_free(dash_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }
    g_ptr_array_free(obj->pids, true);
    free(obj);
}

int pat_processor(mpeg2ts_stream_t* m2s, void* arg)
{
    if(g_p_dash_validator->conformance_level & TS_TEST_DASH && m2s->programs->len != 1) {
        g_critical("DASH Conformance: 6.4.4.2  media segments shall contain exactly one program (%u found)",
                       m2s->programs->len);
        g_p_dash_validator->status = 0;
        return 0;
    }

    if(g_p_dash_validator->use_initialization_segment) {
        g_critical("DASH Conformance: No PAT allowed if initialization segment is used");
        g_p_dash_validator->status = 0;
        return 0;
    }

    for(gsize i = 0; i < m2s->programs->len; i++) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);
        m2p->pmt_processor =  pmt_processor;
    }

    g_p_dash_validator->psi_tables_seen |= (1 << m2s->pat->table_id);
    return 1;
}

int cat_processor(mpeg2ts_stream_t* m2s, void* arg)
{
    conditional_access_section_print(m2s->cat);

    // TODO: Register an EMM processor here

    return 1;
}

pid_validator_t* dash_validator_find_pid(int PID)
{
    for(gsize i = 0; i < g_p_dash_validator->pids->len; ++i) {
        pid_validator_t* pv = g_ptr_array_index(g_p_dash_validator->pids, i);
        if(pv->PID == PID) {
            return pv;
        }
    }

    return NULL;
}

// TODO: fix tpes to try creating last PES packet
int pmt_processor(mpeg2ts_program_t* m2p, void* arg)
{
    if(m2p->pmt == NULL) {  // if we don't have any PSI, there's nothing we can do
        return 0;
    }

    if(g_p_dash_validator->use_initialization_segment) {
        g_critical("DASH Conformance: No PMT allowed if initialization segment is used");
        g_p_dash_validator->status = 0;
        return 0;
    }

    //program_map_section_print(m2p->pmt);

    g_p_dash_validator->PCR_PID = m2p->pmt->PCR_PID;
    g_p_dash_validator->psi_tables_seen |= (1 << m2p->pmt->table_id);
    g_p_dash_validator->pmt_program_number = m2p->pmt->program_number;
    g_p_dash_validator->pmt_version_number = m2p->pmt->version_number;

    GHashTableIter i;
    g_hash_table_iter_init(&i, m2p->pids);
    pid_info_t* pi;
    while(g_hash_table_iter_next(&i, NULL, (void**)&pi)) {
        int process_pid = 0;
        int content_component = 0;
        int PID = pi->es_info->elementary_PID;

        pid_validator_t* pid_validator = dash_validator_find_pid(PID);

// TODO: we need to figure out what we do when section versions change
// Do we need to fix something? Profile something out?
        assert(pid_validator == NULL);

        switch(pi->es_info->stream_type) {
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
            g_p_dash_validator->videoPID = PID;
            break;

        case  STREAM_TYPE_MPEG1_AUDIO:
        case  STREAM_TYPE_MPEG2_AUDIO:
        case  STREAM_TYPE_MPEG4_AAC_RAW:
        case  STREAM_TYPE_MPEG2_AAC:
        case  STREAM_TYPE_MPEG4_AAC:
            process_pid = 1;
//           printf ("pi->es_info->stream_type = %d\n", pi->es_info->stream_type);
            content_component = AUDIO_CONTENT_COMPONENT;
            g_p_dash_validator->audioPID = PID;
            break;

        default:
            process_pid = 0;
        }

        if(process_pid) {
            // hook PES validation to PES demuxer
            pes_demux_t* pd = pes_demux_new(validate_pes_packet);
            pd->pes_arg = NULL;
            pd->pes_arg_destructor = NULL;
            pd->process_pes_packet = validate_pes_packet;

            // hook PES demuxer to the PID processor
            demux_pid_handler_t* demux_handler = calloc(1, sizeof(demux_pid_handler_t));
            demux_handler->process_ts_packet = pes_demux_process_ts_packet;
            demux_handler->arg = pd;
            demux_handler->arg_destructor = (arg_destructor_t)pes_demux_free;


            // hook PES demuxer to the PID processor
            demux_pid_handler_t* demux_validator = calloc(1, sizeof(demux_pid_handler_t));
            demux_validator->process_ts_packet = validate_ts_packet;
            demux_validator->arg = NULL;
            demux_validator->arg_destructor = NULL;

            // hook PID processor to PID
            mpeg2ts_program_register_pid_processor(m2p, pi->es_info->elementary_PID, demux_handler,
                                                   demux_validator);

            pid_validator = pid_validator_new(PID, content_component);
            g_ptr_array_add(g_p_dash_validator->pids, pid_validator);
            // TODO:
            // parse CA descriptors, add ca system and ecm_pid if they don't exist yet
        }
    }
//   free(pmt_str);
    return 1;
}

// TODO: fix tpes to try creating last PES packet
int copy_pmt_info(mpeg2ts_program_t* m2p, dash_validator_t* dash_validator_source,
                  dash_validator_t* dash_validator_dest)
{
    pid_validator_t* pid_validator_dest = NULL;

    dash_validator_dest->PCR_PID = dash_validator_source->PCR_PID;
    dash_validator_dest->psi_tables_seen = 0;

    g_info("copy_pmt_info: dash_validator_source->pids->len = %u",
                  dash_validator_source->pids->len);
    for(gsize i = 0; i < dash_validator_source->pids->len; ++i) {
        pid_validator_t* pid_validator_src = g_ptr_array_index(dash_validator_source->pids, i);
        int content_component = 0;
        int PID = pid_validator_src->PID;

        // hook PES validation to PES demuxer
        pes_demux_t* pd = pes_demux_new(validate_pes_packet);
        pd->pes_arg = NULL;
        pd->pes_arg_destructor = NULL;
        pd->process_pes_packet = validate_pes_packet;

        // hook PES demuxer to the PID processor
        demux_pid_handler_t* demux_handler = calloc(1, sizeof(demux_pid_handler_t));
        demux_handler->process_ts_packet = pes_demux_process_ts_packet;
        demux_handler->arg = pd;
        demux_handler->arg_destructor = (arg_destructor_t)pes_demux_free;

        // hook PES demuxer to the PID processor
        demux_pid_handler_t* demux_validator = calloc(1, sizeof(demux_pid_handler_t));
        demux_validator->process_ts_packet = validate_ts_packet;
        demux_validator->arg = NULL;
        demux_validator->arg_destructor = NULL;

        // hook PID processor to PID
        mpeg2ts_program_register_pid_processor(m2p, PID, demux_handler, demux_validator);

        pid_validator_dest = pid_validator_new(PID, content_component);

        g_info("copy_pmt_info: adding pid_validator %"PRIxPTR" for PID %d", (uintptr_t)pid_validator_dest,
                      PID);
        g_ptr_array_add(dash_validator_dest->pids, pid_validator_dest);
        // TODO: parse CA descriptors, add ca system and ecm_pid if they don't exist yet
    }

    return 0;
}

int validate_ts_packet(ts_packet_t* ts, elementary_stream_info_t* es_info, void* arg)
{
    if(ts == NULL || es_info == NULL) {
        return 0;
    }

    pid_validator_t* pid_validator = dash_validator_find_pid(ts->header.PID);

    assert(pid_validator != NULL);

    if(g_p_dash_validator->PCR_PID == ts->header.PID) {
        int64_t pcr = ts_read_pcr(ts);
        if(PCR_IS_VALID(pcr)) {
            g_p_dash_validator->last_pcr = pcr;
        }
    }

    // This is the first TS packet from this PID
    if(pid_validator->ts_cnt == 0) {
        pid_validator->continuity_counter = ts->header.continuity_counter;

        // we ignore non-payload and non-media packets
        if((ts->header.adaptation_field_control & TS_PAYLOAD)
                && (pid_validator->content_component != UNKNOWN_CONTENT_COMPONENT)) {
            // if we only have complete PES packets, we must start with PUSI=1 followed by PES header in the first payload-bearing packet
            if((g_p_dash_validator->conformance_level & TS_TEST_MAIN)
                    && (ts->header.payload_unit_start_indicator == 0)) {
                g_critical("DASH Conformance: media segments shall contain only complete PES packets");
                g_p_dash_validator->status = 0;
            }

            // by the time we get to the start of the first PES, we need to have seen at least one PCR.
            if(g_p_dash_validator->conformance_level & TS_TEST_SIMPLE) {
                if(!PCR_IS_VALID(g_p_dash_validator->last_pcr)) {
                    g_critical("DASH Conformance: PCR must be present before first bytes of media data");
                    g_p_dash_validator->status = 0;
                }
            }
        }
    }

    // TODO: check for discontinuities
    //       -> CC errors, declared discontinuities
    //       -> PCR-PCR distances, with some arbitrary tolerance
    // we need to figure out what to do here -- we can let PES parsing fail and get a notice

    pid_validator->ts_cnt++;
    return 1;
}

int validate_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GQueue* ts_queue,
                        void* arg)
{
    if(esi == NULL) {
        return 0;
    }

    if(pes == NULL) {
        // we have a queue that didn't appear to be a valid TS packet (e.g., because it didn't start from PUSI=1)
        if(g_p_dash_validator->conformance_level & TS_TEST_MAIN) {
            g_critical("DASH Conformance: media segments shall contain only complete PES packets");
            g_p_dash_validator->status = 0;
            return 0;
        }

        g_critical("NULL PES packet!");
        g_p_dash_validator->status = 0;
        return 0;
    }

    if(g_p_dash_validator->segment_type == INITIALIZATION_SEGMENT) {
        g_critical("DASH Conformance: initialization segment cannot contain program stream");
        g_p_dash_validator->status = 0;
        return 0;
    }

    ts_packet_t* first_ts = g_queue_peek_head(ts_queue);
    pid_validator_t* pid_validator = dash_validator_find_pid(first_ts->header.PID);

    if(first_ts->header.PID == PID_EMSG) {
        uint8_t* buf = pes->payload;
        int len = pes->payload_len;
        if(validateEmsgMsg(buf, len, g_segmentDuration) != 0) {
            g_critical("DASH Conformance: validation of EMSG failed");
            g_p_dash_validator->status = 0;
        }
    }

    assert(pid_validator != NULL);
    if(pes->status > 0) {
        if(g_p_dash_validator->conformance_level & TS_TEST_MAIN) {
            g_critical("DASH Conformance: media segments shall contain only complete PES packets");
            g_p_dash_validator->status = 0;
        }
        pes_free(pes);
        return 0;
    }

    // we are in the first PES packet of a PID
    if(pid_validator->pes_cnt == 0) {
        if(pes->header.PTS_DTS_flags & PES_PTS_FLAG) {
            pid_validator->EPT = pes->header.PTS;
            pid_validator->LPT = pes->header.PTS;
        } else {
            if(g_p_dash_validator->conformance_level & TS_TEST_MAIN) {
                g_critical("DASH Conformance: first PES packet must have PTS");
                g_p_dash_validator->status = 0;
            }
        }

        if(first_ts->adaptation_field.random_access_indicator) {
            pid_validator->SAP = 1; // we trust AF by default.
            if(pid_validator->content_component == VIDEO_CONTENT_COMPONENT) {
                int nal_start, nal_end;
                int returnCode;
                uint8_t* buf = pes->payload;
                int len = pes->payload_len;

                // walk the nal units in the PES payload and check to see if they are type 1 or type 5 -- these determine
                // SAP type
                int index = 0;
                while((len > index)
                        && ((returnCode = find_nal_unit(buf + index, len - index, &nal_start, &nal_end)) !=  0)) {
                    h264_stream_t* h = h264_new();
                    read_nal_unit(h, &buf[nal_start + index], nal_end - nal_start);
                    int unit_type = h->nal->nal_unit_type;
                    h264_free(h);
                    if (unit_type == 5) {
                        pid_validator->SAP_type = 1;
                        break;
                    } else if (unit_type == 1) {
                        pid_validator->SAP_type = 2;
                        break;
                    }

                    index += nal_end;
                }
            }
        }

        // TODO: validate in case of ISO/IEC 14496-10 (?)
    }

    if(pes->header.PTS_DTS_flags & PES_PTS_FLAG) {
        // FIXME: account for rollovers and discontinuities
        // frames can come in out of PTS order
        pid_validator->EPT = (pid_validator->EPT < pes->header.PTS) ? pid_validator->EPT : pes->header.PTS;
        pid_validator->LPT = (pid_validator->LPT > pes->header.PTS) ? pid_validator->LPT : pes->header.PTS;
    }

    if(pid_validator->content_component == VIDEO_CONTENT_COMPONENT) {
        pid_validator->duration = 3000;

        if(g_pIFrameData->doIFrameValidation && first_ts->adaptation_field.random_access_indicator) {
            g_info("Performing IFrame validation");
            // check iFrame location against index file

            if(g_nIFrameCntr < g_pIFrameData->numIFrames) {
                unsigned int expectedIFramePTS = g_pIFrameData->pIFrameLocations_Time[g_nIFrameCntr];
                unsigned int actualIFramePTS = pes->header.PTS;
                g_info("expectedIFramePTS = %u, actualIFramePTS = %u", expectedIFramePTS, actualIFramePTS);
                if(expectedIFramePTS != actualIFramePTS) {
                    g_critical("DASH Conformance: expected IFrame PTS does not match actual.  Expected: %d, Actua: %d",
                                   expectedIFramePTS, actualIFramePTS);
                    g_p_dash_validator->status = 0;
                }

                // check frame byte location
                uint64_t expectedFrameByteLocation = g_pIFrameData->pIFrameLocations_Byte[g_nIFrameCntr];
                uint64_t actualFrameByteLocation = pes->payload_pos_in_stream;
                g_info("expectedIFrameByteLocation = %"PRId64", actualIFrameByteLocation = %"PRId64"",
                              expectedFrameByteLocation, actualFrameByteLocation);
                if(expectedFrameByteLocation != actualFrameByteLocation) {
                    g_critical("DASH Conformance: expected IFrame Byte Locaton does not match actual.  Expected: %"PRId64", Actual: %"PRId64"",
                                   expectedFrameByteLocation, actualFrameByteLocation);
                    g_p_dash_validator->status = 0;
                }

                // check SAP type
                unsigned char expectedStartsWithSAP = g_pIFrameData->pStartsWithSAP[g_nIFrameCntr];
                unsigned char expectedSAPType = g_pIFrameData->pSAPType[g_nIFrameCntr];
                unsigned char actualSAPType = pid_validator->SAP_type;
                g_info("expectedStartsWithSAP = %d, expectedSAPType = %d, actualSAPType = %d",
                              expectedStartsWithSAP, expectedSAPType, actualSAPType);
                if(expectedStartsWithSAP == 1 && expectedSAPType != 0 && expectedSAPType != actualSAPType) {
                    g_critical("DASH Conformance: expected IFrame SAP Type does not match actual: expectedStartsWithSAP = %d, \
expectedSAPType = %d, actualSAPType = %d", expectedStartsWithSAP, expectedSAPType, actualSAPType);
                    g_p_dash_validator->status = 0;
                }

                g_nIFrameCntr++;
            } else {
                g_critical("DASH Conformance: Stream has more IFrames than index file");
                g_p_dash_validator->status = 0;
            }
        }
    }

    if(pid_validator->content_component == AUDIO_CONTENT_COMPONENT) {
        int index = 0;
        int frame_cntr = 0;
        while(index < pes->payload_len) {
            unsigned int frame_length = ((pes->payload[index + 3] & 0x0003) << 11) +
                                        ((pes->payload[index + 4]) << 3) + ((pes->payload[index + 5] & 0x00E0) >> 5);

            index += frame_length;
            frame_cntr++;
        }

        pid_validator->duration = 1920 /* 21.3 msec for 90kHz clock */ * frame_cntr;
    }

    pid_validator->pes_cnt++;

    pes_free(pes);
    return 1;
}

int doSegmentValidation(dash_validator_t* dash_validator, char* fname,
                        dash_validator_t* dash_validator_init,
                        data_segment_iframes_t* pIFrameData, unsigned int segmentDuration)
{
    g_p_dash_validator = dash_validator;
    g_nIFrameCntr = 0;
    g_pIFrameData = pIFrameData;
    g_segmentDuration = segmentDuration;

    g_info("doSegmentValidation : %s", fname);

    mpeg2ts_stream_t* m2s = NULL;

// GORP:    initialization segment shall not contain any media data with an assigned presentation time
// GORP:    initialization segment shall contain PAT and PMT and PCR
// GORP: self-initializing segment shall contain PAT and PMT and PCR
// GORP:     check for complete transport stream packets
// GORP:         check for complete PES packets

    FILE* infile = NULL;
    if((infile = fopen(fname, "rb")) == NULL) {
        g_critical("Cannot open file %s - %s", fname, strerror(errno));
        g_p_dash_validator->status = 0;
        return 1;
    }

    if(g_p_dash_validator->segment_start > 0) {
        if(!fseek(infile, g_p_dash_validator->segment_start, SEEK_SET)) {
            g_critical("Error seeking to offset %ld - %s", g_p_dash_validator->segment_start,
                           strerror(errno));
            g_p_dash_validator->status = 0;
            return 1;
        }
    }
    if(NULL == (m2s = mpeg2ts_stream_new())) {
        g_critical("Error creating MPEG-2 STREAM object");
        g_p_dash_validator->status = 0;
        return 1;
    }

    g_p_dash_validator->last_pcr = PCR_INVALID;
    g_p_dash_validator->status = 1;
    if (g_p_dash_validator->pids->len != 0) {
        g_error("Re-using DASH validator pids!");
        return 1;
    }

    // if had intialization segment, then copy program info and setup PES callbacks
    if(dash_validator_init != NULL) {
        mpeg2ts_program_t* prog = mpeg2ts_program_new(
                                      200 /* GORP */,  // can I just use dummy values here?
                                      201 /* GORP */);
        prog->pmt = dash_validator_init->initializaion_segment_pmt;

        g_info("Adding initialization PSI info...program = %"PRIXPTR, (uintptr_t)prog);
        g_ptr_array_add(m2s->programs, prog);

        int returnCode = copy_pmt_info(prog, dash_validator_init, g_p_dash_validator);
        if(returnCode != 0) {
            g_critical("Error copying PMT info");
            g_p_dash_validator->status = 0;
            return 1;
        }
    }

    m2s->pat_processor = pat_processor;

    long packet_buf_size = 4096;
    uint64_t packets_read = 0;
    long num_packets;
    uint64_t packets_to_read =  UINT64_MAX;
    uint8_t* ts_buf = malloc(TS_SIZE * packet_buf_size);

    if(g_p_dash_validator->segment_end > 0) {
        packets_to_read = (g_p_dash_validator->segment_end - g_p_dash_validator->segment_start) / ((
                              uint64_t)TS_SIZE);
    }

    while((num_packets = fread(ts_buf, TS_SIZE, packet_buf_size, infile)) > 0) {
        for(int i = 0; i < num_packets; i++) {
            int res = 0;
            ts_packet_t* ts = ts_new();
            if((res = ts_read(ts, ts_buf + i * TS_SIZE, TS_SIZE, packets_read)) < TS_SIZE) {
                g_critical("Error parsing TS packet %"PRIo64" (%d)", packets_read, res);
                g_p_dash_validator->status = 0;
                break;
            }

            mpeg2ts_stream_read_ts_packet(m2s, ts);
            packets_read++;
        }

        if(packets_to_read - packets_read < packet_buf_size) {
            packet_buf_size = (long)(packets_to_read - packets_read);
        }
    }
    free(ts_buf);

    // need to reset the mpeg stream to be sure to process the last PES packet
    mpeg2ts_stream_reset(m2s);

    g_info("%"PRIo64" TS packets read", packets_read);

    if(g_p_dash_validator->segment_type == INITIALIZATION_SEGMENT) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, 0);  // should be only one program
        printf("m2p = %"PRIxPTR"\n", (uintptr_t)m2p);
        g_p_dash_validator->initializaion_segment_pmt = m2p->pmt;
    }

    mpeg2ts_stream_free(m2s);
    fclose(infile);
    return tslib_errno;
}


void doDASHEventValidation(uint8_t* buf, int len)
{
    if(validateEmsgMsg(buf, len, g_segmentDuration) != 0) {
        g_critical("DASH Conformance: validation of EMSG failed");
        g_p_dash_validator->status = 0;
    }
}