// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - pass-level frame capture: records, on-disk layout and loader (plain C++17, no Metal).
 *
 * PS4_GNM_CAPTURE_FRAME=<present index> makes ps4_gnm_draw.mm record every draw and dispatch
 * encoded while S.present_idx equals that index (the frame PS4_GNM_TRACE=p<n>:1 traces) into
 * PS4_GNM_CAPTURE_DIR (default ./gnm_captures)/frame_<n>/:
 *   manifest.json             FrameRec (schema: docs/gnm-capture.md)
 *   blobs/<addr>_<size>_<hash>.bin   guest memory read by a pass, as it was before the pass;
 *                             16 KiB pages, stored once per (address, size, content hash)
 *   regs/<hash>.bin           register file of a pass: (index, value) uint32 pairs of the
 *                             non-zero dwords of the 0x10000-dword file, stored once per hash
 *   tex/p<pass>_<role>_<pre|post>.raw   GPU-owned texture contents (see RawLayout)
 *   png/p<pass>_<role>.png    view of a texture after the pass (ToViewRgb8 documents the mapping)
 * tools/gnm_capture_view.py renders the manifest as HTML; tools/gnm_replay replays one pass.
 */
#ifndef PS4_GNM_CAPTURE_H
#define PS4_GNM_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace ps4gnm_capture {

constexpr int kManifestVersion = 1;
constexpr uint64_t kPage = 16384;             // snapshot granularity (host page size)
constexpr uint64_t kMaxSnapshot = 256ull << 20; // larger ranges are recorded but not stored
constexpr uint32_t kNumRegs = 0x10000;

// MTLPixelFormat values (static_assert'ed against Metal in ps4_gnm_draw.mm).
enum MtlFormat : uint32_t {
    kR8Unorm = 10, kR8Snorm = 12, kR8Uint = 13, kR8Sint = 14,
    kR16Unorm = 20, kR16Snorm = 22, kR16Uint = 23, kR16Sint = 24, kR16Float = 25,
    kRG8Unorm = 30, kRG8Snorm = 32, kRG8Uint = 33,
    kR32Uint = 53, kR32Sint = 54, kR32Float = 55,
    kRG16Unorm = 60, kRG16Snorm = 62, kRG16Uint = 63, kRG16Float = 65,
    kRGBA8Unorm = 70, kRGBA8Unorm_sRGB = 71, kRGBA8Snorm = 72, kRGBA8Uint = 73,
    kBGRA8Unorm = 80, kBGRA8Unorm_sRGB = 81,
    kRGB10A2Unorm = 90, kRG11B10Float = 92, kRGB9E5Float = 93,
    kRG32Uint = 103, kRG32Float = 105,
    kRGBA16Unorm = 110, kRGBA16Snorm = 112, kRGBA16Uint = 113, kRGBA16Float = 115,
    kRGBA32Uint = 123, kRGBA32Float = 125,
    kDepth16Unorm = 250, kDepth32Float = 252, kStencil8 = 253, kDepth32Float_Stencil8 = 260,
    kX32_Stencil8 = 261,
};

// Bytes per texel of the raw layout (Depth32Float_Stencil8: 4, its depth plane; the stencil
// plane follows, see RawLayout). 0: not capturable.
uint32_t BytesPerTexel(uint32_t mtl_format);

// Identity of a GPU-owned texture-cache entry (TextureCache::Entry of kind Color or Depth): what
// lookups compare, plus the Metal descriptor, so replay can create an equivalent entry.
struct GpuTextureId {
    int kind = 0; // 1 color / storage, 2 depth
    uint64_t address = 0, size = 0;
    // mtl_format: the texture's MTLPixelFormat (raw layout, views); entry_format: Entry::mtl_format
    uint32_t mtl_format = 0, entry_format = 0, tex_type = 0, width = 0, height = 0, depth = 1, levels = 1, layers = 1,
             samples = 1;
    uint32_t dfmt = 0, nfmt = 0;
    uint8_t swizzle[4] = {}, cb_swizzle[4] = {};
    // Metal descriptor of Entry::tex
    uint32_t mtl_type = 0, mtl_width = 0, mtl_height = 0, mtl_depth = 1, mtl_levels = 1,
             mtl_array = 1, mtl_samples = 1, usage = 0, storage_mode = 0;
    std::vector<uint32_t> surface; // SurfaceDesc + SurfaceLayout (PackSurface, ps4_gnm_resources.h)
};

// Byte layout of a raw texture file: for each mip level l (0..mtl_levels-1), for each array
// slice s (0..slices-1, slices = mtl_array * (cube ? 6 : 1)), for each depth slice z of a 3D
// texture: rows of max(1, mtl_width >> l) texels, BytesPerTexel bytes each, max(1, mtl_height
// >> l) rows. Depth32Float_Stencil8 appends the stencil plane (1 byte per texel) in the same order.
uint64_t RawSize(const GpuTextureId& t);

struct BlobRef {
    uint64_t addr = 0, size = 0, hash = 0;
    std::string file; // relative to the frame directory; "" when not stored (too large, or the
                      // write failed: Writer::Failures)
};

struct StageRec {
    std::string stage; // "vs", "ps", "cs"
    uint64_t code_addr = 0, pgm_hash = 0, msl_hash = 0;
    uint32_t code_size = 0;
    uint32_t user_data[16] = {};
};

struct BufferRec {
    std::string stage;
    uint32_t index = 0;   // gcn buffer binding index
    uint64_t addr = 0;    // V# base
    uint32_t size = 0, stride = 0;
    bool written = false;
    uint32_t vsharp[4] = {};
};

struct ImageRec {
    std::string stage;
    uint32_t index = 0;   // gcn image binding index
    uint64_t addr = 0;
    uint32_t tsharp[8] = {};
    uint32_t dfmt = 0, nfmt = 0, width = 0, height = 0, depth = 1, levels = 1, layers = 1, type = 0, tile_mode = 0;
    bool storage = false, depth_image = false;
    // entry `entry` of T# table `table` of the stage's shader (gcn_image_binding.table), -1 for
    // other images
    int32_t table = -1, entry = -1;
    std::vector<int> textures; // indices into PassRec::textures (GPU-owned entries at addr)
    uint64_t guest_bytes = 0;  // bytes snapshotted from guest memory (0 when GPU-owned)
};

struct SamplerRec {
    std::string stage;
    uint32_t index = 0;
    uint32_t ssharp[4] = {};
};

struct TextureRec {
    std::string role;   // "color<i>", "depth", "storage", "sampled"
    bool written = false; // the pass writes it (render target, depth write, storage image)
    GpuTextureId id;
    std::string pre_file, post_file, png_file, png_mode;
    uint64_t pre_hash = 0, post_hash = 0;
    std::string readback; // "" ok; otherwise why the contents were not read ("msaa", "format")
};

struct StateRec {
    uint32_t prim = 0, color_mode = 1;
    bool blend_enable[8] = {};
    uint32_t blend_raw[8] = {}, write_mask[8] = {};
    bool depth_test = false, depth_write = false, stencil_test = false;
    uint32_t depth_compare = 7, stencil_ref_front = 0, stencil_ref_back = 0;
    double viewport[6] = {}; // origin x, origin y, width, height, znear, zfar (viewport 0)
    uint32_t scissor[4] = {};
};

struct PassRec {
    uint32_t index = 0;
    bool replayable = false; // inputs and raw texture contents stored (PS4_GNM_CAPTURE_REPLAY)
    std::string type;  // "draw" or "dispatch"
    std::string queue; // dispatch queue name ("gfx", compute ring names); "gfx" for draws
    std::string name;  // DrawArgs::name; queue for dispatches
    std::string skipped; // non-empty: the renderer encoded nothing (reason)
    uint64_t regs_hash = 0;
    std::string regs_file;
    // draw arguments (DrawArgs)
    bool indexed = false;
    uint64_t index_base = 0;
    uint32_t index_type = 0, index_offset = 0, count = 0, max_index = 0, num_instances = 1;
    int base_vertex_ud = -1, start_instance_ud = -1;
    uint32_t cb_extent[8] = {}, db_extent = 0;
    // dispatch arguments
    uint32_t dim[3] = {};
    // both: indirect argument record (guest address) and its dwords when the pass was encoded
    uint64_t indirect_addr = 0;
    uint32_t indirect_args[5] = {};
    std::vector<StageRec> stages;
    std::vector<BufferRec> buffers;
    std::vector<ImageRec> images;
    std::vector<SamplerRec> samplers;
    std::vector<TextureRec> textures;
    std::vector<uint64_t> meta_cleared; // CMASK addresses whose fast clear was pending
    // the slice that took each (parallel to meta_cleared; empty in captures made before it existed)
    std::vector<uint32_t> meta_cleared_slices;
    std::vector<BlobRef> inputs;  // guest memory before the pass (indices into nothing: full refs)
    std::vector<BlobRef> outputs; // written guest ranges after the pass
    // inputs that were not the same after the pass ran ("input 0x..+0x..", "texture <role>"): a guest
    // thread or other GPU work changed them between the snapshot and the end of the pass, so the pass
    // may have run on other data than the snapshot holds (replay differences are not renderer bugs then)
    std::vector<std::string> unstable;
    StateRec state;
};

struct FrameRec {
    int version = kManifestVersion;
    uint64_t frame = 0;
    uint64_t mem_size = 0;
    bool neo = false;
    std::string gcn_cache_id;
    std::vector<PassRec> passes;
};

// TextureRec::readback of contents not stored because of PS4_GNM_CAPTURE_RAW_MB.
constexpr const char* kReadbackBudget = "budget";

// Budget of the raw texture files (tex/*.raw) of a capture, PS4_GNM_CAPTURE_RAW_MB. Guest memory
// blobs and PNGs are not counted.
struct RawBudget {
    uint64_t limit = 8192ull << 20, used = 0;
    bool Fits(uint64_t n) const { return n <= limit && used <= limit - n; }
};

// Whether a draw writes its depth target: it writes depth, tests stencil (whose ops may write),
// or its load action clears depth (register or HTILE fast clear) or stencil. The renderer bumps
// the depth version under the same condition.
constexpr bool DepthTargetWritten(bool depth_write, bool stencil_test, bool clear_depth, bool clear_stencil) {
    return depth_write || stencil_test || clear_depth || clear_stencil;
}

uint64_t Fnv64(const void* data, size_t n, uint64_t h = 0xcbf29ce484222325ull);
// The stored `inputs` of a pass whose guest memory is not what the snapshot holds any more, as
// "input 0x<addr>+0x<size>". Ranges overlapping `outputs` (the pass writes them) and ranges that were
// recorded but not stored (no hash) are skipped.
std::vector<std::string> ChangedInputs(const uint8_t* mem_base, const std::vector<BlobRef>& inputs,
                                       const std::vector<BlobRef>& outputs);
// Page-aligned cover of the ranges, sorted and merged (adjacent pages join). Ranges at address 0
// (null pointers) are dropped; the cover is clamped to mem_size.
std::vector<std::pair<uint64_t, uint64_t>> CoalescePages(std::vector<std::pair<uint64_t, uint64_t>> ranges,
                                                         uint64_t mem_size);
// The parts of the (addr, size) ranges of mem_base the host can read: pages that are unmapped or
// not readable (PROT_NONE guards and reservations inside guest memory) are cut out, so a snapshot
// never faults. Sorted input stays sorted.
std::vector<std::pair<uint64_t, uint64_t>> ClampReadable(const uint8_t* mem_base,
                                                         const std::vector<std::pair<uint64_t, uint64_t>>& ranges);

// Identity of a pass across frames: the code addresses of its first two shader stages in the
// order the renderer notes them (vs, ps for a draw; cs, 0 for a dispatch) and how many earlier
// passes of its frame had the same ones. Pass indices drift between frames and runs; this key
// stays the same while the game renders the same scene the same way. pgm: the stages' program
// hashes (OrbShdr footer), which, unlike code addresses of shaders the game loads into heaps,
// are the same in every run; not part of the identity (within a run they follow the addresses).
struct SelectKey {
    uint64_t code[2] = {};
    uint64_t pgm[2] = {};
    uint32_t occurrence = 0;
    bool operator<(const SelectKey& o) const {
        return std::tie(code[0], code[1], occurrence) < std::tie(o.code[0], o.code[1], o.occurrence);
    }
    bool operator==(const SelectKey& o) const {
        return code[0] == o.code[0] && code[1] == o.code[1] && occurrence == o.occurrence;
    }
};

// Replayable passes chosen by shader address (PS4_GNM_CAPTURE_REPLAY_SHADER): every pass with a
// stage at one of the addresses, and the passes of the same frame that may have written the bytes
// those passes read (their producers; not transitive: a producer's own producers are not
// selected). Producers are only known once their readers ran, so the frame before the captured
// one is a learning frame: Learn records what each of its passes read and wrote, FinishLearning
// finds the producers, and in the captured frame Selects matches passes by SelectKey. A producer
// in an earlier frame is not selected (its output is the reader's "pre" contents). Not
// thread-safe; the renderer calls it from the encoder thread only.
class ShaderSelection {
public:
    using Range = std::pair<uint64_t, uint64_t>; // (addr, size)
    // Comma-separated entries: a code address, hex (0x...) or decimal, or pgm:<hash>, a program
    // hash (hex or decimal). Returns false (and the reason) for an empty list or an entry that is
    // not a non-zero number.
    bool Parse(const std::string& list, std::string* err);
    bool Empty() const { return shaders_.empty() && pgms_.empty(); }
    const std::vector<uint64_t>& Shaders() const { return shaders_; }
    const std::vector<uint64_t>& Programs() const { return pgms_; }
    // Start of a frame: occurrences count from 0 again.
    void BeginFrame() { counts_.clear(); }
    // The key of the next pass of the frame (call it once per pass, in pass order).
    SelectKey Next(uint64_t code0, uint64_t code1, uint64_t pgm0 = 0, uint64_t pgm1 = 0);
    // The pass has a stage at a selected address or with a selected program hash.
    bool UsesShader(const SelectKey& k) const;
    // Learning frame, in pass order: the guest ranges the pass read, the ranges it wrote exactly
    // (every byte of them) and the ranges it may have written (a render target or a bound V# range,
    // of which a pass writes the pixels it covers or the bytes it stores).
    void Learn(const SelectKey& k, const std::vector<Range>& reads, const std::vector<Range>& exact_writes,
               const std::vector<Range>& may_writes = {});
    // End of the learning frame. Walking back from each pass using a selected shader, a pass is a
    // producer when it may have written, or wrote, a byte the reader read and no pass between them
    // wrote exactly; exact writes hide earlier writers, writes that may be partial do not (they
    // over-select rather than miss a producer). The learned passes go; their keys stay (Known).
    void FinishLearning();
    bool Selects(const SelectKey& k) const { return UsesShader(k) || producers_.count(k) != 0; }
    // The learning frame had a pass with this key (a selected pass of the captured frame that it
    // had not means the two frames differ: occurrences shifted, producers may be wrong).
    bool Known(const SelectKey& k) const { return known_.count(k) != 0; }
    size_t NumProducers() const { return producers_.size(); }
    size_t NumReaders() const { return readers_; }

private:
    struct Learned {
        SelectKey key;
        bool reader = false;
        std::vector<Range> reads, exact, may; // [lo, hi), sorted and merged; reads only for readers
    };
    std::vector<uint64_t> shaders_, pgms_; // sorted
    std::map<std::pair<uint64_t, uint64_t>, uint32_t> counts_;
    std::vector<Learned> learned_;
    std::set<SelectKey> producers_, known_;
    size_t readers_ = 0;
};

// Writes one frame. Not thread-safe; the renderer calls it from the encoder thread only.
class Writer {
public:
    Writer(std::string frame_dir, uint64_t frame, uint64_t mem_size, bool neo, std::string gcn_cache_id);
    bool Begin(std::string* err); // creates the directories
    const std::string& Dir() const { return dir_; }
    uint64_t Frame() const { return frame_.frame; }
    // Guest bytes [addr, addr+size) of mem_base: stored once per (addr, size, hash).
    BlobRef Snapshot(const uint8_t* mem_base, uint64_t addr, uint64_t size);
    // The register file (kNumRegs dwords): returns the file name, sets *hash.
    std::string SaveRegs(const uint32_t* regs, uint64_t* hash);
    // Raw texture contents for pass/role ("pre" or "post"); returns the file name.
    std::string SaveTexture(uint32_t pass, const std::string& role, const char* when,
                            const std::vector<uint8_t>& raw, uint64_t* hash);
    // View PNG of a texture's level 0, slice 0 (ToViewRgb8), every k-th texel so that it is at
    // most max_width wide (0: full size); returns the file name or "" (mode says why).
    std::string SavePng(uint32_t pass, const std::string& role, const GpuTextureId& id,
                        const std::vector<uint8_t>& raw, uint32_t max_width, std::string* mode);
    // A texture's contents before a replayable pass: tex/..._pre.raw when they fit the budget,
    // else t->readback = kReadbackBudget.
    void SavePre(uint32_t pass, TextureRec* t, const std::vector<uint8_t>& raw, RawBudget* budget);
    // A written texture after the pass: its view PNG always (full size for replayable passes, at
    // most thumb px wide otherwise), and for replayable passes tex/..._post.raw when it fits the
    // budget, else t->readback = kReadbackBudget.
    void SavePost(uint32_t pass, bool replayable, TextureRec* t, const std::vector<uint8_t>& raw, RawBudget* budget,
                  uint32_t thumb);
    void AddPass(PassRec&& p) { frame_.passes.push_back(std::move(p)); }
    size_t NumPasses() const { return frame_.passes.size(); }
    bool Finish(std::string* err); // writes manifest.json
    // Files that could not be written (blobs, register files, raw textures, PNGs). The first
    // failure is reported on stderr; a failed file is not remembered, so a later call retries it.
    uint64_t Failures() const { return failures_; }

private:
    void Failed(const std::string& path);
    uint64_t failures_ = 0;
    std::string dir_;
    FrameRec frame_;
    std::map<std::tuple<uint64_t, uint64_t, uint64_t>, std::string> blobs_;
    std::map<uint64_t, std::string> regs_;
};

// JSON text of a frame (the manifest) and its parser. ParseManifest accepts what WriteManifest
// writes; unknown keys are ignored.
std::string WriteManifest(const FrameRec& f);
bool ParseManifest(const std::string& text, FrameRec* out, std::string* err);
bool LoadFrame(const std::string& frame_dir, FrameRec* out, std::string* err);
// The register file of a pass (regs->size() == kNumRegs).
bool LoadRegs(const std::string& frame_dir, const std::string& file, std::vector<uint32_t>* regs, std::string* err);
bool ReadFile(const std::string& path, std::vector<uint8_t>* out);
bool WriteFile(const std::string& path, const void* data, size_t n);

// 8-bit RGB view of level 0, slice 0 of a raw texture:
//  unorm 8/16 bits, RGB10A2: value / max; snorm: (v + 1) / 2; sRGB: stored bytes;
//  float formats (16F, 32F, RG11B10F, RGB9E5F): x / (1 + x) per channel (Reinhard, x < 0 -> 0),
//    NaN -> magenta (255, 0, 255), +Inf -> white;
//  integer formats: per channel, linear from the image's minimum to its maximum;
//  depth: linear from the image's minimum to its maximum, grey; stencil: value, grey.
// One- and two-channel formats show R (and G); B is 0. Returns false for other formats.
bool ToViewRgb8(const GpuTextureId& t, const uint8_t* raw, size_t raw_size, std::vector<uint8_t>* rgb,
                std::string* mode);
bool WritePngRgb8(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgb);

} // namespace ps4gnm_capture

#endif // PS4_GNM_CAPTURE_H
