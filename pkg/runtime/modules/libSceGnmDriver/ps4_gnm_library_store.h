// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - persistent store of compiled Metal shader libraries (see ps4_gnm_library_store.mm).
 *
 * Compiling a translated shader's MSL into a Metal library (newLibraryWithSource) takes about
 * 320 ms per TLOU shader when the system Metal cache misses. That cache is keyed by the length
 * of the executable's directory path (and the bundle ID), is shared with other processes and is
 * evicted by the OS, so a game moved to another directory, or launched after other Metal work
 * filled the cache, compiles every shader again; shaders seen in earlier sessions are compiled
 * on the CP thread inside a frame. This store keeps each library's compiled form (AIR) on disk,
 * keyed by a hash of the MSL, and loads it with newLibraryWithData in about a millisecond,
 * independent of the system cache and of where the game runs.
 *
 * The AIR comes from a serialized MTLBinaryArchive that holds one pipeline using the function: a
 * serialized archive is a metallib with an AIR slice next to the GPU code, and Metal loads it as
 * a library. A render pipeline's archive holds its vertex and fragment functions (Metal renames
 * one of the two "main0"), so the function is picked by type, not by name.
 *
 * Files: <title cache>/mlib/<config>/<h1>_<h2>.mlib = Header + archive bytes. <config> hashes
 * the file format, the compile options and the OS build (a new OS may bring a new Metal compiler
 * and AIR version); directories of other configurations are deleted at Open. A changed translator
 * or MSL post-processing changes the MSL and therefore the key; entries nobody uses any more age
 * out through the size budget (least recently used first).
 *
 * Environment:
 *   PS4_GNM_NO_LIB_STORE=1   disable the store (libraries come from MSL as before)
 *   PS4_GNM_LIB_STORE_MB     size budget (default 256)
 */
#pragma once

#import <Metal/Metal.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps4gnm_lib_store {

// Function stages, equal to MTLFunctionType values.
enum Stage : uint32_t { kVertex = 1, kFragment = 2, kKernel = 3 };

// Identifies one MSL source: two independent 64-bit hashes and its length. h1 is the renderer's
// MSL hash (ps4_gnm_draw.mm Fnv: FNV-1a steps from the seed 1469598103934665603), h2 a 64-bit
// word hash with a splitmix64 finalizer.
struct Key {
    uint64_t h1 = 0, h2 = 0;
    uint32_t len = 0;
};
bool operator==(const Key& a, const Key& b);
Key MakeKey(const char* msl, size_t len);

// "<h1>_<h2>.mlib" (16 lowercase hex digits each); ParseFileName fills h1 and h2 only.
std::string FileName(const Key& k);
bool ParseFileName(const char* name, Key* k);

// ---- File format (no Metal; tested by tests/host/lib_store_test.mm) ----
constexpr uint32_t kFormatVersion = 1;
struct Header {
    char magic[8];  // "PS4MLIB\0"
    uint32_t version;
    uint32_t stage;
    uint64_t h1, h2;
    uint32_t msl_len;
    uint32_t reserved;
    uint64_t payload_size;
    uint64_t payload_hash;
};
static_assert(sizeof(Header) == 56, "library store header layout");

enum class Status { kOk, kTooSmall, kBadMagic, kBadVersion, kWrongKey, kWrongStage, kBadSize, kBadHash };
const char* StatusName(Status s);

// 64-bit hash of a payload (the same word hash as MakeKey's h2).
uint64_t PayloadHash(const uint8_t* p, size_t n);
void Encode(const Key& k, uint32_t stage, const uint8_t* payload, size_t n, std::vector<uint8_t>* out);
// Checks a file's bytes against the expected key and stage; on kOk *payload/*payload_n locate
// the archive bytes inside data.
Status Decode(const uint8_t* data, size_t n, const Key& want, uint32_t stage, const uint8_t** payload,
              size_t* payload_n);

// Directory name of a configuration: format version, compile options tag, OS build, GPU name.
std::string ConfigName(const char* options_tag, const char* os_build, const char* device);

// Describes every MTLCompileOptions field that changes the compiled code (math mode and
// fast-math, floating-point functions, invariance, language version, library type and install
// name, optimization level, preprocessor macros): the compile-options part of the configuration,
// derived from the options object itself so that a changed option can never reuse entries.
std::string OptionsTag(MTLCompileOptions* o);

// Session circuit breaker: after this many entries that loaded but failed (a refused load, or
// a pipeline that could not be built from a stored function), the store is disabled for the
// rest of the session (logged once) and every library comes from MSL.
constexpr uint32_t kMaxFailuresPerSession = 3;

// ---- Store (Metal) ----
// Opens <title_dir>/mlib/<config>: deletes the directories of other configurations and leftover
// temporary files, trims the directory to the budget (least recently used first) and indexes the
// entries. options are the MTLCompileOptions every library is compiled with (OptionsTag). False
// when disabled or the directory cannot be created.
bool Open(id<MTLDevice> dev, const std::string& title_dir, MTLCompileOptions* options, uint64_t budget_bytes);
// False before Open, when disabled, or once the circuit breaker tripped.
bool Enabled();
const std::string& Dir();
// True if an entry for k is on disk or being written.
bool Has(const Key& k);
// Reads the header of the entry named by k.h1/k.h2 (k.len is not used): its full key and stage.
// False if there is no such entry or its header is not of this format.
bool Peek(const Key& k, Key* full, uint32_t* stage);
// The function of stage `stage` from the entry of k, or nil. Reads and checks the file, creates
// the library from it; an entry that fails is deleted (it is written again by a later pipeline).
// why (optional) gets the reason of a failure.
id<MTLFunction> Load(const Key& k, uint32_t stage, std::string* why);
// One pipeline could not be built with functions loaded from the entries a and b (either may be
// null; pass only the stored ones): deletes the entries (the caller compiles the MSL and
// retries) and counts one failure toward the circuit breaker — none if every entry has counted
// already this session (as a refused load or an earlier report). Returns true if it counted.
// Works also after the breaker tripped.
bool ReportPipelineFailure(const Key* a, const Key* b = nullptr);
// Failures counted this session (refused loads and reported pipelines).
uint32_t Failures();

// Writes entries for the functions of a pipeline that has just been built, for each given key
// without an entry (vs or ps may be null). Asynchronous; the descriptor is copied.
void StoreRender(const Key* vs, const Key* ps, MTLRenderPipelineDescriptor* d);
void StoreCompute(const Key& cs, MTLComputePipelineDescriptor* d);
// Waits until queued writes are done (at most timeout_s). False on timeout.
bool Flush(double timeout_s);
// One "[gnm-lib-store] ..." line with session totals.
void PrintStats();

} // namespace ps4gnm_lib_store
