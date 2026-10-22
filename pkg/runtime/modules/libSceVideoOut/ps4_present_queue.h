// Frame queue and pacing policy of the Metal presenter (ps4_metal_screen.m).
//
// The renderer (GNM encoder thread) converts each flipped display buffer into one of a few
// present slots and queues it; the presenter thread takes queued slots, waits for a drawable,
// copies the slot into it and presents it. A slot moves FREE -> RENDERING (producer owns it)
// -> QUEUED -> PRESENTING (presenter owns it) -> FREE. Plain C (pthreads) so the host tests can
// drive both the state machine and the blocking calls.
#ifndef PS4_PRESENT_QUEUE_H
#define PS4_PRESENT_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_PRESENT_MAX_SLOTS 4

// How the presenter paces frames (PS4_PRESENT_MODE).
typedef enum ps4_present_mode {
    // Every frame is shown, as fast as the game renders it, at most at the display refresh
    // (vsync on, no tearing). When the display falls behind the producer waits for a slot.
    PS4_PRESENT_UNCAPPED = 0,
    // Like the PS4 display engine: a frame stays on screen for at least the guest's flip
    // interval (sceVideoOutSetFlipRate: 60, 30 or 20 Hz); immediate (hsync) flips are not held.
    PS4_PRESENT_VSYNC = 1,
    // The producer never waits for the display: a queued frame that has not got a drawable yet
    // is replaced by the newer one (it is dropped and counted as superseded).
    PS4_PRESENT_MAILBOX = 2,
} ps4_present_mode;

// Parses PS4_PRESENT_MODE ("uncapped", "vsync", "mailbox"; NULL or "" = uncapped). Returns 0
// (and *out = uncapped) for an unknown value.
int ps4_present_parse_mode(const char *s, ps4_present_mode *out);
const char *ps4_present_mode_name(ps4_present_mode m);

// Guest flip modes of sceVideoOutSubmitFlip.
#define PS4_FLIP_MODE_VSYNC 1
#define PS4_FLIP_MODE_HSYNC 2

// Minimum time (seconds) the next frame must stay on screen after the previous one appeared:
// the argument of -[MTLCommandBuffer presentDrawable:afterMinimumDuration:]; 0 = present at
// the next refresh. guest_flip_rate is the sceVideoOutSetFlipRate value (0 = 60 Hz, 1 = 30 Hz,
// 2 = 20 Hz), display_hz the window's screen refresh (<= 0 = unknown, taken as 60). The result
// is half a refresh short of the guest interval, so a frame that is a little late still lands on
// the refresh that matches the guest rate instead of slipping by one.
double ps4_present_min_duration(ps4_present_mode mode, int guest_flip_rate, int guest_flip_mode, double display_hz);

typedef enum ps4_pq_state {
    PS4_PQ_FREE = 0,
    PS4_PQ_RENDERING,
    PS4_PQ_QUEUED,
    PS4_PQ_PRESENTING,
} ps4_pq_state;

typedef struct ps4_present_queue {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int n;
    bool mailbox;
    bool stop;
    ps4_pq_state state[PS4_PRESENT_MAX_SLOTS];
    bool keep[PS4_PRESENT_MAX_SLOTS];      // must be presented (e.g. carries a screenshot)
    uint64_t order[PS4_PRESENT_MAX_SLOTS]; // submission order of queued slots
    uint64_t next_order;
    uint64_t superseded; // mailbox replacements so far
    // FIFO only: drawables the presenter holds for frames not yet acquired. The producer may
    // take a slot only against such a credit, so it blocks exactly where it used to block in
    // nextDrawable and never runs ahead of the display (no added latency).
    int credits;
} ps4_present_queue;

// n slots (1..PS4_PRESENT_MAX_SLOTS); mailbox selects the replacing policy.
void ps4_pq_init(ps4_present_queue *q, int n, bool mailbox);

// Non-blocking transitions; the caller holds q->mu.
// Producer: a FREE slot (lowest index), or with mailbox the oldest QUEUED slot that is not
// `keep` (then *superseded = 1). FIFO also needs a drawable credit (consumed). -1 when none.
int ps4_pq_try_acquire_locked(ps4_present_queue *q, int *superseded);
// Producer: RENDERING -> QUEUED. Returns false for a slot it does not own.
bool ps4_pq_submit_locked(ps4_present_queue *q, int slot, bool keep);
// Producer: RENDERING -> FREE (the frame was abandoned); FIFO returns the drawable credit.
bool ps4_pq_cancel_locked(ps4_present_queue *q, int slot);
// Presenter: the oldest QUEUED slot -> PRESENTING; -1 when none.
int ps4_pq_try_take_locked(ps4_present_queue *q);
// Presenter: PRESENTING -> FREE.
bool ps4_pq_release_locked(ps4_present_queue *q, int slot);
// Presenter (FIFO): it holds a drawable for the next frame. Credits do not accumulate past one.
void ps4_pq_grant_locked(ps4_present_queue *q);
// No slot is QUEUED or PRESENTING.
bool ps4_pq_idle_locked(const ps4_present_queue *q);

// Blocking calls (take the lock, wake the other side). Timeouts are in seconds.
// Producer: waits up to `timeout` for a slot; -1 on timeout or stop. *waited_s = time waited.
int ps4_pq_acquire(ps4_present_queue *q, double timeout, int *superseded, double *waited_s);
bool ps4_pq_submit(ps4_present_queue *q, int slot, bool keep);
bool ps4_pq_cancel(ps4_present_queue *q, int slot);
// Presenter: waits for a queued slot; -1 once stopped.
int ps4_pq_take(ps4_present_queue *q);
bool ps4_pq_release(ps4_present_queue *q, int slot);
void ps4_pq_grant(ps4_present_queue *q);
// Waits up to `timeout` until nothing is queued or being presented; false on timeout.
bool ps4_pq_drain(ps4_present_queue *q, double timeout);
// Wakes every waiter; take and acquire return -1 from now on.
void ps4_pq_stop(ps4_present_queue *q);

#ifdef __cplusplus
}
#endif

#endif // PS4_PRESENT_QUEUE_H
