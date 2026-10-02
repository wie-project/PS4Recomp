#include "recomp_runtime.h"
#include "ps4_sysmodule.h"

#include <signal.h>
#if defined(__APPLE__)
#include <execinfo.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#endif
#if defined(__arm64__) || defined(__aarch64__)
#include <arm_neon.h>
#endif

recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE] = {0};
_Thread_local GuestContext *g_current_ctx = NULL;
pthread_mutex_t g_heap_mutex = PTHREAD_MUTEX_INITIALIZER;

static void crash_handler(int sig, siginfo_t *si, void *ucontext) {
  if (g_current_ctx) {
    fprintf(stderr,
            "\nFATAL: Signal %d at host address %p (mem_base=%p)\n"
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
    uint64_t host_pc = ((ucontext_t *)ucontext)->uc_mcontext->__ss.__pc;
    fprintf(stderr, "Host PC: 0x%llx\n", (unsigned long long)host_pc);
#endif
    void *callstack[64];
    int frames = backtrace(callstack, 64);
    fprintf(stderr, "Call stack (%d frames):\n", frames);
    backtrace_symbols_fd(callstack, frames, 2);
  } else {
    fprintf(stderr,
            "\nFATAL: Signal %d at host address %p (no guest context)\n", sig,
            si->si_addr);
    void *callstack[64];
    int frames = backtrace(callstack, 64);
    fprintf(stderr, "Call stack (%d frames):\n", frames);
    backtrace_symbols_fd(callstack, frames, 2);
  }
  fflush(stderr);
  exit(1);
}

#define UNRESOLVED_BUCKETS 1024
static RecompUnresolvedSym *g_unresolved_table[UNRESOLVED_BUCKETS];
static pthread_mutex_t g_unresolved_mutex = PTHREAD_MUTEX_INITIALIZER;

void recomp_register_unresolved(uint64_t addr, const char *nid,
                                const char *sym_name, const char *lib_name) {
  if (!addr)
    return;
  uint32_t bucket = (uint32_t)((addr ^ (addr >> 12)) % UNRESOLVED_BUCKETS);
  pthread_mutex_lock(&g_unresolved_mutex);
  RecompUnresolvedSym *entry =
      (RecompUnresolvedSym *)malloc(sizeof(RecompUnresolvedSym));
  if (entry) {
    entry->addr = addr;
    entry->nid = nid ? strdup(nid) : NULL;
    entry->sym_name = sym_name ? strdup(sym_name) : NULL;
    entry->lib_name = lib_name ? strdup(lib_name) : NULL;
    entry->next = g_unresolved_table[bucket];
    g_unresolved_table[bucket] = entry;
  }
  pthread_mutex_unlock(&g_unresolved_mutex);
}

const RecompUnresolvedSym *recomp_lookup_unresolved(uint64_t addr) {
  uint32_t bucket = (uint32_t)((addr ^ (addr >> 12)) % UNRESOLVED_BUCKETS);
  pthread_mutex_lock(&g_unresolved_mutex);
  RecompUnresolvedSym *curr = g_unresolved_table[bucket];
  while (curr) {
    if (curr->addr == addr) {
      pthread_mutex_unlock(&g_unresolved_mutex);
      return curr;
    }
    curr = curr->next;
  }
  pthread_mutex_unlock(&g_unresolved_mutex);
  return NULL;
}

void shim_unresolved_stub(GuestContext *ctx) {
  uint64_t caller_rip = 0;
  if (ctx && ctx->mem_base && ctx->rsp + 8 <= ctx->mem_size) {
    caller_rip = *(uint64_t *)(ctx->mem_base + ctx->rsp);
  }
  const RecompUnresolvedSym *sym = recomp_lookup_unresolved(ctx ? ctx->rip : 0);
  if (sym && sym->sym_name && sym->sym_name[0]) {
    fprintf(stderr,
            "[ps4-recomp] WARN: Called unimplemented function '%s' (NID: %s, "
            "Lib: %s) at RIP=0x%llx (caller RIP=0x%llx, RSP=0x%llx)\n",
            sym->sym_name, sym->nid ? sym->nid : "unknown",
            sym->lib_name ? sym->lib_name : "unknown",
            (unsigned long long)(ctx ? ctx->rip : 0),
            (unsigned long long)caller_rip,
            (unsigned long long)(ctx ? ctx->rsp : 0));
  } else if (sym && sym->nid && sym->nid[0]) {
    fprintf(stderr,
            "[ps4-recomp] WARN: Called unimplemented function NID '%s' (Lib: "
            "%s) at RIP=0x%llx (caller RIP=0x%llx, RSP=0x%llx)\n",
            sym->nid, sym->lib_name ? sym->lib_name : "unknown",
            (unsigned long long)(ctx ? ctx->rip : 0),
            (unsigned long long)caller_rip,
            (unsigned long long)(ctx ? ctx->rsp : 0));
  } else {
    fprintf(stderr,
            "[ps4-recomp] WARN: Called unresolved function at RIP=0x%llx "
            "(caller RIP=0x%llx, RSP=0x%llx)\n",
            (unsigned long long)(ctx ? ctx->rip : 0),
            (unsigned long long)caller_rip,
            (unsigned long long)(ctx ? ctx->rsp : 0));
  }
  fflush(stderr);
  abort();
  if (ctx) {
    ctx->rax = 0;
    ctx->rsp += 8;
  }
}

void recomp_unimplemented_shim(GuestContext *ctx, const char *shim_name) {
  uint64_t caller_rip = 0;
  if (ctx && ctx->mem_base && ctx->rsp + 8 <= ctx->mem_size) {
    caller_rip = *(uint64_t *)(ctx->mem_base + ctx->rsp);
  }
  fprintf(stderr,
          "[ps4-recomp] WARN: Called unimplemented host shim '%s' at "
          "RIP=0x%llx (caller RIP=0x%llx, RSP=0x%llx)\n",
          shim_name ? shim_name : "unknown",
          (unsigned long long)(ctx ? ctx->rip : 0),
          (unsigned long long)caller_rip,
          (unsigned long long)(ctx ? ctx->rsp : 0));
  fflush(stderr);
  if (ctx) {
    ctx->rax = 0;
    ctx->rsp += 8;
  }
}

void recomp_register_fn(uint64_t guest_addr, recomp_fn_t fn) {
  if (guest_addr >= (1ULL << 48)) {
    return;
  }
  uint64_t l1_idx = (guest_addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
  if (!g_dispatch_l1[l1_idx]) {
    g_dispatch_l1[l1_idx] =
        (recomp_fn_t **)calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[l1_idx]) {
      perror("calloc dispatch L2 table");
      abort();
    }
  }
  uint64_t l2_idx = (guest_addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
  if (!g_dispatch_l1[l1_idx][l2_idx]) {
    g_dispatch_l1[l1_idx][l2_idx] =
        (recomp_fn_t *)calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    if (!g_dispatch_l1[l1_idx][l2_idx]) {
      perror("calloc dispatch L3 table");
      abort();
    }
  }
  g_dispatch_l1[l1_idx][l2_idx][guest_addr & DISPATCH_LEVEL_MASK] = fn;
}

static size_t parse_mem_size_str(const char *str) {
  if (!str || !*str)
    return 0;
  char *end = NULL;
  unsigned long long val = strtoull(str, &end, 0);
  if (end && *end) {
    if (*end == 'g' || *end == 'G')
      val *= 1024ULL * 1024 * 1024;
    else if (*end == 'm' || *end == 'M')
      val *= 1024ULL * 1024;
    else if (*end == 'k' || *end == 'K')
      val *= 1024ULL;
  }
  return (size_t)val;
}

GuestContext *recomp_init_runtime(size_t guest_mem_sz, const uint8_t *elf_image,
                                  size_t image_size, const char *prog_name) {
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
    // User memory pools (sceKernelMemoryPoolReserve) are located in the user
    // area at 0x1000000000 (64 GB), 0x2000000000 (128 GB), 0x3000000000 (192
    // GB) up to 0x7000000000. Allocate a 512 GB (0x8000000000ULL) virtual
    // address space. On modern 64-bit OS (macOS/Linux), demand paging ensures
    // uncommitted address space consumes zero physical RAM until pages are
    // touched.
    guest_mem_sz = 0x8000000000ULL; // 512 GB virtual address space
  }

  if (image_size > 0 && guest_mem_sz < image_size + 0x20000ULL) {
    guest_mem_sz = image_size + 0x20000ULL;
  }

  uint8_t *mem = (uint8_t *)mmap(NULL, guest_mem_sz, PROT_READ | PROT_WRITE,
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
  MEM_U64(tcb_addr + 0x28ULL) = 0x595e9fbd94fda766ULL;
  ctx->mxcsr = 0x1f80;
  ctx->fpu_cw = 0x037f;

  // Set up process arguments at tcb_addr + 0x10000 for _start_ps4_c
  uint64_t args_addr = tcb_addr + 0x10000ULL;
  ctx->args_addr = args_addr;
  uint64_t prog_name_addr = args_addr + 0x100ULL;
  const char *pname = "/app0/eboot.bin";
  if (prog_name && strncmp(prog_name, "/app0/", 6) == 0) {
    pname = prog_name;
  }
  strncpy((char *)(ctx->mem_base + prog_name_addr), pname, 255);

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
  uint64_t stack_floor = (low_mem_limit > (64ULL * 1024 * 1024))
                             ? (low_mem_limit - (64ULL * 1024 * 1024))
                             : low_mem_limit;
  uint64_t heap_start = (ctx->heap_ptr + 4095ULL) & ~4095ULL;
  if (heap_start < stack_floor) {
    GuestVMExtent *root = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
    root->addr = heap_start;
    root->size = stack_floor - heap_start;
    root->is_free = true;
    ctx->vm_extents = root;
  }

  // Also register the user pool area [0x1000000000 .. guest_mem_sz) as free
  // extents
  if (guest_mem_sz > 0x1000000000ULL) {
    GuestVMExtent *pool_extent =
        (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
    pool_extent->addr = 0x1000000000ULL;
    pool_extent->size = guest_mem_sz - 0x1000000000ULL;
    pool_extent->is_free = true;
    GuestVMExtent *tail = ctx->vm_extents;
    while (tail && tail->next)
      tail = tail->next;
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

GuestContext *recomp_init_runtime_file(const char *image_filename,
                                       size_t requested_mem_sz,
                                       const char *prog_name) {
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
    fprintf(stderr, "[ps4-recomp] Failed to open guest image file: %s\n",
            image_filename ? image_filename : "(null)");
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  size_t image_sz = (sz > 0) ? (size_t)sz : 0;
  GuestContext *ctx =
      recomp_init_runtime(requested_mem_sz, NULL, image_sz, prog_name);
  if (!ctx) {
    fclose(fp);
    return NULL;
  }

  printf("[ps4-recomp] Loading guest memory image (%zu bytes), allocated "
         "dynamic address space (%.1f MB)\n",
         image_sz, (double)ctx->mem_size / (1024.0 * 1024.0));

  if (sz > 0) {
    size_t nread = fread(ctx->mem_base, 1, sz, fp);
    if (nread != (size_t)sz) {
      fprintf(stderr,
              "[ps4-recomp] Warning: only read %zu of %ld bytes from %s\n",
              nread, sz, path);
    }
  }
  fclose(fp);
  return ctx;
}

void recomp_init_process_param(GuestContext *ctx, uint64_t proc_param_addr) {
  if (!ctx)
    return;
  ctx->proc_param_addr = proc_param_addr;
  if (ctx->process_ctx) {
    ctx->process_ctx->proc_param_addr = proc_param_addr;
  }
  if (!ctx->mem_base || proc_param_addr == 0)
    return;
  if (proc_param_addr + 0x40 > ctx->mem_size)
    return;

  // SceKernelProcessParam structure has libc_param at offset 0x38
  uint64_t libc_param_addr =
      *(uint64_t *)(ctx->mem_base + proc_param_addr + 0x38ULL);
  if (libc_param_addr != 0 && libc_param_addr + 0x40 <= ctx->mem_size) {
    uint64_t lp_size = *(uint64_t *)(ctx->mem_base + libc_param_addr);
    uint32_t lp_maj = *(uint32_t *)(ctx->mem_base + libc_param_addr + 0x8ULL);
    uint32_t lp_min = *(uint32_t *)(ctx->mem_base + libc_param_addr + 0xcULL);
    uint64_t malloc_replace_addr =
        *(uint64_t *)(ctx->mem_base + libc_param_addr + 0x30ULL);
    uint64_t new_replace_addr =
        *(uint64_t *)(ctx->mem_base + libc_param_addr + 0x38ULL);
    printf("[ps4-mem] SceLibcParam at 0x%llx: size=0x%llx ver=%u.%u malloc_replace=0x%llx new_replace=0x%llx\n",
           (unsigned long long)libc_param_addr, (unsigned long long)lp_size, lp_maj, lp_min,
           (unsigned long long)malloc_replace_addr, (unsigned long long)new_replace_addr);

    if (malloc_replace_addr != 0 &&
        malloc_replace_addr + 0x20 <= ctx->mem_size) {
      uint64_t mr_size = *(uint64_t *)(ctx->mem_base + malloc_replace_addr);
      uint64_t init_func =
          *(uint64_t *)(ctx->mem_base + malloc_replace_addr + 0x10ULL);
      uint64_t malloc_func = *(uint64_t *)(ctx->mem_base + malloc_replace_addr + 0x20ULL);
      printf("[ps4-mem] SceLibcMallocReplace at 0x%llx: size=0x%llx init=0x%llx malloc=0x%llx\n",
             (unsigned long long)malloc_replace_addr, (unsigned long long)mr_size,
             (unsigned long long)init_func, (unsigned long long)malloc_func);
      if (init_func != 0 && init_func < ctx->mem_size) {
        printf("[ps4-recomp] Initializing custom memory allocator via "
               "SceLibcParam at 0x%llx...\n",
               (unsigned long long)init_func);
        recomp_call_guest(ctx, init_func);
      }
    }

    // Standard PS4 libc startup: call _malloc_init (NID z8GPiQwaAEY).
    // In PS4 ABI, libc _malloc_init reads SceLibcParam via sceKernelGetProcParam(),
    // registers the application heap API with rtld, and invokes _new_setup(new_replace).
    uint64_t malloc_init_addr = recomp_resolve_symbol("libc.prx", "z8GPiQwaAEY");
    if (!malloc_init_addr) {
      malloc_init_addr = recomp_resolve_symbol("libc.prx", "_malloc_init");
    }
    if (malloc_init_addr != 0) {
      printf("[ps4-recomp] Initializing libc malloc subsystem via _malloc_init at 0x%llx...\n",
             (unsigned long long)malloc_init_addr);
      recomp_call_guest(ctx, malloc_init_addr);
    } else if (new_replace_addr != 0) {
      // Fallback: directly invoke _new_setup (NID KNNNbyRieqQ) if _malloc_init was not in libc exports
      uint64_t new_setup_addr = recomp_resolve_symbol("libc.prx", "KNNNbyRieqQ");
      if (!new_setup_addr) {
        new_setup_addr = recomp_resolve_symbol("libc.prx", "_new_setup");
      }
      if (new_setup_addr != 0) {
        printf("[ps4-recomp] Initializing C++ operator new replacements via _new_setup at 0x%llx...\n",
               (unsigned long long)new_setup_addr);
        ctx->rdi = new_replace_addr;
        recomp_call_guest(ctx, new_setup_addr);
      }
    }
  }
}

void recomp_setup_entry_args(GuestContext *ctx, int argc, char **argv, uint64_t entry_addr) {
  if (!ctx || !ctx->mem_base) return;

  uint64_t args_addr = ctx->args_addr;
  if (!args_addr) {
    args_addr = ctx->fs_base + 0x10000ULL;
    ctx->args_addr = args_addr;
    if (ctx->process_ctx) {
      ctx->process_ctx->args_addr = args_addr;
    }
  }

  int guest_argc = 0;
  uint64_t str_buf = args_addr + 0x120ULL;

  const char *p0 = "/app0/eboot.bin";
  if (argc > 0 && argv && argv[0] && strncmp(argv[0], "/app0/", 6) == 0) {
    p0 = argv[0];
  }

  uint64_t p0_addr = str_buf;
  strncpy((char *)(ctx->mem_base + p0_addr), p0, 255);
  *(char *)(ctx->mem_base + p0_addr + 255) = '\0';
  str_buf += (strlen(p0) + 1 + 7) & ~7ULL;

  MEM_U64(args_addr + 8ULL) = p0_addr;
  guest_argc = 1;

  for (int i = 1; i < argc && guest_argc < 32; i++) {
    if (argv && argv[i]) {
      uint64_t arg_addr = str_buf;
      strncpy((char *)(ctx->mem_base + arg_addr), argv[i], 255);
      *(char *)(ctx->mem_base + arg_addr + 255) = '\0';
      str_buf += (strlen(argv[i]) + 1 + 7) & ~7ULL;
      MEM_U64(args_addr + 8ULL + (uint64_t)guest_argc * 8ULL) = arg_addr;
      guest_argc++;
    }
  }

  MEM_U64(args_addr + 8ULL + (uint64_t)guest_argc * 8ULL) = 0;
  *(int32_t *)(ctx->mem_base + args_addr) = guest_argc;
  *(uint32_t *)(ctx->mem_base + args_addr + 4ULL) = 0;
  MEM_U64(args_addr + 0x110ULL) = entry_addr;

  // Set up RDI and RSI according to PS4 System V ABI:
  ctx->rdi = args_addr;
  ctx->rsi = 0;

  // Prepare stack according to PS4 Orbis OS entry conventions:
  // [rsp] = argc
  // [rsp + 8] = argv[0]
  uint64_t sp = ctx->rsp;
  sp &= ~15ULL;
  sp -= 8;
  sp -= 8;
  MEM_U64(sp) = p0_addr;
  sp -= 8;
  MEM_U64(sp) = (uint64_t)guest_argc;

  ctx->rsp = sp;
  ctx->rbp = 0;
}

void shim_sceKernelGetProcParam(GuestContext *ctx) {
  GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
  ctx->rax = proc->proc_param_addr;
  SHIM_RETURN();
}

void recomp_free_runtime(GuestContext *ctx) {
  if (!ctx)
    return;

  ps4_metal_screen_destroy();
  ps4_keyboard_destroy();
  ps4_videoout_destroy();
  ps4_equeue_destroy();
  ps4_direct_mem_destroy();
  ps4_event_flag_destroy();
  ps4_sync_destroy();
  ps4_vfs_destroy();
  ps4_aio_destroy();

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
  fprintf(stderr,
          "FATAL: Unwind target 0x%llx not found in active unwind frames!\n",
          (unsigned long long)target_ip);
  abort();
}

uint64_t recomp_vm_alloc_named_aligned(GuestContext *ctx, size_t size,
                                       size_t alignment, int prot, int flags,
                                       const char *name) {
  if (!ctx || size == 0)
    return (uint64_t)-1;
  if (alignment < 4096)
    alignment = 4096;
  GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
  size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

  printf("[ps4-vm] recomp_vm_alloc_named_aligned (%s): size=%zu (%.2f MB) "
         "align=%zu flags=0x%x\n",
         name ? name : "null", size, (double)size / (1024.0 * 1024.0),
         alignment, flags);
  fflush(stdout);

  pthread_mutex_lock(&proc->vm_mutex);

  GuestVMExtent *curr = proc->vm_extents;
  GuestVMExtent *best = NULL;
  uint64_t best_aligned_addr = 0;

  // Pass 1: search in preferred region
  while (curr) {
    if (curr->is_free) {
      uint64_t cand_addr = (curr->addr + alignment - 1) & ~(alignment - 1);
      if (cand_addr >= curr->addr &&
          (cand_addr + aligned_size) <= (curr->addr + curr->size)) {
        if (curr->addr < 0x800000000ULL || (flags & 0x80) ||
            (name && strstr(name, "pool"))) {
          if (!best || curr->size < best->size) {
            best = curr;
            best_aligned_addr = cand_addr;
            if (curr->size == aligned_size && cand_addr == curr->addr)
              break;
          }
        }
      }
    }
    curr = curr->next;
  }

  // Pass 2: if preferred region is exhausted, allow any free extent in virtual
  // space
  if (!best) {
    curr = proc->vm_extents;
    while (curr) {
      if (curr->is_free) {
        uint64_t cand_addr = (curr->addr + alignment - 1) & ~(alignment - 1);
        if (cand_addr >= curr->addr &&
            (cand_addr + aligned_size) <= (curr->addr + curr->size)) {
          if (!best || curr->size < best->size) {
            best = curr;
            best_aligned_addr = cand_addr;
            if (curr->size == aligned_size && cand_addr == curr->addr)
              break;
          }
        }
      }
      curr = curr->next;
    }
  }

  if (!best) {
    fprintf(stderr,
            "[vmm] ERROR: recomp_vm_alloc_named_aligned failed to find extent "
            "for size=%zu (0x%zx), align=%zu, name=%s\n",
            size, size, alignment, name ? name : "(null)");
    curr = proc->vm_extents;
    while (curr) {
      if (curr->is_free) {
        fprintf(stderr, "  [free extent] addr=0x%llx size=%zu (0x%zx)\n",
                (unsigned long long)curr->addr, curr->size, curr->size);
      }
      curr = curr->next;
    }
    pthread_mutex_unlock(&proc->vm_mutex);
    return (uint64_t)-1;
  }

  // Split leading padding if best_aligned_addr > best->addr
  if (best_aligned_addr > best->addr) {
    GuestVMExtent *prefix = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
    if (!prefix) {
      pthread_mutex_unlock(&proc->vm_mutex);
      return (uint64_t)-1;
    }
    prefix->addr = best->addr;
    prefix->size = best_aligned_addr - best->addr;
    prefix->is_free = true;
    prefix->prev = best->prev;
    prefix->next = best;
    if (best->prev) {
      best->prev->next = prefix;
    } else {
      proc->vm_extents = prefix;
    }
    best->prev = prefix;
    best->addr = best_aligned_addr;
    best->size -= prefix->size;
  }

  // Split trailing remainder
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
  printf("[ps4-vm] recomp_vm_alloc_named_aligned SUCCESS: addr=0x%llx\n",
         (unsigned long long)res);
  fflush(stdout);
  pthread_mutex_unlock(&proc->vm_mutex);
  return res;
}

uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot,
                               int flags, const char *name) {
  return recomp_vm_alloc_named_aligned(ctx, size, 4096, prot, flags, name);
}

uint64_t recomp_vm_alloc(GuestContext *ctx, size_t size) {
  return recomp_vm_alloc_named(ctx, size, PROT_READ | PROT_WRITE, 0, "anon");
}

uint64_t recomp_vm_alloc_fixed(GuestContext *ctx, uint64_t desired_addr,
                               size_t size, int prot, int flags,
                               const char *name) {
  if (!ctx || size == 0)
    return (uint64_t)-1;
  GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
  uint64_t aligned_addr = desired_addr & ~4095ULL;
  size_t aligned_size = (size + 4095ULL) & ~4095ULL;

  printf("[ps4-vm] recomp_vm_alloc_fixed (%s): desired=0x%llx size=%zu (%.2f "
         "MB)\n",
         name ? name : "null", (unsigned long long)desired_addr, size,
         (double)size / (1024.0 * 1024.0));
  fflush(stdout);
  if (aligned_addr + aligned_size > proc->mem_size ||
      aligned_addr + aligned_size < aligned_addr) {
    return (uint64_t)-1;
  }

  pthread_mutex_lock(&proc->vm_mutex);

  GuestVMExtent *curr = proc->vm_extents;
  while (curr) {
    if ((curr->is_free || (curr->name[0] && strstr(curr->name, "reserved"))) &&
        curr->addr <= aligned_addr &&
        (curr->addr + curr->size) >= (aligned_addr + aligned_size)) {
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
  if (!ctx)
    return NULL;
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
  if (!ctx || addr == 0 || size == 0)
    return -1;
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

  // Case 1: aligned_addr is inside curr, but after curr->addr: split off the
  // prefix
  if (aligned_addr > curr->addr) {
    GuestVMExtent *mid = (GuestVMExtent *)calloc(1, sizeof(GuestVMExtent));
    if (mid) {
      mid->addr = aligned_addr;
      mid->size = curr->size - (aligned_addr - curr->addr);
      mid->is_free = false;
      mid->prev = curr;
      mid->next = curr->next;
      if (curr->next)
        curr->next->prev = mid;
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
      if (curr->next)
        curr->next->prev = suffix;
      curr->next = suffix;
      curr->size = aligned_size;
    }
  }

  // Now curr matches [aligned_addr, aligned_addr + aligned_size] exactly
  curr->is_free = true;

  // Coalesce with next if adjacent and free
  if (curr->next && curr->next->is_free &&
      (curr->addr + curr->size == curr->next->addr)) {
    GuestVMExtent *next_node = curr->next;
    curr->size += next_node->size;
    curr->next = next_node->next;
    if (next_node->next)
      next_node->next->prev = curr;
    free(next_node);
  }

  // Coalesce with prev if adjacent and free
  if (curr->prev && curr->prev->is_free &&
      (curr->prev->addr + curr->prev->size == curr->addr)) {
    GuestVMExtent *prev_node = curr->prev;
    prev_node->size += curr->size;
    prev_node->next = curr->next;
    if (curr->next)
      curr->next->prev = prev_node;
    free(curr);
    curr = prev_node;
  }

  pthread_mutex_unlock(&proc->vm_mutex);
  return 0;
}

GuestContext *recomp_create_thread_context(GuestContext *parent,
                                           uint64_t stack_size) {
  if (!parent)
    return NULL;
  if (stack_size == 0)
    stack_size = 2 * 1024 * 1024; // 2MB default stack
  GuestContext *proc = parent->process_ctx ? parent->process_ctx : parent;

  // Allocate stack and TCB (64KB) via VM extent allocator
  size_t alloc_total = stack_size + 0x10000ULL;
  uint64_t stack_base = recomp_vm_alloc(proc, alloc_total);
  if (stack_base == (uint64_t)-1) {
    fprintf(stderr,
            "[ps4-recomp] FATAL: out of guest memory for new thread stack\n");
    return NULL;
  }
  uint64_t tcb_base = stack_base + stack_size;

  // Initialize FS TCB base and stack canary
  *(uint64_t *)(parent->mem_base + tcb_base) = tcb_base;
  *(uint64_t *)(parent->mem_base + tcb_base + 0x28ULL) = 0x595e9fbd94fda766ULL;

  GuestContext *t_ctx = (GuestContext *)calloc(1, sizeof(GuestContext));
  if (!t_ctx)
    return NULL;

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
  if (!ctx)
    return;
  GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
  if (ctx->stack_base && ctx->stack_alloc_size > 0) {
    recomp_vm_free(proc, ctx->stack_base, ctx->stack_alloc_size);
  }
  free(ctx);
}

void recomp_vpcmpistri(GuestContext *ctx, const void *src2_ptr,
                       const void *src1_ptr, uint8_t imm8) {
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
      if (s2_bytes[i] == 0) {
        len2 = i;
        break;
      }
    }
    for (int i = 0; i < sz; i++) {
      if (s1_bytes[i] == 0) {
        len1 = i;
        break;
      }
    }
  } else {
    const uint16_t *s2_w = (const uint16_t *)src2_ptr;
    const uint16_t *s1_w = (const uint16_t *)src1_ptr;
    for (int i = 0; i < sz; i++) {
      if (s2_w[i] == 0) {
        len2 = i;
        break;
      }
    }
    for (int i = 0; i < sz; i++) {
      if (s1_w[i] == 0) {
        len1 = i;
        break;
      }
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
            if (s1_bytes[i] == s2_bytes[j]) {
              match = 1;
              break;
            }
          } else {
            const uint16_t *s2_w = (const uint16_t *)src2_ptr;
            const uint16_t *s1_w = (const uint16_t *)src1_ptr;
            if (s1_w[i] == s2_w[j]) {
              match = 1;
              break;
            }
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
                if (s1_bytes[i] >= lo && s1_bytes[i] <= hi) {
                  match = 1;
                  break;
                }
              } else {
                int8_t lo = (int8_t)s2_bytes[j];
                int8_t hi =
                    (j + 1 < len2) ? (int8_t)s2_bytes[j + 1] : (int8_t)0x7F;
                int8_t v = (int8_t)s1_bytes[i];
                if (v >= lo && v <= hi) {
                  match = 1;
                  break;
                }
              }
            } else {
              const uint16_t *s2_w = (const uint16_t *)src2_ptr;
              const uint16_t *s1_w = (const uint16_t *)src1_ptr;
              if (!is_signed) {
                uint16_t lo = s2_w[j];
                uint16_t hi = (j + 1 < len2) ? s2_w[j + 1] : 0xFFFF;
                if (s1_w[i] >= lo && s1_w[i] <= hi) {
                  match = 1;
                  break;
                }
              } else {
                int16_t lo = (int16_t)s2_w[j];
                int16_t hi =
                    (j + 1 < len2) ? (int16_t)s2_w[j + 1] : (int16_t)0x7FFF;
                int16_t v = (int16_t)s1_w[i];
                if (v >= lo && v <= hi) {
                  match = 1;
                  break;
                }
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
            if (s1_bytes[i + k] != s2_bytes[k]) {
              match = 0;
              break;
            }
          } else {
            const uint16_t *s2_w = (const uint16_t *)src2_ptr;
            const uint16_t *s1_w = (const uint16_t *)src1_ptr;
            if (s1_w[i + k] != s2_w[k]) {
              match = 0;
              break;
            }
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
  ctx->cf = (int_res2 != 0);
  ctx->zf = (len2 < sz);
  ctx->sf = (len1 < sz);
  ctx->of = (int_res2 & 1);
  ctx->af = 0;
  ctx->pf = 0;
}

void recomp_vpcmpe_stri(GuestContext *ctx, const void *src2_ptr,
                        const void *src1_ptr, uint8_t imm8) {
  int is_word = (imm8 & 1);
  int is_signed = ((imm8 >> 1) & 1);
  int agg = ((imm8 >> 2) & 3);
  int pol = ((imm8 >> 4) & 3);
  int msb_index = ((imm8 >> 6) & 1);

  int sz = is_word ? 8 : 16;
  int len1 = (int)(int32_t)ctx->rax;
  int len2 = (int)(int32_t)ctx->rdx;
  if (len1 < 0)
    len1 = 0;
  else if (len1 > sz)
    len1 = sz;
  if (len2 < 0)
    len2 = 0;
  else if (len2 > sz)
    len2 = sz;

  const uint8_t *s2_bytes = (const uint8_t *)src2_ptr;
  const uint8_t *s1_bytes = (const uint8_t *)src1_ptr;

  uint32_t int_res1 = 0;

  for (int i = 0; i < sz; i++) {
    int match = 0;
    switch (agg) {
    case 0: // Equal Any
      if (i < len1) {
        for (int j = 0; j < len2; j++) {
          if (!is_word) {
            if (s1_bytes[i] == s2_bytes[j]) {
              match = 1;
              break;
            }
          } else {
            const uint16_t *s2_w = (const uint16_t *)src2_ptr;
            const uint16_t *s1_w = (const uint16_t *)src1_ptr;
            if (s1_w[i] == s2_w[j]) {
              match = 1;
              break;
            }
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
                if (s1_bytes[i] >= lo && s1_bytes[i] <= hi) {
                  match = 1;
                  break;
                }
              } else {
                int8_t lo = (int8_t)s2_bytes[j];
                int8_t hi =
                    (j + 1 < len2) ? (int8_t)s2_bytes[j + 1] : (int8_t)0x7F;
                int8_t v = (int8_t)s1_bytes[i];
                if (v >= lo && v <= hi) {
                  match = 1;
                  break;
                }
              }
            } else {
              const uint16_t *s2_w = (const uint16_t *)src2_ptr;
              const uint16_t *s1_w = (const uint16_t *)src1_ptr;
              if (!is_signed) {
                uint16_t lo = s2_w[j];
                uint16_t hi = (j + 1 < len2) ? s2_w[j + 1] : 0xFFFF;
                if (s1_w[i] >= lo && s1_w[i] <= hi) {
                  match = 1;
                  break;
                }
              } else {
                int16_t lo = (int16_t)s2_w[j];
                int16_t hi =
                    (j + 1 < len2) ? (int16_t)s2_w[j + 1] : (int16_t)0x7FFF;
                int16_t v = (int16_t)s1_w[i];
                if (v >= lo && v <= hi) {
                  match = 1;
                  break;
                }
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
    case 3: // Equal Ordered
      match = 1;
      for (int k = 0; k < sz - i; k++) {
        if (k < len2) {
          if (i + k >= len1) {
            match = 0;
            break;
          }
          if (!is_word) {
            if (s1_bytes[i + k] != s2_bytes[k]) {
              match = 0;
              break;
            }
          } else {
            const uint16_t *s2_w = (const uint16_t *)src2_ptr;
            const uint16_t *s1_w = (const uint16_t *)src1_ptr;
            if (s1_w[i + k] != s2_w[k]) {
              match = 0;
              break;
            }
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
  case 0:
    int_res2 = int_res1;
    break;
  case 1:
    int_res2 = (~int_res1) & ((1U << sz) - 1);
    break;
  case 2:
    int_res2 = int_res1;
    break;
  case 3:
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
  ctx->cf = (int_res2 != 0);
  ctx->zf = (len2 < sz);
  ctx->sf = (len1 < sz);
  ctx->of = (int_res2 & 1);
  ctx->af = 0;
  ctx->pf = 0;
}

// ============================================================================
// AES-NI and PCLMULQDQ Helpers
// ============================================================================

xmm_reg_t recomp_pclmulqdq(uint64_t a, uint64_t b) {
  xmm_reg_t res = {0};
#if (defined(__aarch64__) || defined(__arm64__))
  poly64_t pa = (poly64_t)a;
  poly64_t pb = (poly64_t)b;
  poly128_t r = vmull_p64(pa, pb);
  memcpy(&res, &r, 16);
#else
  uint64_t lo = 0, hi = 0;
  for (int i = 0; i < 64; i++) {
    if ((b >> i) & 1) {
      lo ^= (a << i);
      if (i > 0) {
        hi ^= (a >> (64 - i));
      }
    }
  }
  res.u64[0] = lo;
  res.u64[1] = hi;
#endif
  return res;
}

static const uint8_t aes_sbox[256] = {
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t aes_inv_sbox[256] = {
  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

static inline uint8_t aes_xtime(uint8_t x) {
  return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0));
}

static inline void aes_mix_column(uint8_t *col) {
  uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
  col[0] = aes_xtime(a0 ^ a1) ^ a1 ^ a2 ^ a3;
  col[1] = aes_xtime(a1 ^ a2) ^ a2 ^ a3 ^ a0;
  col[2] = aes_xtime(a2 ^ a3) ^ a3 ^ a0 ^ a1;
  col[3] = aes_xtime(a3 ^ a0) ^ a0 ^ a1 ^ a2;
}

static inline uint8_t aes_mul_gf(uint8_t a, uint8_t b) {
  uint8_t p = 0;
  for (int i = 0; i < 8; i++) {
    if (b & 1) p ^= a;
    uint8_t hi = a & 0x80;
    a <<= 1;
    if (hi) a ^= 0x1B;
    b >>= 1;
  }
  return p;
}

static inline void aes_inv_mix_column(uint8_t *col) {
  uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
  col[0] = aes_mul_gf(a0, 0x0E) ^ aes_mul_gf(a1, 0x0B) ^ aes_mul_gf(a2, 0x0D) ^ aes_mul_gf(a3, 0x09);
  col[1] = aes_mul_gf(a0, 0x09) ^ aes_mul_gf(a1, 0x0E) ^ aes_mul_gf(a2, 0x0B) ^ aes_mul_gf(a3, 0x0D);
  col[2] = aes_mul_gf(a0, 0x0D) ^ aes_mul_gf(a1, 0x09) ^ aes_mul_gf(a2, 0x0E) ^ aes_mul_gf(a3, 0x0B);
  col[3] = aes_mul_gf(a0, 0x0B) ^ aes_mul_gf(a1, 0x0D) ^ aes_mul_gf(a2, 0x09) ^ aes_mul_gf(a3, 0x0E);
}

xmm_reg_t recomp_vaesenc(xmm_reg_t s1, xmm_reg_t s2) {
  xmm_reg_t state;
  state.u8[0]  = aes_sbox[s1.u8[0]];
  state.u8[4]  = aes_sbox[s1.u8[4]];
  state.u8[8]  = aes_sbox[s1.u8[8]];
  state.u8[12] = aes_sbox[s1.u8[12]];

  state.u8[1]  = aes_sbox[s1.u8[5]];
  state.u8[5]  = aes_sbox[s1.u8[9]];
  state.u8[9]  = aes_sbox[s1.u8[13]];
  state.u8[13] = aes_sbox[s1.u8[1]];

  state.u8[2]  = aes_sbox[s1.u8[10]];
  state.u8[6]  = aes_sbox[s1.u8[14]];
  state.u8[10] = aes_sbox[s1.u8[2]];
  state.u8[14] = aes_sbox[s1.u8[6]];

  state.u8[3]  = aes_sbox[s1.u8[15]];
  state.u8[7]  = aes_sbox[s1.u8[3]];
  state.u8[11] = aes_sbox[s1.u8[7]];
  state.u8[15] = aes_sbox[s1.u8[11]];

  aes_mix_column(&state.u8[0]);
  aes_mix_column(&state.u8[4]);
  aes_mix_column(&state.u8[8]);
  aes_mix_column(&state.u8[12]);

  state.u64[0] ^= s2.u64[0];
  state.u64[1] ^= s2.u64[1];
  return state;
}

xmm_reg_t recomp_vaesenclast(xmm_reg_t s1, xmm_reg_t s2) {
  xmm_reg_t state;
  state.u8[0]  = aes_sbox[s1.u8[0]];
  state.u8[4]  = aes_sbox[s1.u8[4]];
  state.u8[8]  = aes_sbox[s1.u8[8]];
  state.u8[12] = aes_sbox[s1.u8[12]];

  state.u8[1]  = aes_sbox[s1.u8[5]];
  state.u8[5]  = aes_sbox[s1.u8[9]];
  state.u8[9]  = aes_sbox[s1.u8[13]];
  state.u8[13] = aes_sbox[s1.u8[1]];

  state.u8[2]  = aes_sbox[s1.u8[10]];
  state.u8[6]  = aes_sbox[s1.u8[14]];
  state.u8[10] = aes_sbox[s1.u8[2]];
  state.u8[14] = aes_sbox[s1.u8[6]];

  state.u8[3]  = aes_sbox[s1.u8[15]];
  state.u8[7]  = aes_sbox[s1.u8[3]];
  state.u8[11] = aes_sbox[s1.u8[7]];
  state.u8[15] = aes_sbox[s1.u8[11]];

  state.u64[0] ^= s2.u64[0];
  state.u64[1] ^= s2.u64[1];
  return state;
}

xmm_reg_t recomp_vaesdec(xmm_reg_t s1, xmm_reg_t s2) {
  xmm_reg_t state;
  state.u8[0]  = aes_inv_sbox[s1.u8[0]];
  state.u8[4]  = aes_inv_sbox[s1.u8[4]];
  state.u8[8]  = aes_inv_sbox[s1.u8[8]];
  state.u8[12] = aes_inv_sbox[s1.u8[12]];

  state.u8[1]  = aes_inv_sbox[s1.u8[13]];
  state.u8[5]  = aes_inv_sbox[s1.u8[1]];
  state.u8[9]  = aes_inv_sbox[s1.u8[5]];
  state.u8[13] = aes_inv_sbox[s1.u8[9]];

  state.u8[2]  = aes_inv_sbox[s1.u8[10]];
  state.u8[6]  = aes_inv_sbox[s1.u8[14]];
  state.u8[10] = aes_inv_sbox[s1.u8[2]];
  state.u8[14] = aes_inv_sbox[s1.u8[6]];

  state.u8[3]  = aes_inv_sbox[s1.u8[7]];
  state.u8[7]  = aes_inv_sbox[s1.u8[11]];
  state.u8[11] = aes_inv_sbox[s1.u8[15]];
  state.u8[15] = aes_inv_sbox[s1.u8[3]];

  aes_inv_mix_column(&state.u8[0]);
  aes_inv_mix_column(&state.u8[4]);
  aes_inv_mix_column(&state.u8[8]);
  aes_inv_mix_column(&state.u8[12]);

  state.u64[0] ^= s2.u64[0];
  state.u64[1] ^= s2.u64[1];
  return state;
}

xmm_reg_t recomp_vaesdeclast(xmm_reg_t s1, xmm_reg_t s2) {
  xmm_reg_t state;
  state.u8[0]  = aes_inv_sbox[s1.u8[0]];
  state.u8[4]  = aes_inv_sbox[s1.u8[4]];
  state.u8[8]  = aes_inv_sbox[s1.u8[8]];
  state.u8[12] = aes_inv_sbox[s1.u8[12]];

  state.u8[1]  = aes_inv_sbox[s1.u8[13]];
  state.u8[5]  = aes_inv_sbox[s1.u8[1]];
  state.u8[9]  = aes_inv_sbox[s1.u8[5]];
  state.u8[13] = aes_inv_sbox[s1.u8[9]];

  state.u8[2]  = aes_inv_sbox[s1.u8[10]];
  state.u8[6]  = aes_inv_sbox[s1.u8[14]];
  state.u8[10] = aes_inv_sbox[s1.u8[2]];
  state.u8[14] = aes_inv_sbox[s1.u8[6]];

  state.u8[3]  = aes_inv_sbox[s1.u8[7]];
  state.u8[7]  = aes_inv_sbox[s1.u8[11]];
  state.u8[11] = aes_inv_sbox[s1.u8[15]];
  state.u8[15] = aes_inv_sbox[s1.u8[3]];

  state.u64[0] ^= s2.u64[0];
  state.u64[1] ^= s2.u64[1];
  return state;
}

xmm_reg_t recomp_vaesimc(xmm_reg_t s) {
  xmm_reg_t res = s;
  aes_inv_mix_column(&res.u8[0]);
  aes_inv_mix_column(&res.u8[4]);
  aes_inv_mix_column(&res.u8[8]);
  aes_inv_mix_column(&res.u8[12]);
  return res;
}

static inline uint32_t aes_subword(uint32_t w) {
  return ((uint32_t)aes_sbox[w & 0xFF]) |
         (((uint32_t)aes_sbox[(w >> 8) & 0xFF]) << 8) |
         (((uint32_t)aes_sbox[(w >> 16) & 0xFF]) << 16) |
         (((uint32_t)aes_sbox[(w >> 24) & 0xFF]) << 24);
}

static inline uint32_t aes_rotword(uint32_t w) {
  return (w >> 8) | ((w & 0xFF) << 24);
}

xmm_reg_t recomp_vaeskeygenassist(xmm_reg_t s, uint8_t rcon) {
  xmm_reg_t res = {0};
  uint32_t x1 = s.u32[1];
  uint32_t x3 = s.u32[3];
  uint32_t sw1 = aes_subword(x1);
  uint32_t sw3 = aes_subword(x3);
  res.u32[0] = sw1;
  res.u32[1] = aes_rotword(sw1) ^ (uint32_t)rcon;
  res.u32[2] = sw3;
  res.u32[3] = aes_rotword(sw3) ^ (uint32_t)rcon;
  return res;
}
