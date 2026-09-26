#include "recomp_runtime.h"

#include <signal.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#include <execinfo.h>
#endif

recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE] = {0};
_Thread_local GuestContext *g_current_ctx = NULL;
pthread_mutex_t g_heap_mutex = PTHREAD_MUTEX_INITIALIZER;

static void crash_handler(int sig, siginfo_t *si, void *ucontext) {
    if (g_current_ctx) {
        fprintf(stderr, "\nFATAL: Signal %d at host address %p (mem_base=%p)\n"
                        "RIP=0x%llx RSP=0x%llx RBP=0x%llx RAX=0x%llx RBX=0x%llx\n"
                        "RCX=0x%llx RDX=0x%llx RSI=0x%llx RDI=0x%llx\n"
                        "R8=0x%llx R9=0x%llx R10=0x%llx R11=0x%llx\n"
                        "R12=0x%llx R13=0x%llx R14=0x%llx R15=0x%llx\n",
                sig, si->si_addr, g_current_ctx->mem_base,
                (unsigned long long)g_current_ctx->rip,
                (unsigned long long)g_current_ctx->rsp,
                (unsigned long long)g_current_ctx->rbp,
                (unsigned long long)g_current_ctx->rax,
                (unsigned long long)g_current_ctx->rbx,
                (unsigned long long)g_current_ctx->rcx,
                (unsigned long long)g_current_ctx->rdx,
                (unsigned long long)g_current_ctx->rsi,
                (unsigned long long)g_current_ctx->rdi,
                (unsigned long long)g_current_ctx->r8,
                (unsigned long long)g_current_ctx->r9,
                (unsigned long long)g_current_ctx->r10,
                (unsigned long long)g_current_ctx->r11,
                (unsigned long long)g_current_ctx->r12,
                (unsigned long long)g_current_ctx->r13,
                (unsigned long long)g_current_ctx->r14,
                (unsigned long long)g_current_ctx->r15);
#if defined(__APPLE__) && defined(__arm64__)
        uint64_t host_pc = ((ucontext_t*)ucontext)->uc_mcontext->__ss.__pc;
        fprintf(stderr, "Host PC: 0x%llx\n", (unsigned long long)host_pc);
#endif
        void *callstack[64];
        int frames = backtrace(callstack, 64);
        fprintf(stderr, "Call stack (%d frames):\n", frames);
        backtrace_symbols_fd(callstack, frames, 2);
    } else {
        fprintf(stderr, "\nFATAL: Signal %d at host address %p (no guest context)\n", sig, si->si_addr);
        void *callstack[64];
        int frames = backtrace(callstack, 64);
        fprintf(stderr, "Call stack (%d frames):\n", frames);
        backtrace_symbols_fd(callstack, frames, 2);
    }
    fflush(stderr);
    exit(1);
}

void shim_unresolved_stub(GuestContext *ctx) {
    fprintf(stderr, "[ps4-recomp] WARN: Called unresolved function at RIP=0x%llx (RSP=0x%llx)\n",
            (unsigned long long)ctx->rip, (unsigned long long)ctx->rsp);
    fflush(stderr);
    ctx->rax = 0;
    ctx->rsp += 8;
}

void recomp_register_fn(uint64_t guest_addr, recomp_fn_t fn) {
    if (guest_addr >= (1ULL << 48)) {
        return;
    }
    uint64_t l1_idx = (guest_addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[l1_idx]) {
        g_dispatch_l1[l1_idx] = (recomp_fn_t **)calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
        if (!g_dispatch_l1[l1_idx]) {
            perror("calloc dispatch L2 table");
            abort();
        }
    }
    uint64_t l2_idx = (guest_addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[l1_idx][l2_idx]) {
        g_dispatch_l1[l1_idx][l2_idx] = (recomp_fn_t *)calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
        if (!g_dispatch_l1[l1_idx][l2_idx]) {
            perror("calloc dispatch L3 table");
            abort();
        }
    }
    g_dispatch_l1[l1_idx][l2_idx][guest_addr & DISPATCH_LEVEL_MASK] = fn;
}

static size_t parse_mem_size_str(const char *str) {
    if (!str || !*str) return 0;
    char *end = NULL;
    unsigned long long val = strtoull(str, &end, 0);
    if (end && *end) {
        if (*end == 'g' || *end == 'G') val *= 1024ULL * 1024 * 1024;
        else if (*end == 'm' || *end == 'M') val *= 1024ULL * 1024;
        else if (*end == 'k' || *end == 'K') val *= 1024ULL;
    }
    return (size_t)val;
}

GuestContext *recomp_init_runtime(size_t guest_mem_sz, const uint8_t *elf_image, size_t image_size, const char *prog_name) {
    GuestContext *ctx = (GuestContext *)calloc(1, sizeof(GuestContext));
    if (!ctx) {
        perror("calloc GuestContext");
        return NULL;
    }

    g_current_ctx = ctx;
    struct sigaction sa = {0};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = crash_handler;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);

    // Dynamic memory size calculation without hardcoded limits
    if (guest_mem_sz == 0) {
        const char *env_mem = getenv("PS4_RECOMP_MEM");
        if (env_mem) {
            guest_mem_sz = parse_mem_size_str(env_mem);
        }
    }

    if (guest_mem_sz == 0) {
        // PS4 virtual address space:
        // System managed / flexible memory covers 0 - 32GB (0x800000000ULL).
        // User memory pools (sceKernelMemoryPoolReserve) are located in the user area at
        // 0x1000000000 (64 GB), 0x2000000000 (128 GB), 0x3000000000 (192 GB) up to 0x7000000000.
        // Allocate a 512 GB (0x8000000000ULL) virtual address space.
        // On modern 64-bit OS (macOS/Linux), demand paging ensures uncommitted address space
        // consumes zero physical RAM until pages are touched.
        guest_mem_sz = 0x8000000000ULL; // 512 GB virtual address space
    }

    if (image_size > 0 && guest_mem_sz < image_size + 0x20000ULL) {
        guest_mem_sz = image_size + 0x20000ULL;
    }

    uint8_t *mem = (uint8_t *)mmap(NULL, guest_mem_sz,
                                   PROT_READ | PROT_WRITE,
                                   MAP_ANON | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap guest memory");
        free(ctx);
        return NULL;
    }

    ctx->mem_base = mem;
    ctx->mem_size = guest_mem_sz;

    ctx->fpu_cw = 0x037F; // Standard x87 control word default

    // Copy initial ELF memory image if provided
    if (elf_image && image_size > 0) {
        memcpy(ctx->mem_base, elf_image, image_size);
    }

    // Dynamic memory layout positioning relative to image boundary:
    uint64_t image_end = (image_size + 0xFFFFULL) & ~0xFFFFULL;
    if (image_end < 0x10000ULL) {
        image_end = 0x10000ULL;
    }

    // Set up FS segment (Thread Control Block for TLS) right above image
    uint64_t tcb_addr = image_end;
    ctx->fs_base = tcb_addr;
    MEM_U64(tcb_addr) = tcb_addr;

    // Set up process arguments at tcb_addr + 0x10000 for _start_ps4_c
    uint64_t args_addr = tcb_addr + 0x10000ULL;
    uint64_t prog_name_addr = args_addr + 0x100ULL;
    const char *pname = (prog_name && prog_name[0]) ? prog_name : "ps4_app";
    strncpy((char*)(ctx->mem_base + prog_name_addr), pname, 255);

    // argc = 1
    MEM_U64(args_addr) = 1;
    // argv[0] = prog_name_addr
    MEM_U64(args_addr + 8) = prog_name_addr;
    // argv[1] = NULL
    MEM_U64(args_addr + 16) = 0;
    // envp[0] = NULL
    MEM_U64(args_addr + 24) = 0;
    // auxv[0] = AT_NULL (type = 0, val = 0)
    MEM_U64(args_addr + 32) = 0;
    MEM_U64(args_addr + 40) = 0;

    // RDI points to the argument structure
    ctx->rdi = args_addr;

    // Initialize guest heap pointer after args area
    ctx->heap_ptr = (args_addr + 0x10000ULL + 0xFFFULL) & ~0xFFFULL;

    // Set up process root context and VM extent manager
    ctx->process_ctx = ctx;
    pthread_mutex_init(&ctx->vm_mutex, NULL);

    // Set up initial free VM extent for heap
    // In PS4, system managed / flexible memory is below 32 GB (0x800000000ULL),
    // and user memory pools start at 64 GB (0x1000000000ULL).
    // Keep dynamic heap and main thread stack inside the lower 32 GB window.
    uint64_t low_mem_limit = 0x800000000ULL; // 32 GB PS4 system memory boundary
    if (low_mem_limit > guest_mem_sz) {
        low_mem_limit = guest_mem_sz;
    }
    uint64_t stack_floor = (low_mem_limit > (64ULL * 1024 * 1024)) ? (low_mem_limit - (64ULL * 1024 * 1024)) : low_mem_limit;
    uint64_t heap_start = (ctx->heap_ptr + 4095ULL) & ~4095ULL;
    if (heap_start < stack_floor) {
        GuestVMExtent *root = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
        root->addr = heap_start;
        root->size = stack_floor - heap_start;
        root->is_free = true;
        ctx->vm_extents = root;
    }

    // Also register the user pool area [0x1000000000 .. guest_mem_sz) as free extents
    if (guest_mem_sz > 0x1000000000ULL) {
        GuestVMExtent *pool_extent = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
        pool_extent->addr = 0x1000000000ULL;
        pool_extent->size = guest_mem_sz - 0x1000000000ULL;
        pool_extent->is_free = true;
        GuestVMExtent *tail = ctx->vm_extents;
        while (tail && tail->next) tail = tail->next;
        if (tail) {
            tail->next = pool_extent;
            pool_extent->prev = tail;
        } else {
            ctx->vm_extents = pool_extent;
        }
    }

    // Set up guest stack at top of low memory region (growing downwards)
    ctx->rsp = (low_mem_limit - 0x1000ULL) & ~0xFFULL;
    ctx->rbp = ctx->rsp;

    ctx->thread_id = 1000;
    recomp_init_main_thread(ctx);

    return ctx;
}

GuestContext *recomp_init_runtime_file(const char *image_filename, size_t requested_mem_sz, const char *prog_name) {
    char path[1024] = {0};
    FILE *fp = NULL;

    // 1. Try directly (e.g. current working directory or full path)
    if (image_filename && access(image_filename, R_OK) == 0) {
        strncpy(path, image_filename, sizeof(path) - 1);
        fp = fopen(path, "rb");
    }

    // 2. If not found, try adjacent to executable
    if (!fp && image_filename) {
#if defined(__APPLE__)
        char exec_path[1024] = {0};
        uint32_t size = sizeof(exec_path);
        if (_NSGetExecutablePath(exec_path, &size) == 0) {
            char *dir = dirname(exec_path);
            snprintf(path, sizeof(path), "%s/%s", dir, image_filename);
            fp = fopen(path, "rb");
            if (!fp) {
                // Check inside macOS .app bundle Resources directory
                snprintf(path, sizeof(path), "%s/../Resources/%s", dir, image_filename);
                fp = fopen(path, "rb");
            }
        }
#endif
    }

    if (!fp) {
        fprintf(stderr, "[ps4-recomp] Failed to open guest image file: %s\n", image_filename ? image_filename : "(null)");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    size_t image_sz = (sz > 0) ? (size_t)sz : 0;
    GuestContext *ctx = recomp_init_runtime(requested_mem_sz, NULL, image_sz, prog_name);
    if (!ctx) {
        fclose(fp);
        return NULL;
    }

    printf("[ps4-recomp] Loading guest memory image (%zu bytes), allocated dynamic address space (%.1f MB)\n",
           image_sz, (double)ctx->mem_size / (1024.0 * 1024.0));

    if (sz > 0) {
        size_t nread = fread(ctx->mem_base, 1, sz, fp);
        if (nread != (size_t)sz) {
            fprintf(stderr, "[ps4-recomp] Warning: only read %zu of %ld bytes from %s\n", nread, sz, path);
        }
    }
    fclose(fp);
    return ctx;
}

void recomp_free_runtime(GuestContext *ctx) {
    if (!ctx) return;

    ps4_metal_screen_destroy();
    ps4_keyboard_destroy();
    ps4_videoout_destroy();
    ps4_equeue_destroy();
    ps4_direct_mem_destroy();
    ps4_event_flag_destroy();
    ps4_sync_destroy();
    ps4_vfs_destroy();

    if (ctx->mem_base) {
        munmap(ctx->mem_base, ctx->mem_size);
    }
    GuestVMExtent *ext = ctx->vm_extents;
    while (ext) {
        GuestVMExtent *next = ext->next;
        free(ext);
        ext = next;
    }
    pthread_mutex_destroy(&ctx->vm_mutex);
    for (size_t i = 0; i < DISPATCH_LEVEL_SIZE; i++) {
        if (g_dispatch_l1[i]) {
            for (size_t j = 0; j < DISPATCH_LEVEL_SIZE; j++) {
                if (g_dispatch_l1[i][j]) {
                    free(g_dispatch_l1[i][j]);
                }
            }
            free(g_dispatch_l1[i]);
            g_dispatch_l1[i] = NULL;
        }
    }
    free(ctx);
}

void recomp_unwind_to(GuestContext *ctx, uint64_t target_ip) {
    ctx->rip = target_ip;
    UnwindFrame *f = ctx->unwind_frame;
    while (f) {
        if (target_ip >= f->fn_start && target_ip < f->fn_end) {
            ctx->unwind_frame = f;
            longjmp(f->buf, 1);
        }
        f = f->prev;
    }
    fprintf(stderr, "FATAL: Unwind target 0x%llx not found in active unwind frames!\n", (unsigned long long)target_ip);
    abort();
}

uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot, int flags, const char *name) {
    if (!ctx || size == 0) return (uint64_t)-1;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    size_t aligned_size = (size + 4095ULL) & ~4095ULL;

    pthread_mutex_lock(&proc->vm_mutex);

    GuestVMExtent *curr = proc->vm_extents;
    GuestVMExtent *best = NULL;
    while (curr) {
        if (curr->is_free && curr->size >= aligned_size && (curr->addr < 0x800000000ULL || (flags & 0x80) || (name && strstr(name, "pool")))) {
            if (!best || curr->size < best->size) {
                best = curr;
                if (best->size == aligned_size) break;
            }
        }
        curr = curr->next;
    }

    if (!best) {
        pthread_mutex_unlock(&proc->vm_mutex);
        return (uint64_t)-1;
    }

    if (best->size > aligned_size) {
        GuestVMExtent *split = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
        if (split) {
            split->addr = best->addr + aligned_size;
            split->size = best->size - aligned_size;
            split->is_free = true;
            split->prev = best;
            split->next = best->next;
            if (best->next) {
                best->next->prev = split;
            }
            best->next = split;
            best->size = aligned_size;
        }
    }

    best->is_free = false;
    best->prot = prot;
    best->flags = flags;
    if (name) {
        strncpy(best->name, name, sizeof(best->name) - 1);
        best->name[sizeof(best->name) - 1] = '\0';
    } else {
        best->name[0] = '\0';
    }
    uint64_t res = best->addr;
    pthread_mutex_unlock(&proc->vm_mutex);
    return res;
}

uint64_t recomp_vm_alloc(GuestContext *ctx, size_t size) {
    return recomp_vm_alloc_named(ctx, size, PROT_READ | PROT_WRITE, 0, "anon");
}

uint64_t recomp_vm_alloc_fixed(GuestContext *ctx, uint64_t desired_addr, size_t size, int prot, int flags, const char *name) {
    if (!ctx || size == 0) return (uint64_t)-1;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    uint64_t aligned_addr = desired_addr & ~4095ULL;
    size_t aligned_size = (size + 4095ULL) & ~4095ULL;
    if (aligned_addr + aligned_size > proc->mem_size || aligned_addr + aligned_size < aligned_addr) {
        return (uint64_t)-1;
    }

    pthread_mutex_lock(&proc->vm_mutex);

    GuestVMExtent *curr = proc->vm_extents;
    while (curr) {
        if (curr->is_free && curr->addr <= aligned_addr && (curr->addr + curr->size) >= (aligned_addr + aligned_size)) {
            break;
        }
        curr = curr->next;
    }

    if (!curr) {
        pthread_mutex_unlock(&proc->vm_mutex);
        return (uint64_t)-1;
    }

    // Split leading part if desired_addr > curr->addr
    if (aligned_addr > curr->addr) {
        GuestVMExtent *prefix = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
        if (!prefix) {
            pthread_mutex_unlock(&proc->vm_mutex);
            return (uint64_t)-1;
        }
        prefix->addr = curr->addr;
        prefix->size = aligned_addr - curr->addr;
        prefix->is_free = true;
        prefix->prev = curr->prev;
        prefix->next = curr;
        if (curr->prev) {
            curr->prev->next = prefix;
        } else {
            proc->vm_extents = prefix;
        }
        curr->prev = prefix;
        curr->addr = aligned_addr;
        curr->size -= prefix->size;
    }

    // Split trailing part if curr->size > aligned_size
    if (curr->size > aligned_size) {
        GuestVMExtent *suffix = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
        if (suffix) {
            suffix->addr = curr->addr + aligned_size;
            suffix->size = curr->size - aligned_size;
            suffix->is_free = true;
            suffix->prev = curr;
            suffix->next = curr->next;
            if (curr->next) {
                curr->next->prev = suffix;
            }
            curr->next = suffix;
            curr->size = aligned_size;
        }
    }

    curr->is_free = false;
    curr->prot = prot;
    curr->flags = flags;
    if (name) {
        strncpy(curr->name, name, sizeof(curr->name) - 1);
        curr->name[sizeof(curr->name) - 1] = '\0';
    } else {
        curr->name[0] = '\0';
    }

    pthread_mutex_unlock(&proc->vm_mutex);
    return aligned_addr;
}

GuestVMExtent *recomp_vm_find(GuestContext *ctx, uint64_t addr) {
    if (!ctx) return NULL;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    pthread_mutex_lock(&proc->vm_mutex);
    GuestVMExtent *curr = proc->vm_extents;
    while (curr) {
        if (addr >= curr->addr && addr < (curr->addr + curr->size)) {
            pthread_mutex_unlock(&proc->vm_mutex);
            return curr;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&proc->vm_mutex);
    return NULL;
}

int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size) {
    if (!ctx || addr == 0 || size == 0) return -1;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    uint64_t aligned_addr = addr & ~4095ULL;
    size_t aligned_size = (size + 4095ULL) & ~4095ULL;

    pthread_mutex_lock(&proc->vm_mutex);

    GuestVMExtent *curr = proc->vm_extents;
    while (curr) {
        if (aligned_addr >= curr->addr && aligned_addr < curr->addr + curr->size) {
            break;
        }
        curr = curr->next;
    }

    if (!curr) {
        pthread_mutex_unlock(&proc->vm_mutex);
        return 0;
    }

    // Release host physical pages
    if (proc->mem_base && aligned_addr + aligned_size <= proc->mem_size) {
#if defined(__APPLE__) || defined(__linux__)
        madvise(proc->mem_base + aligned_addr, aligned_size, MADV_DONTNEED);
#endif
    }

    // If already free, nothing to do
    if (curr->is_free) {
        pthread_mutex_unlock(&proc->vm_mutex);
        return 0;
    }

    // Case 1: aligned_addr is inside curr, but after curr->addr: split off the prefix
    if (aligned_addr > curr->addr) {
        GuestVMExtent *mid = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
        if (mid) {
            mid->addr = aligned_addr;
            mid->size = curr->size - (aligned_addr - curr->addr);
            mid->is_free = false;
            mid->prev = curr;
            mid->next = curr->next;
            if (curr->next) curr->next->prev = mid;
            curr->next = mid;
            curr->size = aligned_addr - curr->addr;
            curr = mid;
        }
    }

    // Case 2: aligned_size is less than curr->size: split off the suffix
    if (aligned_size < curr->size) {
        GuestVMExtent *suffix = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
        if (suffix) {
            suffix->addr = curr->addr + aligned_size;
            suffix->size = curr->size - aligned_size;
            suffix->is_free = false;
            suffix->prev = curr;
            suffix->next = curr->next;
            if (curr->next) curr->next->prev = suffix;
            curr->next = suffix;
            curr->size = aligned_size;
        }
    }

    // Now curr matches [aligned_addr, aligned_addr + aligned_size] exactly
    curr->is_free = true;

    // Coalesce with next if adjacent and free
    if (curr->next && curr->next->is_free && (curr->addr + curr->size == curr->next->addr)) {
        GuestVMExtent *next_node = curr->next;
        curr->size += next_node->size;
        curr->next = next_node->next;
        if (next_node->next) next_node->next->prev = curr;
        free(next_node);
    }

    // Coalesce with prev if adjacent and free
    if (curr->prev && curr->prev->is_free && (curr->prev->addr + curr->prev->size == curr->addr)) {
        GuestVMExtent *prev_node = curr->prev;
        prev_node->size += curr->size;
        prev_node->next = curr->next;
        if (curr->next) curr->next->prev = prev_node;
        free(curr);
        curr = prev_node;
    }

    pthread_mutex_unlock(&proc->vm_mutex);
    return 0;
}

GuestContext *recomp_create_thread_context(GuestContext *parent, uint64_t stack_size) {
    if (!parent) return NULL;
    if (stack_size == 0) stack_size = 2 * 1024 * 1024; // 2MB default stack
    GuestContext *proc = parent->process_ctx ? parent->process_ctx : parent;

    // Allocate stack and TCB (64KB) via VM extent allocator
    size_t alloc_total = stack_size + 0x10000ULL;
    uint64_t stack_base = recomp_vm_alloc(proc, alloc_total);
    if (stack_base == (uint64_t)-1) {
        fprintf(stderr, "[ps4-recomp] FATAL: out of guest memory for new thread stack\n");
        return NULL;
    }
    uint64_t tcb_base = stack_base + stack_size;

    // Initialize FS TCB base
    *(uint64_t*)(parent->mem_base + tcb_base) = tcb_base;

    GuestContext *t_ctx = (GuestContext *)calloc(1, sizeof(GuestContext));
    if (!t_ctx) return NULL;

    t_ctx->mem_base = parent->mem_base;
    t_ctx->mem_size = parent->mem_size;
    t_ctx->heap_ptr = parent->heap_ptr;
    t_ctx->fs_base = tcb_base;
    t_ctx->process_ctx = proc;
    t_ctx->stack_base = stack_base;
    t_ctx->stack_alloc_size = alloc_total;
    t_ctx->rsp = (stack_base + stack_size - 0x100ULL) & ~0xFFULL;
    t_ctx->rbp = t_ctx->rsp;
    t_ctx->fpu_cw = 0x037F;

    return t_ctx;
}

void recomp_free_thread_context(GuestContext *ctx) {
    if (!ctx) return;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    if (ctx->stack_base && ctx->stack_alloc_size > 0) {
        recomp_vm_free(proc, ctx->stack_base, ctx->stack_alloc_size);
    }
    free(ctx);
}

void recomp_vpcmpistri(GuestContext *ctx, const void *src2_ptr, const void *src1_ptr, uint8_t imm8) {
    const uint8_t *s2_bytes = (const uint8_t *)src2_ptr;
    const uint8_t *s1_bytes = (const uint8_t *)src1_ptr;

    int is_word = (imm8 & 1);
    int is_signed = ((imm8 >> 1) & 1);
    int agg = ((imm8 >> 2) & 3);
    int pol = ((imm8 >> 4) & 3);
    int msb_index = ((imm8 >> 6) & 1);

    int sz = is_word ? 8 : 16;
    int len1 = sz, len2 = sz;

    if (!is_word) {
        for (int i = 0; i < sz; i++) {
            if (s2_bytes[i] == 0) { len2 = i; break; }
        }
        for (int i = 0; i < sz; i++) {
            if (s1_bytes[i] == 0) { len1 = i; break; }
        }
    } else {
        const uint16_t *s2_w = (const uint16_t *)src2_ptr;
        const uint16_t *s1_w = (const uint16_t *)src1_ptr;
        for (int i = 0; i < sz; i++) {
            if (s2_w[i] == 0) { len2 = i; break; }
        }
        for (int i = 0; i < sz; i++) {
            if (s1_w[i] == 0) { len1 = i; break; }
        }
    }

    uint32_t int_res1 = 0;

    for (int i = 0; i < sz; i++) {
        int match = 0;
        switch (agg) {
            case 0: // Equal Any
                if (i < len1) {
                    for (int j = 0; j < len2; j++) {
                        if (!is_word) {
                            if (s1_bytes[i] == s2_bytes[j]) { match = 1; break; }
                        } else {
                            const uint16_t *s2_w = (const uint16_t *)src2_ptr;
                            const uint16_t *s1_w = (const uint16_t *)src1_ptr;
                            if (s1_w[i] == s2_w[j]) { match = 1; break; }
                        }
                    }
                }
                break;
            case 1: // Ranges
                if (i < len1) {
                    for (int j = 0; j < sz; j += 2) {
                        if (j < len2) {
                            if (!is_word) {
                                if (!is_signed) {
                                    uint8_t lo = s2_bytes[j];
                                    uint8_t hi = (j + 1 < len2) ? s2_bytes[j + 1] : 0xFF;
                                    if (s1_bytes[i] >= lo && s1_bytes[i] <= hi) { match = 1; break; }
                                } else {
                                    int8_t lo = (int8_t)s2_bytes[j];
                                    int8_t hi = (j + 1 < len2) ? (int8_t)s2_bytes[j + 1] : (int8_t)0x7F;
                                    int8_t v = (int8_t)s1_bytes[i];
                                    if (v >= lo && v <= hi) { match = 1; break; }
                                }
                            } else {
                                const uint16_t *s2_w = (const uint16_t *)src2_ptr;
                                const uint16_t *s1_w = (const uint16_t *)src1_ptr;
                                if (!is_signed) {
                                    uint16_t lo = s2_w[j];
                                    uint16_t hi = (j + 1 < len2) ? s2_w[j + 1] : 0xFFFF;
                                    if (s1_w[i] >= lo && s1_w[i] <= hi) { match = 1; break; }
                                } else {
                                    int16_t lo = (int16_t)s2_w[j];
                                    int16_t hi = (j + 1 < len2) ? (int16_t)s2_w[j + 1] : (int16_t)0x7FFF;
                                    int16_t v = (int16_t)s1_w[i];
                                    if (v >= lo && v <= hi) { match = 1; break; }
                                }
                            }
                        }
                    }
                }
                break;
            case 2: // Equal Each
                if (i < len1 && i < len2) {
                    if (!is_word) {
                        match = (s1_bytes[i] == s2_bytes[i]);
                    } else {
                        const uint16_t *s2_w = (const uint16_t *)src2_ptr;
                        const uint16_t *s1_w = (const uint16_t *)src1_ptr;
                        match = (s1_w[i] == s2_w[i]);
                    }
                } else if (i >= len1 && i >= len2) {
                    match = 1;
                }
                break;
            case 3: // Equal Ordered (Substring search)
                match = 1;
                for (int k = 0; k < sz - i; k++) {
                    if (k < len2) {
                        if (i + k >= len1) {
                            match = 0;
                            break;
                        }
                        if (!is_word) {
                            if (s1_bytes[i + k] != s2_bytes[k]) { match = 0; break; }
                        } else {
                            const uint16_t *s2_w = (const uint16_t *)src2_ptr;
                            const uint16_t *s1_w = (const uint16_t *)src1_ptr;
                            if (s1_w[i + k] != s2_w[k]) { match = 0; break; }
                        }
                    }
                }
                break;
        }
        if (match) {
            int_res1 |= (1U << i);
        }
    }

    uint32_t int_res2 = 0;
    switch (pol) {
        case 0: // Positive
            int_res2 = int_res1;
            break;
        case 1: // Negative
            int_res2 = (~int_res1) & ((1U << sz) - 1);
            break;
        case 2: // Masked positive
            int_res2 = int_res1;
            break;
        case 3: // Masked negative
            for (int i = 0; i < len1; i++) {
                if (!((int_res1 >> i) & 1)) {
                    int_res2 |= (1U << i);
                }
            }
            break;
    }

    int index = sz;
    if (int_res2 != 0) {
        if (!msb_index) {
            index = __builtin_ctz(int_res2);
        } else {
            index = 31 - __builtin_clz(int_res2);
        }
    }

    ctx->rcx = (uint64_t)index;

    // Update EFLAGS: CF, ZF, SF, OF, clear AF & PF
    ctx->rflags &= ~0x8D5ULL; // Clear CF(0), PF(2), AF(4), ZF(6), SF(7), OF(11)
    if (int_res2 != 0) ctx->rflags |= (1ULL << 0);  // CF
    if (len2 < sz)      ctx->rflags |= (1ULL << 6);  // ZF
    if (len1 < sz)      ctx->rflags |= (1ULL << 7);  // SF
    if (int_res2 & 1)   ctx->rflags |= (1ULL << 11); // OF
}

