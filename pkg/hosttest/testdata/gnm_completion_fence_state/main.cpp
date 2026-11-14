// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_gnm_completion_fence.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Check(bool condition, const char *name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    ps4gnm::CompletionFenceState state;
    if (std::strcmp(argv[1], "initial") == 0) {
        Check(!state.HasSubmitSideFence(), "initial state has no submit-side fence");
        Check(!state.HasCompletionLabel(), "initial state has no completion label");
        Check(!state.CanSkipPresentDrain(), "initial state waits at present");
    } else if (std::strcmp(argv[1], "completion-label") == 0) {
        state.NoteCompletionLabel();
        Check(!state.HasSubmitSideFence(), "completion label does not enable submit-side elision");
        Check(state.HasCompletionLabel(), "completion label recorded");
        Check(state.CanSkipPresentDrain(), "completion label permits present simplification");
    } else if (std::strcmp(argv[1], "submit-fence") == 0) {
        state.NoteSubmitFence();
        Check(state.HasSubmitSideFence(), "submit-side fence recorded");
        Check(state.HasCompletionLabel(), "submit-side fence is also a completion signal");
        Check(state.CanSkipPresentDrain(), "submit-side fence permits present simplification");
    } else {
        return 2;
    }
    std::printf("PASS %s\n", argv[1]);
    return 0;
}
