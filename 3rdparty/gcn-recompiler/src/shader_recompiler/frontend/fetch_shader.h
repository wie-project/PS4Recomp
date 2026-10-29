// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/small_vector.h"
#include "common/types.h"
#include "shader_recompiler/info.h"

namespace Serialization {
struct Archive;
}

namespace Shader::Gcn {

struct VertexAttribute {
    enum InstanceIdType : u8 {
        None = 0,
        OverStepRate0 = 1,
        OverStepRate1 = 2,
        Plain = 3,
    };

    u8 dest_vgpr;     ///< Destination VGPR to load first component.
    u8 num_elements;  ///< Number of components to load
    u8 sgpr_base;     ///< SGPR that contains the pointer to the list of vertex V#
    u8 dword_offset;  ///< The dword offset of the V# that describes this attribute.
    u8 instance_data; ///< Indicates that the buffer will be accessed in instance rate
    u16 inst_offset;  ///< Instruction offset applied on the formatted buffer loads (12 bits)
    u8 data_format;   ///< Data format override when typed buffer loads are used
    u8 num_format;    ///< Number format override when typed buffer loads are used

    InstanceIdType GetStepRate() const {
        return static_cast<InstanceIdType>(instance_data);
    }

    constexpr AmdGpu::Buffer GetSharp(const Shader::Info& info) const {
        auto buffer = info.ReadUdReg<AmdGpu::Buffer>(sgpr_base, dword_offset);
        buffer.base_address += inst_offset;
        if (data_format) {
            buffer.data_format = data_format;
            buffer.num_format = num_format;
        }
        return buffer;
    }

    bool operator==(const VertexAttribute& other) const = default;
};

struct FetchShaderData {
    u32 size = 0;
    SmallVector<VertexAttribute, 8> attributes;
    s8 vertex_offset_sgpr = -1;   ///< SGPR of vertex offset from VADDR
    s8 instance_offset_sgpr = -1; ///< SGPR of instance offset from VADDR

    bool operator==(const FetchShaderData& other) const {
        return attributes == other.attributes && vertex_offset_sgpr == other.vertex_offset_sgpr &&
               instance_offset_sgpr == other.instance_offset_sgpr;
    }

    bool Empty() const {
        return size == 0;
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& buffer);
};

// PS4Recomp: whether Metal's vertex fetch has a vertex format for the V#'s (remapped) data and
// number formats. Mirrors the renderer's table (ps4_gnm_resources.mm GetVertexFormat, macOS 14:
// RG11B10 / RGB9E5 float; 2_10_10_10 other than unorm / snorm is read as a uint and unpacked).
inline bool IsVertexFormatFetchable(AmdGpu::DataFormat d, AmdGpu::NumberFormat n) {
    using D = AmdGpu::DataFormat;
    using N = AmdGpu::NumberFormat;
    const bool norm_int = n == N::Unorm || n == N::Snorm || n == N::Uint || n == N::Sint;
    switch (d) {
    case D::Format8:
    case D::Format8_8:
    case D::Format8_8_8_8:
        return norm_int;
    case D::Format16:
    case D::Format16_16:
    case D::Format16_16_16_16:
        return norm_int || n == N::Float;
    case D::Format32:
    case D::Format32_32:
    case D::Format32_32_32:
    case D::Format32_32_32_32:
        return n == N::Uint || n == N::Sint || n == N::Float;
    case D::Format2_10_10_10:
        return true;
    case D::Format10_11_11:
    case D::Format5_9_9_9:
        return n == N::Float;
    default:
        return false;
    }
}

// PS4Recomp: false for an attribute V# Metal's vertex fetch cannot read. A vertex buffer layout
// stride and an attribute offset must be multiples of 4 bytes there (MTLVertexDescriptor), while
// GCN takes any stride and any byte address (packed half2 or 8-bit data at stride 2; a fetch
// instruction offset that is not a multiple of 4), and Metal has no vertex format for some data
// formats GCN loads (IsVertexFormatFetchable). A shader with such an attribute reads all of its
// attributes from the buffers in the shader: Translator::EmitFetch then translates the fetch
// shader's buffer loads (Info::fetch_shader_inlined).
enum VertexFetchProblem : u8 {
    VertexFetchStride = 1,  ///< stride not a multiple of 4
    VertexFetchAddress = 2, ///< base address (with the instruction offset) not a multiple of 4
    VertexFetchFormat = 4,  ///< no Metal vertex format (IsVertexFormatFetchable)
};
inline u8 VertexFetchProblems(const AmdGpu::Buffer& sharp) {
    return u8(((sharp.GetStride() & 3) != 0 ? VertexFetchStride : 0) |
              ((sharp.base_address & 3) != 0 ? VertexFetchAddress : 0) |
              (!IsVertexFormatFetchable(sharp.GetDataFmt(), sharp.GetNumberFmt()) ? VertexFetchFormat : 0));
}
inline bool IsVertexFetchable(const AmdGpu::Buffer& sharp) {
    return VertexFetchProblems(sharp) == 0;
}

const u32* GetFetchShaderCode(const Info& info, u32 sgpr_base);

bool ParseFetchShader(const Shader::Info& info, FetchShaderData& out_fetch_data);

// PS4Recomp: the fetch shader the user SGPRs at sgpr_base point to, whatever has_fetch_shader says
bool ParseFetchShaderAt(const Shader::Info& info, u32 sgpr_base, FetchShaderData& out_fetch_data);

// PS4Recomp: FNV-1a of the code (up to and including s_setpc_b64) of the fetch shader of a VS
// that translated it inline (Info::fetch_shader_inlined): that VS's permutation holds the fetch
// shader's instructions, so it serves only draws that call the same ones (Gnmx builds a fetch
// shader per remap table and instancing mode for one VS). A scan of the instruction lengths: no
// parse, no allocation (it runs for every draw's specialization of such a VS).
u64 InlinedFetchShaderHash(const Shader::Info& info);

} // namespace Shader::Gcn
