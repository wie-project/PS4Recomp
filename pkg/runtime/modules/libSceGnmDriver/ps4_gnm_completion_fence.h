// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

namespace ps4gnm {

// A submit-side synchronization fence protects guest inputs from encoder overrun. A fixed-value
// completion label only permits eliding the present-side GPU drain.
class CompletionFenceState {
public:
    void NoteSubmitFence() {
        submit_side_.store(true, std::memory_order_relaxed);
        completion_label_.store(true, std::memory_order_relaxed);
    }

    void NoteCompletionLabel() {
        completion_label_.store(true, std::memory_order_relaxed);
    }

    bool HasSubmitSideFence() const {
        return submit_side_.load(std::memory_order_relaxed);
    }

    bool HasCompletionLabel() const {
        return completion_label_.load(std::memory_order_relaxed);
    }

    bool CanSkipPresentDrain() const {
        return HasSubmitSideFence() || HasCompletionLabel();
    }

private:
    std::atomic<bool> submit_side_{false};
    std::atomic<bool> completion_label_{false};
};

} // namespace ps4gnm
