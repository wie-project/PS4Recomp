// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared H.264 decode core on VideoToolbox, used by libSceVideodec (TLOU) and libSceVdecsw
// (Uncharted 4). It takes Annex-B access units and hands out decoded pictures in display order.
//
// - SPS/PPS are pulled out of the stream and turned into the CMVideoFormatDescription; the
//   other NAL units of an access unit are re-packed as one AVCC sample.
// - The session decodes synchronously. VideoToolbox returns pictures in decode order (even with
//   temporal processing requested; checked by tests/host/vdecsw_test.c), so the core puts them in
//   display order itself: it computes each picture's POC from the slice header (8.2.1) and holds
//   up to max_num_reorder_frames pictures (VUI), or MaxDpbFrames when the VUI does not say,
//   releasing the lowest POC first; an IDR or h264vt_finish() releases all. The movies use
//   B-frames, so a picture can come out during a later decode call or at h264vt_finish().
// - Every access unit carries a tag (pts, dts, attached data, the SPS in force and the NAL-unit
//   flags of that access unit); the decoded picture gets the tag of the access unit it came
//   from, so reordering never mixes them up.
// - Pictures are copied out as linear NV12: pitch = align(width, 64), height = align(height, 16),
//   chroma plane right after the luma plane. Rows past the picture repeat the last row.
//
// All functions are thread safe: decode/finish/reset are serialised on the decoder, the
// picture queue has its own lock, so a consumer thread can pop while another thread decodes.
#ifndef PS4_H264_VT_H
#define PS4_H264_VT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Timestamp value meaning "none" (the guests pass -1 for pts/dts they do not know).
#define H264VT_NO_TIME ((int64_t)-1)

// Sequence parameter set fields reported to the guest (ITU-T H.264 7.3.2.1.1 and E.1.1 names).
typedef struct H264SpsInfo {
    bool valid;
    uint8_t profile_idc;
    uint8_t level_idc;
    uint8_t constraint_set_flags; // bit n = constraint_set<n>_flag, n = 0..5
    uint8_t chroma_format_idc;
    uint8_t pic_order_cnt_type;
    uint8_t log2_max_pic_order_cnt_lsb; // pic_order_cnt_type 0 only
    uint32_t pic_width_in_mbs_minus1;
    uint32_t pic_height_in_map_units_minus1;
    uint8_t frame_mbs_only_flag;
    uint8_t frame_cropping_flag;
    uint32_t frame_crop_left_offset; // SPS units (CropUnitX/Y), not pixels
    uint32_t frame_crop_right_offset;
    uint32_t frame_crop_top_offset;
    uint32_t frame_crop_bottom_offset;
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
    uint8_t matrix_coefficients;
    uint8_t timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t fixed_frame_rate_flag;
    uint8_t pic_struct_present_flag;
    uint8_t bitstream_restriction_flag;
    uint8_t max_num_reorder_frames;
    uint8_t max_dec_frame_buffering;
} H264SpsInfo;

// Which NAL units an access unit contained.
typedef struct H264AuFlags {
    uint8_t slice;                // a coded slice (nal_unit_type 1..5): the unit makes a picture
    uint8_t idr;                  // nal_unit_type 5
    uint8_t sps;                  // 7
    uint8_t pps;                  // 8
    uint8_t aud;                  // 9
    uint8_t end_of_sequence;      // 10
    uint8_t end_of_stream;        // 11
    uint8_t filler;               // 12
    uint8_t sei_buffering_period; // SEI payloadType 0
    uint8_t sei_pic_timing;       // SEI payloadType 1
} H264AuFlags;

typedef struct H264VtFrame {
    void *pixbuf; // CVPixelBufferRef (NV12, video range); NULL for an error picture
    bool error;   // the access unit could not be decoded; width/height come from its SPS (0 if none)
    uint32_t width;
    uint32_t height;
    int64_t pts;
    int64_t dts;
    uint64_t attached;
    H264SpsInfo sps;
    H264AuFlags au;
    int32_t poc; // picture order count; pictures leave the decoder in POC order
    struct H264VtFrame *next;
} H264VtFrame;

typedef struct H264VtDecoder H264VtDecoder;

// log_tag prefixes the decoder's stderr messages, e.g. "libSceVideodec".
H264VtDecoder *h264vt_create(const char *log_tag);
void h264vt_destroy(H264VtDecoder *d);

// Decodes one Annex-B access unit. Returns 0 or a negative H264VT_ERR_*; *ready (may be NULL)
// receives the number of pictures that became available during the call. Every access unit
// with a coded slice yields exactly one picture, in POC order: a decoded one, or an error
// picture (H264VtFrame.error) when it cannot be decoded (VideoToolbox error, no or rejected
// parameter sets). An access unit without a slice (parameter sets, SEI) yields none.
#define H264VT_ERR_NOMEM (-1)
#define H264VT_ERR_SESSION (-2) // format description / session creation failed
#define H264VT_ERR_DECODE (-3)  // VTDecompressionSessionDecodeFrame refused the sample
int h264vt_decode(H264VtDecoder *d, const uint8_t *au, size_t size, int64_t pts, int64_t dts, uint64_t attached,
                  int *ready);

// Emits the pictures VideoToolbox still holds for reordering; returns how many became available.
int h264vt_finish(H264VtDecoder *d);
// Emits held pictures and drops every queued picture; parameter sets and session are kept.
void h264vt_reset(H264VtDecoder *d);

H264VtFrame *h264vt_pop(H264VtDecoder *d);
// Pops the oldest queued picture only if h264vt_nv12_size() of it plus `extra` fits in
// `capacity`; the check and the pop are one step under the queue lock, so a concurrent reset or
// pop cannot free the picture in between. *need (may be NULL) receives the size required.
#define H264VT_POP_OK 0
#define H264VT_POP_EMPTY 1
#define H264VT_POP_TOO_SMALL 2
int h264vt_pop_fitting(H264VtDecoder *d, uint64_t capacity, uint64_t extra, H264VtFrame **out, uint64_t *need);
int h264vt_queued(H264VtDecoder *d);
void h264vt_frame_free(H264VtFrame *f);

// Linear NV12 geometry of a picture.
static inline uint32_t h264vt_pitch(uint32_t width) { return (width + 63u) & ~63u; }
static inline uint32_t h264vt_aligned_height(uint32_t height) { return (height + 15u) & ~15u; }
static inline uint64_t h264vt_nv12_size(uint32_t width, uint32_t height) {
    return (uint64_t)h264vt_pitch(width) * h264vt_aligned_height(height) * 3 / 2;
}
// Copies the picture to dst as linear NV12 (see above); dst must hold h264vt_nv12_size() bytes.
void h264vt_copy_nv12(const H264VtFrame *f, uint8_t *dst);

// Parses an SPS NAL unit (starting with the NAL header byte). Returns false on a malformed SPS.
bool h264_parse_sps(const uint8_t *nal, size_t size, H264SpsInfo *out);

// The NAL units of one Annex-B buffer (an access unit, or whatever the guest passed as one).
// Start codes may be 3 or 4 bytes. nal_unit_type 1..5 count as slices (2..4 are data
// partitions A..C); 14 and 20 (prefix and extension NAL units of SVC/MVC) and every other type
// are counted in nal_count and nal_types only. libSceVideodec runs it on every Decode call: one
// linear scan for start codes plus at most 12 bytes of the first slice header.
typedef struct H264AuCensus {
    uint32_t nal_count;
    uint8_t nal_types[32];        // the first 32 nal_unit_types, in stream order
    uint32_t slices;              // coded slices (nal_unit_type 1..5)
    int32_t first_mb_first_slice; // first_mb_in_slice of the first slice; -1 without a slice
    bool has_idr;
    bool has_sps;
} H264AuCensus;

// Counts the NAL units of an Annex-B buffer. Portable, makes no allocation.
void h264_au_census(const uint8_t *au, size_t size, H264AuCensus *out);

#ifdef __cplusplus
}
#endif

#endif // PS4_H264_VT_H
