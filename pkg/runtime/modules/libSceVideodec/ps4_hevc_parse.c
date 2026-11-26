// SPDX-License-Identifier: GPL-2.0-or-later
//
// HEVC parameter set and slice header parsing (see ps4_hevc.h).
#include "ps4_hevc.h"
#include "ps4_vt_bitstream.h"

#include <stdlib.h>
#include <string.h>

#define HEVC_MAX_SUB_LAYERS 7
#define HEVC_MAX_SHORT_TERM_RPS 64

// The 88 bits of a (sub-layer) profile description after the space / tier / idc fields:
// 32 compatibility flags, progressive / interlaced / non-packed / frame-only, 43 + 1 bits.
static void ptl_flags(BitReader *b, uint8_t *progressive, uint8_t *interlaced, uint8_t *frame_only) {
    br_skip(b, 32);                     // profile_compatibility_flag[32]
    *progressive = (uint8_t)br_u(b, 1); // progressive_source_flag
    *interlaced = (uint8_t)br_u(b, 1);  // interlaced_source_flag
    br_u(b, 1);                         // non_packed_constraint_flag
    *frame_only = (uint8_t)br_u(b, 1);  // frame_only_constraint_flag
    br_skip(b, 43 + 1);                 // constraint / reserved bits, inbld_flag or reserved
}

// profile_tier_level(1, max_sub_layers_minus1) (7.3.3).
static void parse_ptl(BitReader *b, HevcSpsInfo *s) {
    s->general_profile_space = (uint8_t)br_u(b, 2);
    s->general_tier_flag = (uint8_t)br_u(b, 1);
    s->general_profile_idc = (uint8_t)br_u(b, 5);
    ptl_flags(b, &s->general_progressive_source_flag, &s->general_interlaced_source_flag,
              &s->general_frame_only_constraint_flag);
    s->general_level_idc = (uint8_t)br_u(b, 8);
    const unsigned n = s->max_sub_layers_minus1;
    uint8_t profile_present[HEVC_MAX_SUB_LAYERS] = {0}, level_present[HEVC_MAX_SUB_LAYERS] = {0};
    for (unsigned i = 0; i < n; i++) {
        profile_present[i] = (uint8_t)br_u(b, 1);
        level_present[i] = (uint8_t)br_u(b, 1);
    }
    if (n > 0)
        for (unsigned i = n; i < 8; i++) br_u(b, 2); // reserved_zero_2bits
    for (unsigned i = 0; i < n; i++) {
        uint8_t space = 0, tier = 0, idc = 0, level = 0, unused;
        if (profile_present[i]) {
            space = (uint8_t)br_u(b, 2);
            tier = (uint8_t)br_u(b, 1);
            idc = (uint8_t)br_u(b, 5);
            ptl_flags(b, &unused, &unused, &unused);
        }
        if (level_present[i]) level = (uint8_t)br_u(b, 8);
        if (i == 0) {
            s->sub_layer_profile_present_flag = profile_present[0];
            s->sub_layer_level_present_flag = level_present[0];
            s->sub_layer_profile_space = space;
            s->sub_layer_tier_flag = tier;
            s->sub_layer_profile_idc = idc;
            s->sub_layer_level_idc = level;
        }
    }
}

// scaling_list_data() (7.3.4), skipped.
static void skip_scaling_list_data(BitReader *b) {
    for (int size_id = 0; size_id < 4; size_id++) {
        for (int matrix_id = 0; matrix_id < 6 && !b->overrun; matrix_id += size_id == 3 ? 3 : 1) {
            if (!br_u(b, 1)) { // scaling_list_pred_mode_flag
                br_ue(b);      // scaling_list_pred_matrix_id_delta
                continue;
            }
            const int coefs = 1 << (4 + (size_id << 1)) < 64 ? 1 << (4 + (size_id << 1)) : 64;
            if (size_id > 1) br_se(b); // scaling_list_dc_coef_minus8
            for (int i = 0; i < coefs && !b->overrun; i++) br_se(b); // scaling_list_delta_coef
        }
    }
}

// st_ref_pic_set(idx) of an SPS (7.3.7); num_delta_pocs[] holds NumDeltaPocs of the earlier sets.
static void skip_st_ref_pic_set(BitReader *b, unsigned idx, uint32_t *num_delta_pocs) {
    if (idx != 0 && br_u(b, 1)) { // inter_ref_pic_set_prediction_flag
        // In an SPS delta_idx_minus1 is absent: the reference set is the previous one.
        br_u(b, 1); // delta_rps_sign
        br_ue(b);   // abs_delta_rps_minus1
        const uint32_t ref = num_delta_pocs[idx - 1];
        uint32_t count = 0;
        for (uint32_t j = 0; j <= ref && !b->overrun; j++) {
            const uint32_t used = br_u(b, 1); // used_by_curr_pic_flag
            const uint32_t use_delta = used ? 1 : br_u(b, 1);
            if (used || use_delta) count++;
        }
        num_delta_pocs[idx] = count;
        return;
    }
    const uint32_t neg = br_ue(b); // num_negative_pics
    const uint32_t pos = br_ue(b); // num_positive_pics
    if (neg > 16 || pos > 16) {
        b->overrun = true;
        return;
    }
    for (uint32_t i = 0; i < neg + pos && !b->overrun; i++) {
        br_ue(b);   // delta_poc_s0/s1_minus1
        br_u(b, 1); // used_by_curr_pic_s0/s1_flag
    }
    num_delta_pocs[idx] = neg + pos;
}

// vui_parameters() (E.2.1) up to the timing information.
static void parse_vui(BitReader *b, HevcSpsInfo *s) {
    s->aspect_ratio_info_present_flag = (uint8_t)br_u(b, 1);
    if (s->aspect_ratio_info_present_flag) {
        s->aspect_ratio_idc = (uint8_t)br_u(b, 8);
        if (s->aspect_ratio_idc == 255) { // EXTENDED_SAR
            s->sar_width = (uint16_t)br_u(b, 16);
            s->sar_height = (uint16_t)br_u(b, 16);
        }
    }
    if (br_u(b, 1)) br_u(b, 1); // overscan_info_present_flag, overscan_appropriate_flag
    s->video_signal_type_present_flag = (uint8_t)br_u(b, 1);
    if (s->video_signal_type_present_flag) {
        s->video_format = (uint8_t)br_u(b, 3);
        s->video_full_range_flag = (uint8_t)br_u(b, 1);
        s->colour_description_present_flag = (uint8_t)br_u(b, 1);
        if (s->colour_description_present_flag) {
            s->colour_primaries = (uint8_t)br_u(b, 8);
            s->transfer_characteristics = (uint8_t)br_u(b, 8);
            s->matrix_coeffs = (uint8_t)br_u(b, 8);
        }
    }
    s->chroma_loc_info_present_flag = (uint8_t)br_u(b, 1);
    if (s->chroma_loc_info_present_flag) {
        s->chroma_sample_loc_type_top_field = (uint8_t)br_ue(b);
        s->chroma_sample_loc_type_bottom_field = (uint8_t)br_ue(b);
    }
    br_u(b, 1); // neutral_chroma_indication_flag
    s->field_seq_flag = (uint8_t)br_u(b, 1);
    s->frame_field_info_present_flag = (uint8_t)br_u(b, 1);
    s->default_display_window_flag = (uint8_t)br_u(b, 1);
    if (s->default_display_window_flag) {
        s->def_disp_win_left_offset = br_ue(b);
        s->def_disp_win_right_offset = br_ue(b);
        s->def_disp_win_top_offset = br_ue(b);
        s->def_disp_win_bottom_offset = br_ue(b);
    }
    s->timing_info_present_flag = (uint8_t)br_u(b, 1);
    if (s->timing_info_present_flag) {
        s->num_units_in_tick = br_u(b, 32);
        s->time_scale = br_u(b, 32);
    }
}

bool hevc_parse_sps(const uint8_t *nal, size_t size, HevcSpsInfo *out) {
    memset(out, 0, sizeof(*out));
    if (size < 4 || hevc_nal_type(nal) != HEVC_NAL_SPS) return false;
    uint8_t *rbsp = (uint8_t *)malloc(size);
    if (!rbsp) return false;
    BitReader br = {rbsp, nal_to_rbsp(nal + 2, size - 2, rbsp) * 8, 0, false};
    BitReader *b = &br;
    HevcSpsInfo s = {0};
    br_u(b, 4); // sps_video_parameter_set_id
    s.max_sub_layers_minus1 = (uint8_t)br_u(b, 3);
    if (s.max_sub_layers_minus1 >= HEVC_MAX_SUB_LAYERS) b->overrun = true;
    br_u(b, 1); // sps_temporal_id_nesting_flag
    if (!b->overrun) parse_ptl(b, &s);
    br_ue(b); // sps_seq_parameter_set_id
    s.chroma_format_idc = (uint8_t)br_ue(b);
    if (s.chroma_format_idc > 3) b->overrun = true;
    if (s.chroma_format_idc == 3) s.separate_colour_plane_flag = (uint8_t)br_u(b, 1);
    s.pic_width_in_luma_samples = br_ue(b);
    s.pic_height_in_luma_samples = br_ue(b);
    s.conformance_window_flag = (uint8_t)br_u(b, 1);
    if (s.conformance_window_flag) {
        s.conf_win_left_offset = br_ue(b);
        s.conf_win_right_offset = br_ue(b);
        s.conf_win_top_offset = br_ue(b);
        s.conf_win_bottom_offset = br_ue(b);
    }
    s.bit_depth_luma_minus8 = (uint8_t)br_ue(b);
    s.bit_depth_chroma_minus8 = (uint8_t)br_ue(b);
    const uint32_t log2_lsb = br_ue(b) + 4;
    if (log2_lsb > 16) b->overrun = true;
    s.log2_max_pic_order_cnt_lsb = (uint8_t)log2_lsb;
    s.sub_layer_ordering_info_present_flag = (uint8_t)br_u(b, 1);
    for (unsigned i = s.sub_layer_ordering_info_present_flag ? 0 : s.max_sub_layers_minus1;
         i <= s.max_sub_layers_minus1 && !b->overrun; i++) {
        const uint32_t dpb = br_ue(b);     // sps_max_dec_pic_buffering_minus1
        const uint32_t reorder = br_ue(b); // sps_max_num_reorder_pics
        br_ue(b);                          // sps_max_latency_increase_plus1
        if (dpb > 15 || reorder > dpb) b->overrun = true;
        s.max_dec_pic_buffering_minus1 = (uint8_t)dpb;
        s.max_num_reorder_pics = (uint8_t)reorder;
    }
    br_ue(b); // log2_min_luma_coding_block_size_minus3
    br_ue(b); // log2_diff_max_min_luma_coding_block_size
    br_ue(b); // log2_min_luma_transform_block_size_minus2
    br_ue(b); // log2_diff_max_min_luma_transform_block_size
    br_ue(b); // max_transform_hierarchy_depth_inter
    br_ue(b); // max_transform_hierarchy_depth_intra
    if (br_u(b, 1) && br_u(b, 1)) skip_scaling_list_data(b); // scaling_list_enabled, sps_scaling_list_data_present
    br_u(b, 1); // amp_enabled_flag
    br_u(b, 1); // sample_adaptive_offset_enabled_flag
    if (br_u(b, 1)) { // pcm_enabled_flag
        br_u(b, 4); // pcm_sample_bit_depth_luma_minus1
        br_u(b, 4); // pcm_sample_bit_depth_chroma_minus1
        br_ue(b);   // log2_min_pcm_luma_coding_block_size_minus3
        br_ue(b);   // log2_diff_max_min_pcm_luma_coding_block_size
        br_u(b, 1); // pcm_loop_filter_disabled_flag
    }
    const uint32_t num_rps = br_ue(b); // num_short_term_ref_pic_sets
    if (num_rps > HEVC_MAX_SHORT_TERM_RPS) b->overrun = true;
    uint32_t num_delta_pocs[HEVC_MAX_SHORT_TERM_RPS] = {0};
    for (uint32_t i = 0; i < num_rps && !b->overrun; i++) skip_st_ref_pic_set(b, i, num_delta_pocs);
    if (br_u(b, 1)) { // long_term_ref_pics_present_flag
        const uint32_t n = br_ue(b); // num_long_term_ref_pics_sps
        if (n > 32) b->overrun = true;
        for (uint32_t i = 0; i < n && !b->overrun; i++) {
            br_u(b, (int)log2_lsb); // lt_ref_pic_poc_lsb_sps
            br_u(b, 1);             // used_by_curr_pic_lt_sps_flag
        }
    }
    br_u(b, 1); // sps_temporal_mvp_enabled_flag
    br_u(b, 1); // strong_intra_smoothing_enabled_flag
    s.vui_parameters_present_flag = (uint8_t)br_u(b, 1);
    if (s.vui_parameters_present_flag) parse_vui(b, &s);
    free(rbsp);
    if (b->overrun || s.pic_width_in_luma_samples == 0 || s.pic_height_in_luma_samples == 0) return false;
    s.valid = true;
    *out = s;
    return true;
}

bool hevc_parse_pps(const uint8_t *nal, size_t size, HevcPpsInfo *out) {
    memset(out, 0, sizeof(*out));
    if (size < 3 || hevc_nal_type(nal) != HEVC_NAL_PPS) return false;
    uint8_t rbsp[16];
    const size_t n = size - 2 < sizeof(rbsp) ? size - 2 : sizeof(rbsp);
    BitReader b = {rbsp, nal_to_rbsp(nal + 2, n, rbsp) * 8, 0, false};
    br_ue(&b); // pps_pic_parameter_set_id
    br_ue(&b); // pps_seq_parameter_set_id
    out->dependent_slice_segments_enabled_flag = (uint8_t)br_u(&b, 1);
    out->output_flag_present_flag = (uint8_t)br_u(&b, 1);
    out->num_extra_slice_header_bits = (uint8_t)br_u(&b, 3);
    out->valid = !b.overrun;
    return out->valid;
}

bool hevc_slice_header(const uint8_t *nal, size_t size, const HevcSpsInfo *sps, const HevcPpsInfo *pps, bool *first,
                       uint32_t *poc_lsb) {
    *first = false;
    *poc_lsb = 0;
    if (size < 3) return false;
    // The fields read take at most 46 bits (pps id <= 63, slice_type <= 2, 7 extra bits, a
    // 16-bit lsb): 6 RBSP bytes, well inside 24 NAL bytes with emulation prevention bytes.
    uint8_t rbsp[24];
    const size_t n = size - 2 < sizeof(rbsp) ? size - 2 : sizeof(rbsp);
    BitReader b = {rbsp, nal_to_rbsp(nal + 2, n, rbsp) * 8, 0, false};
    const uint8_t type = hevc_nal_type(nal);
    *first = br_u(&b, 1) != 0; // first_slice_segment_in_pic_flag
    if (!*first) return !b.overrun;
    if (type >= HEVC_NAL_BLA_W_LP && type <= HEVC_NAL_RSV_IRAP_23) br_u(&b, 1); // no_output_of_prior_pics_flag
    br_ue(&b); // slice_pic_parameter_set_id
    // A first segment is never dependent and has no slice_segment_address.
    br_u(&b, pps->num_extra_slice_header_bits); // slice_reserved_flag[]
    br_ue(&b);                                   // slice_type
    if (pps->output_flag_present_flag) br_u(&b, 1); // pic_output_flag
    if (sps->separate_colour_plane_flag) br_u(&b, 2); // colour_plane_id
    if (type != HEVC_NAL_IDR_W_RADL && type != HEVC_NAL_IDR_N_LP)
        *poc_lsb = br_u(&b, sps->log2_max_pic_order_cnt_lsb);
    return !b.overrun;
}

void hevc_sps_dims(const HevcSpsInfo *s, uint32_t *width, uint32_t *height) {
    *width = *height = 0;
    if (!s->valid) return;
    // Table 6-1: SubWidthC / SubHeightC.
    const uint32_t sub_w = (s->chroma_format_idc == 1 || s->chroma_format_idc == 2) && !s->separate_colour_plane_flag ? 2 : 1;
    const uint32_t sub_h = s->chroma_format_idc == 1 && !s->separate_colour_plane_flag ? 2 : 1;
    const uint64_t cw = (uint64_t)sub_w * ((uint64_t)s->conf_win_left_offset + s->conf_win_right_offset);
    const uint64_t ch = (uint64_t)sub_h * ((uint64_t)s->conf_win_top_offset + s->conf_win_bottom_offset);
    if (cw < s->pic_width_in_luma_samples && ch < s->pic_height_in_luma_samples) {
        *width = s->pic_width_in_luma_samples - (uint32_t)cw;
        *height = s->pic_height_in_luma_samples - (uint32_t)ch;
    }
}
