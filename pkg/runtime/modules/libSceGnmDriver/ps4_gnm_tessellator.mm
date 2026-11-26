// SPDX-License-Identifier: GPL-2.0-or-later AND MIT
//
// Compute tessellator (see ps4_gnm_tessellator.h).
//
// The kernel source below is a port to the Metal Shading Language of CHWTessellator from
// Microsoft's "D3D11 Fixed Function Tessellator Reference" (tessellator.cpp / tessellator.hpp in
// https://github.com/microsoft/DirectX-Specs, d3d/archive/images/d3d11), which is distributed under
// the following license:
//
//   The MIT License (MIT)
//   Copyright (c) Microsoft Corporation
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
//   associated documentation files (the "Software"), to deal in the Software without restriction,
//   including without limitation the rights to use, copy, modify, merge, publish, distribute,
//   sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all copies or
//   substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
//   NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
//   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
//   OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Changes from the reference: tri and quad domains only (Metal cannot run isoline domain shaders);
// no XBox 360 split mode and no in-order line list output; points and indices go to device
// buffers at per-patch bases (a null buffer only counts); a point carries w (1 - u - v for
// triangles) and its patch index.

#include "ps4_gnm_tessellator.h"

namespace ps4gnm_tess {

static const char* const kSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

typedef uint FXP; // fixed point number, 15.16

#define MIN_ODD_TESSELLATION_FACTOR 1
#define MAX_ODD_TESSELLATION_FACTOR 63
#define MIN_EVEN_TESSELLATION_FACTOR 2
#define MAX_EVEN_TESSELLATION_FACTOR 64

#define PARTITIONING_INTEGER 0
#define PARTITIONING_POW2 1
#define PARTITIONING_FRACTIONAL_ODD 2
#define PARTITIONING_FRACTIONAL_EVEN 3

#define OUTPUT_POINT 0
#define OUTPUT_TRIANGLE_CW 2
#define OUTPUT_TRIANGLE_CCW 3

#define PARITY_EVEN 0
#define PARITY_ODD 1

#define DOMAIN_TRI 0
#define DOMAIN_QUAD 1

#define FXP_FRACTION_BITS 16
#define FXP_FRACTION_MASK 0x0000ffffu
#define FXP_INTEGER_MASK 0x7fff0000u
#define FXP_ONE (1u << FXP_FRACTION_BITS)
#define FXP_ONE_THIRD 0x00005555u
#define FXP_TWO_THIRDS 0x0000aaaau
#define FXP_ONE_HALF 0x00008000u

#define EPSILON 0.0000152587890625f // 2^(-16), min positive fixed point fraction
#define MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON (MIN_ODD_TESSELLATION_FACTOR + EPSILON / 2)

#define U 0
#define V 1
#define Ueq0 0
#define Veq0 1
#define Weq0 2
#define Ueq1 2
#define Veq1 3
#define QUAD_AXES 2
#define QUAD_EDGES 4
#define TRI_EDGES 3

#define DIAGONALS_INSIDE_TO_OUTSIDE 0
#define DIAGONALS_INSIDE_TO_OUTSIDE_EXCEPT_MIDDLE 1
#define DIAGONALS_MIRRORED 2

constant FXP s_fixedReciprocal[65] = {
    0xffffffff, // 1/0 is the first entry (unused)
    0x10000, 0x8000, 0x5555, 0x4000, 0x3333, 0x2aab, 0x2492, 0x2000, 0x1c72, 0x199a, 0x1746, 0x1555,
    0x13b1, 0x1249, 0x1111, 0x1000, 0xf0f, 0xe39, 0xd79, 0xccd, 0xc31, 0xba3, 0xb21, 0xaab,
    0xa3d, 0x9d9, 0x97b, 0x925, 0x8d4, 0x889, 0x842, 0x800, 0x7c2, 0x788, 0x750, 0x71c,
    0x6eb, 0x6bd, 0x690, 0x666, 0x63e, 0x618, 0x5f4, 0x5d1, 0x5b0, 0x591, 0x572, 0x555,
    0x539, 0x51f, 0x505, 0x4ec, 0x4d5, 0x4be, 0x4a8, 0x492, 0x47e, 0x46a, 0x457, 0x444,
    0x432, 0x421, 0x410, 0x400, // 1/64 is the last entry
};

// Ruler function vertex split order of a half edge (supports odd TessFactor up to 65 and even
// up to 64), and loop bounds for each half TessFactor.
constant int finalPointPositionTable[33] = {0,  32, 16, 8,  17, 4,  18, 9,  19, 2,  20,
                                            10, 21, 5,  22, 11, 23, 1,  24, 12, 25, 6,
                                            26, 13, 27, 3,  28, 14, 29, 7,  30, 15, 31};
constant int loopStart[33] = {1, 1, 17, 9, 9, 5, 5, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3,
                              2, 2, 2,  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
constant int loopEnd[33] = {0,  0,  17, 17, 25, 25, 25, 25, 29, 29, 29, 29, 29, 29, 29, 29, 31,
                            31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 32};

// ---- D3D compliant float math
static bool isNaN(float a) {
    const uint u = as_type<uint>(a);
    return ((u & 0x7f800000u) == 0x7f800000u) && (u & 0x007fffffu);
}
static float flushDenorm(float a) {
    const int b = as_type<int>(a) & 0x7fffffff; // fabs()
    if (b < 0x00800000) return as_type<float>(as_type<int>(a) & int(0x80000000u));
    return a;
}
// IEEE754R min / max
static float d3dMin(float a, float b) {
    const float _a = flushDenorm(a), _b = flushDenorm(b);
    if (isNaN(_b)) return a;
    if ((_a == 0) && (_b == 0)) return (as_type<uint>(_a) & 0x80000000u) ? a : b;
    return _a < _b ? a : b;
}
static float d3dMax(float a, float b) {
    const float _a = flushDenorm(a), _b = flushDenorm(b);
    if (isNaN(_b)) return a;
    if ((_a == 0) && (_b == 0)) return (as_type<uint>(_b) & 0x80000000u) ? a : b;
    return _a >= _b ? a : b;
}

// floatToIDotF<15, 16, unsigned> of the reference
static FXP floatToFixed(float input) {
    const int maxPosValueFloat = (127 + 15) << 23; // 2^15 - 2^-16 rounds up to 2^15
    int output = as_type<int>(input);
    const int unbiasedExponent = ((output & 0x7f800000) >> 23) - 127;
    const int isNegative = output & int(0x80000000u);
    if (unbiasedExponent == 128 && (output & 0x007fffff)) {
        output = 0; // nan converts to 0
    } else if (!isNegative && output >= maxPosValueFloat) {
        output = -1; // ~0: the largest value
    } else if (isNegative) {
        output = 0; // the most negative value of an unsigned format
    } else if (unbiasedExponent < -16 - 1) {
        output = 0; // too small
    } else {
        output = (output & 0x007fffff) | 0x00800000; // mantissa with the hidden bit
        const int extraBits = 23 - 16 - unbiasedExponent;
        if (extraBits >= 0) {
            const int LSB = 1 << extraBits; // last bit being kept
            const int extraBitsMask = LSB - 1;
            const int roundBias = LSB >> 1;
            if ((output & LSB) || (output & extraBitsMask) > roundBias) output += roundBias;
            output >>= extraBits;
        } else {
            output <<= -extraBits;
        }
    }
    return as_type<FXP>(output);
}
static float fixedToFloat(FXP input) {
    return float(input >> FXP_FRACTION_BITS) + float(input & FXP_FRACTION_MASK) / float(1 << FXP_FRACTION_BITS);
}
static bool isEven(float input) { return (int(input) & 1) ? false : true; }
static FXP fxpCeil(FXP input) {
    if (input & FXP_FRACTION_MASK) return (input & FXP_INTEGER_MASK) + FXP_ONE;
    return input;
}
static FXP fxpFloor(FXP input) { return input & FXP_INTEGER_MASK; }
static int RemoveMSB(int val) {
    int check;
    if (val <= 0x0000ffff) check = (val <= 0x000000ff) ? 0x00000080 : 0x00008000;
    else check = (val <= 0x00ffffff) ? 0x00800000 : int(0x80000000u);
    for (int i = 0; i < 8; i++, check >>= 1)
        if (val & check) return (val & ~check);
    return 0;
}

struct TessFactorContext {
    FXP fxpInvNumSegmentsOnFloorTessFactor;
    FXP fxpInvNumSegmentsOnCeilTessFactor;
    FXP fxpHalfTessFactorFraction;
    int numHalfTessFactorPoints;
    int splitPointOnFloorHalfTessFactor;
};

struct ProcessedTri {
    FXP outsideTessFactor[TRI_EDGES];
    FXP insideTessFactor;
    int outsideTessFactorParity[TRI_EDGES];
    int insideTessFactorParity;
    TessFactorContext outsideTessFactorCtx[TRI_EDGES];
    TessFactorContext insideTessFactorCtx;
    bool bJustDoMinimumTessFactor;
    bool bPatchCulled;
    int numPointsForOutsideEdge[TRI_EDGES];
    int numPointsForInsideTessFactor;
    int insideEdgePointBaseOffset;
};

struct ProcessedQuad {
    FXP outsideTessFactor[QUAD_EDGES];
    FXP insideTessFactor[QUAD_AXES];
    int outsideTessFactorParity[QUAD_EDGES];
    int insideTessFactorParity[QUAD_AXES];
    TessFactorContext outsideTessFactorCtx[QUAD_EDGES];
    TessFactorContext insideTessFactorCtx[QUAD_AXES];
    bool bJustDoMinimumTessFactor;
    bool bPatchCulled;
    int numPointsForOutsideEdge[QUAD_EDGES];
    int numPointsForInsideTessFactor[QUAD_AXES];
    int insideEdgePointBaseOffset;
};

struct IndexPatchContext {
    int insidePointIndexDeltaToRealValue;
    int insidePointIndexBadValue;
    int insidePointIndexReplacementValue;
    int outsidePointIndexPatchBase;
    int outsidePointIndexDeltaToRealValue;
    int outsidePointIndexBadValue;
    int outsidePointIndexReplacementValue;
};

struct IndexPatchContext2 {
    int baseIndexToInvert;
    int indexInversionEndPoint;
    int cornerCaseBadValue;
    int cornerCaseReplacementValue;
};

// CHWTessellator. points / indices null: count only (m_NumPoints / m_NumIndices).
struct Tessellator {
    int m_parity;
    int m_originalParity;
    int m_partitioning;
    int m_originalPartitioning;
    int m_outputPrimitive;
    int m_domain;
    int m_NumPoints;
    int m_NumIndices;
    bool m_bUsingPatchedIndices;
    bool m_bUsingPatchedIndices2;
    IndexPatchContext m_IndexPatchContext;
    IndexPatchContext2 m_IndexPatchContext2;
    device float4* m_points;
    device uint* m_indices;
    uint m_pointBase;
    uint m_indexBase;
    uint m_patch;

    void Init(int partitioning, int outputPrimitive, int domain) {
        m_partitioning = partitioning;
        m_originalPartitioning = partitioning;
        m_parity = PARITY_EVEN; // integer partitioning sets the parity per TessFactor
        if (partitioning == PARTITIONING_FRACTIONAL_ODD) m_parity = PARITY_ODD;
        else if (partitioning == PARTITIONING_FRACTIONAL_EVEN) m_parity = PARITY_EVEN;
        m_originalParity = m_parity;
        m_outputPrimitive = outputPrimitive;
        m_domain = domain;
        m_NumPoints = 0;
        m_NumIndices = 0;
        m_bUsingPatchedIndices = false;
        m_bUsingPatchedIndices2 = false;
    }

    bool Odd() const { return m_parity == PARITY_ODD; }
    void SetTessellationParity(int parity) { m_parity = parity; }
    bool HWIntegerPartitioning() const {
        return m_partitioning == PARTITIONING_INTEGER || m_partitioning == PARTITIONING_POW2;
    }
    void SetUsingPatchedIndices(bool b) { m_bUsingPatchedIndices = b; }
    void SetUsingPatchedIndices2(bool b) { m_bUsingPatchedIndices2 = b; }

    void DefinePoint(FXP fxpU, FXP fxpV, int pointStorageOffset) {
        if (!m_points) return;
        const float u = fixedToFloat(fxpU), v = fixedToFloat(fxpV);
        const float w = m_domain == DOMAIN_TRI ? 1.0f - u - v : 0.0f;
        m_points[m_pointBase + uint(pointStorageOffset)] = float4(u, v, w, as_type<float>(m_patch));
    }

    int PatchIndexValue(int index) const {
        if (m_bUsingPatchedIndices) {
            if (index >= m_IndexPatchContext.outsidePointIndexPatchBase) {
                if (index == m_IndexPatchContext.outsidePointIndexBadValue)
                    index = m_IndexPatchContext.outsidePointIndexReplacementValue;
                else
                    index += m_IndexPatchContext.outsidePointIndexDeltaToRealValue;
            } else {
                if (index == m_IndexPatchContext.insidePointIndexBadValue)
                    index = m_IndexPatchContext.insidePointIndexReplacementValue;
                else
                    index += m_IndexPatchContext.insidePointIndexDeltaToRealValue;
            }
        } else if (m_bUsingPatchedIndices2) {
            if (index >= m_IndexPatchContext2.baseIndexToInvert) {
                if (index == m_IndexPatchContext2.cornerCaseBadValue)
                    index = m_IndexPatchContext2.cornerCaseReplacementValue;
                else
                    index = m_IndexPatchContext2.indexInversionEndPoint - index;
            } else if (index == m_IndexPatchContext2.cornerCaseBadValue) {
                index = m_IndexPatchContext2.cornerCaseReplacementValue;
            }
        }
        return index;
    }

    void DefineIndex(int index, int indexStorageOffset) {
        if (!m_indices) return;
        index = PatchIndexValue(index);
        m_indices[m_indexBase + uint(indexStorageOffset)] = m_pointBase + uint(index);
    }

    void DefineClockwiseTriangle(int index0, int index1, int index2, int indexStorageBaseOffset) {
        // inputs a clockwise triangle, stores a CW or CCW triangle depending on the state
        DefineIndex(index0, indexStorageBaseOffset);
        if (m_outputPrimitive == OUTPUT_TRIANGLE_CW) {
            DefineIndex(index1, indexStorageBaseOffset + 1);
            DefineIndex(index2, indexStorageBaseOffset + 2);
        } else {
            DefineIndex(index2, indexStorageBaseOffset + 1);
            DefineIndex(index1, indexStorageBaseOffset + 2);
        }
    }

    void DumpAllPoints() {
        for (int p = 0; p < m_NumPoints; p++) DefineIndex(p, m_NumIndices++);
    }

    int NumPointsForTessFactor(FXP fxpTessFactor) const {
        if (Odd()) return int((fxpCeil(FXP_ONE_HALF + (fxpTessFactor + 1 /*round*/) / 2) * 2) >> FXP_FRACTION_BITS);
        return int(((fxpCeil((fxpTessFactor + 1 /*round*/) / 2) * 2) >> FXP_FRACTION_BITS) + 1);
    }

    void ComputeTessFactorContext(FXP fxpTessFactor, thread TessFactorContext& ctx) const {
        FXP fxpHalfTessFactor = (fxpTessFactor + 1 /*round*/) / 2;
        // fxpHalfTessFactor == 1/2 if TessFactor is 1, but we're pretending we are even.
        if (Odd() || (fxpHalfTessFactor == FXP_ONE_HALF)) fxpHalfTessFactor += FXP_ONE_HALF;
        const FXP fxpFloorHalfTessFactor = fxpFloor(fxpHalfTessFactor);
        const FXP fxpCeilHalfTessFactor = fxpCeil(fxpHalfTessFactor);
        ctx.fxpHalfTessFactorFraction = fxpHalfTessFactor - fxpFloorHalfTessFactor;
        // for EVEN, we don't include the point always fixed at the midpoint of the TessFactor
        ctx.numHalfTessFactorPoints = int(fxpCeilHalfTessFactor >> FXP_FRACTION_BITS);
        if (fxpCeilHalfTessFactor == fxpFloorHalfTessFactor) {
            ctx.splitPointOnFloorHalfTessFactor = ctx.numHalfTessFactorPoints + 1; // ignored
        } else if (Odd()) {
            if (fxpFloorHalfTessFactor == FXP_ONE)
                ctx.splitPointOnFloorHalfTessFactor = 0;
            else
                ctx.splitPointOnFloorHalfTessFactor =
                    (RemoveMSB(int(fxpFloorHalfTessFactor >> FXP_FRACTION_BITS) - 1) << 1) + 1;
        } else {
            ctx.splitPointOnFloorHalfTessFactor = (RemoveMSB(int(fxpFloorHalfTessFactor >> FXP_FRACTION_BITS)) << 1) + 1;
        }
        int numFloorSegments = int((fxpFloorHalfTessFactor * 2) >> FXP_FRACTION_BITS);
        int numCeilSegments = int((fxpCeilHalfTessFactor * 2) >> FXP_FRACTION_BITS);
        if (Odd()) {
            numFloorSegments -= 1;
            numCeilSegments -= 1;
        }
        ctx.fxpInvNumSegmentsOnFloorTessFactor = s_fixedReciprocal[numFloorSegments];
        ctx.fxpInvNumSegmentsOnCeilTessFactor = s_fixedReciprocal[numCeilSegments];
    }

    FXP PlacePointIn1D(thread const TessFactorContext& ctx, int point) const {
        bool bFlip;
        if (point >= ctx.numHalfTessFactorPoints) {
            point = (ctx.numHalfTessFactorPoints << 1) - point;
            if (Odd()) point -= 1;
            bFlip = true;
        } else {
            bFlip = false;
        }
        if (point == ctx.numHalfTessFactorPoints) return FXP_ONE_HALF; // 16 bit fixed math can't reproduce 0.5
        const uint indexOnCeilHalfTessFactor = uint(point);
        uint indexOnFloorHalfTessFactor = indexOnCeilHalfTessFactor;
        if (point > ctx.splitPointOnFloorHalfTessFactor) indexOnFloorHalfTessFactor -= 1;
        const FXP fxpLocationOnFloorHalfTessFactor = indexOnFloorHalfTessFactor * ctx.fxpInvNumSegmentsOnFloorTessFactor;
        const FXP fxpLocationOnCeilHalfTessFactor = indexOnCeilHalfTessFactor * ctx.fxpInvNumSegmentsOnCeilTessFactor;
        FXP fxpLocation = fxpLocationOnFloorHalfTessFactor * (FXP_ONE - ctx.fxpHalfTessFactorFraction) +
                          fxpLocationOnCeilHalfTessFactor * (ctx.fxpHalfTessFactorFraction);
        fxpLocation = (fxpLocation + FXP_ONE_HALF /*round*/) >> FXP_FRACTION_BITS; // get back to n.16
        if (bFlip) fxpLocation = FXP_ONE - fxpLocation;
        return fxpLocation;
    }

    void StitchRegular(bool bTrapezoid, int diagonals, int baseIndexOffset, int numInsideEdgePoints,
                       int insideEdgePointBaseOffset, int outsideEdgePointBaseOffset) {
        int insidePoint = insideEdgePointBaseOffset;
        int outsidePoint = outsideEdgePointBaseOffset;
        if (bTrapezoid) {
            DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint, baseIndexOffset);
            baseIndexOffset += 3;
            outsidePoint++;
        }
        int p;
        switch (diagonals) {
        case DIAGONALS_INSIDE_TO_OUTSIDE:
            // Diagonals pointing from inside edge forward towards outside edge
            for (p = 0; p < numInsideEdgePoints - 1; p++) {
                DefineClockwiseTriangle(insidePoint, outsidePoint, outsidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                DefineClockwiseTriangle(insidePoint, outsidePoint + 1, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
                outsidePoint++;
            }
            break;
        case DIAGONALS_INSIDE_TO_OUTSIDE_EXCEPT_MIDDLE: // Assumes ODD tessellation
            // First half: diagonals pointing from outside edge forward towards inside edge
            for (p = 0; p < numInsideEdgePoints / 2 - 1; p++) {
                DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint, baseIndexOffset);
                baseIndexOffset += 3;
                DefineClockwiseTriangle(insidePoint, outsidePoint + 1, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
                outsidePoint++;
            }
            // Middle
            DefineClockwiseTriangle(outsidePoint, insidePoint + 1, insidePoint, baseIndexOffset);
            baseIndexOffset += 3;
            DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint + 1, baseIndexOffset);
            baseIndexOffset += 3;
            insidePoint++;
            outsidePoint++;
            p += 2;
            // Second half
            for (; p < numInsideEdgePoints; p++) {
                DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint, baseIndexOffset);
                baseIndexOffset += 3;
                DefineClockwiseTriangle(insidePoint, outsidePoint + 1, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
                outsidePoint++;
            }
            break;
        case DIAGONALS_MIRRORED:
            // First half, diagonals pointing from outside of outside edge to inside of inside edge
            for (p = 0; p < numInsideEdgePoints / 2; p++) {
                DefineClockwiseTriangle(outsidePoint, insidePoint + 1, insidePoint, baseIndexOffset);
                baseIndexOffset += 3;
                DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
                outsidePoint++;
            }
            // Second half, diagonals pointing from inside of inside edge to outside of outside edge
            for (; p < numInsideEdgePoints - 1; p++) {
                DefineClockwiseTriangle(insidePoint, outsidePoint, outsidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                DefineClockwiseTriangle(insidePoint, outsidePoint + 1, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
                outsidePoint++;
            }
            break;
        }
        if (bTrapezoid) {
            DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint, baseIndexOffset);
            baseIndexOffset += 3;
        }
    }

    void StitchTransition(int baseIndexOffset, int insideEdgePointBaseOffset, int insideNumHalfTessFactorPoints,
                          int insideEdgeTessFactorParity, int outsideEdgePointBaseOffset,
                          int outsideNumHalfTessFactorPoints, int outsideTessFactorParity) {
        if (insideEdgeTessFactorParity == PARITY_ODD) insideNumHalfTessFactorPoints -= 1;
        if (outsideTessFactorParity == PARITY_ODD) outsideNumHalfTessFactorPoints -= 1;
        // Walk first half
        int outsidePoint = outsideEdgePointBaseOffset;
        int insidePoint = insideEdgePointBaseOffset;
        // iStart, iEnd are a small optimization so the loop below doesn't have to go from 0 up to 31
        const int iStart = min(loopStart[insideNumHalfTessFactorPoints], loopStart[outsideNumHalfTessFactorPoints]);
        const int iEnd = max(loopEnd[insideNumHalfTessFactorPoints], loopEnd[outsideNumHalfTessFactorPoints]);
        if (finalPointPositionTable[0] < outsideNumHalfTessFactorPoints) {
            // Advance outside (the loop below does not start at 0)
            DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint, baseIndexOffset);
            baseIndexOffset += 3;
            outsidePoint++;
        }
        for (int i = iStart; i <= iEnd; i++) {
            if (finalPointPositionTable[i] < insideNumHalfTessFactorPoints) {
                // Advance inside
                DefineClockwiseTriangle(insidePoint, outsidePoint, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
            }
            if (finalPointPositionTable[i] < outsideNumHalfTessFactorPoints) {
                // Advance outside
                DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint, baseIndexOffset);
                baseIndexOffset += 3;
                outsidePoint++;
            }
        }
        if ((insideEdgeTessFactorParity != outsideTessFactorParity) || (insideEdgeTessFactorParity == PARITY_ODD)) {
            if (insideEdgeTessFactorParity == outsideTessFactorParity) {
                // Quad in the middle
                DefineClockwiseTriangle(insidePoint, outsidePoint, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                DefineClockwiseTriangle(insidePoint + 1, outsidePoint, outsidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
                outsidePoint++;
            } else if (insideEdgeTessFactorParity == PARITY_EVEN) {
                // Triangle pointing inside
                DefineClockwiseTriangle(insidePoint, outsidePoint, outsidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                outsidePoint++;
            } else {
                // Triangle pointing outside
                DefineClockwiseTriangle(insidePoint, outsidePoint, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
            }
        }
        // Walk second half.
        for (int i = iEnd; i >= iStart; i--) {
            if (finalPointPositionTable[i] < outsideNumHalfTessFactorPoints) {
                // Advance outside
                DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint, baseIndexOffset);
                baseIndexOffset += 3;
                outsidePoint++;
            }
            if (finalPointPositionTable[i] < insideNumHalfTessFactorPoints) {
                // Advance inside
                DefineClockwiseTriangle(insidePoint, outsidePoint, insidePoint + 1, baseIndexOffset);
                baseIndexOffset += 3;
                insidePoint++;
            }
        }
        if (finalPointPositionTable[0] < outsideNumHalfTessFactorPoints) {
            DefineClockwiseTriangle(outsidePoint, outsidePoint + 1, insidePoint, baseIndexOffset);
            baseIndexOffset += 3;
            outsidePoint++;
        }
    }

    // The edge clamp bounds of the original partitioning.
    void Bounds(thread float& lowerBound, thread float& upperBound) const {
        switch (m_originalPartitioning) {
        case PARTITIONING_FRACTIONAL_EVEN:
            lowerBound = MIN_EVEN_TESSELLATION_FACTOR;
            upperBound = MAX_EVEN_TESSELLATION_FACTOR;
            break;
        case PARTITIONING_FRACTIONAL_ODD:
            lowerBound = MIN_ODD_TESSELLATION_FACTOR;
            upperBound = MAX_ODD_TESSELLATION_FACTOR;
            break;
        default: // integer or pow2
            lowerBound = MIN_ODD_TESSELLATION_FACTOR;
            upperBound = MAX_EVEN_TESSELLATION_FACTOR;
            break;
        }
    }

    // ---- quad domain
    void QuadProcessTessFactors(float tessFactor_Ueq0, float tessFactor_Veq0, float tessFactor_Ueq1,
                                float tessFactor_Veq1, float insideTessFactor_U, float insideTessFactor_V,
                                thread ProcessedQuad& pf) {
        // Is the patch culled? (NaN will pass)
        if (!(tessFactor_Ueq0 > 0) || !(tessFactor_Veq0 > 0) || !(tessFactor_Ueq1 > 0) || !(tessFactor_Veq1 > 0)) {
            pf.bPatchCulled = true;
            return;
        }
        pf.bPatchCulled = false;
        float lowerBound, upperBound;
        Bounds(lowerBound, upperBound);
        tessFactor_Ueq0 = d3dMin(upperBound, d3dMax(lowerBound, tessFactor_Ueq0));
        tessFactor_Veq0 = d3dMin(upperBound, d3dMax(lowerBound, tessFactor_Veq0));
        tessFactor_Ueq1 = d3dMin(upperBound, d3dMax(lowerBound, tessFactor_Ueq1));
        tessFactor_Veq1 = d3dMin(upperBound, d3dMax(lowerBound, tessFactor_Veq1));
        if (HWIntegerPartitioning()) {
            tessFactor_Ueq0 = ceil(tessFactor_Ueq0);
            tessFactor_Veq0 = ceil(tessFactor_Veq0);
            tessFactor_Ueq1 = ceil(tessFactor_Ueq1);
            tessFactor_Veq1 = ceil(tessFactor_Veq1);
        }
        // Clamp inside TessFactors
        if (m_originalPartitioning == PARTITIONING_FRACTIONAL_ODD) {
            // If any TessFactor will end up > 1 after floatToFixed conversion later,
            // then force the inside TessFactors to be > 1 so there is a picture frame.
            if ((tessFactor_Ueq0 > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON) ||
                (tessFactor_Veq0 > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON) ||
                (tessFactor_Ueq1 > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON) ||
                (tessFactor_Veq1 > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON) ||
                (insideTessFactor_U > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON) ||
                (insideTessFactor_V > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON))
                lowerBound = MIN_ODD_TESSELLATION_FACTOR + EPSILON; // Force picture frame
        }
        insideTessFactor_U = d3dMin(upperBound, d3dMax(lowerBound, insideTessFactor_U));
        insideTessFactor_V = d3dMin(upperBound, d3dMax(lowerBound, insideTessFactor_V));
        // Note the above clamps map NaN to lowerBound
        if (HWIntegerPartitioning()) {
            insideTessFactor_U = ceil(insideTessFactor_U);
            insideTessFactor_V = ceil(insideTessFactor_V);
        }
        m_NumPoints = 0;
        m_NumIndices = 0;
        const float outsideTessFactor[QUAD_EDGES] = {tessFactor_Ueq0, tessFactor_Veq0, tessFactor_Ueq1, tessFactor_Veq1};
        const float insideTessFactor[QUAD_AXES] = {insideTessFactor_U, insideTessFactor_V};
        if (HWIntegerPartitioning()) {
            for (int edge = 0; edge < QUAD_EDGES; edge++)
                pf.outsideTessFactorParity[edge] = isEven(outsideTessFactor[edge]) ? PARITY_EVEN : PARITY_ODD;
            for (int axis = 0; axis < QUAD_AXES; axis++)
                pf.insideTessFactorParity[axis] =
                    (isEven(insideTessFactor[axis]) || (1.0f == insideTessFactor[axis])) ? PARITY_EVEN : PARITY_ODD;
        } else {
            for (int edge = 0; edge < QUAD_EDGES; edge++) pf.outsideTessFactorParity[edge] = m_originalParity;
            pf.insideTessFactorParity[U] = pf.insideTessFactorParity[V] = m_originalParity;
        }
        for (int edge = 0; edge < QUAD_EDGES; edge++) pf.outsideTessFactor[edge] = floatToFixed(outsideTessFactor[edge]);
        for (int axis = 0; axis < QUAD_AXES; axis++) pf.insideTessFactor[axis] = floatToFixed(insideTessFactor[axis]);
        if (HWIntegerPartitioning() || Odd()) {
            // Special case if all TessFactors are 1
            if ((FXP_ONE == pf.insideTessFactor[U]) && (FXP_ONE == pf.insideTessFactor[V]) &&
                (FXP_ONE == pf.outsideTessFactor[Ueq0]) && (FXP_ONE == pf.outsideTessFactor[Veq0]) &&
                (FXP_ONE == pf.outsideTessFactor[Ueq1]) && (FXP_ONE == pf.outsideTessFactor[Veq1])) {
                pf.bJustDoMinimumTessFactor = true;
                return;
            }
        }
        pf.bJustDoMinimumTessFactor = false;
        for (int edge = 0; edge < QUAD_EDGES; edge++) {
            SetTessellationParity(pf.outsideTessFactorParity[edge]);
            ComputeTessFactorContext(pf.outsideTessFactor[edge], pf.outsideTessFactorCtx[edge]);
        }
        for (int axis = 0; axis < QUAD_AXES; axis++) {
            SetTessellationParity(pf.insideTessFactorParity[axis]);
            ComputeTessFactorContext(pf.insideTessFactor[axis], pf.insideTessFactorCtx[axis]);
        }
        // outside edge offsets and storage
        for (int edge = 0; edge < QUAD_EDGES; edge++) {
            SetTessellationParity(pf.outsideTessFactorParity[edge]);
            pf.numPointsForOutsideEdge[edge] = NumPointsForTessFactor(pf.outsideTessFactor[edge]);
            m_NumPoints += pf.numPointsForOutsideEdge[edge];
        }
        m_NumPoints -= 4;
        // inside edge offsets
        for (int axis = 0; axis < QUAD_AXES; axis++) {
            SetTessellationParity(pf.insideTessFactorParity[axis]);
            pf.numPointsForInsideTessFactor[axis] = NumPointsForTessFactor(pf.insideTessFactor[axis]);
            const int pointCountMin = (PARITY_ODD == pf.insideTessFactorParity[axis]) ? 4 : 3;
            // max() allows degenerate transition regions when inside TessFactor == 1
            pf.numPointsForInsideTessFactor[axis] = max(pointCountMin, pf.numPointsForInsideTessFactor[axis]);
        }
        pf.insideEdgePointBaseOffset = m_NumPoints;
        // inside storage, including interior edges above
        m_NumPoints += (pf.numPointsForInsideTessFactor[U] - 2) * (pf.numPointsForInsideTessFactor[V] - 2);
    }

    void QuadGeneratePoints(thread const ProcessedQuad& pf) {
        // Generate exterior ring edge points, clockwise from top-left
        int pointOffset = 0;
        for (int edge = 0; edge < QUAD_EDGES; edge++) {
            const int parity = edge & 0x1;
            const int startPoint = 0;
            const int endPoint = pf.numPointsForOutsideEdge[edge] - 1;
            for (int p = startPoint; p < endPoint; p++, pointOffset++) { // next edge starts with the end
                const int q = ((edge == 1) || (edge == 2)) ? p : endPoint - p; // reverse order
                SetTessellationParity(pf.outsideTessFactorParity[edge]);
                const FXP fxpParam = PlacePointIn1D(pf.outsideTessFactorCtx[edge], q);
                if (parity)
                    DefinePoint(fxpParam, (edge == 3) ? FXP_ONE : 0, pointOffset);
                else
                    DefinePoint((edge == 2) ? FXP_ONE : 0, fxpParam, pointOffset);
            }
        }
        // Generate interior ring points, clockwise from (U==0,V==1) (bottom-left) spiralling toward center
        const int startRing = 1;
        const int minNumPointsForTessFactor = min(pf.numPointsForInsideTessFactor[U], pf.numPointsForInsideTessFactor[V]);
        const int numRings = (minNumPointsForTessFactor >> 1); // for even tess we aren't counting center point here.
        for (int ring = startRing; ring < numRings; ring++) {
            const int startPoint = ring;
            const int endPoint[QUAD_AXES] = {pf.numPointsForInsideTessFactor[U] - 1 - startPoint,
                                             pf.numPointsForInsideTessFactor[V] - 1 - startPoint};
            for (int edge = 0; edge < QUAD_EDGES; edge++) {
                const int parity[QUAD_AXES] = {edge & 0x1, ((edge + 1) & 0x1)};
                const int perpendicularAxisPoint = (edge < 2) ? startPoint : endPoint[parity[0]];
                SetTessellationParity(pf.insideTessFactorParity[parity[0]]);
                const FXP fxpPerpParam = PlacePointIn1D(pf.insideTessFactorCtx[parity[0]], perpendicularAxisPoint);
                SetTessellationParity(pf.insideTessFactorParity[parity[1]]);
                for (int p = startPoint; p < endPoint[parity[1]]; p++, pointOffset++) { // next edge starts with the end
                    const int q = ((edge == 1) || (edge == 2)) ? p : endPoint[parity[1]] - (p - startPoint);
                    const FXP fxpParam = PlacePointIn1D(pf.insideTessFactorCtx[parity[1]], q);
                    if (parity[1])
                        DefinePoint(fxpPerpParam, fxpParam, pointOffset);
                    else
                        DefinePoint(fxpParam, fxpPerpParam, pointOffset);
                }
            }
        }
        // For even tessellation, the inner "ring" is degenerate - a row of points
        if ((pf.numPointsForInsideTessFactor[U] > pf.numPointsForInsideTessFactor[V]) &&
            (PARITY_EVEN == pf.insideTessFactorParity[V])) {
            const int startPoint = numRings;
            const int endPoint = pf.numPointsForInsideTessFactor[U] - 1 - startPoint;
            SetTessellationParity(pf.insideTessFactorParity[U]);
            for (int p = startPoint; p <= endPoint; p++, pointOffset++)
                DefinePoint(PlacePointIn1D(pf.insideTessFactorCtx[U], p), FXP_ONE_HALF /*middle*/, pointOffset);
        } else if ((pf.numPointsForInsideTessFactor[V] >= pf.numPointsForInsideTessFactor[U]) &&
                   (PARITY_EVEN == pf.insideTessFactorParity[U])) {
            const int startPoint = numRings;
            const int endPoint = pf.numPointsForInsideTessFactor[V] - 1 - startPoint;
            SetTessellationParity(pf.insideTessFactorParity[V]);
            for (int p = endPoint; p >= startPoint; p--, pointOffset++)
                DefinePoint(FXP_ONE_HALF /*middle*/, PlacePointIn1D(pf.insideTessFactorCtx[V], p), pointOffset);
        }
    }

    void QuadGenerateConnectivity(thread const ProcessedQuad& pf) {
        // Generate primitives for all the concentric rings, one side at a time for each ring
        const int startRing = 1;
        // +1 is so even tess includes the center point
        const int numPointRowsToCenter[QUAD_AXES] = {((pf.numPointsForInsideTessFactor[U] + 1) >> 1),
                                                     ((pf.numPointsForInsideTessFactor[V] + 1) >> 1)};
        const int numRings = min(numPointRowsToCenter[U], numPointRowsToCenter[V]);
        // Even partitioning causes degenerate row of points, which results in exceptions to the point
        // ordering conventions when travelling around the rings counterclockwise.
        const int degeneratePointRing[QUAD_AXES] = {
            (PARITY_EVEN == pf.insideTessFactorParity[V]) ? numPointRowsToCenter[V] - 1 : -1,
            (PARITY_EVEN == pf.insideTessFactorParity[U]) ? numPointRowsToCenter[U] - 1 : -1};
        thread const TessFactorContext* outsideTessFactorCtx[QUAD_EDGES] = {
            &pf.outsideTessFactorCtx[Ueq0], &pf.outsideTessFactorCtx[Veq0], &pf.outsideTessFactorCtx[Ueq1],
            &pf.outsideTessFactorCtx[Veq1]};
        int outsideTessFactorParity[QUAD_EDGES] = {pf.outsideTessFactorParity[Ueq0], pf.outsideTessFactorParity[Veq0],
                                                   pf.outsideTessFactorParity[Ueq1], pf.outsideTessFactorParity[Veq1]};
        int numPointsForOutsideEdge[QUAD_EDGES] = {pf.numPointsForOutsideEdge[Ueq0], pf.numPointsForOutsideEdge[Veq0],
                                                   pf.numPointsForOutsideEdge[Ueq1], pf.numPointsForOutsideEdge[Veq1]};
        int insideEdgePointBaseOffset = pf.insideEdgePointBaseOffset;
        int outsideEdgePointBaseOffset = 0;
        for (int ring = startRing; ring < numRings; ring++) {
            const int numPointsForInsideEdge[QUAD_AXES] = {pf.numPointsForInsideTessFactor[U] - 2 * ring,
                                                           pf.numPointsForInsideTessFactor[V] - 2 * ring};
            const int edge0InsidePointBaseOffset = insideEdgePointBaseOffset;
            const int edge0OutsidePointBaseOffset = outsideEdgePointBaseOffset;
            for (int edge = 0; edge < QUAD_EDGES; edge++) {
                const int parity = (edge + 1) & 0x1;
                const int numTriangles = numPointsForInsideEdge[parity] + numPointsForOutsideEdge[edge] - 2;
                int insideBaseOffset;
                int outsideBaseOffset;
                if (edge == 3) {
                    // Patch the indexing so Stitch() can think it sees 2 sequentially increasing rows of
                    // points, even though we have wrapped around to the end of the inner and outer ring's
                    // points, so the last point is really the first point for the ring.
                    if (ring == degeneratePointRing[parity]) {
                        m_IndexPatchContext2.baseIndexToInvert = insideEdgePointBaseOffset + 1;
                        m_IndexPatchContext2.cornerCaseBadValue = outsideEdgePointBaseOffset + numPointsForOutsideEdge[edge] - 1;
                        m_IndexPatchContext2.cornerCaseReplacementValue = edge0OutsidePointBaseOffset;
                        m_IndexPatchContext2.indexInversionEndPoint = (m_IndexPatchContext2.baseIndexToInvert << 1) - 1;
                        insideBaseOffset = m_IndexPatchContext2.baseIndexToInvert;
                        outsideBaseOffset = outsideEdgePointBaseOffset;
                        SetUsingPatchedIndices2(true);
                    } else {
                        m_IndexPatchContext.insidePointIndexDeltaToRealValue = insideEdgePointBaseOffset;
                        m_IndexPatchContext.insidePointIndexBadValue = numPointsForInsideEdge[parity] - 1;
                        m_IndexPatchContext.insidePointIndexReplacementValue = edge0InsidePointBaseOffset;
                        // past inside patched index range
                        m_IndexPatchContext.outsidePointIndexPatchBase = m_IndexPatchContext.insidePointIndexBadValue + 1;
                        m_IndexPatchContext.outsidePointIndexDeltaToRealValue =
                            outsideEdgePointBaseOffset - m_IndexPatchContext.outsidePointIndexPatchBase;
                        m_IndexPatchContext.outsidePointIndexBadValue =
                            m_IndexPatchContext.outsidePointIndexPatchBase + numPointsForOutsideEdge[edge] - 1;
                        m_IndexPatchContext.outsidePointIndexReplacementValue = edge0OutsidePointBaseOffset;
                        insideBaseOffset = 0;
                        outsideBaseOffset = m_IndexPatchContext.outsidePointIndexPatchBase;
                        SetUsingPatchedIndices(true);
                    }
                } else if ((edge == 2) && (ring == degeneratePointRing[parity])) {
                    m_IndexPatchContext2.baseIndexToInvert = insideEdgePointBaseOffset;
                    m_IndexPatchContext2.cornerCaseBadValue = -1;         // unused
                    m_IndexPatchContext2.cornerCaseReplacementValue = -1; // unused
                    m_IndexPatchContext2.indexInversionEndPoint = m_IndexPatchContext2.baseIndexToInvert << 1;
                    insideBaseOffset = m_IndexPatchContext2.baseIndexToInvert;
                    outsideBaseOffset = outsideEdgePointBaseOffset;
                    SetUsingPatchedIndices2(true);
                } else {
                    insideBaseOffset = insideEdgePointBaseOffset;
                    outsideBaseOffset = outsideEdgePointBaseOffset;
                }
                if (ring == startRing) {
                    StitchTransition(m_NumIndices, insideBaseOffset, pf.insideTessFactorCtx[parity].numHalfTessFactorPoints,
                                     pf.insideTessFactorParity[parity], outsideBaseOffset,
                                     outsideTessFactorCtx[edge]->numHalfTessFactorPoints, outsideTessFactorParity[edge]);
                } else {
                    StitchRegular(/*bTrapezoid*/ true, DIAGONALS_MIRRORED, m_NumIndices, numPointsForInsideEdge[parity],
                                  insideBaseOffset, outsideBaseOffset);
                }
                SetUsingPatchedIndices(false);
                SetUsingPatchedIndices2(false);
                m_NumIndices += numTriangles * 3;
                outsideEdgePointBaseOffset += numPointsForOutsideEdge[edge] - 1;
                if ((edge == 2) && (ring == degeneratePointRing[parity]))
                    insideEdgePointBaseOffset -= numPointsForInsideEdge[parity] - 1;
                else
                    insideEdgePointBaseOffset += numPointsForInsideEdge[parity] - 1;
                numPointsForOutsideEdge[edge] = numPointsForInsideEdge[parity];
            }
            if (startRing == ring) {
                for (int edge = 0; edge < QUAD_EDGES; edge++) {
                    outsideTessFactorCtx[edge] = &pf.insideTessFactorCtx[edge & 1];
                    outsideTessFactorParity[edge] = pf.insideTessFactorParity[edge & 1];
                }
            }
        }
        // Triangulate center - a row of quads if odd
        if ((pf.numPointsForInsideTessFactor[U] > pf.numPointsForInsideTessFactor[V]) &&
            (PARITY_ODD == pf.insideTessFactorParity[V])) {
            SetUsingPatchedIndices2(true);
            const int stripNumQuads =
                (((pf.numPointsForInsideTessFactor[U] >> 1) - (pf.numPointsForInsideTessFactor[V] >> 1)) << 1) +
                ((PARITY_EVEN == pf.insideTessFactorParity[U]) ? 2 : 1);
            m_IndexPatchContext2.baseIndexToInvert = outsideEdgePointBaseOffset + stripNumQuads + 2;
            m_IndexPatchContext2.cornerCaseBadValue = m_IndexPatchContext2.baseIndexToInvert;
            m_IndexPatchContext2.cornerCaseReplacementValue = outsideEdgePointBaseOffset;
            m_IndexPatchContext2.indexInversionEndPoint =
                m_IndexPatchContext2.baseIndexToInvert + m_IndexPatchContext2.baseIndexToInvert + stripNumQuads;
            StitchRegular(/*bTrapezoid*/ false, DIAGONALS_INSIDE_TO_OUTSIDE, m_NumIndices, stripNumQuads + 1,
                          m_IndexPatchContext2.baseIndexToInvert, outsideEdgePointBaseOffset + 1);
            SetUsingPatchedIndices2(false);
            m_NumIndices += stripNumQuads * 6;
        } else if ((pf.numPointsForInsideTessFactor[V] >= pf.numPointsForInsideTessFactor[U]) &&
                   (PARITY_ODD == pf.insideTessFactorParity[U])) {
            SetUsingPatchedIndices2(true);
            const int stripNumQuads =
                (((pf.numPointsForInsideTessFactor[V] >> 1) - (pf.numPointsForInsideTessFactor[U] >> 1)) << 1) +
                ((PARITY_EVEN == pf.insideTessFactorParity[V]) ? 2 : 1);
            m_IndexPatchContext2.baseIndexToInvert = outsideEdgePointBaseOffset + stripNumQuads + 1;
            m_IndexPatchContext2.cornerCaseBadValue = -1; // unused
            m_IndexPatchContext2.indexInversionEndPoint =
                m_IndexPatchContext2.baseIndexToInvert + m_IndexPatchContext2.baseIndexToInvert + stripNumQuads;
            const int diag = (PARITY_EVEN == pf.insideTessFactorParity[V]) ? DIAGONALS_INSIDE_TO_OUTSIDE
                                                                            : DIAGONALS_INSIDE_TO_OUTSIDE_EXCEPT_MIDDLE;
            StitchRegular(/*bTrapezoid*/ false, diag, m_NumIndices, stripNumQuads + 1,
                          m_IndexPatchContext2.baseIndexToInvert, outsideEdgePointBaseOffset);
            SetUsingPatchedIndices2(false);
            m_NumIndices += stripNumQuads * 6;
        }
    }

    void TessellateQuadDomain(float tessFactor_Ueq0, float tessFactor_Veq0, float tessFactor_Ueq1, float tessFactor_Veq1,
                              float insideTessFactor_U, float insideTessFactor_V) {
        ProcessedQuad pf;
        QuadProcessTessFactors(tessFactor_Ueq0, tessFactor_Veq0, tessFactor_Ueq1, tessFactor_Veq1, insideTessFactor_U,
                               insideTessFactor_V, pf);
        if (pf.bPatchCulled) {
            m_NumPoints = 0;
            m_NumIndices = 0;
            return;
        }
        if (pf.bJustDoMinimumTessFactor) {
            DefinePoint(0, 0, 0);
            DefinePoint(FXP_ONE, 0, 1);
            DefinePoint(FXP_ONE, FXP_ONE, 2);
            DefinePoint(0, FXP_ONE, 3);
            m_NumPoints = 4;
            if (m_outputPrimitive == OUTPUT_POINT) {
                DumpAllPoints();
            } else {
                // function orients them CCW if needed
                DefineClockwiseTriangle(0, 1, 3, 0);
                DefineClockwiseTriangle(1, 2, 3, 3);
                m_NumIndices = 6;
            }
            return;
        }
        QuadGeneratePoints(pf);
        if (m_outputPrimitive == OUTPUT_POINT) {
            DumpAllPoints();
            return;
        }
        QuadGenerateConnectivity(pf);
    }

    // ---- tri domain
    void TriProcessTessFactors(float tessFactor_Ueq0, float tessFactor_Veq0, float tessFactor_Weq0,
                               float insideTessFactor, thread ProcessedTri& pf) {
        // Is the patch culled? (NaN will pass)
        if (!(tessFactor_Ueq0 > 0) || !(tessFactor_Veq0 > 0) || !(tessFactor_Weq0 > 0)) {
            pf.bPatchCulled = true;
            return;
        }
        pf.bPatchCulled = false;
        float lowerBound, upperBound;
        Bounds(lowerBound, upperBound);
        tessFactor_Ueq0 = d3dMin(upperBound, d3dMax(lowerBound, tessFactor_Ueq0));
        tessFactor_Veq0 = d3dMin(upperBound, d3dMax(lowerBound, tessFactor_Veq0));
        tessFactor_Weq0 = d3dMin(upperBound, d3dMax(lowerBound, tessFactor_Weq0));
        if (HWIntegerPartitioning()) {
            tessFactor_Ueq0 = ceil(tessFactor_Ueq0);
            tessFactor_Veq0 = ceil(tessFactor_Veq0);
            tessFactor_Weq0 = ceil(tessFactor_Weq0);
        }
        // Clamp inside TessFactors
        if (m_originalPartitioning == PARTITIONING_FRACTIONAL_ODD) {
            if ((tessFactor_Ueq0 > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON) ||
                (tessFactor_Veq0 > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON) ||
                (tessFactor_Weq0 > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON))
                lowerBound = MIN_ODD_TESSELLATION_FACTOR + EPSILON; // Force picture frame
        }
        insideTessFactor = d3dMin(upperBound, d3dMax(lowerBound, insideTessFactor));
        // Note the above clamps map NaN to lowerBound
        if (HWIntegerPartitioning()) insideTessFactor = ceil(insideTessFactor);
        m_NumPoints = 0;
        m_NumIndices = 0;
        const float outsideTessFactor[TRI_EDGES] = {tessFactor_Ueq0, tessFactor_Veq0, tessFactor_Weq0};
        if (HWIntegerPartitioning()) {
            for (int edge = 0; edge < TRI_EDGES; edge++)
                pf.outsideTessFactorParity[edge] = isEven(outsideTessFactor[edge]) ? PARITY_EVEN : PARITY_ODD;
            pf.insideTessFactorParity =
                (isEven(insideTessFactor) || (1.0f == insideTessFactor)) ? PARITY_EVEN : PARITY_ODD;
        } else {
            for (int edge = 0; edge < TRI_EDGES; edge++) pf.outsideTessFactorParity[edge] = m_originalParity;
            pf.insideTessFactorParity = m_originalParity;
        }
        for (int edge = 0; edge < TRI_EDGES; edge++) pf.outsideTessFactor[edge] = floatToFixed(outsideTessFactor[edge]);
        pf.insideTessFactor = floatToFixed(insideTessFactor);
        if (HWIntegerPartitioning() || Odd()) {
            // Special case if all TessFactors are 1
            if ((FXP_ONE == pf.insideTessFactor) && (FXP_ONE == pf.outsideTessFactor[Ueq0]) &&
                (FXP_ONE == pf.outsideTessFactor[Veq0]) && (FXP_ONE == pf.outsideTessFactor[Weq0])) {
                pf.bJustDoMinimumTessFactor = true;
                return;
            }
        }
        pf.bJustDoMinimumTessFactor = false;
        for (int edge = 0; edge < TRI_EDGES; edge++) {
            SetTessellationParity(pf.outsideTessFactorParity[edge]);
            ComputeTessFactorContext(pf.outsideTessFactor[edge], pf.outsideTessFactorCtx[edge]);
        }
        SetTessellationParity(pf.insideTessFactorParity);
        ComputeTessFactorContext(pf.insideTessFactor, pf.insideTessFactorCtx);
        // outside edge offsets and storage
        for (int edge = 0; edge < TRI_EDGES; edge++) {
            SetTessellationParity(pf.outsideTessFactorParity[edge]);
            pf.numPointsForOutsideEdge[edge] = NumPointsForTessFactor(pf.outsideTessFactor[edge]);
            m_NumPoints += pf.numPointsForOutsideEdge[edge];
        }
        m_NumPoints -= 3;
        // inside edge offsets
        SetTessellationParity(pf.insideTessFactorParity);
        pf.numPointsForInsideTessFactor = NumPointsForTessFactor(pf.insideTessFactor);
        {
            const int pointCountMin = Odd() ? 4 : 3;
            // max() allows degenerate transition regions when inside TessFactor == 1
            pf.numPointsForInsideTessFactor = max(pointCountMin, pf.numPointsForInsideTessFactor);
        }
        pf.insideEdgePointBaseOffset = m_NumPoints;
        // inside storage, including interior edges above
        {
            const int numInteriorRings = (pf.numPointsForInsideTessFactor >> 1) - 1;
            int numInteriorPoints;
            if (Odd())
                numInteriorPoints = TRI_EDGES * (numInteriorRings * (numInteriorRings + 1) - numInteriorRings);
            else
                numInteriorPoints = TRI_EDGES * (numInteriorRings * (numInteriorRings + 1)) + 1;
            m_NumPoints += numInteriorPoints;
        }
    }

    void TriGeneratePoints(thread const ProcessedTri& pf) {
        // Generate exterior ring edge points, clockwise starting from point V (VW, the U==0 edge)
        int pointOffset = 0;
        for (int edge = 0; edge < TRI_EDGES; edge++) {
            const int parity = edge & 0x1;
            const int startPoint = 0;
            const int endPoint = pf.numPointsForOutsideEdge[edge] - 1;
            for (int p = startPoint; p < endPoint; p++, pointOffset++) { // next edge starts with the end
                // whether to reverse point order given we are defining V or U (W implicit)
                const int q = (parity) ? p : endPoint - p;
                SetTessellationParity(pf.outsideTessFactorParity[edge]);
                const FXP fxpParam = PlacePointIn1D(pf.outsideTessFactorCtx[edge], q);
                if (edge == 0)
                    DefinePoint(0, fxpParam, pointOffset);
                else
                    DefinePoint(fxpParam, (edge == 2) ? FXP_ONE - fxpParam : 0, pointOffset);
            }
        }
        // Generate interior ring points, clockwise spiralling in
        SetTessellationParity(pf.insideTessFactorParity);
        const int startRing = 1;
        const int numRings = (pf.numPointsForInsideTessFactor >> 1);
        for (int ring = startRing; ring < numRings; ring++) {
            const int startPoint = ring;
            const int endPoint = pf.numPointsForInsideTessFactor - 1 - startPoint;
            for (int edge = 0; edge < TRI_EDGES; edge++) {
                const int parity = edge & 0x1;
                const int perpendicularAxisPoint = startPoint;
                FXP fxpPerpParam = PlacePointIn1D(pf.insideTessFactorCtx, perpendicularAxisPoint);
                // Map location to the right size in barycentric space (no over/underflow).
                fxpPerpParam *= FXP_TWO_THIRDS;
                fxpPerpParam = (fxpPerpParam + FXP_ONE_HALF /*round*/) >> FXP_FRACTION_BITS; // get back to n.16
                for (int p = startPoint; p < endPoint; p++, pointOffset++) { // next edge starts with the end
                    const int q = (parity) ? p : endPoint - (p - startPoint);
                    const FXP fxpParam = PlacePointIn1D(pf.insideTessFactorCtx, q);
                    // edge0 VW has perpendicular parameter U constant, edge1 WU V, edge2 UV W
                    const uint deriv = 2; // reciprocal is the rate of change of edge-parallel parameters
                    switch (edge) {
                    case 0:
                        DefinePoint(fxpPerpParam, fxpParam - (fxpPerpParam + 1 /*round*/) / deriv, pointOffset);
                        break;
                    case 1:
                        DefinePoint(fxpParam - (fxpPerpParam + 1 /*round*/) / deriv, fxpPerpParam, pointOffset);
                        break;
                    case 2:
                        DefinePoint(fxpParam - (fxpPerpParam + 1 /*round*/) / deriv,
                                    FXP_ONE - (fxpParam - (fxpPerpParam + 1 /*round*/) / deriv) - fxpPerpParam,
                                    pointOffset);
                        break;
                    }
                }
            }
        }
        if (!Odd()) DefinePoint(FXP_ONE_THIRD, FXP_ONE_THIRD, pointOffset); // Last point is the point at the center.
    }

    void TriGenerateConnectivity(thread const ProcessedTri& pf) {
        // Generate primitives for all the concentric rings, one side at a time for each ring
        const int startRing = 1;
        // +1 is so even tess includes the center point, which we want to now
        const int numRings = ((pf.numPointsForInsideTessFactor + 1) >> 1);
        thread const TessFactorContext* outsideTessFactorCtx[TRI_EDGES] = {
            &pf.outsideTessFactorCtx[Ueq0], &pf.outsideTessFactorCtx[Veq0], &pf.outsideTessFactorCtx[Weq0]};
        int outsideTessFactorParity[TRI_EDGES] = {pf.outsideTessFactorParity[Ueq0], pf.outsideTessFactorParity[Veq0],
                                                  pf.outsideTessFactorParity[Weq0]};
        int numPointsForOutsideEdge[TRI_EDGES] = {pf.numPointsForOutsideEdge[Ueq0], pf.numPointsForOutsideEdge[Veq0],
                                                  pf.numPointsForOutsideEdge[Weq0]};
        int insideEdgePointBaseOffset = pf.insideEdgePointBaseOffset;
        int outsideEdgePointBaseOffset = 0;
        for (int ring = startRing; ring < numRings; ring++) {
            const int numPointsForInsideEdge = pf.numPointsForInsideTessFactor - 2 * ring;
            const int edge0InsidePointBaseOffset = insideEdgePointBaseOffset;
            const int edge0OutsidePointBaseOffset = outsideEdgePointBaseOffset;
            for (int edge = 0; edge < TRI_EDGES; edge++) {
                const int numTriangles = numPointsForInsideEdge + numPointsForOutsideEdge[edge] - 2;
                int insideBaseOffset;
                int outsideBaseOffset;
                if (edge == 2) {
                    m_IndexPatchContext.insidePointIndexDeltaToRealValue = insideEdgePointBaseOffset;
                    m_IndexPatchContext.insidePointIndexBadValue = numPointsForInsideEdge - 1;
                    m_IndexPatchContext.insidePointIndexReplacementValue = edge0InsidePointBaseOffset;
                    // past inside patched index range
                    m_IndexPatchContext.outsidePointIndexPatchBase = m_IndexPatchContext.insidePointIndexBadValue + 1;
                    m_IndexPatchContext.outsidePointIndexDeltaToRealValue =
                        outsideEdgePointBaseOffset - m_IndexPatchContext.outsidePointIndexPatchBase;
                    m_IndexPatchContext.outsidePointIndexBadValue =
                        m_IndexPatchContext.outsidePointIndexPatchBase + numPointsForOutsideEdge[edge] - 1;
                    m_IndexPatchContext.outsidePointIndexReplacementValue = edge0OutsidePointBaseOffset;
                    SetUsingPatchedIndices(true);
                    insideBaseOffset = 0;
                    outsideBaseOffset = m_IndexPatchContext.outsidePointIndexPatchBase;
                } else {
                    insideBaseOffset = insideEdgePointBaseOffset;
                    outsideBaseOffset = outsideEdgePointBaseOffset;
                }
                if (ring == startRing) {
                    StitchTransition(m_NumIndices, insideBaseOffset, pf.insideTessFactorCtx.numHalfTessFactorPoints,
                                     pf.insideTessFactorParity, outsideBaseOffset,
                                     outsideTessFactorCtx[edge]->numHalfTessFactorPoints, outsideTessFactorParity[edge]);
                } else {
                    StitchRegular(/*bTrapezoid*/ true, DIAGONALS_MIRRORED, m_NumIndices, numPointsForInsideEdge,
                                  insideBaseOffset, outsideBaseOffset);
                }
                if (2 == edge) SetUsingPatchedIndices(false);
                m_NumIndices += numTriangles * 3;
                outsideEdgePointBaseOffset += numPointsForOutsideEdge[edge] - 1;
                insideEdgePointBaseOffset += numPointsForInsideEdge - 1;
                numPointsForOutsideEdge[edge] = numPointsForInsideEdge;
            }
            if (startRing == ring) {
                for (int edge = 0; edge < TRI_EDGES; edge++) {
                    outsideTessFactorCtx[edge] = &pf.insideTessFactorCtx;
                    outsideTessFactorParity[edge] = pf.insideTessFactorParity;
                }
            }
        }
        if (Odd()) {
            // Triangulate center (a single triangle)
            DefineClockwiseTriangle(outsideEdgePointBaseOffset, outsideEdgePointBaseOffset + 1,
                                    outsideEdgePointBaseOffset + 2, m_NumIndices);
            m_NumIndices += 3;
        }
    }

    void TessellateTriDomain(float tessFactor_Ueq0, float tessFactor_Veq0, float tessFactor_Weq0, float insideTessFactor) {
        ProcessedTri pf;
        TriProcessTessFactors(tessFactor_Ueq0, tessFactor_Veq0, tessFactor_Weq0, insideTessFactor, pf);
        if (pf.bPatchCulled) {
            m_NumPoints = 0;
            m_NumIndices = 0;
            return;
        }
        if (pf.bJustDoMinimumTessFactor) {
            DefinePoint(0, FXP_ONE, 0); // V=1 (beginning of Ueq0 edge VW)
            DefinePoint(0, 0, 1);       // W=1 (beginning of Veq0 edge WU)
            DefinePoint(FXP_ONE, 0, 2); // U=1 (beginning of Weq0 edge UV)
            m_NumPoints = 3;
            if (m_outputPrimitive == OUTPUT_POINT) {
                DumpAllPoints();
            } else {
                // function orients them CCW if needed
                DefineClockwiseTriangle(0, 1, 2, m_NumIndices);
                m_NumIndices = 3;
            }
            return;
        }
        TriGeneratePoints(pf);
        if (m_outputPrimitive == OUTPUT_POINT) {
            DumpAllPoints();
            return;
        }
        TriGenerateConnectivity(pf);
    }
};

struct Params {
    uint domain;
    uint partitioning;
    uint output;
    uint patch_count;
    uint point_capacity;
    uint index_capacity;
    uint base_instance;
    uint pad;
};

struct Result {
    uint draw[5];
    uint dropped;
    uint needed_points;
    uint needed_indices;
};

// One patch, counted (points / indices null) or emitted.
static void Tessellate(constant Params& p, const device half* factors, uint patch, device float4* points,
                       device uint* indices, uint point_base, uint index_base, thread int& num_points,
                       thread int& num_indices) {
    Tessellator t;
    t.Init(int(p.partitioning), int(p.output), int(p.domain));
    t.m_points = points;
    t.m_indices = indices;
    t.m_pointBase = point_base;
    t.m_indexBase = index_base;
    t.m_patch = patch;
    if (p.domain == DOMAIN_TRI) {
        const device half* f = factors + patch * 4; // MTLTriangleTessellationFactorsHalf
        t.TessellateTriDomain(float(f[0]), float(f[1]), float(f[2]), float(f[3]));
    } else {
        const device half* f = factors + patch * 6; // MTLQuadTessellationFactorsHalf
        t.TessellateQuadDomain(float(f[0]), float(f[1]), float(f[2]), float(f[3]), float(f[4]), float(f[5]));
    }
    num_points = t.m_NumPoints;
    num_indices = t.m_NumIndices;
}

kernel void ps4_tess_count(constant Params& p [[buffer(0)]], const device half* factors [[buffer(1)]],
                           device uint2* counts [[buffer(2)]], uint id [[thread_position_in_grid]]) {
    if (id >= p.patch_count) return;
    int num_points, num_indices;
    Tessellate(p, factors, id, nullptr, nullptr, 0, 0, num_points, num_indices);
    counts[id] = uint2(uint(num_points), uint(num_indices));
}

// shortcut: one thread sums the patches in order; a parallel scan if patch counts make it slow.
kernel void ps4_tess_offsets(constant Params& p [[buffer(0)]], const device uint2* counts [[buffer(2)]],
                             device uint2* offsets [[buffer(3)]], device Result* result [[buffer(4)]],
                             device atomic_uint* feedback [[buffer(7)]], uint id [[thread_position_in_grid]]) {
    if (id != 0) return;
    uint points = 0, indices = 0, needed_points = 0, needed_indices = 0, dropped = 0;
    for (uint i = 0; i < p.patch_count; ++i) {
        const uint2 c = counts[i];
        needed_points += c.x;
        needed_indices += c.y;
        if (points + c.x > p.point_capacity || indices + c.y > p.index_capacity) {
            offsets[i] = uint2(0xffffffffu);
            dropped += 1;
            continue;
        }
        offsets[i] = uint2(points, indices);
        points += c.x;
        indices += c.y;
    }
    result->draw[0] = indices; // indexCount
    result->draw[1] = 1;       // instanceCount
    result->draw[2] = 0;       // indexStart
    result->draw[3] = 0;       // baseVertex
    result->draw[4] = p.base_instance;
    result->dropped = dropped;
    result->needed_points = needed_points;
    result->needed_indices = needed_indices;
    atomic_fetch_max_explicit(&feedback[0], needed_points, memory_order_relaxed);
    atomic_fetch_max_explicit(&feedback[1], needed_indices, memory_order_relaxed);
}

kernel void ps4_tess_emit(constant Params& p [[buffer(0)]], const device half* factors [[buffer(1)]],
                          const device uint2* offsets [[buffer(3)]], device float4* points [[buffer(5)]],
                          device uint* indices [[buffer(6)]], uint id [[thread_position_in_grid]]) {
    if (id >= p.patch_count) return;
    const uint2 base = offsets[id];
    if (base.x == 0xffffffffu) return; // dropped: over the capacity
    int num_points, num_indices;
    Tessellate(p, factors, id, points, indices, base.x, base.y, num_points, num_indices);
}
)MSL";

const char* KernelSource() { return kSource; }

bool MakeKernels(id<MTLDevice> device, Kernels* out, std::string* why) {
    MTLCompileOptions* opts = [MTLCompileOptions new];
    // as the renderer's other libraries (ps4_gnm_draw.mm CompileOptions)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    opts.fastMathEnabled = NO;
#pragma clang diagnostic pop
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:@(kSource) options:opts error:&err];
    if (!lib) {
        if (why) *why = err ? err.localizedDescription.UTF8String : "the tessellator library did not compile";
        return false;
    }
    id<MTLComputePipelineState> pso[3];
    const char* names[3] = {"ps4_tess_count", "ps4_tess_offsets", "ps4_tess_emit"};
    for (int i = 0; i < 3; ++i) {
        id<MTLFunction> fn = [lib newFunctionWithName:@(names[i])];
        err = nil;
        pso[i] = fn ? [device newComputePipelineStateWithFunction:fn error:&err] : nil;
        if (!pso[i]) {
            if (why) *why = std::string(names[i]) + ": " + (err ? err.localizedDescription.UTF8String : "no function");
            return false;
        }
    }
    out->count = pso[0];
    out->offsets = pso[1];
    out->emit = pso[2];
    return true;
}

} // namespace ps4gnm_tess
