// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the guest memory ranges written by GPU work that has not completed, for the
// renderer's hazard checks (OverlapsPendingWrite in ps4_gnm_draw.mm: a sampled texture written
// by pending work is copied behind it on the GPU, or the GPU is drained).
// The renderer used to scan, per check (about 6200 per TLOU frame), the write list of the open
// command buffer and of every command buffer in flight: about 400 records per check on Metal 4,
// whose seqs complete later than Metal 3's. Here each distinct range is kept once, with its latest
// write: the epoch of that write and the seq it was committed in (kOpen while it is in the open
// command buffer). A range is pending while its latest write is (the latest write is the last to
// complete), and the latest epoch among the overlapping pending writes is the latest write's: the
// answers are those of the scan. Most pending ranges are distinct (guest memory granules), so
// a query does not walk them all: the pending entries are also listed per 1 MiB of guest address
// (kBucketShift), and a query looks at the buckets it covers; one covering 65 or more buckets
// (kWideBuckets + 1) walks the pending entries instead, and an entry covering 65 or more is in every
// query's list (wide_).
// Completed entries stay for reuse (no allocation in steady state). Not thread-safe (the encoder
// thread's).
// Two command buffers can be open at once (the main one and the asynchronous compute one, kTargets):
// a write belongs to the open command buffer of its target until that one is committed. A range
// written by both while both are open keeps its latest write only (a race between the GNM queues on
// GCN as well); the renderer's per-command-buffer write lists still invalidate at each completion.
#ifndef PS4_GNM_PENDING_WRITES_H
#define PS4_GNM_PENDING_WRITES_H

#include <stdint.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace ps4gnm {

class GnmPendingWrites {
public:
    static constexpr uint64_t kOpen = UINT64_MAX; // the seq of a write in an open command buffer
    static constexpr size_t kKeepIdle = 4096;      // the default keep_idle (below)
    static constexpr int kTargets = 2;             // open command buffers: 0 main, 1 asynchronous compute

    struct Range {
        uint64_t addr = 0, size = 0, epoch = 0;
    };
    // keep_idle: completed entries kept for reuse beyond 4x the pending ones (lower in the test, to
    // run Compact)
    explicit GnmPendingWrites(size_t keep_idle = kKeepIdle) : keep_idle_(keep_idle) {}

    // [addr, addr + size) written by target tgt's open command buffer, as write number `epoch`
    // (increasing over all targets).
    void Note(uint64_t addr, uint64_t size, uint64_t epoch, int tgt = 0) {
        if (!size) return;
        const Key k{addr, size};
        auto it = idx_.find(k);
        uint32_t i;
        if (it == idx_.end()) {
            i = (uint32_t)e_.size();
            idx_.emplace(k, i);
            e_.push_back({addr, size, epoch, 0, 0, false}); // not open yet: joins open_ below
        } else {
            i = it->second;
        }
        Entry& e = e_[i];
        if (e.seq != kOpen || e.tgt != tgt) open_[tgt].push_back(i);
        if (!e.live) {
            live_.push_back(i);
            AddToBuckets(i);
        }
        e.epoch = epoch;
        e.seq = kOpen;
        e.tgt = (uint8_t)tgt;
        e.live = true;
    }
    // Target tgt's open command buffer was committed as `seq`.
    void Commit(uint64_t seq, int tgt = 0) {
        for (uint32_t i : open_[tgt])
            if (e_[i].seq == kOpen && e_[i].tgt == tgt) e_[i].seq = seq; // (a later write of another target took it)
        open_[tgt].clear();
    }
    // Every seq up to `done` completed: their writes are no longer pending. Walks the pending
    // ranges once per new `done`. A completed range keeps its entry (the next frame writes it
    // again: no allocation); entries left unwritten for long are dropped once they outnumber the
    // pending ones by far.
    void Prune(uint64_t done) {
        if (done <= done_) return;
        done_ = done;
        size_t n = 0;
        for (uint32_t i : live_) {
            Entry& e = e_[i];
            if (e.seq != kOpen && e.seq <= done) {
                e.live = false;
                continue;
            }
            live_[n++] = i;
        }
        const bool dropped = n != live_.size();
        live_.resize(n);
        if (e_.size() > 4 * live_.size() + keep_idle_) Compact();
        else if (dropped) RebuildBuckets();
    }
    bool Empty() const { return live_.empty(); }
    size_t Size() const { return live_.size(); }
    size_t Entries() const { return e_.size(); } // pending and kept for reuse
    uint64_t Compactions() const { return compactions_; }

    // True if a pending write overlaps [addr, addr + size); *hit (optional): the overlapping one
    // with the latest epoch.
    bool Overlaps(uint64_t addr, uint64_t size, Range* hit = nullptr) const {
        bool any = false;
        auto check = [&](uint32_t i) {
            const Entry& r = e_[i];
            if (r.addr < addr + size && addr < r.addr + r.size) {
                if (hit && (!any || r.epoch > hit->epoch)) *hit = Range{r.addr, r.size, r.epoch};
                any = true;
            }
        };
        const uint64_t p0 = addr >> kBucketShift, p1 = (addr + (size ? size - 1 : 0)) >> kBucketShift;
        if (p1 - p0 >= kWideBuckets) {
            for (uint32_t i : live_) check(i);
            return any;
        }
        if (++gen_ == 0) { // the stamps wrapped
            std::fill(stamp_.begin(), stamp_.end(), 0u);
            gen_ = 1;
        }
        for (uint32_t i : wide_) check(i);
        for (uint64_t p = p0; p <= p1; ++p) {
            auto it = buckets_.find(p);
            if (it == buckets_.end()) continue;
            for (uint32_t i : it->second) {
                if (stamp_[i] == gen_) continue; // in an earlier bucket of this query
                stamp_[i] = gen_;
                check(i);
            }
        }
        return any;
    }

private:
    static constexpr int kBucketShift = 20;      // buckets of 1 MiB of guest address
    static constexpr uint64_t kWideBuckets = 64; // an entry or query covering 65 or more: wide_ / a walk
    void AddToBuckets(uint32_t i) {
        if (stamp_.size() < e_.size()) stamp_.resize(e_.size(), 0u);
        const Entry& e = e_[i];
        const uint64_t p0 = e.addr >> kBucketShift, p1 = (e.addr + e.size - 1) >> kBucketShift;
        if (p1 - p0 >= kWideBuckets) {
            wide_.push_back(i);
            return;
        }
        for (uint64_t p = p0; p <= p1; ++p) buckets_[p].push_back(i);
    }
    // the buckets list exactly the pending entries (after Prune dropped some; the vectors keep
    // their capacity)
    void RebuildBuckets() {
        for (auto& [p, v] : buckets_) v.clear();
        wide_.clear();
        for (uint32_t i : live_) AddToBuckets(i);
    }
    struct Entry {
        uint64_t addr, size, epoch, seq;
        uint8_t tgt; // the target of its latest write
        bool live;   // in live_: its latest write is pending
    };
    struct Key {
        uint64_t addr, size;
        bool operator==(const Key& o) const { return addr == o.addr && size == o.size; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            uint64_t v = k.addr * 0x9e3779b97f4a7c15ull ^ (k.size + (k.size << 17));
            return (size_t)(v ^ (v >> 31));
        }
    };
    // keeps the pending entries only (rare: after many ranges were written once and not again)
    void Compact() {
        compactions_++;
        std::vector<Entry> keep;
        keep.reserve(live_.size());
        for (uint32_t& i : live_) {
            keep.push_back(e_[i]);
            i = (uint32_t)keep.size() - 1;
        }
        e_.swap(keep);
        idx_.clear();
        for (auto& o : open_) o.clear();
        for (uint32_t i = 0; i < (uint32_t)e_.size(); ++i) {
            idx_.emplace(Key{e_[i].addr, e_[i].size}, i);
            if (e_[i].seq == kOpen) open_[e_[i].tgt].push_back(i);
        }
        stamp_.assign(e_.size(), 0u);
        gen_ = 0;
        RebuildBuckets();
    }
    std::vector<Entry> e_;
    std::unordered_map<Key, uint32_t, KeyHash> idx_;
    std::vector<uint32_t> live_; // pending entries
    std::vector<uint32_t> open_[kTargets]; // per target: entries whose latest write is in its open command buffer
    std::unordered_map<uint64_t, std::vector<uint32_t>> buckets_; // pending entries by 1 MiB
    std::vector<uint32_t> wide_;                                  // pending entries covering 65+ buckets
    mutable std::vector<uint32_t> stamp_;                         // per entry: the query that saw it
    mutable uint32_t gen_ = 0;
    uint64_t done_ = 0;          // the completed seq Prune last saw
    size_t keep_idle_;
    uint64_t compactions_ = 0;
};

} // namespace ps4gnm

#endif // PS4_GNM_PENDING_WRITES_H
