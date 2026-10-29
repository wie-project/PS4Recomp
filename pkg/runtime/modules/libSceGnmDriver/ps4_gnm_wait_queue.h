// A command queue whose submissions run in order and can stop at a WAIT_REG_MEM (the compute
// queues of ps4_gnm_metal.mm). Kept free of Metal and guest state so the scheduling rules are
// testable (tests/host/gnm_compute_wait_test.cc).
#pragma once
#include <stdint.h>
#include <deque>
#include <utility>

namespace ps4gnm_wq {

template <class Submit>
struct WaitQueue {
    std::deque<Submit> pending;
    uint64_t suspended_at = 0; // time the current wait started (ns), 0 = running
    uint64_t suspends = 0, timeouts = 0;

    // Runs the submissions in order until they are done or one stops at a wait.
    // process(Submit&, bool skip_wait) returns true when the submission finished; skip_wait
    // makes the first wait it reaches pass (the current wait timed out). force: skip the wait
    // the queue is stopped at. Returns true when the queue newly stopped at a wait.
    template <class Process>
    bool Run(bool force, uint64_t now, Process &&process) {
        while (!pending.empty()) {
            const bool skip = force && suspended_at != 0;
            force = false;
            if (skip) {
                timeouts++;
                suspended_at = 0; // a later wait of this submission gets its own time limit
            }
            if (!process(pending.front(), skip)) {
                if (suspended_at) return false;
                suspended_at = now ? now : 1;
                suspends++;
                return true;
            }
            suspended_at = 0;
            pending.pop_front();
        }
        return false;
    }

    bool Suspended() const { return suspended_at != 0 && !pending.empty(); }
    bool TimedOut(uint64_t now, uint64_t limit_ns) const { return Suspended() && now - suspended_at > limit_ns; }

    // The queue was unmapped or mapped again: its submissions point into the old ring.
    void Reset() {
        pending.clear();
        suspended_at = 0;
    }
};

} // namespace ps4gnm_wq
