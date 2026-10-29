// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - residency sets of the Metal 4 backend (docs/metal4-backend.md, "Residency").
// Metal 4 makes nothing resident by itself: a no-copy buffer in no set has its GPU writes
// dropped without an error (the research's probe P4). Every allocation the GPU may touch is in
// one of kGnmResidencyClasses sets from its creation (Add) until every seq that may still use it
// has completed after its release (Remove(a, after_seq), then Retire(completed_seq)). The sets
// are added to the queue once; Commit() applies the changes and runs before every queue commit.
// There are kGnmResidencyClasses (3) sets, well under the 32 residency sets a queue takes.
// GnmResidency holds a strong reference to every allocation it holds, so a release the renderer
// forgets shows as a leak (Count), never as a set entry of a freed object. Thread-safe.
// Misuse aborts with a message: Add or Remove before a successful Init, or an Add of an
// allocation already held in another class's set.
#ifndef PS4_GNM_RESIDENCY_H
#define PS4_GNM_RESIDENCY_H

#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ps4_gnm_backend.h"

namespace ps4gnm {

static_assert(kGnmResidencyClasses <= 32, "a queue takes at most 32 residency sets");

class GnmResidency {
public:
    bool Init(id<MTLDevice> dev, std::string* why);
    id<MTLResidencySet> Set(GnmResidencyClass c) const { return sets_[(int)c]; }
    // Adds a to class c's set (again after a Remove whose seq has not completed: the removal is
    // cancelled). An allocation held in another class's set aborts.
    void Add(id<MTLAllocation> a, GnmResidencyClass c);
    // a leaves its set at the first Retire with completed_seq >= after_seq; a second Remove before
    // that keeps the later of the two seqs. Removals retire in seq order, whatever the order of
    // the calls. Unknown allocations: ignored.
    void Remove(id<MTLAllocation> a, uint64_t after_seq);
    void Retire(uint64_t completed_seq);
    void Commit(); // [set commit] for every changed set
    // In a set (held or waiting for its removal): a itself, or for a texture view its parent
    // texture, for a texture over a buffer that buffer.
    bool Contains(id<MTLAllocation> a) const;
    // Resident for the whole of seq: Contains, and not waiting for a removal due before seq ends
    // (removed after seq or later: a removal after seq N may retire while N + 1 runs).
    bool ResidentFor(id<MTLAllocation> a, uint64_t seq) const;
    size_t Count() const;        // allocations in the sets (held + waiting for removal)
    size_t Count(GnmResidencyClass c) const; // of those, class c's (walks them)
    size_t PendingCount() const; // waiting for removal
    uint64_t AllocatedSize() const;

private:
    struct Entry {
        id<MTLAllocation> a;
        uint8_t cls;
        uint64_t remove_after; // 0: held; else the seq its removal waits for
    };
    using Pending = std::pair<uint64_t, const void*>; // (after_seq, allocation)
    mutable std::mutex mu_;
    bool inited_ = false;
    id<MTLResidencySet> sets_[kGnmResidencyClasses] = {};
    bool dirty_[kGnmResidencyClasses] = {};
    std::unordered_map<const void*, Entry> entries_;
    // removals by seq (a min-heap: the smallest after_seq on top)
    std::priority_queue<Pending, std::vector<Pending>, std::greater<Pending>> pending_;
};

} // namespace ps4gnm

#endif // PS4_GNM_RESIDENCY_H
