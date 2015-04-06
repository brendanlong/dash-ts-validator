/*
 Copyright (c) 2015-, ISO/IEC JTC1/SC29/WG11
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
#include <check.h>
#include <stdlib.h>

#include "mpd.h"
#include "test_common.h"

START_TEST(test_full_mpd)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011' profiles='urn:mpeg:dash:profile:full:2011' minBufferTime='PT1.5S'> \
                <Period duration='PT30S'> \
                    <BaseURL>ad/</BaseURL> \
                    <AdaptationSet mimeType='video/mp2t'> \
                        <Representation id='720p' bandwidth='3200000' width='1280' height='720'> \
                            <BaseURL>720p.ts</BaseURL> \
                            <SegmentBase> \
                                <RepresentationIndex sourceURL='720p.sidx'/> \
                            </SegmentBase> \
                        </Representation> \
                        <Representation id='1080p' bandwidth='6800000' width='1920' height='1080'> \
                            <BaseURL>1080p.ts</BaseURL> \
                            <SegmentBase> \
                                <RepresentationIndex sourceURL='1080p.sidx'/> \
                            </SegmentBase> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
                <Period duration='PT5M'> \
                    <BaseURL>main/</BaseURL> \
                    <AdaptationSet mimeType='video/mp2t'> \
                        <BaseURL>video/</BaseURL> \
                        <Representation id='720p' bandwidth='3200000' width='1280' height='720'> \
                            <BaseURL>720p/</BaseURL> \
                            <SegmentList timescale='90000' duration='5400000'> \
                                <RepresentationIndex sourceURL='representation-index.sidx'/> \
                                <SegmentURL media='segment-1.ts'/> \
                                <SegmentURL media='segment-2.ts'/> \
                                <SegmentURL media='segment-3.ts'/> \
                                <SegmentURL media='segment-4.ts'/> \
                                <SegmentURL media='segment-5.ts'/> \
                                <SegmentURL media='segment-6.ts'/> \
                                <SegmentURL media='segment-7.ts'/> \
                                <SegmentURL media='segment-8.ts'/> \
                                <SegmentURL media='segment-9.ts'/> \
                                <SegmentURL media='segment-10.ts'/> \
                            </SegmentList> \
                        </Representation> \
                        <Representation id='1080p' bandwidth='6800000' width='1920' height='1080'> \
                            <BaseURL>1080p/</BaseURL> \
                            <SegmentTemplate media='segment-$Number$.ts' timescale='90000'> \
                                <RepresentationIndex sourceURL='representation-index.sidx'/> \
                                <SegmentTimeline> \
                                    <S t='0' r='10' d='5400000'/> \
                                </SegmentTimeline> \
                            </SegmentTemplate> \
                        </Representation> \
                    </AdaptationSet> \
                    <AdaptationSet mimeType='audio/mp2t'> \
                        <BaseURL>audio/</BaseURL> \
                        <Representation id='audio' bandwidth='128000'> \
                            <SegmentTemplate media='segment-$Number$.ts' timescale='90000'> \
                                <RepresentationIndex sourceURL='representation-index.sidx'/> \
                                <SegmentTimeline> \
                                    <S t='0' r='10' d='5400000'/> \
                                </SegmentTimeline> \
                            </SegmentTemplate> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");

    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->profile, DASH_PROFILE_FULL);
    ck_assert_int_eq(mpd->presentation_type, MPD_PRESENTATION_STATIC);
    ck_assert_uint_eq(mpd->duration, 0);
    ck_assert_int_eq(mpd->periods->len, 2);

    if (mpd->periods->len > 0) {
        period_t* period = g_ptr_array_index(mpd->periods, 0);
        ck_assert_ptr_ne(period, NULL);
        ck_assert_ptr_eq(period->mpd, mpd);
        ck_assert(!period->bitstream_switching);
        ck_assert_uint_eq(period->duration, 30);
        ck_assert_int_eq(period->adaptation_sets->len, 1);

        if (period->adaptation_sets->len > 0) {
            adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
            ck_assert_ptr_ne(set, NULL);
            ck_assert_ptr_eq(set->period, period);
            ck_assert_str_eq(set->mime_type, "video/mp2t");
            ck_assert_int_eq(set->representations->len, 2);

            ck_assert_uint_eq(set->id, 0);
            ck_assert_int_eq(set->profile, DASH_PROFILE_FULL);
            ck_assert_uint_eq(set->audio_pid, 0);
            ck_assert_uint_eq(set->video_pid, 0);
            ck_assert(!set->segment_alignment.has_int && !set->segment_alignment.b);
            ck_assert(!set->subsegment_alignment.has_int && !set->subsegment_alignment.b);
            ck_assert(!set->bitstream_switching);

            if (set->representations->len > 0) {
                representation_t* representation = g_ptr_array_index(set->representations, 0);
                ck_assert_ptr_ne(representation, NULL);
                ck_assert_ptr_eq(representation->adaptation_set, set);
                ck_assert_str_eq(representation->id, "720p");
                ck_assert_uint_eq(representation->bandwidth, 3200000);
                ck_assert_str_eq(representation->index_file_name, "/ad/720p.sidx");
                ck_assert_int_eq(representation->segments->len, 1);

                ck_assert_int_eq(representation->profile, DASH_PROFILE_FULL);
                ck_assert_ptr_eq(representation->mime_type, NULL);
                ck_assert_uint_eq(representation->index_range_start, 0);
                ck_assert_uint_eq(representation->index_range_end, 0);
                ck_assert_ptr_eq(representation->initialization_file_name, NULL);
                ck_assert_uint_eq(representation->initialization_range_start, 0);
                ck_assert_uint_eq(representation->initialization_range_end, 0);
                ck_assert_ptr_eq(representation->bitstream_switching_file_name, NULL);
                ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
                ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
                ck_assert_uint_eq(representation->start_with_sap, 0);
                ck_assert_uint_eq(representation->presentation_time_offset, 0);
                ck_assert_uint_eq(representation->timescale, 1);
                ck_assert_int_eq(representation->subrepresentations->len, 0);

                if (representation->segments->len > 0) {
                    segment_t* segment = g_ptr_array_index(representation->segments, 0);
                    ck_assert_ptr_ne(segment, NULL);
                    ck_assert_ptr_eq(segment->representation, representation);
                    ck_assert_str_eq(segment->file_name, "/ad/720p.ts");

                    ck_assert_uint_eq(segment->media_range_start, 0);
                    ck_assert_uint_eq(segment->media_range_end, 0);
                    ck_assert_uint_eq(segment->start, 0);
                    ck_assert_uint_eq(segment->duration, 2700000);
                    ck_assert_uint_eq(segment->end, 2700000);
                    ck_assert_ptr_eq(segment->index_file_name, NULL);
                    ck_assert_uint_eq(segment->index_range_start, 0);
                    ck_assert_uint_eq(segment->index_range_end, 0);
                }
            }

            if (set->representations->len > 1) {
                representation_t* representation = g_ptr_array_index(set->representations, 1);
                ck_assert_ptr_ne(representation, NULL);
                ck_assert_ptr_eq(representation->adaptation_set, set);
                ck_assert_str_eq(representation->id, "1080p");
                ck_assert_uint_eq(representation->bandwidth, 6800000);
                ck_assert_str_eq(representation->index_file_name, "/ad/1080p.sidx");
                ck_assert_int_eq(representation->segments->len, 1);

                ck_assert_int_eq(representation->profile, DASH_PROFILE_FULL);
                ck_assert_ptr_eq(representation->mime_type, NULL);
                ck_assert_uint_eq(representation->index_range_start, 0);
                ck_assert_uint_eq(representation->index_range_end, 0);
                ck_assert_ptr_eq(representation->initialization_file_name, NULL);
                ck_assert_uint_eq(representation->initialization_range_start, 0);
                ck_assert_uint_eq(representation->initialization_range_end, 0);
                ck_assert_ptr_eq(representation->bitstream_switching_file_name, NULL);
                ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
                ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
                ck_assert_uint_eq(representation->start_with_sap, 0);
                ck_assert_uint_eq(representation->presentation_time_offset, 0);
                ck_assert_uint_eq(representation->timescale, 1);
                ck_assert_int_eq(representation->subrepresentations->len, 0);

                if (representation->segments->len > 0) {
                    segment_t* segment = g_ptr_array_index(representation->segments, 0);
                    ck_assert_ptr_ne(segment, NULL);
                    ck_assert_ptr_eq(segment->representation, representation);
                    ck_assert_str_eq(segment->file_name, "/ad/1080p.ts");

                    ck_assert_uint_eq(segment->media_range_start, 0);
                    ck_assert_uint_eq(segment->media_range_end, 0);
                    ck_assert_uint_eq(segment->start, 0);
                    ck_assert_uint_eq(segment->duration, 2700000);
                    ck_assert_uint_eq(segment->end, 2700000);
                    ck_assert_ptr_eq(segment->index_file_name, NULL);
                    ck_assert_uint_eq(segment->index_range_start, 0);
                    ck_assert_uint_eq(segment->index_range_end, 0);
                }
            }
        }
    }

    if (mpd->periods->len > 1) {
        period_t* period = g_ptr_array_index(mpd->periods, 1);
        ck_assert_ptr_ne(period, NULL);
        ck_assert_ptr_eq(period->mpd, mpd);
        ck_assert(!period->bitstream_switching);
        ck_assert_uint_eq(period->duration, 300);
        ck_assert_int_eq(period->adaptation_sets->len, 2);

        if (period->adaptation_sets->len > 0) {
            adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
            ck_assert_ptr_ne(set, NULL);
            ck_assert_ptr_eq(set->period, period);
            ck_assert_str_eq(set->mime_type, "video/mp2t");
            ck_assert_int_eq(set->representations->len, 2);

            ck_assert_uint_eq(set->id, 0);
            ck_assert_int_eq(set->profile, DASH_PROFILE_FULL);
            ck_assert_uint_eq(set->audio_pid, 0);
            ck_assert_uint_eq(set->video_pid, 0);
            ck_assert(!set->segment_alignment.has_int && !set->segment_alignment.b);
            ck_assert(!set->subsegment_alignment.has_int && !set->subsegment_alignment.b);
            ck_assert(!set->bitstream_switching);

            if (set->representations->len > 0) {
                representation_t* representation = g_ptr_array_index(set->representations, 0);
                ck_assert_ptr_ne(representation, NULL);
                ck_assert_ptr_eq(representation->adaptation_set, set);
                ck_assert_str_eq(representation->id, "720p");
                ck_assert_uint_eq(representation->bandwidth, 3200000);
                ck_assert_str_eq(representation->index_file_name, "/main/video/720p/representation-index.sidx");
                ck_assert_int_eq(representation->segments->len, 10);

                ck_assert_int_eq(representation->profile, DASH_PROFILE_FULL);
                ck_assert_ptr_eq(representation->mime_type, NULL);
                ck_assert_uint_eq(representation->index_range_start, 0);
                ck_assert_uint_eq(representation->index_range_end, 0);
                ck_assert_ptr_eq(representation->initialization_file_name, NULL);
                ck_assert_uint_eq(representation->initialization_range_start, 0);
                ck_assert_uint_eq(representation->initialization_range_end, 0);
                ck_assert_ptr_eq(representation->bitstream_switching_file_name, NULL);
                ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
                ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
                ck_assert_uint_eq(representation->start_with_sap, 0);
                ck_assert_uint_eq(representation->presentation_time_offset, 0);
                ck_assert_uint_eq(representation->timescale, 90000);
                ck_assert_int_eq(representation->subrepresentations->len, 0);

                for (size_t i = 0; i < representation->segments->len; ++i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, i);
                    ck_assert_ptr_ne(segment, NULL);
                    ck_assert_ptr_eq(segment->representation, representation);
                    char* segment_file_name = g_strdup_printf("/main/video/%s/segment-%zu.ts", representation->id,
                            i + 1);
                    ck_assert_str_eq(segment->file_name, segment_file_name);
                    g_free(segment_file_name);

                    ck_assert_uint_eq(segment->media_range_start, 0);
                    ck_assert_uint_eq(segment->media_range_end, 0);
                    ck_assert_uint_eq(segment->start, i * (60 * 90000));
                    ck_assert_uint_eq(segment->duration, 60 * 90000);
                    ck_assert_uint_eq(segment->end, (i + 1) * (60 * 90000));
                    ck_assert_ptr_eq(segment->index_file_name, NULL);
                    ck_assert_uint_eq(segment->index_range_start, 0);
                    ck_assert_uint_eq(segment->index_range_end, 0);
                }
            }

            if (set->representations->len > 1) {
                representation_t* representation = g_ptr_array_index(set->representations, 1);
                ck_assert_ptr_ne(representation, NULL);
                ck_assert_ptr_eq(representation->adaptation_set, set);
                ck_assert_str_eq(representation->id, "1080p");
                ck_assert_uint_eq(representation->bandwidth, 6800000);
                ck_assert_str_eq(representation->index_file_name, "/main/video/1080p/representation-index.sidx");
                ck_assert_int_eq(representation->segments->len, 10);

                ck_assert_int_eq(representation->profile, DASH_PROFILE_FULL);
                ck_assert_ptr_eq(representation->mime_type, NULL);
                ck_assert_uint_eq(representation->index_range_start, 0);
                ck_assert_uint_eq(representation->index_range_end, 0);
                ck_assert_ptr_eq(representation->initialization_file_name, NULL);
                ck_assert_uint_eq(representation->initialization_range_start, 0);
                ck_assert_uint_eq(representation->initialization_range_end, 0);
                ck_assert_ptr_eq(representation->bitstream_switching_file_name, NULL);
                ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
                ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
                ck_assert_uint_eq(representation->start_with_sap, 0);
                ck_assert_uint_eq(representation->presentation_time_offset, 0);
                ck_assert_uint_eq(representation->timescale, 90000);
                ck_assert_int_eq(representation->subrepresentations->len, 0);

                for (size_t i = 0; i < representation->segments->len; ++i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, i);
                    ck_assert_ptr_ne(segment, NULL);
                    ck_assert_ptr_eq(segment->representation, representation);
                    char* segment_file_name = g_strdup_printf("/main/video/%s/segment-%zu.ts", representation->id,
                            i + 1);
                    ck_assert_str_eq(segment->file_name, segment_file_name);
                    g_free(segment_file_name);

                    ck_assert_uint_eq(segment->media_range_start, 0);
                    ck_assert_uint_eq(segment->media_range_end, 0);
                    ck_assert_uint_eq(segment->start, i * (60 * 90000));
                    ck_assert_uint_eq(segment->duration, 60 * 90000);
                    ck_assert_uint_eq(segment->end, (i + 1) * (60 * 90000));
                    ck_assert_ptr_eq(segment->index_file_name, NULL);
                    ck_assert_uint_eq(segment->index_range_start, 0);
                    ck_assert_uint_eq(segment->index_range_end, 0);
                }
            }
        }

        if (period->adaptation_sets->len > 1) {
            adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 1);
            ck_assert_ptr_ne(set, NULL);
            ck_assert_ptr_eq(set->period, period);
            ck_assert_str_eq(set->mime_type, "audio/mp2t");
            ck_assert_int_eq(set->representations->len, 1);

            ck_assert_uint_eq(set->id, 0);
            ck_assert_int_eq(set->profile, DASH_PROFILE_FULL);
            ck_assert_uint_eq(set->audio_pid, 0);
            ck_assert_uint_eq(set->video_pid, 0);
            ck_assert(!set->segment_alignment.has_int && !set->segment_alignment.b);
            ck_assert(!set->subsegment_alignment.has_int && !set->subsegment_alignment.b);
            ck_assert(!set->bitstream_switching);

            if (set->representations->len > 0) {
                representation_t* representation = g_ptr_array_index(set->representations, 0);
                ck_assert_ptr_ne(representation, NULL);
                ck_assert_ptr_eq(representation->adaptation_set, set);
                ck_assert_str_eq(representation->id, "audio");
                ck_assert_uint_eq(representation->bandwidth, 128000);
                ck_assert_str_eq(representation->index_file_name, "/main/audio/representation-index.sidx");
                ck_assert_int_eq(representation->segments->len, 10);

                ck_assert_int_eq(representation->profile, DASH_PROFILE_FULL);
                ck_assert_ptr_eq(representation->mime_type, NULL);
                ck_assert_uint_eq(representation->index_range_start, 0);
                ck_assert_uint_eq(representation->index_range_end, 0);
                ck_assert_ptr_eq(representation->initialization_file_name, NULL);
                ck_assert_uint_eq(representation->initialization_range_start, 0);
                ck_assert_uint_eq(representation->initialization_range_end, 0);
                ck_assert_ptr_eq(representation->bitstream_switching_file_name, NULL);
                ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
                ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
                ck_assert_uint_eq(representation->start_with_sap, 0);
                ck_assert_uint_eq(representation->presentation_time_offset, 0);
                ck_assert_uint_eq(representation->timescale, 90000);
                ck_assert_int_eq(representation->subrepresentations->len, 0);

                for (size_t i = 0; i < representation->segments->len; ++i) {
                    segment_t* segment = g_ptr_array_index(representation->segments, i);
                    ck_assert_ptr_ne(segment, NULL);
                    ck_assert_ptr_eq(segment->representation, representation);
                    char* segment_file_name = g_strdup_printf("/main/audio/segment-%zu.ts", i + 1);
                    ck_assert_str_eq(segment->file_name, segment_file_name);
                    g_free(segment_file_name);

                    ck_assert_uint_eq(segment->media_range_start, 0);
                    ck_assert_uint_eq(segment->media_range_end, 0);
                    ck_assert_uint_eq(segment->start, i * (60 * 90000));
                    ck_assert_uint_eq(segment->duration, 60 * 90000);
                    ck_assert_uint_eq(segment->end, (i + 1) * (60 * 90000));
                    ck_assert_ptr_eq(segment->index_file_name, NULL);
                    ck_assert_uint_eq(segment->index_range_start, 0);
                    ck_assert_uint_eq(segment->index_range_end, 0);
                }
            }
        }
    }

    mpd_free(mpd);
END_TEST

START_TEST(test_mpd)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011' profiles='urn:mpeg:dash:profile:mp2t-simple:2011' \
                type='dynamic' mediaPresentationDuration='PT4H20M34.20S' minBufferTime='PT1.5S'/>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");

    ck_assert_ptr_ne(mpd, NULL);

    ck_assert_int_eq(mpd->profile, DASH_PROFILE_MPEG2TS_SIMPLE);
    ck_assert_int_eq(mpd->presentation_type, MPD_PRESENTATION_DYNAMIC);
    ck_assert_uint_eq(mpd->duration, 15634);
    ck_assert_int_eq(mpd->periods->len, 0);

    mpd_free(mpd);
END_TEST

START_TEST(test_period)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011' profiles='urn:mpeg:dash:profile:full:2011' \
                minBufferTime='PT1.5S'> \
                <Period duration='PT42S' bitstreamSwitching='true'> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");

    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);

    ck_assert_ptr_ne(period, NULL);
    ck_assert_ptr_eq(period->mpd, mpd);
    ck_assert_uint_eq(period->duration, 42);
    ck_assert(period->bitstream_switching);
    ck_assert_int_eq(period->adaptation_sets->len, 0);

    mpd_free(mpd);
END_TEST

START_TEST(test_adaptation_set)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period> \
                    <AdaptationSet id='55' mimeType='audio/mp4' segmentAlignment='5' subsegmentAlignment='true' \
                        profiles='urn:mpeg:dash:profile:full:2011,urn:mpeg:dash:profile:mp2t-main:2011' \
                        bitstreamSwitching='true'> \
                        <ContentComponent id='123' contentType='audio'/> \
                        <ContentComponent id='444' contentType='video'/> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);

    ck_assert_ptr_ne(set, NULL);
    ck_assert_ptr_eq(set->period, period);
    ck_assert_uint_eq(set->id, 55);
    ck_assert_str_eq(set->mime_type, "audio/mp4");
    ck_assert_int_eq(set->profile, DASH_PROFILE_MPEG2TS_MAIN);
    ck_assert_uint_eq(set->audio_pid, 123);
    ck_assert_uint_eq(set->video_pid, 444);
    ck_assert(set->segment_alignment.has_int);
    ck_assert_uint_eq(set->segment_alignment.i, 5);
    ck_assert(!set->subsegment_alignment.has_int);
    ck_assert(set->subsegment_alignment.b);
    ck_assert(set->bitstream_switching);
    ck_assert_int_eq(set->representations->len, 0);

    mpd_free(mpd);
END_TEST

START_TEST(test_representation)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period> \
                    <AdaptationSet> \
                        <Representation id='asdf' mimeType='video/mp2t' startWithSAP='4' bandwidth='409940' \
                            profiles='urn:mpeg:dash:profile:mp2t-main:2011, urn:mpeg:dash:profile:full:2011'> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_ptr_eq(representation->adaptation_set, set);
    ck_assert_str_eq(representation->id, "asdf");
    ck_assert_int_eq(representation->profile, DASH_PROFILE_MPEG2TS_MAIN);
    ck_assert_str_eq(representation->mime_type, "video/mp2t");
    ck_assert_ptr_eq(representation->index_file_name, NULL);
    ck_assert_uint_eq(representation->index_range_start, 0);
    ck_assert_uint_eq(representation->index_range_end, 0);
    ck_assert_ptr_eq(representation->initialization_file_name, NULL);
    ck_assert_uint_eq(representation->initialization_range_start, 0);
    ck_assert_uint_eq(representation->initialization_range_end, 0);
    ck_assert_ptr_eq(representation->bitstream_switching_file_name, NULL);
    ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
    ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
    ck_assert_uint_eq(representation->start_with_sap, 4);
    ck_assert_uint_eq(representation->presentation_time_offset, 0);
    ck_assert_uint_eq(representation->bandwidth, 409940);
    ck_assert_uint_eq(representation->timescale, 1);
    ck_assert_int_eq(representation->subrepresentations->len, 0);
    ck_assert_int_eq(representation->segments->len, 0);

    mpd_free(mpd);
END_TEST

START_TEST(test_subrepresentation)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period> \
                    <AdaptationSet> \
                        <Representation> \
                            <SubRepresentation startWithSAP='5' level='4' bandwidth='80983' \
                                dependencyLevel='1 234\t999999' contentComponent='256 5\ta' \
                                profiles='urn:mpeg:dash:profile:mp2t-simple:2011'> \
                            </SubRepresentation> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->subrepresentations->len, 1);

    subrepresentation_t* subrepresentation = g_ptr_array_index(representation->subrepresentations, 0);
    ck_assert_ptr_ne(subrepresentation, NULL);
    ck_assert_ptr_eq(subrepresentation->representation, representation);
    ck_assert_int_eq(subrepresentation->profile, DASH_PROFILE_MPEG2TS_SIMPLE);
    ck_assert_uint_eq(subrepresentation->start_with_sap, 5);
    ck_assert(subrepresentation->has_level);
    ck_assert_uint_eq(subrepresentation->level, 4);
    ck_assert_uint_eq(subrepresentation->bandwidth, 80983);

    uint32_t dependency_levels[] = {1, 234, 999999};
    assert_arrays_eq(ck_assert_uint_eq, (uint32_t*)subrepresentation->dependency_level->data,
            subrepresentation->dependency_level->len, dependency_levels, 3);

    char* content_components[] = {"256", "5", "a"};
    assert_arrays_eq(ck_assert_str_eq, subrepresentation->content_component->pdata,
            subrepresentation->content_component->len, content_components, 3);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_base_in_representation)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <BaseURL>period/</BaseURL> \
                    <AdaptationSet> \
                        <BaseURL>set/</BaseURL> \
                        <Representation> \
                            <BaseURL>rep/segment.ts</BaseURL> \
                            <SegmentBase timescale='12' presentationTimeOffset='528' indexRange='32-74'> \
                                <Initialization sourceURL='subfolder/init.ts' range='4092-302409' /> \
                                <RepresentationIndex sourceURL='index.sidx' range='9938-178933' /> \
                            </SegmentBase> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 1);

    ck_assert_str_eq(representation->index_file_name, "/period/set/rep/index.sidx");
    ck_assert_uint_eq(representation->index_range_start, 9938);
    ck_assert_uint_eq(representation->index_range_end, 178933);
    ck_assert_str_eq(representation->initialization_file_name, "/period/set/rep/subfolder/init.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 4092);
    ck_assert_uint_eq(representation->initialization_range_end, 302409);
    ck_assert_uint_eq(representation->timescale, 12);
    ck_assert_uint_eq(representation->presentation_time_offset, 3960000);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment.ts");
    ck_assert_uint_eq(segment->start, representation->presentation_time_offset);
    ck_assert_uint_eq(segment->duration, 34 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_base_in_adaptation_set)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <BaseURL>period/</BaseURL> \
                    <SegmentBase timescale='12' presentationTimeOffset='528' indexRange='32-74'> \
                        <Initialization sourceURL='subfolder/init.ts' range='4092-302409' /> \
                        <RepresentationIndex sourceURL='index.sidx' range='9938-178933' /> \
                    </SegmentBase> \
                    <AdaptationSet> \
                        <BaseURL>set/</BaseURL> \
                        <Representation> \
                            <BaseURL>rep/segment.ts</BaseURL> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 1);

    ck_assert_str_eq(representation->index_file_name, "/period/set/rep/index.sidx");
    ck_assert_uint_eq(representation->index_range_start, 9938);
    ck_assert_uint_eq(representation->index_range_end, 178933);
    ck_assert_str_eq(representation->initialization_file_name, "/period/set/rep/subfolder/init.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 4092);
    ck_assert_uint_eq(representation->initialization_range_end, 302409);
    ck_assert_uint_eq(representation->timescale, 12);
    ck_assert_uint_eq(representation->presentation_time_offset, 3960000);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment.ts");
    ck_assert_uint_eq(segment->start, representation->presentation_time_offset);
    ck_assert_uint_eq(segment->duration, 34 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_base_in_period)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <BaseURL>period/</BaseURL> \
                    <SegmentBase timescale='12' presentationTimeOffset='528' indexRange='32-74'> \
                        <Initialization sourceURL='subfolder/init.ts' range='4092-302409' /> \
                        <RepresentationIndex sourceURL='index.sidx' range='9938-178933' /> \
                    </SegmentBase> \
                    <AdaptationSet> \
                        <BaseURL>set/</BaseURL> \
                        <Representation> \
                            <BaseURL>rep/segment.ts</BaseURL> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 1);

    ck_assert_str_eq(representation->index_file_name, "/period/set/rep/index.sidx");
    ck_assert_uint_eq(representation->index_range_start, 9938);
    ck_assert_uint_eq(representation->index_range_end, 178933);
    ck_assert_str_eq(representation->initialization_file_name, "/period/set/rep/subfolder/init.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 4092);
    ck_assert_uint_eq(representation->initialization_range_end, 302409);
    ck_assert_uint_eq(representation->timescale, 12);
    ck_assert_uint_eq(representation->presentation_time_offset, 3960000);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment.ts");
    ck_assert_uint_eq(segment->start, representation->presentation_time_offset);
    ck_assert_uint_eq(segment->duration, 34 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_list_in_representation)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <BaseURL>period/</BaseURL> \
                    <AdaptationSet> \
                        <BaseURL>set/ignorethis</BaseURL> \
                        <Representation> \
                            <BaseURL>rep/segment.ts</BaseURL> \
                            <SegmentList timescale='9' presentationTimeOffset='27' indexRange='32-74' duration='18'> \
                                <Initialization sourceURL='subfolder/init.ts' range='14092-3032409' /> \
                                <RepresentationIndex sourceURL='index.sidx' range='99238-1789433' /> \
                                <BitstreamSwitching sourceURL='bitswitch.ts' range='234-3248' /> \
                                <SegmentURL media='s1.ts' mediaRange='2-309' index='s1.sidx' indexRange='290-9292' /> \
                                <SegmentURL media='segment-2.ts' mediaRange='3-339' indexRange='3290-39292' /> \
                                <SegmentURL media='segment-3.ts' /> \
                            </SegmentList> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 3);

    ck_assert_str_eq(representation->index_file_name, "/period/set/rep/index.sidx");
    ck_assert_uint_eq(representation->index_range_start, 99238);
    ck_assert_uint_eq(representation->index_range_end, 1789433);
    ck_assert_str_eq(representation->initialization_file_name, "/period/set/rep/subfolder/init.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 14092);
    ck_assert_uint_eq(representation->initialization_range_end, 3032409);
    ck_assert_uint_eq(representation->timescale, 9);
    ck_assert_uint_eq(representation->presentation_time_offset, 270000);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/s1.ts");
    ck_assert_uint_eq(segment->media_range_start, 2);
    ck_assert_uint_eq(segment->media_range_end, 309);
    ck_assert_uint_eq(segment->start, 270000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/period/set/rep/s1.sidx");
    ck_assert_uint_eq(segment->index_range_start, 290);
    ck_assert_uint_eq(segment->index_range_end, 9292);

    segment = g_ptr_array_index(representation->segments, 1);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment-2.ts");
    ck_assert_uint_eq(segment->media_range_start, 3);
    ck_assert_uint_eq(segment->media_range_end, 339);
    ck_assert_uint_eq(segment->start, 450000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 3290);
    ck_assert_uint_eq(segment->index_range_end, 39292);

    segment = g_ptr_array_index(representation->segments, 2);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment-3.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 4 * 90000 + 270000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_list_in_adaptation_set)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <BaseURL>period/</BaseURL> \
                    <AdaptationSet> \
                        <SegmentList timescale='9' presentationTimeOffset='27' indexRange='32-74' duration='18'> \
                            <Initialization sourceURL='subfolder/init.ts' range='14092-3032409' /> \
                            <RepresentationIndex sourceURL='index.sidx' range='99238-1789433' /> \
                            <BitstreamSwitching sourceURL='bitswitch.ts' range='234-3248' /> \
                            <SegmentURL media='s1.ts' mediaRange='2-309' index='s1.sidx' indexRange='290-9292' /> \
                            <SegmentURL media='segment-2.ts' mediaRange='3-339' indexRange='3290-39292' /> \
                            <SegmentURL media='segment-3.ts' /> \
                        </SegmentList> \
                        <BaseURL>set/ignorethis</BaseURL> \
                        <Representation> \
                            <BaseURL>rep/segment.ts</BaseURL> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 3);

    ck_assert_str_eq(representation->index_file_name, "/period/set/rep/index.sidx");
    ck_assert_uint_eq(representation->index_range_start, 99238);
    ck_assert_uint_eq(representation->index_range_end, 1789433);
    ck_assert_str_eq(representation->initialization_file_name, "/period/set/rep/subfolder/init.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 14092);
    ck_assert_uint_eq(representation->initialization_range_end, 3032409);
    ck_assert_uint_eq(representation->timescale, 9);
    ck_assert_uint_eq(representation->presentation_time_offset, 270000);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/s1.ts");
    ck_assert_uint_eq(segment->media_range_start, 2);
    ck_assert_uint_eq(segment->media_range_end, 309);
    ck_assert_uint_eq(segment->start, 270000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/period/set/rep/s1.sidx");
    ck_assert_uint_eq(segment->index_range_start, 290);
    ck_assert_uint_eq(segment->index_range_end, 9292);

    segment = g_ptr_array_index(representation->segments, 1);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment-2.ts");
    ck_assert_uint_eq(segment->media_range_start, 3);
    ck_assert_uint_eq(segment->media_range_end, 339);
    ck_assert_uint_eq(segment->start, 450000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 3290);
    ck_assert_uint_eq(segment->index_range_end, 39292);

    segment = g_ptr_array_index(representation->segments, 2);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment-3.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 4 * 90000 + 270000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_list_in_period)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <BaseURL>period/</BaseURL> \
                    <SegmentList timescale='9' presentationTimeOffset='27' indexRange='32-74' duration='18'> \
                        <Initialization sourceURL='subfolder/init.ts' range='14092-3032409' /> \
                        <RepresentationIndex sourceURL='index.sidx' range='99238-1789433' /> \
                        <BitstreamSwitching sourceURL='bitswitch.ts' range='234-3248' /> \
                        <SegmentURL media='s1.ts' mediaRange='2-309' index='s1.sidx' indexRange='290-9292' /> \
                        <SegmentURL media='segment-2.ts' mediaRange='3-339' indexRange='3290-39292' /> \
                        <SegmentURL media='segment-3.ts' /> \
                    </SegmentList> \
                    <AdaptationSet> \
                        <BaseURL>set/ignorethis</BaseURL> \
                        <Representation> \
                            <BaseURL>rep/segment.ts</BaseURL> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 3);

    ck_assert_str_eq(representation->index_file_name, "/period/set/rep/index.sidx");
    ck_assert_uint_eq(representation->index_range_start, 99238);
    ck_assert_uint_eq(representation->index_range_end, 1789433);
    ck_assert_str_eq(representation->initialization_file_name, "/period/set/rep/subfolder/init.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 14092);
    ck_assert_uint_eq(representation->initialization_range_end, 3032409);
    ck_assert_uint_eq(representation->timescale, 9);
    ck_assert_uint_eq(representation->presentation_time_offset, 270000);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/s1.ts");
    ck_assert_uint_eq(segment->media_range_start, 2);
    ck_assert_uint_eq(segment->media_range_end, 309);
    ck_assert_uint_eq(segment->start, 270000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/period/set/rep/s1.sidx");
    ck_assert_uint_eq(segment->index_range_start, 290);
    ck_assert_uint_eq(segment->index_range_end, 9292);

    segment = g_ptr_array_index(representation->segments, 1);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment-2.ts");
    ck_assert_uint_eq(segment->media_range_start, 3);
    ck_assert_uint_eq(segment->media_range_end, 339);
    ck_assert_uint_eq(segment->start, 450000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 3290);
    ck_assert_uint_eq(segment->index_range_end, 39292);

    segment = g_ptr_array_index(representation->segments, 2);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/period/set/rep/segment-3.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 4 * 90000 + 270000);
    ck_assert_uint_eq(segment->duration, 2 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, segment->file_name);
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_template_in_representation)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <AdaptationSet> \
                        <Representation id='REP-asdf' bandwidth='7838'> \
                            <BaseURL>rep/asdf.ts</BaseURL> \
                            <SegmentTemplate media='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.ts' \
                                    index='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.sidx' \
                                    bitstreamSwitching='bs-$$-$Bandwidth$-$RepresentationID$.ts' \
                                    initialization='init-$$-$Bandwidth$-$RepresentationID$.ts' \
                                    timescale='5' presentationTimeOffset='25' indexRange='32-74' \
                                    duration='50'> \
                            </SegmentTemplate> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 4);

    ck_assert_uint_eq(representation->timescale, 5);
    ck_assert_uint_eq(representation->presentation_time_offset, 450000);
    ck_assert_ptr_ne(representation->bitstream_switching_file_name, NULL);
    ck_assert_str_eq(representation->bitstream_switching_file_name, "/rep/bs-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
    ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
    ck_assert_ptr_ne(representation->initialization_file_name, NULL);
    ck_assert_str_eq(representation->initialization_file_name, "/rep/init-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 0);
    ck_assert_uint_eq(representation->initialization_range_end, 0);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-1-7838-REP-asdf-25-25.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 450000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-1-7838-REP-asdf-25-25.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 1);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-2-7838-REP-asdf-75-75.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 1350000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-2-7838-REP-asdf-75-75.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 2);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-3-7838-REP-asdf-125-125.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-3-7838-REP-asdf-125-125.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 3);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-4-7838-REP-asdf-175-175.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000 + 10 * 90000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-4-7838-REP-asdf-175-175.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_template_in_adaptation_set)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <AdaptationSet> \
                        <SegmentTemplate media='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.ts' \
                                index='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.sidx' \
                                bitstreamSwitching='bs-$$-$Bandwidth$-$RepresentationID$.ts' \
                                initialization='init-$$-$Bandwidth$-$RepresentationID$.ts' \
                                timescale='5' presentationTimeOffset='25' indexRange='32-74' \
                                duration='50'> \
                        </SegmentTemplate> \
                        <Representation id='REP-asdf' bandwidth='7838'> \
                            <BaseURL>rep/asdf.ts</BaseURL> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 4);

    ck_assert_uint_eq(representation->timescale, 5);
    ck_assert_uint_eq(representation->presentation_time_offset, 450000);
    ck_assert_ptr_ne(representation->bitstream_switching_file_name, NULL);
    ck_assert_str_eq(representation->bitstream_switching_file_name, "/rep/bs-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
    ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
    ck_assert_ptr_ne(representation->initialization_file_name, NULL);
    ck_assert_str_eq(representation->initialization_file_name, "/rep/init-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 0);
    ck_assert_uint_eq(representation->initialization_range_end, 0);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-1-7838-REP-asdf-25-25.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 450000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-1-7838-REP-asdf-25-25.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 1);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-2-7838-REP-asdf-75-75.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 1350000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-2-7838-REP-asdf-75-75.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 2);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-3-7838-REP-asdf-125-125.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-3-7838-REP-asdf-125-125.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 3);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-4-7838-REP-asdf-175-175.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000 + 10 * 90000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-4-7838-REP-asdf-175-175.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_template_in_period)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <SegmentTemplate media='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.ts' \
                            index='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.sidx' \
                            bitstreamSwitching='bs-$$-$Bandwidth$-$RepresentationID$.ts' \
                            initialization='init-$$-$Bandwidth$-$RepresentationID$.ts' \
                            timescale='5' presentationTimeOffset='25' indexRange='32-74' \
                            duration='50'> \
                    </SegmentTemplate> \
                    <AdaptationSet> \
                        <Representation id='REP-asdf' bandwidth='7838'> \
                            <BaseURL>rep/asdf.ts</BaseURL> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 4);

    ck_assert_uint_eq(representation->timescale, 5);
    ck_assert_uint_eq(representation->presentation_time_offset, 450000);
    ck_assert_ptr_ne(representation->bitstream_switching_file_name, NULL);
    ck_assert_str_eq(representation->bitstream_switching_file_name, "/rep/bs-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
    ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
    ck_assert_ptr_ne(representation->initialization_file_name, NULL);
    ck_assert_str_eq(representation->initialization_file_name, "/rep/init-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 0);
    ck_assert_uint_eq(representation->initialization_range_end, 0);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-1-7838-REP-asdf-25-25.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 450000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-1-7838-REP-asdf-25-25.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 1);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-2-7838-REP-asdf-75-75.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 1350000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-2-7838-REP-asdf-75-75.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 2);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-3-7838-REP-asdf-125-125.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-3-7838-REP-asdf-125-125.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 3);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-4-7838-REP-asdf-175-175.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000 + 10 * 90000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-4-7838-REP-asdf-175-175.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_template_mixed_levels)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <SegmentTemplate startNumber='8' timescale='5' presentationTimeOffset='25' indexRange='32-74' \
                            bitstreamSwitching='bs-$$-$Bandwidth$-$RepresentationID$.ts'/> \
                    <AdaptationSet> \
                        <SegmentBase> \
                            <SegmentTimeline> \
                                <S d='50' r='3' /> \
                                <S d='5' /> \
                            </SegmentTimeline> \
                        </SegmentBase> \
                        <Representation id='REP-asdf' bandwidth='7838'> \
                            <SegmentTemplate media='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.ts' \
                                    index='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.sidx' \
                                    initialization='init-$$-$Bandwidth$-$RepresentationID$.ts'> \
                            </SegmentTemplate> \
                            <BaseURL>rep/asdf.ts</BaseURL> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 4);

    ck_assert_uint_eq(representation->timescale, 5);
    ck_assert_uint_eq(representation->presentation_time_offset, 450000);
    ck_assert_ptr_ne(representation->bitstream_switching_file_name, NULL);
    ck_assert_str_eq(representation->bitstream_switching_file_name, "/rep/bs-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
    ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
    ck_assert_ptr_ne(representation->initialization_file_name, NULL);
    ck_assert_str_eq(representation->initialization_file_name, "/rep/init-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 0);
    ck_assert_uint_eq(representation->initialization_range_end, 0);
    ck_assert_uint_eq(representation->start_number, 8);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-8-7838-REP-asdf-25-25.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 450000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-8-7838-REP-asdf-25-25.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 1);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_uint_eq(segment->start, 1350000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_str_eq(segment->file_name, "/rep/$-9-7838-REP-asdf-75-75.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-9-7838-REP-asdf-75-75.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 2);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-10-7838-REP-asdf-125-125.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-10-7838-REP-asdf-125-125.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 3);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-11-7838-REP-asdf-175-175.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000 + 10 * 90000);
    ck_assert_uint_eq(segment->duration, 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-11-7838-REP-asdf-175-175.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

START_TEST(test_segment_template_s_negative_r)
    char* xml_doc = "<?xml version='1.0'?> \
            <MPD xmlns='urn:mpeg:dash:schema:mpd:2011'> \
                <Period duration='PT34S'> \
                    <SegmentTemplate startNumber='8' timescale='5' presentationTimeOffset='25' indexRange='32-74' \
                            bitstreamSwitching='bs-$$-$Bandwidth$-$RepresentationID$.ts'/> \
                    <AdaptationSet> \
                        <SegmentBase> \
                            <SegmentTimeline> \
                                <S d='50' r='-1' /> \
                                <S d='5' /> \
                            </SegmentTimeline> \
                        </SegmentBase> \
                        <Representation id='REP-asdf' bandwidth='7838'> \
                            <SegmentTemplate media='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.ts' \
                                    index='$$-$Number$-$Bandwidth$-$RepresentationID$-$Time$-$Time$.sidx' \
                                    initialization='init-$$-$Bandwidth$-$RepresentationID$.ts'> \
                            </SegmentTemplate> \
                            <BaseURL>rep/asdf.ts</BaseURL> \
                        </Representation> \
                    </AdaptationSet> \
                </Period> \
            </MPD>";
    mpd_t* mpd = mpd_read_doc(xml_doc, "/");
    ck_assert_ptr_ne(mpd, NULL);
    ck_assert_int_eq(mpd->periods->len, 1);

    period_t* period = g_ptr_array_index(mpd->periods, 0);
    ck_assert_ptr_ne(period, NULL);
    ck_assert_int_eq(period->adaptation_sets->len, 1);

    adaptation_set_t* set = g_ptr_array_index(period->adaptation_sets, 0);
    ck_assert_ptr_ne(set, NULL);
    ck_assert_int_eq(set->representations->len, 1);

    representation_t* representation = g_ptr_array_index(set->representations, 0);
    ck_assert_ptr_ne(representation, NULL);
    ck_assert_int_eq(representation->segments->len, 4);

    ck_assert_uint_eq(representation->timescale, 5);
    ck_assert_uint_eq(representation->presentation_time_offset, 450000);
    ck_assert_ptr_ne(representation->bitstream_switching_file_name, NULL);
    ck_assert_str_eq(representation->bitstream_switching_file_name, "/rep/bs-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->bitstream_switching_range_start, 0);
    ck_assert_uint_eq(representation->bitstream_switching_range_end, 0);
    ck_assert_ptr_ne(representation->initialization_file_name, NULL);
    ck_assert_str_eq(representation->initialization_file_name, "/rep/init-$-7838-REP-asdf.ts");
    ck_assert_uint_eq(representation->initialization_range_start, 0);
    ck_assert_uint_eq(representation->initialization_range_end, 0);
    ck_assert_uint_eq(representation->start_number, 8);

    segment_t* segment = g_ptr_array_index(representation->segments, 0);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-8-7838-REP-asdf-25-25.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 450000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-8-7838-REP-asdf-25-25.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 1);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_uint_eq(segment->start, 1350000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_str_eq(segment->file_name, "/rep/$-9-7838-REP-asdf-75-75.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-9-7838-REP-asdf-75-75.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 2);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-10-7838-REP-asdf-125-125.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000);
    ck_assert_uint_eq(segment->duration, 10 * 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-10-7838-REP-asdf-125-125.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    segment = g_ptr_array_index(representation->segments, 3);
    ck_assert_ptr_ne(segment, NULL);
    ck_assert_ptr_eq(segment->representation, representation);
    ck_assert_str_eq(segment->file_name, "/rep/$-11-7838-REP-asdf-175-175.ts");
    ck_assert_uint_eq(segment->media_range_start, 0);
    ck_assert_uint_eq(segment->media_range_end, 0);
    ck_assert_uint_eq(segment->start, 2250000 + 10 * 90000);
    ck_assert_uint_eq(segment->duration, 90000);
    ck_assert_uint_eq(segment->end, segment->start + segment->duration);
    ck_assert_str_eq(segment->index_file_name, "/rep/$-11-7838-REP-asdf-175-175.sidx");
    ck_assert_uint_eq(segment->index_range_start, 32);
    ck_assert_uint_eq(segment->index_range_end, 74);

    mpd_free(mpd);
END_TEST

Suite *suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("MPD");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_full_mpd);
    tcase_add_test(tc_core, test_mpd);
    tcase_add_test(tc_core, test_period);
    tcase_add_test(tc_core, test_adaptation_set);
    tcase_add_test(tc_core, test_representation);
    tcase_add_test(tc_core, test_subrepresentation);
    tcase_add_test(tc_core, test_segment_base_in_representation);
    tcase_add_test(tc_core, test_segment_base_in_adaptation_set);
    tcase_add_test(tc_core, test_segment_base_in_period);
    tcase_add_test(tc_core, test_segment_list_in_representation);
    tcase_add_test(tc_core, test_segment_list_in_adaptation_set);
    tcase_add_test(tc_core, test_segment_list_in_period);
    tcase_add_test(tc_core, test_segment_template_in_representation);
    tcase_add_test(tc_core, test_segment_template_in_adaptation_set);
    tcase_add_test(tc_core, test_segment_template_in_period);
    tcase_add_test(tc_core, test_segment_template_mixed_levels);
    tcase_add_test(tc_core, test_segment_template_s_negative_r);

    suite_add_tcase(s, tc_core);

    return s;
}