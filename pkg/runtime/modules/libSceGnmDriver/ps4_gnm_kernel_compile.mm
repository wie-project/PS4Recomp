// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - background compilation of compute kernels (see ps4_gnm_kernel_compile.h).
 */
#include "ps4_gnm_kernel_compile.h"

#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>

namespace ps4gnm_kernels {

id<MTLFunction> Job::Wait(double* waited_ms) {
    if (Done()) {
        if (waited_ms) *waited_ms = 0;
        std::lock_guard<std::mutex> lk(mu_);
        return fn_;
    }
    const double t0 = CFAbsoluteTimeGetCurrent();
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this] { return done_.load(std::memory_order_relaxed); });
    if (waited_ms) *waited_ms = (CFAbsoluteTimeGetCurrent() - t0) * 1000.0;
    return fn_;
}

std::string Job::Error() const {
    std::lock_guard<std::mutex> lk(mu_);
    return err_;
}

id<MTLLibrary> Job::Library() const {
    std::lock_guard<std::mutex> lk(mu_);
    return lib_;
}

void Job::Finish(id<MTLLibrary> lib, id<MTLFunction> fn, const std::string& err, double ms) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        lib_ = lib;
        fn_ = fn;
        err_ = err;
        compile_ms_ = ms;
        done_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

Compiler::Compiler(id<MTLDevice> dev, MTLCompileOptions* (*make_options)(), unsigned max_parallel)
    : dev_(dev), make_options_(make_options),
      queue_(dispatch_queue_create("ps4.gnm.kernel_compile",
                                   dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_CONCURRENT,
                                                                           QOS_CLASS_USER_INTERACTIVE, 0))),
      max_parallel_(max_parallel ? max_parallel : 1) {}

Compiler::~Compiler() {
    std::unique_lock<std::mutex> lk(mu_);
    for (auto& q : queued_) {
        for (Queued& item : q) {
            in_flight_.fetch_sub(1, std::memory_order_relaxed);
            item.job->Finish(nil, nil, "compile queue shut down", 0);
        }
        q.clear();
    }
    idle_.wait(lk, [this] { return running_ == 0; });
}

std::shared_ptr<Job> Compiler::Find(uint64_t mh) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = jobs_.find(mh);
    return it == jobs_.end() ? nullptr : it->second;
}

std::shared_ptr<Job> Compiler::Start(uint64_t mh, const std::string& msl, bool* started, Priority pri,
                                    Done on_done) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& slot = jobs_[mh];
    if (slot) {
        if (started) *started = false;
        if (pri == kDispatch && !slot->Done()) {
            // still queued behind draw shaders: a dispatch waits for it, move it ahead
            auto& low = queued_[kDraw];
            for (auto it = low.begin(); it != low.end(); ++it)
                if (it->job == slot) {
                    queued_[kDispatch].push_back(std::move(*it));
                    low.erase(it);
                    break;
                }
        }
        return slot;
    }
    slot = std::make_shared<Job>(mh);
    if (started) *started = true;
    const Priority p = pri == kDraw ? kDraw : kDispatch;
    started_[p].fetch_add(1, std::memory_order_relaxed);
    in_flight_.fetch_add(1, std::memory_order_relaxed);
    queued_[p].push_back({slot, std::make_shared<std::string>(msl), std::move(on_done), p});
    StartQueued();
    return slot;
}

void Compiler::StartQueued() {
    while (running_ < max_parallel_ && (!queued_[kDispatch].empty() || !queued_[kDraw].empty())) {
        auto& q = queued_[kDispatch].empty() ? queued_[kDraw] : queued_[kDispatch];
        Queued item = std::move(q.front());
        q.pop_front();
        running_++;
        for (uint64_t p = peak_.load(std::memory_order_relaxed);
             running_ > p && !peak_.compare_exchange_weak(p, running_, std::memory_order_relaxed);) {
        }
        auto box = std::make_shared<Queued>(std::move(item));
        dispatch_async(queue_, ^{
            Run(std::move(*box));
        });
    }
}

void Compiler::Run(Queued q) {
    @autoreleasepool {
        NSError* e = nil;
        const double t0 = CFAbsoluteTimeGetCurrent();
        id<MTLLibrary> lib = [dev_ newLibraryWithSource:[NSString stringWithUTF8String:q.msl->c_str()]
                                                options:make_options_()
                                                  error:&e];
        id<MTLFunction> fn = lib ? [lib newFunctionWithName:@"main0"] : nil;
        const double ms = (CFAbsoluteTimeGetCurrent() - t0) * 1000.0;
        std::string err;
        if (!fn) {
            err = e ? e.localizedDescription.UTF8String : lib ? "no main0" : "unknown error";
            failed_[q.pri].fetch_add(1, std::memory_order_relaxed);
        }
        compile_us_[q.pri].fetch_add((uint64_t)(ms * 1000.0), std::memory_order_relaxed);
        in_flight_.fetch_sub(1, std::memory_order_relaxed);
        q.job->Finish(lib, fn, err, ms);
        if (q.on_done) q.on_done(*q.job);
    }
    std::lock_guard<std::mutex> lk(mu_);
    running_--;
    StartQueued();
    // under mu_: the destructor cannot return before this call has released it
    if (running_ == 0 && queued_[0].empty() && queued_[1].empty()) idle_.notify_all();
}

} // namespace ps4gnm_kernels
