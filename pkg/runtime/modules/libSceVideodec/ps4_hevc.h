// SPDX-License-Identifier: GPL-2.0-or-later
//
// HEVC (ITU-T H.265) bitstream parsing for the VideoToolbox decode core (ps4_h264_vt.c):
// the sequence parameter set fields the PS4 picture info reports, the picture parameter set
// and slice segment header fields needed for the picture order count (8.3.1). Section numbers
// refer to H.265 (V8). Portable; the SPS parser allocates one RBSP copy.
#ifndef PS4_HEVC_H
#define PS4_HEVC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// nal_unit_type values (Table 7-1).
enum {
    HEVC_NAL_TRAIL_N = 0,
    HEVC_NAL_RADL_N = 6,
    HEVC_NAL_RADL_R = 7,
    HEVC_NAL_RASL_N = 8,
    HEVC_NAL_RASL_R = 9,
    HEVC_NAL_RSV_VCL_N14 = 14,
    HEVC_NAL_BLA_W_LP = 16,
    HEVC_NAL_BLA_N_LP = 18,
    HEVC_NAL_IDR_W_RADL = 19,
    HEVC_NAL_IDR_N_LP = 20,
    HEVC_NAL_CRA_NUT = 21,
    HEVC_NAL_RSV_IRAP_23 = 23,
    HEVC_NAL_VCL_LAST = 31,
    HEVC_NAL_VPS = 32,
    HEVC_NAL_SPS = 33,
    HEVC_NAL_PPS = 34,
    HEVC_NAL_AUD = 35,
    HEVC_NAL_EOS = 36,
    HEVC_NAL_EOB = 37,
    HEVC_NAL_FD = 38,
    HEVC_NAL_PREFIX_SEI = 39,
    HEVC_NAL_SUFFIX_SEI = 40,
};

// SEI payloadType values the picture info reports (D.2.1).
enum {
    HEVC_SEI_BUFFERING_PERIOD = 0,
    HEVC_SEI_PIC_TIMING = 1,
    HEVC_SEI_FRAME_PACKING = 45,
    HEVC_SEI_ALTERNATIVE_TRANSFER = 147,
};

static inline uint8_t hevc_nal_type(const uint8_t *nal) { return (uint8_t)((nal[0] >> 1) & 0x3f); }
// TemporalId (nuh_temporal_id_plus1 - 1); the NAL unit header is two bytes.
static inline uint8_t hevc_temporal_id(const uint8_t *nal) { return (uint8_t)((nal[1] & 7) - 1); }

// Sequence parameter set fields (7.3.2.2, profile_tier_level 7.3.3, VUI E.2.1). The sub-layer
// fields are those of sub-layer 0; the ordering fields those of the highest sub-layer.
typedef struct HevcSpsInfo {
    bool valid;
    uint8_t max_sub_layers_minus1;
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
    uint8_t chroma_format_idc;
    uint8_t separate_colour_plane_flag;
    uint32_t pic_width_in_luma_samples;
    uint32_t pic_height_in_luma_samples;
    uint8_t conformance_window_flag;
    uint32_t conf_win_left_offset; // chroma sample units (SubWidthC / SubHeightC)
    uint32_t conf_win_right_offset;
    uint32_t conf_win_top_offset;
    uint32_t conf_win_bottom_offset;
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    uint8_t log2_max_pic_order_cnt_lsb;
    uint8_t sub_layer_ordering_info_present_flag;
    uint8_t max_dec_pic_buffering_minus1;
    uint8_t max_num_reorder_pics;
    uint8_t vui_parameters_present_flag;
    uint8_t aspect_ratio_info_present_flag;
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
    uint8_t chroma_loc_info_present_flag;
    uint8_t chroma_sample_loc_type_top_field;
    uint8_t chroma_sample_loc_type_bottom_field;
    uint8_t field_seq_flag;
    uint8_t frame_field_info_present_flag;
    uint8_t default_display_window_flag;
    uint32_t def_disp_win_left_offset;
    uint32_t def_disp_win_right_offset;
    uint32_t def_disp_win_top_offset;
    uint32_t def_disp_win_bottom_offset;
    uint8_t timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
} HevcSpsInfo;

// Picture parameter set fields that precede slice_pic_order_cnt_lsb in a slice header (7.3.2.3).
typedef struct HevcPpsInfo {
    bool valid;
    uint8_t dependent_slice_segments_enabled_flag;
    uint8_t output_flag_present_flag;
    uint8_t num_extra_slice_header_bits;
} HevcPpsInfo;

// Parse a NAL unit starting with its two-byte header. They return false on a malformed or
// truncated parameter set. The SPS is read up to the VUI timing information (later fields are
// not reported).
bool hevc_parse_sps(const uint8_t *nal, size_t size, HevcSpsInfo *out);
bool hevc_parse_pps(const uint8_t *nal, size_t size, HevcPpsInfo *out);

// Reads the start of a slice segment header (7.3.6.1). *first is first_slice_segment_in_pic_flag;
// for a first segment, *poc_lsb receives slice_pic_order_cnt_lsb (0 for IDR pictures, which do
// not carry it). Returns false when the header is cut short.
bool hevc_slice_header(const uint8_t *nal, size_t size, const HevcSpsInfo *sps, const HevcPpsInfo *pps, bool *first,
                       uint32_t *poc_lsb);

// Picture size after the conformance window (7.4.3.2.1); 0 x 0 for an invalid SPS.
void hevc_sps_dims(const HevcSpsInfo *s, uint32_t *width, uint32_t *height);

#ifdef __cplusplus
}
#endif

#endif // PS4_HEVC_H
