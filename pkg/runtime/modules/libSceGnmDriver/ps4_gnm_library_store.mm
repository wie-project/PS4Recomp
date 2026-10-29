// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - persistent store of compiled Metal shader libraries (see ps4_gnm_library_store.h).
#include "ps4_gnm_library_store.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>

namespace ps4gnm_lib_store {
namespace {

constexpr char kMagic[8] = {'P', 'S', '4', 'M', 'L', 'I', 'B', 0};
constexpr const char* kSuffix = ".mlib";
// A larger file is not an entry of this store (an entry is one pipeline's archive).
constexpr size_t kMaxFileBytes = 32u << 20;

uint64_t Fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

// 64-bit word mixing with a splitmix64 finalizer; independent of FNV.
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

bool IsHex(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

bool EndsWith(const char* s, const char* suffix) {
    const size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

struct StoreState {
    id<MTLDevice> dev = nil;
    std::atomic<bool> enabled{false};
    std::string dir;
    uint64_t budget = 0;
    dispatch_queue_t q = nil;       // writes and use marks, in order
    std::mutex mu;                  // present, pending, touched, bytes
    std::set<std::pair<uint64_t, uint64_t>> present, pending, touched;
    // entries that already counted toward the circuit breaker (each counts once per session)
    std::set<std::pair<uint64_t, uint64_t>> counted;
    uint64_t bytes = 0;             // size of the entries on disk
    bool full = false;              // over the budget: no more writes this session
    // statistics (session)
    std::atomic<uint64_t> loads{0}, load_us{0}, load_fail{0}, writes{0}, write_us{0}, write_fail{0};
    std::atomic<uint32_t> failures{0}; // circuit breaker (refused loads + reported pipelines)
    size_t opened = 0;
};
StoreState G;

std::pair<uint64_t, uint64_t> Id(const Key& k) {
    return {k.h1, k.h2};
}

std::string PathOf(const Key& k) {
    return G.dir + "/" + FileName(k);
}

bool ReadAll(const std::string& path, std::vector<uint8_t>* out, size_t limit) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || (uint64_t)st.st_size > limit) {
        close(fd);
        return false;
    }
    out->resize((size_t)st.st_size);
    size_t got = 0;
    while (got < out->size()) {
        const ssize_t r = read(fd, out->data() + got, out->size() - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    return got == out->size();
}

bool WriteAll(const std::string& path, const std::vector<uint8_t>& data) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    size_t put = 0;
    while (put < data.size()) {
        const ssize_t w = write(fd, data.data() + put, data.size() - put);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        put += (size_t)w;
    }
    const bool ok = close(fd) == 0 && put == data.size();
    return ok;
}

// Temporary files are "<name>.tmp<pid>"; those of processes that no longer run are removed.
bool StaleTemp(const char* name) {
    const char* t = strstr(name, ".tmp");
    if (!t) return false;
    const long pid = strtol(t + 4, nullptr, 10);
    if (pid <= 0) return true;
    return pid != getpid() && kill((pid_t)pid, 0) == -1 && errno == ESRCH;
}

// Deletes the entries (and temporaries) of other configurations under root.
void RemoveOtherConfigs(const std::string& root, const std::string& keep) {
    DIR* d = opendir(root.c_str());
    if (!d) return;
    std::vector<std::string> other;
    while (dirent* de = readdir(d))
        if (strlen(de->d_name) == 16 && IsHex(de->d_name, 16) && keep != de->d_name) other.push_back(de->d_name);
    closedir(d);
    for (const std::string& o : other) {
        const std::string p = root + "/" + o;
        DIR* sd = opendir(p.c_str());
        if (!sd) continue;
        size_t removed = 0;
        while (dirent* de = readdir(sd)) {
            if (EndsWith(de->d_name, kSuffix) || strstr(de->d_name, ".tmp")) {
                unlink((p + "/" + de->d_name).c_str());
                removed++;
            }
        }
        closedir(sd);
        rmdir(p.c_str());
        fprintf(stderr, "[gnm-lib-store] removed %zu entries of another configuration (%s)\n", removed, o.c_str());
    }
}

void MarkUsed(const Key& k) {
    {
        std::lock_guard<std::mutex> lk(G.mu);
        if (!G.touched.insert(Id(k)).second) return;
    }
    const std::string path = PathOf(k);
    dispatch_async(G.q, ^{
        utimes(path.c_str(), nullptr);
    });
}

void Forget(const Key& k, bool remove_file) {
    if (remove_file) unlink(PathOf(k).c_str());
    std::lock_guard<std::mutex> lk(G.mu);
    G.present.erase(Id(k));
}

// Counts one failure for an event involving the given entries, unless every one of them has
// counted already (a failed load racing a reported pipeline of the same entry counts once); at
// kMaxFailuresPerSession the store is disabled for the session. Returns true if it counted.
bool CountFailure(const Key* const* keys, size_t n) {
    bool any = false;
    {
        std::lock_guard<std::mutex> lk(G.mu);
        for (size_t i = 0; i < n; ++i)
            if (keys[i] && G.counted.insert(Id(*keys[i])).second) any = true;
    }
    if (!any) return false;
    if (G.failures.fetch_add(1) + 1 == kMaxFailuresPerSession) {
        G.enabled = false;
        fprintf(stderr,
                "[gnm-lib-store] %u stored libraries failed this session: store disabled until the next start, "
                "libraries come from MSL\n",
                kMaxFailuresPerSession);
    }
    return true;
}

struct Item {
    Key key;
    uint32_t stage;
};

// Keeps the items without an entry and marks them pending; false if nothing is left.
bool Claim(std::vector<Item>& items) {
    std::lock_guard<std::mutex> lk(G.mu);
    if (G.full) return false;
    std::vector<Item> keep;
    for (const Item& it : items)
        if (!G.present.count(Id(it.key)) && G.pending.insert(Id(it.key)).second) keep.push_back(it);
    items.swap(keep);
    return !items.empty();
}

// Runs on G.q: archives the pipeline (add), serializes it and writes one entry per item.
void WriteEntries(const std::vector<Item>& items, BOOL (^add)(id<MTLBinaryArchive>, NSError**)) {
    const double t0 = CFAbsoluteTimeGetCurrent();
    NSError* e = nil;
    std::string why;
    std::vector<uint8_t> payload;
    const std::string metal_tmp = G.dir + "/" + FileName(items[0].key) + ".metalar.tmp" + std::to_string((int)getpid());
    @autoreleasepool {
        MTLBinaryArchiveDescriptor* ad = [MTLBinaryArchiveDescriptor new];
        id<MTLBinaryArchive> ar = [G.dev newBinaryArchiveWithDescriptor:ad error:&e];
        if (!ar)
            why = "new archive";
        else if (!add(ar, &e))
            why = "add";
        else if (![ar serializeToURL:[NSURL fileURLWithPath:@(metal_tmp.c_str())] error:&e])
            why = "serialize";
        else if (!ReadAll(metal_tmp, &payload, kMaxFileBytes))
            why = "read back";
    }
    unlink(metal_tmp.c_str());
    size_t written = 0;
    uint64_t added = 0;
    if (why.empty()) {
        std::vector<uint8_t> file;
        for (const Item& it : items) {
            Encode(it.key, it.stage, payload.data(), payload.size(), &file);
            const std::string final_path = PathOf(it.key);
            const std::string tmp = final_path + ".tmp" + std::to_string((int)getpid());
            if (!WriteAll(tmp, file) || rename(tmp.c_str(), final_path.c_str()) != 0) {
                unlink(tmp.c_str());
                why = "write " + final_path + ": " + strerror(errno);
                continue;
            }
            std::lock_guard<std::mutex> lk(G.mu);
            G.present.insert(Id(it.key));
            G.touched.insert(Id(it.key)); // just written: its time is current
            ++written;
            added += file.size();
        }
    }
    {
        std::lock_guard<std::mutex> lk(G.mu);
        for (const Item& it : items) G.pending.erase(Id(it.key));
        G.bytes += added;
        if (G.bytes > G.budget && !G.full) {
            G.full = true;
            fprintf(stderr, "[gnm-lib-store] %llu MB reached the budget (%llu MB): no more entries this session\n",
                    (unsigned long long)(G.bytes >> 20), (unsigned long long)(G.budget >> 20));
        }
    }
    G.writes += written;
    G.write_us += (uint64_t)((CFAbsoluteTimeGetCurrent() - t0) * 1e6);
    if (!why.empty()) {
        G.write_fail++;
        fprintf(stderr, "[gnm-lib-store] could not store %016llx: %s%s%s\n", (unsigned long long)items[0].key.h1,
                why.c_str(), e ? ": " : "", e ? e.localizedDescription.UTF8String : "");
    }
}

} // namespace

bool operator==(const Key& a, const Key& b) {
    return a.h1 == b.h1 && a.h2 == b.h2 && a.len == b.len;
}

Key MakeKey(const char* msl, size_t len) {
    Key k;
    k.h1 = Fnv(msl, len);
    k.h2 = WordHash((const uint8_t*)msl, len);
    k.len = (uint32_t)std::min<size_t>(len, UINT32_MAX);
    return k;
}

std::string FileName(const Key& k) {
    char b[64];
    snprintf(b, sizeof(b), "%016llx_%016llx%s", (unsigned long long)k.h1, (unsigned long long)k.h2, kSuffix);
    return b;
}

bool ParseFileName(const char* name, Key* k) {
    // 16 hex, '_', 16 hex, ".mlib"
    if (strlen(name) != 33 + strlen(kSuffix) || name[16] != '_' || strcmp(name + 33, kSuffix) != 0 ||
        !IsHex(name, 16) || !IsHex(name + 17, 16))
        return false;
    char hex[17];
    memcpy(hex, name, 16);
    hex[16] = 0;
    k->h1 = strtoull(hex, nullptr, 16);
    memcpy(hex, name + 17, 16);
    k->h2 = strtoull(hex, nullptr, 16);
    return true;
}

const char* StatusName(Status s) {
    switch (s) {
        case Status::kOk: return "ok";
        case Status::kTooSmall: return "too small";
        case Status::kBadMagic: return "not a library store entry";
        case Status::kBadVersion: return "other format version";
        case Status::kWrongKey: return "entry of another MSL";
        case Status::kWrongStage: return "entry of another stage";
        case Status::kBadSize: return "truncated or extended";
        case Status::kBadHash: return "contents damaged";
    }
    return "?";
}

uint64_t PayloadHash(const uint8_t* p, size_t n) {
    return WordHash(p, n);
}

void Encode(const Key& k, uint32_t stage, const uint8_t* payload, size_t n, std::vector<uint8_t>* out) {
    Header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, kMagic, sizeof(kMagic));
    h.version = kFormatVersion;
    h.stage = stage;
    h.h1 = k.h1;
    h.h2 = k.h2;
    h.msl_len = k.len;
    h.payload_size = n;
    h.payload_hash = PayloadHash(payload, n);
    out->resize(sizeof(h) + n);
    memcpy(out->data(), &h, sizeof(h));
    if (n) memcpy(out->data() + sizeof(h), payload, n);
}

Status Decode(const uint8_t* data, size_t n, const Key& want, uint32_t stage, const uint8_t** payload,
              size_t* payload_n) {
    if (n < sizeof(Header)) return Status::kTooSmall;
    Header h;
    memcpy(&h, data, sizeof(h));
    if (memcmp(h.magic, kMagic, sizeof(kMagic)) != 0) return Status::kBadMagic;
    if (h.version != kFormatVersion) return Status::kBadVersion;
    if (h.h1 != want.h1 || h.h2 != want.h2 || h.msl_len != want.len) return Status::kWrongKey;
    if (h.stage != stage) return Status::kWrongStage;
    if (h.payload_size != n - sizeof(Header)) return Status::kBadSize;
    const uint8_t* p = data + sizeof(Header);
    if (PayloadHash(p, (size_t)h.payload_size) != h.payload_hash) return Status::kBadHash;
    *payload = p;
    *payload_n = (size_t)h.payload_size;
    return Status::kOk;
}

std::string ConfigName(const char* options_tag, const char* os_build, const char* device) {
    uint64_t h = Fnv(&kFormatVersion, sizeof(kFormatVersion));
    h = Fnv(options_tag, strlen(options_tag) + 1, h);
    h = Fnv(os_build, strlen(os_build) + 1, h);
    h = Fnv(device, strlen(device) + 1, h);
    char b[17];
    snprintf(b, sizeof(b), "%016llx", (unsigned long long)h);
    return b;
}

std::string OptionsTag(MTLCompileOptions* o) {
    std::string t;
    char b[96];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    snprintf(b, sizeof(b), "fastMath=%d;", (int)o.fastMathEnabled);
#pragma clang diagnostic pop
    t += b;
    if (@available(macOS 15.0, *)) {
        snprintf(b, sizeof(b), "mathMode=%ld;mathFP=%ld;", (long)o.mathMode, (long)o.mathFloatingPointFunctions);
        t += b;
    }
    snprintf(b, sizeof(b), "invariance=%d;lang=%lu;libType=%ld;", (int)o.preserveInvariance, (unsigned long)o.languageVersion,
             (long)o.libraryType);
    t += b;
    if (@available(macOS 13.0, *)) {
        snprintf(b, sizeof(b), "opt=%ld;", (long)o.optimizationLevel);
        t += b;
    }
    t += "install=";
    if (o.installName) t += o.installName.UTF8String;
    t += ";macros=";
    NSArray* names = [o.preprocessorMacros.allKeys sortedArrayUsingSelector:@selector(compare:)];
    for (NSString* n in names) {
        t += n.UTF8String;
        t += "=";
        t += [[o.preprocessorMacros[n] description] UTF8String];
        t += ",";
    }
    return t;
}

bool Open(id<MTLDevice> dev, const std::string& title_dir, MTLCompileOptions* options, uint64_t budget_bytes) {
    const char* off = getenv("PS4_GNM_NO_LIB_STORE");
    if ((off && strcmp(off, "1") == 0) || !dev || title_dir.empty()) return false;
    char os[64] = {0};
    size_t osn = sizeof(os) - 1;
    if (sysctlbyname("kern.osversion", os, &osn, nullptr, 0) != 0) os[0] = 0;
    const std::string root = title_dir + "/mlib";
    const std::string tag = OptionsTag(options);
    const std::string config = ConfigName(tag.c_str(), os, dev.name.UTF8String ? dev.name.UTF8String : "");
    G.dir = root + "/" + config;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:@(G.dir.c_str())
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:nil]) {
        fprintf(stderr, "[gnm-lib-store] cannot create %s: disabled\n", G.dir.c_str());
        return false;
    }
    RemoveOtherConfigs(root, config);
    struct F {
        std::string name;
        Key key;
        uint64_t size;
        struct timespec mtime;
    };
    std::vector<F> files;
    uint64_t total = 0;
    if (DIR* d = opendir(G.dir.c_str())) {
        while (dirent* de = readdir(d)) {
            if (strstr(de->d_name, ".tmp")) {
                if (StaleTemp(de->d_name)) unlink((G.dir + "/" + de->d_name).c_str());
                continue;
            }
            Key k;
            if (!ParseFileName(de->d_name, &k)) continue;
            struct stat st;
            if (stat((G.dir + "/" + de->d_name).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            files.push_back({de->d_name, k, (uint64_t)st.st_size, st.st_mtimespec});
            total += (uint64_t)st.st_size;
        }
        closedir(d);
    }
    if (total > budget_bytes) {
        std::sort(files.begin(), files.end(), [](const F& a, const F& b) {
            return a.mtime.tv_sec != b.mtime.tv_sec ? a.mtime.tv_sec < b.mtime.tv_sec : a.mtime.tv_nsec < b.mtime.tv_nsec;
        });
        const uint64_t target = budget_bytes / 4 * 3, before = total;
        size_t i = 0;
        for (; i < files.size() && total > target; ++i)
            if (unlink((G.dir + "/" + files[i].name).c_str()) == 0) total -= files[i].size;
        fprintf(stderr, "[gnm-lib-store] over the budget (%llu MB > %llu MB): removed %zu least recently used entries\n",
                (unsigned long long)(before >> 20), (unsigned long long)(budget_bytes >> 20), i);
        files.erase(files.begin(), files.begin() + (ptrdiff_t)i);
    }
    G.present.clear();
    for (const F& f : files) G.present.insert(Id(f.key));
    G.bytes = total;
    G.budget = budget_bytes;
    G.full = false;
    G.dev = dev;
    G.q = dispatch_queue_create("ps4.gnm.lib_store",
                                dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
    G.opened = G.present.size();
    G.failures = 0;
    G.counted.clear();
    G.enabled = true;
    fprintf(stderr, "[gnm-lib-store] %s: %zu libraries (%.1f MB)\n", G.dir.c_str(), G.opened, total / 1048576.0);
    return true;
}

bool Enabled() {
    return G.enabled;
}

const std::string& Dir() {
    return G.dir;
}

bool Has(const Key& k) {
    if (!G.enabled) return false;
    std::lock_guard<std::mutex> lk(G.mu);
    return G.present.count(Id(k)) || G.pending.count(Id(k));
}

bool Peek(const Key& k, Key* full, uint32_t* stage) {
    if (!G.enabled) return false;
    const int fd = open(PathOf(k).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    Header h;
    const bool ok = read(fd, &h, sizeof(h)) == (ssize_t)sizeof(h);
    close(fd);
    if (!ok || memcmp(h.magic, kMagic, sizeof(kMagic)) != 0 || h.version != kFormatVersion || h.h1 != k.h1 ||
        h.h2 != k.h2)
        return false;
    full->h1 = h.h1;
    full->h2 = h.h2;
    full->len = h.msl_len;
    *stage = h.stage;
    return true;
}

id<MTLFunction> Load(const Key& k, uint32_t stage, std::string* why) {
    if (!G.enabled) return nil;
    {
        std::lock_guard<std::mutex> lk(G.mu);
        if (!G.present.count(Id(k))) {
            if (why) *why = "no entry";
            return nil;
        }
    }
    const double t0 = CFAbsoluteTimeGetCurrent();
    std::vector<uint8_t> data;
    const uint8_t* payload = nullptr;
    size_t payload_n = 0;
    std::string fail;
    id<MTLFunction> fn = nil;
    if (!ReadAll(PathOf(k), &data, kMaxFileBytes)) {
        fail = "unreadable, empty or over the size limit";
    } else if (Status s = Decode(data.data(), data.size(), k, stage, &payload, &payload_n); s != Status::kOk) {
        fail = StatusName(s);
    } else {
        @autoreleasepool {
            dispatch_data_t dd = dispatch_data_create(payload, payload_n, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            NSError* e = nil;
            id<MTLLibrary> lib = [G.dev newLibraryWithData:dd error:&e];
            if (!lib) {
                fail = std::string("library: ") + (e ? e.localizedDescription.UTF8String : "unknown error");
            } else {
                for (NSString* name in lib.functionNames) {
                    id<MTLFunction> f = [lib newFunctionWithName:name];
                    if (!f || (uint32_t)f.functionType != stage) continue;
                    if (fn) {
                        fail = "more than one function of the stage";
                        fn = nil;
                        break;
                    }
                    fn = f;
                }
                if (!fn && fail.empty()) fail = "no function of the stage";
            }
        }
    }
    G.load_us += (uint64_t)((CFAbsoluteTimeGetCurrent() - t0) * 1e6);
    if (!fn) {
        G.load_fail++;
        fprintf(stderr, "[gnm-lib-store] entry %s unusable (%s): deleted\n", FileName(k).c_str(), fail.c_str());
        // only the caller that removes the entry counts it (another thread may have reported or
        // refused it meanwhile)
        bool was_present;
        {
            std::lock_guard<std::mutex> lk(G.mu);
            was_present = G.present.erase(Id(k)) != 0;
        }
        unlink(PathOf(k).c_str());
        const Key* kp = &k;
        if (was_present) CountFailure(&kp, 1);
        if (why) *why = fail;
        return nil;
    }
    G.loads++;
    MarkUsed(k);
    return fn;
}

bool ReportPipelineFailure(const Key* a, const Key* b) {
    if (G.dir.empty()) return false;
    const Key* keys[2] = {a, b};
    for (const Key* k : keys) {
        if (!k) continue;
        fprintf(stderr, "[gnm-lib-store] a pipeline could not be built from the stored library %s: deleted, compiling the MSL\n",
                FileName(*k).c_str());
        Forget(*k, true);
    }
    return CountFailure(keys, 2);
}

uint32_t Failures() {
    return G.failures.load();
}

void StoreRender(const Key* vs, const Key* ps, MTLRenderPipelineDescriptor* d) {
    if (!G.enabled || !d) return;
    std::vector<Item> items;
    if (vs) items.push_back({*vs, kVertex});
    if (ps) items.push_back({*ps, kFragment});
    if (!Claim(items)) return;
    // The copy keeps the descriptor's binary archives: they are only searched (Metal can take
    // the pipeline's GPU code from them instead of compiling it again), never copied whole.
    MTLRenderPipelineDescriptor* c = [d copy];
    dispatch_async(G.q, ^{
        WriteEntries(items, ^BOOL(id<MTLBinaryArchive> a, NSError** e) {
            return [a addRenderPipelineFunctionsWithDescriptor:c error:e];
        });
    });
}

void StoreCompute(const Key& cs, MTLComputePipelineDescriptor* d) {
    if (!G.enabled || !d) return;
    std::vector<Item> items{{cs, kKernel}};
    if (!Claim(items)) return;
    MTLComputePipelineDescriptor* c = [d copy];
    dispatch_async(G.q, ^{
        WriteEntries(items, ^BOOL(id<MTLBinaryArchive> a, NSError** e) {
            return [a addComputePipelineFunctionsWithDescriptor:c error:e];
        });
    });
}

bool Flush(double timeout_s) {
    if (!G.q) return true;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_block_t b = dispatch_block_create_with_qos_class(DISPATCH_BLOCK_ENFORCE_QOS_CLASS, QOS_CLASS_USER_INITIATED,
                                                              0, ^{
                                                                  dispatch_semaphore_signal(done);
                                                              });
    dispatch_async(G.q, b);
    return dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_s * NSEC_PER_SEC))) == 0;
}

void PrintStats() {
    if (!G.q) return;
    size_t present;
    uint64_t bytes;
    {
        std::lock_guard<std::mutex> lk(G.mu);
        present = G.present.size();
        bytes = G.bytes;
    }
    fprintf(stderr,
            "[gnm-lib-store] session: %llu loaded (%.0f ms), %llu unusable; %llu written (%.0f ms in the background), "
            "%llu write failures; %zu entries (%zu at start), %.1f MB; %u failures%s\n",
            (unsigned long long)G.loads.load(), G.load_us.load() / 1000.0, (unsigned long long)G.load_fail.load(),
            (unsigned long long)G.writes.load(), G.write_us.load() / 1000.0, (unsigned long long)G.write_fail.load(),
            present, G.opened, bytes / 1048576.0, G.failures.load(), G.enabled ? "" : " (disabled)");
}

} // namespace ps4gnm_lib_store
