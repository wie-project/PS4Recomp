// SPDX-License-Identifier: GPL-2.0-or-later
//
// The HEVC picture info that libSceVideodec2 and libSceVdecsw write after a decoded picture
// (SceVideodec2HevcPictureInfo / SceVdecswHevcPictureInfo share this layout; shadPS4
// videodec2.h / vdecsw.h, 0xB8 bytes).
//
// shadPS4 fills only is_valid, the timestamps and the frame_crop offsets. Here every field the
// stream carries is reported: the SPS / VUI / profile_tier_level of the picture's sequence and
// the NAL units of its access unit. Not parsed: the picture timing SEI, so pic_struct and
// duplicate_flag are 0 and source_scan_type is the value D.3.3 infers from
// general_progressive_source_flag / general_interlaced_source_flag. The frame_crop offsets are
// shadPS4's: left / top 0, right = pitch - width, bottom = align(height, 16) - height, in pixels.
#ifndef PS4_HEVC_PICTURE_INFO_H
#define PS4_HEVC_PICTURE_INFO_H

#include "ps4_h264_vt.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct Ps4HevcPictureInfo {
    uint64_t this_size;
    uint8_t is_valid;
    uint64_t pts_data;
    uint64_t dts_data;
    uint64_t attached_data;
    uint32_t pic_width_in_luma_samples;
    uint32_t pic_height_in_luma_samples;
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    uint8_t timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint32_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;
    uint8_t video_signal_type_present_flag;
    uint8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coeffs;
    uint8_t frame_field_info_present_flag;
    uint32_t pic_struct;
    uint32_t source_scan_type;
    uint32_t duplicate_flag;
    uint32_t conformance_window_flag;
    uint32_t conf_win_left_offset;
    uint32_t conf_win_right_offset;
    uint32_t conf_win_top_offset;
    uint32_t conf_win_bottom_offset;
    uint32_t default_display_window_flag;
    uint32_t def_disp_win_left_offset;
    uint32_t def_disp_win_right_offset;
    uint32_t def_disp_win_top_offset;
    uint32_t def_disp_win_bottom_offset;
    uint8_t chroma_loc_info_present_flag;
    uint8_t chroma_sample_loc_type_top_field;
    uint8_t chroma_sample_loc_type_bottom_field;
    uint8_t field_seq_flag;
    uint8_t video_parameter_set_present_flag;
    uint8_t sequence_parameter_set_present_flag;
    uint8_t picture_parameter_set_present_flag;
    uint8_t au_delimiter_present_flag;
    uint8_t end_of_sequence_present_flag;
    uint8_t end_of_stream_present_flag;
    uint8_t filler_data_present_flag;
    uint8_t picture_timing_sei_present_flag;
    uint8_t buffering_period_sei_present_flag;
    uint8_t frame_packing_arrangement_sei_present_flag;
    uint8_t alternative_transfer_characteristics_sei_present_flag;
    uint8_t idr_pictureflag;
    uint8_t irap_picture_flag;
    uint8_t general_profile_space;
    uint8_t general_tier_flag;
    uint8_t general_profile_idc;
    uint8_t general_progressive_source_flag;
    uint8_t general_interlaced_source_flag;
    uint8_t general_frame_only_constraint_flag;
    uint8_t general_level_idc;
    uint8_t sub_layer_profile_present_flag;
    uint8_t sub_layer_level_present_flag;
    uint8_t sub_layer_profile_space;
    uint8_t sub_layer_tier_flag;
    uint8_t sub_layer_profile_idc;
    uint8_t sub_layer_level_idc;
    uint8_t sub_layer_ordering_info_present_flag;
    uint8_t max_dec_pic_buffering_minus1;
    uint8_t preferred_transfer_characteristics;
    uint8_t frame_cropping_flag;
    uint32_t frame_crop_left_offset;
    uint32_t frame_crop_right_offset;
    uint32_t frame_crop_top_offset;
    uint32_t frame_crop_bottom_offset;
} Ps4HevcPictureInfo;
_Static_assert(sizeof(Ps4HevcPictureInfo) == 0xB8, "HevcPictureInfo");
_Static_assert(offsetof(Ps4HevcPictureInfo, pts_data) == 0x10, "HevcPictureInfo.pts");
_Static_assert(offsetof(Ps4HevcPictureInfo, pic_width_in_luma_samples) == 0x28, "HevcPictureInfo.width");
_Static_assert(offsetof(Ps4HevcPictureInfo, pic_struct) == 0x50, "HevcPictureInfo.pic_struct");
_Static_assert(offsetof(Ps4HevcPictureInfo, chroma_loc_info_present_flag) == 0x84, "HevcPictureInfo.chroma_loc");
_Static_assert(offsetof(Ps4HevcPictureInfo, frame_crop_left_offset) == 0xA8, "HevcPictureInfo.crop");

// source_scan_type inferred from the general profile flags (D.3.3): 0 interlaced,
// 1 progressive, 2 unknown.
static inline uint32_t ps4_hevc_source_scan_type(const HevcSpsInfo *s) {
    if (s->general_progressive_source_flag && !s->general_interlaced_source_flag) return 1;
    if (!s->general_progressive_source_flag && s->general_interlaced_source_flag) return 0;
    return 2;
}

static inline void ps4_hevc_fill_picture_info(Ps4HevcPictureInfo *p, const H264VtFrame *f) {
    const HevcSpsInfo *s = &f->hevc;
    const uint32_t pitch = h264vt_pitch(f->width);
    const uint32_t ah = h264vt_aligned_height(f->height);
    memset(p, 0, sizeof(*p));
    p->this_size = sizeof(*p);
    p->is_valid = 1;
    p->pts_data = (uint64_t)f->pts;
    p->dts_data = (uint64_t)f->dts;
    p->attached_data = f->attached;
    p->pic_width_in_luma_samples = s->pic_width_in_luma_samples;
    p->pic_height_in_luma_samples = s->pic_height_in_luma_samples;
    p->bit_depth_luma_minus8 = s->bit_depth_luma_minus8;
    p->bit_depth_chroma_minus8 = s->bit_depth_chroma_minus8;
    p->timing_info_present_flag = s->timing_info_present_flag;
    p->num_units_in_tick = s->num_units_in_tick;
    p->time_scale = s->time_scale;
    p->aspect_ratio_info_present_flag = s->aspect_ratio_info_present_flag;
    p->aspect_ratio_idc = s->aspect_ratio_idc;
    p->sar_width = s->sar_width;
    p->sar_height = s->sar_height;
    p->video_signal_type_present_flag = s->video_signal_type_present_flag;
    p->video_format = s->video_format;
    p->video_full_range_flag = s->video_full_range_flag;
    p->colour_description_present_flag = s->colour_description_present_flag;
    p->colour_primaries = s->colour_primaries;
    p->transfer_characteristics = s->transfer_characteristics;
    p->matrix_coeffs = s->matrix_coeffs;
    p->frame_field_info_present_flag = s->frame_field_info_present_flag;
    p->source_scan_type = ps4_hevc_source_scan_type(s);
    p->conformance_window_flag = s->conformance_window_flag;
    p->conf_win_left_offset = s->conf_win_left_offset;
    p->conf_win_right_offset = s->conf_win_right_offset;
    p->conf_win_top_offset = s->conf_win_top_offset;
    p->conf_win_bottom_offset = s->conf_win_bottom_offset;
    p->default_display_window_flag = s->default_display_window_flag;
    p->def_disp_win_left_offset = s->def_disp_win_left_offset;
    p->def_disp_win_right_offset = s->def_disp_win_right_offset;
    p->def_disp_win_top_offset = s->def_disp_win_top_offset;
    p->def_disp_win_bottom_offset = s->def_disp_win_bottom_offset;
    p->chroma_loc_info_present_flag = s->chroma_loc_info_present_flag;
    p->chroma_sample_loc_type_top_field = s->chroma_sample_loc_type_top_field;
    p->chroma_sample_loc_type_bottom_field = s->chroma_sample_loc_type_bottom_field;
    p->field_seq_flag = s->field_seq_flag;
    p->video_parameter_set_present_flag = f->au.vps;
    p->sequence_parameter_set_present_flag = f->au.sps;
    p->picture_parameter_set_present_flag = f->au.pps;
    p->au_delimiter_present_flag = f->au.aud;
    p->end_of_sequence_present_flag = f->au.end_of_sequence;
    p->end_of_stream_present_flag = f->au.end_of_stream;
    p->filler_data_present_flag = f->au.filler;
    p->picture_timing_sei_present_flag = f->au.sei_pic_timing;
    p->buffering_period_sei_present_flag = f->au.sei_buffering_period;
    p->frame_packing_arrangement_sei_present_flag = f->au.sei_frame_packing;
    p->alternative_transfer_characteristics_sei_present_flag = f->au.sei_alternative_transfer;
    p->idr_pictureflag = f->au.idr;
    p->irap_picture_flag = f->au.irap;
    p->general_profile_space = s->general_profile_space;
    p->general_tier_flag = s->general_tier_flag;
    p->general_profile_idc = s->general_profile_idc;
    p->general_progressive_source_flag = s->general_progressive_source_flag;
    p->general_interlaced_source_flag = s->general_interlaced_source_flag;
    p->general_frame_only_constraint_flag = s->general_frame_only_constraint_flag;
    p->general_level_idc = s->general_level_idc;
    p->sub_layer_profile_present_flag = s->sub_layer_profile_present_flag;
    p->sub_layer_level_present_flag = s->sub_layer_level_present_flag;
    p->sub_layer_profile_space = s->sub_layer_profile_space;
    p->sub_layer_tier_flag = s->sub_layer_tier_flag;
    p->sub_layer_profile_idc = s->sub_layer_profile_idc;
    p->sub_layer_level_idc = s->sub_layer_level_idc;
    p->sub_layer_ordering_info_present_flag = s->sub_layer_ordering_info_present_flag;
    p->max_dec_pic_buffering_minus1 = s->max_dec_pic_buffering_minus1;
    p->preferred_transfer_characteristics = f->au.preferred_transfer_characteristics;
    p->frame_crop_right_offset = pitch - f->width;
    p->frame_crop_bottom_offset = ah - f->height;
    p->frame_cropping_flag = (p->frame_crop_right_offset || p->frame_crop_bottom_offset) ? 1 : 0;
}

#endif // PS4_HEVC_PICTURE_INFO_H
