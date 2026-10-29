// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - residency sets of the Metal 4 backend (ps4_gnm_residency.h).
#include "ps4_gnm_residency.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>

namespace ps4gnm {

static const char* const kSetNames[kGnmResidencyClasses] = {"gnm guest memory", "gnm textures", "gnm built-ins"};

[[noreturn]] static void Fatal(const char* what, id<MTLAllocation> a) {
    fprintf(stderr, "[gnm-draw] FATAL: GnmResidency: %s (allocation %p '%s')\n", what, (__bridge const void*)a,
            [(id)a respondsToSelector:@selector(label)] ? [[(id)a label] UTF8String] : "");
    abort();
}

bool GnmResidency::Init(id<MTLDevice> dev, std::string* why) {
    for (int i = 0; i < kGnmResidencyClasses; ++i) {
        MTLResidencySetDescriptor* d = [MTLResidencySetDescriptor new];
        d.label = [NSString stringWithUTF8String:kSetNames[i]];
        d.initialCapacity = 256;
        NSError* err = nil;
        sets_[i] = [dev newResidencySetWithDescriptor:d error:&err];
        if (!sets_[i]) {
            if (why) *why = err ? err.localizedDescription.UTF8String : "newResidencySetWithDescriptor failed";
            return false;
        }
    }
    std::lock_guard<std::mutex> lk(mu_);
    inited_ = true;
    return true;
}

void GnmResidency::Add(id<MTLAllocation> a, GnmResidencyClass c) {
    if (!a) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (!inited_) Fatal("Add before a successful Init", a);
    const void* k = (__bridge const void*)a;
    auto it = entries_.find(k);
    if (it != entries_.end()) {
        if (it->second.cls != (uint8_t)c) Fatal("Add with another class than the set that holds it", a);
        it->second.remove_after = 0; // a pending removal is cancelled (its pending_ record is skipped)
        return;
    }
    entries_.emplace(k, Entry{a, (uint8_t)c, 0});
    [sets_[(int)c] addAllocation:a];
    dirty_[(int)c] = true;
}

void GnmResidency::Remove(id<MTLAllocation> a, uint64_t after_seq) {
    if (!a) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (!inited_) Fatal("Remove before a successful Init", a);
    auto it = entries_.find((__bridge const void*)a);
    if (it == entries_.end()) return;
    // the later seq of two removals wins: a use the earlier one did not cover may be in flight
    it->second.remove_after = std::max<uint64_t>({after_seq, it->second.remove_after, 1});
    pending_.emplace(it->second.remove_after, it->first);
}

void GnmResidency::Retire(uint64_t completed_seq) {
    std::lock_guard<std::mutex> lk(mu_);
    while (!pending_.empty() && pending_.top().first <= completed_seq) {
        const Pending p = pending_.top();
        pending_.pop();
        auto it = entries_.find(p.second);
        // skipped: re-added (remove_after 0) or removed again later (another record follows)
        if (it != entries_.end() && it->second.remove_after == p.first) {
            [sets_[it->second.cls] removeAllocation:it->second.a];
            dirty_[it->second.cls] = true;
            entries_.erase(it);
        }
    }
}

void GnmResidency::Commit() {
    std::lock_guard<std::mutex> lk(mu_);
    for (int i = 0; i < kGnmResidencyClasses; ++i)
        if (dirty_[i]) {
            [sets_[i] commit];
            dirty_[i] = false;
        }
}

// The allocation that makes a resident: the root of a texture view's parents (a view of a view has
// the inner view as its parentTexture, and only the root is in a set), or a linear texture's buffer
// (nil: a is its own allocation).
static id<MTLAllocation> BaseOf(id<MTLAllocation> a) {
    if (![(id)a conformsToProtocol:@protocol(MTLTexture)]) return nil;
    id<MTLTexture> t = (id<MTLTexture>)a;
    id<MTLTexture> root = t;
    while (root.parentTexture) root = root.parentTexture;
    if (root.buffer) return root.buffer;
    return root != t ? root : nil;
}

bool GnmResidency::Contains(id<MTLAllocation> a) const { return a && ResidentFor(a, 0); }

bool GnmResidency::ResidentFor(id<MTLAllocation> a, uint64_t seq) const {
    if (!a) return false;
    const id<MTLAllocation> base = BaseOf(a);
    std::lock_guard<std::mutex> lk(mu_);
    // seq 0: any entry, held or waiting for its removal
    auto held = [&](id<MTLAllocation> x) {
        auto it = entries_.find((__bridge const void*)x);
        return it != entries_.end() && (it->second.remove_after == 0 || it->second.remove_after >= seq);
    };
    return held(a) || (base && held(base)); // a itself (also a view made resident on its own), or its base
}

size_t GnmResidency::Count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return entries_.size();
}

size_t GnmResidency::Count(GnmResidencyClass c) const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t n = 0;
    for (auto& [k, e] : entries_) n += e.cls == (uint8_t)c;
    return n;
}

size_t GnmResidency::PendingCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t n = 0;
    for (auto& [k, e] : entries_) n += e.remove_after != 0;
    return n;
}

uint64_t GnmResidency::AllocatedSize() const {
    std::lock_guard<std::mutex> lk(mu_);
    uint64_t n = 0;
    for (int i = 0; i < kGnmResidencyClasses; ++i) n += sets_[i].allocatedSize;
    return n;
}

} // namespace ps4gnm
