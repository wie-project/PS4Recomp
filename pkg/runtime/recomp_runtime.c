#include "recomp_runtime.h"

#include <signal.h>

recomp_fn_t g_dispatch_table[DISPATCH_TABLE_SIZE] = {0};
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
    if (guest_addr < DISPATCH_TABLE_SIZE) {
        g_dispatch_table[guest_addr] = fn;
    }
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

void recomp_free_runtime(GuestContext *ctx) {
    if (!ctx) return;
    if (ctx->mem_base) {
        munmap(ctx->mem_base, ctx->mem_size);
    }
    free(ctx);
}
