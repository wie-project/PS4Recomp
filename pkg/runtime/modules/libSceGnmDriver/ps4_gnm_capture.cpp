// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - pass-level frame capture (see ps4_gnm_capture.h).
#include "ps4_gnm_capture.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

#include <algorithm>
#include <cmath>
#include <memory>
#include <type_traits>

namespace ps4gnm_capture {

uint32_t BytesPerTexel(uint32_t f) {
    switch (f) {
    case kR8Unorm: case kR8Snorm: case kR8Uint: case kR8Sint: case kStencil8: return 1;
    case kR16Unorm: case kR16Snorm: case kR16Uint: case kR16Sint: case kR16Float:
    case kRG8Unorm: case kRG8Snorm: case kRG8Uint: case kDepth16Unorm: return 2;
    case kR32Uint: case kR32Sint: case kR32Float: case kRG16Unorm: case kRG16Snorm: case kRG16Uint:
    case kRG16Float: case kRGBA8Unorm: case kRGBA8Unorm_sRGB: case kRGBA8Snorm: case kRGBA8Uint:
    case kBGRA8Unorm: case kBGRA8Unorm_sRGB: case kRGB10A2Unorm: case kRG11B10Float: case kRGB9E5Float:
    case kDepth32Float: case kDepth32Float_Stencil8: return 4;
    case kRG32Uint: case kRG32Float: case kRGBA16Unorm: case kRGBA16Snorm: case kRGBA16Uint:
    case kRGBA16Float: return 8;
    case kRGBA32Uint: case kRGBA32Float: return 16;
    default: return 0;
    }
}

namespace {
constexpr uint32_t kMtlTypeCube = 5, kMtlTypeCubeArray = 6, kMtlType3D = 7;

uint64_t PlaneSize(const GpuTextureId& t, uint32_t bpe) {
    const uint32_t slices = t.mtl_array * ((t.mtl_type == kMtlTypeCube || t.mtl_type == kMtlTypeCubeArray) ? 6 : 1);
    uint64_t n = 0;
    for (uint32_t l = 0; l < t.mtl_levels; ++l) {
        const uint64_t w = std::max(1u, t.mtl_width >> l), h = std::max(1u, t.mtl_height >> l);
        const uint64_t d = t.mtl_type == kMtlType3D ? std::max(1u, t.mtl_depth >> l) : 1;
        n += w * h * d * slices * bpe;
    }
    return n;
}
} // namespace

uint64_t RawSize(const GpuTextureId& t) {
    const uint32_t bpe = BytesPerTexel(t.mtl_format);
    if (!bpe) return 0;
    uint64_t n = PlaneSize(t, bpe);
    if (t.mtl_format == kDepth32Float_Stencil8) n += PlaneSize(t, 1);
    return n;
}

uint64_t Fnv64(const void* data, size_t n, uint64_t h) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

std::vector<std::pair<uint64_t, uint64_t>> CoalescePages(std::vector<std::pair<uint64_t, uint64_t>> r,
                                                         uint64_t mem_size) {
    std::vector<std::pair<uint64_t, uint64_t>> pages; // [lo, hi)
    for (auto& [a, n] : r) {
        if (!n || !a || a >= mem_size) continue;
        const uint64_t lo = a & ~(kPage - 1);
        const uint64_t hi = std::min(mem_size, (a + n + kPage - 1) & ~(kPage - 1));
        pages.push_back({lo, hi});
    }
    std::sort(pages.begin(), pages.end());
    std::vector<std::pair<uint64_t, uint64_t>> out;
    for (auto& p : pages) {
        if (!out.empty() && p.first <= out.back().second) out.back().second = std::max(out.back().second, p.second);
        else out.push_back(p);
    }
    for (auto& p : out) p.second -= p.first; // -> (addr, size)
    return out;
}

std::vector<std::pair<uint64_t, uint64_t>> ClampReadable(const uint8_t* mem_base,
                                                         const std::vector<std::pair<uint64_t, uint64_t>>& ranges) {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    const uint64_t base = (uint64_t)(uintptr_t)mem_base;
    for (const auto& [a, n] : ranges) {
        uint64_t lo = base + a;
        const uint64_t hi = lo + n;
#if defined(__APPLE__)
        while (lo < hi) {
            mach_vm_address_t r = lo;
            mach_vm_size_t rsize = 0;
            vm_region_basic_info_data_64_t info;
            mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object = MACH_PORT_NULL;
            // the region containing lo, or the next one above it
            if (mach_vm_region(mach_task_self(), &r, &rsize, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count,
                               &object) != KERN_SUCCESS ||
                r >= hi)
                break;
            const uint64_t from = std::max<uint64_t>(lo, r), to = std::min<uint64_t>(hi, r + rsize);
            if (info.protection & VM_PROT_READ) {
                if (!out.empty() && out.back().first + out.back().second == from - base) out.back().second += to - from;
                else out.push_back({from - base, to - from});
            }
            lo = to;
        }
#else
        out.push_back({lo - base, hi - lo}); // no region query on this host
#endif
    }
    return out;
}

namespace {
// (addr, size) ranges -> sorted, merged [lo, hi) intervals; empty ranges are dropped.
void MergeIntervals(const std::vector<ShaderSelection::Range>& in, std::vector<ShaderSelection::Range>* out) {
    out->clear();
    out->reserve(in.size());
    for (const auto& [a, n] : in)
        if (n) out->push_back({a, a + n < a ? UINT64_MAX : a + n});
    std::sort(out->begin(), out->end());
    size_t k = 0;
    for (size_t i = 0; i < out->size(); ++i) {
        if (k && (*out)[i].first <= (*out)[k - 1].second) (*out)[k - 1].second = std::max((*out)[k - 1].second, (*out)[i].second);
        else (*out)[k++] = (*out)[i];
    }
    out->resize(k);
}

// Whether the sorted, merged intervals a and b (both [lo, hi)) overlap.
bool Overlaps(const std::vector<ShaderSelection::Range>& a, const std::vector<ShaderSelection::Range>& b) {
    for (size_t i = 0, j = 0; i < a.size() && j < b.size();) {
        if (a[i].second <= b[j].first) ++i;
        else if (b[j].second <= a[i].first) ++j;
        else return true;
    }
    return false;
}

// rest minus the sorted, merged intervals w (both [lo, hi)); true when w overlapped rest.
bool Subtract(std::vector<ShaderSelection::Range>* rest, const std::vector<ShaderSelection::Range>& w,
              std::vector<ShaderSelection::Range>* scratch) {
    scratch->clear();
    bool hit = false;
    size_t j = 0;
    for (auto [lo, hi] : *rest) {
        while (j < w.size() && w[j].second <= lo) ++j;
        for (size_t k = j; k < w.size() && w[k].first < hi; ++k) {
            hit = true;
            if (w[k].first > lo) scratch->push_back({lo, w[k].first});
            lo = std::max(lo, w[k].second);
            if (lo >= hi) break;
        }
        if (lo < hi) scratch->push_back({lo, hi});
    }
    rest->swap(*scratch);
    return hit;
}
} // namespace

bool ShaderSelection::Parse(const std::string& list, std::string* err) {
    shaders_.clear();
    pgms_.clear();
    static constexpr char kPgm[] = "pgm:";
    size_t i = 0;
    while (i <= list.size()) {
        const size_t e = std::min(list.find(',', i), list.size());
        const bool pgm = list.compare(i, sizeof(kPgm) - 1, kPgm) == 0;
        const std::string item = list.substr(i, e - i);
        const char* num = item.c_str() + (pgm ? sizeof(kPgm) - 1 : 0);
        char* end = nullptr;
        errno = 0;
        // strtoull would take a sign or leading spaces ("-1" -> UINT64_MAX): only digits
        const bool digits = *num >= '0' && *num <= '9';
        const unsigned long long v = digits ? strtoull(num, &end, 0) : 0;
        if (!digits || errno || !end || *end || !v) {
            *err = std::string(pgm ? "not a program hash" : "not a shader code address") + ": \"" + item + "\"";
            shaders_.clear();
            pgms_.clear();
            return false;
        }
        (pgm ? pgms_ : shaders_).push_back(v);
        i = e + 1;
    }
    for (std::vector<uint64_t>* v : {&shaders_, &pgms_}) {
        std::sort(v->begin(), v->end());
        v->erase(std::unique(v->begin(), v->end()), v->end());
    }
    return true;
}

SelectKey ShaderSelection::Next(uint64_t code0, uint64_t code1, uint64_t pgm0, uint64_t pgm1) {
    SelectKey k;
    k.code[0] = code0;
    k.code[1] = code1;
    k.pgm[0] = pgm0;
    k.pgm[1] = pgm1;
    k.occurrence = counts_[{code0, code1}]++;
    return k;
}

bool ShaderSelection::UsesShader(const SelectKey& k) const {
    for (int i = 0; i < 2; ++i) {
        if (k.code[i] && std::binary_search(shaders_.begin(), shaders_.end(), k.code[i])) return true;
        if (k.pgm[i] && std::binary_search(pgms_.begin(), pgms_.end(), k.pgm[i])) return true;
    }
    return false;
}

void ShaderSelection::Learn(const SelectKey& k, const std::vector<Range>& reads, const std::vector<Range>& exact_writes,
                            const std::vector<Range>& may_writes) {
    known_.insert(k);
    Learned l;
    l.key = k;
    l.reader = UsesShader(k);
    if (l.reader) MergeIntervals(reads, &l.reads);
    MergeIntervals(exact_writes, &l.exact);
    MergeIntervals(may_writes, &l.may);
    if (l.reader || !l.exact.empty() || !l.may.empty()) learned_.push_back(std::move(l));
}

void ShaderSelection::FinishLearning() {
    producers_.clear();
    readers_ = 0;
    std::vector<Range> rest, scratch;
    for (size_t i = 0; i < learned_.size(); ++i) {
        if (!learned_[i].reader) continue;
        ++readers_;
        // walking back from the reader, each pass that wrote, or may have written, some of the bytes
        // no later pass wrote exactly
        rest = learned_[i].reads;
        for (size_t j = i; j-- > 0 && !rest.empty();) {
            const bool may = Overlaps(rest, learned_[j].may);
            if (Subtract(&rest, learned_[j].exact, &scratch) || may) producers_.insert(learned_[j].key);
        }
    }
    learned_.clear();
    learned_.shrink_to_fit();
}

bool ReadFile(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseeko(f, 0, SEEK_END);
    const off_t n = ftello(f);
    fseeko(f, 0, SEEK_SET);
    out->resize((size_t)std::max<off_t>(n, 0));
    const bool ok = n >= 0 && fread(out->data(), 1, out->size(), f) == out->size();
    fclose(f);
    return ok;
}

bool WriteFile(const std::string& path, const void* data, size_t n) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

namespace {
std::string Hex(uint64_t v) {
    char b[24];
    snprintf(b, sizeof(b), "0x%llx", (unsigned long long)v);
    return b;
}
bool MkDir(const std::string& p) { return mkdir(p.c_str(), 0755) == 0 || errno == EEXIST; }
} // namespace

Writer::Writer(std::string frame_dir, uint64_t frame, uint64_t mem_size, bool neo, std::string gcn_cache_id)
    : dir_(std::move(frame_dir)) {
    frame_.frame = frame;
    frame_.mem_size = mem_size;
    frame_.neo = neo;
    frame_.gcn_cache_id = std::move(gcn_cache_id);
}

bool Writer::Begin(std::string* err) {
    // parent directories are created one level at a time (dir_ = <capture dir>/frame_<n>)
    const size_t slash = dir_.find_last_of('/');
    if (slash != std::string::npos && slash > 0) MkDir(dir_.substr(0, slash));
    for (const char* sub : {"", "/blobs", "/regs", "/tex", "/png"})
        if (!MkDir(dir_ + sub)) {
            if (err) *err = "cannot create " + dir_ + sub + ": " + strerror(errno);
            return false;
        }
    return true;
}

void Writer::Failed(const std::string& path) {
    if (failures_++ == 0)
        fprintf(stderr, "[gnm-capture] cannot write %s: %s (further write failures are only counted)\n", path.c_str(),
                strerror(errno));
}

std::vector<std::string> ChangedInputs(const uint8_t* mem_base, const std::vector<BlobRef>& inputs,
                                       const std::vector<BlobRef>& outputs) {
    std::vector<std::string> r;
    for (const BlobRef& b : inputs) {
        if (b.file.empty() || b.size == 0) continue; // recorded, not stored
        bool written = false;
        for (const BlobRef& o : outputs) written |= b.addr < o.addr + o.size && o.addr < b.addr + b.size;
        if (written || Fnv64(mem_base + b.addr, b.size) == b.hash) continue;
        r.push_back("input " + Hex(b.addr) + "+" + Hex(b.size));
    }
    return r;
}

BlobRef Writer::Snapshot(const uint8_t* mem_base, uint64_t addr, uint64_t size) {
    BlobRef b;
    b.addr = addr;
    b.size = size;
    if (size > kMaxSnapshot) return b; // recorded, not stored
    b.hash = Fnv64(mem_base + addr, size);
    auto key = std::make_tuple(addr, size, b.hash);
    auto it = blobs_.find(key);
    if (it != blobs_.end()) {
        b.file = it->second;
        return b;
    }
    char name[96];
    snprintf(name, sizeof(name), "blobs/%llx_%llx_%016llx.bin", (unsigned long long)addr, (unsigned long long)size,
             (unsigned long long)b.hash);
    if (!WriteFile(dir_ + "/" + name, mem_base + addr, size)) {
        Failed(dir_ + "/" + name);
        return b;
    }
    b.file = name;
    blobs_[key] = b.file;
    return b;
}

std::string Writer::SaveRegs(const uint32_t* regs, uint64_t* hash) {
    std::vector<uint32_t> pairs;
    for (uint32_t i = 0; i < kNumRegs; ++i)
        if (regs[i]) {
            pairs.push_back(i);
            pairs.push_back(regs[i]);
        }
    const uint64_t h = Fnv64(pairs.data(), pairs.size() * 4);
    *hash = h;
    auto it = regs_.find(h);
    if (it != regs_.end()) return it->second;
    char name[64];
    snprintf(name, sizeof(name), "regs/%016llx.bin", (unsigned long long)h);
    if (!WriteFile(dir_ + "/" + name, pairs.data(), pairs.size() * 4)) {
        Failed(dir_ + "/" + name);
        return "";
    }
    regs_[h] = name;
    return name;
}

std::string Writer::SaveTexture(uint32_t pass, const std::string& role, const char* when,
                                const std::vector<uint8_t>& raw, uint64_t* hash) {
    *hash = Fnv64(raw.data(), raw.size());
    char name[96];
    snprintf(name, sizeof(name), "tex/p%05u_%s_%s.raw", pass, role.c_str(), when);
    if (WriteFile(dir_ + "/" + name, raw.data(), raw.size())) return name;
    Failed(dir_ + "/" + name);
    return "";
}

std::string Writer::SavePng(uint32_t pass, const std::string& role, const GpuTextureId& id,
                            const std::vector<uint8_t>& raw, uint32_t max_width, std::string* mode) {
    const uint32_t bpe = BytesPerTexel(id.mtl_format);
    const uint32_t k = max_width && id.mtl_width > max_width ? (id.mtl_width + max_width - 1) / max_width : 1;
    if (!bpe || raw.size() < (uint64_t)id.mtl_width * id.mtl_height * bpe) {
        *mode = "unsupported format " + std::to_string(id.mtl_format);
        return "";
    }
    // every k-th texel of every k-th row of level 0, slice 0 (a depth+stencil texture: its depth plane)
    GpuTextureId v = id;
    v.mtl_width = (id.mtl_width + k - 1) / k;
    v.mtl_height = (id.mtl_height + k - 1) / k;
    v.mtl_levels = v.mtl_array = v.mtl_depth = 1;
    std::vector<uint8_t> small((size_t)v.mtl_width * v.mtl_height * bpe);
    for (uint32_t y = 0; y < v.mtl_height; ++y)
        for (uint32_t x = 0; x < v.mtl_width; ++x)
            memcpy(&small[((size_t)y * v.mtl_width + x) * bpe], &raw[((size_t)y * k * id.mtl_width + x * k) * bpe], bpe);
    std::vector<uint8_t> rgb;
    if (!ToViewRgb8(v, small.data(), small.size(), &rgb, mode)) return "";
    if (k > 1) *mode += ", 1/" + std::to_string(k) + " scale";
    char name[96];
    snprintf(name, sizeof(name), "png/p%05u_%s.png", pass, role.c_str());
    if (WritePngRgb8(dir_ + "/" + name, v.mtl_width, v.mtl_height, rgb)) return name;
    Failed(dir_ + "/" + name);
    *mode = "write failed";
    return "";
}

void Writer::SavePre(uint32_t pass, TextureRec* t, const std::vector<uint8_t>& raw, RawBudget* budget) {
    if (!budget->Fits(raw.size())) {
        t->readback = kReadbackBudget;
        return;
    }
    t->pre_file = SaveTexture(pass, t->role, "pre", raw, &t->pre_hash);
    if (!t->pre_file.empty()) budget->used += raw.size(); // a failed write (Failures) uses nothing
}

void Writer::SavePost(uint32_t pass, bool replayable, TextureRec* t, const std::vector<uint8_t>& raw,
                      RawBudget* budget, uint32_t thumb) {
    if (replayable) {
        if (budget->Fits(raw.size())) {
            t->post_file = SaveTexture(pass, t->role, "post", raw, &t->post_hash);
            if (!t->post_file.empty()) budget->used += raw.size();
        } else {
            t->readback = kReadbackBudget;
        }
    }
    t->png_file = SavePng(pass, t->role, t->id, raw, replayable ? 0 : thumb, &t->png_mode);
}

bool Writer::Finish(std::string* err) {
    const std::string text = WriteManifest(frame_);
    if (!WriteFile(dir_ + "/manifest.json", text.data(), text.size())) {
        if (err) *err = "cannot write " + dir_ + "/manifest.json";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------------------------
namespace {

struct Out {
    std::string s;
    bool first = true;
    void Key(const char* k) {
        if (!first) s += ',';
        first = false;
        s += '"';
        s += k;
        s += "\":";
    }
    void Str(const std::string& v) {
        s += '"';
        for (char c : v) {
            if (c == '"' || c == '\\') s += '\\';
            if ((unsigned char)c < 0x20) {
                char b[8];
                snprintf(b, sizeof(b), "\\u%04x", c);
                s += b;
                continue;
            }
            s += c;
        }
        s += '"';
    }
    void Open(char c) { s += c; first = true; }
    void Close(char c) { s += c; first = false; }
    void Elem() {
        if (!first) s += ',';
        first = false;
    }
    void U(const char* k, uint64_t v) { Key(k); s += std::to_string(v); }
    void I(const char* k, int64_t v) { Key(k); s += std::to_string(v); }
    void H(const char* k, uint64_t v) { Key(k); Str(Hex(v)); }
    void B(const char* k, bool v) { Key(k); s += v ? "true" : "false"; }
    void S(const char* k, const std::string& v) { Key(k); Str(v); }
    void D(const char* k, double v) {
        Key(k);
        char b[40];
        snprintf(b, sizeof(b), "%.17g", std::isfinite(v) ? v : 0.0);
        s += b;
    }
    template <class T> void Arr(const char* k, const T* v, size_t n) {
        Key(k);
        Open('[');
        for (size_t i = 0; i < n; ++i) {
            Elem();
            if constexpr (std::is_same_v<T, double>) {
                char b[40];
                snprintf(b, sizeof(b), "%.17g", v[i]);
                s += b;
            } else if constexpr (std::is_same_v<T, bool>) {
                s += v[i] ? "true" : "false";
            } else {
                s += std::to_string((uint64_t)v[i]);
            }
        }
        Close(']');
    }
    void Hexes(const char* k, const uint32_t* v, size_t n) {
        Key(k);
        Open('[');
        for (size_t i = 0; i < n; ++i) {
            Elem();
            Str(Hex(v[i]));
        }
        Close(']');
    }
};

void WriteBlob(Out& o, const BlobRef& b) {
    o.Elem();
    o.Open('{');
    o.H("addr", b.addr);
    o.U("size", b.size);
    o.H("hash", b.hash);
    o.S("file", b.file);
    o.Close('}');
}

void WriteTextureId(Out& o, const GpuTextureId& t) {
    o.Key("id");
    o.Open('{');
    o.I("kind", t.kind);
    o.H("address", t.address);
    o.U("size", t.size);
    o.U("mtl_format", t.mtl_format);
    o.U("entry_format", t.entry_format);
    o.U("tex_type", t.tex_type);
    o.U("width", t.width);
    o.U("height", t.height);
    o.U("depth", t.depth);
    o.U("levels", t.levels);
    o.U("layers", t.layers);
    o.U("samples", t.samples);
    o.U("dfmt", t.dfmt);
    o.U("nfmt", t.nfmt);
    o.Arr("swizzle", t.swizzle, 4);
    o.Arr("cb_swizzle", t.cb_swizzle, 4);
    o.U("mtl_type", t.mtl_type);
    o.U("mtl_width", t.mtl_width);
    o.U("mtl_height", t.mtl_height);
    o.U("mtl_depth", t.mtl_depth);
    o.U("mtl_levels", t.mtl_levels);
    o.U("mtl_array", t.mtl_array);
    o.U("mtl_samples", t.mtl_samples);
    o.U("usage", t.usage);
    o.U("storage_mode", t.storage_mode);
    o.Arr("surface", t.surface.data(), t.surface.size());
    o.Close('}');
}

void WritePass(Out& o, const PassRec& p) {
    o.Elem();
    o.Open('{');
    o.U("index", p.index);
    o.B("replayable", p.replayable);
    o.S("type", p.type);
    o.S("queue", p.queue);
    o.S("name", p.name);
    o.S("skipped", p.skipped);
    o.H("regs_hash", p.regs_hash);
    o.S("regs_file", p.regs_file);
    o.B("indexed", p.indexed);
    o.H("index_base", p.index_base);
    o.U("index_type", p.index_type);
    o.U("index_offset", p.index_offset);
    o.U("count", p.count);
    o.U("max_index", p.max_index);
    o.U("num_instances", p.num_instances);
    o.I("base_vertex_ud", p.base_vertex_ud);
    o.I("start_instance_ud", p.start_instance_ud);
    o.Arr("cb_extent", p.cb_extent, 8);
    o.U("db_extent", p.db_extent);
    o.Arr("dim", p.dim, 3);
    o.H("indirect_addr", p.indirect_addr);
    o.Arr("indirect_args", p.indirect_args, 5);
    o.Key("stages");
    o.Open('[');
    for (const StageRec& s : p.stages) {
        o.Elem();
        o.Open('{');
        o.S("stage", s.stage);
        o.H("code_addr", s.code_addr);
        o.U("code_size", s.code_size);
        o.H("pgm_hash", s.pgm_hash);
        o.H("msl_hash", s.msl_hash);
        o.Hexes("user_data", s.user_data, 16);
        o.Close('}');
    }
    o.Close(']');
    o.Key("buffers");
    o.Open('[');
    for (const BufferRec& b : p.buffers) {
        o.Elem();
        o.Open('{');
        o.S("stage", b.stage);
        o.U("index", b.index);
        o.H("addr", b.addr);
        o.U("size", b.size);
        o.U("stride", b.stride);
        o.B("written", b.written);
        o.Hexes("vsharp", b.vsharp, 4);
        o.Close('}');
    }
    o.Close(']');
    o.Key("images");
    o.Open('[');
    for (const ImageRec& i : p.images) {
        o.Elem();
        o.Open('{');
        o.S("stage", i.stage);
        o.U("index", i.index);
        o.H("addr", i.addr);
        o.Hexes("tsharp", i.tsharp, 8);
        o.U("dfmt", i.dfmt);
        o.U("nfmt", i.nfmt);
        o.U("width", i.width);
        o.U("height", i.height);
        o.U("depth", i.depth);
        o.U("levels", i.levels);
        o.U("layers", i.layers);
        o.U("type", i.type);
        o.U("tile_mode", i.tile_mode);
        o.B("storage", i.storage);
        o.B("depth_image", i.depth_image);
        if (i.table >= 0) {
            o.U("table", (uint64_t)i.table);
            o.U("entry", (uint64_t)i.entry);
        }
        o.Arr("textures", i.textures.data(), i.textures.size());
        o.U("guest_bytes", i.guest_bytes);
        o.Close('}');
    }
    o.Close(']');
    o.Key("samplers");
    o.Open('[');
    for (const SamplerRec& s : p.samplers) {
        o.Elem();
        o.Open('{');
        o.S("stage", s.stage);
        o.U("index", s.index);
        o.Hexes("ssharp", s.ssharp, 4);
        o.Close('}');
    }
    o.Close(']');
    o.Key("textures");
    o.Open('[');
    for (const TextureRec& t : p.textures) {
        o.Elem();
        o.Open('{');
        o.S("role", t.role);
        o.B("written", t.written);
        WriteTextureId(o, t.id);
        o.S("pre_file", t.pre_file);
        o.S("post_file", t.post_file);
        o.S("png_file", t.png_file);
        o.S("png_mode", t.png_mode);
        o.H("pre_hash", t.pre_hash);
        o.H("post_hash", t.post_hash);
        o.S("readback", t.readback);
        o.Close('}');
    }
    o.Close(']');
    o.Key("meta_cleared");
    o.Open('[');
    for (uint64_t a : p.meta_cleared) {
        o.Elem();
        o.Str(Hex(a));
    }
    o.Close(']');
    o.Key("meta_cleared_slices");
    o.Open('[');
    for (uint32_t sl : p.meta_cleared_slices) {
        o.Elem();
        o.Str(Hex(sl));
    }
    o.Close(']');
    o.Key("unstable");
    o.Open('[');
    for (const std::string& u : p.unstable) {
        o.Elem();
        o.Str(u);
    }
    o.Close(']');
    o.Key("inputs");
    o.Open('[');
    for (const BlobRef& b : p.inputs) WriteBlob(o, b);
    o.Close(']');
    o.Key("outputs");
    o.Open('[');
    for (const BlobRef& b : p.outputs) WriteBlob(o, b);
    o.Close(']');
    const StateRec& s = p.state;
    o.Key("state");
    o.Open('{');
    o.U("prim", s.prim);
    o.U("color_mode", s.color_mode);
    o.Arr("blend_enable", s.blend_enable, 8);
    o.Hexes("blend_raw", s.blend_raw, 8);
    o.Arr("write_mask", s.write_mask, 8);
    o.B("depth_test", s.depth_test);
    o.B("depth_write", s.depth_write);
    o.U("depth_compare", s.depth_compare);
    o.B("stencil_test", s.stencil_test);
    o.U("stencil_ref_front", s.stencil_ref_front);
    o.U("stencil_ref_back", s.stencil_ref_back);
    o.Arr("viewport", s.viewport, 6);
    o.Arr("scissor", s.scissor, 4);
    o.Close('}');
    o.Close('}');
}

} // namespace

std::string WriteManifest(const FrameRec& f) {
    Out o;
    o.Open('{');
    o.I("version", f.version);
    o.U("frame", f.frame);
    o.H("mem_size", f.mem_size);
    o.B("neo", f.neo);
    o.S("gcn_cache_id", f.gcn_cache_id);
    o.Key("passes");
    o.Open('[');
    for (const PassRec& p : f.passes) {
        o.s += '\n';
        WritePass(o, p);
    }
    o.Close(']');
    o.Close('}');
    o.s += '\n';
    return o.s;
}

// Minimal JSON reader: objects, arrays, strings (\" \\ \uXXXX < 0x80), numbers, true/false/null.
namespace {

struct J {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;
    const J* Get(const char* k) const {
        for (auto& kv : o)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct Parser {
    const char* p;
    const char* end;
    std::string err;
    void Ws() {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    }
    bool Fail(const char* what) {
        if (err.empty()) err = what;
        return false;
    }
    bool String(std::string* s) {
        if (p >= end || *p != '"') return Fail("expected string");
        ++p;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                if (++p >= end) return Fail("bad escape");
                if (*p == 'u') {
                    if (end - p < 5) return Fail("bad \\u");
                    *s += (char)strtol(std::string(p + 1, 4).c_str(), nullptr, 16);
                    p += 5;
                    continue;
                }
                *s += *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
                ++p;
                continue;
            }
            *s += *p++;
        }
        if (p >= end) return Fail("unterminated string");
        ++p;
        return true;
    }
    bool Value(J* v) {
        Ws();
        if (p >= end) return Fail("unexpected end");
        if (*p == '{') {
            v->t = J::Obj;
            ++p;
            Ws();
            if (p < end && *p == '}') return ++p, true;
            for (;;) {
                Ws();
                std::pair<std::string, J> kv;
                if (!String(&kv.first)) return false;
                Ws();
                if (p >= end || *p != ':') return Fail("expected ':'");
                ++p;
                if (!Value(&kv.second)) return false;
                v->o.push_back(std::move(kv));
                Ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') return ++p, true;
                return Fail("expected ',' or '}'");
            }
        }
        if (*p == '[') {
            v->t = J::Arr;
            ++p;
            Ws();
            if (p < end && *p == ']') return ++p, true;
            for (;;) {
                J e;
                if (!Value(&e)) return false;
                v->a.push_back(std::move(e));
                Ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') return ++p, true;
                return Fail("expected ',' or ']'");
            }
        }
        if (*p == '"') {
            v->t = J::Str;
            return String(&v->s);
        }
        if (end - p >= 4 && !strncmp(p, "true", 4)) { v->t = J::Bool; v->b = true; p += 4; return true; }
        if (end - p >= 5 && !strncmp(p, "false", 5)) { v->t = J::Bool; p += 5; return true; }
        if (end - p >= 4 && !strncmp(p, "null", 4)) { p += 4; return true; }
        const char* q = p;
        while (q < end && (strchr("+-.eE", *q) || (*q >= '0' && *q <= '9'))) ++q;
        if (q == p) return Fail("unexpected character");
        v->n = strtod(std::string(p, q - p).c_str(), nullptr);
        v->t = J::Num;
        p = q;
        return true;
    }
};

uint64_t U(const J& o, const char* k) {
    const J* v = o.Get(k);
    if (!v) return 0;
    if (v->t == J::Str) return strtoull(v->s.c_str(), nullptr, 0); // "0x..." hex
    if (v->t == J::Bool) return v->b;
    return (uint64_t)(int64_t)v->n;
}
int64_t I(const J& o, const char* k) {
    const J* v = o.Get(k);
    return v && v->t == J::Num ? (int64_t)v->n : 0;
}
bool B(const J& o, const char* k) {
    const J* v = o.Get(k);
    return v && v->t == J::Bool && v->b;
}
std::string S(const J& o, const char* k) {
    const J* v = o.Get(k);
    return v && v->t == J::Str ? v->s : std::string();
}
template <class T> void Arr(const J& o, const char* k, T* out, size_t n) {
    const J* v = o.Get(k);
    if (!v || v->t != J::Arr) return;
    for (size_t i = 0; i < n && i < v->a.size(); ++i) {
        const J& e = v->a[i];
        if constexpr (std::is_same_v<T, double>) out[i] = e.n;
        else if constexpr (std::is_same_v<T, bool>) out[i] = e.t == J::Bool && e.b;
        else out[i] = (T)(e.t == J::Str ? strtoull(e.s.c_str(), nullptr, 0) : (uint64_t)(int64_t)e.n);
    }
}
template <class T> void Vec(const J& o, const char* k, std::vector<T>* out) {
    const J* v = o.Get(k);
    if (!v || v->t != J::Arr) return;
    out->resize(v->a.size());
    Arr(o, k, out->data(), out->size());
}
BlobRef Blob(const J& j) {
    BlobRef b;
    b.addr = U(j, "addr");
    b.size = U(j, "size");
    b.hash = U(j, "hash");
    b.file = S(j, "file");
    return b;
}
GpuTextureId TexId(const J& j) {
    GpuTextureId t;
    t.kind = (int)I(j, "kind");
    t.address = U(j, "address");
    t.size = U(j, "size");
    t.mtl_format = (uint32_t)U(j, "mtl_format");
    t.entry_format = (uint32_t)U(j, "entry_format");
    t.tex_type = (uint32_t)U(j, "tex_type");
    t.width = (uint32_t)U(j, "width");
    t.height = (uint32_t)U(j, "height");
    t.depth = (uint32_t)U(j, "depth");
    t.levels = (uint32_t)U(j, "levels");
    t.layers = (uint32_t)U(j, "layers");
    t.samples = (uint32_t)U(j, "samples");
    t.dfmt = (uint32_t)U(j, "dfmt");
    t.nfmt = (uint32_t)U(j, "nfmt");
    Arr(j, "swizzle", t.swizzle, 4);
    Arr(j, "cb_swizzle", t.cb_swizzle, 4);
    t.mtl_type = (uint32_t)U(j, "mtl_type");
    t.mtl_width = (uint32_t)U(j, "mtl_width");
    t.mtl_height = (uint32_t)U(j, "mtl_height");
    t.mtl_depth = (uint32_t)U(j, "mtl_depth");
    t.mtl_levels = (uint32_t)U(j, "mtl_levels");
    t.mtl_array = (uint32_t)U(j, "mtl_array");
    t.mtl_samples = (uint32_t)U(j, "mtl_samples");
    t.usage = (uint32_t)U(j, "usage");
    t.storage_mode = (uint32_t)U(j, "storage_mode");
    Vec(j, "surface", &t.surface);
    return t;
}

PassRec Pass(const J& j) {
    PassRec p;
    p.index = (uint32_t)U(j, "index");
    p.replayable = B(j, "replayable");
    p.type = S(j, "type");
    p.queue = S(j, "queue");
    p.name = S(j, "name");
    p.skipped = S(j, "skipped");
    p.regs_hash = U(j, "regs_hash");
    p.regs_file = S(j, "regs_file");
    p.indexed = B(j, "indexed");
    p.index_base = U(j, "index_base");
    p.index_type = (uint32_t)U(j, "index_type");
    p.index_offset = (uint32_t)U(j, "index_offset");
    p.count = (uint32_t)U(j, "count");
    p.max_index = (uint32_t)U(j, "max_index");
    p.num_instances = (uint32_t)U(j, "num_instances");
    p.base_vertex_ud = (int)I(j, "base_vertex_ud");
    p.start_instance_ud = (int)I(j, "start_instance_ud");
    Arr(j, "cb_extent", p.cb_extent, 8);
    p.db_extent = (uint32_t)U(j, "db_extent");
    Arr(j, "dim", p.dim, 3);
    p.indirect_addr = U(j, "indirect_addr");
    Arr(j, "indirect_args", p.indirect_args, 5);
    if (const J* a = j.Get("stages"))
        for (const J& e : a->a) {
            StageRec s;
            s.stage = S(e, "stage");
            s.code_addr = U(e, "code_addr");
            s.code_size = (uint32_t)U(e, "code_size");
            s.pgm_hash = U(e, "pgm_hash");
            s.msl_hash = U(e, "msl_hash");
            Arr(e, "user_data", s.user_data, 16);
            p.stages.push_back(s);
        }
    if (const J* a = j.Get("buffers"))
        for (const J& e : a->a) {
            BufferRec b;
            b.stage = S(e, "stage");
            b.index = (uint32_t)U(e, "index");
            b.addr = U(e, "addr");
            b.size = (uint32_t)U(e, "size");
            b.stride = (uint32_t)U(e, "stride");
            b.written = B(e, "written");
            Arr(e, "vsharp", b.vsharp, 4);
            p.buffers.push_back(b);
        }
    if (const J* a = j.Get("images"))
        for (const J& e : a->a) {
            ImageRec i;
            i.stage = S(e, "stage");
            i.index = (uint32_t)U(e, "index");
            i.addr = U(e, "addr");
            Arr(e, "tsharp", i.tsharp, 8);
            i.dfmt = (uint32_t)U(e, "dfmt");
            i.nfmt = (uint32_t)U(e, "nfmt");
            i.width = (uint32_t)U(e, "width");
            i.height = (uint32_t)U(e, "height");
            i.depth = (uint32_t)U(e, "depth");
            i.levels = (uint32_t)U(e, "levels");
            i.layers = (uint32_t)U(e, "layers");
            i.type = (uint32_t)U(e, "type");
            i.tile_mode = (uint32_t)U(e, "tile_mode");
            i.storage = B(e, "storage");
            i.depth_image = B(e, "depth_image");
            if (e.Get("table")) {
                i.table = (int32_t)U(e, "table");
                i.entry = (int32_t)U(e, "entry");
            }
            Vec(e, "textures", &i.textures);
            i.guest_bytes = U(e, "guest_bytes");
            p.images.push_back(i);
        }
    if (const J* a = j.Get("samplers"))
        for (const J& e : a->a) {
            SamplerRec s;
            s.stage = S(e, "stage");
            s.index = (uint32_t)U(e, "index");
            Arr(e, "ssharp", s.ssharp, 4);
            p.samplers.push_back(s);
        }
    if (const J* a = j.Get("textures"))
        for (const J& e : a->a) {
            TextureRec t;
            t.role = S(e, "role");
            t.written = B(e, "written");
            if (const J* id = e.Get("id")) t.id = TexId(*id);
            t.pre_file = S(e, "pre_file");
            t.post_file = S(e, "post_file");
            t.png_file = S(e, "png_file");
            t.png_mode = S(e, "png_mode");
            t.pre_hash = U(e, "pre_hash");
            t.post_hash = U(e, "post_hash");
            t.readback = S(e, "readback");
            p.textures.push_back(t);
        }
    Vec(j, "meta_cleared", &p.meta_cleared);
    Vec(j, "meta_cleared_slices", &p.meta_cleared_slices);
    if (const J* a = j.Get("unstable"))
        for (const J& e : a->a)
            if (e.t == J::Str) p.unstable.push_back(e.s);
    if (const J* a = j.Get("inputs"))
        for (const J& e : a->a) p.inputs.push_back(Blob(e));
    if (const J* a = j.Get("outputs"))
        for (const J& e : a->a) p.outputs.push_back(Blob(e));
    if (const J* s = j.Get("state")) {
        StateRec& r = p.state;
        r.prim = (uint32_t)U(*s, "prim");
        r.color_mode = (uint32_t)U(*s, "color_mode");
        Arr(*s, "blend_enable", r.blend_enable, 8);
        Arr(*s, "blend_raw", r.blend_raw, 8);
        Arr(*s, "write_mask", r.write_mask, 8);
        r.depth_test = B(*s, "depth_test");
        r.depth_write = B(*s, "depth_write");
        r.depth_compare = (uint32_t)U(*s, "depth_compare");
        r.stencil_test = B(*s, "stencil_test");
        r.stencil_ref_front = (uint32_t)U(*s, "stencil_ref_front");
        r.stencil_ref_back = (uint32_t)U(*s, "stencil_ref_back");
        Arr(*s, "viewport", r.viewport, 6);
        Arr(*s, "scissor", r.scissor, 4);
    }
    return p;
}

} // namespace

bool ParseManifest(const std::string& text, FrameRec* out, std::string* err) {
    Parser ps{text.data(), text.data() + text.size(), {}};
    J root;
    if (!ps.Value(&root) || root.t != J::Obj) {
        if (err) *err = "manifest: " + (ps.err.empty() ? std::string("not an object") : ps.err);
        return false;
    }
    out->version = (int)I(root, "version");
    if (out->version != kManifestVersion) {
        if (err) *err = "manifest version " + std::to_string(out->version) + ", expected " + std::to_string(kManifestVersion);
        return false;
    }
    out->frame = U(root, "frame");
    out->mem_size = U(root, "mem_size");
    out->neo = B(root, "neo");
    out->gcn_cache_id = S(root, "gcn_cache_id");
    out->passes.clear();
    if (const J* a = root.Get("passes"))
        for (const J& e : a->a) out->passes.push_back(Pass(e));
    return true;
}

bool LoadFrame(const std::string& frame_dir, FrameRec* out, std::string* err) {
    std::vector<uint8_t> b;
    if (!ReadFile(frame_dir + "/manifest.json", &b)) {
        if (err) *err = "cannot read " + frame_dir + "/manifest.json";
        return false;
    }
    return ParseManifest(std::string(b.begin(), b.end()), out, err);
}

bool LoadRegs(const std::string& frame_dir, const std::string& file, std::vector<uint32_t>* regs, std::string* err) {
    std::vector<uint8_t> b;
    if (file.empty() || !ReadFile(frame_dir + "/" + file, &b) || b.size() % 8) {
        if (err) *err = "cannot read register file '" + file + "'";
        return false;
    }
    regs->assign(kNumRegs, 0);
    const uint32_t* w = reinterpret_cast<const uint32_t*>(b.data());
    for (size_t i = 0; i + 1 < b.size() / 4; i += 2) {
        if (w[i] >= kNumRegs) {
            if (err) *err = "register index out of range in " + file;
            return false;
        }
        (*regs)[w[i]] = w[i + 1];
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------------------------
namespace {

float Half(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp((float)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}
// 11/10-bit unsigned floats (5-bit exponent, 6/5-bit mantissa)
float Small(uint32_t v, int mbits) {
    const uint32_t e = v >> mbits, m = v & ((1u << mbits) - 1);
    if (e == 0) return std::ldexp((float)m, -14 - mbits);
    if (e == 31) return m ? NAN : INFINITY;
    return std::ldexp((float)(m | (1u << mbits)), (int)e - 15 - mbits);
}
uint8_t Tone(float x) {
    if (!(x > 0)) return 0; // also -Inf
    if (std::isinf(x)) return 255;
    return (uint8_t)std::lround(255.0f * x / (1.0f + x));
}
float F32(const uint8_t* p) {
    float f;
    memcpy(&f, p, 4);
    return f;
}

} // namespace

bool ToViewRgb8(const GpuTextureId& t, const uint8_t* raw, size_t raw_size, std::vector<uint8_t>* rgb,
                std::string* mode) {
    const uint32_t f = t.mtl_format, bpe = BytesPerTexel(f);
    const size_t w = t.mtl_width, h = t.mtl_height, n = w * h;
    if (!bpe || !n || raw_size < n * bpe || t.mtl_samples > 1) {
        *mode = t.mtl_samples > 1 ? "unsupported: multisampled" : "unsupported format " + std::to_string(f);
        return false;
    }
    rgb->assign(n * 3, 0);
    uint8_t* o = rgb->data();
    auto px = [&](size_t i, float r, float g, float b, bool tone) {
        const float c[3] = {r, g, b};
        for (int k = 0; k < 3; ++k) {
            if (std::isnan(c[k])) { o[i * 3] = 255; o[i * 3 + 1] = 0; o[i * 3 + 2] = 255; return; }
        }
        for (int k = 0; k < 3; ++k)
            o[i * 3 + k] = tone ? Tone(c[k]) : (uint8_t)std::lround(255.0f * std::min(1.0f, std::max(0.0f, c[k])));
    };
    switch (f) {
    case kRGBA8Unorm: case kRGBA8Unorm_sRGB: case kBGRA8Unorm: case kBGRA8Unorm_sRGB: {
        const bool bgr = f == kBGRA8Unorm || f == kBGRA8Unorm_sRGB;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t* p = raw + i * 4;
            o[i * 3] = bgr ? p[2] : p[0];
            o[i * 3 + 1] = p[1];
            o[i * 3 + 2] = bgr ? p[0] : p[2];
        }
        *mode = f == kRGBA8Unorm_sRGB || f == kBGRA8Unorm_sRGB ? "srgb bytes" : "unorm";
        return true;
    }
    case kR8Unorm: case kRG8Unorm:
        for (size_t i = 0; i < n; ++i) {
            o[i * 3] = raw[i * bpe];
            if (bpe == 2) o[i * 3 + 1] = raw[i * bpe + 1];
        }
        *mode = "unorm";
        return true;
    case kR8Snorm: case kRG8Snorm: case kRGBA8Snorm:
        for (size_t i = 0; i < n; ++i) {
            const uint32_t ch = bpe == 4 ? 3 : bpe;
            float c[3] = {};
            for (uint32_t k = 0; k < ch; ++k) c[k] = (std::max(-1.0f, (int8_t)raw[i * bpe + k] / 127.0f) + 1) / 2;
            px(i, c[0], c[1], c[2], false);
        }
        *mode = "snorm (v+1)/2";
        return true;
    case kR16Unorm: case kRG16Unorm: case kRGBA16Unorm: case kR16Snorm: case kRG16Snorm: case kRGBA16Snorm: {
        const bool sn = f == kR16Snorm || f == kRG16Snorm || f == kRGBA16Snorm;
        const uint32_t ch = std::min(3u, bpe / 2);
        for (size_t i = 0; i < n; ++i) {
            float c[3] = {};
            for (uint32_t k = 0; k < ch; ++k) {
                uint16_t v;
                memcpy(&v, raw + i * bpe + k * 2, 2);
                c[k] = sn ? (std::max(-1.0f, (int16_t)v / 32767.0f) + 1) / 2 : v / 65535.0f;
            }
            px(i, c[0], c[1], c[2], false);
        }
        *mode = sn ? "snorm (v+1)/2" : "unorm";
        return true;
    }
    case kRGB10A2Unorm:
        for (size_t i = 0; i < n; ++i) {
            uint32_t v;
            memcpy(&v, raw + i * 4, 4);
            px(i, (v & 1023) / 1023.0f, ((v >> 10) & 1023) / 1023.0f, ((v >> 20) & 1023) / 1023.0f, false);
        }
        *mode = "unorm";
        return true;
    case kR16Float: case kRG16Float: case kRGBA16Float: {
        const uint32_t ch = std::min(3u, bpe / 2);
        for (size_t i = 0; i < n; ++i) {
            float c[3] = {};
            for (uint32_t k = 0; k < ch; ++k) {
                uint16_t v;
                memcpy(&v, raw + i * bpe + k * 2, 2);
                c[k] = Half(v);
            }
            px(i, c[0], c[1], c[2], true);
        }
        *mode = "float x/(1+x)";
        return true;
    }
    case kR32Float: case kRG32Float: case kRGBA32Float: {
        const uint32_t ch = std::min(3u, bpe / 4);
        for (size_t i = 0; i < n; ++i) {
            float c[3] = {};
            for (uint32_t k = 0; k < ch; ++k) c[k] = F32(raw + i * bpe + k * 4);
            px(i, c[0], c[1], c[2], true);
        }
        *mode = "float x/(1+x)";
        return true;
    }
    case kRG11B10Float:
        for (size_t i = 0; i < n; ++i) {
            uint32_t v;
            memcpy(&v, raw + i * 4, 4);
            px(i, Small(v & 2047, 6), Small((v >> 11) & 2047, 6), Small(v >> 22, 5), true);
        }
        *mode = "float x/(1+x)";
        return true;
    case kRGB9E5Float:
        for (size_t i = 0; i < n; ++i) {
            uint32_t v;
            memcpy(&v, raw + i * 4, 4);
            const int e = (int)(v >> 27) - 15 - 9;
            px(i, std::ldexp((float)(v & 511), e), std::ldexp((float)((v >> 9) & 511), e),
               std::ldexp((float)((v >> 18) & 511), e), true);
        }
        *mode = "float x/(1+x)";
        return true;
    case kR8Uint: case kR8Sint: case kRG8Uint: case kR16Uint: case kR16Sint: case kR32Uint: case kR32Sint:
    case kRG16Uint: case kRGBA8Uint: case kRG32Uint: case kRGBA16Uint: case kRGBA32Uint: {
        uint32_t cb; // bytes per channel
        switch (f) {
        case kR8Uint: case kR8Sint: case kRG8Uint: case kRGBA8Uint: cb = 1; break;
        case kR16Uint: case kR16Sint: case kRG16Uint: case kRGBA16Uint: cb = 2; break;
        default: cb = 4; break;
        }
        const bool sgn = f == kR8Sint || f == kR16Sint || f == kR32Sint;
        const uint32_t ch = std::min(3u, bpe / cb);
        auto val = [&](size_t i, uint32_t k) -> double {
            const uint8_t* p = raw + i * bpe + k * cb;
            if (cb == 1) return sgn ? (double)(int8_t)p[0] : p[0];
            if (cb == 2) { uint16_t v; memcpy(&v, p, 2); return sgn ? (double)(int16_t)v : v; }
            uint32_t v;
            memcpy(&v, p, 4);
            return sgn ? (double)(int32_t)v : v;
        };
        for (uint32_t k = 0; k < ch; ++k) {
            double lo = 1e300, hi = -1e300;
            for (size_t i = 0; i < n; ++i) lo = std::min(lo, val(i, k)), hi = std::max(hi, val(i, k));
            const double sc = hi > lo ? 255.0 / (hi - lo) : 0.0;
            for (size_t i = 0; i < n; ++i) o[i * 3 + k] = (uint8_t)std::lround((val(i, k) - lo) * sc);
        }
        *mode = "integer min..max";
        return true;
    }
    case kDepth16Unorm: case kDepth32Float: case kDepth32Float_Stencil8: {
        std::vector<float> v(n);
        for (size_t i = 0; i < n; ++i) {
            if (f == kDepth16Unorm) { uint16_t d; memcpy(&d, raw + i * 2, 2); v[i] = d / 65535.0f; }
            else v[i] = F32(raw + i * 4);
        }
        float lo = INFINITY, hi = -INFINITY;
        for (float d : v) if (std::isfinite(d)) lo = std::min(lo, d), hi = std::max(hi, d);
        const float sc = hi > lo ? 255.0f / (hi - lo) : 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t g = std::isfinite(v[i]) ? (uint8_t)std::lround((v[i] - lo) * sc) : 255;
            o[i * 3] = o[i * 3 + 1] = o[i * 3 + 2] = g;
        }
        *mode = "depth min..max";
        return true;
    }
    case kStencil8:
        for (size_t i = 0; i < n; ++i) o[i * 3] = o[i * 3 + 1] = o[i * 3 + 2] = raw[i];
        *mode = "stencil";
        return true;
    default:
        *mode = "unsupported format " + std::to_string(f);
        return false;
    }
}

bool WritePngRgb8(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgb) {
    if (!w || !h || rgb.size() < (size_t)w * h * 3) return false;
    std::vector<uint8_t> scan;
    scan.reserve(((size_t)w * 3 + 1) * h);
    for (uint32_t y = 0; y < h; ++y) {
        scan.push_back(0); // filter: none
        scan.insert(scan.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3);
    }
    uLongf zn = compressBound(scan.size());
    std::vector<uint8_t> z(zn);
    if (compress2(z.data(), &zn, scan.data(), scan.size(), 6) != Z_OK) return false;
    z.resize(zn);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    auto be32 = [&](uint32_t v) {
        for (int s = 24; s >= 0; s -= 8) png.push_back((uint8_t)(v >> s));
    };
    auto chunk = [&](const char* type, const uint8_t* d, size_t n) {
        be32((uint32_t)n);
        const size_t at = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), d, d + n);
        be32((uint32_t)crc32(0, png.data() + at, (uInt)(n + 4)));
    };
    uint8_t ihdr[13] = {};
    for (int i = 0; i < 4; ++i) {
        ihdr[i] = (uint8_t)(w >> (24 - 8 * i));
        ihdr[4 + i] = (uint8_t)(h >> (24 - 8 * i));
    }
    ihdr[8] = 8; // bit depth
    ihdr[9] = 2; // RGB
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), z.size());
    chunk("IEND", nullptr, 0);
    return WriteFile(path, png.data(), png.size());
}

} // namespace ps4gnm_capture
