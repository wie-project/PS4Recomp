// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared H.264 decode core on VideoToolbox (see ps4_h264_vt.h).
#include "ps4_h264_vt.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------
// Bitstream parsing (portable)
// ---------------------------------------------------------------------------------------------

// Removes emulation prevention bytes (00 00 03 -> 00 00). Returns the RBSP length.
static size_t nal_to_rbsp(const uint8_t *src, size_t n, uint8_t *dst) {
    size_t o = 0;
    int zeros = 0;
    for (size_t i = 0; i < n; i++) {
        if (zeros >= 2 && src[i] == 3) {
            zeros = 0;
            continue;
        }
        zeros = src[i] == 0 ? zeros + 1 : 0;
        dst[o++] = src[i];
    }
    return o;
}

typedef struct BitReader {
    const uint8_t *p;
    size_t bits;
    size_t pos;
    bool overrun;
} BitReader;

static uint32_t br_u(BitReader *b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (b->pos >= b->bits) {
            b->overrun = true;
            return 0;
        }
        v = (v << 1) | ((b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u);
        b->pos++;
    }
    return v;
}

static uint32_t br_ue(BitReader *b) {
    int lz = 0;
    while (br_u(b, 1) == 0) {
        if (b->overrun || ++lz > 31) {
            b->overrun = true;
            return 0;
        }
    }
    if (lz == 0) return 0;
    return ((1u << lz) - 1u) + br_u(b, lz);
}

static int32_t br_se(BitReader *b) {
    uint32_t k = br_ue(b);
    return (k & 1) ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
}

static void skip_scaling_list(BitReader *b, int size) {
    int last = 8, next = 8;
    for (int j = 0; j < size && !b->overrun; j++) {
        if (next != 0) next = (last + br_se(b) + 256) % 256;
        last = next == 0 ? last : next;
    }
}

static void skip_hrd_parameters(BitReader *b) {
    uint32_t cpb_cnt_minus1 = br_ue(b);
    if (cpb_cnt_minus1 > 31) {
        b->overrun = true;
        return;
    }
    br_u(b, 4); // bit_rate_scale
    br_u(b, 4); // cpb_size_scale
    for (uint32_t i = 0; i <= cpb_cnt_minus1 && !b->overrun; i++) {
        br_ue(b); // bit_rate_value_minus1
        br_ue(b); // cpb_size_value_minus1
        br_u(b, 1); // cbr_flag
    }
    br_u(b, 5); // initial_cpb_removal_delay_length_minus1
    br_u(b, 5); // cpb_removal_delay_length_minus1
    br_u(b, 5); // dpb_output_delay_length_minus1
    br_u(b, 5); // time_offset_length
}

// SPS fields needed to compute picture order counts (8.2.1).
typedef struct PocParams {
    bool valid;
    uint8_t separate_colour_plane_flag;
    uint32_t log2_max_frame_num;
    uint32_t pic_order_cnt_type;
    uint32_t log2_max_poc_lsb;
    uint8_t delta_pic_order_always_zero_flag;
    int32_t offset_for_non_ref_pic;
    int32_t offset_for_top_to_bottom_field;
    uint32_t num_ref_frames_in_poc_cycle;
    int32_t offset_for_ref_frame[256];
    uint32_t reorder_depth; // pictures held back for display reordering
} PocParams;

// Pictures the decoder may hold back for reordering when the VUI does not say (MaxDpbFrames,
// Table A-1 / A.3.1 h).
static uint32_t max_dpb_frames(const H264SpsInfo *s) {
    uint32_t mbs;
    switch (s->level_idc) {
    case 9: case 10: mbs = 396; break;
    case 11: mbs = (s->constraint_set_flags & 8) && s->profile_idc != 100 ? 396 : 900; break; // level 1b
    case 12: case 13: case 20: mbs = 2376; break;
    case 21: mbs = 4752; break;
    case 22: case 30: mbs = 8100; break;
    case 31: mbs = 18000; break;
    case 32: mbs = 20480; break;
    case 40: case 41: mbs = 32768; break;
    case 42: mbs = 34816; break;
    case 50: mbs = 110400; break;
    default: mbs = 184320; break;
    }
    uint64_t frame_mbs = (uint64_t)(s->pic_width_in_mbs_minus1 + 1) * (2 - s->frame_mbs_only_flag) *
                         (s->pic_height_in_map_units_minus1 + 1);
    uint64_t n = frame_mbs ? mbs / frame_mbs : 16;
    return n > 16 ? 16 : (uint32_t)n;
}

static bool parse_sps(const uint8_t *nal, size_t size, H264SpsInfo *out, PocParams *poc) {
    memset(out, 0, sizeof(*out));
    PocParams pp;
    memset(&pp, 0, sizeof(pp));
    if (size < 4 || (nal[0] & 0x1f) != 7) return false;
    uint8_t *rbsp = (uint8_t *)malloc(size);
    if (!rbsp) return false;
    BitReader br = {rbsp, nal_to_rbsp(nal + 1, size - 1, rbsp) * 8, 0, false};
    BitReader *b = &br;
    H264SpsInfo s = {0};
    s.profile_idc = (uint8_t)br_u(b, 8);
    s.constraint_set_flags = 0;
    uint32_t cflags = br_u(b, 8);
    for (int n = 0; n < 6; n++)
        if (cflags & (0x80u >> n)) s.constraint_set_flags |= (uint8_t)(1u << n);
    s.level_idc = (uint8_t)br_u(b, 8);
    br_ue(b); // seq_parameter_set_id
    s.chroma_format_idc = 1;
    switch (s.profile_idc) {
    case 100: case 110: case 122: case 244: case 44: case 83: case 86: case 118: case 128: case 138:
    case 139: case 134: case 135: {
        s.chroma_format_idc = (uint8_t)br_ue(b);
        if (s.chroma_format_idc == 3) pp.separate_colour_plane_flag = (uint8_t)br_u(b, 1);
        br_ue(b); // bit_depth_luma_minus8
        br_ue(b); // bit_depth_chroma_minus8
        br_u(b, 1); // qpprime_y_zero_transform_bypass_flag
        if (br_u(b, 1)) { // seq_scaling_matrix_present_flag
            int lists = s.chroma_format_idc != 3 ? 8 : 12;
            for (int i = 0; i < lists && !b->overrun; i++)
                if (br_u(b, 1)) skip_scaling_list(b, i < 6 ? 16 : 64);
        }
        break;
    }
    default:
        break;
    }
    pp.log2_max_frame_num = br_ue(b) + 4;
    pp.pic_order_cnt_type = br_ue(b);
    if (pp.pic_order_cnt_type == 0) {
        pp.log2_max_poc_lsb = br_ue(b) + 4;
    } else if (pp.pic_order_cnt_type == 1) {
        pp.delta_pic_order_always_zero_flag = (uint8_t)br_u(b, 1);
        pp.offset_for_non_ref_pic = br_se(b);
        pp.offset_for_top_to_bottom_field = br_se(b);
        pp.num_ref_frames_in_poc_cycle = br_ue(b);
        if (pp.num_ref_frames_in_poc_cycle > 255) b->overrun = true;
        for (uint32_t i = 0; i < pp.num_ref_frames_in_poc_cycle && !b->overrun; i++) pp.offset_for_ref_frame[i] = br_se(b);
    }
    if (pp.log2_max_frame_num > 16 || pp.log2_max_poc_lsb > 16 || pp.pic_order_cnt_type > 2) b->overrun = true;
    br_ue(b);   // max_num_ref_frames
    br_u(b, 1); // gaps_in_frame_num_value_allowed_flag
    s.pic_width_in_mbs_minus1 = br_ue(b);
    s.pic_height_in_map_units_minus1 = br_ue(b);
    s.frame_mbs_only_flag = (uint8_t)br_u(b, 1);
    if (!s.frame_mbs_only_flag) br_u(b, 1); // mb_adaptive_frame_field_flag
    br_u(b, 1); // direct_8x8_inference_flag
    s.frame_cropping_flag = (uint8_t)br_u(b, 1);
    if (s.frame_cropping_flag) {
        s.frame_crop_left_offset = br_ue(b);
        s.frame_crop_right_offset = br_ue(b);
        s.frame_crop_top_offset = br_ue(b);
        s.frame_crop_bottom_offset = br_ue(b);
    }
    s.vui_parameters_present_flag = (uint8_t)br_u(b, 1);
    if (s.vui_parameters_present_flag) {
        s.aspect_ratio_info_present_flag = (uint8_t)br_u(b, 1);
        if (s.aspect_ratio_info_present_flag) {
            s.aspect_ratio_idc = (uint8_t)br_u(b, 8);
            if (s.aspect_ratio_idc == 255) { // Extended_SAR
                s.sar_width = (uint16_t)br_u(b, 16);
                s.sar_height = (uint16_t)br_u(b, 16);
            }
        }
        if (br_u(b, 1)) br_u(b, 1); // overscan_info_present_flag, overscan_appropriate_flag
        s.video_signal_type_present_flag = (uint8_t)br_u(b, 1);
        if (s.video_signal_type_present_flag) {
            s.video_format = (uint8_t)br_u(b, 3);
            s.video_full_range_flag = (uint8_t)br_u(b, 1);
            s.colour_description_present_flag = (uint8_t)br_u(b, 1);
            if (s.colour_description_present_flag) {
                s.colour_primaries = (uint8_t)br_u(b, 8);
                s.transfer_characteristics = (uint8_t)br_u(b, 8);
                s.matrix_coefficients = (uint8_t)br_u(b, 8);
            }
        }
        if (br_u(b, 1)) { // chroma_loc_info_present_flag
            br_ue(b);
            br_ue(b);
        }
        s.timing_info_present_flag = (uint8_t)br_u(b, 1);
        if (s.timing_info_present_flag) {
            s.num_units_in_tick = br_u(b, 32);
            s.time_scale = br_u(b, 32);
            s.fixed_frame_rate_flag = (uint8_t)br_u(b, 1);
        }
        uint32_t nal_hrd = br_u(b, 1);
        if (nal_hrd) skip_hrd_parameters(b);
        uint32_t vcl_hrd = br_u(b, 1);
        if (vcl_hrd) skip_hrd_parameters(b);
        if (nal_hrd || vcl_hrd) br_u(b, 1); // low_delay_hrd_flag
        s.pic_struct_present_flag = (uint8_t)br_u(b, 1);
        s.bitstream_restriction_flag = (uint8_t)br_u(b, 1);
        if (s.bitstream_restriction_flag) {
            br_u(b, 1); // motion_vectors_over_pic_boundaries_flag
            br_ue(b);   // max_bytes_per_pic_denom
            br_ue(b);   // max_bits_per_mb_denom
            br_ue(b);   // log2_max_mv_length_horizontal
            br_ue(b);   // log2_max_mv_length_vertical
            s.max_num_reorder_frames = (uint8_t)br_ue(b);
            s.max_dec_frame_buffering = (uint8_t)br_ue(b);
        }
    }
    free(rbsp);
    if (b->overrun) return false;
    s.pic_order_cnt_type = (uint8_t)pp.pic_order_cnt_type;
    s.log2_max_pic_order_cnt_lsb = (uint8_t)pp.log2_max_poc_lsb;
    s.valid = true;
    *out = s;
    if (poc) {
        // POC type 2 outputs in decode order; intra-only and baseline streams have no B-frames.
        bool no_reorder = pp.pic_order_cnt_type == 2 || s.profile_idc == 66 ||
                          ((s.constraint_set_flags & 8) &&
                           (s.profile_idc == 44 || s.profile_idc == 86 || s.profile_idc == 100 ||
                            s.profile_idc == 110 || s.profile_idc == 122 || s.profile_idc == 244));
        if (no_reorder) pp.reorder_depth = 0;
        else if (s.bitstream_restriction_flag) pp.reorder_depth = s.max_num_reorder_frames;
        else pp.reorder_depth = max_dpb_frames(&s);
        pp.valid = true;
        *poc = pp;
    }
    return true;
}

bool h264_parse_sps(const uint8_t *nal, size_t size, H264SpsInfo *out) { return parse_sps(nal, size, out, NULL); }

// Records SEI payload types 0 (buffering period) and 1 (picture timing) of an SEI NAL unit.
static void scan_sei(const uint8_t *nal, size_t n, H264AuFlags *f) {
    uint8_t *rbsp = (uint8_t *)malloc(n);
    if (!rbsp) return;
    size_t len = nal_to_rbsp(nal + 1, n - 1, rbsp);
    size_t i = 0;
    while (i < len && rbsp[i] != 0x80) { // 0x80 = rbsp_trailing_bits
        uint32_t type = 0, size = 0;
        while (i < len && rbsp[i] == 0xFF) type += rbsp[i++];
        if (i >= len) break;
        type += rbsp[i++];
        while (i < len && rbsp[i] == 0xFF) size += rbsp[i++];
        if (i >= len) break;
        size += rbsp[i++];
        if (type == 0) f->sei_buffering_period = 1;
        if (type == 1) f->sei_pic_timing = 1;
        i += size;
    }
    free(rbsp);
}

// Position of the next 00 00 01 start code at or after i (size if none).
static size_t next_start(const uint8_t *p, size_t i, size_t size) {
    while (i + 3 <= size) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) return i;
        i++;
    }
    return size;
}

// first_mb_in_slice, the first field of a slice header (7.3.3); -1 if the NAL is cut short.
// A ue(v) takes at most 63 bits (8 RBSP bytes); with emulation-prevention bytes (at most one in
// every three) that is at most 12 bytes of the NAL.
static int32_t slice_first_mb(const uint8_t *nal, size_t n) {
    uint8_t rbsp[12];
    const size_t len = nal_to_rbsp(nal + 1, n - 1 < sizeof(rbsp) ? n - 1 : sizeof(rbsp), rbsp);
    BitReader b = {rbsp, len * 8, 0, false};
    const uint32_t v = br_ue(&b);
    return b.overrun || v > INT32_MAX ? -1 : (int32_t)v;
}

void h264_au_census(const uint8_t *au, size_t size, H264AuCensus *out) {
    memset(out, 0, sizeof(*out));
    out->first_mb_first_slice = -1;
    if (!au) return;
    size_t sc = next_start(au, 0, size);
    while (sc < size) {
        const size_t s = sc + 3;
        const size_t nsc = next_start(au, s, size);
        size_t e = nsc;
        while (e > s && au[e - 1] == 0) e--; // zero bytes before the next start code
        sc = nsc;
        if (e == s) continue;
        const uint8_t type = au[s] & 0x1f;
        if (out->nal_count < sizeof(out->nal_types)) out->nal_types[out->nal_count] = type;
        out->nal_count++;
        if (type == 5) out->has_idr = true;
        if (type == 7) out->has_sps = true;
        if (type >= 1 && type <= 5) {
            if (out->slices++ == 0) out->first_mb_first_slice = e - s > 1 ? slice_first_mb(au + s, e - s) : -1;
        }
    }
}

#if defined(__APPLE__)
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

// Per access unit, travels through VideoToolbox as the sourceFrameRefCon.
typedef struct AuTag {
    int64_t pts;
    int64_t dts;
    uint64_t attached;
    H264SpsInfo sps;
    H264AuFlags au;
    int32_t poc;
    uint32_t reorder_depth;
    // Lifetime: one reference for the decoder (dropped once the outcome of the access unit is
    // known) and one for VideoToolbox's callback. Both fields are protected by q_lock.
    int refs;
    bool delivered; // a picture (decoded or error) was queued for this access unit
    struct AuTag *next_pending;
} AuTag;

struct H264VtDecoder {
    char tag[32];
    pthread_mutex_t dec_lock; // session, parameter sets, AVCC scratch
    VTDecompressionSessionRef session;
    CMVideoFormatDescriptionRef fmt;
    uint8_t sps[512];
    size_t sps_len;
    uint8_t pps[512];
    size_t pps_len;
    bool params_dirty;
    H264SpsInfo sps_info;
    PocParams poc_params;
    bool pps_valid;
    uint8_t pps_bottom_field_pic_order_in_frame_present_flag;
    // Picture order count state (8.2.1), decode order.
    int32_t prev_poc_msb;
    int32_t prev_poc_lsb;
    uint32_t prev_frame_num_offset;
    uint32_t prev_frame_num;
    uint8_t *avcc;
    size_t avcc_cap;
    AuTag *pending; // submitted access units whose picture has not come back yet
    bool warned_no_session;

    pthread_mutex_t q_lock; // picture queues
    // VideoToolbox hands pictures out in decode order; they wait here, sorted by POC, until
    // more than reorder_depth are held (or a flush), then move to the output queue below.
    H264VtFrame *held;
    int held_count;
    // Early release (see hold_locked): POC of the last released picture.
    bool have_last_out;
    int32_t last_out_poc;
    H264VtFrame *head;
    H264VtFrame *tail;
    int queued;
    uint64_t emitted;
    uint64_t errors;
};

// Moves the held picture with the lowest POC to the output queue. q_lock held.
static void release_one_locked(H264VtDecoder *d) {
    H264VtFrame *f = d->held;
    if (!f) return;
    d->held = f->next;
    d->held_count--;
    f->next = NULL;
    d->have_last_out = true;
    d->last_out_poc = f->poc;
    if (d->tail) d->tail->next = f;
    else d->head = f;
    d->tail = f;
    d->queued++;
    d->emitted++;
}

static void release_all(H264VtDecoder *d) {
    pthread_mutex_lock(&d->q_lock);
    while (d->held) release_one_locked(d);
    pthread_mutex_unlock(&d->q_lock);
}

// Picture size an SPS describes (7.4.2.1.1 frame cropping), for error pictures.
static void sps_dims(const H264SpsInfo *s, uint32_t *w, uint32_t *h) {
    *w = *h = 0;
    if (!s->valid) return;
    const uint32_t cux = (s->chroma_format_idc == 1 || s->chroma_format_idc == 2) ? 2 : 1;
    const uint32_t cuy = (s->chroma_format_idc == 1 ? 2 : 1) * (2 - s->frame_mbs_only_flag);
    const uint32_t fw = (s->pic_width_in_mbs_minus1 + 1) * 16;
    const uint32_t fh = (s->pic_height_in_map_units_minus1 + 1) * 16 * (2 - s->frame_mbs_only_flag);
    const uint32_t cw = cux * (s->frame_crop_left_offset + s->frame_crop_right_offset);
    const uint32_t ch = cuy * (s->frame_crop_top_offset + s->frame_crop_bottom_offset);
    if (cw < fw && ch < fh) {
        *w = fw - cw;
        *h = fh - ch;
    }
}

// Sorted insert into the reorder list, then releases what exceeds the depth. q_lock held.
//
// Besides the depth bound (the most pictures that can precede one in display order), the lowest
// held picture leaves early when its POC is the last released POC + 1: no picture can fall
// between two consecutive integers, so this never misorders. VideoToolbox's encoder steps POC by
// 1 per frame and writes no VUI max_num_reorder_frames, where the bound alone would be
// MaxDpbFrames (8-16 pictures of delay); x264 steps by 2 but writes the VUI value. An IDR
// picture starts a new sequence after a full flush and leaves at once.
static void hold_locked(H264VtDecoder *d, H264VtFrame *f, uint32_t depth) {
    if (f->au.idr && !d->held) {
        f->next = NULL;
        d->held = f;
        d->held_count = 1;
        release_one_locked(d);
        return;
    }
    // A picture with the same POC goes after the ones already held.
    H264VtFrame **pp = &d->held;
    while (*pp && (*pp)->poc <= f->poc) pp = &(*pp)->next;
    f->next = *pp;
    *pp = f;
    d->held_count++;
    while (d->held_count > (int)depth) release_one_locked(d);
    while (d->held && d->have_last_out && d->held->poc == d->last_out_poc + 1) release_one_locked(d);
}

// Queues the picture of the tag's access unit: decoded (image) or an error picture (NULL).
// q_lock held. Returns false if the tag already had its picture.
static bool deliver_locked(H264VtDecoder *d, AuTag *t, CVImageBufferRef image) {
    if (t->delivered) return false;
    t->delivered = true;
    H264VtFrame *f = (H264VtFrame *)calloc(1, sizeof(*f));
    if (!f) return true;
    if (image) {
        f->pixbuf = (void *)CVPixelBufferRetain(image);
        f->width = (uint32_t)CVPixelBufferGetWidth(image);
        f->height = (uint32_t)CVPixelBufferGetHeight(image);
    } else {
        f->error = true;
        sps_dims(&t->sps, &f->width, &f->height);
    }
    f->pts = t->pts;
    f->dts = t->dts;
    f->attached = t->attached;
    f->sps = t->sps;
    f->au = t->au;
    f->poc = t->poc;
    hold_locked(d, f, t->reorder_depth);
    return true;
}

static void tag_unref_locked(AuTag *t) {
    if (--t->refs == 0) free(t);
}

static void output_cb(void *refcon, void *frame_refcon, OSStatus status, VTDecodeInfoFlags flags, CVImageBufferRef image,
                      CMTime pts, CMTime duration) {
    (void)pts;
    (void)duration;
    H264VtDecoder *d = (H264VtDecoder *)refcon;
    AuTag *t = (AuTag *)frame_refcon;
    const bool failed = status != noErr || !image || (flags & kVTDecodeInfo_FrameDropped);
    pthread_mutex_lock(&d->q_lock);
    uint64_t n = failed ? d->errors++ : 0;
    if (t) {
        deliver_locked(d, t, failed ? NULL : image);
        tag_unref_locked(t);
    }
    pthread_mutex_unlock(&d->q_lock);
    if (failed && n < 8)
        fprintf(stderr, "[%s] VideoToolbox decode error %d (flags 0x%x): error picture\n", d->tag, (int)status,
                (unsigned)flags);
}

// Settles the decoder's reference of every pending access unit. With fail_undelivered, an access
// unit whose picture never came back (after FinishDelayedFrames) gets an error picture; otherwise
// only the delivered ones are dropped from the list. dec_lock held.
static void settle_pending(H264VtDecoder *d, bool fail_undelivered) {
    pthread_mutex_lock(&d->q_lock);
    AuTag **pp = &d->pending;
    while (*pp) {
        AuTag *t = *pp;
        if (t->delivered || fail_undelivered) {
            if (!t->delivered) {
                deliver_locked(d, t, NULL);
                fprintf(stderr, "[%s] no picture for access unit %llu: error picture\n", d->tag,
                        (unsigned long long)t->attached);
            }
            *pp = t->next_pending;
            tag_unref_locked(t);
        } else {
            pp = &t->next_pending;
        }
    }
    pthread_mutex_unlock(&d->q_lock);
}

H264VtDecoder *h264vt_create(const char *log_tag) {
    H264VtDecoder *d = (H264VtDecoder *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    snprintf(d->tag, sizeof(d->tag), "%s", log_tag ? log_tag : "h264vt");
    pthread_mutex_init(&d->dec_lock, NULL);
    pthread_mutex_init(&d->q_lock, NULL);
    return d;
}

static void destroy_session(H264VtDecoder *d) {
    if (d->session) {
        VTDecompressionSessionFinishDelayedFrames(d->session);
        VTDecompressionSessionWaitForAsynchronousFrames(d->session);
        VTDecompressionSessionInvalidate(d->session);
        CFRelease(d->session);
        d->session = NULL;
    }
    if (d->fmt) {
        CFRelease(d->fmt);
        d->fmt = NULL;
    }
}

static void drop_queue(H264VtDecoder *d) {
    release_all(d);
    H264VtFrame *f;
    while ((f = h264vt_pop(d)) != NULL) h264vt_frame_free(f);
}

void h264vt_destroy(H264VtDecoder *d) {
    if (!d) return;
    pthread_mutex_lock(&d->dec_lock);
    destroy_session(d);
    settle_pending(d, true);
    pthread_mutex_unlock(&d->dec_lock);
    drop_queue(d);
    free(d->avcc);
    pthread_mutex_destroy(&d->q_lock);
    pthread_mutex_destroy(&d->dec_lock);
    free(d);
}

static bool create_session(H264VtDecoder *d) {
    const uint8_t *sets[2] = {d->sps, d->pps};
    const size_t sizes[2] = {d->sps_len, d->pps_len};
    CMVideoFormatDescriptionRef fmt = NULL;
    OSStatus st = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, sets, sizes, 4, &fmt);
    if (st != noErr) {
        fprintf(stderr, "[%s] CMVideoFormatDescriptionCreateFromH264ParameterSets failed: %d\n", d->tag, (int)st);
        return false;
    }
    if (d->session && d->fmt && VTDecompressionSessionCanAcceptFormatDescription(d->session, fmt)) {
        CFRelease(d->fmt);
        d->fmt = fmt;
        return true;
    }
    destroy_session(d);
    d->fmt = fmt;

    int32_t pixfmt = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    CFNumberRef pf = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixfmt);
    const void *keys[] = {kCVPixelBufferPixelFormatTypeKey};
    const void *vals[] = {pf};
    CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFRelease(pf);
    VTDecompressionOutputCallbackRecord cb = {output_cb, d};
    st = VTDecompressionSessionCreate(kCFAllocatorDefault, d->fmt, NULL, attrs, &cb, &d->session);
    CFRelease(attrs);
    if (st != noErr) {
        fprintf(stderr, "[%s] VTDecompressionSessionCreate failed: %d\n", d->tag, (int)st);
        d->session = NULL;
        return false;
    }
    CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(d->fmt);
    fprintf(stderr, "[%s] VideoToolbox H.264 session %dx%d\n", d->tag, dim.width, dim.height);
    return true;
}

static bool append_nal(H264VtDecoder *d, size_t *len, const uint8_t *nal, size_t n) {
    if (*len + n + 4 > d->avcc_cap) {
        size_t cap = (*len + n + 4) * 2;
        uint8_t *p = (uint8_t *)realloc(d->avcc, cap);
        if (!p) return false;
        d->avcc = p;
        d->avcc_cap = cap;
    }
    uint8_t *o = d->avcc + *len;
    o[0] = (uint8_t)(n >> 24);
    o[1] = (uint8_t)(n >> 16);
    o[2] = (uint8_t)(n >> 8);
    o[3] = (uint8_t)n;
    memcpy(o + 4, nal, n);
    *len += n + 4;
    return true;
}

// pic_parameter_set_rbsp up to bottom_field_pic_order_in_frame_present_flag (7.3.2.2).
static void parse_pps(H264VtDecoder *d, const uint8_t *nal, size_t n) {
    uint8_t rbsp[64];
    size_t len = nal_to_rbsp(nal + 1, n - 1 < sizeof(rbsp) ? n - 1 : sizeof(rbsp), rbsp);
    BitReader b = {rbsp, len * 8, 0, false};
    br_ue(&b);   // pic_parameter_set_id
    br_ue(&b);   // seq_parameter_set_id
    br_u(&b, 1); // entropy_coding_mode_flag
    d->pps_bottom_field_pic_order_in_frame_present_flag = (uint8_t)br_u(&b, 1);
    d->pps_valid = !b.overrun;
}

// Picture order count of a slice (slice header 7.3.3 up to delta_pic_order_cnt, decoding
// process 8.2.1). Returns false when the parameter sets are unknown. A frame's POC is
// min(TopFieldOrderCnt, BottomFieldOrderCnt). memory_management_control_operation 5 is not
// looked at (it would need the full slice header); the encoders of the shipped movies do not
// use it.
static bool slice_poc(H264VtDecoder *d, const uint8_t *nal, size_t n, int32_t *poc) {
    const PocParams *pp = &d->poc_params;
    if (!pp->valid || !d->pps_valid) return false;
    uint8_t rbsp[96];
    size_t len = nal_to_rbsp(nal + 1, n - 1 < sizeof(rbsp) ? n - 1 : sizeof(rbsp), rbsp);
    BitReader b = {rbsp, len * 8, 0, false};
    const bool idr = (nal[0] & 0x1f) == 5;
    const bool ref = (nal[0] >> 5) != 0; // nal_ref_idc
    br_ue(&b); // first_mb_in_slice
    br_ue(&b); // slice_type
    br_ue(&b); // pic_parameter_set_id
    if (pp->separate_colour_plane_flag) br_u(&b, 2);
    const uint32_t frame_num = br_u(&b, (int)pp->log2_max_frame_num);
    bool field = false, bottom = false;
    if (!d->sps_info.frame_mbs_only_flag) {
        field = br_u(&b, 1);
        if (field) bottom = br_u(&b, 1);
    }
    if (idr) br_ue(&b); // idr_pic_id
    uint32_t lsb = 0;
    int32_t delta_bottom = 0, delta0 = 0, delta1 = 0;
    if (pp->pic_order_cnt_type == 0) {
        lsb = br_u(&b, (int)pp->log2_max_poc_lsb);
        if (d->pps_bottom_field_pic_order_in_frame_present_flag && !field) delta_bottom = br_se(&b);
    } else if (pp->pic_order_cnt_type == 1 && !pp->delta_pic_order_always_zero_flag) {
        delta0 = br_se(&b);
        if (d->pps_bottom_field_pic_order_in_frame_present_flag && !field) delta1 = br_se(&b);
    }
    if (b.overrun) return false;

    int32_t top, bot;
    if (pp->pic_order_cnt_type == 0) {
        const int32_t max_lsb = 1 << pp->log2_max_poc_lsb;
        if (idr) d->prev_poc_msb = d->prev_poc_lsb = 0;
        int32_t msb;
        if ((int32_t)lsb < d->prev_poc_lsb && d->prev_poc_lsb - (int32_t)lsb >= max_lsb / 2)
            msb = d->prev_poc_msb + max_lsb;
        else if ((int32_t)lsb > d->prev_poc_lsb && (int32_t)lsb - d->prev_poc_lsb > max_lsb / 2)
            msb = d->prev_poc_msb - max_lsb;
        else
            msb = d->prev_poc_msb;
        top = msb + (int32_t)lsb;
        bot = field ? top : top + delta_bottom;
        if (ref) {
            d->prev_poc_msb = msb;
            d->prev_poc_lsb = (int32_t)lsb;
        }
    } else {
        const uint32_t max_frame_num = 1u << pp->log2_max_frame_num;
        uint32_t offset;
        if (idr) offset = 0;
        else if (d->prev_frame_num > frame_num) offset = d->prev_frame_num_offset + max_frame_num;
        else offset = d->prev_frame_num_offset;
        if (pp->pic_order_cnt_type == 1) {
            uint32_t abs_frame_num = pp->num_ref_frames_in_poc_cycle ? offset + frame_num : 0;
            if (!ref && abs_frame_num > 0) abs_frame_num--;
            int32_t expected = 0;
            if (abs_frame_num > 0) {
                int32_t per_cycle = 0;
                for (uint32_t i = 0; i < pp->num_ref_frames_in_poc_cycle; i++) per_cycle += pp->offset_for_ref_frame[i];
                uint32_t cycle_cnt = (abs_frame_num - 1) / pp->num_ref_frames_in_poc_cycle;
                uint32_t in_cycle = (abs_frame_num - 1) % pp->num_ref_frames_in_poc_cycle;
                expected = (int32_t)cycle_cnt * per_cycle;
                for (uint32_t i = 0; i <= in_cycle; i++) expected += pp->offset_for_ref_frame[i];
            }
            if (!ref) expected += pp->offset_for_non_ref_pic;
            // 8-6..8-10: a frame has both counts; a bottom field alone uses delta[0] plus the
            // top-to-bottom offset. (Field pictures are decoded but not paired into frames.)
            if (!field) {
                top = expected + delta0;
                bot = top + pp->offset_for_top_to_bottom_field + delta1;
            } else if (!bottom) {
                top = bot = expected + delta0;
            } else {
                top = bot = expected + pp->offset_for_top_to_bottom_field + delta0;
            }
        } else {
            int32_t t = idr ? 0 : (int32_t)(2 * (offset + frame_num)) - (ref ? 0 : 1);
            top = bot = t;
        }
        d->prev_frame_num_offset = offset;
        d->prev_frame_num = frame_num;
    }
    if (field) *poc = bottom ? bot : top;
    else *poc = top < bot ? top : bot;
    return true;
}

// Splits the Annex-B access unit, records SPS/PPS and the NAL flags, computes the POC of its
// first slice (*poc, *have_poc), and leaves the AVCC sample with the other NAL units in
// d->avcc. Returns its length, or (size_t)-1 when out of memory.
static size_t parse_au(H264VtDecoder *d, const uint8_t *au, size_t size, H264AuFlags *flags, int32_t *poc,
                       bool *have_poc) {
    bool seen_slice = false;
    size_t out = 0;
    size_t sc = next_start(au, 0, size);
    while (sc < size) {
        size_t s = sc + 3;
        size_t nsc = next_start(au, s, size);
        size_t e = nsc;
        while (e > s && au[e - 1] == 0) e--; // zero bytes before the next start code
        size_t n = e - s;
        if (n > 0) {
            uint8_t type = au[s] & 0x1f;
            switch (type) {
            case 7:
                flags->sps = 1;
                if (n <= sizeof(d->sps) && (n != d->sps_len || memcmp(d->sps, au + s, n) != 0)) {
                    memcpy(d->sps, au + s, n);
                    d->sps_len = n;
                    d->params_dirty = true;
                    if (!parse_sps(au + s, n, &d->sps_info, &d->poc_params))
                        fprintf(stderr, "[%s] could not parse the SPS (%zu bytes)\n", d->tag, n);
                }
                break;
            case 8:
                flags->pps = 1;
                if (n <= sizeof(d->pps) && (n != d->pps_len || memcmp(d->pps, au + s, n) != 0)) {
                    memcpy(d->pps, au + s, n);
                    d->pps_len = n;
                    d->params_dirty = true;
                    parse_pps(d, au + s, n);
                }
                break;
            case 9:
                flags->aud = 1;
                break;
            case 10:
                flags->end_of_sequence = 1;
                break;
            case 11:
                flags->end_of_stream = 1;
                break;
            case 12:
                flags->filler = 1;
                break;
            default:
                if (type == 5) flags->idr = 1;
                if (type >= 1 && type <= 5) flags->slice = 1;
                if ((type == 1 || type == 5) && !seen_slice) {
                    seen_slice = true;
                    *have_poc = slice_poc(d, au + s, n, poc);
                }
                if (type == 6) scan_sei(au + s, n, flags);
                if (!append_nal(d, &out, au + s, n)) return (size_t)-1;
                break;
            }
        }
        sc = nsc;
    }
    return out;
}

static uint64_t emitted_count(H264VtDecoder *d) {
    pthread_mutex_lock(&d->q_lock);
    uint64_t n = d->emitted;
    pthread_mutex_unlock(&d->q_lock);
    return n;
}

static CMTime to_cmtime(int64_t t) { return t == H264VT_NO_TIME ? kCMTimeInvalid : CMTimeMake(t, 90000); }

// Queues an error picture for an access unit that cannot be decoded at all. dec_lock held.
static void error_picture(H264VtDecoder *d, const AuTag *proto) {
    // An IDR starts a new sequence even when it cannot be decoded: earlier pictures go first.
    if (proto->au.idr) {
        settle_pending(d, true);
        release_all(d);
    }
    AuTag t = *proto;
    t.delivered = false;
    pthread_mutex_lock(&d->q_lock);
    deliver_locked(d, &t, NULL);
    d->errors++;
    pthread_mutex_unlock(&d->q_lock);
}

int h264vt_decode(H264VtDecoder *d, const uint8_t *au, size_t size, int64_t pts, int64_t dts, uint64_t attached,
                  int *ready) {
    if (ready) *ready = 0;
    pthread_mutex_lock(&d->dec_lock);
    const uint64_t before = emitted_count(d);
    int rc = 0;
    AuTag proto = {0};
    int32_t poc = 0;
    bool have_poc = false;
    size_t len = parse_au(d, au, size, &proto.au, &poc, &have_poc);
    proto.pts = pts;
    proto.dts = dts;
    proto.attached = attached;
    proto.sps = d->sps_info;
    proto.poc = poc;
    // Without a POC the picture cannot be placed, so it is not held back.
    proto.reorder_depth = have_poc ? d->poc_params.reorder_depth : 0;
    if (len == (size_t)-1) {
        rc = H264VT_ERR_NOMEM;
        error_picture(d, &proto);
        goto out;
    }
    if (d->params_dirty && d->sps_len && d->pps_len) {
        d->params_dirty = false;
        if (!create_session(d)) {
            // The new parameter sets are unusable; the old session must not decode their slices.
            rc = H264VT_ERR_SESSION;
            destroy_session(d);
            settle_pending(d, true);
        }
    }
    if (!proto.au.slice) goto out; // parameter sets / SEI only: no picture
    if (!d->session) {
        // No SPS/PPS seen yet, or the session could not be created: every picture of the
        // stream is an error picture, so callers still get one picture per access unit.
        if (!d->warned_no_session) {
            d->warned_no_session = true;
            fprintf(stderr, "[%s] no decoder session (parameter sets missing or rejected): error pictures\n", d->tag);
        }
        if (!rc) rc = H264VT_ERR_SESSION;
        error_picture(d, &proto);
        goto out;
    }
    // An IDR picture starts a new POC sequence: every earlier picture is shown before it.
    if (proto.au.idr) {
        VTDecompressionSessionFinishDelayedFrames(d->session);
        VTDecompressionSessionWaitForAsynchronousFrames(d->session);
        settle_pending(d, true);
        release_all(d);
    }
    AuTag *t = (AuTag *)malloc(sizeof(*t));
    if (!t) {
        rc = H264VT_ERR_NOMEM;
        error_picture(d, &proto);
        goto out;
    }
    *t = proto;
    t->refs = 2; // the decoder and VideoToolbox's callback
    CMBlockBufferRef bb = NULL;
    OSStatus st = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, NULL, len, kCFAllocatorDefault, NULL, 0, len,
                                                     kCMBlockBufferAssureMemoryNowFlag, &bb);
    if (st == noErr) st = CMBlockBufferReplaceDataBytes(d->avcc, bb, 0, len);
    CMSampleBufferRef sb = NULL;
    if (st == noErr) {
        CMSampleTimingInfo timing = {kCMTimeInvalid, to_cmtime(pts), to_cmtime(dts)};
        const size_t ssz = len;
        st = CMSampleBufferCreateReady(kCFAllocatorDefault, bb, d->fmt, 1, 1, &timing, 1, &ssz, &sb);
    }
    bool submitted = false;
    if (st == noErr) {
        VTDecodeInfoFlags info = 0;
        st = VTDecompressionSessionDecodeFrame(d->session, sb, kVTDecodeFrame_EnableTemporalProcessing, t, &info);
        submitted = true;
        if (st != noErr) {
            static int warned;
            if (warned++ < 8) fprintf(stderr, "[%s] VTDecompressionSessionDecodeFrame: %d\n", d->tag, (int)st);
            rc = H264VT_ERR_DECODE;
        }
    } else {
        rc = H264VT_ERR_NOMEM;
    }
    if (sb) CFRelease(sb);
    if (bb) CFRelease(bb);
    pthread_mutex_lock(&d->q_lock);
    if (!submitted) t->refs--; // VideoToolbox never saw the tag
    if (st != noErr && deliver_locked(d, t, NULL)) d->errors++;
    if (t->delivered) {
        tag_unref_locked(t);
    } else {
        // Decoding is synchronous, so this does not happen with VideoToolbox as observed; if
        // the picture comes later, the callback delivers it, else settle_pending() fails it.
        t->next_pending = d->pending;
        d->pending = t;
    }
    pthread_mutex_unlock(&d->q_lock);
out:
    if (ready) *ready = (int)(emitted_count(d) - before);
    pthread_mutex_unlock(&d->dec_lock);
    return rc;
}

int h264vt_finish(H264VtDecoder *d) {
    pthread_mutex_lock(&d->dec_lock);
    const uint64_t before = emitted_count(d);
    if (d->session) {
        VTDecompressionSessionFinishDelayedFrames(d->session);
        VTDecompressionSessionWaitForAsynchronousFrames(d->session);
    }
    settle_pending(d, true);
    release_all(d);
    // The sequence ended: the next picture is not "last + 1" of it (early release restarts at
    // the next IDR).
    pthread_mutex_lock(&d->q_lock);
    d->have_last_out = false;
    d->last_out_poc = 0;
    pthread_mutex_unlock(&d->q_lock);
    int n = (int)(emitted_count(d) - before);
    pthread_mutex_unlock(&d->dec_lock);
    return n;
}

void h264vt_reset(H264VtDecoder *d) {
    h264vt_finish(d);
    drop_queue(d);
}

H264VtFrame *h264vt_pop(H264VtDecoder *d) {
    pthread_mutex_lock(&d->q_lock);
    H264VtFrame *f = d->head;
    if (f) {
        d->head = f->next;
        if (!d->head) d->tail = NULL;
        d->queued--;
        f->next = NULL;
    }
    pthread_mutex_unlock(&d->q_lock);
    return f;
}

int h264vt_pop_fitting(H264VtDecoder *d, uint64_t capacity, uint64_t extra, H264VtFrame **out,
                       uint64_t *need) {
    *out = NULL;
    pthread_mutex_lock(&d->q_lock);
    H264VtFrame *f = d->head;
    if (!f) {
        pthread_mutex_unlock(&d->q_lock);
        return H264VT_POP_EMPTY;
    }
    const uint64_t n = h264vt_nv12_size(f->width, f->height) + extra;
    if (need) *need = n;
    if (capacity < n) {
        pthread_mutex_unlock(&d->q_lock);
        return H264VT_POP_TOO_SMALL;
    }
    d->head = f->next;
    if (!d->head) d->tail = NULL;
    d->queued--;
    f->next = NULL;
    pthread_mutex_unlock(&d->q_lock);
    *out = f;
    return H264VT_POP_OK;
}

int h264vt_queued(H264VtDecoder *d) {
    pthread_mutex_lock(&d->q_lock);
    int n = d->queued;
    pthread_mutex_unlock(&d->q_lock);
    return n;
}

void h264vt_frame_free(H264VtFrame *f) {
    if (!f) return;
    if (f->pixbuf) CVPixelBufferRelease((CVPixelBufferRef)f->pixbuf);
    free(f);
}

void h264vt_copy_nv12(const H264VtFrame *f, uint8_t *dst) {
    CVPixelBufferRef pb = (CVPixelBufferRef)f->pixbuf;
    if (!pb) return; // error picture: nothing decoded
    const uint32_t w = f->width, h = f->height;
    const uint32_t pitch = h264vt_pitch(w);
    const uint32_t ah = h264vt_aligned_height(h);
    uint8_t *dst_c = dst + (uint64_t)pitch * ah;
    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    const uint8_t *y = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
    const uint8_t *c = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
    const size_t ys = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
    const size_t cs = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
    for (uint32_t r = 0; r < ah; r++) memcpy(dst + (uint64_t)r * pitch, y + (size_t)(r < h ? r : h - 1) * ys, w);
    for (uint32_t r = 0; r < ah / 2; r++)
        memcpy(dst_c + (uint64_t)r * pitch, c + (size_t)(r < h / 2 ? r : h / 2 - 1) * cs, w);
    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
}

#else
#error "the H.264 decode core needs VideoToolbox (macOS)"
#endif
