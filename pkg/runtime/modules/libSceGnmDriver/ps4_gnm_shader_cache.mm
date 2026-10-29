// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - persistent cache of GCN shader translations (see ps4_gnm_shader_cache.h).
 *
 * Cache key. A translation depends on
 *   1. the program bytes, the stage and the register-derived runtime info (gcn_runtime_info),
 *   2. the properties of the resources bound by the draw that triggered it (buffer strides and
 *      formats, image types, vertex attribute formats, ...), which shadPS4 records per
 *      permutation in its StageSpecialization,
 *   3. the host profile and MSL options, and the recompiler build itself.
 * (3) selects the cache directory: <title>/gcn/<hash of gcn_shader_cache_id(), kFormat, profile,
 * MSL options>; directories of other configurations are removed at startup. (1) is the Key,
 * whose hash starts the file name. (2) is only known after translation, so it is not hashed:
 * a key may have several cached permutations and each is checked with gcn_shader_is_compatible()
 * against the current draw, exactly like the permutations compiled during the session.
 *
 * File: FileHeader (key fields, blob size and hash) + gcn_shader_serialize() blob, named
 * <bucket>_<blob hash>.gsc and written to a temporary file that is renamed into place.
 * The stored shader is the raw recompiler output: the renderer's MSL post-processing
 * (FinishShader) runs on cached shaders as well, so changing it needs no invalidation.
 *
 * Loading. Start() lists the directory and indexes the files by key; a background thread then
 * deserializes every entry into a template (an entry is ~26 KB, mostly SPIR-V and MSL, and its
 * template about as large in memory). A lookup whose key the thread has not reached yet loads
 * that key's entries itself. Hits get a clone of the compatible template with the current code
 * address.
 */

#import <Foundation/Foundation.h>

#include "ps4_gnm_shader_cache.h"
#include "ps4_game_key.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace ps4gnm_shader_cache {
namespace {

// Bump when FileHeader or the key derivation changes.
constexpr uint32_t kFormat = 2;
constexpr char kMagic[8] = {'P', 'S', '4', 'G', 'S', 'C', '1', 0};

struct FileHeader {
    char magic[8];
    uint32_t hw, sw;
    uint32_t code_size;
    uint32_t rt_size;
    uint64_t code_hash[2];
    gcn_runtime_info rt;
    uint64_t blob_size;
    uint64_t blob_hash;
};

inline uint64_t Fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

// Second, independent hash of the program bytes (64-bit word mixing, splitmix64 finalizer).
uint64_t WordHash(const uint8_t* p, size_t n) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = (h ^ w) * 0xBF58476D1CE4E5B9ull;
        h ^= h >> 31;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 0x94D049BB133111EBull;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

uint64_t BucketOf(const Key& k) {
    uint64_t h = Fnv(&k.hw, sizeof(k.hw));
    h = Fnv(&k.sw, sizeof(k.sw), h);
    h = Fnv(&k.code_size, sizeof(k.code_size), h);
    h = Fnv(k.code_hash, sizeof(k.code_hash), h);
    return Fnv(&k.rt, sizeof(k.rt), h);
}

bool SameKey(const Key& a, const Key& b) {
    return a.bucket == b.bucket && a.hw == b.hw && a.sw == b.sw && a.code_size == b.code_size &&
           a.code_hash[0] == b.code_hash[0] && a.code_hash[1] == b.code_hash[1] &&
           memcmp(&a.rt, &b.rt, sizeof(a.rt)) == 0;
}

struct Entry {
    Key key;
    std::string name; // file name in C.dir
    uint64_t content = 0;
    gcn_shader* tmpl = nullptr; // owned
};

// Permutations of a bucket indexed by their specialization hash under one draw slot mask
// (gcn_draw_spec_mask): a lookup compares only the entries whose hash equals the draw's.
struct MaskIndex {
    uint64_t mask[2] = {0, 0};
    std::unordered_map<uint64_t, std::vector<uint32_t>> by_hash; // spec hash -> entry indexes, ascending
};

struct Bucket {
    std::vector<std::string> files; // not deserialized yet (startup loader)
    std::vector<Entry> entries;
    bool loading = false;           // files are being deserialized by the startup loader
    std::vector<uint32_t> odd;      // entries with another resource layout than entries[0]
    std::vector<MaskIndex> masks;   // one per draw slot mask seen (at most kMaxMasks)
};
// Distinct slot masks indexed per bucket; draws with yet another mask compare every entry.
constexpr size_t kMaxMasks = 32;

struct Cache {
    bool enabled = false;
    bool verify = false;
    std::string dir;
    std::mutex mu;
    std::condition_variable loaded_cv; // a bucket finished loading
    std::unordered_map<uint64_t, Bucket> buckets;
    std::unordered_set<std::string> names; // files present or being written
    dispatch_queue_t writer = nullptr;
    // statistics (since the last PrintStats)
    std::atomic<uint64_t> lookups{0}, hits{0}, stores{0}, loaded{0}, rejected{0}, not_ready{0}, compared{0};
    std::atomic<uint64_t> load_us{0}, lookup_us{0};
    std::atomic<uint64_t> total_hits{0};
};

Cache C;

// Adds an entry to its bucket and to the bucket's mask indexes (C.mu held; only hashing, no I/O).
void AddEntryLocked(Bucket& b, const Entry& e) {
    const uint32_t idx = (uint32_t)b.entries.size();
    b.entries.push_back(e);
    if (!gcn_shader_same_resource_layout(e.tmpl, b.entries[0].tmpl)) {
        b.odd.push_back(idx);
        return;
    }
    for (MaskIndex& m : b.masks) m.by_hash[gcn_shader_spec_hash(e.tmpl, m.mask)].push_back(idx);
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(FileHeader) || st.st_size > (64 << 20)) {
        close(fd);
        return false;
    }
    out.resize((size_t)st.st_size);
    size_t got = 0;
    while (got < out.size()) {
        const ssize_t r = read(fd, out.data() + got, out.size() - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    return got == out.size();
}

// Validates a cache file; on success fills `key` (and `blob`/`blob_size` pointing into data).
bool ParseFile(const std::vector<uint8_t>& data, Key* key, const uint8_t** blob, size_t* blob_size,
               uint64_t* content) {
    if (data.size() < sizeof(FileHeader)) return false;
    FileHeader h;
    memcpy(&h, data.data(), sizeof(h));
    if (memcmp(h.magic, kMagic, sizeof(kMagic)) != 0 || h.rt_size != sizeof(gcn_runtime_info) ||
        h.blob_size != data.size() - sizeof(FileHeader))
        return false;
    const uint8_t* b = data.data() + sizeof(FileHeader);
    if (WordHash(b, (size_t)h.blob_size) != h.blob_hash) return false;
    key->hw = h.hw;
    key->sw = h.sw;
    key->code_size = h.code_size;
    key->code_hash[0] = h.code_hash[0];
    key->code_hash[1] = h.code_hash[1];
    key->rt = h.rt;
    key->bucket = BucketOf(*key);
    *blob = b;
    *blob_size = (size_t)h.blob_size;
    *content = h.blob_hash;
    return true;
}

// Deserializes the given files of one bucket (no lock held).
std::vector<Entry> LoadFiles(uint64_t bucket, const std::vector<std::string>& files) {
    std::vector<Entry> out;
    std::vector<uint8_t> data;
    for (const std::string& name : files) {
        const double t0 = CFAbsoluteTimeGetCurrent();
        Entry e;
        const uint8_t* blob = nullptr;
        size_t blob_size = 0;
        char err[256] = {0};
        bool ok = ReadFile(C.dir + "/" + name, data) && ParseFile(data, &e.key, &blob, &blob_size, &e.content) &&
                  e.key.bucket == bucket &&
                  gcn_shader_deserialize(blob, blob_size, 0, &e.tmpl, err, sizeof(err)) == GCN_OK;
        C.load_us += (uint64_t)((CFAbsoluteTimeGetCurrent() - t0) * 1e6);
        if (!ok) {
            // Corrupt or foreign file: remove it so it is rewritten by the next translation.
            fprintf(stderr, "[gnm-cache] dropping unusable cache entry %s%s%s\n", name.c_str(), err[0] ? ": " : "",
                    err);
            if (e.tmpl) gcn_shader_destroy(e.tmpl);
            unlink((C.dir + "/" + name).c_str());
            C.rejected++;
            continue;
        }
        C.loaded++;
        e.name = name;
        out.push_back(e);
    }
    return out;
}

// Deserializes the not yet loaded files of a bucket; returns once every file of the bucket
// known so far is loaded (also when another thread is loading them).
void LoadBucket(uint64_t bucket) {
    std::vector<std::string> files;
    {
        std::unique_lock<std::mutex> lk(C.mu);
        auto it = C.buckets.find(bucket);
        if (it == C.buckets.end()) return;
        C.loaded_cv.wait(lk, [&] { return !C.buckets[bucket].loading; });
        Bucket& b = C.buckets[bucket];
        if (b.files.empty()) return;
        files.swap(b.files);
        b.loading = true;
    }
    std::vector<Entry> loaded = LoadFiles(bucket, files);
    {
        std::lock_guard<std::mutex> lk(C.mu);
        Bucket& b = C.buckets[bucket];
        for (Entry& e : loaded) AddEntryLocked(b, e);
        b.loading = false;
    }
    C.loaded_cv.notify_all();
}

bool ParseName(const char* name, uint64_t* bucket) {
    // <16 hex>_<16 hex>.gsc
    if (strlen(name) != 37 || name[16] != '_' || strcmp(name + 33, ".gsc") != 0) return false;
    char hex[17];
    memcpy(hex, name, 16);
    hex[16] = 0;
    char* end = nullptr;
    *bucket = strtoull(hex, &end, 16);
    return end == hex + 16;
}

bool IsHexName(const char* s, size_t n) {
    if (strlen(s) != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

// Removes the cache directories of other recompiler builds/configurations (only files this
// cache writes: *.gsc and their temporaries).
void PruneOtherConfigs(const std::string& gcn_root, const std::string& keep) {
    DIR* d = opendir(gcn_root.c_str());
    if (!d) return;
    std::vector<std::string> stale;
    while (struct dirent* de = readdir(d))
        if (IsHexName(de->d_name, 16) && keep != de->d_name) stale.push_back(de->d_name);
    closedir(d);
    for (const std::string& s : stale) {
        const std::string path = gcn_root + "/" + s;
        DIR* sd = opendir(path.c_str());
        if (!sd) continue;
        size_t removed = 0;
        while (struct dirent* de = readdir(sd)) {
            const char* n = de->d_name;
            const size_t len = strlen(n);
            if ((len > 4 && strcmp(n + len - 4, ".gsc") == 0) || strstr(n, ".gsc.tmp")) {
                unlink((path + "/" + n).c_str());
                removed++;
            }
        }
        closedir(sd);
        rmdir(path.c_str());
        fprintf(stderr, "[gnm-cache] removed %zu entries of another recompiler build/configuration (%s)\n", removed,
                s.c_str());
    }
}

} // namespace

void Start(const gcn_profile& profile, const gcn_msl_options& msl) {
    if (C.enabled || getenv("PS4_GNM_NO_SHADER_CACHE") || getenv("PS4_GNM_NO_GCN_CACHE")) return;
    // Same per-title cache root as the MSL cache (ps4_gnm_draw.mm StartShaderWarmup).
    const std::string title_dir = TitleDir();
    if (title_dir.empty()) return;
    const char* id = gcn_shader_cache_id();
    uint64_t cfg = Fnv(id, strlen(id));
    cfg = Fnv(&kFormat, sizeof(kFormat), cfg);
    cfg = Fnv(&profile, sizeof(profile), cfg);
    cfg = Fnv(&msl, sizeof(msl), cfg);
    char cfg_name[17];
    snprintf(cfg_name, sizeof(cfg_name), "%016llx", (unsigned long long)cfg);
    const std::string gcn_root = title_dir + "/gcn";
    C.dir = gcn_root + "/" + cfg_name;
    [[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:C.dir.c_str()]
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    PruneOtherConfigs(gcn_root, cfg_name);
    // every entry is deserialized into memory at startup, so the size limit bounds memory too
    PruneDir(C.dir, ".gsc", BudgetBytes(160), "GCN translation");
    DIR* d = opendir(C.dir.c_str());
    if (!d) {
        fprintf(stderr, "[gnm-cache] cannot open %s; GCN translation cache disabled\n", C.dir.c_str());
        return;
    }
    std::vector<uint64_t> order;
    size_t nfiles = 0;
    while (struct dirent* de = readdir(d)) {
        uint64_t bucket;
        if (strstr(de->d_name, ".gsc.tmp")) {
            unlink((C.dir + "/" + de->d_name).c_str()); // left over by an interrupted write
            continue;
        }
        if (!ParseName(de->d_name, &bucket)) continue;
        auto& b = C.buckets[bucket];
        if (b.files.empty()) order.push_back(bucket);
        b.files.push_back(de->d_name);
        C.names.insert(de->d_name);
        nfiles++;
    }
    closedir(d);
    C.verify = getenv("PS4_GNM_GCN_CACHE_VERIFY") != nullptr;
    C.writer = dispatch_queue_create("ps4.gnm.gcn_cache.writer", DISPATCH_QUEUE_SERIAL);
    C.enabled = true;
    fprintf(stderr, "[gnm-cache] GCN translation cache %s: %zu entries (%zu programs)\n", C.dir.c_str(), nfiles,
            order.size());
    if (order.empty()) return;
    // Deserialize every entry in the background so that first uses only run the compatibility
    // check. A lookup that comes first loads the entries of its key itself.
    std::thread([order] {
        const double t0 = CFAbsoluteTimeGetCurrent();
        for (uint64_t b : order) LoadBucket(b);
        size_t n = 0;
        {
            std::lock_guard<std::mutex> lk(C.mu);
            for (const auto& [k, b] : C.buckets) n += b.entries.size();
        }
        fprintf(stderr, "[gnm-cache] warm-up: %zu entries ready in %.2f s\n", n, CFAbsoluteTimeGetCurrent() - t0);
    }).detach();
}

bool Enabled() {
    return C.enabled;
}

std::string TitleDir() {
    static const std::string dir = [] {
        std::string r;
        if (const char* e = getenv("PS4RECOMP_CACHE_DIR"); e && e[0]) {
            r = e;
        } else if (const char* home = getenv("HOME"); home && home[0]) {
            r = std::string(home) + "/Library/Caches/PS4Recomp";
        } else {
            return std::string();
        }
        while (r.size() > 1 && r.back() == '/') r.pop_back();
        // Keyed like the settings file: the TITLE_ID, else the app root's name (a dump directory
        // named after its TITLE_ID keeps the caches it had under the old app-root naming).
        char key[256];
        if (!ps4_current_game_key(key, sizeof(key))) snprintf(key, sizeof(key), "default");
        r += "/";
        r += key;
        fprintf(stderr, "[gnm-cache] shader caches of this title: %s\n", r.c_str());
        return r;
    }();
    return dir;
}

uint64_t BudgetBytes(uint64_t default_mb) {
    const char* e = getenv("PS4_GNM_SHADER_CACHE_MB");
    const long long mb = e ? atoll(e) : 0;
    return (uint64_t)(mb > 0 ? mb : (long long)default_mb) << 20;
}

void PruneDir(const std::string& dir, const char* suffix, uint64_t budget, const char* what) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct F {
        std::string name;
        uint64_t size;
        struct timespec mtime;
    };
    std::vector<F> files;
    uint64_t total = 0;
    const size_t slen = strlen(suffix);
    while (struct dirent* de = readdir(d)) {
        const size_t len = strlen(de->d_name);
        if (len <= slen || strcmp(de->d_name + len - slen, suffix) != 0) continue;
        struct stat st;
        if (stat((dir + "/" + de->d_name).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        files.push_back({de->d_name, (uint64_t)st.st_size, st.st_mtimespec});
        total += (uint64_t)st.st_size;
    }
    closedir(d);
    if (total <= budget) return;
    std::sort(files.begin(), files.end(), [](const F& a, const F& b) {
        return a.mtime.tv_sec != b.mtime.tv_sec ? a.mtime.tv_sec < b.mtime.tv_sec : a.mtime.tv_nsec < b.mtime.tv_nsec;
    });
    const uint64_t target = budget / 4 * 3, before = total;
    size_t removed = 0;
    for (const F& f : files) {
        if (total <= target) break;
        if (unlink((dir + "/" + f.name).c_str()) == 0) {
            total -= f.size;
            removed++;
        }
    }
    fprintf(stderr, "[gnm-cache] %s cache over its limit (%.0f MB > %.0f MB): removed %zu least recently used files, %.0f MB left\n",
            what, before / 1048576.0, budget / 1048576.0, removed, total / 1048576.0);
}

void MarkUsed(const std::string& path) {
    static std::mutex mu;
    static std::unordered_set<std::string> done;
    {
        std::lock_guard<std::mutex> lk(mu);
        if (!done.insert(path).second) return;
    }
    static dispatch_queue_t q = dispatch_queue_create("ps4.gnm.cache.touch", DISPATCH_QUEUE_SERIAL);
    std::string* p = new std::string(path);
    dispatch_async(q, ^{
        utimes(p->c_str(), nullptr);
        delete p;
    });
}

bool VerifyMode() {
    return C.verify;
}

bool MakeKey(gcn_hw_stage hw, gcn_sw_stage sw, const gcn_runtime_info& rt, uint64_t code_addr,
             gcn_read_guest_fn read, void* ctx, Key* out) {
    uint32_t size = 0;
    uint64_t footer_hash = 0;
    if (!gcn_find_binary_info(read, ctx, code_addr, &size, &footer_hash) || size < 8 || size > (1u << 22))
        return false;
    // gcn_compile translates exactly `size` bytes from the code address (OrbShdr length).
    std::vector<uint8_t> code(size);
    if (!read(ctx, code_addr, code.data(), size)) return false;
    *out = Key{};
    out->hw = hw;
    out->sw = sw;
    out->code_size = size;
    // The footer hash becomes the shader's pgm_hash, so it is part of the key as well.
    out->code_hash[0] = Fnv(&footer_hash, sizeof(footer_hash), Fnv(code.data(), size));
    out->code_hash[1] = WordHash(code.data(), size);
    out->rt = rt;
    out->bucket = BucketOf(*out);
    return true;
}

gcn_shader* Lookup(const Key& key, uint64_t code_addr, const uint32_t ud[16], gcn_read_guest_fn read, void* ctx) {
    if (!C.enabled) return nullptr;
    const double t0 = CFAbsoluteTimeGetCurrent();
    C.lookups++;
    auto done = [&](gcn_shader* hit) {
        C.lookup_us += (uint64_t)((CFAbsoluteTimeGetCurrent() - t0) * 1e6);
        if (hit) {
            C.hits++;
            C.total_hits++;
        }
        return hit;
    };
    // No disk I/O and no waiting here: entries stored in this session are in memory (Store), and
    // a bucket the startup loader has not finished counts as a miss (the caller translates).
    const gcn_shader* base = nullptr;
    {
        std::lock_guard<std::mutex> lk(C.mu);
        auto it = C.buckets.find(key.bucket);
        if (it == C.buckets.end()) return done(nullptr);
        const Bucket& b = it->second;
        if (b.loading || !b.files.empty()) {
            C.not_ready++;
            return done(nullptr);
        }
        if (b.entries.empty()) return done(nullptr);
        base = b.entries[0].tmpl;
    }
    // The draw's specialization, built once outside the lock (it walks guest pointer chains).
    // Templates are immutable once inserted and never removed.
    gcn_draw_spec* ds = gcn_draw_spec_create(base, ud, &key.rt, read, ctx);
    uint64_t mask[2] = {0, 0};
    uint64_t dh = 0;
    if (ds) {
        gcn_draw_spec_mask(ds, mask);
        dh = gcn_draw_spec_hash(ds, mask);
    }
    // The first compatible entry in insertion order, as a scan over all entries finds it. The
    // comparisons run under the lock: they only compare specializations (entries of another
    // layout read guest memory), no I/O, no compile.
    const gcn_shader* found = nullptr;
    std::string found_name;
    {
        std::lock_guard<std::mutex> lk(C.mu);
        Bucket& b = C.buckets[key.bucket];
        uint64_t compared = 0;
        auto test = [&](uint32_t i) {
            const Entry& e = b.entries[i];
            if (!SameKey(e.key, key)) return false;
            compared++;
            if (!gcn_shader_matches_draw_spec(e.tmpl, ds, ud, &key.rt, read, ctx)) return false;
            found = e.tmpl;
            found_name = e.name;
            return true;
        };
        MaskIndex* mi = nullptr;
        if (ds) {
            for (MaskIndex& m : b.masks)
                if (m.mask[0] == mask[0] && m.mask[1] == mask[1]) mi = &m;
            if (!mi && b.masks.size() < kMaxMasks) {
                // first draw with this slot mask: index the bucket's entries under it (hashing only)
                MaskIndex& m = b.masks.emplace_back();
                m.mask[0] = mask[0];
                m.mask[1] = mask[1];
                const gcn_shader* b0 = b.entries[0].tmpl;
                for (uint32_t i = 0; i < b.entries.size(); ++i)
                    if (gcn_shader_same_resource_layout(b.entries[i].tmpl, b0))
                        m.by_hash[gcn_shader_spec_hash(b.entries[i].tmpl, mask)].push_back(i);
                mi = &m;
            }
        }
        if (mi) {
            // merge the hash slot and the entries of another layout, both ascending
            static const std::vector<uint32_t> none;
            auto it = mi->by_hash.find(dh);
            const std::vector<uint32_t>& slot = it == mi->by_hash.end() ? none : it->second;
            size_t x = 0, y = 0;
            while (x < slot.size() || y < b.odd.size()) {
                const uint32_t i = y >= b.odd.size() || (x < slot.size() && slot[x] < b.odd[y]) ? slot[x++] : b.odd[y++];
                if (test(i)) break;
            }
        } else {
            for (uint32_t i = 0; i < b.entries.size(); ++i) // no index for this draw
                if (test(i)) break;
        }
        C.compared += compared;
        static const bool verify_index = getenv("PS4_GNM_GCN_INDEX_VERIFY") != nullptr;
        if (verify_index) {
            // Debug (PS4_GNM_GCN_INDEX_VERIFY): the index must pick what a scan over every entry
            // with gcn_shader_is_compatible picks.
            const gcn_shader* scan = nullptr;
            for (const Entry& e : b.entries)
                if (SameKey(e.key, key) && gcn_shader_is_compatible(e.tmpl, ud, &key.rt, read, ctx)) {
                    scan = e.tmpl;
                    break;
                }
            if (scan != found)
                fprintf(stderr, "[gnm-cache] verify: index lookup %s, a scan %s (bucket %016llx, %zu entries, %s)\n",
                        found ? "hit" : "missed", scan ? "hits" : "misses", (unsigned long long)key.bucket,
                        b.entries.size(), mi ? "indexed" : "not indexed");
        }
    }
    gcn_shader* hit = nullptr;
    if (found) {
        hit = gcn_shader_clone(found, code_addr);
        MarkUsed(C.dir + "/" + found_name);
    }
    gcn_draw_spec_destroy(ds);
    return done(hit);
}

void Store(const Key& key, const gcn_shader* sh) {
    if (!C.enabled || !sh) return;
    void* data = nullptr;
    size_t size = 0;
    if (gcn_shader_serialize(sh, &data, &size) != GCN_OK) return;
    FileHeader h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, kMagic, sizeof(kMagic));
    h.hw = key.hw;
    h.sw = key.sw;
    h.code_size = key.code_size;
    h.rt_size = sizeof(gcn_runtime_info);
    h.code_hash[0] = key.code_hash[0];
    h.code_hash[1] = key.code_hash[1];
    h.rt = key.rt;
    h.blob_size = size;
    h.blob_hash = WordHash((const uint8_t*)data, size);
    char name[64];
    snprintf(name, sizeof(name), "%016llx_%016llx.gsc", (unsigned long long)key.bucket,
             (unsigned long long)h.blob_hash);
    // The entry serves lookups of this session from memory at once (no reading back the file).
    gcn_shader* tmpl = nullptr;
    gcn_shader_deserialize(data, size, 0, &tmpl, nullptr, 0);
    {
        std::lock_guard<std::mutex> lk(C.mu);
        if (!C.names.insert(name).second) { // already on disk (or being written)
            if (tmpl) gcn_shader_destroy(tmpl);
            free(data);
            return;
        }
        if (tmpl) {
            Entry e;
            e.key = key;
            e.name = name;
            e.content = h.blob_hash;
            e.tmpl = tmpl;
            AddEntryLocked(C.buckets[key.bucket], e);
        }
    }
    C.stores++;
    const std::string path = C.dir + "/" + name;
    dispatch_async(C.writer, ^{
        char tmp[1200];
        snprintf(tmp, sizeof(tmp), "%s.tmp%d", path.c_str(), (int)getpid());
        FILE* f = fopen(tmp, "wb");
        bool ok = f && fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(data, 1, size, f) == size;
        if (f) ok = (fclose(f) == 0) && ok;
        if (ok) ok = rename(tmp, path.c_str()) == 0;
        if (!ok) {
            unlink(tmp);
            fprintf(stderr, "[gnm-cache] could not write %s\n", path.c_str());
        }
        free(data);
    });
}

bool SameTranslation(const gcn_shader* a, const gcn_shader* b, std::string* why) {
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    gcn_shader_info ia, ib;
    gcn_shader_get_info(a, &ia);
    gcn_shader_get_info(b, &ib);
    ia.code_addr = ib.code_addr = 0;
    if (memcmp(&ia, &ib, sizeof(ia)) != 0) return fail("shader info");
    const char* ma = gcn_shader_msl(a);
    const char* mb = gcn_shader_msl(b);
    if ((ma == nullptr) != (mb == nullptr) || (ma && strcmp(ma, mb) != 0)) return fail("MSL");
    size_t na = 0, nb = 0;
    const uint32_t* sa = gcn_shader_spirv(a, &na);
    const uint32_t* sb = gcn_shader_spirv(b, &nb);
    if (na != nb || (na && memcmp(sa, sb, na * 4) != 0)) return fail("SPIR-V");
    for (uint32_t i = 0; i < ia.num_buffers; ++i) {
        gcn_buffer_binding x, y;
        gcn_shader_get_buffer(a, i, &x);
        gcn_shader_get_buffer(b, i, &y);
        if (memcmp(&x, &y, sizeof(x)) != 0) return fail("buffer binding");
    }
    for (uint32_t i = 0; i < ia.num_images; ++i) {
        gcn_image_binding x, y;
        gcn_shader_get_image(a, i, &x);
        gcn_shader_get_image(b, i, &y);
        if (memcmp(&x, &y, sizeof(x)) != 0) return fail("image binding");
    }
    for (uint32_t i = 0; i < ia.num_samplers; ++i) {
        gcn_sampler_binding x, y;
        gcn_shader_get_sampler(a, i, &x);
        gcn_shader_get_sampler(b, i, &y);
        if (memcmp(&x, &y, sizeof(x)) != 0) return fail("sampler binding");
    }
    for (uint32_t i = 0; i < ia.num_vertex_attribs; ++i) {
        gcn_vertex_attrib x, y;
        gcn_shader_get_vertex_attrib(a, i, &x);
        gcn_shader_get_vertex_attrib(b, i, &y);
        if (memcmp(&x, &y, sizeof(x)) != 0) return fail("vertex attribute");
    }
    return true;
}

void PrintStats() {
    if (!C.enabled) return;
    const uint64_t lookups = C.lookups.exchange(0), hits = C.hits.exchange(0), stores = C.stores.exchange(0);
    const uint64_t loaded = C.loaded.exchange(0), rejected = C.rejected.exchange(0);
    const uint64_t load_us = C.load_us.exchange(0), lookup_us = C.lookup_us.exchange(0);
    const uint64_t not_ready = C.not_ready.exchange(0), compared = C.compared.exchange(0);
    if (!lookups && !stores && !loaded) return;
    fprintf(stderr,
            "[gnm-cache] lookups %llu, hits %llu (%llu this session), new entries %llu, loaded %llu (%.1f ms), "
            "rejected %llu, lookup time %.1f ms, %llu permutations compared, %llu before their entries were loaded\n",
            (unsigned long long)lookups, (unsigned long long)hits, (unsigned long long)C.total_hits.load(),
            (unsigned long long)stores, (unsigned long long)loaded, load_us / 1000.0, (unsigned long long)rejected,
            lookup_us / 1000.0, (unsigned long long)compared, (unsigned long long)not_ready);
}

} // namespace ps4gnm_shader_cache
