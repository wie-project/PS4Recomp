#include "recomp_runtime.h"
#include "ps4_sync.h"
#include "ps4_sync_objects.h"
#include "ps4_threading.h"
#include <errno.h>
#include <mach/mach_time.h>
#include <stdatomic.h>
#include <time.h>

// scePthread* report failures as SCE kernel errors (0x80020000 | errno),
// while the POSIX shims they forward to return plain errno values.
#define SCE_PTHREAD_ERR(ctx)                                                   \
  do {                                                                         \
    uint32_t e_ = (uint32_t)(ctx)->rax;                                        \
    if (e_ != 0 && e_ < 0x100)                                                 \
      (ctx)->rax = 0x80020000ULL | e_;                                         \
  } while (0)

#define MUTEX_MAP_SIZE 1024

// Guest sync objects are identified by the value stored in the guest handle variable, not by
// its address: games copy handles around (e.g. Scream passes the address of a stack copy to
// scePthreadMutexUnlock). The value is the guest address of the object (ps4_sync_objects.h).
// Init stores a fresh object in the variable; an uninitialized (static initializer, 0) variable
// is given one on first use.
static pthread_mutex_t g_sync_handle_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t sync_handle(GuestContext *ctx, uint64_t addr, int fresh) {
  if (!addr) return 0;
  uint64_t *p = (uint64_t *)(ctx->mem_base + addr);
  uint64_t v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
  if (!fresh && sync_obj_slot(v) >= 0) return v;
  pthread_mutex_lock(&g_sync_handle_lock);
  v = *p;
  if (fresh || sync_obj_slot(v) < 0) {
    v = sync_obj_alloc(ctx);
    if (!v) {
      fprintf(stderr, "FATAL: no guest memory for sync objects (%llu in use at most)\n",
              (unsigned long long)SYNC_OBJ_SLOTS);
      abort();
    }
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
  }
  pthread_mutex_unlock(&g_sync_handle_lock);
  return v;
}

// Returns the object held by the guest variable (0 if none) and clears the variable.
static uint64_t sync_handle_take(GuestContext *ctx, uint64_t addr) {
  if (!addr) return 0;
  uint64_t *p = (uint64_t *)(ctx->mem_base + addr);
  uint64_t v = __atomic_exchange_n(p, 0, __ATOMIC_ACQ_REL);
  return sync_obj_slot(v) >= 0 ? v : 0;
}

// Guest mutexes track ownership by guest thread id rather than host pthread:
// a fiber adopts the thread_id of the guest thread it runs on, so a lock taken
// on a worker and released inside one of its fibers (or the reverse) must
// still be treated as the same owner.
//
// The owner word is taken with a CAS while the mutex is free and released with
// a store. A contended lock first spins for a few microseconds (Orbis mutexes
// are adaptive; the game's job queue mutex is held ~1 us at a time) and only
// then sleeps on cv under lk. `waiters` counts the threads in that slow path:
// a waiter increments it before its last CAS and an unlock clears the owner
// before reading it (both seq_cst), so either the waiter sees the mutex free
// or the unlock sees the waiter and signals it under lk.
// PS4_MUTEX_LEGACY=1 restores the previous scheme for comparison: ownership
// changes under lk, every contended lock sleeps, lookups take the global lock.
typedef struct GuestMutex {
  _Atomic uint64_t owner; // guest thread id, 0 = free
  uint32_t count;         // recursion depth, touched by the owner only
  int type;
  _Atomic uint32_t waiters;
  // Statistics (ps4_sync_print_stats), written by the owner.
  uint64_t var;   // guest address of the handle variable last seen
  uint64_t acq_t; // when the current owner took it (PS4_MUTEX_STATS)
  uint64_t rel_t; // last release that had a sleeping waiter
  uint64_t st_locks, st_cont, st_wait, st_hold;
  struct GuestMutex *next_free, *next_all;
  pthread_mutex_t lk __attribute__((aligned(64)));
  pthread_cond_t cv;
} __attribute__((aligned(128))) GuestMutex;

static bool g_mutex_legacy;
static bool g_mstat_detail; // PS4_MUTEX_STATS=1: also time lookups, calls and hold times
static uint64_t g_mutex_spin_ticks;

// Mutex statistics, printed with the GNM perf line (ps4_sync_print_stats).
// Counted per thread name, one cache-line aligned slot each, so counting does
// not bounce lines between the job workers. Times are mach ticks.
#define MSTAT_SLOTS 64
#define MSTAT_BUCKETS 256 // 1 us buckets of contended waits; the last collects everything slower
typedef struct MStat {
  char name[32];
  _Atomic uint64_t locks, trylocks, contended, spun, sleeps, futile, cond_waits;
  _Atomic uint64_t wait, wake, wakes, lookup, shim, lk, lk_cont;
  _Atomic uint32_t hist[MSTAT_BUCKETS];
} __attribute__((aligned(128))) MStat;
static MStat g_mstat[MSTAT_SLOTS];
static int g_mstat_used;
static pthread_mutex_t g_mstat_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local MStat *tl_mstat;
static _Thread_local RecompThread *tl_mstat_thread;
static double g_mticks_per_us;
// PS4_MUTEX_EXTRA_US (legacy mode): busy-wait this long (lock free meanwhile)
// after every wake-up of a contended lock, to measure what wake latency costs.
static uint64_t g_mutex_extra_ticks;

__attribute__((constructor)) static void sync_global_init(void) {
  mach_timebase_info_data_t tb;
  mach_timebase_info(&tb);
  g_mticks_per_us = 1000.0 * tb.denom / tb.numer;
  const char *e = getenv("PS4_MUTEX_LEGACY");
  g_mutex_legacy = e && atoi(e) > 0;
  e = getenv("PS4_MUTEX_STATS");
  g_mstat_detail = e && atoi(e) > 0;
  e = getenv("PS4_MUTEX_SPIN_US");
  const int spin_us = e ? atoi(e) : 10;
  g_mutex_spin_ticks = spin_us > 0 ? (uint64_t)(spin_us * g_mticks_per_us) : 0;
  e = getenv("PS4_MUTEX_EXTRA_US");
  if (g_mutex_legacy && e && atoi(e) > 0) g_mutex_extra_ticks = (uint64_t)(atoi(e) * g_mticks_per_us);
  if (g_mutex_legacy)
    fprintf(stderr, "[ps4-mutex] legacy mutexes (PS4_MUTEX_LEGACY), %d us added to contended wake-ups\n",
            e && atoi(e) > 0 ? atoi(e) : 0);
  else
    fprintf(stderr, "[ps4-mutex] atomic mutexes, contended locks spin up to %d us before sleeping\n",
            spin_us > 0 ? spin_us : 0);
}

static MStat *mstat_slot_slow(RecompThread *t) {
  char name[32];
  snprintf(name, sizeof name, "%s", (t && t->name[0]) ? t->name : "(host)");
  pthread_mutex_lock(&g_mstat_lock);
  MStat *s = NULL;
  for (int i = 0; i < g_mstat_used && !s; i++)
    if (!strcmp(g_mstat[i].name, name)) s = &g_mstat[i];
  if (!s && g_mstat_used < MSTAT_SLOTS) {
    s = &g_mstat[g_mstat_used++];
    memcpy(s->name, name, sizeof name);
  }
  if (!s) s = &g_mstat[MSTAT_SLOTS - 1]; // table full: share the last slot
  pthread_mutex_unlock(&g_mstat_lock);
  tl_mstat_thread = t;
  tl_mstat = s;
  return s;
}

static inline MStat *mstat_slot(void) {
  RecompThread *t = recomp_get_current_thread();
  if (__builtin_expect(tl_mstat != NULL && tl_mstat_thread == t, 1)) return tl_mstat;
  return mstat_slot_slow(t);
}

static inline uint64_t mstat_now(void) { return g_mstat_detail ? mach_absolute_time() : 0; }

#define MSTAT_ADD(s, f, v) atomic_fetch_add_explicit(&(s)->f, (v), memory_order_relaxed)
#define GM_ADD(m, f, v) __atomic_store_n(&(m)->f, __atomic_load_n(&(m)->f, __ATOMIC_RELAXED) + (v), __ATOMIC_RELAXED)
#define GM_OWNER(m) atomic_load_explicit(&(m)->owner, memory_order_relaxed)
#define GM_SET_OWNER(m, v) atomic_store_explicit(&(m)->owner, (v), memory_order_relaxed)

static void mstat_wait(MStat *s, GuestMutex *m, uint64_t dt) {
  MSTAT_ADD(s, wait, dt);
  GM_ADD(m, st_cont, 1);
  GM_ADD(m, st_wait, dt);
  const uint64_t us = (uint64_t)((double)dt / g_mticks_per_us);
  MSTAT_ADD(s, hist[us < MSTAT_BUCKETS ? us : MSTAT_BUCKETS - 1], 1);
}

// Takes the host lock behind m, counting the time when it is contended.
static inline void gm_lk_lock(GuestMutex *m, MStat *s) {
  if (__builtin_expect(pthread_mutex_trylock(&m->lk) == 0, 1)) return;
  const uint64_t t0 = mach_absolute_time();
  pthread_mutex_lock(&m->lk);
  MSTAT_ADD(s, lk, mach_absolute_time() - t0);
  MSTAT_ADD(s, lk_cont, 1);
}

// Mutexes are found by handle id in a two-level table that is read without
// locks. Nodes are never freed while the game runs (destroyed ones are
// recycled), so a lookup racing with a destroy still sees valid memory. Ids
// past the table (never seen in practice) go to a hash map.
#define MTAB_PAGE_BITS 12
#define MTAB_PAGES 4096
#define MTAB_IDS ((uint64_t)MTAB_PAGES << MTAB_PAGE_BITS)
typedef _Atomic(GuestMutex *) MtabEntry;
static MtabEntry *_Atomic g_mtab[MTAB_PAGES];

typedef struct MutexNode {
  uint64_t guest_addr;
  GuestMutex *gm;
  struct MutexNode *next;
} MutexNode;

static MutexNode *g_mutex_map[MUTEX_MAP_SIZE] = {0};
static pthread_mutex_t g_mutex_map_lock = PTHREAD_MUTEX_INITIALIZER;
static GuestMutex *g_mutex_free, *g_mutex_all;

static uint64_t guest_owner_id(GuestContext *ctx) {
  if (ctx && ctx->thread_id) return ctx->thread_id;
  return (uint64_t)(uintptr_t)pthread_self() | (1ULL << 63);
}

// handle comes from sync_handle/sync_handle_take, so it was validated already.
static inline uint64_t gm_id(uint64_t handle) {
  const int64_t slot = sync_obj_index(handle);
  return slot >= 0 ? (uint64_t)slot + 1 : 0;
}

static GuestMutex *gm_alloc_locked(int type) {
  GuestMutex *m = g_mutex_free;
  if (m) {
    g_mutex_free = m->next_free;
  } else {
    void *p = NULL;
    if (posix_memalign(&p, 128, sizeof(GuestMutex))) abort();
    m = (GuestMutex *)p;
    memset(m, 0, sizeof *m);
    pthread_mutex_init(&m->lk, NULL);
    pthread_cond_init(&m->cv, NULL);
    m->next_all = g_mutex_all;
    g_mutex_all = m;
  }
  GM_SET_OWNER(m, 0);
  m->count = 0;
  m->type = type;
  atomic_store_explicit(&m->waiters, 0, memory_order_relaxed);
  m->var = m->acq_t = m->rel_t = 0;
  m->st_locks = m->st_cont = m->st_wait = m->st_hold = 0;
  m->next_free = NULL;
  return m;
}

// Finds (or creates with guest_type) the mutex for handle; g_mutex_map_lock held.
static GuestMutex *gm_get_locked(uint64_t handle, int guest_type) {
  const uint64_t id = gm_id(handle);
  if (id < MTAB_IDS) {
    MtabEntry *page = atomic_load_explicit(&g_mtab[id >> MTAB_PAGE_BITS], memory_order_relaxed);
    if (!page) {
      page = (MtabEntry *)calloc((size_t)1 << MTAB_PAGE_BITS, sizeof(MtabEntry));
      atomic_store_explicit(&g_mtab[id >> MTAB_PAGE_BITS], page, memory_order_release);
    }
    MtabEntry *e = &page[id & ((1u << MTAB_PAGE_BITS) - 1)];
    GuestMutex *m = atomic_load_explicit(e, memory_order_relaxed);
    if (!m) {
      m = gm_alloc_locked(guest_type);
      atomic_store_explicit(e, m, memory_order_release);
    }
    return m;
  }
  uint64_t bucket = sync_obj_hash(handle) % MUTEX_MAP_SIZE;
  for (MutexNode *curr = g_mutex_map[bucket]; curr; curr = curr->next)
    if (curr->guest_addr == handle) return curr->gm;
  MutexNode *node = (MutexNode *)calloc(1, sizeof(MutexNode));
  node->guest_addr = handle;
  node->gm = gm_alloc_locked(guest_type);
  node->next = g_mutex_map[bucket];
  g_mutex_map[bucket] = node;
  return node->gm;
}

static GuestMutex *get_host_mutex_with_type(uint64_t handle, int guest_type) {
  const uint64_t id = gm_id(handle);
  if (!g_mutex_legacy && id < MTAB_IDS) {
    MtabEntry *page = atomic_load_explicit(&g_mtab[id >> MTAB_PAGE_BITS], memory_order_acquire);
    if (page) {
      GuestMutex *m = atomic_load_explicit(&page[id & ((1u << MTAB_PAGE_BITS) - 1)], memory_order_acquire);
      if (m) return m;
    }
  }
  pthread_mutex_lock(&g_mutex_map_lock);
  GuestMutex *m = gm_get_locked(handle, guest_type);
  pthread_mutex_unlock(&g_mutex_map_lock);
  return m;
}

static GuestMutex *get_host_mutex(uint64_t handle) {
  return get_host_mutex_with_type(handle, 0);
}

// Mutex named by the guest handle variable at addr.
static GuestMutex *gm_lookup(GuestContext *ctx, uint64_t addr) {
  GuestMutex *m = get_host_mutex(sync_handle(ctx, addr, 0));
  if (__atomic_load_n(&m->var, __ATOMIC_RELAXED) != addr) __atomic_store_n(&m->var, addr, __ATOMIC_RELAXED);
  return m;
}

// Sleeps until the absolute CLOCK_REALTIME time abs.
static void gm_sleep_until(const struct timespec *abs) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  int64_t ns = (int64_t)(abs->tv_sec - now.tv_sec) * 1000000000 + (abs->tv_nsec - now.tv_nsec);
  if (ns <= 0) return;
  struct timespec d = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
  while (nanosleep(&d, &d) != 0 && errno == EINTR) {
  }
}

// Relocks by the owner, counted by mutex type (0..4; 5 = any other value). Reported with
// PS4_MUTEX_STATS, to measure what the type-0 divergence below covers.
#define RELOCK_TYPES 6
static _Atomic uint64_t g_relocks[RELOCK_TYPES];

void ps4_sync_relocks_by_type(uint64_t out[6]) {
  for (int i = 0; i < RELOCK_TYPES; i++) out[i] = atomic_load_explicit(&g_relocks[i], memory_order_relaxed);
}

// Lock of a mutex the caller already owns, by type (FreeBSD libthr mutex_self_lock):
// RECURSIVE (2) counts. ERRORCHECK (1) and ADAPTIVE_NP (4) fail with EDEADLK without
// waiting, so a caller that tracks its own nesting unlocks once. NORMAL (3) would deadlock on
// Orbis; it fails with EDEADLK here as well. With a timeout, the non-recursive types wait it
// out and fail with ETIMEDOUT.
// Type 0 (no type set: a NULL attribute, or an attribute never given a type) stays recursive.
// This is a known divergence: on Orbis that default is ERRORCHECK (shadPS4 mutex.cpp
// PthreadMutexattrDefault, used by MutexInit for a NULL attribute and by
// posix_pthread_mutexattr_init), and the static adaptive initializer gives ADAPTIVE_NP.
static int gm_relock(GuestMutex *m, const struct timespec *abs) {
  const unsigned t = (unsigned)m->type < RELOCK_TYPES - 1 ? (unsigned)m->type : RELOCK_TYPES - 1;
  atomic_fetch_add_explicit(&g_relocks[t], 1, memory_order_relaxed);
  if (m->type == 1 || m->type == 3 || m->type == 4) {
    if (!abs) return EDEADLK;
    gm_sleep_until(abs);
    return ETIMEDOUT;
  }
  m->count++;
  return 0;
}

static inline void gm_acquired(GuestMutex *m, uint64_t now) {
  m->count = 1;
  m->acq_t = now;
  GM_ADD(m, st_locks, 1);
}

// Bookkeeping when m stops being owned.
static inline void gm_released(GuestMutex *m, uint64_t now) {
  if (g_mstat_detail) GM_ADD(m, st_hold, now - m->acq_t);
}

// ---- Legacy scheme (PS4_MUTEX_LEGACY=1): ownership changes under lk.

// Wait until m is free or owned by me; m->lk held. abs == NULL waits forever.
// t0 is when the caller started waiting for m.
// The caller does not own m (gm_lock handles a relock before taking m->lk).
static int gm_acquire_locked(GuestMutex *m, uint64_t me, const struct timespec *abs, MStat *s, uint64_t t0) {
  uint64_t now = t0;
  if (GM_OWNER(m) != 0) {
    MSTAT_ADD(s, contended, 1);
    if (!t0) t0 = mach_absolute_time();
    while (GM_OWNER(m) != 0) {
      const uint64_t ts = mach_absolute_time();
      int r = 0;
      atomic_fetch_add_explicit(&m->waiters, 1, memory_order_relaxed);
      if (abs) r = pthread_cond_timedwait(&m->cv, &m->lk, abs);
      else pthread_cond_wait(&m->cv, &m->lk);
      atomic_fetch_sub_explicit(&m->waiters, 1, memory_order_relaxed);
      MSTAT_ADD(s, sleeps, 1);
      now = mach_absolute_time();
      if (GM_OWNER(m) == 0 && m->rel_t >= ts) {
        MSTAT_ADD(s, wake, now - m->rel_t);
        MSTAT_ADD(s, wakes, 1);
      }
      if (g_mutex_extra_ticks) {
        pthread_mutex_unlock(&m->lk);
        const uint64_t until = now + g_mutex_extra_ticks;
        while ((now = mach_absolute_time()) < until) {
        }
        gm_lk_lock(m, s);
      }
      if (GM_OWNER(m) != 0) {
        if (r == ETIMEDOUT) return ETIMEDOUT;
        MSTAT_ADD(s, futile, 1);
      }
    }
    now = mach_absolute_time();
    mstat_wait(s, m, now - t0);
  }
  GM_SET_OWNER(m, me);
  gm_acquired(m, now);
  return 0;
}

static int gm_unlock_locked(GuestMutex *m, uint64_t me, uint64_t now) {
  if (GM_OWNER(m) != me) return EPERM;
  if (--m->count == 0) {
    GM_SET_OWNER(m, 0);
    gm_released(m, now);
    if (atomic_load_explicit(&m->waiters, memory_order_relaxed)) m->rel_t = mach_absolute_time();
    pthread_cond_signal(&m->cv);
  }
  return 0;
}

// ---- Atomic scheme (default).

static inline bool gm_cas(GuestMutex *m, uint64_t me, memory_order mo) {
  uint64_t z = 0;
  return atomic_compare_exchange_strong_explicit(&m->owner, &z, me, mo, memory_order_relaxed);
}

// Spins until `until` for m to become free and takes it.
static bool gm_spin(GuestMutex *m, uint64_t me, uint64_t until) {
  do {
    for (int i = 0; i < 32; i++) {
      __asm__ __volatile__("isb" ::: "memory");
      if (GM_OWNER(m) == 0 && gm_cas(m, me, memory_order_acquire)) return true;
    }
  } while (mach_absolute_time() < until);
  return false;
}

// Sleeps on m->cv until m is taken; returns ETIMEDOUT if abs passes first.
static int gm_sleep(GuestMutex *m, uint64_t me, const struct timespec *abs, MStat *s) {
  gm_lk_lock(m, s);
  atomic_fetch_add_explicit(&m->waiters, 1, memory_order_seq_cst);
  int r = 0;
  bool got;
  while (!(got = gm_cas(m, me, memory_order_seq_cst))) {
    if (r == ETIMEDOUT) break;
    const uint64_t ts = mach_absolute_time();
    if (abs) r = pthread_cond_timedwait(&m->cv, &m->lk, abs);
    else pthread_cond_wait(&m->cv, &m->lk);
    MSTAT_ADD(s, sleeps, 1);
    if (GM_OWNER(m) != 0) {
      MSTAT_ADD(s, futile, 1);
    } else if (m->rel_t >= ts) {
      MSTAT_ADD(s, wake, mach_absolute_time() - m->rel_t);
      MSTAT_ADD(s, wakes, 1);
    }
  }
  atomic_fetch_sub_explicit(&m->waiters, 1, memory_order_relaxed);
  pthread_mutex_unlock(&m->lk);
  return got ? 0 : ETIMEDOUT;
}

// Clears the owner word and wakes a sleeping waiter, if any.
static inline void gm_release(GuestMutex *m) {
  atomic_store_explicit(&m->owner, 0, memory_order_seq_cst);
  if (atomic_load_explicit(&m->waiters, memory_order_seq_cst)) {
    pthread_mutex_lock(&m->lk);
    m->rel_t = mach_absolute_time();
    pthread_cond_signal(&m->cv);
    pthread_mutex_unlock(&m->lk);
  }
}

// ---- Entry points used by the shims.

static int gm_lock(GuestMutex *m, uint64_t me, const struct timespec *abs, MStat *s, uint64_t t0) {
  // Only this thread makes m owned by it or not, so the check needs no lock, and a timed
  // relock waits without holding m->lk.
  const uint64_t o = GM_OWNER(m);
  if (o == me) return gm_relock(m, abs);
  if (g_mutex_legacy) {
    gm_lk_lock(m, s);
    int r = gm_acquire_locked(m, me, abs, s, t0);
    pthread_mutex_unlock(&m->lk);
    return r;
  }
  if (__builtin_expect(o == 0, 1) && gm_cas(m, me, memory_order_acquire)) {
    gm_acquired(m, t0);
    return 0;
  }
  MSTAT_ADD(s, contended, 1);
  const uint64_t ts = mach_absolute_time();
  if (g_mutex_spin_ticks && gm_spin(m, me, ts + g_mutex_spin_ticks)) {
    MSTAT_ADD(s, spun, 1);
  } else if (gm_sleep(m, me, abs, s) == ETIMEDOUT) {
    return ETIMEDOUT;
  }
  const uint64_t now = mach_absolute_time();
  gm_acquired(m, now);
  mstat_wait(s, m, now - ts);
  return 0;
}

static int gm_trylock(GuestMutex *m, uint64_t me, MStat *s, uint64_t t0) {
  int r;
  if (g_mutex_legacy) gm_lk_lock(m, s);
  const uint64_t o = GM_OWNER(m);
  if (o == me) {
    r = gm_relock(m, NULL);
    if (r == EDEADLK) r = EBUSY;
  } else if (o != 0) {
    r = EBUSY;
  } else if (g_mutex_legacy) {
    GM_SET_OWNER(m, me);
    gm_acquired(m, t0);
    r = 0;
  } else if (gm_cas(m, me, memory_order_acquire)) {
    gm_acquired(m, t0);
    r = 0;
  } else {
    r = EBUSY;
  }
  if (g_mutex_legacy) pthread_mutex_unlock(&m->lk);
  return r;
}

static int gm_unlock(GuestMutex *m, uint64_t me, MStat *s, uint64_t t0) {
  if (g_mutex_legacy) {
    gm_lk_lock(m, s);
    int r = gm_unlock_locked(m, me, t0);
    pthread_mutex_unlock(&m->lk);
    return r;
  }
  if (GM_OWNER(m) != me) return EPERM;
  if (--m->count == 0) {
    gm_released(m, t0);
    gm_release(m);
  }
  return 0;
}

static void abs_from_usec(struct timespec *ts, uint64_t usec) {
  clock_gettime(CLOCK_REALTIME, ts);
  ts->tv_sec += (time_t)(usec / 1000000ULL);
  ts->tv_nsec += (long)((usec % 1000000ULL) * 1000ULL);
  if (ts->tv_nsec >= 1000000000L) {
    ts->tv_sec += 1;
    ts->tv_nsec -= 1000000000L;
  }
}

void shim_pthread_mutex_init(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t attr_addr = ctx->rsi;
  int guest_type = 0;
  if (attr_addr) {
    guest_type = *(int *)(ctx->mem_base + attr_addr);
  }
  const uint64_t h = sync_handle(ctx, mtx_addr, 1);
  (void)get_host_mutex_with_type(h, guest_type);
  *(uint32_t *)(ctx->mem_base + h + SYNC_OBJ_MUTEX_FLAGS) = (uint32_t)guest_type & 0xff;
  ctx->rax = 0;
  SHIM_RETURN();
}

// One lock-family call on the mutex named by ctx->rdi; with PS4_MUTEX_STATS
// it times the lookup and the whole call.
typedef struct GmCall {
  MStat *s;
  uint64_t t0, t1;
} GmCall;

static inline GuestMutex *gm_call_begin(GuestContext *ctx, GmCall *c) {
  c->s = mstat_slot();
  c->t0 = mstat_now();
  GuestMutex *m = gm_lookup(ctx, ctx->rdi);
  c->t1 = mstat_now();
  return m;
}

static inline void gm_call_end(const GmCall *c) {
  if (!g_mstat_detail) return;
  MSTAT_ADD(c->s, lookup, c->t1 - c->t0);
  MSTAT_ADD(c->s, shim, mach_absolute_time() - c->t0);
}

void shim_pthread_mutex_lock(GuestContext *ctx) {
  GmCall c;
  GuestMutex *m = gm_call_begin(ctx, &c);
  ctx->rax = (uint64_t)gm_lock(m, guest_owner_id(ctx), NULL, c.s, c.t1);
  MSTAT_ADD(c.s, locks, 1);
  gm_call_end(&c);
  SHIM_RETURN();
}

void shim_pthread_mutex_trylock(GuestContext *ctx) {
  GmCall c;
  GuestMutex *m = gm_call_begin(ctx, &c);
  ctx->rax = (uint64_t)gm_trylock(m, guest_owner_id(ctx), c.s, c.t1);
  MSTAT_ADD(c.s, trylocks, 1);
  gm_call_end(&c);
  SHIM_RETURN();
}

void shim_pthread_mutex_timedlock(GuestContext *ctx) {
  GmCall c;
  GuestMutex *m = gm_call_begin(ctx, &c);
  uint64_t abstime_addr = ctx->rsi;
  struct timespec ts;
  if (abstime_addr) {
    uint64_t *guest_ts = (uint64_t *)(ctx->mem_base + abstime_addr);
    ts.tv_sec = (time_t)guest_ts[0];
    ts.tv_nsec = (long)guest_ts[1];
  }
  ctx->rax = (uint64_t)gm_lock(m, guest_owner_id(ctx), abstime_addr ? &ts : NULL, c.s, c.t1);
  MSTAT_ADD(c.s, locks, 1);
  gm_call_end(&c);
  SHIM_RETURN();
}

void shim_pthread_mutex_unlock(GuestContext *ctx) {
  GmCall c;
  GuestMutex *m = gm_call_begin(ctx, &c);
  ctx->rax = (uint64_t)gm_unlock(m, guest_owner_id(ctx), c.s, c.t1);
  gm_call_end(&c);
  SHIM_RETURN();
}

void shim_pthread_mutex_destroy(GuestContext *ctx) {
  uint64_t handle = sync_handle_take(ctx, ctx->rdi);
  const uint64_t id = gm_id(handle);
  GuestMutex *m = NULL;
  pthread_mutex_lock(&g_mutex_map_lock);
  if (id < MTAB_IDS) {
    MtabEntry *page = atomic_load_explicit(&g_mtab[id >> MTAB_PAGE_BITS], memory_order_relaxed);
    if (page) m = atomic_exchange_explicit(&page[id & ((1u << MTAB_PAGE_BITS) - 1)], NULL, memory_order_relaxed);
  } else {
    uint64_t bucket = sync_obj_hash(handle) % MUTEX_MAP_SIZE;
    for (MutexNode **curr = &g_mutex_map[bucket]; *curr; curr = &(*curr)->next) {
      if ((*curr)->guest_addr == handle) {
        MutexNode *to_free = *curr;
        *curr = to_free->next;
        m = to_free->gm;
        free(to_free);
        break;
      }
    }
  }
  if (m) {
    m->next_free = g_mutex_free;
    g_mutex_free = m;
  }
  pthread_mutex_unlock(&g_mutex_map_lock);
  sync_obj_free(handle);
  ctx->rax = 0;
  SHIM_RETURN();
}

void ps4_sync_print_stats(int frames) {
  if (!g_mticks_per_us || frames <= 0) return;
  const double tpu = g_mticks_per_us, fr = frames;
  uint64_t locks = 0, trylocks = 0, cont = 0, spun = 0, sleeps = 0, futile = 0, cwaits = 0;
  uint64_t wait = 0, wake = 0, wakes = 0, lookup = 0, shim = 0, lk = 0, lk_cont = 0;
  uint64_t hist[MSTAT_BUCKETS] = {0};
  struct {
    const char *name;
    uint64_t wait, cont;
  } top[6] = {{0}};
  pthread_mutex_lock(&g_mstat_lock);
  const int used = g_mstat_used;
  pthread_mutex_unlock(&g_mstat_lock);
  for (int i = 0; i < used; i++) {
    MStat *s = &g_mstat[i];
    locks += atomic_exchange(&s->locks, 0);
    trylocks += atomic_exchange(&s->trylocks, 0);
    const uint64_t c = atomic_exchange(&s->contended, 0);
    cont += c;
    spun += atomic_exchange(&s->spun, 0);
    sleeps += atomic_exchange(&s->sleeps, 0);
    futile += atomic_exchange(&s->futile, 0);
    cwaits += atomic_exchange(&s->cond_waits, 0);
    const uint64_t w = atomic_exchange(&s->wait, 0);
    wait += w;
    wake += atomic_exchange(&s->wake, 0);
    wakes += atomic_exchange(&s->wakes, 0);
    lookup += atomic_exchange(&s->lookup, 0);
    shim += atomic_exchange(&s->shim, 0);
    lk += atomic_exchange(&s->lk, 0);
    lk_cont += atomic_exchange(&s->lk_cont, 0);
    for (int b = 0; b < MSTAT_BUCKETS; b++) hist[b] += atomic_exchange(&s->hist[b], 0);
    for (int k = 0; k < 6; k++) {
      if (w <= top[k].wait) continue;
      memmove(&top[k + 1], &top[k], (5 - k) * sizeof top[0]);
      top[k].name = s->name;
      top[k].wait = w;
      top[k].cont = c;
      break;
    }
  }
  int p50 = 0, p95 = 0, p99 = 0;
  if (cont) {
    uint64_t acc = 0;
    p50 = p95 = p99 = -1;
    for (int b = 0; b < MSTAT_BUCKETS; b++) {
      acc += hist[b];
      if (p50 < 0 && acc * 100 >= cont * 50) p50 = b;
      if (p95 < 0 && acc * 100 >= cont * 95) p95 = b;
      if (p99 < 0 && acc * 100 >= cont * 99) p99 = b;
    }
  }
  char detail[160] = "";
  if (g_mstat_detail)
    snprintf(detail, sizeof detail, "; shims %.2f ms (lookup %.2f ms)", shim / tpu / 1000.0 / fr,
             lookup / tpu / 1000.0 / fr);
  fprintf(stderr, "[ps4-mutex] per frame: %.0f locks, %.0f contended (%.0f spun, %.0f sleeps, %.0f futile wakes), "
                  "%.0f trylocks, %.0f cond waits; contended wait total %.2f ms, avg %.1f us p50 %d p95 %d p99 %d%s us; "
                  "wake avg %.1f us; host-lock contention %.2f ms in %.0f%s\n",
          locks / fr, cont / fr, spun / fr, sleeps / fr, futile / fr, trylocks / fr, cwaits / fr,
          wait / tpu / 1000.0 / fr, cont ? wait / tpu / cont : 0.0, p50, p95, p99,
          p99 >= MSTAT_BUCKETS - 1 ? "+" : "", wakes ? wake / tpu / wakes : 0.0, lk / tpu / 1000.0 / fr,
          lk_cont / fr, detail);
  char line[512];
  int n = 0;
  for (int k = 0; k < 6 && top[k].name && top[k].wait; k++)
    n += snprintf(line + n, sizeof line - n, "%s%s %.2f ms/%.0f", k ? ", " : "", top[k].name,
                  top[k].wait / tpu / 1000.0 / fr, top[k].cont / fr);
  if (n) fprintf(stderr, "[ps4-mutex] waiting threads (ms/contended per frame): %s\n", line);
  if (g_mstat_detail) {
    static uint64_t prev[RELOCK_TYPES];
    uint64_t cur[RELOCK_TYPES];
    ps4_sync_relocks_by_type(cur);
    fprintf(stderr, "[ps4-mutex] relocks by the owner per frame: type 0 (none set) %.1f, 1 ERRORCHECK %.1f, "
                    "2 RECURSIVE %.1f, 3 NORMAL %.1f, 4 ADAPTIVE_NP %.1f, other %.1f\n",
            (cur[0] - prev[0]) / fr, (cur[1] - prev[1]) / fr, (cur[2] - prev[2]) / fr, (cur[3] - prev[3]) / fr,
            (cur[4] - prev[4]) / fr, (cur[5] - prev[5]) / fr);
    memcpy(prev, cur, sizeof prev);
  }

  // Busiest mutexes by contended wait.
  struct {
    uint64_t var, locks, cont, wait, hold;
  } mt[4] = {{0}};
  pthread_mutex_lock(&g_mutex_map_lock);
  for (GuestMutex *m = g_mutex_all; m; m = m->next_all) {
    const uint64_t l = __atomic_exchange_n(&m->st_locks, 0, __ATOMIC_RELAXED);
    const uint64_t c = __atomic_exchange_n(&m->st_cont, 0, __ATOMIC_RELAXED);
    const uint64_t w = __atomic_exchange_n(&m->st_wait, 0, __ATOMIC_RELAXED);
    const uint64_t h = __atomic_exchange_n(&m->st_hold, 0, __ATOMIC_RELAXED);
    for (int k = 0; k < 4; k++) {
      if (w <= mt[k].wait) continue;
      memmove(&mt[k + 1], &mt[k], (3 - k) * sizeof mt[0]);
      mt[k].var = __atomic_load_n(&m->var, __ATOMIC_RELAXED);
      mt[k].locks = l;
      mt[k].cont = c;
      mt[k].wait = w;
      mt[k].hold = h;
      break;
    }
  }
  pthread_mutex_unlock(&g_mutex_map_lock);
  n = 0;
  for (int k = 0; k < 4 && mt[k].wait; k++) {
    n += snprintf(line + n, sizeof line - n, "%s0x%llx %.0f locks %.0f contended %.2f ms wait", k ? "; " : "",
                  (unsigned long long)mt[k].var, mt[k].locks / fr, mt[k].cont / fr, mt[k].wait / tpu / 1000.0 / fr);
    if (g_mstat_detail)
      n += snprintf(line + n, sizeof line - n, ", hold %.2f ms (avg %.2f us)", mt[k].hold / tpu / 1000.0 / fr,
                    mt[k].locks ? mt[k].hold / tpu / mt[k].locks : 0.0);
  }
  if (n) fprintf(stderr, "[ps4-mutex] top mutexes per frame: %s\n", line);
}

// The attribute starts with type 0, which gm_relock keeps recursive; on Orbis a fresh
// attribute is ERRORCHECK (a known divergence, see gm_relock).
void shim_pthread_mutexattr_init(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  if (attr_addr) {
    *(uint32_t *)(ctx->mem_base + attr_addr) = 0;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_settype(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  int type = (int)ctx->rsi;
  if (attr_addr) {
    *(uint32_t *)(ctx->mem_base + attr_addr) = (uint32_t)type;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_destroy(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

// Condition variables
#define COND_MAP_SIZE 1024
typedef struct CondNode {
  uint64_t guest_addr;
  pthread_cond_t host_cond;
  _Atomic(GuestMutex *) m; // mutex of the latest wait (see gm_cond_wait)
  struct CondNode *next;
} CondNode;

static CondNode *g_cond_map[COND_MAP_SIZE] = {0};
static pthread_mutex_t g_cond_map_lock = PTHREAD_MUTEX_INITIALIZER;

static CondNode *get_host_cond(uint64_t guest_addr) {
  uint64_t bucket = sync_obj_hash(guest_addr) % COND_MAP_SIZE;
  pthread_mutex_lock(&g_cond_map_lock);
  CondNode *curr = g_cond_map[bucket];
  while (curr) {
    if (curr->guest_addr == guest_addr) {
      pthread_mutex_unlock(&g_cond_map_lock);
      return curr;
    }
    curr = curr->next;
  }
  CondNode *node = (CondNode *)calloc(1, sizeof(CondNode));
  node->guest_addr = guest_addr;
  pthread_cond_init(&node->host_cond, NULL);
  node->next = g_cond_map[bucket];
  g_cond_map[bucket] = node;
  pthread_mutex_unlock(&g_cond_map_lock);
  return node;
}

// Release m fully, wait on cn, then reacquire with the saved recursion depth.
// The waiter holds m->lk from before it gives up m until it sleeps on the
// condvar. With atomic mutexes the next owner can take m without lk, so a
// signal takes the lk of the mutex last used with the condvar (cn->m): a
// wake-up sent after the next owner took m cannot slip in before we sleep.
static int gm_cond_wait(GuestMutex *m, CondNode *cn, uint64_t me, const struct timespec *abs) {
  MStat *s = mstat_slot();
  MSTAT_ADD(s, cond_waits, 1);
  if (GM_OWNER(m) != me) return EPERM;
  const uint32_t depth = m->count;
  gm_lk_lock(m, s);
  atomic_store_explicit(&cn->m, m, memory_order_relaxed);
  m->count = 0;
  gm_released(m, mstat_now());
  if (g_mutex_legacy) {
    GM_SET_OWNER(m, 0);
    pthread_cond_signal(&m->cv);
  } else {
    atomic_store_explicit(&m->owner, 0, memory_order_seq_cst);
    if (atomic_load_explicit(&m->waiters, memory_order_seq_cst)) {
      m->rel_t = mach_absolute_time();
      pthread_cond_signal(&m->cv);
    }
  }
  int r = abs ? pthread_cond_timedwait(&cn->host_cond, &m->lk, abs) : pthread_cond_wait(&cn->host_cond, &m->lk);
  atomic_fetch_add_explicit(&m->waiters, 1, memory_order_seq_cst);
  if (g_mutex_legacy) {
    while (GM_OWNER(m) != 0) pthread_cond_wait(&m->cv, &m->lk);
    GM_SET_OWNER(m, me);
  } else {
    while (!gm_cas(m, me, memory_order_seq_cst)) pthread_cond_wait(&m->cv, &m->lk);
  }
  atomic_fetch_sub_explicit(&m->waiters, 1, memory_order_relaxed);
  m->count = depth;
  m->acq_t = mstat_now();
  pthread_mutex_unlock(&m->lk);
  return r;
}

static int gc_signal(CondNode *cn, bool all) {
  GuestMutex *m = g_mutex_legacy ? NULL : atomic_load_explicit(&cn->m, memory_order_acquire);
  if (m) pthread_mutex_lock(&m->lk);
  int r = all ? pthread_cond_broadcast(&cn->host_cond) : pthread_cond_signal(&cn->host_cond);
  if (m) pthread_mutex_unlock(&m->lk);
  return r;
}

void shim_pthread_cond_init(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  (void)get_host_cond(sync_handle(ctx, cond_addr, 1));
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_cond_wait(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t mtx_addr = ctx->rsi;
  CondNode *c = get_host_cond(sync_handle(ctx, cond_addr, 0));
  GuestMutex *m = get_host_mutex(sync_handle(ctx, mtx_addr, 0));
  ctx->rax = (uint64_t)gm_cond_wait(m, c, guest_owner_id(ctx), NULL);
  SHIM_RETURN();
}

void shim_pthread_cond_timedwait(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t mtx_addr = ctx->rsi;
  uint64_t abstime_addr = ctx->rdx;
  CondNode *c = get_host_cond(sync_handle(ctx, cond_addr, 0));
  GuestMutex *m = get_host_mutex(sync_handle(ctx, mtx_addr, 0));

  struct timespec ts;
  if (abstime_addr) {
    uint64_t *guest_ts = (uint64_t *)(ctx->mem_base + abstime_addr);
    ts.tv_sec = (time_t)guest_ts[0];
    ts.tv_nsec = (long)guest_ts[1];
  }
  ctx->rax = (uint64_t)gm_cond_wait(m, c, guest_owner_id(ctx), abstime_addr ? &ts : NULL);
  SHIM_RETURN();
}

void shim_pthread_cond_signal(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  CondNode *c = get_host_cond(sync_handle(ctx, cond_addr, 0));
  ctx->rax = (uint64_t)gc_signal(c, false);
  SHIM_RETURN();
}

void shim_pthread_cond_broadcast(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  CondNode *c = get_host_cond(sync_handle(ctx, cond_addr, 0));
  ctx->rax = (uint64_t)gc_signal(c, true);
  SHIM_RETURN();
}

void shim_pthread_cond_destroy(GuestContext *ctx) {
  uint64_t cond_addr = sync_handle_take(ctx, ctx->rdi);
  uint64_t bucket = sync_obj_hash(cond_addr) % COND_MAP_SIZE;
  pthread_mutex_lock(&g_cond_map_lock);
  CondNode **curr = &g_cond_map[bucket];
  while (*curr) {
    if ((*curr)->guest_addr == cond_addr) {
      CondNode *to_free = *curr;
      *curr = (*curr)->next;
      pthread_cond_destroy(&to_free->host_cond);
      free(to_free);
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_cond_map_lock);
  sync_obj_free(cond_addr);
  ctx->rax = 0;
  SHIM_RETURN();
}

// RWLock
#define RWLOCK_MAP_SIZE 1024
typedef struct RwlockNode {
  uint64_t guest_addr;
  pthread_rwlock_t host_rwlock;
  struct RwlockNode *next;
} RwlockNode;

static RwlockNode *g_rwlock_map[RWLOCK_MAP_SIZE] = {0};
static pthread_mutex_t g_rwlock_map_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_rwlock_t *get_host_rwlock(uint64_t guest_addr) {
  uint64_t bucket = sync_obj_hash(guest_addr) % RWLOCK_MAP_SIZE;
  pthread_mutex_lock(&g_rwlock_map_lock);
  RwlockNode *curr = g_rwlock_map[bucket];
  while (curr) {
    if (curr->guest_addr == guest_addr) {
      pthread_mutex_unlock(&g_rwlock_map_lock);
      return &curr->host_rwlock;
    }
    curr = curr->next;
  }
  RwlockNode *node = (RwlockNode *)calloc(1, sizeof(RwlockNode));
  node->guest_addr = guest_addr;
  pthread_rwlock_init(&node->host_rwlock, NULL);
  node->next = g_rwlock_map[bucket];
  g_rwlock_map[bucket] = node;
  pthread_mutex_unlock(&g_rwlock_map_lock);
  return &node->host_rwlock;
}

void shim_pthread_rwlock_rdlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(sync_handle(ctx, rw_addr, 0));
  ctx->rax = (uint64_t)pthread_rwlock_rdlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_wrlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(sync_handle(ctx, rw_addr, 0));
  ctx->rax = (uint64_t)pthread_rwlock_wrlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_tryrdlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(sync_handle(ctx, rw_addr, 0));
  ctx->rax = (uint64_t)pthread_rwlock_tryrdlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_trywrlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(sync_handle(ctx, rw_addr, 0));
  ctx->rax = (uint64_t)pthread_rwlock_trywrlock(rw);
  SHIM_RETURN();
}

void shim_scePthreadRwlockTryrdlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(sync_handle(ctx, rw_addr, 0));
  int ret = pthread_rwlock_tryrdlock(rw);
  if (ret != 0) {
    ctx->rax = (ret == EBUSY) ? 0x80020010 : (0x80020000 | (uint32_t)ret);
  } else {
    ctx->rax = 0;
  }
  SHIM_RETURN();
}

void shim_scePthreadRwlockTrywrlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(sync_handle(ctx, rw_addr, 0));
  int ret = pthread_rwlock_trywrlock(rw);
  if (ret != 0) {
    ctx->rax = (ret == EBUSY) ? 0x80020010 : (0x80020000 | (uint32_t)ret);
  } else {
    ctx->rax = 0;
  }
  SHIM_RETURN();
}

void shim_pthread_rwlock_unlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(sync_handle(ctx, rw_addr, 0));
  ctx->rax = (uint64_t)pthread_rwlock_unlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_init(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  (void)get_host_rwlock(sync_handle(ctx, rw_addr, 1));
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_rwlock_destroy(GuestContext *ctx) {
  uint64_t rw_addr = sync_handle_take(ctx, ctx->rdi);
  uint64_t bucket = sync_obj_hash(rw_addr) % RWLOCK_MAP_SIZE;
  pthread_mutex_lock(&g_rwlock_map_lock);
  RwlockNode **curr = &g_rwlock_map[bucket];
  while (*curr) {
    if ((*curr)->guest_addr == rw_addr) {
      RwlockNode *to_free = *curr;
      *curr = (*curr)->next;
      pthread_rwlock_destroy(&to_free->host_rwlock);
      free(to_free);
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_rwlock_map_lock);
  sync_obj_free(rw_addr);
  ctx->rax = 0;
  SHIM_RETURN();
}

static pthread_mutex_t g_once_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_once_cond = PTHREAD_COND_INITIALIZER;

void shim_pthread_once(GuestContext *ctx) {
  uint64_t once_control_addr = ctx->rdi;
  uint64_t init_routine_addr = ctx->rsi;
  if (!once_control_addr) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  int *ctrl = (int *)(ctx->mem_base + once_control_addr);

  pthread_mutex_lock(&g_once_mutex);
  while (*ctrl == 2) { // 2 = InProgress
    pthread_cond_wait(&g_once_cond, &g_once_mutex);
  }
  if (*ctrl == 1) { // 1 = Done
    pthread_mutex_unlock(&g_once_mutex);
    ctx->rax = 0;
    SHIM_RETURN();
  }
  *ctrl = 2; // InProgress
  pthread_mutex_unlock(&g_once_mutex);

  ctx->rsp -= 8;
  MEM_U64(ctx->rsp) = 0xdeadbeefULL;
  recomp_dispatch(ctx, init_routine_addr);

  pthread_mutex_lock(&g_once_mutex);
  *ctrl = 1; // Done
  pthread_cond_broadcast(&g_once_cond);
  pthread_mutex_unlock(&g_once_mutex);

  ctx->rax = 0;
  SHIM_RETURN();
}

void ps4_sync_destroy(void) {
  pthread_mutex_lock(&g_mutex_map_lock);
  for (int i = 0; i < MUTEX_MAP_SIZE; i++) {
    MutexNode *curr = g_mutex_map[i];
    while (curr) {
      MutexNode *next = curr->next;
      free(curr);
      curr = next;
    }
    g_mutex_map[i] = NULL;
  }
  for (int i = 0; i < MTAB_PAGES; i++) free(atomic_exchange_explicit(&g_mtab[i], NULL, memory_order_relaxed));
  for (GuestMutex *m = g_mutex_all, *next; m; m = next) {
    next = m->next_all;
    pthread_mutex_destroy(&m->lk);
    pthread_cond_destroy(&m->cv);
    free(m);
  }
  g_mutex_all = g_mutex_free = NULL;
  pthread_mutex_unlock(&g_mutex_map_lock);

  pthread_mutex_lock(&g_cond_map_lock);
  for (int i = 0; i < COND_MAP_SIZE; i++) {
    CondNode *curr = g_cond_map[i];
    while (curr) {
      CondNode *next = curr->next;
      pthread_cond_destroy(&curr->host_cond);
      free(curr);
      curr = next;
    }
    g_cond_map[i] = NULL;
  }
  pthread_mutex_unlock(&g_cond_map_lock);

  pthread_mutex_lock(&g_rwlock_map_lock);
  for (int i = 0; i < RWLOCK_MAP_SIZE; i++) {
    RwlockNode *curr = g_rwlock_map[i];
    while (curr) {
      RwlockNode *next = curr->next;
      pthread_rwlock_destroy(&curr->host_rwlock);
      free(curr);
      curr = next;
    }
    g_rwlock_map[i] = NULL;
  }
  pthread_mutex_unlock(&g_rwlock_map_lock);
}

// scePthreadMutex shims
void shim_scePthreadMutexInit(GuestContext *ctx) {
  shim_pthread_mutex_init(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadMutexLock(GuestContext *ctx) {
  shim_pthread_mutex_lock(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadMutexTrylock(GuestContext *ctx) {
  shim_pthread_mutex_trylock(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadMutexTimedlock(GuestContext *ctx) {
  uint64_t usec = ctx->rsi;
  GmCall c;
  GuestMutex *m = gm_call_begin(ctx, &c);
  if (usec == 0) {
    ctx->rax = (uint64_t)gm_trylock(m, guest_owner_id(ctx), c.s, c.t1);
    MSTAT_ADD(c.s, trylocks, 1);
  } else {
    struct timespec ts;
    abs_from_usec(&ts, usec);
    ctx->rax = (uint64_t)gm_lock(m, guest_owner_id(ctx), &ts, c.s, c.t1);
    MSTAT_ADD(c.s, locks, 1);
  }
  gm_call_end(&c);
  SCE_PTHREAD_ERR(ctx);
  SHIM_RETURN();
}

void shim_scePthreadMutexUnlock(GuestContext *ctx) {
  shim_pthread_mutex_unlock(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadMutexDestroy(GuestContext *ctx) {
  shim_pthread_mutex_destroy(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadMutexattrInit(GuestContext *ctx) {
  shim_pthread_mutexattr_init(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadMutexattrDestroy(GuestContext *ctx) {
  shim_pthread_mutexattr_destroy(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadMutexattrSettype(GuestContext *ctx) {
  shim_pthread_mutexattr_settype(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadMutexattrSetprotocol(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_setprotocol(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadOnce(GuestContext *ctx) {
  shim_pthread_once(ctx);
  SCE_PTHREAD_ERR(ctx);
}

// scePthreadCond shims
void shim_scePthreadCondInit(GuestContext *ctx) {
  shim_pthread_cond_init(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadCondDestroy(GuestContext *ctx) {
  shim_pthread_cond_destroy(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadCondSignal(GuestContext *ctx) {
  shim_pthread_cond_signal(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadCondBroadcast(GuestContext *ctx) {
  shim_pthread_cond_broadcast(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadCondWait(GuestContext *ctx) {
  shim_pthread_cond_wait(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadCondTimedwait(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t mtx_addr = ctx->rsi;
  uint64_t usec = ctx->rdx;
  CondNode *c = get_host_cond(sync_handle(ctx, cond_addr, 0));
  GuestMutex *m = get_host_mutex(sync_handle(ctx, mtx_addr, 0));
  struct timespec ts;
  abs_from_usec(&ts, usec);
  ctx->rax = (uint64_t)gm_cond_wait(m, c, guest_owner_id(ctx), &ts);
  SCE_PTHREAD_ERR(ctx); // ETIMEDOUT -> SCE_KERNEL_ERROR_ETIMEDOUT (0x8002003c)
  SHIM_RETURN();
}

// Condition attributes carry no state the host implementation uses.
void shim_pthread_condattr_init(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_condattr_destroy(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadCondattrInit(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadCondattrDestroy(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

// C++ ABI static guard shims (matching Itanium C++ ABI with recursion prevention)
static pthread_mutex_t g_cxa_guard_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cxa_guard_cond = PTHREAD_COND_INITIALIZER;

void shim_cxa_guard_acquire(GuestContext *ctx) {
  uint64_t guard_addr = ctx->rdi;
  if (!guard_addr) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  uint8_t *guard = (uint8_t *)(ctx->mem_base + guard_addr);
  // Fast path: if byte 0 is set, initialization is already complete.
  if (*guard & 1) {
    ctx->rax = 0;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&g_cxa_guard_mutex);
  while (1) {
    if (*guard & 1) {
      pthread_mutex_unlock(&g_cxa_guard_mutex);
      ctx->rax = 0;
      SHIM_RETURN();
    }
    // If byte 1 is set, initialization is in progress
    if (guard[1] != 0) {
      // Check if held by current thread (recursive static initialization).
      // We store thread_id in bytes 4..7
      uint32_t holder = *(uint32_t *)(guard + 4);
      uint32_t my_tid = (uint32_t)(ctx->thread_id ? ctx->thread_id : 1);
      if (holder == my_tid) {
        // Recursive static initialization detected on the same thread!
        // Returning 0 avoids recursive deadlock and allows caller to proceed.
        pthread_mutex_unlock(&g_cxa_guard_mutex);
        ctx->rax = 0;
        SHIM_RETURN();
      }
      // Held by another thread: wait for release/abort
      pthread_cond_wait(&g_cxa_guard_cond, &g_cxa_guard_mutex);
      continue;
    }

    // Mark as in-progress
    guard[1] = 1;
    uint32_t my_tid = (uint32_t)(ctx->thread_id ? ctx->thread_id : 1);
    *(uint32_t *)(guard + 4) = my_tid;
    pthread_mutex_unlock(&g_cxa_guard_mutex);
    ctx->rax = 1; // Proceed with initialization
    SHIM_RETURN();
  }
}

void shim_cxa_guard_release(GuestContext *ctx) {
  uint64_t guard_addr = ctx->rdi;
  if (guard_addr) {
    uint8_t *guard = (uint8_t *)(ctx->mem_base + guard_addr);
    pthread_mutex_lock(&g_cxa_guard_mutex);
    *guard = 1; // Mark completed
    guard[1] = 0; // Clear in-progress
    *(uint32_t *)(guard + 4) = 0;
    pthread_cond_broadcast(&g_cxa_guard_cond);
    pthread_mutex_unlock(&g_cxa_guard_mutex);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_cxa_guard_abort(GuestContext *ctx) {
  uint64_t guard_addr = ctx->rdi;
  if (guard_addr) {
    uint8_t *guard = (uint8_t *)(ctx->mem_base + guard_addr);
    pthread_mutex_lock(&g_cxa_guard_mutex);
    guard[1] = 0; // Clear in-progress
    *(uint32_t *)(guard + 4) = 0;
    pthread_cond_broadcast(&g_cxa_guard_cond);
    pthread_mutex_unlock(&g_cxa_guard_mutex);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

