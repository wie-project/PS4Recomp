#include "ps4_gnm_pipeline_archive.h"
#include "ps4_gnm_library_store.h"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace ps4gnm_pso_archive {
namespace {
// Metal fails to serialize an archive that was loaded from a file and then got pipelines added
// ("expecting 'fragment' stage in pipeline no. N", seen with the game's pipelines), while a new
// archive always serializes. So archive files are never added to after they are written: each
// save writes the pipelines added since the previous save into a new file, pipelines-<n>.metalar,
// and further additions go into a new archive. That also keeps the save at quit short (it only
// writes the additions since the last save; at most kSaveBatch of them).
struct FileInfo {
    uint64_t bytes = 0;
    std::vector<uint64_t> keys; // from the index; empty if the index has no record of the file
};

struct ArchiveState {
    id<MTLDevice> dev = nil;
    std::atomic<bool> enabled{false};           // Open succeeded (lookups and saves work)
    std::atomic<bool> adding{false};            // new pipelines are accepted (false once over budget)
    bool lookup_only = false;                   // another process owns the directory; set by Open
    int lock_fd = -1;                           // <dir>/pipelines.lock, held for the life of the process
    id<MTLBinaryArchive> archive = nil;         // additions since the last save (written to `path`); q only
    NSArray<id<MTLBinaryArchive>>* list = nil; // all archives (files read and written, then `archive`); mu
    std::string dir, path, index_path;          // path: q only
    int next_file = 0;                          // q only
    uint64_t budget = 0;
    uint64_t file_bytes = 0;           // summed size of the archive files; q only after Open
    bool backoff = false;              // the last save failed: batch saves wait for a timer or quit save; q only
    dispatch_queue_t q = nil;          // additions and saves, in order
    dispatch_source_t timer = nil;     // periodic save
    std::mutex mu;                     // keys, unsaved, list
    std::unordered_set<uint64_t> keys; // in the archive or queued for it
    std::vector<uint64_t> unsaved;     // added since the last save (written to the index then)
    bool dirty = false;                // q only
};
ArchiveState A;

// Index file: header, then one record per saved file.
// Header (bytes): magic 0..7, version 8..11, reserved 12..15, kern.osversion 16..79, device name
// 80..207, registryID 208..215; the archive is valid only for the OS build and GPU it was written
// on. Record: uint32 file number, uint32 key count, then the keys (8 bytes each).
struct IndexHeader {
    char magic[8];
    uint32_t version;
    uint32_t reserved;
    char os[64];
    char device[128];
    uint64_t registry_id;
};
static_assert(sizeof(IndexHeader) == 216, "index header layout");
struct RecordHeader {
    uint32_t file_no;
    uint32_t count;
};
static_assert(sizeof(RecordHeader) == 8, "index record layout");
constexpr uint32_t kIndexVersion = 3; // 1: a single pipelines.metalar; 2: keys without file numbers
// At most this many archive files. Measured on the M1 Pro with 16..256 attached archives of 8
// pipelines each: a pipeline found in an archive builds in 0.05-0.09 ms, a miss costs 0.1-0.2 ms
// (both grow slowly with the count), against about 0.3 ms for a build from the system cache. At
// 64 additions per file, 128 files hold about 8000 pipelines. Over the cap (or the size budget)
// the oldest files are deleted.
constexpr int kMaxFiles = 128;
// A save starts as soon as this many pipelines were added since the last one.
constexpr size_t kSaveBatch = 64;
// The periodic save skips smaller additions (the next batch or the quit save takes them), so
// files are not created for one or two pipelines.
constexpr size_t kTimerMin = 16;
// A record claiming more keys than this means the index is corrupt.
constexpr uint32_t kMaxRecordKeys = 1u << 20;

IndexHeader CurrentHeader(id<MTLDevice> dev) {
    IndexHeader h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "PS4PSOAR", 8);
    h.version = kIndexVersion;
    size_t n = sizeof(h.os) - 1;
    if (sysctlbyname("kern.osversion", h.os, &n, nullptr, 0) != 0) h.os[0] = 0;
    strncpy(h.device, dev.name.UTF8String ? dev.name.UTF8String : "", sizeof(h.device) - 1);
    h.registry_id = dev.registryID;
    return h;
}

int MaxFiles() {
    // for tests/host: a small cap
    if (const char* s = getenv("PS4_GNM_PSO_ARCHIVE_MAX_FILES")) {
        const int n = atoi(s);
        if (n > 0) return n;
    }
    return kMaxFiles;
}

// Number n of a pipelines-<n>.metalar file name, or -1.
int FileNumber(const char* name) {
    int n = -1, end = 0;
    if (sscanf(name, "pipelines-%d.metalar%n", &n, &end) == 1 && name[end] == 0 && n >= 0) return n;
    return -1;
}

// Deletes temporary files left by a save that was cut short (quit timeout), unless the process
// that writes them (pid in the name) still runs.
void RemoveStaleTemps(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* de = readdir(d)) {
        if (strncmp(de->d_name, "pipelines", 9) != 0) continue;
        const char* t = strstr(de->d_name, ".tmp");
        if (!t) continue;
        const long pid = strtol(t + 4, nullptr, 10);
        if (pid > 0 && pid != getpid() && !(kill((pid_t)pid, 0) == -1 && errno == ESRCH)) continue;
        unlink((dir + "/" + de->d_name).c_str());
    }
    closedir(d);
}

// Numbers of the pipelines-<n>.metalar files, sorted.
std::vector<int> ListFiles(const std::string& dir) {
    std::vector<int> v;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* de = readdir(d))
            if (const int n = FileNumber(de->d_name); n >= 0) v.push_back(n);
        closedir(d);
    }
    std::sort(v.begin(), v.end());
    return v;
}

std::string FilePath(int n) {
    return A.dir + "/pipelines-" + std::to_string(n) + ".metalar";
}

id<MTLBinaryArchive> NewArchive(id<MTLDevice> dev, NSURL* url, NSError** err) {
    MTLBinaryArchiveDescriptor* ad = [MTLBinaryArchiveDescriptor new];
    ad.url = url; // nil: a new, empty archive
    return [dev newBinaryArchiveWithDescriptor:ad error:err];
}

// Deletes every archive file (including the single file of index version 1) and the index.
void DropFiles() {
    for (int n : ListFiles(A.dir)) unlink(FilePath(n).c_str());
    unlink((A.dir + "/pipelines.metalar").c_str());
    unlink(A.index_path.c_str());
}

// Reads the index. Returns false if it is missing, has another OS/GPU header or is corrupt;
// otherwise fills records (file number -> keys). A trailing partial record is ignored.
bool LoadIndex(const IndexHeader& cur, std::map<int, std::vector<uint64_t>>& records) {
    FILE* f = fopen(A.index_path.c_str(), "rb");
    if (!f) return false;
    IndexHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(&h, &cur, sizeof(h)) != 0) {
        fclose(f);
        return false;
    }
    RecordHeader r;
    bool ok = true;
    while (fread(&r, sizeof(r), 1, f) == 1) {
        if (r.count > kMaxRecordKeys || r.file_no > (uint32_t)INT32_MAX) {
            ok = false;
            break;
        }
        std::vector<uint64_t> k(r.count);
        if (r.count && fread(k.data(), sizeof(uint64_t), r.count, f) != r.count) break;
        std::vector<uint64_t>& dst = records[(int)r.file_no];
        dst.insert(dst.end(), k.begin(), k.end());
    }
    fclose(f);
    return ok;
}

bool WriteAll(int fd, const void* p, size_t n) {
    const char* c = (const char*)p;
    while (n) {
        const ssize_t w = write(fd, c, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        c += w;
        n -= (size_t)w;
    }
    return true;
}

bool WriteRecord(int fd, int file_no, const std::vector<uint64_t>& keys) {
    const RecordHeader r = {(uint32_t)file_no, (uint32_t)keys.size()};
    return WriteAll(fd, &r, sizeof(r)) && WriteAll(fd, keys.data(), keys.size() * sizeof(uint64_t));
}

// Writes the whole index (temporary file + rename).
bool RewriteIndex(const IndexHeader& h, const std::map<int, FileInfo>& files) {
    const std::string tmp = A.index_path + ".tmp" + std::to_string((int)getpid());
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    bool ok = WriteAll(fd, &h, sizeof(h));
    for (const auto& [n, fi] : files)
        if (ok && !fi.keys.empty()) ok = WriteRecord(fd, n, fi.keys);
    ok = close(fd) == 0 && ok;
    if (ok) ok = rename(tmp.c_str(), A.index_path.c_str()) == 0;
    if (!ok) unlink(tmp.c_str());
    return ok;
}

// Appends one record; after a partial write the index is cut back to its previous size.
bool AppendRecord(int file_no, const std::vector<uint64_t>& keys) {
    const int fd = open(A.index_path.c_str(), O_WRONLY | O_APPEND);
    if (fd < 0) return false;
    struct stat st;
    bool ok = fstat(fd, &st) == 0;
    if (ok && !WriteRecord(fd, file_no, keys)) {
        if (ftruncate(fd, st.st_size) != 0)
            fprintf(stderr, "[gnm-pso-archive] cannot cut %s back (errno %d)\n", A.index_path.c_str(), errno);
        ok = false;
    }
    ok = close(fd) == 0 && ok;
    return ok;
}

// Starts a new in-memory archive for later additions (the old one stays in the list for lookups).
void StartNewArchive() {
    NSError* e = nil;
    id<MTLBinaryArchive> next = NewArchive(A.dev, nil, &e);
    if (!next) {
        fprintf(stderr, "[gnm-pso-archive] no new archive (%s): later additions are not saved\n",
                e.localizedDescription.UTF8String);
        A.adding = false;
        return;
    }
    A.archive = next;
    std::lock_guard<std::mutex> lk(A.mu);
    A.list = [A.list arrayByAddingObject:next];
}

enum class SaveKind { Batch, Timer, Now };

// Runs on A.q.
bool SaveOnQueue(SaveKind kind) {
    if (!A.dirty) return true;
    if (kind == SaveKind::Batch && A.backoff) return true;
    if (kind == SaveKind::Timer && A.unsaved.size() < kTimerMin) return true;
    NSString* tmp = [NSString stringWithFormat:@"%s.tmp%d", A.path.c_str(), (int)getpid()];
    NSError* e = nil;
    if (![A.archive serializeToURL:[NSURL fileURLWithPath:tmp] error:&e]) {
        // Retrying the same archive would fail again and lose every later addition, so its
        // pipelines are given up (archived again in a later session) and a new archive started.
        fprintf(stderr, "[gnm-pso-archive] save failed, %zu pipelines dropped: %s\n", A.unsaved.size(),
                e.localizedDescription.UTF8String);
        unlink(tmp.UTF8String);
        {
            std::lock_guard<std::mutex> lk(A.mu);
            for (uint64_t k : A.unsaved) A.keys.erase(k);
            A.unsaved.clear();
        }
        A.dirty = false;
        A.backoff = true;
        StartNewArchive();
        return false;
    }
    if (rename(tmp.UTF8String, A.path.c_str()) != 0) {
        fprintf(stderr, "[gnm-pso-archive] rename to %s failed (errno %d)\n", A.path.c_str(), errno);
        unlink(tmp.UTF8String);
        A.backoff = true;
        return false;
    }
    A.backoff = false;
    std::vector<uint64_t> saved;
    {
        std::lock_guard<std::mutex> lk(A.mu);
        saved.swap(A.unsaved);
    }
    A.dirty = false;
    struct stat st;
    if (stat(A.path.c_str(), &st) == 0) A.file_bytes += (uint64_t)st.st_size;
    bool ok = true;
    if (!AppendRecord(A.next_file - 1, saved)) {
        // The file holds these pipelines but the index does not list them: a later session does
        // not know they are archived and adds them again (into a new file; harmless).
        fprintf(stderr, "[gnm-pso-archive] writing %s failed (errno %d)\n", A.index_path.c_str(), errno);
        ok = false;
    } else {
        fprintf(stderr, "[gnm-pso-archive] saved %s (%zu new pipelines)\n", A.path.c_str(), saved.size());
    }
    A.path = FilePath(A.next_file++);
    if (A.file_bytes > A.budget) {
        fprintf(stderr, "[gnm-pso-archive] archive files reached %llu MB (budget %llu MB): no more additions this session\n",
                (unsigned long long)(A.file_bytes >> 20), (unsigned long long)(A.budget >> 20));
        A.adding = false;
    }
    if (A.adding) StartNewArchive();
    return ok;
}

template <class Desc, class AddFn>
void AddImpl(uint64_t key, Desc* d, AddFn add) {
    if (!A.adding || !d) return;
    {
        std::lock_guard<std::mutex> lk(A.mu);
        if (!A.keys.insert(key).second) return;
    }
    Desc* copy = [d copy];
    dispatch_async(A.q, ^{
        NSError* e = nil;
        if (!A.adding) {
            std::lock_guard<std::mutex> lk(A.mu);
            A.keys.erase(key);
            return;
        }
        if (add(copy, &e)) {
            bool full;
            {
                std::lock_guard<std::mutex> lk(A.mu);
                A.unsaved.push_back(key);
                full = A.unsaved.size() >= kSaveBatch;
            }
            A.dirty = true;
            if (full) SaveOnQueue(SaveKind::Batch);
        } else {
            fprintf(stderr, "[gnm-pso-archive] add failed: %s\n", e.localizedDescription.UTF8String);
            std::lock_guard<std::mutex> lk(A.mu);
            A.keys.erase(key);
        }
    });
}

NSArray<id<MTLBinaryArchive>>* List() {
    std::lock_guard<std::mutex> lk(A.mu);
    return A.list;
}
} // namespace

namespace {
// Takes <dir>/pipelines.lock (flock, never released: the fd stays open until the process ends,
// and the kernel drops the lock with it, also after a crash). Returns false if another process
// holds it or the lock cannot be taken at all.
bool TakeDirLock(const std::string& dir) {
    const std::string p = dir + "/pipelines.lock";
    const int fd = open(p.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[gnm-pso-archive] cannot open %s (errno %d)\n", p.c_str(), errno);
        return false;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno != EWOULDBLOCK)
            fprintf(stderr, "[gnm-pso-archive] cannot lock %s (errno %d)\n", p.c_str(), errno);
        close(fd);
        return false;
    }
    A.lock_fd = fd;
    return true;
}

// Another process owns the directory: attach the files its index lists and know their keys, but
// change nothing (no temp cleanup, eviction, index rewrite, additions or saves). The owner may
// rewrite or append to the index meanwhile: the rename is atomic and LoadIndex ignores a partial
// trailing record. A file the owner deletes before it is opened here is skipped.
bool OpenLookupOnly(id<MTLDevice> dev, const std::string& dir) {
    fprintf(stderr, "[gnm-pso-archive] another process owns the archive: lookup only\n");
    std::map<int, std::vector<uint64_t>> records;
    if (!LoadIndex(CurrentHeader(dev), records)) {
        fprintf(stderr, "[gnm-pso-archive] %s is missing, corrupt or not for this OS/GPU: no archive this session\n",
                A.index_path.c_str());
        return false;
    }
    NSMutableArray<id<MTLBinaryArchive>>* list = [NSMutableArray array];
    size_t files = 0;
    for (int n : ListFiles(dir)) {
        NSError* e = nil;
        id<MTLBinaryArchive> a = NewArchive(dev, [NSURL fileURLWithPath:@(FilePath(n).c_str())], &e);
        if (!a) continue;
        [list addObject:a];
        ++files;
        if (auto it = records.find(n); it != records.end()) A.keys.insert(it->second.begin(), it->second.end());
    }
    A.dev = dev;
    A.list = list;
    A.lookup_only = true;
    A.enabled = true;
    A.adding = false;
    fprintf(stderr, "[gnm-pso-archive] %s: %zu pipelines in %zu files (lookup only)\n", dir.c_str(), A.keys.size(),
            files);
    return true;
}
} // namespace

bool Open(id<MTLDevice> dev, const std::string& dir, uint64_t budget_bytes) {
    const char* off = getenv("PS4_GNM_NO_PSO_ARCHIVE");
    if (off && strcmp(off, "1") == 0) return false;
    A.dir = dir;
    A.index_path = dir + "/pipelines_archived.bin";
    A.budget = budget_bytes;
    // Two processes may share the directory (the owner's game and a benchmark): only the one
    // holding the lock changes files; the other only looks pipelines up.
    if (!TakeDirLock(dir)) return OpenLookupOnly(dev, dir);
    A.q = dispatch_queue_create("ps4.gnm.pso_archive",
                                dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
    RemoveStaleTemps(dir);
    const IndexHeader cur = CurrentHeader(dev);
    std::map<int, std::vector<uint64_t>> records;
    std::map<int, FileInfo> files;
    struct stat st;
    if (!LoadIndex(cur, records)) {
        if (!ListFiles(dir).empty() || stat(A.index_path.c_str(), &st) == 0)
            fprintf(stderr, "[gnm-pso-archive] %s is missing, corrupt or not for this OS/GPU: starting a new archive\n",
                    A.index_path.c_str());
        DropFiles();
    } else {
        // A file without a record is kept (its pipelines are only looked up, not known); a record
        // without a file is forgotten (those pipelines are archived again).
        for (int n : ListFiles(dir)) {
            if (stat(FilePath(n).c_str(), &st) != 0) continue;
            FileInfo& fi = files[n];
            fi.bytes = (uint64_t)st.st_size;
            if (auto it = records.find(n); it != records.end()) fi.keys = std::move(it->second);
        }
    }
    // Over the file cap or the size budget: delete the oldest files down to 3/4 of the limits.
    const int cap = MaxFiles();
    uint64_t total = 0;
    for (const auto& [n, fi] : files) total += fi.bytes;
    if ((int)files.size() > cap || total > budget_bytes) {
        const size_t keep_files = (size_t)std::max(1, cap * 3 / 4);
        const uint64_t keep_bytes = budget_bytes / 4 * 3;
        size_t dropped = 0;
        while (!files.empty() && (files.size() > keep_files || total > keep_bytes)) {
            auto it = files.begin();
            unlink(FilePath(it->first).c_str());
            total -= it->second.bytes;
            files.erase(it);
            ++dropped;
        }
        fprintf(stderr, "[gnm-pso-archive] over %d files or %llu MB: deleted the %zu oldest files\n", cap,
                (unsigned long long)(budget_bytes >> 20), dropped);
    }
    // Open the files; an unreadable one is deleted with its keys.
    NSError* e = nil;
    NSMutableArray<id<MTLBinaryArchive>>* list = [NSMutableArray array];
    for (auto it = files.begin(); it != files.end();) {
        id<MTLBinaryArchive> a = NewArchive(dev, [NSURL fileURLWithPath:@(FilePath(it->first).c_str())], &e);
        if (!a) {
            fprintf(stderr, "[gnm-pso-archive] cannot open %s (%s): deleted\n", FilePath(it->first).c_str(),
                    e.localizedDescription.UTF8String);
            unlink(FilePath(it->first).c_str());
            total -= it->second.bytes;
            it = files.erase(it);
            continue;
        }
        [list addObject:a];
        ++it;
    }
    if (!RewriteIndex(cur, files)) {
        fprintf(stderr, "[gnm-pso-archive] cannot write %s (errno %d): disabled\n", A.index_path.c_str(), errno);
        return false;
    }
    A.keys.clear();
    for (const auto& [n, fi] : files) A.keys.insert(fi.keys.begin(), fi.keys.end());
    A.dev = dev;
    A.archive = NewArchive(dev, nil, &e);
    if (!A.archive) {
        fprintf(stderr, "[gnm-pso-archive] disabled: %s\n", e.localizedDescription.UTF8String);
        return false;
    }
    A.file_bytes = total;
    A.next_file = files.empty() ? 0 : files.rbegin()->first + 1;
    A.path = FilePath(A.next_file++);
    [list addObject:A.archive];
    A.list = list;
    A.enabled = true;
    A.adding = true;
    fprintf(stderr, "[gnm-pso-archive] %s: %zu pipelines in %zu files\n", dir.c_str(), A.keys.size(), files.size());
    return true;
}

bool Enabled() {
    return A.enabled;
}

bool LookupOnly() {
    return A.lookup_only;
}

void Attach(MTLRenderPipelineDescriptor* d) {
    if (NSArray<id<MTLBinaryArchive>>* l = List()) d.binaryArchives = l;
}

void Attach(MTLComputePipelineDescriptor* d) {
    if (NSArray<id<MTLBinaryArchive>>* l = List()) d.binaryArchives = l;
}

bool Contains(uint64_t key) {
    std::lock_guard<std::mutex> lk(A.mu);
    return A.keys.count(key) != 0;
}

void Add(uint64_t key, MTLRenderPipelineDescriptor* d) {
    AddImpl(key, d, ^bool(MTLRenderPipelineDescriptor* c, NSError** e) {
        return [A.archive addRenderPipelineFunctionsWithDescriptor:c error:e];
    });
}

void Add(uint64_t key, MTLComputePipelineDescriptor* d) {
    AddImpl(key, d, ^bool(MTLComputePipelineDescriptor* c, NSError** e) {
        return [A.archive addComputePipelineFunctionsWithDescriptor:c error:e];
    });
}

bool SaveNow(double timeout_s) {
    if (!A.q) return true;
    __block bool ok = false;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    // The queue runs at utility QoS; the quit save has a short budget, so its block (and the
    // queued additions ahead of it, through QoS override) runs at user-initiated QoS.
    dispatch_block_t b = dispatch_block_create_with_qos_class(DISPATCH_BLOCK_ENFORCE_QOS_CLASS, QOS_CLASS_USER_INITIATED,
                                                              0, ^{
                                                                  ok = SaveOnQueue(SaveKind::Now);
                                                                  dispatch_semaphore_signal(done);
                                                              });
    dispatch_async(A.q, b);
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_s * NSEC_PER_SEC))) != 0)
        return false;
    return ok;
}

void StartPeriodicSave(double period_s) {
    if (!A.enabled || A.lookup_only || A.timer) return;
    A.timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, A.q);
    const uint64_t ns = (uint64_t)(period_s * NSEC_PER_SEC);
    dispatch_source_set_timer(A.timer, dispatch_time(DISPATCH_TIME_NOW, (int64_t)ns), ns, NSEC_PER_SEC);
    dispatch_source_set_event_handler(A.timer, ^{
        SaveOnQueue(SaveKind::Timer);
    });
    dispatch_resume(A.timer);
}
} // namespace ps4gnm_pso_archive

extern "C" void ps4_gnm_pso_archive_save_for_quit(double timeout_s) {
    // The pipeline archive first (the harder cache to rebuild), then the library store's queued
    // entries in the time that is left (a missing entry is written again in a later session).
    const double t0 = CFAbsoluteTimeGetCurrent();
    if (ps4gnm_pso_archive::Enabled() && !ps4gnm_pso_archive::SaveNow(timeout_s))
        fprintf(stderr, "[gnm-pso-archive] quit: save failed or did not finish in %.1f s\n", timeout_s);
    const double left = timeout_s - (CFAbsoluteTimeGetCurrent() - t0);
    if (!ps4gnm_lib_store::Flush(left > 0.05 ? left : 0.05))
        fprintf(stderr, "[gnm-lib-store] quit: queued entries not written in time\n");
}
