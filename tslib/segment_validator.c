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
#include "segment_validator.h"

#include <inttypes.h>
#include <errno.h>

#include "cets_ecm.h"
#include "h264_stream.h"
#include "mpeg2ts_demux.h"
#include "pes_demux.h"


static void cat_processor(mpeg2ts_stream_t*, void*);
static void pat_processor(mpeg2ts_stream_t*, void*);
static void pmt_processor(mpeg2ts_program_t*, void*);
static void validate_ts_packet(ts_packet_t*, elementary_stream_info_t*, void*);
static void validate_pes_packet(pes_packet_t*, elementary_stream_info_t*, GArray* ts_packets, void*);
static int validate_emsg_msg(uint8_t* buffer, size_t len, unsigned segment_duration);
static int analyze_sidx_references(sidx_t*, int* num_subsegments, int* num_nested_sidx, dash_profile_t);


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

static subsegment_t* subsegment_new(void)
{
    subsegment_t* obj = g_new0(subsegment_t, 1);
    obj->ssix_offsets = g_array_new(false, false, sizeof(uint64_t));
    return obj;
}

static void subsegment_free(subsegment_t* obj)
{
    if (obj == NULL) {
        return;
    }
    g_array_free(obj->ssix_offsets, true);
    g_free(obj);
}

static index_segment_validator_t* index_segment_validator_new(void)
{
    index_segment_validator_t* obj = g_new0(index_segment_validator_t, 1);
    obj->segment_subsegments = g_ptr_array_new();
    return obj;
}

void index_segment_validator_free(index_segment_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }

    for (size_t i = 0; i < obj->segment_subsegments->len; ++i) {
        GPtrArray* subsegments = g_ptr_array_index(obj->segment_subsegments, i);
        g_ptr_array_set_free_func(subsegments, (GDestroyNotify)subsegment_free);
        g_ptr_array_free(subsegments, true);
    }
    g_ptr_array_free(obj->segment_subsegments, true);
    g_free(obj);
}

static pid_validator_t* pid_validator_new(uint16_t pid, content_component_t content_component)
{
    pid_validator_t* obj = calloc(1, sizeof(*obj));
    obj->pid = pid;
    obj->content_component = content_component;
    obj->ecm_pids = g_hash_table_new(g_direct_hash, g_direct_equal);
    return obj;
}

static void pid_validator_free(pid_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }
    g_hash_table_destroy(obj->ecm_pids);
    free(obj);
}

dash_validator_t* dash_validator_new(segment_type_t segment_type, dash_profile_t profile)
{
    dash_validator_t* obj = calloc(1, sizeof(*obj));
    obj->segment_type = segment_type;
    obj->profile = profile;
    obj->subsegments = g_ptr_array_new_with_free_func((GDestroyNotify)subsegment_free);
    obj->pids = g_ptr_array_new_with_free_func((GDestroyNotify)pid_validator_free);
    obj->ecm_pids = g_hash_table_new(g_direct_hash, g_direct_equal);
    obj->initialization_segment_ts = g_array_new(false, false, sizeof(ts_packet_t));
    return obj;
}

void dash_validator_free(dash_validator_t* obj)
{
    if (obj == NULL) {
        return;
    }
    program_association_section_unref(obj->pat);
    program_map_section_unref(obj->pmt);
    conditional_access_section_unref(obj->cat);
    g_ptr_array_free(obj->subsegments, true);
    g_ptr_array_free(obj->pids, true);
    g_hash_table_destroy(obj->ecm_pids);
    g_array_free(obj->initialization_segment_ts, true);
    free(obj);
}

static void pat_processor(mpeg2ts_stream_t* m2s, void* arg)
{
    g_return_if_fail(m2s);
    g_return_if_fail(m2s->pat);
    g_return_if_fail(m2s->programs);
    g_return_if_fail(arg);

    dash_validator_t* dash_validator = arg;
    dash_validator->pat = program_association_section_ref(m2s->pat);

    if (m2s->programs->len != 1) {
        g_critical("DASH Conformance: 6.4.4.2  Media segments shall contain exactly one program (%u found)",
                       m2s->programs->len);
        dash_validator->status = 0;
        return;
    }

    for (gsize i = 0; i < m2s->programs->len; i++) {
        mpeg2ts_program_t* m2p = g_ptr_array_index(m2s->programs, i);
        m2p->pmt_processor = pmt_processor;
        m2p->arg = dash_validator;
    }
}

static void cat_processor(mpeg2ts_stream_t* m2s, void* arg)
{
    g_return_if_fail(m2s);
    g_return_if_fail(m2s->cat);
    g_return_if_fail(arg);

    dash_validator_t* dash_validator = arg;
    dash_validator->cat = conditional_access_section_ref(m2s->cat);
}

static pid_validator_t* dash_validator_find_pid(int pid, dash_validator_t* dash_validator)
{
    g_return_val_if_fail(dash_validator, NULL);

    for (gsize i = 0; i < dash_validator->pids->len; ++i) {
        pid_validator_t* pv = g_ptr_array_index(dash_validator->pids, i);
        if (pv->pid == pid) {
            return pv;
        }
    }
    return NULL;
}

static void pmt_processor(mpeg2ts_program_t* m2p, void* arg)
{
    g_return_if_fail(m2p);
    g_return_if_fail(m2p->pmt);
    g_return_if_fail(arg);

    dash_validator_t* dash_validator = arg;
    dash_validator->pmt = program_map_section_ref(m2p->pmt);

    dash_validator->pcr_pid = m2p->pmt->pcr_pid;

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
        if (pid_validator != NULL) {
            g_ptr_array_remove(dash_validator->pids, pid_validator);
        }

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
            break;
        case  STREAM_TYPE_MPEG1_AUDIO:
        case  STREAM_TYPE_MPEG2_AUDIO:
        case  STREAM_TYPE_MPEG4_AAC_RAW:
        case  STREAM_TYPE_MPEG2_AAC:
        case  STREAM_TYPE_MPEG4_AAC:
            process_pid = 1;
            content_component = AUDIO_CONTENT_COMPONENT;
            break;
        default:
            process_pid = 0;
        }

        if (process_pid) {
            pid_validator = pid_validator_new(pid, content_component);
            g_ptr_array_add(dash_validator->pids, pid_validator);

            // Register callback for TS packets on CA_PID
            for (size_t d = 0; d < pi->es_info->descriptors_len; ++d) {
                descriptor_t* descriptor = pi->es_info->descriptors[d];
                if (descriptor->tag == CA_DESCRIPTOR) {
                    dash_validator->is_encrypted = true;
                    ca_descriptor_t* ca_descriptor = (ca_descriptor_t*)descriptor;
                    if (ca_descriptor->ca_system_id == 0x6365 /* 'ce' */) {
                        g_hash_table_add(dash_validator->ecm_pids, GINT_TO_POINTER(ca_descriptor->ca_pid));
                        g_hash_table_add(pid_validator->ecm_pids, GINT_TO_POINTER(ca_descriptor->ca_pid));
                    } else {
                        g_warning("Saw CA_descriptor with unknown system_id = %"PRIu16". Encrypted content must use "
                                "common encryption to be tested by this utility.", ca_descriptor->ca_system_id);
                    }
                }
            }

            // hook PES validation to PES demuxer
            pes_demux_t* pd = pes_demux_new(validate_pes_packet);
            pd->arg = dash_validator;

            // hook PES demuxer to the PID processor
            demux_pid_handler_t* demux_handler = demux_pid_handler_new(pes_demux_process_ts_packet);
            demux_handler->arg = pd;
            demux_handler->arg_destructor = (arg_destructor_t)pes_demux_free;

            // hook PID processor to PID
            mpeg2ts_program_register_pid_processor(m2p, pi->es_info->elementary_pid, demux_handler, NULL);
        }
    }
}

static void validate_ts_packet(ts_packet_t* ts, elementary_stream_info_t* esi, void* arg)
{
    g_return_if_fail(arg);
    if (ts == NULL) {
        /* NULL TS packet sent to end stream */
        return;
    }

    dash_validator_t* dash_validator = arg;

    if (dash_validator->segment_type == INITIALIZATION_SEGMENT && ts->adaptation_field.pcr_flag) {
        g_critical("DASH Conformance: TS packet in initialization segment has pcr_flag = 1. 6.4.3.2 says, "
                "\"PCR-bearing packets shall not be present in the Initialization Segment;\".");
        dash_validator->status = 0;
    }

    if (dash_validator->pcr_pid == ts->pid && ts->adaptation_field.pcr_flag) {
        dash_validator->last_pcr = ts->adaptation_field.program_clock_reference;
    }

    while (dash_validator->current_subsegment
            && ts->pos_in_stream >= dash_validator->current_subsegment->end_byte) {
        if (dash_validator->current_subsegment->ts_count == 0) {
            g_critical("Did not see any TS packets for subsegment %zu in segment %s. 6.4.2.3 Segment Index: All "
                    "media offsets within `sidx` boxes shall be to the first (sync) byte of a TS packet.",
                    dash_validator->subsegment_index, dash_validator->segment->file_name);
            dash_validator->status = 0;
        } else {
            if (!dash_validator->current_subsegment->saw_random_access) {
                g_critical("Error: Did not see iframe for subsegment %zu in segment %s.",
                        dash_validator->subsegment_index, dash_validator->segment->file_name);
                dash_validator->status = 0;
            }
            if (dash_validator->current_subsegment->ssix_offset_index != dash_validator->current_subsegment->ssix_offsets->len) {
                uint64_t next_ssix_offset = g_array_index(dash_validator->current_subsegment->ssix_offsets,
                        uint64_t, dash_validator->current_subsegment->ssix_offset_index);
                g_critical("Error: 'ssix' has next offset %"PRIu64", but the subsegment ends at %"PRIu64".",
                        next_ssix_offset, dash_validator->current_subsegment->end_byte);
            }
        }
        ++dash_validator->subsegment_index;
        if (dash_validator->subsegment_index >= dash_validator->subsegments->len) {
            dash_validator->current_subsegment = NULL;
        } else {
            dash_validator->current_subsegment = g_ptr_array_index(dash_validator->subsegments,
                    dash_validator->subsegment_index);
            dash_validator->current_subsegment->pes_count = 0;
        }
        for (size_t i = 0; i < dash_validator->pids->len; ++i) {
            pid_validator_t* pv = g_ptr_array_index(dash_validator->pids, i);
            for (size_t j = 1; j < TRANSPORT_SCRAMBLING_CONTROL_BITS; ++j) {
                pv->au_for_transport_scrambling_control[j] = 0;
            }
        }
    }
    subsegment_t* subsegment = dash_validator->current_subsegment;
    if (subsegment) {
        if (subsegment->ssix_offset_index < subsegment->ssix_offsets->len) {
            uint64_t next_ssix_offset = g_array_index(subsegment->ssix_offsets, uint64_t,
                    subsegment->ssix_offset_index);
            if (ts->pos_in_stream >= next_ssix_offset) {
                if (ts->pos_in_stream != next_ssix_offset) {
                    g_critical("DASH Conformance: Subsegment index in %s has offset %zu, but closest following TS "
                            "packet starts at %zu. 6.4.2.4 Subsegment Index: All media offsets within `ssix` boxes "
                            "shall be to the first (sync) byte of a TS packet.",
                            dash_validator->segment->file_name, next_ssix_offset, ts->pos_in_stream);
                    dash_validator->status = 0;
                }
                ++dash_validator->current_subsegment->ssix_offset_index;
            }
        }

        // First TS packet in this subsegment
        if (ts->pos_in_stream >= dash_validator->current_subsegment->start_byte
                && dash_validator->current_subsegment->ts_count == 0) {
            if (dash_validator->current_subsegment->start_byte != ts->pos_in_stream) {
                g_critical("DASH Conformance: Subsegment %zu in segment %s starts at byte offset %zu, but the sync byte "
                        "for the first TS packet following the subsegment start is at %zu. 6.4.2.3 Segment Index: All "
                        "media offsets within `sidx` boxes shall be to the first (sync) byte of a TS packet.",
                        dash_validator->subsegment_index, dash_validator->segment->file_name,
                        dash_validator->current_subsegment->start_byte, ts->pos_in_stream);
                dash_validator->status = 0;
            }
        }
        ++dash_validator->current_subsegment->ts_count;
    }

    if (g_hash_table_contains(dash_validator->ecm_pids, GINT_TO_POINTER(ts->pid))) {
        cets_ecm_t* cets_ecm = cets_ecm_read(ts->payload, ts->payload_len);
        if (!cets_ecm) {
            g_critical("Invalid CETS ECM found on PID %"PRIu16, ts->pid);
        }
        /* Ignore keys that don't apply yet */
        else if (!cets_ecm->next_key_id_flag) {
            for (size_t s = 0; s < cets_ecm->num_states; ++s) {
                cets_ecm_state_t* state = &cets_ecm->states[s];
                uint8_t transport_scrambling_control = state->transport_scrambling_control;
                if (transport_scrambling_control == 0) {
                    g_warning("Segment %s contains CETS ECM with transport_scrambling_control = '00'. That value is "
                        "reserved for unencrypted TS packets.",
                        dash_validator->segment ? dash_validator->segment->file_name : "?");
                    continue;
                }
                /* TODO check number of AU */
                for (gsize i = 0; i < dash_validator->pids->len; ++i) {
                    pid_validator_t* pv = g_ptr_array_index(dash_validator->pids, i);
                    if (g_hash_table_contains(pv->ecm_pids, GINT_TO_POINTER(ts->pid))) {
                        pv->au_for_transport_scrambling_control[transport_scrambling_control] += state->num_au;
                    }
                }
            }
        }
        cets_ecm_free(cets_ecm);
    }

    pid_validator_t* pid_validator = dash_validator_find_pid(ts->pid, dash_validator);
    if (pid_validator == NULL) {
        /* This PID is not registered as part of the main program */
        goto cleanup;
    }

    if (ts->transport_scrambling_control) {
        if (pid_validator->au_for_transport_scrambling_control[ts->transport_scrambling_control]) {
            --pid_validator->au_for_transport_scrambling_control[ts->transport_scrambling_control];
        } else {
            g_critical("DASH Conformance: Segment %s contains TS packet for PID %"PRIu16" with "
                    "transport_scrambling_control = '%d%d', but we have not seen a CETS ECM with that "
                    "transport_scrambling_control value (or we have, but we used all of its 'au' values already). "
                    "6.4.4.3 Content Protection: All information necessary for "
                    "decrypting, or locating information required to decrypt, the encrypted TS packets in a (Sub)Segment "
                    "shall be present before the encrypted packet(s) to which they apply, either in the same (Sub)Segment, "
                    "and/or in the Initialization Segment (if used). As an example, this requires the presence of the ECM "
                    "necessary for decrypting the first encrypted packet of the (Sub)Segment is within the (Sub)Segment "
                    "before such a packet.",
                    (dash_validator->segment ? dash_validator->segment->file_name : "?"), ts->pid,
                    (bool)(ts->transport_scrambling_control & 2), ts->transport_scrambling_control & 1);
            dash_validator->status = 0;
        }
    }

    // This is the first TS packet from this PID
    if (pid_validator->ts_count == 0) {
        pid_validator->continuity_counter = ts->continuity_counter;

        // we ignore non-payload and non-media packets
        if (ts->has_payload && pid_validator->content_component != UNKNOWN_CONTENT_COMPONENT) {
            // if we only have complete PES packets, we must start with PUSI=1 followed by PES header in the first payload-bearing packet
            if ((dash_validator->profile >= DASH_PROFILE_MPEG2TS_MAIN)
                    && (ts->payload_unit_start_indicator == 0)) {
                g_critical("DASH Conformance: media segments shall contain only complete PES packets");
                dash_validator->status = 0;
            }

            // by the time we get to the start of the first PES, we need to have seen at least one PCR.
            if (!PCR_IS_VALID(dash_validator->last_pcr) && dash_validator->adaptation_set->bitstream_switching) {
                g_critical("DASH Conformance: PCR must be present before first bytes of media data. 7.4.3.4 "
                        "Bitstream switching: PCR shall be present in the Segment prior to the first byte of a "
                        "TS packet payload containing media data, and not inferred from the `pcrb` box.");
                dash_validator->status = 0;
            }
        }
    }

    // TODO: check for discontinuities
    //       -> CC errors, declared discontinuities
    //       -> PCR-PCR distances, with some arbitrary tolerance
    // we need to figure out what to do here -- we can let PES parsing fail and get a notice

    pid_validator->ts_count++;
cleanup:
    return;
}

static bool validate_pes_packet_common(pes_packet_t* pes, GArray* ts_packets, dash_validator_t* dash_validator)
{
    g_return_val_if_fail(ts_packets, false);
    g_return_val_if_fail(ts_packets->len > 0, false);
    g_return_val_if_fail(dash_validator, false);

    if (dash_validator->segment_type == INITIALIZATION_SEGMENT
            || dash_validator->segment_type == BITSTREAM_SWITCHING_SEGMENT) {
        bool is_init = dash_validator->segment_type == INITIALIZATION_SEGMENT;
        g_critical("DASH Conformance: PES packet found in %s segment. %s: The concatenation of %s Segment with any "
                "Media Segment shall have the same presentation duration as the original Media Segment.",
                is_init ? "initialization" : "bitstream switching",
                is_init ? "" : "6.4.5 Bitstream Switching Segment",
                is_init ? "an Initialization" : "a Bitstream Switching");
        dash_validator->status = 0;
    }

    if (pes == NULL) {
        ts_packet_t* ts = &g_array_index(ts_packets, ts_packet_t, 0);
        if (ts->adaptation_field.random_access_indicator) {
            g_critical("DASH Conformance: Found partial PES packet starting with a TS packet with "
                    "random_access_indicator = 1. 6.4.2.2 Media stream access points: PES packet starting at I_SAU "
                    "shall contain only an integral number of access units and shall contain a PTS.");
            dash_validator->status = 0;
        }
        goto cleanup;
    }

    if (dash_validator->segment_type == INITIALIZATION_SEGMENT && pes->pts_flag) {
        g_critical("DASH Conformance: PES packet in initialization segment has PTS_DTS_flags set to '%u%u'. "
                "6.4.3.2 says, \"Time-varying initialization information shall not be present in the Initialization "
                "Segment.\"", pes->pts_flag, pes->dts_flag);
        dash_validator->status = 0;
    }
cleanup:
    return dash_validator->status;
}

void validate_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GArray* ts_packets, void* arg)
{
    g_return_if_fail(ts_packets);
    g_return_if_fail(ts_packets->len > 0);
    g_return_if_fail(arg);

    dash_validator_t* dash_validator = arg;
    ts_packet_t* first_ts = &g_array_index(ts_packets, ts_packet_t, 0);
    pid_validator_t* pid_validator = dash_validator_find_pid(first_ts->pid, dash_validator);
    g_return_if_fail(pid_validator);

    if (!validate_pes_packet_common(pes, ts_packets, dash_validator)) {
        dash_validator->status = 0;
    }

    if (pes == NULL) {
        // we have a queue that didn't appear to be a valid PES packet (e.g., because it didn't start with
        // payload_unit_start_indicator = 1)
        if (dash_validator->adaptation_set->segment_alignment.has_int ||
                dash_validator->adaptation_set->segment_alignment.b ||
                dash_validator->adaptation_set->bitstream_switching) {
            g_critical("DASH Conformance: Media segment %s does not contain complete PES packets and "
                    "@segmentAlignment is not 'false'. 7.4.3.2 Segment alignment: If the @segmentAlignment attribute "
                    "is not set to 'false' [...] the Media Segment shall contain only complete PES packets [...] %s",
                    dash_validator->segment->file_name,
                    dash_validator->adaptation_set->bitstream_switching ? "7.4.3.4 Bitstream switching: [...] at "
                    "least the following conditions are satisfied if @bitstreamSwitching flag is set to  'true': The "
                    "conditions required for setting the @segmentAlignment attribute not set to 'false' for the "
                    "Adaptation Set are fulfilled." : "");
            dash_validator->status = 0;
        }
        if (dash_validator->current_subsegment && first_ts->pid == dash_validator->current_subsegment->reference_id) {
            g_critical("DASH Conformance: Media segment %s has an incomplete PES packet for the indexed media stream "
                    "in this subsegment (PID %"PRIu16"). 6.4.2.1. Subsegment: A subsegment shall contain complete "
                    "access units for the indexed media stream (i.e., stream for which reference_ID equals PID), "
                    "however it may contain incomplete PES packets from other media streams.",
                    dash_validator->segment->file_name, first_ts->pid);
            dash_validator->status = 0;
        }
        goto cleanup;
    }

    if (dash_validator->current_subsegment && (dash_validator->adaptation_set->subsegment_alignment.has_int ||
            dash_validator->adaptation_set->subsegment_alignment.b)) {
        ts_packet_t* last_ts = &g_array_index(ts_packets, ts_packet_t, ts_packets->len - 1);
        uint64_t last_ts_end_byte = last_ts->pos_in_stream + 188 /* length of TS packet */; 
        if (first_ts->pos_in_stream < dash_validator->current_subsegment->end_byte
                && last_ts->pos_in_stream >= dash_validator->current_subsegment->end_byte) {
            g_critical("DASH Conformance: TS packet in segment %s spans byte locations %"PRIu64" to %"PRIu64", but "
                    "'sidx' says that there is a subsegment from %"PRIu64" to %"PRIu64". 7.4.3.3 Subsegment alignment: "
                    "If the @subsegmentAlignment flag is not set to 'false', [...]] a Subsegment shall contain only "
                    "complete PES packets [...]", dash_validator->segment->file_name,
                    first_ts->pos_in_stream, last_ts_end_byte, dash_validator->current_subsegment->start_byte,
                    dash_validator->current_subsegment->end_byte);
            dash_validator->status = 0;
        }
    }

    // we are in the first PES packet of a PID
    if (pid_validator->pes_count == 0) {
        if (pes->pts_flag) {
            pid_validator->earliest_playout_time = pes->pts;
            pid_validator->latest_playout_time = pes->pts;
        } else if (dash_validator->adaptation_set->segment_alignment.has_int ||
                dash_validator->adaptation_set->segment_alignment.b ||
                dash_validator->adaptation_set->bitstream_switching) {
            g_critical("DASH Conformance: First PES packet in segment %s does not have PTS and @segmentAlignment is "
                    "not 'false'. 7.4.3.2 Segment alignment: If the @segmentAlignment attribute is not set to 'false' "
                    "[...] the first PES packet shall contain a PTS timestamp.", dash_validator->segment->file_name);
            if (dash_validator->adaptation_set->bitstream_switching) {
                g_critical("7.4.3.4 Bitstream switching: [...] at "
                    "least the following conditions are satisfied if @bitstreamSwitching flag is set to  'true': The "
                    "conditions required for setting the @segmentAlignment attribute not set to 'false' for the "
                    "Adaptation Set are fulfilled.");
            }
            dash_validator->status = 0;
        }

        if (first_ts->adaptation_field.random_access_indicator) {
            pid_validator->sap = 1; // we trust AF by default.
            if (pid_validator->content_component == VIDEO_CONTENT_COMPONENT) {
                int nal_start, nal_end;
                uint8_t* buf = pes->payload;
                size_t len = pes->payload_len;

                // walk the nal units in the PES payload and check to see if they are type 1 or type 5 -- these determine
                // SAP type
                for (size_t i = 0; i < len; i += nal_end) {
                    if (!find_nal_unit(buf + i, len - i, &nal_start, &nal_end) != 0) {
                        break;
                    }
                    h264_stream_t* h = h264_new();
                    read_nal_unit(h, &buf[nal_start + i], nal_end - nal_start);
                    int unit_type = h->nal->nal_unit_type;
                    h264_free(h);
                    if (unit_type == 5) {
                        pid_validator->sap_type = 1;
                        break;
                    } else if (unit_type == 1) {
                        pid_validator->sap_type = 2;
                        break;
                    }
                }
            }
        }
        // TODO: validate in case of ISO/IEC 14496-10 (?)
    }

    if (dash_validator->current_subsegment && dash_validator->current_subsegment->pes_count == 0
            && !pes->pts_flag
            && (dash_validator->adaptation_set->subsegment_alignment.has_int ||
                dash_validator->adaptation_set->subsegment_alignment.b)) {
        g_critical("DASH Conformance: First PES packet in subsegment %zu of %s does not have PTS and "
                "@subsegmentAlignment is not 'false'. 7.4.3.3 Subsegment alignment: If the @subsegmentAlignment flag "
                "is not set to 'false' [...] the first PES packet from each elementary stream shall contain a PTS.",
                dash_validator->subsegment_index, dash_validator->segment->file_name);
        dash_validator->status = 0;
    }

    if (pes->pts_flag) {
        // TODO: account for rollovers and discontinuities
        // frames can come in out of PTS order
        pid_validator->earliest_playout_time = MIN(pid_validator->earliest_playout_time, pes->pts);
        pid_validator->latest_playout_time = MAX(pid_validator->latest_playout_time, pes->pts);
    }

    if (pid_validator->content_component == VIDEO_CONTENT_COMPONENT) {
        /* TODO: Where did this magic number come from? */
        pid_validator->duration = 3000;

        if (dash_validator->current_subsegment && first_ts->adaptation_field.random_access_indicator) {
            // check subsegment location against index file
            dash_validator->current_subsegment->saw_random_access = true;
            // time location
            if (dash_validator->current_subsegment->start_time != pes->pts) {
                g_critical("DASH Conformance: expected subsegment PTS does not match actual.  Expected: %"PRIu64", "
                        "Actual: %"PRIu64, dash_validator->current_subsegment->start_time, pes->pts);
                dash_validator->status = 0;
            }

            // byte location
            if (dash_validator->current_subsegment->start_byte != pes->payload_pos_in_stream) {
                g_critical("DASH Conformance: expected subsegment Byte Location does not match actual.  Expected: "
                        "%"PRIu64", Actual: %"PRIu64"", dash_validator->current_subsegment->start_byte, pes->payload_pos_in_stream);
                dash_validator->status = 0;
            }

            // SAP type
            if (dash_validator->current_subsegment->starts_with_sap
                    && dash_validator->current_subsegment->sap_type != 0
                    && dash_validator->current_subsegment->sap_type != pid_validator->sap_type) {
                g_critical("DASH Conformance: expected subsegment SAP Type does not match actual: expected SAP_type "
                        "= %d, actual SAP_type = %d", dash_validator->current_subsegment->sap_type, 
                        pid_validator->sap_type);
                dash_validator->status = 0;
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
    pid_validator->pes_count++;
    if (dash_validator->current_subsegment) {
        dash_validator->current_subsegment->pes_count++;
    }
    pes_free(pes);
    return;
fail:
    dash_validator->status = 0;
    goto cleanup;
}

static void validate_emsg_pes_packet(pes_packet_t* pes, elementary_stream_info_t* esi, GArray* ts_packets, void* arg)
{
    g_return_if_fail(ts_packets);
    g_return_if_fail(ts_packets->len > 0);
    g_return_if_fail(arg);

    dash_validator_t* dash_validator = arg;
    if (!validate_pes_packet_common(pes, ts_packets, dash_validator)) {
        dash_validator->status = 0;
    }

    ts_packet_t* first_ts = &g_array_index(ts_packets, ts_packet_t, 0);
    if (!first_ts->payload_unit_start_indicator) {
        g_critical("DASH Conformance: First 'emsg' packet (PID = 0x0004) does not have "
                "payload_unit_start_indicator = 1. 5.10.3.3.5 says, \"the transport stream packet carrying the start "
                "of the `emsg` box shall have the payload_unit_start_indicator field set to `1`\".");
        dash_validator->status = 0;
    }

    if (dash_validator->current_subsegment && dash_validator->adaptation_set->bitstream_switching) {
        subsegment_t* subsegment = dash_validator->current_subsegment;
        if (subsegment->ssix_offset_index < subsegment->ssix_offsets->len) {
            ts_packet_t* last_ts = &g_array_index(ts_packets, ts_packet_t, ts_packets->len - 1);
            uint64_t last_ts_end = last_ts->pos_in_stream + TS_SIZE;
            uint64_t next_ssix_offset = g_array_index(subsegment->ssix_offsets, uint64_t,
                    subsegment->ssix_offset_index);
            if (next_ssix_offset >= last_ts_end) {
                g_critical("DASH Conformance: @bitstreamSwitching is true and current subsegment ends at offset "
                        "%"PRIu64", but the current 'emsg' PES packet ends at offset %"PRIu64". 5.10.3.3.5 "
                        "Carriage of the Event Message Box in MPEG-2 TS: If @bitstreamSwitching is set, and "
                        "subsegments are used, a subsegment shall contain only complete `emsg` boxes.",
                        next_ssix_offset, last_ts_end);
                dash_validator->status = 0;
            }
            if (subsegment->ssix_offset_index != 0) {
                uint64_t previous_ssix_offset = g_array_index(subsegment->ssix_offsets, uint64_t,
                    subsegment->ssix_offset_index - 1);
                if (first_ts->pos_in_stream < previous_ssix_offset) {
                    g_critical("DASH Conformance: @bitstreamSwitching is true and current subsegment starts at offset "
                            "%"PRIu64", but the current 'emsg' PES packet started at offset %"PRIu64". 5.10.3.3.5 "
                            "Carriage of the Event Message Box in MPEG-2 TS: If @bitstreamSwitching is set, and "
                            "subsegments are used, a subsegment shall contain only complete `emsg` boxes.",
                            previous_ssix_offset, first_ts->pos_in_stream);
                    dash_validator->status = 0;
                }
            }
        }
    }

    if (first_ts->payload_len < 8) {
        /* Note: The first 8 bytes of a Box are the "size" and "type" fields, so if the payload size is >= 8 bytes,
         *       we can guarantee that the Box.type is in it. We check that the type (and size) are correct in
         *       validate_emsg_msg(). */
        g_critical("DASH Conformance: The first TS packet with 'emsg' data has payload size of %zu bytes, but should "
                "be at least 8 bytes. 5.10.3.3.5 says, \"The complete Box.type field shall be present in this first "
                "packet, and the payload size shall be at least 8 bytes.\".", first_ts->payload_len);
        dash_validator->status = 0;
    }

    for (size_t i = 0; i < ts_packets->len; ++i) {
        ts_packet_t* tsp = &g_array_index(ts_packets, ts_packet_t, i);
        if (tsp->transport_scrambling_control != 0) {
            g_critical("DASH Conformance: EMSG packet transport_scrambling_control was 0x%x but should be 0. From "
                    "\"5.10.3.3.5 Carriage of the Event Message Box in MPEG-2 TS\": \"For any packet with PID value "
                    "of 0x0004 the value of the transport_scrambling_control field shall be set to '00'\".",
                    first_ts->transport_scrambling_control);
            dash_validator->status = 0;
        }
    }

    if (!pes) {
        g_critical("DASH Conformance: 5.10.3.3.5 \"A segment shall contain only complete [emsg] boxes. If "
                "@bitstreamSwitching is set, and subsegments are used, a subsegment shall contain only complete "
                "`emsg` boxes.\"");
        dash_validator->status = 0;
        goto cleanup;
    }

    if (validate_emsg_msg(pes->payload, pes->payload_len, dash_validator->segment->duration) != 0) {
        g_critical("DASH Conformance: validation of EMSG failed");
        dash_validator->status = 0;
    }

cleanup:
    pes_free(pes);
}

#define TS_BUFFER_SIZE 4096 * TS_SIZE

int validate_segment(dash_validator_t* dash_validator, char* file_name, uint64_t byte_range_start,
        uint64_t byte_range_end, dash_validator_t* dash_validator_init)
{
    g_return_val_if_fail(dash_validator, 1);
    g_return_val_if_fail(file_name, 1);

    dash_validator->current_subsegment = dash_validator->has_subsegments ?
            g_ptr_array_index(dash_validator->subsegments, 0) : NULL;
    mpeg2ts_stream_t* m2s = NULL;

    FILE* infile = fopen(file_name, "rb");
    if (infile == NULL) {
        g_critical("Cannot open file %s - %s", file_name, strerror(errno));
        goto fail;
    }

    if (byte_range_start > 0 && fseek(infile, byte_range_start, SEEK_SET)) {
        g_critical("Error seeking to offset %ld in %s - %s", byte_range_start, file_name, strerror(errno));
        goto fail;
    }

    m2s = mpeg2ts_stream_new();

    dash_validator->last_pcr = PCR_INVALID;
    dash_validator->status = 1;
    if (dash_validator->pids->len != 0) {
        g_error("Re-using DASH validator pids!");
        goto fail;
    }

    m2s->pat_processor = pat_processor;
    m2s->cat_processor = cat_processor;
    m2s->arg = dash_validator;

    // Connect handler for DASH EMSG streams
    pes_demux_t* emsg_pd = pes_demux_new(validate_emsg_pes_packet);
    emsg_pd->arg = dash_validator;
    demux_pid_handler_t* emsg_handler = demux_pid_handler_new(pes_demux_process_ts_packet);
    emsg_handler->arg = emsg_pd;
    emsg_handler->arg_destructor = (arg_destructor_t)pes_demux_free;
    m2s->emsg_processor = emsg_handler;

    // hook PES demuxer to the PID processor
    demux_pid_handler_t* ts_validator = demux_pid_handler_new(validate_ts_packet);
    ts_validator->arg = dash_validator;
    m2s->ts_processor = ts_validator;

    // Read TS packets from initialization segment
    for (gsize i = 0; dash_validator_init && i < dash_validator_init->initialization_segment_ts->len; ++i) {
        ts_packet_t* ts = &g_array_index(dash_validator_init->initialization_segment_ts, ts_packet_t, i);
        mpeg2ts_stream_read_ts_packet(m2s, ts);
    }

    long packet_buf_size = 4096;
    uint64_t packets_read = 0;
    long num_packets;
    uint64_t packets_to_read =  UINT64_MAX;
    uint8_t* ts_buf = malloc(TS_SIZE * packet_buf_size);

    if (byte_range_end > 0) {
        packets_to_read = (byte_range_end - byte_range_start) / (uint64_t)TS_SIZE;
    }

    while ((num_packets = fread(ts_buf, TS_SIZE, packet_buf_size, infile)) > 0) {
        for (int i = 0; i < num_packets; i++) {
            ts_packet_t ts;
            if (!ts_read(&ts, ts_buf + i * TS_SIZE, TS_SIZE, packets_read)) {
                g_critical("DASH Conformance: Error parsing TS packet %"PRIo64" in segment %s. %s",
                        packets_read, file_name,
                        dash_validator->segment_type == INITIALIZATION_SEGMENT ? "6.4.3.2 Initialization Segment: An "
                        "Initialization Segment shall be a valid MPEG-2 TS, conforming to ISO/IEC 13818-1."
                        : dash_validator->segment_type == BITSTREAM_SWITCHING_SEGMENT ? "6.4.5 Bitstream Switching "
                        "Segment: A Bitstream Switching Segment shall be a valid MPEG-2 TS, conforming to ISO/IEC "
                        "13818-1."
                        : "6.4.4.2 Basic Media Segment: A Media Segment shall be a valid MPEG-2 TS, conforming to "
                        "ISO/IEC 13818-1.");
                goto fail;
            }
            if (dash_validator->segment_type == INITIALIZATION_SEGMENT) {
                size_t new_i = dash_validator->initialization_segment_ts->len;
                g_array_set_size(dash_validator->initialization_segment_ts, new_i + 1);
                ts_copy(&g_array_index(dash_validator->initialization_segment_ts, ts_packet_t, new_i), &ts);
            }
            mpeg2ts_stream_read_ts_packet(m2s, &ts);
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

cleanup:
    mpeg2ts_stream_free(m2s);
    if (infile) {
        fclose(infile);
    }
    return dash_validator->status != 1;
fail:
    dash_validator->status = 0;
    goto cleanup;
}

bool validate_bitstream_switching(const char* file_names[], uint64_t byte_starts[], uint64_t byte_ends[], size_t len)
{
    g_return_val_if_fail(file_names, false);
    g_return_val_if_fail(byte_starts, false);
    g_return_val_if_fail(byte_ends, false);
    g_return_val_if_fail(len > 0, false);
    for (size_t i = 0; i < len; ++i) {
        g_return_val_if_fail(file_names[i], false);
    }

    bool result = true;

    mpeg2ts_stream_t* m2s = mpeg2ts_stream_new();
    FILE* infile = NULL;
    for (size_t f = 0; f < len; ++f) {
        infile = fopen(file_names[f], "rb");
        if (infile == NULL) {
            g_critical("Cannot open file %s - %s", file_names[f], strerror(errno));
            goto fail;
        }

        if (byte_starts[f] > 0 && fseek(infile, byte_starts[f], SEEK_SET)) {
            g_critical("Error seeking to offset %ld in %s - %s", byte_starts[f], file_names[f], strerror(errno));
            goto fail;
        }

        uint64_t packets_to_read = UINT64_MAX / TS_SIZE;
        if (byte_ends[f] > 0) {
            packets_to_read = (byte_ends[f] - byte_starts[f]) / TS_SIZE;
        }

        size_t packets_read_total = 0;
        uint8_t ts_buf[TS_BUFFER_SIZE];
        size_t packets_read;
        while ((packets_read = fread(ts_buf, TS_SIZE, MIN(TS_BUFFER_SIZE / TS_SIZE, packets_to_read), infile)) > 0) {
            for (size_t i = 0; i < packets_read; i++) {
                ts_packet_t ts;
                if (!ts_read(&ts, ts_buf + i * TS_SIZE, TS_SIZE, packets_read_total)) {
                    goto fail;
                }
                mpeg2ts_stream_read_ts_packet(m2s, &ts);
                packets_read_total++;
            }
            packets_to_read -= packets_read;
        }
    }

    // need to reset the mpeg stream to be sure to process the last PES packet
    mpeg2ts_stream_reset(m2s);

cleanup:
    mpeg2ts_stream_free(m2s);
    if (infile) {
        fclose(infile);
    }
    return result;
fail:
    result = false;
    goto cleanup;
}

int analyze_sidx_references(sidx_t* sidx, int* pnum_subsegments, int* pnum_nested_sidx, dash_profile_t profile)
{
    g_return_val_if_fail(sidx, -1);
    g_return_val_if_fail(pnum_subsegments, -1);
    g_return_val_if_fail(pnum_nested_sidx, -1);

    int originalnum_nested_sidx = *pnum_nested_sidx;
    int originalnum_subsegments = *pnum_subsegments;

    for(int i = 0; i < sidx->reference_count; i++) {
        sidx_reference_t ref = sidx->references[i];
        if (ref.reference_type == 1) {
            (*pnum_nested_sidx)++;
        } else {
            (*pnum_subsegments)++;
        }
    }

    if (profile >= DASH_PROFILE_MPEG2TS_SIMPLE) {
        if (originalnum_nested_sidx != *pnum_nested_sidx && originalnum_subsegments != *pnum_subsegments) {
            // failure -- references contain references to both media and nested sidx boxes
            g_critical("ERROR validating Representation Index Segment: Section 8.7.3: Simple profile requires that \
sidx boxes have either media references or sidx references, but not both.");
            return -1;
        }
    }

    return 0;
}

index_segment_validator_t* validate_index_segment(char* file_name, segment_t* segment_in, representation_t* representation,
        adaptation_set_t* adaptation_set)
{
    g_return_val_if_fail(file_name, NULL);
    g_return_val_if_fail(representation, NULL);
    g_return_val_if_fail(adaptation_set, NULL);

    bool is_single_index = segment_in != NULL;
    g_info("Validating %s Index Segment %s", is_single_index ? "Single" : "Representation", file_name);
    GPtrArray* segments;
    if (is_single_index) {
        segments = g_ptr_array_new();
        g_ptr_array_add(segments, segment_in);
    } else {
        segments = representation->segments;
    }
    size_t num_boxes = 0;
    box_t** boxes = NULL;
    index_segment_validator_t* validator = index_segment_validator_new();

    if (representation->segments->len == 0) {
        g_critical("ERROR validating Index Segment: No segments in representation.");
        goto fail;
    }

    int error = 0;
    boxes = read_boxes_from_file(file_name, &num_boxes, &error);
    if (error) {
        goto fail;
    }
    print_boxes(boxes, num_boxes);

    if (num_boxes == 0) {
        g_critical("ERROR validating Index Segment %s: no boxes in segment.", file_name);
        goto fail;
    }

    size_t box_index = 0;

    bool found_ssss = false;
    if (boxes[box_index]->type != BOX_TYPE_STYP) {
        g_critical("DASH Conformance: First box in index segment %sis not an 'styp'. %s", file_name,
                is_single_index ? "6.4.6.2 Single Index Segment: Each Single Index Segment shall begin with a ‘styp’ "
                "box" : "6.4.6.3 Representation Index Segment: Each Representation Index Segment shall begin with an "
                "‘styp’ box");
        validator->error = true;
    } else {
        styp_t* styp = (styp_t*)boxes[box_index];
        bool found_brand = false;
        uint32_t expected_brand = is_single_index ? BRAND_SISX : BRAND_RISX;
        for(size_t i = 0; i < styp->num_compatible_brands; ++i) {
            uint32_t brand = styp->compatible_brands[i];
            if (brand == expected_brand) {
                found_brand = true;
            } else if (brand == BRAND_SSSS) {
                found_ssss = true;
            }
        }
        if (!found_brand) {
            g_critical("DASH Conformance: 'styp' box in index segment %s does not contain %s as a compatible brand. "
                    "%s", file_name, is_single_index ? "sisx" : "risx",
                    is_single_index ? "6.4.6.2 Single Index Segment: Each Single Index Segment shall begin with a "
                    "‘styp’ box, and the brand ‘sisx’ shall be present in the ‘styp’ box." : "6.4.6.3 Representation "
                    "Index Segment: Each Representation Index Segment shall begin with an ‘styp’ box, and the brand "
                    "‘risx’ shall be present in the ‘styp’ box.");
            g_info("Brands found are:");
            g_info("styp major brand = %x", styp->major_brand);
            for (size_t i = 0; i < styp->num_compatible_brands; ++i) {
                char brand_str[5] = {0};
                uint32_to_string(brand_str, styp->compatible_brands[i]);
                g_info("styp compatible brand = %s", brand_str);
            }
            validator->error = true;
        }
        ++box_index;
    }

    for (size_t i = 0; i < num_boxes; ++i) {
        if (boxes[i]->type != BOX_TYPE_SIDX) {
            continue;
        }
        sidx_t* sidx = (sidx_t*)boxes[i];
        if (sidx->timescale != representation->timescale) {
            g_critical("DASH Conformance: 'sidx' in box %zu of %s has timescale %"PRIu32", but SegmentBase@timescale "
                    "is %"PRIu32". 5.3.9.6 Segment timeline: the value of @timescale shall be identical to the value "
                    "of the timescale field in the first 'sidx' box",
                    i, file_name, sidx->timescale, representation->timescale);
            validator->error = true;
        }
    }

    sidx_t* master_sidx = NULL;
    uint32_t master_reference_id = 0;
    if (!is_single_index) {
        box_t* box = boxes[box_index];
        if (box->type != BOX_TYPE_SIDX) {
            char type_str[5] = {0};
            uint32_to_string(type_str, box->type);
            /* Is this strictly required? Couldn't there be other boxes that come first, and long as they're not
             * 'ssix' or 'pcrb'? */
            g_critical("DASH Conformance: Representation Index Segment %s has box type '%s' following styp, but "
                    "should have an 'sidx'. 6.4.6.3 Representation Index Segment: The Segment Index for each Media "
                    "Segments is concatenated in order, preceded by a single Segment Index box that indexes the "
                    "Index Segment.", file_name, type_str);
            validator->error = true;
        } else {
            // walk all references: they should all be of type 1 and should point to sidx boxes
            master_sidx = (sidx_t*)boxes[box_index];
            master_reference_id = master_sidx->reference_id;
            if (master_reference_id != adaptation_set->video_pid) {
                g_critical("ERROR validating Representation Index Segment: master ref ID does not equal video PID. "
                        "Expected %d, actual %d.", adaptation_set->video_pid, master_reference_id);
                validator->error = true;
            }
            for (size_t i = 0; i < master_sidx->reference_count; i++) {
                sidx_reference_t ref = master_sidx->references[i];
                if (ref.reference_type != 1) {
                    g_critical("DASH Conformance: In Representation Index Segment %s, found reference_type != 1 in "
                            "first 'sidx'. The first 'sidx' should index the representation index itself. 6.4.6.3 "
                            "Representation Index Segment: The Segment Index for each Media Segments is concatenated "
                            "in order, preceded by a single Segment Index box that indexes the Index Segment. This "
                            "initial Segment Index box shall have one entry in its loop for each Media Segment, and "
                            "each entry refers to the Segment Index information for a single Media Segment.",
                            file_name);
                    validator->error = true;
                    /* Check this box as a normal sidx instead */
                    --box_index;
                    break;
                }

                // validate duration
                if (i < segments->len) {
                    segment_t* segment = g_ptr_array_index(representation->segments, i);
                    if (segment->duration != ref.subsegment_duration) {
                        /* Is this a valid test? What if we have more than one sidx per segment? If this is valid,
                         * shouldn't we have an error for when there are too many references? */
                        g_critical("ERROR validating Representation Index Segment: master ref segment duration does not equal "
                                "segment duration.  Expected %"PRIu64", actual %d.", segment->duration, ref.subsegment_duration);
                        validator->error = true;
                    }
                }
            }
            box_index++;
        }
    }

    size_t sidx_start = box_index;
    size_t segment_index = 0;
    bool ssix_present = false;
    bool pcrb_present = false;
    int num_nested_sidx = 0;
    int num_subsegments = 0;
    uint64_t referenced_size = 0;

    // now walk all the boxes, validating that the number of sidx boxes is correct and doing a few other checks
    sidx_t* current_sidx = NULL;
    for (box_index = sidx_start; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        switch(box->type) {
        case BOX_TYPE_SIDX: {
            if (box_index != sidx_start && !ssix_present && representation->subrepresentations->len > 0) {
                g_critical("DASH Conformance: Segment index is missing a 'ssix' box for segment %zu, but there is a "
                        "SubRepresentation present. 7.4.4 Sub-Representations: The Subsegment Index box shall contain "
                        "at least one entry for the value of SubRepresentation@level and for each value provided in "
                        "the SubRepresentation@dependencyLevel.", box_index - sidx_start);
                validator->error = true;
            }
            ssix_present = false;
            pcrb_present = false;

            sidx_t* sidx = (sidx_t*)box;
            current_sidx = sidx;
            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                // GORP: check earliest presentation time
            } else {
                // check size:
                g_debug("Validating referenced_size for segment %zu.", segment_index);
                if (segment_index > 1 && referenced_size != master_sidx->references[segment_index - 1].referenced_size) {
                    g_critical("ERROR validating Representation Index Segment: referenced_size for segment %zu. "
                            "Expected %"PRIu32", actual %"PRIu64"\n", segment_index,
                            master_sidx->references[segment_index - 1].referenced_size, referenced_size);
                    validator->error = true;
                }

                referenced_size = 0;
                if (segment_index >= segments->len) {
                    g_critical("ERROR, representation index has too many 'sidx' boxes.");
                    goto fail;
                }
                segment_t* segment = g_ptr_array_index(segments, segment_index);
                segment_index++;

                g_debug("Validating earliest_presentation_time for segment %zu.", segment_index);
                if (segment->start != sidx->earliest_presentation_time) {
                    g_critical("ERROR validating Representation Index Segment: invalid earliest_presentation_time in "
                            "sidx box. Expected %"PRId64", actual %"PRId64".", segment->start,
                            sidx->earliest_presentation_time);
                    validator->error = true;
                }
            }
            referenced_size += sidx->size;

            g_debug("Validating reference_id");
            if (!is_single_index && master_reference_id != sidx->reference_id) {
                g_critical("ERROR validating Representation Index Segment: invalid reference id in sidx box. "
                        "Expected %d, actual %d.", master_reference_id, sidx->reference_id);
                validator->error = true;
            }

            // count number of subsegments and number of sidx boxes in reference list of this sidx
            if (analyze_sidx_references(sidx, &num_subsegments, &num_nested_sidx, representation->profile) != 0) {
                validator->error = true;
            }
            break;
        }
        case BOX_TYPE_SSIX: {
            ssix_t* ssix = (ssix_t*)box;
            referenced_size += ssix->size;
            g_debug("Validating ssix box");
            if (ssix_present) {
                g_critical("ERROR validating Index Segment: More than one ssix box following sidx box.");
                validator->error = true;
            } else {
                ssix_present = true;
            }
            if (pcrb_present) {
                g_critical("ERROR validating Index Segment: pcrb occurred before ssix. 6.4.6.4 says "
                        "\"The Subsegment Index box (‘ssix’) [...] shall follow immediately after the ‘sidx’ box that "
                        "documents the same Subsegment. [...] If the 'pcrb' box is present, it shall follow 'ssix'.\".");
                validator->error = true;
            }
            if (!found_ssss) {
                g_critical("ERROR validating Index Segment: Saw ssix box, but 'ssss' is not in compatible brands. See 6.4.6.4.");
                validator->error = true;
            }
            if (current_sidx == NULL) {
                g_critical("DASH Conformance: In Index Segment %s, saw an 'ssix' before the first 'sidx'. 6.4.6.4 "
                        "Subsegment Index Segment: The Subsegment Index box ('ssix') shall be present and shall "
                        "follow immediately after the 'sidx' box that documents the same Subsegment.", file_name);
                validator->error = true;
            }
            for (size_t sr = 0; sr < representation->subrepresentations->len; ++sr) {
                subrepresentation_t* subrepresentation = g_ptr_array_index(representation->subrepresentations, sr);
                /* This loop is messy, going over by 1 and then using that overflow to insert subrepresentation->level.
                 * Feel free to make it nicer if you can. */
                for (size_t l = 0; l <= subrepresentation->dependency_level->len; ++l) {
                    uint32_t level = (l == subrepresentation->dependency_level->len) ? subrepresentation->level : \
                        g_array_index(subrepresentation->dependency_level, uint32_t, l);
                    bool found = false;
                    for (uint32_t ss = 0; ss < ssix->subsegment_count && !found; ++ss) {
                        ssix_subsegment_t* subsegment = &ssix->subsegments[ss];
                        for (uint32_t r = 0; r < subsegment->ranges_count && !found; ++r) {
                            ssix_subsegment_range_t* range = &subsegment->ranges[r];
                            if (range->level == level) {
                                found = true;
                            }
                        }
                    }
                    if (!found) {
                        g_critical("DASH Conformance: Index segment %s has 'ssix' which does not contain "
                                "SubRepresentation@%s %"PRIu32". 7.4.4 Sub-Representations: The "
                                "Subsegment Index box shall contain at least one entry for the value of "
                                "SubRepresentation@level and for each value provided in the "
                                "SubRepresentation@dependencyLevel.",
                                file_name, l == subrepresentation->dependency_level->len ? "level" : "dependencyLevel",
                                level);
                        validator->error = true;
                    }
                }
            }
            break;
        }
        case BOX_TYPE_PCRB: {
            pcrb_t* pcrb = (pcrb_t*)box;
            referenced_size += pcrb->size;
            g_info("Validating pcrb box");
            if (pcrb_present) {
                g_critical("ERROR validating Index Segment: More than one pcrb box following sidx box.");
                validator->error = true;
            } else {
                pcrb_present = true;
            }
            break;
        }
        default:
            g_warning("Invalid box type in Index Segment %s: %x.", file_name, box->type);
            break;
        }
    }
    if (!ssix_present && representation->subrepresentations->len > 0) {
        g_critical("DASH Conformance: Segment index is missing a 'ssix' box for segment %zu, but there is a "
                "SubRepresentation present. 7.4.4 Sub-Representations: The Subsegment Index box shall contain "
                "at least one entry for the value of SubRepresentation@level and for each value provided in "
                "the SubRepresentation@dependencyLevel.", box_index - sidx_start);
        validator->error = true;
    }

    // check the last reference size -- the last one is not checked in the above loop
    if (master_sidx && segment_index > 0 && referenced_size != master_sidx->references[segment_index - 1].referenced_size) {
        g_critical("ERROR validating Representation Index Segment: referenced_size for reference %zu. Expected "
                "%"PRIu32", actual %"PRIu64".", segment_index,
                master_sidx->references[segment_index - 1].referenced_size, referenced_size);
        validator->error = true;
    }

    if (num_nested_sidx != 0) {
        g_critical("ERROR validating Index Segment: Incorrect number of nested sidx boxes: %d.",
                num_nested_sidx);
        /* No point build up a list of indexes since we won't understand them */
        goto fail;
    }

    if (segment_index != segments->len) {
        g_critical("ERROR validating Index Segment: Invalid number of segment sidx boxes following master sidx box: "
                "expected %u, found %zu.", segments->len, segment_index);
        /* No point build up a list of indexes since we won't understand them */
        goto fail;
    }

    // fill in subsegment locations by walking the list of sidx's again, starting from the third box
    num_nested_sidx = 0;
    segment_index = 0;
    uint64_t next_subsegment_byte_location = 0;
    uint64_t last_subsegment_start_time = representation->presentation_time_offset;
    uint64_t last_subsegment_duration = 0;
    GPtrArray* subsegments = NULL;
    for (box_index = sidx_start; box_index < num_boxes; ++box_index) {
        box_t* box = boxes[box_index];
        switch (box->type) {
        case BOX_TYPE_SIDX: {
            sidx_t* sidx = (sidx_t*)box;

            if (num_nested_sidx > 0) {
                num_nested_sidx--;
                next_subsegment_byte_location += sidx->first_offset;  // convert from 64-bit t0 32 bit
            } else {
                segment_t* segment = g_ptr_array_index(segments, segment_index);
                if (subsegments) {
                    g_ptr_array_add(validator->segment_subsegments, subsegments);
                }
                subsegments = g_ptr_array_new();
                last_subsegment_start_time = segment->start;
                last_subsegment_duration = 0;
                segment_index++;

                next_subsegment_byte_location = sidx->first_offset;
            }

            // fill in subsegment locations here
            for (size_t i = 0; i < sidx->reference_count; i++) {
                sidx_reference_t ref = sidx->references[i];
                if (ref.reference_type == 0) {
                    subsegment_t* subsegment = subsegment_new();
                    subsegment->reference_id = sidx->reference_id;
                    subsegment->starts_with_sap = ref.starts_with_sap;
                    subsegment->sap_type = ref.sap_type;
                    subsegment->start_byte = next_subsegment_byte_location;
                    subsegment->end_byte = subsegment->start_byte + ref.referenced_size;
                    subsegment->start_time = last_subsegment_start_time + last_subsegment_duration + ref.sap_delta_time;
                    g_ptr_array_add(subsegments, subsegment);

                    last_subsegment_start_time = subsegment->start_time;
                    last_subsegment_duration = ref.subsegment_duration;
                    next_subsegment_byte_location += ref.referenced_size;
                } else {
                    num_nested_sidx++;
                }
            }
            break;
        }
        case BOX_TYPE_SSIX: {
            ssix_t* ssix = (ssix_t*)box;
            if (ssix->subsegment_count != subsegments->len) {
                g_critical("Error: 'ssix' has %"PRIu32" subsegments, but the proceeding 'sidx' box has %u. 8.16.4.3 "
                        "of ISO/IEC 14496-12 says: subsegment_count shall be equal to reference_count (i.e., the "
                        "number of movie fragment references) in the immediately preceding Segment Index box.",
                        ssix->subsegment_count, subsegments->len);
                goto fail;
            }
            for (uint32_t i = 0; i < ssix->subsegment_count; ++i) {
                ssix_subsegment_t* s = &ssix->subsegments[i];
                subsegment_t* subsegment = g_ptr_array_index(subsegments, i);
                uint64_t byte_offset = subsegment->start_byte;
                for (uint32_t j = 0; j < s->ranges_count; ++j) {
                    ssix_subsegment_range_t* range = &s->ranges[j];
                    g_array_append_val(subsegment->ssix_offsets, byte_offset);
                    byte_offset += range->range_size;
                }
            }
            break;
        }
        }
    }
    if (subsegments) {
        g_ptr_array_add(validator->segment_subsegments, subsegments);
    }

cleanup:
    free_boxes(boxes, num_boxes);
    if (is_single_index) {
        g_ptr_array_free(segments, true);
    }
    return validator;
fail:
    validator->error = true;
    goto cleanup;
}

int validate_emsg_msg(uint8_t* buffer, size_t len, unsigned segment_duration)
{
    g_return_val_if_fail(buffer, -1);

    bitreader_new_stack(b, buffer, len);
    size_t num_boxes;
    int error = 0;
    box_t** boxes = read_boxes_from_stream(b, &num_boxes, &error);
    if (error) {
        goto fail;
    }
    print_boxes(boxes, num_boxes);

    for (size_t i = 0; i < num_boxes; i++) {
        emsg_t* box = (emsg_t*)boxes[i];
        if (box->type != BOX_TYPE_EMSG) {
            char tmp[5] = {0};
            uint32_to_string(tmp, box->type);
            g_critical("DASH Conformance: Saw a box with type %s in a PES packet for PID 0x0004, which is reserved "
                    "for 'emsg' boxes. 5.10.3.3.5: \"[...] the packet payload will start with the `emsg` box [...].\"",
                    tmp);
            error = 1;
        }

        if (box->presentation_time_delta + box->event_duration > segment_duration) {
            g_critical("ERROR validating EMSG: event lasts longer tha segment duration.");
            goto fail;
        }
    }

cleanup:
    free_boxes(boxes, num_boxes);
    return !error;
fail:
    error = 1;
    goto cleanup;
}