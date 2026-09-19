#include "recomp_runtime.h"

#include <signal.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#endif

recomp_fn_t *g_dispatch_l1[DISPATCH_L1_SIZE] = {0};
GuestContext *g_current_ctx = NULL;

static void crash_handler(int sig, siginfo_t *si, void *ucontext) {
    if (g_current_ctx) {
        fprintf(stderr, "\nFATAL: Signal %d at host address %p (mem_base=%p, guest RIP=0x%llx, RSP=0x%llx, RBP=0x%llx, RAX=0x%llx, RDI=0x%llx)\n",
                sig, si->si_addr, g_current_ctx->mem_base,
                (unsigned long long)g_current_ctx->rip,
                (unsigned long long)g_current_ctx->rsp,
                (unsigned long long)g_current_ctx->rbp,
                (unsigned long long)g_current_ctx->rax,
                (unsigned long long)g_current_ctx->rdi);
#if defined(__APPLE__) && defined(__arm64__)
        uint64_t host_pc = ((ucontext_t*)ucontext)->uc_mcontext->__ss.__pc;
        fprintf(stderr, "Host PC: 0x%llx\n", (unsigned long long)host_pc);
#endif
    } else {
        fprintf(stderr, "\nFATAL: Signal %d at host address %p (no guest context)\n", sig, si->si_addr);
    }
    fflush(stderr);
    exit(1);
}

void recomp_register_fn(uint64_t guest_addr, recomp_fn_t fn) {
    uint64_t l1_idx = guest_addr >> DISPATCH_L1_SHIFT;
    if (l1_idx >= DISPATCH_L1_SIZE) {
        return;
    }
    if (!g_dispatch_l1[l1_idx]) {
        g_dispatch_l1[l1_idx] = (recomp_fn_t *)calloc(DISPATCH_L2_SIZE, sizeof(recomp_fn_t));
        if (!g_dispatch_l1[l1_idx]) {
            perror("calloc dispatch L2 table");
            abort();
        }
    }
    g_dispatch_l1[l1_idx][guest_addr & DISPATCH_L2_MASK] = fn;
}

GuestContext *recomp_init_runtime(const uint8_t *elf_image, size_t image_size) {
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

    // Allocate 1GB guest address space
    size_t guest_mem_sz = 1ULL << 30; // 1GB
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

    // Copy initial ELF memory image
    if (elf_image && image_size > 0) {
        memcpy(ctx->mem_base, elf_image, image_size);
    }

    // Set up guest stack (grow downwards from 0x30000000)
    ctx->rsp = 0x30000000ULL;
    ctx->rbp = ctx->rsp;

    // Set up FS segment (Thread Control Block for TLS)
    uint64_t tcb_addr = 0x10000000ULL;
    ctx->fs_base = tcb_addr;
    // System V AMD64 TLS: %fs:0x0 points to the TCB itself
    MEM_U64(tcb_addr) = tcb_addr;

    // Set up process arguments at 0x20000000 for _start_ps4_c
    uint64_t args_addr = 0x20000000ULL;
    uint64_t prog_name_addr = 0x20000100ULL;
    strcpy((char*)(ctx->mem_base + prog_name_addr), "hello_world");

    // argc = 1
    MEM_U64(args_addr) = 1;
    // argv[0] = prog_name_addr
    MEM_U64(args_addr + 8) = prog_name_addr;
    // argv[1] = NULL
    MEM_U64(args_addr + 16) = 0;
    // envp[0] = NULL
    MEM_U64(args_addr + 24) = 0;

    // RDI points to the argument structure
    ctx->rdi = args_addr;

    return ctx;
}

GuestContext *recomp_init_runtime_file(const char *image_filename) {
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
        }
#endif
    }

    if (!fp) {
        fprintf(stderr, "[ps4-recomp] Failed to open guest image file: %s\n", image_filename ? image_filename : "(null)");
        return NULL;
    }

    printf("[ps4-recomp] Loading guest memory image from: %s\n", path);

    GuestContext *ctx = recomp_init_runtime(NULL, 0);
    if (!ctx) {
        fclose(fp);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz > 0) {
        if ((size_t)sz > ctx->mem_size) {
            fprintf(stderr, "[ps4-recomp] Guest image size (%ld bytes) exceeds guest memory (1GB)\n", sz);
            fclose(fp);
            recomp_free_runtime(ctx);
            return NULL;
        }
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
    if (ctx->mem_base) {
        munmap(ctx->mem_base, ctx->mem_size);
    }
    for (size_t i = 0; i < DISPATCH_L1_SIZE; i++) {
        if (g_dispatch_l1[i]) {
            free(g_dispatch_l1[i]);
            g_dispatch_l1[i] = NULL;
        }
    }
    free(ctx);
}
