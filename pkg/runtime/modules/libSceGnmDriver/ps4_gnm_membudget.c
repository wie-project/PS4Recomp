// GPU memory accounting and budget decisions (see ps4_gnm_membudget.h).
#include "ps4_gnm_membudget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/sysctl.h>
#endif

uint64_t ps4_gpumem_charged(const ps4_gpumem_sample *s) {
    const uint64_t own = s->device > s->nocopy_sum ? s->device - s->nocopy_sum : 0;
    return own + s->nocopy_distinct;
}

// Parses a positive decimal number; returns 0 for NULL, empty, junk or overflow.
static uint64_t parse_u64(const char *v) {
    if (!v || !*v) return 0;
    uint64_t n = 0;
    for (const char *c = v; *c; ++c) {
        if (*c < '0' || *c > '9') return 0;
        if (n > (UINT64_MAX - 9) / 10) return 0;
        n = n * 10 + (uint64_t)(*c - '0');
    }
    return n;
}

void ps4_gpumem_policy_init(ps4_gpumem_policy *p, uint64_t mem_total, const char *budget_pct,
                            const char *budget_mb, const char *idle_s) {
    memset(p, 0, sizeof(*p));
    p->mem_total = mem_total;
    const uint64_t mb = parse_u64(budget_mb);
    if (mb && mb <= (UINT64_MAX >> 20)) {
        p->budget = mb << 20;
    } else {
        uint64_t pct = parse_u64(budget_pct);
        if (!pct) pct = 40; // unset or not a number
        if (pct < 5) pct = 5;
        if (pct > 90) pct = 90;
        // hw.memsize unknown: no budget rather than one of 0 that would always be exceeded
        p->budget = mem_total ? mem_total / 100 * pct : UINT64_MAX;
    }
    p->nocopy_warn = 1ull << 30;
    p->nocopy_max = 2ull << 30;
    const uint64_t idle = idle_s ? parse_u64(idle_s) : 30;
    // "0" disables idle release; junk keeps the default
    const int idle_zero = idle_s && idle_s[0] == '0' && idle_s[1] == 0;
    p->idle_ns = idle_zero ? 0 : (idle ? idle : 30) * 1000000000ull;
    p->press_ns = 250000000ull;
}

void ps4_texcache_policy_init(ps4_texcache_policy *p, const ps4_gpumem_policy *gp, const char *tex_mb,
                              const char *keep_ms) {
    memset(p, 0, sizeof(*p));
    const uint64_t mb = parse_u64(tex_mb);
    const int mb_zero = tex_mb && tex_mb[0] == '0' && tex_mb[1] == 0;
    if (mb_zero) p->budget = UINT64_MAX;
    else if (mb && mb <= (UINT64_MAX >> 20)) p->budget = mb << 20;
    else p->budget = gp->budget == UINT64_MAX ? UINT64_MAX : gp->budget / 12;
    p->low_water = p->budget == UINT64_MAX ? UINT64_MAX : p->budget / 10 * 9;
    uint64_t keep = keep_ms ? parse_u64(keep_ms) : 2000;
    if (!keep) keep = 2000;
    if (keep < 100) keep = 100;
    p->keep_ns = keep * 1000000ull;
    p->press_ns = gp->press_ns < 100000000ull ? 100000000ull : gp->press_ns;
}

int ps4_texcache_evict_due(const ps4_texcache_policy *p, uint64_t sampled_bytes, int gpu_over) {
    return gpu_over || sampled_bytes > p->budget;
}

static int texcache_lru_cmp(const void *a, const void *b) {
    const ps4_texcache_item *x = (const ps4_texcache_item *)a, *y = (const ps4_texcache_item *)b;
    if (x->last_ns != y->last_ns) return x->last_ns < y->last_ns ? -1 : 1;
    if (x->last_seq != y->last_seq) return x->last_seq < y->last_seq ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

uint64_t ps4_texcache_select(ps4_texcache_item *items, uint64_t n, uint64_t total, const ps4_texcache_policy *p,
                             int gpu_over, uint64_t now_ns, uint64_t completed_seq, uint64_t *freed) {
    *freed = 0;
    if (!ps4_texcache_evict_due(p, total, gpu_over)) return 0;
    const uint64_t min_age = gpu_over ? p->press_ns : p->keep_ns;
    // candidates to the front
    uint64_t m = 0;
    for (uint64_t i = 0; i < n; ++i) {
        const ps4_texcache_item it = items[i];
        if (it.last_seq > completed_seq) continue;            // a command buffer may still use it
        if (now_ns < it.last_ns || now_ns - it.last_ns < min_age) continue; // used recently
        items[i] = items[m];
        items[m++] = it;
    }
    qsort(items, (size_t)m, sizeof(*items), texcache_lru_cmp);
    uint64_t k = 0;
    while (k < m && (gpu_over || (*freed < total && total - *freed > p->low_water))) *freed += items[k++].bytes;
    return k;
}

enum ps4_gpumem_admit ps4_gpumem_admit_nocopy(const ps4_gpumem_policy *p, uint64_t length, int has_holes) {
    if (length > p->nocopy_max) return PS4_GPUMEM_REFUSE_TOO_LARGE;
    if (has_holes) return PS4_GPUMEM_REFUSE_HOLES;
    return length > p->nocopy_warn ? PS4_GPUMEM_ADMIT_LARGE : PS4_GPUMEM_ADMIT;
}

int ps4_gpumem_refused_copy(enum ps4_gpumem_admit adm, int written, uint64_t size) {
    return adm == PS4_GPUMEM_REFUSE_HOLES && !written && size <= PS4_GPUMEM_COPY_MAX;
}

int ps4_gpumem_should_release(const ps4_gpumem_policy *p, uint64_t now_ns, uint64_t last_use_ns,
                              int over_budget) {
    if (now_ns < last_use_ns) return 0;
    const uint64_t idle = now_ns - last_use_ns;
    if (p->idle_ns && idle >= p->idle_ns) return 1;
    return over_budget && idle >= p->press_ns;
}

int ps4_gpumem_budget_update(ps4_gpumem_budget_state *st, const ps4_gpumem_policy *p, uint64_t charged) {
    if (!st->over && charged > p->budget) {
        st->over = 1;
        st->times_over++;
        return 1;
    }
    if (st->over && charged < p->budget / 10 * 9) {
        st->over = 0;
        return -1;
    }
    return 0;
}

int ps4_gpumem_purge_due(ps4_gpumem_purge_state *st, int over, uint64_t charged) {
    if (!over) {
        st->futile = 0;
        st->have = 0;
        return 0;
    }
    if (st->have) {
        if (charged < st->last_charged) st->futile = 0;
        else st->futile++;
    }
    if (st->futile >= 10) {
        st->last_charged = charged; // a later drop (e.g. textures freed) re-enables purging
        return 0;
    }
    st->have = 1;
    st->last_charged = charged;
    return 1;
}

int ps4_gpumem_log_due(ps4_gpumem_log_state *st, const ps4_gpumem_sample *s, uint64_t period_ns,
                       uint64_t step) {
    int due = !st->have || s->t_ns - st->t_ns >= period_ns;
    const uint64_t now[4] = {s->wired, s->footprint, s->device, s->nocopy_distinct};
    const uint64_t was[4] = {st->last.wired, st->last.footprint, st->last.device, st->last.nocopy_distinct};
    for (int i = 0; i < 4 && !due; ++i) due = now[i] >= was[i] + step;
    if (!due) return 0;
    st->have = 1;
    st->t_ns = s->t_ns;
    st->last = *s;
    return 1;
}

int ps4_gpumem_windows_init(ps4_gpumem_windows *w, uint64_t mem_size, uint64_t win_size) {
    memset(w, 0, sizeof(*w));
    if (!win_size) return 0;
    w->win_size = win_size;
    w->nwin = (mem_size + win_size - 1) / win_size;
    w->refs = (uint16_t *)calloc(w->nwin ? w->nwin : 1, sizeof(uint16_t));
    return w->refs != NULL;
}

void ps4_gpumem_windows_free(ps4_gpumem_windows *w) {
    free(w->refs);
    memset(w, 0, sizeof(*w));
}

void ps4_gpumem_windows_add(ps4_gpumem_windows *w, uint64_t first_win, uint64_t n, uint64_t length) {
    uint64_t newly = 0;
    for (uint64_t i = first_win; i < first_win + n && i < w->nwin; ++i)
        if (w->refs[i]++ == 0) newly++;
    __atomic_store_n(&w->count, w->count + 1, __ATOMIC_RELAXED);
    __atomic_store_n(&w->sum, w->sum + length, __ATOMIC_RELAXED);
    __atomic_store_n(&w->distinct, w->distinct + newly * w->win_size, __ATOMIC_RELAXED);
}

void ps4_gpumem_windows_remove(ps4_gpumem_windows *w, uint64_t first_win, uint64_t n, uint64_t length) {
    uint64_t freed = 0;
    for (uint64_t i = first_win; i < first_win + n && i < w->nwin; ++i)
        if (w->refs[i] && --w->refs[i] == 0) freed++;
    __atomic_store_n(&w->count, w->count ? w->count - 1 : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&w->sum, w->sum > length ? w->sum - length : 0, __ATOMIC_RELAXED);
    const uint64_t fb = freed * w->win_size;
    __atomic_store_n(&w->distinct, w->distinct > fb ? w->distinct - fb : 0, __ATOMIC_RELAXED);
}

void ps4_gpumem_windows_read(const ps4_gpumem_windows *w, ps4_gpumem_sample *s) {
    s->nocopy_count = __atomic_load_n(&w->count, __ATOMIC_RELAXED);
    s->nocopy_sum = __atomic_load_n(&w->sum, __ATOMIC_RELAXED);
    s->nocopy_distinct = __atomic_load_n(&w->distinct, __ATOMIC_RELAXED);
}

#if defined(__APPLE__)
// Walks the VM regions over [p, p + len): fn gets each readable piece. Returns the bytes passed
// to fn; *hole is set when some part is unmapped or not readable.
static uint64_t walk_regions(const void *p, uint64_t len, int *hole,
                             void (*fn)(void *ctx, uint64_t offset, uint64_t n), void *ctx) {
    const mach_vm_address_t start = (mach_vm_address_t)(uintptr_t)p, end = start + len;
    mach_vm_address_t cur = start;
    uint64_t good = 0;
    *hole = 0;
    while (cur < end) {
        mach_vm_address_t addr = cur;
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj = MACH_PORT_NULL;
        const kern_return_t kr = mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64,
                                                (vm_region_info_t)&info, &count, &obj);
        if (kr != KERN_SUCCESS || addr >= end) { // nothing mapped from cur to the end
            *hole = 1;
            break;
        }
        if (addr > cur) *hole = 1; // unmapped gap before this region
        const mach_vm_address_t a = addr, b = addr + size < end ? addr + size : end;
        if (info.protection & VM_PROT_READ) {
            if (fn) fn(ctx, a - start, b - a);
            good += b - a;
        } else {
            *hole = 1;
        }
        cur = b;
    }
    return good;
}
#endif

int ps4_gpumem_range_has_holes(const void *p, uint64_t len) {
#if defined(__APPLE__)
    int hole = 0;
    walk_regions(p, len, &hole, NULL, NULL);
    return hole;
#else
    (void)p;
    (void)len;
    return 0;
#endif
}

uint64_t ps4_gpumem_for_each_readable(const void *p, uint64_t len,
                                      void (*fn)(void *ctx, uint64_t offset, uint64_t n), void *ctx) {
#if defined(__APPLE__)
    int hole = 0;
    return walk_regions(p, len, &hole, fn, ctx);
#else
    if (fn) fn(ctx, 0, len);
    return len;
#endif
}

uint64_t ps4_gpumem_wired_bytes(void) {
#if defined(__APPLE__)
    vm_statistics64_data_t st;
    mach_msg_type_number_t n = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&st, &n) != KERN_SUCCESS) return 0;
    return (uint64_t)st.wire_count * vm_kernel_page_size;
#else
    return 0;
#endif
}

uint64_t ps4_gpumem_footprint_bytes(void) {
#if defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
    return info.phys_footprint;
#else
    return 0;
#endif
}

uint64_t ps4_gpumem_mem_total(void) {
#if defined(__APPLE__)
    uint64_t v = 0;
    size_t sz = sizeof(v);
    if (sysctlbyname("hw.memsize", &v, &sz, NULL, 0) != 0) return 0;
    return v;
#else
    return 0;
#endif
}

void ps4_gpumem_format(char *buf, unsigned long size, const ps4_gpumem_sample *s, uint64_t wired_base,
                       const ps4_gpumem_policy *p) {
    const long long wired_delta = ((long long)s->wired - (long long)wired_base) >> 20;
    snprintf(buf, size,
             "charged %llu/%llu MiB (device %llu MiB incl. no-copy %llu MiB; no-copy buffers %llu covering %llu MiB); "
             "wired %llu MiB (%+lld since start); footprint %llu MiB",
             (unsigned long long)(ps4_gpumem_charged(s) >> 20), (unsigned long long)(p->budget >> 20),
             (unsigned long long)(s->device >> 20), (unsigned long long)(s->nocopy_sum >> 20),
             (unsigned long long)s->nocopy_count, (unsigned long long)(s->nocopy_distinct >> 20),
             (unsigned long long)(s->wired >> 20), wired_delta, (unsigned long long)(s->footprint >> 20));
}
