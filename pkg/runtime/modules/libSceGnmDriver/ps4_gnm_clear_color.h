// Fast-clear colours: the CB_COLOR*_CLEAR_WORD registers decoded for a Metal attachment format.
#pragma once

#import <Metal/Metal.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace ps4gnm_clear {

inline float AsFloat(uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}

// Raw clear words -> MTLClearColor for the attachment's Metal format (the texture has the
// same memory layout as the guest surface, so the raw bits decode directly; cf. shadPS4
// LiverpoolToVK::ColorBufferClearValue).
inline double SrgbToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
inline float Half(uint32_t h) {
    __fp16 v;
    uint16_t x = (uint16_t)h;
    memcpy(&v, &x, 2);
    return (float)v;
}
inline double SnormByte(uint32_t v) {
    const int8_t b = (int8_t)(v & 255);
    return b < -127 ? -1.0 : b / 127.0;
}
inline float UFloat(uint32_t v, int mbits) { // unsigned small float with 5 bit exponent
    const uint32_t e = (v >> mbits) & 31, m = v & ((1u << mbits) - 1);
    if (e == 0) return ldexpf((float)m, -14 - mbits);
    if (e == 31) return m ? NAN : INFINITY;
    return ldexpf(1.0f + (float)m / (float)(1u << mbits), (int)e - 15);
}

inline bool ClearColorFor(MTLPixelFormat f, uint32_t w0, uint32_t w1, MTLClearColor* out) {
    double r = 0, g = 0, b = 0, a = 1;
    switch (f) {
    case MTLPixelFormatRGBA8Unorm:
    case MTLPixelFormatRGBA8Unorm_sRGB:
        r = (w0 & 255) / 255.0; g = ((w0 >> 8) & 255) / 255.0; b = ((w0 >> 16) & 255) / 255.0; a = (w0 >> 24) / 255.0;
        break;
    case MTLPixelFormatBGRA8Unorm:
    case MTLPixelFormatBGRA8Unorm_sRGB:
        b = (w0 & 255) / 255.0; g = ((w0 >> 8) & 255) / 255.0; r = ((w0 >> 16) & 255) / 255.0; a = (w0 >> 24) / 255.0;
        break;
    case MTLPixelFormatRGB10A2Unorm:
        r = (w0 & 1023) / 1023.0; g = ((w0 >> 10) & 1023) / 1023.0; b = ((w0 >> 20) & 1023) / 1023.0; a = (w0 >> 30) / 3.0;
        break;
    case MTLPixelFormatBGR10A2Unorm:
        b = (w0 & 1023) / 1023.0; g = ((w0 >> 10) & 1023) / 1023.0; r = ((w0 >> 20) & 1023) / 1023.0; a = (w0 >> 30) / 3.0;
        break;
    case MTLPixelFormatRG11B10Float:
        r = UFloat(w0 & 2047, 6); g = UFloat((w0 >> 11) & 2047, 6); b = UFloat(w0 >> 22, 5);
        break;
    case MTLPixelFormatRGBA16Float:
        r = Half(w0); g = Half(w0 >> 16); b = Half(w1); a = Half(w1 >> 16);
        break;
    case MTLPixelFormatRGBA16Unorm:
        r = (w0 & 65535) / 65535.0; g = (w0 >> 16) / 65535.0; b = (w1 & 65535) / 65535.0; a = (w1 >> 16) / 65535.0;
        break;
    case MTLPixelFormatRG16Float:
        r = Half(w0); g = Half(w0 >> 16);
        break;
    case MTLPixelFormatRG16Unorm:
        r = (w0 & 65535) / 65535.0; g = (w0 >> 16) / 65535.0;
        break;
    case MTLPixelFormatR16Float:
        r = Half(w0);
        break;
    case MTLPixelFormatR16Unorm:
        r = (w0 & 65535) / 65535.0;
        break;
    case MTLPixelFormatR32Float:
        r = AsFloat(w0);
        break;
    case MTLPixelFormatRG32Float:
        r = AsFloat(w0); g = AsFloat(w1);
        break;
    case MTLPixelFormatRGBA32Float:
        r = g = b = AsFloat(w0); a = AsFloat(w1);
        break;
    case MTLPixelFormatR8Unorm:
        r = (w0 & 255) / 255.0;
        break;
    case MTLPixelFormatRG8Unorm:
        r = (w0 & 255) / 255.0; g = ((w0 >> 8) & 255) / 255.0;
        break;
    case MTLPixelFormatR8Uint:
        r = w0 & 255;
        break;
    case MTLPixelFormatR8Sint:
        r = (int8_t)(w0 & 255);
        break;
    case MTLPixelFormatRG8Uint:
        r = w0 & 255; g = (w0 >> 8) & 255;
        break;
    case MTLPixelFormatRG8Sint:
        r = (int8_t)(w0 & 255); g = (int8_t)((w0 >> 8) & 255);
        break;
    case MTLPixelFormatRGBA8Uint:
        r = w0 & 255; g = (w0 >> 8) & 255; b = (w0 >> 16) & 255; a = w0 >> 24;
        break;
    case MTLPixelFormatRGBA8Sint:
        r = (int8_t)(w0 & 255); g = (int8_t)((w0 >> 8) & 255); b = (int8_t)((w0 >> 16) & 255); a = (int8_t)(w0 >> 24);
        break;
    case MTLPixelFormatR8Snorm:
        r = SnormByte(w0);
        break;
    case MTLPixelFormatRG8Snorm:
        r = SnormByte(w0); g = SnormByte(w0 >> 8);
        break;
    case MTLPixelFormatRGBA8Snorm:
        r = SnormByte(w0); g = SnormByte(w0 >> 8); b = SnormByte(w0 >> 16); a = SnormByte(w0 >> 24);
        break;
    case MTLPixelFormatR16Uint:
        r = w0 & 65535;
        break;
    case MTLPixelFormatR16Sint:
        r = (int16_t)(w0 & 65535);
        break;
    case MTLPixelFormatRG16Uint:
        r = w0 & 65535; g = w0 >> 16;
        break;
    case MTLPixelFormatRG16Sint:
        r = (int16_t)(w0 & 65535); g = (int16_t)(w0 >> 16);
        break;
    case MTLPixelFormatRGBA16Uint:
        r = w0 & 65535; g = w0 >> 16; b = w1 & 65535; a = w1 >> 16;
        break;
    case MTLPixelFormatRGBA16Sint:
        r = (int16_t)(w0 & 65535); g = (int16_t)(w0 >> 16); b = (int16_t)(w1 & 65535); a = (int16_t)(w1 >> 16);
        break;
    case MTLPixelFormatRG32Uint:
        r = w0; g = w1;
        break;
    case MTLPixelFormatRG32Sint:
        r = (int32_t)w0; g = (int32_t)w1;
        break;
    case MTLPixelFormatR32Uint:
        r = (double)w0;
        break;
    case MTLPixelFormatR32Sint:
        r = (double)(int32_t)w0;
        break;
    default:
        return false;
    }
    if (f == MTLPixelFormatRGBA8Unorm_sRGB || f == MTLPixelFormatBGRA8Unorm_sRGB) {
        r = SrgbToLinear(r);
        g = SrgbToLinear(g);
        b = SrgbToLinear(b);
    }
    *out = MTLClearColorMake(r, g, b, a);
    return true;
}

} // namespace ps4gnm_clear
