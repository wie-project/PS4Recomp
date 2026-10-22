// libSceAudiodec HLE: the PS4 audio decoder library, used by Uncharted 4's movie player.
// Codec type 3 (MPEG-4 AAC-LC) is decoded with AudioToolbox's AudioConverter.
//
// Neither shadPS4 nor this repository had an implementation, and no public header documents the
// PS4 structures, so the contract below comes from Uncharted 4 (CUSA04529 v1.32) itself: the one
// caller of CreateDecoder/Decode is the audio feed of the movie player (eboot 0x16cd410, call
// sites 0x16cd7ec and 0x16cd80b); InitLibrary has one call site (0x1ce21e, next to the
// AudioOut port the movie audio plays on: 0xFF, MAIN, 0x100 frames, 48 kHz, float stereo), and
// DeleteDecoder + TermLibrary are called on the player's shutdown path (0x16cceca, 0x16cced4).
//
// ---------------------------------------------------------------------------------------------
// API contract. [V] = verified in U4's code, [I] = inferred (no reference shows it). Offsets hex.
//
// Codec type 3 is MPEG-4 AAC [V]: on its ADTS path (config 1 below) the caller checks the 12-bit
//   ADTS syncword (0xFFF) of every access unit (0x16cd720) and takes the sampling-frequency
//   index (byte 2 bits 5..2) and the channel configuration (byte 2 bit 0, byte 3 bits 7..6) from
//   the ADTS header before CreateDecoder/Decode.
//   All four calls pass the literal 3. Other types return INVALID_TYPE (U4 never uses them).
//
// SceAudiodecCtrl (0x20) [V]: 00 param ptr, 08 bsi-info ptr, 10 au-info ptr, 18 pcm-item ptr.
// Param, M4AAC (0x18) [V]: 00 u32 size 0x18, 04 u32 word size (U4: 2), 08 u32 config number
//   (U4: 1 or 2, chosen by a flag of the player), 0C u32 sampling-frequency index (U4: from the
//   ADTS header, or from its own stream info in config 2), 10 u32 max channels (U4: 2),
//   14 u32 enable HE-AAC (U4: 0).
//   Word size: 2 = float [V: U4 reads the PCM as floats]; 0 = s16 and 1 = s32 [I: libSceAjm's
//   format numbering]. Others -> INVALID_WORD_LENGTH.
//   Config 1 = ADTS [V]: the access unit starts with the ADTS header (U4 passes the frame from
//   the syncword on). Config 2 = raw [V: U4 then passes its buffer as is, with the frequency
//   index from its stream info and no ADTS check]; the channel count comes from the first
//   syntactic element of each access unit (CPE = 2, else 1) [I]; a stereo frame that starts with
//   a DSE/PCE/FIL element would be taken for mono. Others -> UNSUPPORTED.
//   HE-AAC (SBR) is not implemented: enable HE-AAC != 0 -> UNSUPPORTED [I].
// BsiInfo, M4AAC (0x14) [V size]: 00 u32 size, then written by Decode [I, U4 never reads it]:
//   04 u32 sampling frequency (Hz), 08 u32 channels, 0C u32 samples per channel of the last
//   access unit, 10 u32 HE-AAC flag (0).
// AuInfo (0x18) [V]: 00 u32 size 0x18, 08 au address, 10 u32 au size. Decode writes the number
//   of bytes it consumed to au size [I]; harmless for U4, which rewrites the au fields before
//   every call (0x16cd699, 0x16cd6c7, 0x16cd709) [V].
// PcmItem (0x18) [V]: 00 u32 size 0x18, 08 pcm address, 10 u32 pcm buffer size (U4: 0x4000).
//   Decode writes the bytes of PCM it produced [V: U4 reads it back and plays size / 8 stereo or
//   size / 4 mono float frames]. PCM is interleaved, one AAC frame (1024 samples per channel).
// Every struct size must match exactly, else INVALID_SIZE [I].
//
// Calls:
//   InitLibrary(type) / TermLibrary(type): reference count per type [I]; Term without Init ->
//     NOT_INITIALIZED [I]. U4 ignores both results [V].
//   CreateDecoder(ctrl, type) -> handle in eax [V]. U4 stores a negative result as 0 and treats
//     0 as "no decoder yet", calling CreateDecoder again before the next access unit [V], so
//     handles are >= 1. CreateDecoder does not require InitLibrary [I: InitLibrary and
//     TermLibrary sit in different paths of U4 (see above); if the player starts again after a
//     shutdown, a strict rule would refuse its decoder]. Only the param struct is used
//     (validated, then kept); the decoder is built at the first access unit, which fixes the
//     sampling rate and channel count.
//   Decode(handle, ctrl) -> 0 or an error [V: non-zero is U4's error path]. One access unit per
//     call: in ADTS mode exactly one ADTS frame (frame_length bytes) is consumed, the au size may
//     be larger. Channels above the param's max channels -> CH_SHORTAGE [I]; PCM buffer too small
//     for 1024 samples per channel -> INVALID_SIZE [I]; a frame AudioToolbox rejects -> API_FAIL,
//     the decoder stays usable. A different sampling rate or channel count in a later ADTS
//     header rebuilds the decoder [I].
//   DeleteDecoder(handle): frees it; a Decode running on another thread keeps the decoder alive
//     until it returns. Handles carry a generation, so a stale one is never a newer decoder.
// Error codes: the PS4 values are not documented; the Vita SDK's libSceAudiodec numbering
//   (0x807F00xx) is used [I]. U4 only tests "< 0" / "!= 0" [V].
// ---------------------------------------------------------------------------------------------
#include "ps4_audiodec.h"

#include <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIODEC_MAX_DECODERS 16
// Handle = generation << AUDIODEC_SLOT_BITS | (slot + 1): slot + 1 (1..16) needs 5 bits.
#define AUDIODEC_SLOT_BITS 5
#define AUDIODEC_FRAME 1024u
#define AUDIODEC_MAX_CH 2u

#define AUDIODEC_WORD_S16 0u
#define AUDIODEC_WORD_S32 1u
#define AUDIODEC_WORD_FLOAT 2u

#define AUDIODEC_CONFIG_ADTS 1u
#define AUDIODEC_CONFIG_RAW 2u

typedef struct {
    uint64_t param, bsi, au, pcm;
} AudiodecCtrl;

typedef struct {
    uint32_t size, word_size, config_number, sampling_freq_index, max_channels, enable_heaac;
} AudiodecParamM4aac;

typedef struct {
    uint32_t size, sampling_freq, channels, samples, heaac;
} AudiodecBsiM4aac;

typedef struct {
    uint32_t size, pad;
    uint64_t addr;
    uint32_t data_size, pad2;
} AudiodecBuffer; // AuInfo and PcmItem share this layout

_Static_assert(sizeof(AudiodecCtrl) == 0x20, "ctrl");
_Static_assert(sizeof(AudiodecParamM4aac) == 0x18, "param");
_Static_assert(sizeof(AudiodecBsiM4aac) == 0x14, "bsi");
_Static_assert(sizeof(AudiodecBuffer) == 0x18, "au/pcm");
_Static_assert(AUDIODEC_MAX_DECODERS < (1 << AUDIODEC_SLOT_BITS), "slot + 1 must fit the slot field");

static const uint32_t k_rates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                     22050, 16000, 12000, 11025, 8000,  7350};

typedef struct Audiodec {
    pthread_mutex_t lock;
    int refs;   // table reference + calls in progress; guarded by g_table_lock
    int slot;
    uint32_t id;
    AudiodecParamM4aac param;
    AudioConverterRef conv; // NULL until the first access unit
    uint32_t rate, channels;
    uint32_t frames; // samples per channel of the last access unit
    // Input of the converter's callback: one packet.
    const uint8_t *in_data;
    uint32_t in_size;
    bool in_taken;
    AudioStreamPacketDescription in_desc;
    float out[AUDIODEC_FRAME * AUDIODEC_MAX_CH];
} Audiodec;

static pthread_mutex_t g_table_lock = PTHREAD_MUTEX_INITIALIZER;
static Audiodec *g_decoders[AUDIODEC_MAX_DECODERS];
static uint32_t g_generation;
static int g_init_count;
static bool g_logged_create;

#define AUDIODEC_RETURN(code)                                                                  \
    do {                                                                                       \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(code);                                         \
        SHIM_RETURN();                                                                         \
    } while (0)

// Guest range [addr, addr + size) inside guest memory, without wrapping around; NULL otherwise.
static void *grange(GuestContext *ctx, uint64_t addr, uint64_t size) {
    if (!addr || addr > ctx->mem_size || size > ctx->mem_size - addr) return NULL;
    return ctx->mem_base + addr;
}

static Audiodec *audiodec_acquire(uint64_t handle) {
    if (handle == 0 || handle > 0x7fffffffu) return NULL;
    uint32_t id = (uint32_t)handle;
    uint32_t slot = (id & ((1u << AUDIODEC_SLOT_BITS) - 1)) - 1;
    if (slot >= AUDIODEC_MAX_DECODERS) return NULL;
    pthread_mutex_lock(&g_table_lock);
    Audiodec *d = g_decoders[slot];
    if (d && d->id == id)
        d->refs++;
    else
        d = NULL;
    pthread_mutex_unlock(&g_table_lock);
    return d;
}

static void audiodec_release(Audiodec *d) {
    pthread_mutex_lock(&g_table_lock);
    const bool last = --d->refs == 0;
    pthread_mutex_unlock(&g_table_lock);
    if (!last) return;
    if (d->conv) AudioConverterDispose(d->conv);
    pthread_mutex_destroy(&d->lock);
    free(d);
}

// ---------------------------------------------------------------------------------------------
// AAC bitstream helpers
// ---------------------------------------------------------------------------------------------

typedef struct {
    uint32_t object_type; // MPEG-4 audio object type (2 = AAC-LC)
    uint32_t freq_index, channels;
    uint32_t header_size, frame_size;
    uint32_t raw_blocks; // number_of_raw_data_blocks_in_frame + 1
} AdtsHeader;

static bool adts_parse(const uint8_t *p, uint32_t size, AdtsHeader *h) {
    if (size < 7 || p[0] != 0xFF || (p[1] & 0xF0) != 0xF0) return false;
    if (p[1] & 0x06) return false; // layer must be 0
    const bool crc = !(p[1] & 1);
    h->object_type = (p[2] >> 6) + 1u;
    h->freq_index = (p[2] >> 2) & 0xF;
    h->channels = ((p[2] & 1u) << 2) | (p[3] >> 6);
    h->frame_size = ((p[3] & 3u) << 11) | ((uint32_t)p[4] << 3) | (p[5] >> 5);
    h->raw_blocks = (p[6] & 3u) + 1u;
    h->header_size = crc ? 9u : 7u;
    return h->freq_index < 13 && h->frame_size > h->header_size;
}

// Magic cookie for AudioConverter: an MPEG-4 ES_Descriptor (the payload of an 'esds' box) that
// carries the 2-byte AudioSpecificConfig.
static uint32_t make_esds(uint8_t *out, uint32_t object_type, uint32_t freq_index, uint32_t channels) {
    const uint8_t asc0 = (uint8_t)((object_type << 3) | (freq_index >> 1));
    const uint8_t asc1 = (uint8_t)(((freq_index & 1) << 7) | (channels << 3));
    const uint8_t esds[] = {
        0x03, 0x19, 0x00, 0x00, 0x00,                   // ES_Descriptor, ES_ID 0, no flags
        0x04, 0x11, 0x40, 0x15, 0x00, 0x00, 0x00,       // DecoderConfig: MPEG-4 audio, audio stream
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // max / average bitrate unknown
        0x05, 0x02, asc0, asc1,                         // DecoderSpecificInfo = AudioSpecificConfig
        0x06, 0x01, 0x02,                               // SLConfig: predefined MP4
    };
    memcpy(out, esds, sizeof(esds));
    return (uint32_t)sizeof(esds);
}

static OSStatus audiodec_input(AudioConverterRef conv, UInt32 *packets, AudioBufferList *data,
                               AudioStreamPacketDescription **desc, void *user) {
    (void)conv;
    Audiodec *d = (Audiodec *)user;
    if (d->in_taken) {
        *packets = 0;
        return 'pend'; // no more input for this call; the converter keeps its state
    }
    d->in_taken = true;
    data->mNumberBuffers = 1;
    data->mBuffers[0].mData = (void *)d->in_data;
    data->mBuffers[0].mDataByteSize = d->in_size;
    data->mBuffers[0].mNumberChannels = d->channels;
    d->in_desc.mStartOffset = 0;
    d->in_desc.mVariableFramesInPacket = 0;
    d->in_desc.mDataByteSize = d->in_size;
    if (desc) *desc = &d->in_desc;
    *packets = 1;
    return noErr;
}

static int32_t audiodec_open_converter(Audiodec *d, uint32_t freq_index, uint32_t channels) {
    if (d->conv) {
        if (d->rate == k_rates[freq_index] && d->channels == channels) return 0;
        AudioConverterDispose(d->conv);
        d->conv = NULL;
    }
    AudioStreamBasicDescription in = {0}, out = {0};
    in.mSampleRate = k_rates[freq_index];
    in.mFormatID = kAudioFormatMPEG4AAC;
    in.mFormatFlags = kMPEG4Object_AAC_LC;
    in.mFramesPerPacket = AUDIODEC_FRAME;
    in.mChannelsPerFrame = channels;
    out.mSampleRate = in.mSampleRate;
    out.mFormatID = kAudioFormatLinearPCM;
    out.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    out.mBitsPerChannel = 32;
    out.mChannelsPerFrame = channels;
    out.mFramesPerPacket = 1;
    out.mBytesPerFrame = out.mBytesPerPacket = 4 * channels;
    OSStatus st = AudioConverterNew(&in, &out, &d->conv);
    if (st != noErr) {
        fprintf(stderr, "[libSceAudiodec] AudioConverterNew(%u Hz, %u ch) failed: %d\n", k_rates[freq_index], channels,
                (int)st);
        d->conv = NULL;
        return PS4_AUDIODEC_ERROR_API_FAIL;
    }
    uint8_t cookie[32];
    const uint32_t n = make_esds(cookie, 2, freq_index, channels);
    st = AudioConverterSetProperty(d->conv, kAudioConverterDecompressionMagicCookie, n, cookie);
    if (st != noErr) {
        fprintf(stderr, "[libSceAudiodec] magic cookie rejected: %d\n", (int)st);
        AudioConverterDispose(d->conv);
        d->conv = NULL;
        return PS4_AUDIODEC_ERROR_API_FAIL;
    }
    d->rate = k_rates[freq_index];
    d->channels = channels;
    return 0;
}

// Decodes one raw AAC frame into d->out (interleaved float, d->channels channels).
static int32_t audiodec_decode_raw(Audiodec *d, const uint8_t *data, uint32_t size) {
    d->in_data = data;
    d->in_size = size;
    d->in_taken = false;
    AudioBufferList list;
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = d->channels;
    list.mBuffers[0].mDataByteSize = (UInt32)(AUDIODEC_FRAME * d->channels * sizeof(float));
    list.mBuffers[0].mData = d->out;
    UInt32 frames = AUDIODEC_FRAME;
    OSStatus st = AudioConverterFillComplexBuffer(d->conv, audiodec_input, d, &frames, &list, NULL);
    if (st != noErr && st != 'pend') {
        static _Atomic int warned;
        if (warned++ < 8) fprintf(stderr, "[libSceAudiodec] frame of %u bytes rejected: %d\n", size, (int)st);
        // Start the next frame from a clean decoder state.
        AudioConverterReset(d->conv);
        return PS4_AUDIODEC_ERROR_API_FAIL;
    }
    d->frames = frames;
    return 0;
}

static void store_pcm(void *dst, const float *src, uint32_t n, uint32_t word_size) {
    if (word_size == AUDIODEC_WORD_FLOAT) {
        memcpy(dst, src, n * sizeof(float));
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        float v = src[i];
        if (!(v > -1.0f)) v = -1.0f; // also NaN
        if (v > 1.0f) v = 1.0f;
        if (word_size == AUDIODEC_WORD_S16) {
            int32_t s = (int32_t)lrintf(v * 32767.0f);
            int16_t s16 = (int16_t)s;
            memcpy((uint8_t *)dst + 2 * i, &s16, 2);
        } else {
            int32_t s = (int32_t)llrint((double)v * 2147483647.0);
            memcpy((uint8_t *)dst + 4 * i, &s, 4);
        }
    }
}

static uint32_t word_bytes(uint32_t word_size) { return word_size == AUDIODEC_WORD_S16 ? 2u : 4u; }

// Reads and validates the ctrl block and its param struct.
static int32_t read_param(GuestContext *ctx, uint64_t ctrl_addr, AudiodecCtrl *ctrl, AudiodecParamM4aac *p) {
    const AudiodecCtrl *c = (const AudiodecCtrl *)grange(ctx, ctrl_addr, sizeof(*c));
    if (!c) return PS4_AUDIODEC_ERROR_INVALID_PTR;
    *ctrl = *c;
    const AudiodecParamM4aac *gp = (const AudiodecParamM4aac *)grange(ctx, ctrl->param, sizeof(*gp));
    if (!gp) return PS4_AUDIODEC_ERROR_INVALID_PTR;
    *p = *gp;
    if (p->size != sizeof(*p)) return PS4_AUDIODEC_ERROR_INVALID_SIZE;
    if (p->word_size > AUDIODEC_WORD_FLOAT) return PS4_AUDIODEC_ERROR_INVALID_WORD_LENGTH;
    if (p->config_number != AUDIODEC_CONFIG_ADTS && p->config_number != AUDIODEC_CONFIG_RAW)
        return PS4_AUDIODEC_ERROR_UNSUPPORTED;
    if (p->sampling_freq_index >= 13 || p->max_channels == 0 || p->max_channels > 8)
        return PS4_AUDIODEC_ERROR_INVALID_INIT_PARAM;
    if (p->enable_heaac) return PS4_AUDIODEC_ERROR_UNSUPPORTED;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Guest shims
// ---------------------------------------------------------------------------------------------

// int sceAudiodecInitLibrary(uint32_t codecType)
void shim_sceAudiodecInitLibrary(GuestContext *ctx) {
    const uint32_t type = (uint32_t)ctx->rdi;
    if (type != PS4_AUDIODEC_TYPE_M4AAC) {
        fprintf(stderr, "[libSceAudiodec] InitLibrary: codec type %u is not supported (M4AAC = 3 only)\n", type);
        AUDIODEC_RETURN(PS4_AUDIODEC_ERROR_INVALID_TYPE);
    }
    pthread_mutex_lock(&g_table_lock);
    g_init_count++;
    pthread_mutex_unlock(&g_table_lock);
    AUDIODEC_RETURN(0);
}

// int sceAudiodecTermLibrary(uint32_t codecType)
void shim_sceAudiodecTermLibrary(GuestContext *ctx) {
    if ((uint32_t)ctx->rdi != PS4_AUDIODEC_TYPE_M4AAC) AUDIODEC_RETURN(PS4_AUDIODEC_ERROR_INVALID_TYPE);
    pthread_mutex_lock(&g_table_lock);
    const bool ok = g_init_count > 0;
    if (ok) g_init_count--;
    pthread_mutex_unlock(&g_table_lock);
    AUDIODEC_RETURN(ok ? 0 : PS4_AUDIODEC_ERROR_NOT_INITIALIZED);
}

static void log_first_create(GuestContext *ctx, const AudiodecCtrl *c, uint32_t type) {
    pthread_mutex_lock(&g_table_lock);
    const bool first = !g_logged_create;
    g_logged_create = true;
    pthread_mutex_unlock(&g_table_lock);
    if (!first) return;
    fprintf(stderr, "[libSceAudiodec] first CreateDecoder: type %u, ctrl {param 0x%llx, bsi 0x%llx, au 0x%llx, pcm 0x%llx}\n",
            type, (unsigned long long)c->param, (unsigned long long)c->bsi, (unsigned long long)c->au,
            (unsigned long long)c->pcm);
    const uint32_t *p = (const uint32_t *)grange(ctx, c->param, sizeof(AudiodecParamM4aac));
    if (p)
        fprintf(stderr, "[libSceAudiodec]   param {size 0x%x, word %u, config %u, freq index %u, max ch %u, heaac %u}\n",
                p[0], p[1], p[2], p[3], p[4], p[5]);
    const uint32_t *b = (const uint32_t *)grange(ctx, c->bsi, 4);
    if (b) fprintf(stderr, "[libSceAudiodec]   bsi {size 0x%x}\n", b[0]);
    const AudiodecBuffer *au = (const AudiodecBuffer *)grange(ctx, c->au, sizeof(AudiodecBuffer));
    if (au) {
        fprintf(stderr, "[libSceAudiodec]   au {size 0x%x, addr 0x%llx, bytes %u}", au->size,
                (unsigned long long)au->addr, au->data_size);
        const uint32_t n = au->data_size < 16 ? au->data_size : 16;
        const uint8_t *s = (const uint8_t *)grange(ctx, au->addr, n);
        if (s) {
            fprintf(stderr, ", stream:");
            for (uint32_t i = 0; i < n; i++) fprintf(stderr, " %02x", s[i]);
        }
        fprintf(stderr, "\n");
    }
    const AudiodecBuffer *pcm = (const AudiodecBuffer *)grange(ctx, c->pcm, sizeof(AudiodecBuffer));
    if (pcm)
        fprintf(stderr, "[libSceAudiodec]   pcm {size 0x%x, addr 0x%llx, bytes 0x%x}\n", pcm->size,
                (unsigned long long)pcm->addr, pcm->data_size);
}

// int sceAudiodecCreateDecoder(SceAudiodecCtrl *ctrl, uint32_t codecType); returns the handle
void shim_sceAudiodecCreateDecoder(GuestContext *ctx) {
    const uint32_t type = (uint32_t)ctx->rsi;
    AudiodecCtrl c;
    AudiodecParamM4aac p;
    const AudiodecCtrl *gc = (const AudiodecCtrl *)grange(ctx, ctx->rdi, sizeof(*gc));
    if (gc) log_first_create(ctx, gc, type);
    if (type != PS4_AUDIODEC_TYPE_M4AAC) AUDIODEC_RETURN(PS4_AUDIODEC_ERROR_INVALID_TYPE);
    const int32_t rc = read_param(ctx, ctx->rdi, &c, &p);
    if (rc) {
        fprintf(stderr, "[libSceAudiodec] CreateDecoder: invalid param (0x%08x)\n", (uint32_t)rc);
        AUDIODEC_RETURN(rc);
    }
    Audiodec *d = (Audiodec *)calloc(1, sizeof(*d));
    if (!d) AUDIODEC_RETURN(PS4_AUDIODEC_ERROR_API_FAIL);
    pthread_mutex_init(&d->lock, NULL);
    d->param = p;
    d->refs = 1;
    int slot = -1;
    pthread_mutex_lock(&g_table_lock);
    for (int i = 0; i < AUDIODEC_MAX_DECODERS; i++)
        if (!g_decoders[i]) {
            slot = i;
            g_generation = (g_generation + 1) & ((1u << (31 - AUDIODEC_SLOT_BITS)) - 1);
            d->id = (g_generation << AUDIODEC_SLOT_BITS) | (uint32_t)(i + 1);
            d->slot = i;
            g_decoders[i] = d;
            break;
        }
    pthread_mutex_unlock(&g_table_lock);
    if (slot < 0) {
        pthread_mutex_destroy(&d->lock);
        free(d);
        AUDIODEC_RETURN(PS4_AUDIODEC_ERROR_ALL_HANDLES_IN_USE);
    }
    fprintf(stderr, "[libSceAudiodec] decoder 0x%x: M4AAC, %s, freq index %u, max %u ch, word size %u\n", d->id,
            p.config_number == AUDIODEC_CONFIG_ADTS ? "ADTS" : "raw", p.sampling_freq_index, p.max_channels,
            p.word_size);
    AUDIODEC_RETURN(d->id);
}

static int32_t audiodec_decode(GuestContext *ctx, Audiodec *d, uint64_t ctrl_addr) {
    const AudiodecCtrl *c = (const AudiodecCtrl *)grange(ctx, ctrl_addr, sizeof(*c));
    if (!c) return PS4_AUDIODEC_ERROR_INVALID_PTR;
    AudiodecBuffer *au = (AudiodecBuffer *)grange(ctx, c->au, sizeof(*au));
    AudiodecBuffer *pcm = (AudiodecBuffer *)grange(ctx, c->pcm, sizeof(*pcm));
    if (!au || !pcm) return PS4_AUDIODEC_ERROR_INVALID_PTR;
    if (au->size != sizeof(*au) || pcm->size != sizeof(*pcm)) return PS4_AUDIODEC_ERROR_INVALID_SIZE;
    AudiodecBsiM4aac *bsi = NULL;
    if (c->bsi) {
        bsi = (AudiodecBsiM4aac *)grange(ctx, c->bsi, sizeof(*bsi));
        if (!bsi) return PS4_AUDIODEC_ERROR_INVALID_PTR;
        if (bsi->size != sizeof(*bsi)) return PS4_AUDIODEC_ERROR_INVALID_SIZE;
    }
    const uint8_t *data = (const uint8_t *)grange(ctx, au->addr, au->data_size);
    if (!data || au->data_size == 0) return PS4_AUDIODEC_ERROR_INVALID_PTR;

    uint32_t freq_index = d->param.sampling_freq_index, channels, consumed;
    const uint8_t *frame;
    uint32_t frame_size;
    if (d->param.config_number == AUDIODEC_CONFIG_ADTS) {
        AdtsHeader h;
        if (!adts_parse(data, au->data_size, &h) || h.frame_size > au->data_size) return PS4_AUDIODEC_ERROR_INVALID_SIZE;
        if (h.object_type != 2 || h.raw_blocks != 1 || h.channels == 0) {
            static _Atomic int warned;
            if (warned++ < 4)
                fprintf(stderr, "[libSceAudiodec] ADTS frame not supported: object type %u, %u raw blocks, channel config %u\n",
                        h.object_type, h.raw_blocks, h.channels);
            return PS4_AUDIODEC_ERROR_UNSUPPORTED;
        }
        freq_index = h.freq_index;
        channels = h.channels;
        frame = data + h.header_size;
        frame_size = h.frame_size - h.header_size;
        consumed = h.frame_size;
    } else {
        // The first syntactic element of the raw_data_block, per access unit: ID_CPE (1) = 2
        // channels, anything else (ID_SCE, or a leading DSE/PCE/FIL) = 1. A change rebuilds the
        // decoder, as a changed ADTS header does.
        channels = (data[0] >> 5) == 1 ? 2u : 1u;
        frame = data;
        frame_size = au->data_size;
        consumed = au->data_size;
    }
    if (channels > d->param.max_channels || channels > AUDIODEC_MAX_CH) return PS4_AUDIODEC_ERROR_CH_SHORTAGE;
    const uint32_t need = AUDIODEC_FRAME * channels * word_bytes(d->param.word_size);
    void *out = grange(ctx, pcm->addr, pcm->data_size);
    if (!out) return PS4_AUDIODEC_ERROR_INVALID_PTR;
    if (pcm->data_size < need) return PS4_AUDIODEC_ERROR_INVALID_SIZE;

    int32_t rc = audiodec_open_converter(d, freq_index, channels);
    if (rc) return rc;
    rc = audiodec_decode_raw(d, frame, frame_size);
    if (rc) return rc;
    store_pcm(out, d->out, d->frames * d->channels, d->param.word_size);
    pcm->data_size = d->frames * d->channels * word_bytes(d->param.word_size);
    au->data_size = consumed;
    if (bsi) {
        bsi->sampling_freq = d->rate;
        bsi->channels = d->channels;
        bsi->samples = d->frames;
        bsi->heaac = 0;
    }
    return 0;
}

// int sceAudiodecDecode(int32_t handle, SceAudiodecCtrl *ctrl)
void shim_sceAudiodecDecode(GuestContext *ctx) {
    Audiodec *d = audiodec_acquire(ctx->rdi);
    if (!d) AUDIODEC_RETURN(PS4_AUDIODEC_ERROR_INVALID_HANDLE);
    pthread_mutex_lock(&d->lock);
    const int32_t rc = audiodec_decode(ctx, d, ctx->rsi);
    pthread_mutex_unlock(&d->lock);
    audiodec_release(d);
    AUDIODEC_RETURN(rc);
}

// int sceAudiodecDeleteDecoder(int32_t handle)
void shim_sceAudiodecDeleteDecoder(GuestContext *ctx) {
    Audiodec *d = audiodec_acquire(ctx->rdi);
    if (!d) AUDIODEC_RETURN(PS4_AUDIODEC_ERROR_INVALID_HANDLE);
    pthread_mutex_lock(&g_table_lock);
    const bool mine = g_decoders[d->slot] == d; // a concurrent Delete may have won
    if (mine) {
        g_decoders[d->slot] = NULL;
        d->refs--; // the table's reference
    }
    pthread_mutex_unlock(&g_table_lock);
    audiodec_release(d);
    AUDIODEC_RETURN(mine ? 0 : PS4_AUDIODEC_ERROR_INVALID_HANDLE);
}
