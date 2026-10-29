// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - background compilation of compute kernels (see ps4_gnm_kernel_compile.mm).
 *
 * A dispatch cannot be skipped while its kernel compiles: later passes read what it writes
 * (buffers, indirect arguments, textures). Before this, a new compute shader's Metal library was
 * compiled on the CP thread, inside the guest's submit call, about 300 ms per TLOU kernel on a
 * cold system Metal cache, one after another.
 *
 * Now the CP thread translates the shader (GCN -> MSL, about a millisecond, it needs the
 * reflection) and starts the Metal compile here, then hands the dispatch to the encoder thread as
 * usual. The encoder thread waits for the library when it reaches that dispatch, in stream order,
 * so every dispatch still runs, with its own kernel, before the work recorded after it. Meanwhile
 * the CP thread keeps processing the guest's command stream (the encoder runs up to a frame
 * behind it), and kernels met in the same stretch compile in parallel instead of in sequence.
 *
 * One job per MSL (keyed by the renderer's MSL hash): a second permutation or address with the
 * same MSL shares the job, finished or not. A failed compile stays failed for the session.
 * At most `max_parallel` compiles run at once, started in the order they were asked for (the
 * order the encoder needs them); more would only slow each other and the CP and encoder threads
 * down (22 at once were measured in a TLOU loading burst).
 *
 * The renderer's vertex and fragment libraries go through the same queue at low priority
 * (`kDraw`): a draw is skipped while its shader compiles, a dispatch waits. Kernels (`kDispatch`)
 * start before any queued draw shader, and the limit holds for both together, so a loading burst
 * of hundreds of new draw shaders no longer runs as hundreds of compiles at once (measured: the
 * MTLCompilerService processes at 900 % CPU, load above 20, frames of 0.3–1.4 s without any work
 * of the frame's own on the CP).
 */
#pragma once

#import <Metal/Metal.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ps4gnm_kernels {

// One kernel's Metal library, compiled on a background thread.
class Job {
public:
    explicit Job(uint64_t mh) : mh_(mh) {}
    uint64_t Hash() const { return mh_; }
    // True once the compile finished (successfully or not); never blocks.
    bool Done() const { return done_.load(std::memory_order_acquire); }
    // Blocks until the compile finished. Returns the kernel ("main0"), nil if Metal rejected the
    // MSL. *waited_ms (optional): how long this call blocked (0 if the job was done already).
    id<MTLFunction> Wait(double* waited_ms = nullptr);
    // After Done(): Metal's error text of a failed compile ("" when it succeeded); compile time;
    // the library (nil if the MSL did not compile).
    std::string Error() const;
    id<MTLLibrary> Library() const;
    double CompileMs() const { return compile_ms_; }

private:
    friend class Compiler;
    void Finish(id<MTLLibrary> lib, id<MTLFunction> fn, const std::string& err, double ms);
    const uint64_t mh_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> done_{false};
    id<MTLLibrary> lib_ = nil; // written once under mu_ before done_
    id<MTLFunction> fn_ = nil;
    std::string err_;
    double compile_ms_ = 0;
};

class Compiler {
public:
    // make_options: called once per compile (MTLCompileOptions objects are not shared between
    // threads). The compiles run on a concurrent queue at user-interactive QoS: the frame waits
    // for them.
    Compiler(id<MTLDevice> dev, MTLCompileOptions* (*make_options)(), unsigned max_parallel);
    // Waits for the running compiles; queued ones finish as failed ("compile queue shut down")
    // without compiling (an exit, e.g. from a fatal-signal handler, must not sit through a burst).
    ~Compiler();
    Compiler(const Compiler&) = delete;
    Compiler& operator=(const Compiler&) = delete;
    // The job of this MSL: the existing one (in flight or finished), or a new one, queued
    // behind the jobs asked for earlier. *started (optional): true if this call started it. Never blocks on a compile.
    enum Priority { kDispatch, kDraw };
    // Called on the compiling thread once the job is done (only for the call that started it).
    using Done = std::function<void(const Job&)>;
    // A kDispatch request for a job still queued as kDraw moves it ahead.
    std::shared_ptr<Job> Start(uint64_t mh, const std::string& msl, bool* started = nullptr,
                               Priority pri = kDispatch, Done on_done = nullptr);
    // The job of this MSL, null if none was started.
    std::shared_ptr<Job> Find(uint64_t mh) const;
    // Session counters, all priorities or one (jobs count under the priority they were started
    // with).
    uint64_t Started() const { return Started(kDispatch) + Started(kDraw); }
    uint64_t Failed() const { return Failed(kDispatch) + Failed(kDraw); }
    uint64_t Started(Priority p) const { return started_[p].load(std::memory_order_relaxed); }
    uint64_t Failed(Priority p) const { return failed_[p].load(std::memory_order_relaxed); }
    double CompileMs(Priority p) const { return compile_us_[p].load(std::memory_order_relaxed) / 1000.0; }
    // Jobs not finished yet (queued or compiling).
    uint64_t Unfinished() const { return in_flight_.load(std::memory_order_relaxed); }
    // Largest number of compiles that ran at the same time (at most max_parallel).
    uint64_t PeakRunning() const { return peak_.load(std::memory_order_relaxed); }
    double CompileMsTotal() const { return CompileMs(kDispatch) + CompileMs(kDraw); }

private:
    struct Queued {
        std::shared_ptr<Job> job;
        std::shared_ptr<std::string> msl;
        Done on_done;
        Priority pri = kDispatch;
    };
    void Run(Queued q);
    void StartQueued(); // with mu_ held
    id<MTLDevice> dev_;
    MTLCompileOptions* (*make_options_)();
    dispatch_queue_t queue_;
    mutable std::mutex mu_;
    std::unordered_map<uint64_t, std::shared_ptr<Job>> jobs_;
    std::deque<Queued> queued_[2]; // FIFO per priority; kDispatch first
    const unsigned max_parallel_;
    unsigned running_ = 0;
    std::condition_variable idle_;
    std::atomic<uint64_t> started_[2] = {}, failed_[2] = {}, compile_us_[2] = {};
    std::atomic<uint64_t> in_flight_{0}, peak_{0};
};

} // namespace ps4gnm_kernels
