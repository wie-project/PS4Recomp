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

// Parses PS4_PRESENT_MODE ("uncapped", "vsync", "mailbox"; NULL or "" = vsync). Returns 0
// (and *out = vsync) for an unknown value.
int ps4_present_parse_mode(const char *s, ps4_present_mode *out);
const char *ps4_present_mode_name(ps4_present_mode m);

// Guest flip modes of sceVideoOutSubmitFlip.
#define PS4_FLIP_MODE_VSYNC 1
#define PS4_FLIP_MODE_HSYNC 2

// The host display as the presenter sees it.
typedef struct ps4_display_info {
    double hz;     // its highest refresh rate (NSScreen maximumFramesPerSecond); <= 0 = unknown, taken as 60
    bool variable; // adaptive refresh (ProMotion, VRR): no fixed grid of refreshes to pace by
} ps4_display_info;

// Does the display alone pace the guest's flip rate (sceVideoOutSetFlipRate: 0 = 60 Hz, 1 = 30 Hz,
// 2 = 20 Hz) exactly? Only a fixed refresh at a whole multiple of it (60 or 120 Hz for 60 Hz, 1%
// tolerance for 59.94 Hz panels): each frame is then shown for the same number of refreshes.
bool ps4_display_paces_guest(ps4_display_info d, int guest_flip_rate);

// Guest flip pacing of the presenter thread (one thread: no lock).
typedef struct ps4_flip_pacer {
    uint64_t epoch_ns;  // the guest vblank timeline (ps4_vblank_clock.h)
    uint64_t last_tick; // guest vblank of the previous flip paced on the timeline
    bool has_last;
} ps4_flip_pacer;

void ps4_flip_pacer_init(ps4_flip_pacer *p, uint64_t epoch_ns);

// How the presenter shows the next frame.
typedef struct ps4_present_plan {
    // host time (ps4_vblank_now_ns) the presenter waits for before presenting; 0 = no wait
    uint64_t not_before_ns;
    // the argument of -[MTLCommandBuffer presentDrawable:afterMinimumDuration:]: the least time
    // the frame comes after the previous one appeared; 0 = the next refresh
    double min_duration;
} ps4_present_plan;

// The plan for a frame the presenter takes at now_ns. Only vsync mode with vsync flips paces
// (immediate flips, uncapped and mailbox present at the next refresh). A display that paces the
// guest rate (ps4_display_paces_guest) is left to do it: min_duration is half a refresh short of
// the guest interval, so a frame that is a little late still lands on the refresh that matches
// the guest rate instead of slipping by one. Any other display (72, 144 Hz, adaptive) cannot show
// every frame for the same number of refreshes: like the PS4 display engine, a frame then flips on
// the guest vblank timeline, on the first vblank after now and at least the flip rate's number of
// vblanks after the previous flip, and goes to the display's next refresh. The deadlines are
// vblank ticks, so pacing neither drifts nor bursts to catch up after a late frame.
ps4_present_plan ps4_flip_pacer_plan(ps4_flip_pacer *p, ps4_present_mode mode, int guest_flip_rate,
                                     int guest_flip_mode, ps4_display_info d, uint64_t now_ns);

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
// Would ps4_pq_try_acquire_locked return a slot now? Changes nothing.
bool ps4_pq_can_acquire_locked(const ps4_present_queue *q);
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
// Presenter: may its drawable go to a frame from outside the slots (a held frame, see
// ps4_metal_screen.m)? Only when no slot frame is ahead of it, i.e. no slot is RENDERING or
// QUEUED: an acquired slot is an older frame that the drawable was granted for. On true the
// FIFO credit for the drawable, if not yet claimed, is consumed (the drawable is used up).
bool ps4_pq_claim_drawable_locked(ps4_present_queue *q);
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
