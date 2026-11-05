// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_FONT_H
#define PS4_FONT_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
#define ORBIS_FONT_OK                         0
#define ORBIS_FONT_ERROR_INVALID_PARAMETER    ((int32_t)0x80540001)
#define ORBIS_FONT_ERROR_INVALID_MEMORY       ((int32_t)0x80540002)
#define ORBIS_FONT_ERROR_ALLOCATION_FAILED    ((int32_t)0x80540003)
#define ORBIS_FONT_ERROR_INVALID_LIBRARY      ((int32_t)0x80540004)
#define ORBIS_FONT_ERROR_INVALID_FONT_HANDLE  ((int32_t)0x80540005)
#define ORBIS_FONT_ERROR_INVALID_RENDERER     ((int32_t)0x80540006)
#define ORBIS_FONT_ERROR_NO_SUPPORT_FUNCTION  ((int32_t)0x80540007)
#define ORBIS_FONT_ERROR_NO_SUPPORT_FONTSET   ((int32_t)0x80540008)
#define ORBIS_FONT_ERROR_FONT_OPEN_FAILED     ((int32_t)0x80540009)
#define ORBIS_FONT_ERROR_INVALID_TEXT_SOURCE  ((int32_t)0x8054000a)
#define ORBIS_FONT_ERROR_INVALID_STRING       ((int32_t)0x8054000b)
#define ORBIS_FONT_ERROR_ALREADY_SPECIFIED    ((int32_t)0x8054000c)
#define ORBIS_FONT_ERROR_NO_SUPPORT_CODE      ((int32_t)0x80540010)

typedef void *OrbisFontLib;
typedef void *OrbisFontHandle;
typedef void *OrbisFontRenderer;
typedef void *OrbisFontString;
typedef void *OrbisFontWritingLine;

typedef struct OrbisFontMemInterface {
    void *(*alloc)(void *ctx, size_t size);
    void (*dealloc)(void *ctx, void *ptr);
    void *(*realloc_fn)(void *ctx, void *ptr, size_t size);
    void *(*calloc_fn)(void *ctx, size_t num, size_t size);
    void *(*mspace_create)(void *parent, void *base, size_t size);
    void (*mspace_destroy)(void *parent, void *mspace);
} OrbisFontMemInterface;

typedef void (*OrbisFontMemDestroyCb)(void *fontMemory, void *object, void *destroyArg);

typedef struct OrbisFontMem {
    uint16_t mem_kind;
    uint16_t attr_bits;
    uint32_t region_size;
    void *region_base;
    void *mspace_handle;
    const OrbisFontMemInterface *iface;
    OrbisFontMemDestroyCb on_destroy;
    void *destroy_ctx;
    void *some_ctx1;
    void *some_ctx2;
} OrbisFontMem;

typedef struct OrbisFontOpenParams {
    uint16_t tag;
    uint16_t pad16;
    uint32_t flags;
    uint32_t subfont_index;
    int32_t unique_id;
    const void *reserved_ptr2;
    const void *reserved_ptr1;
} OrbisFontOpenParams;

typedef struct OrbisFontGlyphMetrics {
    float width;
    float height;
    struct {
        float bearingX;
        float bearingY;
        float advance;
    } Horizontal;
    struct {
        float bearingX;
        float bearingY;
        float advance;
    } Vertical;
} OrbisFontGlyphMetrics;

typedef struct OrbisFontRenderOutput {
    uint8_t reserved[64];
} OrbisFontRenderOutput;

typedef struct OrbisFontRenderSurface {
    void *buffer;
    int32_t widthByte;
    int8_t pixelSizeByte;
    uint8_t pad0;
    uint8_t styleFlag;
    uint8_t pad2;
    int32_t width;
    int32_t height;
    uint32_t sc_x0;
    uint32_t sc_y0;
    uint32_t sc_x1;
    uint32_t sc_y1;
    uint64_t reserved_q[11];
} OrbisFontRenderSurface;

typedef struct OrbisFontStyleFrame {
    uint16_t magic;
    uint8_t flags1;
    uint8_t flags2;
    uint32_t hDpi;
    uint32_t vDpi;
    uint32_t scaleUnit;
    float baseScale;
    float scalePixelW;
    float scalePixelH;
    float effectWeightX;
    float effectWeightY;
    float slantRatio;
    uint32_t reserved_0x28;
    uint32_t layout_cache_state;
    uint32_t cache_flags_and_direction;
    uint32_t cache_lock_word;
    uint8_t layout_cache_bytes[0x20];
    uint32_t reserved_0x58;
    uint32_t cached_scalar_bits;
} OrbisFontStyleFrame;

typedef struct OrbisFontTextCharacter {
    struct OrbisFontTextCharacter *prev;       // 0x00
    struct OrbisFontTextCharacter *next;       // 0x08
    void *textOrder;                           // 0x10
    OrbisFontHandle font;                      // 0x18
    void *shapeEntry;                          // 0x20
    uint32_t characterCode;                    // 0x28
    uint8_t reserved_0x2c[4];                  // 0x2c
    uint8_t clusterSpan;                       // 0x30
    uint8_t clusterIndex;                      // 0x31
    uint8_t clusterKind;                       // 0x32
    int8_t synthetic;                          // 0x33
    uint8_t reserved_0x34[4];                  // 0x34
    uint64_t flags;                            // 0x38
    uint8_t reserved_0x40[136];                // 0x40
} OrbisFontTextCharacter;
_Static_assert(sizeof(OrbisFontTextCharacter) == 0xC8, "OrbisFontTextCharacter size must be 200 bytes");
_Static_assert(offsetof(OrbisFontTextCharacter, font) == 0x18, "font offset must be 0x18");
_Static_assert(offsetof(OrbisFontTextCharacter, characterCode) == 0x28, "characterCode offset must be 0x28");
_Static_assert(offsetof(OrbisFontTextCharacter, flags) == 0x38, "flags offset must be 0x38");


typedef union OrbisFontTextParseResult {
    void *reserved[4];
    struct {
        OrbisFontHandle font;
        uint32_t code;
    } FontCode;
    struct {
        void *reserved;
        uint32_t terminateCode;
    } Terminate;
    struct {
        void *reserved;
        int32_t errorCode;
    } Error;
} OrbisFontTextParseResult;

struct OrbisFontTextSource;
typedef int32_t (*OrbisFontTextParseFunction)(struct OrbisFontTextSource *source,
                                              void **order,
                                              OrbisFontTextParseResult *result);
typedef OrbisFontTextParseFunction OrbisFontTextParser;

typedef struct OrbisFontTextSource {
    uint64_t systemUse0;
    const void *start;
    const void *end;
    const void *current;
    OrbisFontTextParseFunction textParser;
    void *textObject;
    OrbisFontHandle defaultFont;
    void *systemUse[5];
} OrbisFontTextSource;

typedef struct OrbisFontCreateStringDetail {
    uint16_t detailId;
    uint8_t detailType;
    uint8_t detections;
    uint32_t ordersOption;
    OrbisFontHandle defaultFont;
    union {
        struct {
            void *function;
            void *object;
        } Callback;
    } Orders;
} OrbisFontCreateStringDetail;

typedef struct OrbisFontWriting {
    void *systemUse[32];
} OrbisFontWriting;

typedef struct OrbisFontWritingProfile {
    uint32_t characterCount : 8;
    uint32_t invisibleGlyph : 1;
    uint32_t reserved : 23;
} OrbisFontWritingProfile;

typedef struct OrbisFontWritingStep {
    float x;
    float y;
    float advanceX;
    float advanceY;
    OrbisFontHandle font;
    OrbisFontWritingProfile Profile;
    uint32_t glyphCode;
    struct {
        float x;
        float y;
    } Positioning;
    OrbisFontGlyphMetrics GlyphMetrics;
} OrbisFontWritingStep;

typedef struct OrbisFontWritingLetterStep {
    float x;
    float y;
    float advanceX;
    float advanceY;
    struct {
        uint32_t textsCount;
        uint32_t textsIndex;
        uint32_t glyphsCount;
        uint32_t glyphsIndex;
        uint8_t baseComponentsCount;
        uint8_t baseComponentsIndex;
        uint8_t baseFocusCount;
        uint8_t baseFocusIndex;
        uint8_t oppositeDirection;
        uint8_t characterTextCount;
        uint8_t baseTextCount;
        uint8_t markTextCount;
        uint32_t marksTextCount;
        uint32_t marksTextNumber;
        uint32_t reserved[3];
        int32_t textLetterOffset;
    } Components;
} OrbisFontWritingLetterStep;

typedef struct OrbisFontWritingMetrics {
    float advanceX;
    float advanceY;
    struct {
        float top;
        float bottom;
        float left;
        float right;
    } Extent;
} OrbisFontWritingMetrics;

// Guest ABI shims for libSceFont
void shim_sceFontMemoryInit(GuestContext *ctx);
void shim_sceFontMemoryTerm(GuestContext *ctx);
void shim_sceFontCreateLibraryWithEdition(GuestContext *ctx);
void shim_sceFontDestroyLibrary(GuestContext *ctx);
void shim_sceFontSupportSystemFonts(GuestContext *ctx);
void shim_sceFontSupportExternalFonts(GuestContext *ctx);
void shim_sceFontCreateRendererWithEdition(GuestContext *ctx);
void shim_sceFontDestroyRenderer(GuestContext *ctx);
void shim_sceFontAttachDeviceCacheBuffer(GuestContext *ctx);
void shim_sceFontOpenFontSet(GuestContext *ctx);
void shim_sceFontCloseFont(GuestContext *ctx);
void shim_sceFontBindRenderer(GuestContext *ctx);
void shim_sceFontUnbindRenderer(GuestContext *ctx);
void shim_sceFontRebindRenderer(GuestContext *ctx);
void shim_sceFontSetupRenderScalePixel(GuestContext *ctx);
void shim_sceFontSetScalePixel(GuestContext *ctx);
void shim_sceFontRenderSurfaceInit(GuestContext *ctx);
void shim_sceFontTextSourceInit(GuestContext *ctx);
void shim_sceFontTextSourceSetDefaultFont(GuestContext *ctx);
void shim_sceFontTextSourceSetWritingForm(GuestContext *ctx);
void shim_sceFontCreateString(GuestContext *ctx);
void shim_sceFontDestroyString(GuestContext *ctx);
void shim_sceFontStringRefersTextCharacters(GuestContext *ctx);
void shim_sceFontStringGetTerminateOrder(GuestContext *ctx);
void shim_sceFontStringGetTerminateCode(GuestContext *ctx);
void shim_sceFontStringRefersRenderCharacters(GuestContext *ctx);
void shim_sceFontStringGetWritingForm(GuestContext *ctx);
void shim_sceFontCreateWritingLine(GuestContext *ctx);
void shim_sceFontDestroyWritingLine(GuestContext *ctx);
void shim_sceFontWritingLineClear(GuestContext *ctx);
void shim_sceFontWritingLineGetOrderingSpace(GuestContext *ctx);
void shim_sceFontWritingLineGetRenderMetrics(GuestContext *ctx);
void shim_sceFontWritingLineWritesOrder(GuestContext *ctx);
void shim_sceFontWritingLineRefersRenderStep(GuestContext *ctx);
void shim_sceFontWritingInit(GuestContext *ctx);
void shim_sceFontWritingGetRenderMetrics(GuestContext *ctx);
void shim_sceFontWritingRefersRenderStep(GuestContext *ctx);
void shim_sceFontWritingRefersRenderStepCharacter(GuestContext *ctx);
void shim_sceFontCharacterGetBidiLevel(GuestContext *ctx);
void shim_sceFontCharacterGetSyllableStringState(GuestContext *ctx);
void shim_sceFontCharacterGetTextFontCode(GuestContext *ctx);
void shim_sceFontCharacterGetTextOrder(GuestContext *ctx);
void shim_sceFontCharacterLooksWhiteSpace(GuestContext *ctx);
void shim_sceFontCharacterRefersTextNext(GuestContext *ctx);
void shim_sceFontCharactersRefersTextCodes(GuestContext *ctx);
void shim_sceFontTextCodesStepNext(GuestContext *ctx);
void shim_sceFontWordsFindWordCharacters(GuestContext *ctx);
void shim_sceFontRenderCharGlyphImageHorizontal(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_FONT_H
