// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNet against shadPS4's libraries/net (net.cpp, net_kernel.cpp, net_resolver.cpp,
// net_translate.cpp) and core/net (guest_net.cpp, host_net.cpp), on loopback sockets: the error
// convention (ORBIS_NET_ERROR_BASE | errno, the per-thread sceNetErrnoLoc slot and the kernel
// errno), the helpers, memory pools, TCP and UDP with the PS4 socket options, blocking calls cut
// short by sceNetSocketAbort and close, epolls (including an asynchronous resolver as an epoll
// event), resolvers, the libkernel socket calls and kernel descriptor routing, and
// sceNetThreadCreate / sceNetThreadJoin. Shims are called as guest code calls them, on a guest
// memory region; each test thread has its own TCB (fs_base), so errno slots are per thread.

#include "ps4_net.h"
#include "ps4_net_core.h"
#include "ps4_net_p2p.h"
#include "ps4_threading.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

#define MEM_SIZE 0x800000ull
#define TCB_MAIN 0x1000ull     // fs_base of the main test thread
#define TCB_WORKER 0x2000ull   // fs_base of worker i: TCB_WORKER + i * 0x400
#define STACK_MAIN 0x40000ull  // shim call stacks: top of each 0x8000 block
#define VARS 0x60000ull        // guest structures
#define STRINGS 0x70000ull
#define BUFS 0x80000ull        // data buffers, 0x10000 each
#define HEAP 0x400000ull       // recomp_vm_alloc_named bump allocator
#define GUEST_THREAD_ENTRY 0x600000ull

#define ERR(e) ((int32_t)(0x80410100u | (uint32_t)(e)))

// ---- Runtime symbols the sources link against.

recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];
static uint8_t *mem;
static uint64_t heap_next = HEAP;
static pthread_mutex_t heap_mutex = PTHREAD_MUTEX_INITIALIZER;

uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot, int flags, const char *name) {
    pthread_mutex_lock(&heap_mutex);
    const uint64_t g = heap_next;
    heap_next += (size + 0xfff) & ~0xfffull;
    pthread_mutex_unlock(&heap_mutex);
    CHECK(heap_next <= GUEST_THREAD_ENTRY);
    memset(mem + g, 0xcd, size); // reused memory is not zeroed
    return g;
}

static int g_freed;
int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size) {
    g_freed++;
    return 0;
}

uint64_t sceKernelGetProcessTime(void) {
    return 123456789;
}

void shim_scePthreadExit(GuestContext *ctx) {
    pthread_exit(NULL);
}

typedef struct TestThread {
    pthread_t host;
    GuestContext ctx;
    void (*entry)(GuestContext *, void *);
    void *arg;
} TestThread;
static TestThread g_threads[8];
static int g_thread_count;
static char g_thread_name[32];

static void *thread_main(void *p) {
    TestThread *t = p;
    t->entry(&t->ctx, t->arg);
    return NULL;
}

int ps4_create_host_entry_thread(GuestContext *ctx, uint64_t thread_out, const PthreadAttr *attr,
                                 void (*entry)(GuestContext *child_ctx, void *arg), void *arg, const char *name) {
    const int i = ++g_thread_count;
    CHECK(i < 8);
    TestThread *t = &g_threads[i];
    t->ctx.mem_base = mem;
    t->ctx.mem_size = MEM_SIZE;
    t->ctx.fs_base = TCB_WORKER + 0x400ull * 7;
    t->ctx.rsp = STACK_MAIN + 0x8000ull * 7 - 64;
    t->entry = entry;
    t->arg = arg;
    snprintf(g_thread_name, sizeof g_thread_name, "%s", name ? name : "");
    if (thread_out) *(uint64_t *)(mem + thread_out) = (uint64_t)i;
    return pthread_create(&t->host, NULL, thread_main, t);
}

int ps4_join_guest_thread(uint64_t handle, uint64_t *ret) {
    if (handle == 0 || handle > (uint64_t)g_thread_count) return ESRCH;
    return pthread_join(g_threads[handle].host, NULL);
}

static void register_entry(uint64_t addr, recomp_fn_t fn) {
    const uint64_t l1 = (addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    const uint64_t l2 = (addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[l1]) g_dispatch_l1[l1] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[l1][l2]) g_dispatch_l1[l1][l2] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    g_dispatch_l1[l1][l2][addr & DISPATCH_LEVEL_MASK] = fn;
}

// ---- Shim calls.

void shim_sceNetInit(GuestContext *ctx);
void shim_sceNetErrnoLoc(GuestContext *ctx);
void shim_sceNetSocket(GuestContext *ctx);
void shim_sceNetSocketClose(GuestContext *ctx);
void shim_sceNetSocketAbort(GuestContext *ctx);
void shim_sceNetBind(GuestContext *ctx);
void shim_sceNetListen(GuestContext *ctx);
void shim_sceNetAccept(GuestContext *ctx);
void shim_sceNetConnect(GuestContext *ctx);
void shim_sceNetSend(GuestContext *ctx);
void shim_sceNetSendto(GuestContext *ctx);
void shim_sceNetSendmsg(GuestContext *ctx);
void shim_sceNetRecv(GuestContext *ctx);
void shim_sceNetRecvfrom(GuestContext *ctx);
void shim_sceNetRecvmsg(GuestContext *ctx);
void shim_sceNetSetsockopt(GuestContext *ctx);
void shim_sceNetGetsockopt(GuestContext *ctx);
void shim_sceNetGetsockname(GuestContext *ctx);
void shim_sceNetGetpeername(GuestContext *ctx);
void shim_sceNetShutdown(GuestContext *ctx);
void shim_sceNetIoctl(GuestContext *ctx);
void shim_sceNetGetSockInfo(GuestContext *ctx);
void shim_sceNetInetPton(GuestContext *ctx);
void shim_sceNetInetNtop(GuestContext *ctx);
void shim_sceNetEtherNtostr(GuestContext *ctx);
void shim_sceNetEtherStrton(GuestContext *ctx);
void shim_sceNetHtons(GuestContext *ctx);
void shim_sceNetHtonll(GuestContext *ctx);
void shim_sceNetPoolCreate(GuestContext *ctx);
void shim_sceNetPoolDestroy(GuestContext *ctx);
void shim_sceNetGetMemoryPoolStats(GuestContext *ctx);
void shim_sceNetShowRouteWithMemory(GuestContext *ctx);
void shim_sceNetEpollCreate(GuestContext *ctx);
void shim_sceNetEpollControl(GuestContext *ctx);
void shim_sceNetEpollWait(GuestContext *ctx);
void shim_sceNetEpollAbort(GuestContext *ctx);
void shim_sceNetEpollDestroy(GuestContext *ctx);
void shim_sceNetResolverCreate(GuestContext *ctx);
void shim_sceNetResolverDestroy(GuestContext *ctx);
void shim_sceNetResolverAbort(GuestContext *ctx);
void shim_sceNetResolverGetError(GuestContext *ctx);
void shim_sceNetResolverStartNtoa(GuestContext *ctx);
void shim_sceNetResolverStartAton(GuestContext *ctx);
void shim_sceNetResolverStartNtoaMultipleRecords(GuestContext *ctx);
void shim_sceNetResolverStartNtoaMultipleRecordsEx(GuestContext *ctx);
void shim_sceNetResolverConnect(GuestContext *ctx);
void shim_sceNetGetDnsInfo(GuestContext *ctx);
void shim_sceNetSetDnsInfo(GuestContext *ctx);
void shim_sceNetGetDns6Info(GuestContext *ctx);
void shim_sceNetGetIfName(GuestContext *ctx);
void shim_sceNetGetRandom(GuestContext *ctx);
void shim_sceNetGetSystemTime(GuestContext *ctx);
void shim_sceNetGetStatisticsInfo(GuestContext *ctx);
void shim_sceNetMemoryAllocate(GuestContext *ctx);
void shim_sceNetMemoryFree(GuestContext *ctx);
void shim_sceNetThreadCreate(GuestContext *ctx);
void shim_sceNetThreadJoin(GuestContext *ctx);
void shim_sceNetUsleep(GuestContext *ctx);
void shim_sceNetEmulationGet(GuestContext *ctx);
void shim_sceNetConfigWlanScan(GuestContext *ctx);
void shim_socket(GuestContext *ctx);
void shim_socketpair(GuestContext *ctx);
void shim_connect(GuestContext *ctx);
void shim_recv(GuestContext *ctx);
void shim_send(GuestContext *ctx);
void shim_htonl(GuestContext *ctx);

// One context per test thread (index 0: the main thread).
static GuestContext g_ctx[8];

static GuestContext *ctx_for(int thread) {
    GuestContext *c = &g_ctx[thread];
    if (!c->mem_base) {
        c->mem_base = mem;
        c->mem_size = MEM_SIZE;
        c->fs_base = thread == 0 ? TCB_MAIN : TCB_WORKER + 0x400ull * (uint64_t)(thread - 1);
    }
    return c;
}

// Calls a shim on thread `t` as guest code would: a return address slot, then stack arguments.
static int64_t callt(int t, void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t stack0) {
    GuestContext *c = ctx_for(t);
    const uint64_t top = STACK_MAIN + 0x8000ull * (uint64_t)(t + 1) - 64;
    c->rsp = top - 16;
    *(uint64_t *)(mem + c->rsp) = 0;
    *(uint64_t *)(mem + c->rsp + 8) = stack0;
    c->rdi = a0, c->rsi = a1, c->rdx = a2, c->rcx = a3, c->r8 = a4, c->r9 = a5;
    c->rax = 0xdeadbeef;
    shim(c);
    CHECK(c->rsp == top - 8); // returned to the caller
    return (int64_t)c->rax;
}

#define CALL(shim, ...) ((int32_t)callN(0, shim, __VA_ARGS__))
#define callN(t, shim, ...) callt_args(t, shim, __VA_ARGS__, 0, 0, 0, 0, 0, 0, 0)
#define callt_args(t, shim, a0, a1, a2, a3, a4, a5, s0, ...) callt(t, shim, a0, a1, a2, a3, a4, a5, s0)
#define CALL0(shim) ((int32_t)callt(0, shim, 0, 0, 0, 0, 0, 0, 0))

static int32_t net_errno(int t) {
    return *(int32_t *)(mem + ctx_for(t)->fs_base + RECOMP_TCB_NET_ERRNO_SLOT);
}
static int32_t kernel_errno(int t) {
    return *(int32_t *)(mem + ctx_for(t)->fs_base + RECOMP_TCB_ERRNO_SLOT);
}
static void clear_errnos(int t) {
    *(int32_t *)(mem + ctx_for(t)->fs_base + RECOMP_TCB_NET_ERRNO_SLOT) = 0;
    *(int32_t *)(mem + ctx_for(t)->fs_base + RECOMP_TCB_ERRNO_SLOT) = 0;
}

static void *at(uint64_t g) { return mem + g; }
static uint64_t str(uint64_t g, const char *s) {
    strcpy((char *)at(g), s);
    return g;
}
static int32_t *i32(uint64_t g) { return (int32_t *)at(g); }
static uint32_t *u32(uint64_t g) { return (uint32_t *)at(g); }

static uint64_t sockaddr_in_at(uint64_t g, uint32_t addr_host_order, uint16_t port) {
    OrbisNetSockaddrIn in;
    memset(&in, 0, sizeof(in));
    in.sin_len = sizeof(in);
    in.sin_family = ORBIS_NET_AF_INET;
    in.sin_port = htons(port);
    in.sin_addr = htonl(addr_host_order);
    memcpy(at(g), &in, sizeof(in));
    return g;
}

static uint16_t port_of(uint64_t g) {
    return ntohs(((OrbisNetSockaddrIn *)at(g))->sin_port);
}

static int32_t set_int_opt(int32_t s, int32_t level, int32_t name, int32_t value) {
    *i32(VARS + 0x500) = value;
    return CALL(shim_sceNetSetsockopt, (uint64_t)s, (uint64_t)level, (uint64_t)name, VARS + 0x500, 4);
}

static int32_t get_int_opt(int32_t s, int32_t level, int32_t name, int32_t *value) {
    *u32(VARS + 0x508) = 4;
    const int32_t r = CALL(shim_sceNetGetsockopt, (uint64_t)s, (uint64_t)level, (uint64_t)name, VARS + 0x500, VARS + 0x508);
    *value = *i32(VARS + 0x500);
    return r;
}

// A connected loopback TCP pair: *server_side is the accepted socket.
static void tcp_pair(int32_t *client, int32_t *server_side, int32_t *listener) {
    const int32_t l = CALL(shim_sceNetSocket, str(STRINGS, "listener"), ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    CHECK(l >= 0);
    CHECK(CALL(shim_sceNetBind, (uint64_t)l, sockaddr_in_at(VARS, 0x7f000001, 0), 16) == 0);
    CHECK(CALL(shim_sceNetListen, (uint64_t)l, 4) == 0);
    *u32(VARS + 0x40) = 16;
    CHECK(CALL(shim_sceNetGetsockname, (uint64_t)l, VARS + 0x20, VARS + 0x40) == 0);
    CHECK(*u32(VARS + 0x40) == 16 && ((OrbisNetSockaddrIn *)at(VARS + 0x20))->sin_len == 16);
    const int32_t c = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, ORBIS_NET_IPPROTO_TCP);
    CHECK(c >= 0);
    CHECK(CALL(shim_sceNetConnect, (uint64_t)c, VARS + 0x20, 16) == 0); // blocking connect
    *u32(VARS + 0x60) = 16;
    const int32_t a = CALL(shim_sceNetAccept, (uint64_t)l, VARS + 0x48, VARS + 0x60);
    CHECK(a >= 0 && *u32(VARS + 0x60) == 16);
    CHECK(((OrbisNetSockaddrIn *)at(VARS + 0x48))->sin_addr == htonl(0x7f000001));
    *client = c;
    *server_side = a;
    *listener = l;
}

// ---- Cases.

static int test_basics(void) {
    CHECK(CALL0(shim_sceNetInit) == 0);
    // sceNetErrnoLoc: a per-thread slot, apart from the kernel errno.
    CHECK((uint64_t)callt(0, shim_sceNetErrnoLoc, 0, 0, 0, 0, 0, 0, 0) == TCB_MAIN + RECOMP_TCB_NET_ERRNO_SLOT);
    CHECK((uint64_t)callt(1, shim_sceNetErrnoLoc, 0, 0, 0, 0, 0, 0, 0) == TCB_WORKER + RECOMP_TCB_NET_ERRNO_SLOT);

    // Socket kinds: EPROTONOSUPPORT for AF_INET6 and unknown types, EPROTOTYPE for a wrong
    // protocol (P2P sockets take none), ENAMETOOLONG for a 32-character name.
    clear_errnos(0);
    CHECK(CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET6, ORBIS_NET_SOCK_STREAM, 0) == ERR(ORBIS_NET_EPROTONOSUPPORT));
    CHECK(net_errno(0) == ORBIS_NET_EPROTONOSUPPORT && kernel_errno(0) == ORBIS_NET_EPROTONOSUPPORT);
    CHECK(CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, 7, 0) == ERR(ORBIS_NET_EPROTONOSUPPORT));
    CHECK(CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, ORBIS_NET_IPPROTO_UDP) ==
          ERR(ORBIS_NET_EPROTOTYPE));
    CHECK(CALL(shim_sceNetSocket, str(STRINGS, "0123456789abcdef0123456789abcdef"), ORBIS_NET_AF_INET,
               ORBIS_NET_SOCK_DGRAM, 0) == ERR(ORBIS_NET_ENAMETOOLONG));
    CHECK(CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_DGRAM_P2P, ORBIS_NET_IPPROTO_UDP) ==
          ERR(ORBIS_NET_EPROTOTYPE));
    // Errors land in the calling thread's slot only.
    clear_errnos(1);
    CHECK((int32_t)callt(1, shim_sceNetSocketClose, 12345, 0, 0, 0, 0, 0, 0) == ERR(ORBIS_NET_EBADF));
    CHECK(net_errno(1) == ORBIS_NET_EBADF && net_errno(0) == ORBIS_NET_EPROTOTYPE);

    // Byte order and addresses.
    CHECK((uint16_t)CALL(shim_sceNetHtons, 0x1234) == 0x3412);
    CHECK((uint64_t)callt(0, shim_sceNetHtonll, 0x0102030405060708ull, 0, 0, 0, 0, 0, 0) == 0x0807060504030201ull);
    CHECK(CALL(shim_sceNetInetPton, ORBIS_NET_AF_INET, str(STRINGS, "192.168.1.20"), VARS) == 1);
    CHECK(*u32(VARS) == htonl(0xc0a80114));
    CHECK(CALL(shim_sceNetInetPton, ORBIS_NET_AF_INET, str(STRINGS, "300.1.1.1"), VARS) == 0);
    CHECK(CALL(shim_sceNetInetPton, 99, str(STRINGS, "1.2.3.4"), VARS) == ERR(ORBIS_NET_EAFNOSUPPORT));
    CHECK(CALL(shim_sceNetInetPton, ORBIS_NET_AF_INET, 0, VARS) == ERR(ORBIS_NET_EFAULT));
    *u32(VARS) = htonl(0x0a000001);
    CHECK((uint64_t)CALL(shim_sceNetInetNtop, ORBIS_NET_AF_INET, VARS, STRINGS + 0x100, 16) == STRINGS + 0x100);
    CHECK(strcmp((char *)at(STRINGS + 0x100), "10.0.0.1") == 0);
    clear_errnos(0);
    CHECK(CALL(shim_sceNetInetNtop, ORBIS_NET_AF_INET, VARS, STRINGS + 0x100, 4) == 0);
    CHECK(net_errno(0) == ORBIS_NET_ENOSPC && kernel_errno(0) == 0); // shadPS4 sets only the net errno

    // Ethernet addresses.
    CHECK(CALL(shim_sceNetEtherStrton, str(STRINGS, "00:1A-2b:3c:4D:5e"), VARS) == 0);
    const uint8_t want[6] = {0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e};
    CHECK(memcmp(at(VARS), want, 6) == 0);
    CHECK(CALL(shim_sceNetEtherNtostr, VARS, STRINGS + 0x200, 18) == 0);
    CHECK(strcmp((char *)at(STRINGS + 0x200), "00:1a:2b:3c:4d:5e") == 0);
    CHECK(CALL(shim_sceNetEtherNtostr, VARS, STRINGS + 0x200, 17) == ERR(ORBIS_NET_ENOSPC));
    CHECK(CALL(shim_sceNetEtherStrton, str(STRINGS, "00:1a:2b:3c:4d"), VARS) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetEtherStrton, str(STRINGS, "00:1a:2b:3c:4d:5e:"), VARS) == ERR(ORBIS_NET_EINVAL));

    // Interface names live in guest memory; a bad index gives "" and EINVAL.
    const uint64_t eth0 = (uint64_t)callt(0, shim_sceNetGetIfName, 1, 0, 0, 0, 0, 0, 0);
    CHECK(eth0 && strcmp((char *)at(eth0), "eth0") == 0);
    clear_errnos(0);
    const uint64_t bad = (uint64_t)callt(0, shim_sceNetGetIfName, 10, 0, 0, 0, 0, 0, 0);
    CHECK(bad && *(char *)at(bad) == '\0' && net_errno(0) == ORBIS_NET_EINVAL);

    // Pools: ids from 3000, EINVAL below 4 KiB, ENOTEMPTY while a resolver holds one.
    CHECK(CALL(shim_sceNetPoolCreate, str(STRINGS, "pool"), 4095, 0) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetPoolCreate, str(STRINGS, "pool"), 4096, 1) == ERR(ORBIS_NET_EINVAL));
    const int32_t pool = CALL(shim_sceNetPoolCreate, str(STRINGS, "pool"), 16384, 0);
    CHECK(pool == 3000);
    CHECK(CALL(shim_sceNetGetMemoryPoolStats, (uint64_t)pool, VARS) == 0);
    CHECK(((OrbisNetMemoryPoolStats *)at(VARS))->pool_size == 16384);
    CHECK(CALL(shim_sceNetGetMemoryPoolStats, 2999, VARS) == ERR(ORBIS_NET_EBADF));
    CHECK(CALL(shim_sceNetShowRouteWithMemory, 2999) == ERR(ORBIS_NET_EBADF));
    CHECK(CALL(shim_sceNetShowRouteWithMemory, 0) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetShowRouteWithMemory, (uint64_t)pool) == 0);
    const int32_t rid = CALL(shim_sceNetResolverCreate, str(STRINGS, "res"), (uint64_t)pool, 0);
    CHECK(rid >= 0);
    CHECK(CALL(shim_sceNetResolverCreate, str(STRINGS, "res"), 2999, 0) == ERR(ORBIS_NET_EBADF));
    CHECK(CALL(shim_sceNetPoolDestroy, (uint64_t)pool) == ERR(ORBIS_NET_ENOTEMPTY));
    CHECK(CALL(shim_sceNetResolverDestroy, (uint64_t)rid) == 0);
    CHECK(CALL(shim_sceNetPoolDestroy, (uint64_t)pool) == 0);
    CHECK(CALL(shim_sceNetPoolDestroy, (uint64_t)pool) == ERR(ORBIS_NET_EBADF));

    // DNS servers: none while offline; an override is reported; NULL clears it.
    CHECK(CALL(shim_sceNetGetDnsInfo, VARS, 0) == 0 && *u32(VARS) == 0);
    *u32(VARS) = htonl(0x08080808);
    *u32(VARS + 4) = 0;
    CHECK(CALL(shim_sceNetSetDnsInfo, VARS, 0) == 0);
    CHECK(CALL(shim_sceNetGetDnsInfo, VARS + 8, 0) == 1 && *u32(VARS + 8) == htonl(0x08080808));
    CHECK(CALL(shim_sceNetSetDnsInfo, 0, 0) == 0);
    CHECK(CALL(shim_sceNetGetDnsInfo, VARS + 8, 0) == 0);
    *u32(VARS) = 0;
    CHECK(CALL(shim_sceNetSetDnsInfo, VARS, 0) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetGetDns6Info, VARS, 0) == 0);

    // Misc.
    CHECK(CALL(shim_sceNetGetRandom, 0) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetGetRandom, VARS) == 0);
    callt(0, shim_sceNetGetSystemTime, VARS, 0, 0, 0, 0, 0, 0);
    CHECK(*(uint64_t *)at(VARS) == 123456789);
    CHECK(CALL(shim_sceNetGetStatisticsInfo, VARS) == 0 && *i32(VARS) == 16 * 1024 * 1024 && *i32(VARS + 8) == 0);
    CHECK(CALL(shim_sceNetUsleep, (uint64_t)(uint32_t)-1) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetUsleep, 10) == 0);
    CHECK(CALL0(shim_sceNetEmulationGet) == ERR(ORBIS_NET_EOPNOTSUPP));
    CHECK(CALL0(shim_sceNetConfigWlanScan) == 0); // a stub in shadPS4 as well
    CHECK(CALL(shim_sceNetResolverConnect, 0) == (int32_t)0x804101e3 && net_errno(0) == 0xe3);

    // sceNetMemoryAllocate: guest memory, zero-filled with flag 2, freed by sceNetMemoryFree.
    CHECK(callt(0, shim_sceNetMemoryAllocate, 0, 0, 0, 0, 0, 0, 0) == 0);
    const uint64_t m = (uint64_t)callt(0, shim_sceNetMemoryAllocate, 100, 2, 0, 0, 0, 0, 0);
    CHECK(m > HEAP && m < GUEST_THREAD_ENTRY);
    for (int i = 0; i < 100; i++) CHECK(((uint8_t *)at(m))[i] == 0);
    const int freed = g_freed;
    callt(0, shim_sceNetMemoryFree, m, 0, 0, 0, 0, 0, 0);
    CHECK(g_freed == freed + 1);
    return 0;
}

static int test_tcp(void) {
    int32_t c, a, l;
    tcp_pair(&c, &a, &l);

    // Data both ways; MSG_WAITALL collects split writes.
    memcpy(at(BUFS), "hello, world", 12);
    CHECK(CALL(shim_sceNetSend, (uint64_t)c, BUFS, 5, 0) == 5);
    CHECK(CALL(shim_sceNetSend, (uint64_t)c, BUFS + 5, 7, 0) == 7);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)a, BUFS + 0x10000, 12, ORBIS_NET_MSG_WAITALL) == 12);
    CHECK(memcmp(at(BUFS + 0x10000), "hello, world", 12) == 0);
    *u32(VARS + 0x80) = 16;
    CHECK(CALL(shim_sceNetGetpeername, (uint64_t)a, VARS + 0x70, VARS + 0x80) == 0);
    CHECK(((OrbisNetSockaddrIn *)at(VARS + 0x70))->sin_family == ORBIS_NET_AF_INET);

    // FIONREAD and MSG_PEEKLEN without a buffer report what is queued.
    CHECK(CALL(shim_sceNetSend, (uint64_t)a, BUFS, 3, 0) == 3);
    usleep(20000);
    CHECK(CALL(shim_sceNetIoctl, (uint64_t)c, 0x4004667f, VARS + 0x90) == 0 && *i32(VARS + 0x90) == 3);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, 0, 64, ORBIS_NET_MSG_PEEKLEN) == 3);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, BUFS + 0x10000, 64, 0) == 3);

    // Options: PS4 buffer defaults, SO_TYPE, SO_NBIO, SO_ERROR, PS4-only options stored.
    int32_t v;
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_SNDBUF, &v) == 0 && v == 32768);
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_RCVBUF, &v) == 0 && v == 65536);
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_SNDBUF, 1 << 20) == 0);
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_SNDBUF, &v) == 0 && v == 512 * 1024);
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_SNDBUF, 0) == ERR(ORBIS_NET_EINVAL));
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_TYPE, &v) == 0 && v == ORBIS_NET_SOCK_STREAM);
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_ERROR, &v) == 0 && v == 0);
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_NBIO, &v) == 0 && v == 0);
    CHECK(get_int_opt(c, ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_NODELAY, &v) == 0 && v == 0);
    CHECK(set_int_opt(c, ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_NODELAY, 1) == 0);
    CHECK(get_int_opt(c, ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_NODELAY, &v) == 0 && v != 0);
    CHECK(get_int_opt(c, ORBIS_NET_IPPROTO_IP, ORBIS_NET_IP_TTLCHK, &v) == 0 && v == 0);
    CHECK(set_int_opt(c, ORBIS_NET_IPPROTO_IP, ORBIS_NET_IP_TTLCHK, 7) == 0);
    CHECK(get_int_opt(c, ORBIS_NET_IPPROTO_IP, ORBIS_NET_IP_TTLCHK, &v) == 0 && v == 7);
    CHECK(set_int_opt(c, ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_MSS_TO_ADVERTISE, 100) == ERR(ORBIS_NET_EINVAL));
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_PRIORITY, 7) == ERR(ORBIS_NET_EINVAL));
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_PRIORITY, 9) == 0);
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_PRIORITY, &v) == 0 && v == 9);
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_BROADCAST, 1) == 0); // ignored on TCP
    CHECK(set_int_opt(c, ORBIS_NET_IPPROTO_IP, ORBIS_NET_IP_HDRINCL, 1) == ERR(ORBIS_NET_EPROCUNAVAIL));
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_TYPE, 1) == ERR(ORBIS_NET_ENOPROTOOPT));
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, 0x7777, 1) == ERR(ORBIS_NET_ENOPROTOOPT));
    OrbisNetLinger linger = {1, 3};
    memcpy(at(VARS + 0x500), &linger, sizeof(linger));
    CHECK(CALL(shim_sceNetSetsockopt, (uint64_t)c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_LINGER, VARS + 0x500, 8) == 0);
    *u32(VARS + 0x508) = 8;
    memset(at(VARS + 0x500), 0, 8);
    CHECK(CALL(shim_sceNetGetsockopt, (uint64_t)c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_LINGER, VARS + 0x500, VARS + 0x508) ==
          0);
    CHECK(((OrbisNetLinger *)at(VARS + 0x500))->l_onoff != 0 && ((OrbisNetLinger *)at(VARS + 0x500))->l_linger == 3);
    CHECK(CALL(shim_sceNetSetsockopt, (uint64_t)c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_NAME, str(STRINGS, "renamed"), 8) ==
          0);

    // SO_RCVTIMEO (microseconds): an expired wait is EAGAIN; the BSD timeval form reads back.
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_RCVTIMEO, 50000) == 0);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, BUFS, 16, 0) == ERR(ORBIS_NET_EAGAIN));
    *u32(VARS + 0x520) = 16;
    CHECK(CALL(shim_sceNetGetsockopt, (uint64_t)c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_BSD_RCVTIMEO, VARS + 0x500,
               VARS + 0x520) == 0);
    CHECK(*u32(VARS + 0x520) == 16 && *(int64_t *)at(VARS + 0x500) == 0 && *(int64_t *)at(VARS + 0x508) == 50000);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, BUFS, 16, ORBIS_NET_MSG_DONTWAIT) == ERR(ORBIS_NET_EAGAIN));
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_NBIO, 1) == 0);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, BUFS, 16, 0) == ERR(ORBIS_NET_EAGAIN));
    CHECK(CALL(shim_sceNetIoctl, (uint64_t)c, 0x8004667e, (*i32(VARS + 0x90) = 0, VARS + 0x90)) == 0);
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_NBIO, &v) == 0 && v == 0);
    CHECK(CALL(shim_sceNetIoctl, (uint64_t)c, 0x1234, VARS + 0x90) == ERR(ORBIS_NET_EINVAL));

    // Socket information: one socket, and all of them.
    OrbisNetSockInfo *info = at(VARS + 0x1000);
    CHECK(CALL(shim_sceNetGetSockInfo, (uint64_t)c, VARS + 0x1000, 1, 0) == 1);
    CHECK(strcmp(info->name, "renamed") == 0 && info->s == c && info->socket_type == ORBIS_NET_SOCK_STREAM);
    CHECK(info->state == ORBIS_NET_SOCKINFO_STATE_ESTABLISHED && info->priority == 9);
    CHECK(info->send_buffer_size == 512 * 1024 && info->local_adr.inaddr_addr == htonl(0x7f000001));
    CHECK(CALL(shim_sceNetGetSockInfo, (uint64_t)(uint32_t)-1, 0, 0, 0) == 3);
    CHECK(CALL(shim_sceNetGetSockInfo, (uint64_t)(uint32_t)-1, VARS + 0x1000, 8, 0) == 3);
    CHECK(strcmp(info[0].name, "listener") == 0 || strcmp(info[1].name, "listener") == 0 ||
          strcmp(info[2].name, "listener") == 0);
    CHECK(CALL(shim_sceNetGetSockInfo, 9999, VARS + 0x1000, 1, 0) == ERR(ORBIS_NET_EBADF));

    // Reserved ports are EACCES; a stream connect to the broadcast address is EACCES.
    const int32_t r = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    CHECK(CALL(shim_sceNetBind, (uint64_t)r, sockaddr_in_at(VARS + 0x100, 0x7f000001, 80), 16) == ERR(ORBIS_NET_EACCES));
    CHECK(CALL(shim_sceNetBind, (uint64_t)r, sockaddr_in_at(VARS + 0x100, 0x7f000001, 40000), 16) ==
          ERR(ORBIS_NET_EACCES));
    CHECK(CALL(shim_sceNetBind, (uint64_t)r, VARS + 0x100, 8) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetConnect, (uint64_t)r, sockaddr_in_at(VARS + 0x100, 0xffffffff, 1234), 16) ==
          ERR(ORBIS_NET_EACCES));
    CHECK(CALL(shim_sceNetShutdown, (uint64_t)r, 3) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)r) == 0);

    // Shutdown then EOF.
    CHECK(CALL(shim_sceNetShutdown, (uint64_t)a, ORBIS_NET_SHUT_WR) == 0);
    CHECK(set_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_RCVTIMEO, 0) == 0);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, BUFS, 16, 0) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)c) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)c) == ERR(ORBIS_NET_EBADF));
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)a) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)l) == 0);
    return 0;
}

typedef struct Blocked {
    int thread; // test context index
    int32_t s;
    int32_t result;
    uint64_t buf;
} Blocked;

static void *blocked_recv(void *p) {
    Blocked *b = p;
    b->result = (int32_t)callt(b->thread, shim_sceNetRecv, (uint64_t)b->s, b->buf, 16, 0, 0, 0, 0);
    return NULL;
}

static void *blocked_accept(void *p) {
    Blocked *b = p;
    b->result = (int32_t)callt(b->thread, shim_sceNetAccept, (uint64_t)b->s, 0, 0, 0, 0, 0, 0);
    return NULL;
}

static int test_abort(void) {
    int32_t c, a, l;
    tcp_pair(&c, &a, &l);

    // Nothing blocked and no preservation: ENOTBLK.
    CHECK(CALL(shim_sceNetSocketAbort, (uint64_t)c, 0) == ERR(ORBIS_NET_ENOTBLK));

    // A blocked recv is cut short with EINTR; queued data stays.
    Blocked b = {1, c, 0, BUFS};
    pthread_t t;
    pthread_create(&t, NULL, blocked_recv, &b);
    usleep(50000);
    CHECK(CALL(shim_sceNetSocketAbort, (uint64_t)c, 0) == 0);
    pthread_join(t, NULL);
    CHECK(b.result == ERR(ORBIS_NET_EINTR) && net_errno(1) == ORBIS_NET_EINTR);

    // A preserved abort fails the next receive, once.
    CHECK(CALL(shim_sceNetSocketAbort, (uint64_t)c, ORBIS_NET_SOCKET_ABORT_FLAG_RCV_PRESERVATION) == 0);
    CHECK(CALL(shim_sceNetSend, (uint64_t)a, BUFS + 0x100, 4, 0) == 4);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, BUFS, 16, 0) == ERR(ORBIS_NET_EINTR));
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, BUFS, 16, 0) == 4);
    // A preserved send abort fails a send; non-blocking calls consume it as well.
    CHECK(CALL(shim_sceNetSocketAbort, (uint64_t)c, ORBIS_NET_SOCKET_ABORT_FLAG_SND_PRESERVATION) == 0);
    CHECK(CALL(shim_sceNetSend, (uint64_t)c, BUFS, 4, ORBIS_NET_MSG_DONTWAIT) == ERR(ORBIS_NET_EINTR));
    CHECK(CALL(shim_sceNetSend, (uint64_t)c, BUFS, 4, 0) == 4);

    // Close wakes a blocked accept (EINTR) and the id is gone afterwards.
    Blocked ba = {2, l, 0, 0};
    pthread_create(&t, NULL, blocked_accept, &ba);
    usleep(50000);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)l) == 0);
    pthread_join(t, NULL);
    CHECK(ba.result == ERR(ORBIS_NET_EINTR));
    CHECK(ps4_net_object_kind(l) == PS4_NET_OBJ_NONE && !ps4_net_id_reserved(l));

    // SO_ACCEPTTIMEO on a fresh listener.
    const int32_t l2 = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    CHECK(CALL(shim_sceNetBind, (uint64_t)l2, sockaddr_in_at(VARS, 0x7f000001, 0), 16) == 0);
    CHECK(CALL(shim_sceNetListen, (uint64_t)l2, 1) == 0);
    CHECK(set_int_opt(l2, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_ACCEPTTIMEO, 30000) == 0);
    CHECK(CALL(shim_sceNetAccept, (uint64_t)l2, 0, 0) == ERR(ORBIS_NET_EAGAIN));
    CHECK(CALL(shim_sceNetAccept, (uint64_t)l2, VARS, 0) == ERR(ORBIS_NET_EFAULT));

    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)l2) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)c) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)a) == 0);
    return 0;
}

typedef struct EpollWaiter {
    int thread;
    int32_t eid;
    int32_t result;
    uint64_t events;
} EpollWaiter;

static void *epoll_waiter(void *p) {
    EpollWaiter *w = p;
    w->result = (int32_t)callt(w->thread, shim_sceNetEpollWait, (uint64_t)w->eid, w->events, 4, (uint64_t)(uint32_t)-1, 0,
                               0, 0);
    return NULL;
}

static int32_t epoll_ctl(int32_t eid, int32_t op, int32_t id, uint32_t events, uint64_t data) {
    OrbisNetEpollEvent *ev = at(VARS + 0x200);
    memset(ev, 0, sizeof(*ev));
    ev->events = events;
    ev->data = data;
    return CALL(shim_sceNetEpollControl, (uint64_t)eid, (uint64_t)op, (uint64_t)id, events ? VARS + 0x200 : 0);
}

static int test_epoll(void) {
    int32_t c, a, l;
    tcp_pair(&c, &a, &l);
    CHECK(CALL(shim_sceNetEpollCreate, 0, 1) == ERR(ORBIS_NET_EINVAL));
    const int32_t eid = CALL(shim_sceNetEpollCreate, str(STRINGS, "ep"), 0);
    CHECK(eid >= 0);
    OrbisNetEpollEvent *out = at(VARS + 0x300);

    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, c, ORBIS_NET_EPOLLIN, 0x1111) == 0);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, c, ORBIS_NET_EPOLLIN, 0x1111) == ERR(ORBIS_NET_EEXIST));
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, eid, ORBIS_NET_EPOLLIN, 0) == ERR(ORBIS_NET_EPERM));
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, 9999, ORBIS_NET_EPOLLIN, 0) == ERR(ORBIS_NET_EBADF));
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, a, 0, 0) == ERR(ORBIS_NET_EINVAL)); // ADD needs an event
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_DEL, a, 0, 0) == ERR(ORBIS_NET_EBADF));  // not registered
    CHECK(epoll_ctl(c, ORBIS_NET_EPOLL_CTL_ADD, a, ORBIS_NET_EPOLLIN, 0) == ERR(ORBIS_NET_EBADF));
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 0);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 0, 0) == ERR(ORBIS_NET_EINVAL));

    CHECK(CALL(shim_sceNetSend, (uint64_t)a, BUFS, 4, 0) == 4);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 1000000) == 1);
    CHECK(out[0].events == ORBIS_NET_EPOLLIN && out[0].ident == (uint64_t)c && out[0].data == 0x1111);
    // Level-triggered: still readable.
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 1);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)c, BUFS, 16, 0) == 4);
    // MOD to EPOLLOUT; MOD of an unregistered id adds it.
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_MOD, c, ORBIS_NET_EPOLLOUT, 0x2222) == 0);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_MOD, a, ORBIS_NET_EPOLLOUT, 0x3333) == 0);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 2);
    CHECK(out[0].events == ORBIS_NET_EPOLLOUT && out[1].events == ORBIS_NET_EPOLLOUT);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_DEL, a, 0, 0) == 0);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_MOD, c, ORBIS_NET_EPOLLIN, 0x1111) == 0);

    // EpollAbort: a waiter gets EINTR; with nothing waiting ENOTBLK, or a preserved abort.
    CHECK(CALL(shim_sceNetEpollAbort, (uint64_t)eid, 0) == ERR(ORBIS_NET_ENOTBLK));
    CHECK(CALL(shim_sceNetEpollAbort, (uint64_t)eid, 2) == ERR(ORBIS_NET_EINVAL));
    EpollWaiter w = {1, eid, 0, VARS + 0x400};
    pthread_t t;
    pthread_create(&t, NULL, epoll_waiter, &w);
    usleep(50000);
    CHECK(CALL(shim_sceNetEpollAbort, (uint64_t)eid, 0) == 0);
    pthread_join(t, NULL);
    CHECK(w.result == ERR(ORBIS_NET_EINTR));
    CHECK(CALL(shim_sceNetEpollAbort, (uint64_t)eid, 1) == 0);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == ERR(ORBIS_NET_EINTR));
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 0);

    // A socket abort reports EPOLLHUP to a waiting epoll.
    pthread_create(&t, NULL, epoll_waiter, &w);
    usleep(50000);
    CHECK(CALL(shim_sceNetSocketAbort, (uint64_t)c, 0) == 0);
    pthread_join(t, NULL);
    const OrbisNetEpollEvent *wev = at(VARS + 0x400);
    CHECK(w.result == 1 && wev[0].events == ORBIS_NET_EPOLLHUP && wev[0].ident == (uint64_t)c && wev[0].data == 0x1111);

    // Closing a registered socket drops it; destroying the epoll wakes a waiter with EINTR.
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)c) == 0);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_DEL, c, 0, 0) == ERR(ORBIS_NET_EBADF));
    pthread_create(&t, NULL, epoll_waiter, &w);
    usleep(50000);
    CHECK(CALL(shim_sceNetEpollDestroy, (uint64_t)a) == ERR(ORBIS_NET_EPERM));
    CHECK(CALL(shim_sceNetEpollDestroy, (uint64_t)eid) == 0);
    pthread_join(t, NULL);
    CHECK(w.result == ERR(ORBIS_NET_EINTR));
    CHECK(CALL(shim_sceNetEpollDestroy, (uint64_t)eid) == ERR(ORBIS_NET_EBADF));
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)a) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)l) == 0);
    return 0;
}

static int test_udp(void) {
    const int32_t r = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_DGRAM, 0);
    const int32_t s = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_DGRAM, ORBIS_NET_IPPROTO_UDP);
    CHECK(r >= 0 && s >= 0);
    CHECK(CALL(shim_sceNetBind, (uint64_t)r, sockaddr_in_at(VARS, 0x7f000001, 0), 16) == 0);
    *u32(VARS + 0x40) = 16;
    CHECK(CALL(shim_sceNetGetsockname, (uint64_t)r, VARS + 0x20, VARS + 0x40) == 0);
    const uint16_t port = port_of(VARS + 0x20);
    CHECK(port != 0);
    int32_t v;
    CHECK(get_int_opt(s, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_SNDBUF, &v) == 0 && v == 9216);
    CHECK(get_int_opt(s, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_RCVBUF, &v) == 0 && v == 40960);
    CHECK(get_int_opt(s, ORBIS_NET_IPPROTO_IP, ORBIS_NET_IP_MULTICAST_TTL, &v) == 0 && v == 1);

    // Datagrams above the send buffer are EMSGSIZE.
    CHECK(CALL(shim_sceNetSendto, (uint64_t)s, BUFS, 9217, 0, VARS + 0x20, 16) == ERR(ORBIS_NET_EMSGSIZE));
    memcpy(at(BUFS), "datagram", 8);
    CHECK(CALL(shim_sceNetSendto, (uint64_t)s, BUFS, 8, 0, VARS + 0x20, 16) == 8);
    *u32(VARS + 0x80) = 16;
    CHECK(CALL(shim_sceNetRecvfrom, (uint64_t)r, BUFS + 0x10000, 64, 0, VARS + 0x60, VARS + 0x80) == 8);
    CHECK(memcmp(at(BUFS + 0x10000), "datagram", 8) == 0 && *u32(VARS + 0x80) == 16);
    CHECK(((OrbisNetSockaddrIn *)at(VARS + 0x60))->sin_addr == htonl(0x7f000001));
    // A short address buffer gets what fits and the full length.
    CHECK(CALL(shim_sceNetSendto, (uint64_t)s, BUFS, 3, 0, VARS + 0x20, 16) == 3);
    *u32(VARS + 0x80) = 4;
    CHECK(CALL(shim_sceNetRecvfrom, (uint64_t)r, BUFS + 0x10000, 64, 0, VARS + 0x60, VARS + 0x80) == 3);
    CHECK(*u32(VARS + 0x80) == 16);

    // The limited broadcast address needs SO_BROADCAST (EACCES, as FreeBSD).
    CHECK(CALL(shim_sceNetSendto, (uint64_t)s, BUFS, 4, 0, sockaddr_in_at(VARS + 0x100, 0xffffffff, port), 16) ==
          ERR(ORBIS_NET_EACCES));
    CHECK(get_int_opt(s, ORBIS_NET_IPPROTO_UDP, ORBIS_NET_UDP_SND_ON_SUSPEND, &v) == ERR(ORBIS_NET_EINVAL));

    // sendmsg / recvmsg gather and scatter.
    OrbisNetIovec *iov = at(VARS + 0x600);
    memcpy(at(BUFS + 0x100), "abc", 3);
    memcpy(at(BUFS + 0x200), "defgh", 5);
    iov[0] = (OrbisNetIovec){BUFS + 0x100, 3};
    iov[1] = (OrbisNetIovec){BUFS + 0x200, 5};
    OrbisNetMsghdr *msg = at(VARS + 0x700);
    memset(msg, 0, sizeof(*msg));
    msg->msg_name = VARS + 0x20;
    msg->msg_namelen = 16;
    msg->msg_iov = VARS + 0x600;
    msg->msg_iovlen = 2;
    CHECK(CALL(shim_sceNetSendmsg, (uint64_t)s, VARS + 0x700, 0) == 8);
    iov[0] = (OrbisNetIovec){BUFS + 0x300, 2};
    iov[1] = (OrbisNetIovec){BUFS + 0x400, 10};
    msg->msg_name = VARS + 0x800;
    msg->msg_namelen = 16;
    CHECK(CALL(shim_sceNetRecvmsg, (uint64_t)r, VARS + 0x700, 0) == 8);
    CHECK(memcmp(at(BUFS + 0x300), "ab", 2) == 0 && memcmp(at(BUFS + 0x400), "cdefgh", 6) == 0);
    CHECK(msg->msg_namelen == 16 && ((OrbisNetSockaddrIn *)at(VARS + 0x800))->sin_family == ORBIS_NET_AF_INET);
    CHECK(CALL(shim_sceNetRecvmsg, (uint64_t)r, VARS + 0x700, ORBIS_NET_MSG_PEEKLEN) == ERR(ORBIS_NET_EINVAL));
    msg->msg_iovlen = 1025;
    CHECK(CALL(shim_sceNetSendmsg, (uint64_t)s, VARS + 0x700, 0) == ERR(ORBIS_NET_EMSGSIZE));

    // Connected UDP: sendto with an address is EISCONN; send without one works.
    CHECK(CALL(shim_sceNetConnect, (uint64_t)s, VARS + 0x20, 16) == 0);
    CHECK(CALL(shim_sceNetSendto, (uint64_t)s, BUFS, 4, 0, VARS + 0x20, 16) == ERR(ORBIS_NET_EISCONN));
    CHECK(CALL(shim_sceNetSend, (uint64_t)s, BUFS, 4, 0) == 4);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)r, BUFS + 0x10000, 64, 0) == 4);
    // An unconnected send has no destination.
    CHECK(CALL(shim_sceNetSend, (uint64_t)r, BUFS, 4, 0) == ERR(ORBIS_NET_EDESTADDRREQ));
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)r) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)s) == 0);
    return 0;
}

static int test_resolver(void) {
    const int32_t pool = CALL(shim_sceNetPoolCreate, str(STRINGS, "rpool"), 8192, 0);
    CHECK(pool >= 3000);
    const int32_t rid = CALL(shim_sceNetResolverCreate, str(STRINGS, "r"), (uint64_t)pool, 0);
    CHECK(rid >= 0 && ps4_net_object_kind(rid) == PS4_NET_OBJ_EXTERNAL);
    CHECK(CALL(shim_sceNetResolverCreate, 0, (uint64_t)pool, 1) == ERR(ORBIS_NET_EINVAL));

    // Synchronous lookups: localhost and literals need no DNS.
    *u32(VARS) = 0;
    CHECK(CALL(shim_sceNetResolverStartNtoa, (uint64_t)rid, str(STRINGS, "localhost."), VARS, 0, 0, 0) == 0);
    CHECK(*u32(VARS) == htonl(0x7f000001));
    CHECK(CALL(shim_sceNetResolverStartNtoa, (uint64_t)rid, str(STRINGS, "10.1.2.3"), VARS, 0, 0, 0) == 0);
    CHECK(*u32(VARS) == htonl(0x0a010203));
    // Without literals and offline (no PS4_NETWORK): no DNS.
    CHECK(CALL(shim_sceNetResolverStartNtoa, (uint64_t)rid, str(STRINGS, "10.1.2.3"), VARS, 0, 0,
               ORBIS_NET_RESOLVER_START_NTOA_DISABLE_IPADDRESS) == (int32_t)0x804101e1);
    CHECK(net_errno(0) == 0xe1);
    CHECK(CALL(shim_sceNetResolverGetError, (uint64_t)rid, VARS + 8) == 0 && *i32(VARS + 8) == (int32_t)0x804101e1);
    CHECK(CALL(shim_sceNetResolverStartNtoa, (uint64_t)rid, str(STRINGS, ""), VARS, 0, 0, 0) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetResolverStartNtoa, 9999, str(STRINGS, "localhost"), VARS, 0, 0, 0) == ERR(ORBIS_NET_EBADF));

    // Multiple records.
    OrbisNetResolverInfo *info = at(VARS + 0x100);
    memset(info, 0xff, sizeof(*info));
    CHECK(CALL(shim_sceNetResolverStartNtoaMultipleRecords, (uint64_t)rid, str(STRINGS, "127.0.0.2"), VARS + 0x100, 0, 0,
               0) == 0);
    CHECK(info->records == 1 && info->recordsv4 == 1 && info->addrs[0].af == ORBIS_NET_AF_INET);
    CHECK(info->addrs[0].u.addr.inaddr_addr == htonl(0x7f000002) && info->addrs[1].af == 0);
    CHECK(CALL(shim_sceNetResolverStartNtoaMultipleRecordsEx, (uint64_t)rid, str(STRINGS, "localhost"), VARS + 0x100, 0,
               0, 0x1000000) == (int32_t)0x804101e9);
    CHECK(CALL(shim_sceNetResolverStartNtoaMultipleRecordsEx, (uint64_t)rid, str(STRINGS, "localhost"), VARS + 0x100, 0,
               0, 0x3000000) == 0);
    CHECK(CALL(shim_sceNetResolverStartNtoaMultipleRecordsEx, (uint64_t)rid, str(STRINGS, "localhost"), VARS + 0x100, 0,
               0, 0x4) == ERR(ORBIS_NET_EINVAL));

    // Reverse lookup (flags are the 7th argument); a short buffer is ENOSPACE.
    *u32(VARS) = htonl(0x7f000001);
    CHECK((int32_t)callt(0, shim_sceNetResolverStartAton, (uint64_t)rid, VARS, STRINGS + 0x300, 64, 0, 0, 0) == 0);
    CHECK(strcmp((char *)at(STRINGS + 0x300), "localhost") == 0);
    CHECK((int32_t)callt(0, shim_sceNetResolverStartAton, (uint64_t)rid, VARS, STRINGS + 0x300, 9, 0, 0, 0) ==
          (int32_t)0x804101de);

    // An asynchronous lookup is an epoll event: EPOLLIN | EPOLLDESCID, reported once.
    const int32_t eid = CALL(shim_sceNetEpollCreate, 0, 0);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, rid, ORBIS_NET_EPOLLIN, 0xabc) == 0);
    *u32(VARS) = 0;
    CHECK(CALL(shim_sceNetResolverStartNtoa, (uint64_t)rid, str(STRINGS, "localhost"), VARS, 0, 0,
               ORBIS_NET_RESOLVER_ASYNC) == 0);
    OrbisNetEpollEvent *out = at(VARS + 0x300);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 2000000) == 1);
    CHECK(out[0].events == (ORBIS_NET_EPOLLIN | ORBIS_NET_EPOLLDESCID) && out[0].ident == (uint64_t)rid &&
          out[0].data == 0xabc);
    CHECK(*u32(VARS) == htonl(0x7f000001));
    CHECK(CALL(shim_sceNetResolverGetError, (uint64_t)rid, VARS + 8) == 0 && *i32(VARS + 8) == 0);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 0);
    CHECK(CALL(shim_sceNetEpollDestroy, (uint64_t)eid) == 0);

    // A preserved abort fails the next lookup of its kind with EINTR.
    CHECK(CALL(shim_sceNetResolverAbort, (uint64_t)rid, ORBIS_NET_RESOLVER_ABORT_FLAG_NTOA_PRESERVATION) == 0);
    CHECK(CALL(shim_sceNetResolverStartNtoa, (uint64_t)rid, str(STRINGS, "localhost"), VARS, 0, 0, 0) ==
          ERR(ORBIS_NET_EINTR));
    CHECK(CALL(shim_sceNetResolverStartNtoa, (uint64_t)rid, str(STRINGS, "localhost"), VARS, 0, 0, 0) == 0);
    CHECK(CALL(shim_sceNetResolverAbort, 9999, 0) == ERR(ORBIS_NET_EBADF));

    // Closing a resolver id through the kernel destroys it and releases its pool.
    CHECK(ps4_net_kernel_close(ctx_for(0), rid) == 0);
    CHECK(CALL(shim_sceNetResolverDestroy, (uint64_t)rid) == ERR(ORBIS_NET_EBADF));
    CHECK(CALL(shim_sceNetPoolDestroy, (uint64_t)pool) == 0);
    return 0;
}

static int test_kernel(void) {
    GuestContext *ctx = ctx_for(0);
    // The libkernel socket calls: POSIX results, the kernel errno, the net errno untouched.
    clear_errnos(0);
    *i32(TCB_MAIN + RECOMP_TCB_NET_ERRNO_SLOT) = 77;
    CHECK(callt(0, shim_socket, ORBIS_NET_AF_INET6, ORBIS_NET_SOCK_STREAM, 0, 0, 0, 0, 0) == -1);
    CHECK(kernel_errno(0) == ORBIS_NET_EPROTONOSUPPORT && net_errno(0) == 77);
    CHECK(callt(0, shim_socketpair, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0, VARS, 0, 0, 0) == -1);
    CHECK(kernel_errno(0) == ORBIS_NET_EAFNOSUPPORT);
    CHECK(callt(0, shim_socketpair, ORBIS_NET_AF_UNIX, ORBIS_NET_SOCK_STREAM, 0, VARS, 0, 0, 0) == 0);
    const int32_t s0 = *i32(VARS), s1 = *i32(VARS + 4);
    CHECK(ps4_net_kernel_owns(s0) && ps4_net_kernel_owns(s1));
    CHECK((uint32_t)callt(0, shim_htonl, 0x01020304, 0, 0, 0, 0, 0, 0) == 0x04030201);

    // Kernel read / write on socket descriptors go through the net layer (blocking emulation).
    CHECK(ps4_net_kernel_write(ctx, s0, "ping", 4) == 4);
    char buf[16];
    CHECK(ps4_net_kernel_read(ctx, s1, buf, sizeof buf) == 4 && memcmp(buf, "ping", 4) == 0);
    CHECK(callt(0, shim_send, (uint64_t)s1, BUFS, 2, 0, 0, 0, 0) == 2);
    CHECK(callt(0, shim_recv, (uint64_t)s0, BUFS + 0x10000, 16, 0, 0, 0, 0) == 2);
    // fcntl: FreeBSD O_RDWR, O_NONBLOCK through F_SETFL; a non-blocking read is EAGAIN.
    CHECK(ps4_net_kernel_fcntl(ctx, s0, 3, 0) == 0x2);
    CHECK(ps4_net_kernel_fcntl(ctx, s0, 4, 0x4) == 0);
    CHECK(ps4_net_kernel_fcntl(ctx, s0, 3, 0) == 0x6);
    CHECK(ps4_net_kernel_read(ctx, s0, buf, sizeof buf) == -1 && kernel_errno(0) == ORBIS_NET_EAGAIN);
    CHECK(net_errno(0) == 77);
    CHECK(ps4_net_kernel_fcntl(ctx, s0, 1, 0) == -1 && kernel_errno(0) == ORBIS_NET_EINVAL);
    CHECK(ps4_net_kernel_read(ctx, 0, buf, 1) == -1 && kernel_errno(0) == ORBIS_NET_EBADF);

    // dup: a second id for the same socket; closing it leaves the first.
    const int32_t d = dup(s1);
    CHECK(d >= 0 && ps4_net_kernel_duplicate(ctx, s1, d) == 0);
    CHECK(ps4_net_kernel_write(ctx, d, "x", 1) == 1);
    CHECK(ps4_net_kernel_close(ctx, d) == 0);
    CHECK(ps4_net_kernel_write(ctx, s1, "y", 1) == 1);
    CHECK(ps4_net_kernel_fcntl(ctx, s0, 4, 0) == 0);
    CHECK(ps4_net_kernel_read(ctx, s0, buf, sizeof buf) == 2 && memcmp(buf, "xy", 2) == 0);

    // Close through the kernel; the descriptors are gone afterwards.
    CHECK(ps4_net_kernel_close(ctx, s0) == 0 && ps4_net_kernel_close(ctx, s1) == 0);
    CHECK(!ps4_net_kernel_owns(s0) && ps4_net_kernel_close(ctx, s0) == -1 && kernel_errno(0) == ORBIS_NET_EBADF);
    CHECK(fcntl(s0, F_GETFD) == -1 && errno == EBADF);

    // A libkernel connect to a closed port: ECONNREFUSED.
    const int32_t c = (int32_t)callt(0, shim_socket, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0, 0, 0, 0, 0);
    const int32_t l = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    CHECK(CALL(shim_sceNetBind, (uint64_t)l, sockaddr_in_at(VARS, 0x7f000001, 0), 16) == 0);
    *u32(VARS + 0x40) = 16;
    CHECK(CALL(shim_sceNetGetsockname, (uint64_t)l, VARS + 0x20, VARS + 0x40) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)l) == 0); // nobody listens there now
    CHECK(callt(0, shim_connect, (uint64_t)c, VARS + 0x20, 16, 0, 0, 0, 0) == -1);
    CHECK(kernel_errno(0) == ORBIS_NET_ECONNREFUSED);
    CHECK(ps4_net_kernel_close(ctx, c) == 0);
    return 0;
}

static volatile int g_thread_ran;
static uint64_t g_thread_arg;

static void guest_thread_entry(GuestContext *ctx) {
    g_thread_arg = ctx->rdi;
    g_thread_ran = 1;
    ctx->rsp += 8; // return to the caller
}

static int test_thread(void) {
    register_entry(GUEST_THREAD_ENTRY, guest_thread_entry);
    OrbisNetThreadParam *param = at(VARS);
    memset(param, 0, sizeof(*param));
    param->entry = GUEST_THREAD_ENTRY;
    param->arg = 0x5555;
    param->flags = 1;
    CHECK(CALL(shim_sceNetThreadCreate, VARS + 0x40, 0, str(STRINGS, "worker")) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetThreadCreate, VARS + 0x40, VARS, str(STRINGS, "worker")) == 0);
    const uint64_t handle = *(uint64_t *)at(VARS + 0x40);
    CHECK(CALL(shim_sceNetThreadJoin, handle) == 0);
    CHECK(g_thread_ran && g_thread_arg == 0x5555 && strcmp(g_thread_name, "worker") == 0);
    // A failed join is ORBIS_KERNEL_ERROR_UNKNOWN + errno; shadPS4's ThreadReturn sets net errno
    // 0xcd for it (only 0x....01xx codes carry an errno in their low byte).
    CHECK(CALL(shim_sceNetThreadJoin, 99) == (int32_t)(0x80020000 + ESRCH) && net_errno(0) == 0xcd);
    return 0;
}

// A P2P address: for DGRAM_P2P port is the UDP port and vport the virtual port; for
// STREAM_P2P port is the TCP port and vport the UDP port.
static uint64_t p2p_addr_at(uint64_t g, uint32_t addr_host_order, uint16_t port, uint16_t vport) {
    sockaddr_in_at(g, addr_host_order, port);
    ((OrbisNetSockaddrIn *)at(g))->sin_vport = htons(vport);
    return g;
}

static uint16_t vport_of(uint64_t g) {
    return ntohs(((OrbisNetSockaddrIn *)at(g))->sin_vport);
}

typedef struct Transfer {
    int thread;
    int32_t s;
    uint64_t buf;
    uint32_t len;
    int32_t result;
} Transfer;

static void *blocking_send(void *p) {
    Transfer *t = p;
    t->result = (int32_t)callt(t->thread, shim_sceNetSend, (uint64_t)t->s, t->buf, t->len, 0, 0, 0, 0);
    return NULL;
}

static void fill_pattern(uint64_t g, uint32_t len, uint32_t seed) {
    for (uint32_t i = 0; i < len; i++) ((uint8_t *)at(g))[i] = (uint8_t)(i * 131u + seed + (i >> 9));
}

// Sends len bytes from `from` (thread 1, blocking) while the main thread receives them whole
// (MSG_WAITALL), and checks them.
static void p2p_transfer(int32_t from, int32_t to, uint32_t len, uint32_t seed) {
    const uint64_t src = 0x100000, dst = 0x180000;
    CHECK(len <= 0x80000);
    fill_pattern(src, len, seed);
    memset(at(dst), 0, len);
    Transfer t = {1, from, src, len, 0};
    pthread_t th;
    pthread_create(&th, NULL, blocking_send, &t);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)to, dst, len, ORBIS_NET_MSG_WAITALL) == (int32_t)len);
    pthread_join(th, NULL);
    CHECK(t.result == (int32_t)len);
    CHECK(memcmp(at(src), at(dst), len) == 0);
}

static int32_t p2p_ioctl(int32_t s, uint32_t cmd, uint64_t data) {
    return CALL(shim_sceNetIoctl, (uint64_t)s, cmd, data);
}

static int test_p2p(void) {
    setenv("PS4_P2P_PORT", "0", 1); // any free UDP port for the transport
    const uint32_t lo = 0x7f000001;

    // ---- DGRAM_P2P
    const int32_t a = CALL(shim_sceNetSocket, str(STRINGS, "p2p-a"), ORBIS_NET_AF_INET, ORBIS_NET_SOCK_DGRAM_P2P, 0);
    const int32_t b = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_DGRAM_P2P, 0);
    CHECK(a >= 0 && b >= 0 && ps4_net_p2p_transport_is_ready());
    int32_t v = 0;
    CHECK(get_int_opt(a, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_TYPE, &v) == 0 && v == ORBIS_NET_SOCK_DGRAM_P2P);
    // An unbound datagram socket cannot connect; binds need a UDP port, a unicast address and a
    // vport below the system range; vports are exclusive.
    CHECK(CALL(shim_sceNetConnect, (uint64_t)b, p2p_addr_at(VARS, lo, 3658, 1000), 16) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetBind, (uint64_t)a, p2p_addr_at(VARS, 0, 0, 1000), 16) == ERR(ORBIS_NET_EADDRNOTAVAIL));
    CHECK(CALL(shim_sceNetBind, (uint64_t)a, p2p_addr_at(VARS, 0xe0000001, 3658, 1000), 16) ==
          ERR(ORBIS_NET_EADDRNOTAVAIL));
    CHECK(CALL(shim_sceNetBind, (uint64_t)a, p2p_addr_at(VARS, 0, 3658, 40000), 16) == ERR(ORBIS_NET_EACCES));
    CHECK(CALL(shim_sceNetBind, (uint64_t)a, p2p_addr_at(VARS, 0, 3658, 1000), 8) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetBind, (uint64_t)a, p2p_addr_at(VARS, 0, 3658, 1000), 16) == 0);
    CHECK(CALL(shim_sceNetBind, (uint64_t)b, p2p_addr_at(VARS, 0, 3658, 1000), 16) == ERR(ORBIS_NET_EADDRINUSE));
    CHECK(CALL(shim_sceNetBind, (uint64_t)b, p2p_addr_at(VARS, 0, 3658, 1001), 16) == 0);
    *u32(VARS + 0x40) = 16;
    CHECK(CALL(shim_sceNetGetsockname, (uint64_t)a, VARS + 0x20, VARS + 0x40) == 0);
    const uint16_t udp = port_of(VARS + 0x20);
    CHECK(udp != 0 && udp == ps4_net_p2p_bound_port() && vport_of(VARS + 0x20) == 1000);
    // Host-socket options do not apply.
    CHECK(set_int_opt(a, ORBIS_NET_IPPROTO_IP, ORBIS_NET_IP_ADD_MEMBERSHIP, 0) == ERR(ORBIS_NET_EPROCUNAVAIL));
    CHECK(CALL(shim_sceNetIoctl, (uint64_t)a, 0x4004667f, VARS) == ERR(ORBIS_NET_EINVAL)); // FIONREAD

    // b -> a over the transport's UDP socket.
    memcpy(at(BUFS), "hello p2p", 9);
    CHECK(CALL(shim_sceNetSendto, (uint64_t)b, BUFS, 9, 0, p2p_addr_at(VARS, lo, udp, 1000), 16) == 9);
    CHECK(CALL(shim_sceNetSendto, (uint64_t)b, BUFS, 9, 0, p2p_addr_at(VARS, lo, udp, 0), 16) ==
          ERR(ORBIS_NET_EADDRNOTAVAIL));
    *u32(VARS + 0x40) = 16;
    memset(at(BUFS + 0x100), 0, 64);
    CHECK(CALL(shim_sceNetRecvfrom, (uint64_t)a, BUFS + 0x100, 64, 0, VARS + 0x20, VARS + 0x40) == 9);
    CHECK(memcmp(at(BUFS + 0x100), "hello p2p", 9) == 0);
    CHECK(((OrbisNetSockaddrIn *)at(VARS + 0x20))->sin_addr == htonl(lo) && port_of(VARS + 0x20) == udp &&
          vport_of(VARS + 0x20) == 1001);
    CHECK(CALL(shim_sceNetRecvfrom, (uint64_t)a, BUFS + 0x100, 64, ORBIS_NET_MSG_DONTWAIT, 0, 0) ==
          ERR(ORBIS_NET_EAGAIN));

    // Epoll: readable once a datagram is queued; a datagram socket is always writable.
    const int32_t eid = CALL(shim_sceNetEpollCreate, str(STRINGS, "p2p-ep"), 0);
    OrbisNetEpollEvent *out = at(VARS + 0x300);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, a, ORBIS_NET_EPOLLIN, 0xa) == 0);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 0);
    CHECK(CALL(shim_sceNetSendto, (uint64_t)b, BUFS, 3, 0, p2p_addr_at(VARS, lo, udp, 1000), 16) == 3);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 1000000) == 1);
    CHECK(out[0].events == ORBIS_NET_EPOLLIN && out[0].ident == (uint64_t)a && out[0].data == 0xa);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)a, BUFS + 0x100, 64, 0) == 3);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 0);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_MOD, a, ORBIS_NET_EPOLLOUT, 0xb) == 0);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 1 && out[0].events == ORBIS_NET_EPOLLOUT);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_DEL, a, 0, 0) == 0);

    // A connected datagram socket: its peer only, and no address on send.
    CHECK(CALL(shim_sceNetConnect, (uint64_t)b, p2p_addr_at(VARS, lo, udp, 1000), 16) == 0);
    CHECK(CALL(shim_sceNetConnect, (uint64_t)b, p2p_addr_at(VARS, lo, udp, 1000), 16) == ERR(ORBIS_NET_EISCONN));
    CHECK(CALL(shim_sceNetSendto, (uint64_t)b, BUFS, 9, 0, p2p_addr_at(VARS, lo, udp, 1000), 16) ==
          ERR(ORBIS_NET_EISCONN));
    *u32(VARS + 0x40) = 16;
    CHECK(CALL(shim_sceNetGetpeername, (uint64_t)b, VARS + 0x20, VARS + 0x40) == 0 && vport_of(VARS + 0x20) == 1000);
    *u32(VARS + 0x40) = 16;
    CHECK(CALL(shim_sceNetGetpeername, (uint64_t)a, VARS + 0x20, VARS + 0x40) == ERR(ORBIS_NET_ENOTCONN));
    CHECK(CALL(shim_sceNetSend, (uint64_t)b, BUFS, 9, 0) == 9);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)a, BUFS + 0x100, 4, 0) == 4); // the rest of the datagram is dropped
    CHECK(CALL(shim_sceNetRecv, (uint64_t)a, BUFS + 0x100, 64, ORBIS_NET_MSG_DONTWAIT) == ERR(ORBIS_NET_EAGAIN));

    // Keys (net_p2p_ioctl): a record is the peer address, the 16-byte key, the local port,
    // flags and the key id.
    uint8_t *rec = at(VARS + 0x200);
    memset(rec, 0, 36);
    for (int i = 0; i < 16; i++) rec[16 + i] = (uint8_t)(0xa0 + i);
    rec[35] = 7; // id
    CHECK(p2p_ioctl(a, 0x802450cd, VARS + 0x200) == 0); // wildcard key
    memset(rec + 16, 0, 16);
    CHECK(p2p_ioctl(a, 0xc02450ce, VARS + 0x200) == 0 && rec[16] == 0xa0 && rec[31] == 0xaf);
    CHECK(p2p_ioctl(a, 0x400250cf, VARS + 0x200) == 0 && *(uint16_t *)rec == htons(udp)); // the NP port
    CHECK(p2p_ioctl(a, 0x200050c8, 0) == 0);                                              // enable
    CHECK(p2p_ioctl(a, 0x802450cb, 0) == ERR(ORBIS_NET_EFAULT));
    CHECK(p2p_ioctl(a, 0x802450ff, VARS + 0x200) == ERR(ORBIS_NET_EINVAL));
    CHECK(p2p_ioctl(a, 0x8024661f, VARS + 0x200) == ERR(ORBIS_NET_EINVAL));
    p2p_addr_at(VARS + 0x200, lo, udp, 0);
    CHECK(p2p_ioctl(a, 0x802450cc, VARS + 0x200) == ERR(ORBIS_NET_ENOENT)); // not installed
    // Crypto and signature, per socket and per message: a decrypts with the same keys.
    CHECK(set_int_opt(b, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_USECRYPTO, 1) == 0);
    CHECK(get_int_opt(b, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_USECRYPTO, &v) == 0 && v == 1);
    CHECK(get_int_opt(b, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_USESIGNATURE, &v) == 0 && v == 0);
    CHECK(set_int_opt(b, ORBIS_NET_IPPROTO_UDP, ORBIS_NET_UDP_SND_ON_SUSPEND, 1) == ERR(ORBIS_NET_EPROCUNAVAIL));
    fill_pattern(BUFS + 0x1000, 1000, 3);
    CHECK(CALL(shim_sceNetSend, (uint64_t)b, BUFS + 0x1000, 1000, ORBIS_NET_MSG_USESIGNATURE) == 1000);
    memset(at(BUFS + 0x2000), 0, 1000);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)a, BUFS + 0x2000, 2000, 0) == 1000);
    CHECK(memcmp(at(BUFS + 0x1000), at(BUFS + 0x2000), 1000) == 0);
    // The communication ID (ioctl 0xd8) adds a digest that receivers check; 0xd9 clears it.
    for (int i = 0; i < 16; i++) rec[i] = (uint8_t)i;
    CHECK(p2p_ioctl(a, 0x801050d8, VARS + 0x200) == 0);
    CHECK(CALL(shim_sceNetSend, (uint64_t)b, BUFS + 0x1000, 100, 0) == 100);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)a, BUFS + 0x2000, 2000, 0) == 100);
    CHECK(p2p_ioctl(a, 0x200050d9, 0) == 0);
    CHECK(set_int_opt(b, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_USECRYPTO, 0) == 0);

    // SockInfo reports the transport port and the vports.
    OrbisNetSockInfo *info = at(VARS + 0x400);
    CHECK(CALL(shim_sceNetGetSockInfo, (uint64_t)b, VARS + 0x400, 1, 0) == 1);
    CHECK(info->socket_type == ORBIS_NET_SOCK_DGRAM_P2P && info->local_port == htons(udp) &&
          info->local_vport == htons(1001) && info->remote_vport == htons(1000) &&
          info->state == ORBIS_NET_SOCKINFO_STATE_ESTABLISHED);

    // A blocked P2P receive is cut short by sceNetSocketAbort.
    Blocked bl = {1, a, 0, BUFS + 0x3000};
    pthread_t th;
    pthread_create(&th, NULL, blocked_recv, &bl);
    usleep(50000);
    CHECK(CALL(shim_sceNetSocketAbort, (uint64_t)a, 0) == 0);
    pthread_join(th, NULL);
    CHECK(bl.result == ERR(ORBIS_NET_EINTR));
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)a) == 0);
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)b) == 0);

    // ---- STREAM_P2P: TCP over the transport.
    const int32_t l = CALL(shim_sceNetSocket, str(STRINGS, "p2p-l"), ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM_P2P, 0);
    const int32_t c = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM_P2P, 0);
    CHECK(l >= 0 && c >= 0);
    CHECK(CALL(shim_sceNetListen, (uint64_t)l, 4) == ERR(ORBIS_NET_EINVAL)); // listeners must be bound
    CHECK(CALL(shim_sceNetBind, (uint64_t)l, p2p_addr_at(VARS, 0, 2000, 1234), 16) == ERR(ORBIS_NET_EINVAL));
    CHECK(CALL(shim_sceNetBind, (uint64_t)l, p2p_addr_at(VARS, 0, 80, 0), 16) == ERR(ORBIS_NET_EACCES));
    CHECK(CALL(shim_sceNetBind, (uint64_t)l, p2p_addr_at(VARS, 0, 2000, 0), 16) == 0);
    CHECK(CALL(shim_sceNetListen, (uint64_t)l, 4) == 0);
    CHECK(get_int_opt(c, ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_NODELAY, &v) == 0 && v == 1);
    CHECK(get_int_opt(c, ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_MAXSEG, &v) == 0 && v == 1200);
    // No address of the UDP port in sin_vport means the PS4's 3658; here the transport's.
    CHECK(CALL(shim_sceNetConnect, (uint64_t)c, p2p_addr_at(VARS, lo, 0, udp), 16) == ERR(ORBIS_NET_EADDRNOTAVAIL));
    CHECK(CALL(shim_sceNetConnect, (uint64_t)c, p2p_addr_at(VARS, lo, 2000, udp), 16) == 0); // blocking connect
    CHECK(CALL(shim_sceNetConnect, (uint64_t)c, p2p_addr_at(VARS, lo, 2000, udp), 16) == ERR(ORBIS_NET_EISCONN));
    *u32(VARS + 0x60) = 16;
    const int32_t s2 = CALL(shim_sceNetAccept, (uint64_t)l, VARS + 0x48, VARS + 0x60);
    CHECK(s2 >= 0 && *u32(VARS + 0x60) == 16 && vport_of(VARS + 0x48) == udp && port_of(VARS + 0x48) >= 49152);
    *u32(VARS + 0x40) = 16;
    CHECK(CALL(shim_sceNetGetpeername, (uint64_t)c, VARS + 0x20, VARS + 0x40) == 0 && port_of(VARS + 0x20) == 2000 &&
          vport_of(VARS + 0x20) == udp);
    CHECK(get_int_opt(c, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_ERROR, &v) == 0 && v == 0);
    // A bound listener port cannot connect out (EPROTO).
    const int32_t c2 = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM_P2P, 0);
    CHECK(CALL(shim_sceNetBind, (uint64_t)c2, p2p_addr_at(VARS, 0, 2001, 0), 16) == 0);
    CHECK(CALL(shim_sceNetConnect, (uint64_t)c2, p2p_addr_at(VARS, lo, 2000, udp), 16) == ERR(ORBIS_NET_EPROTO));
    CHECK(CALL(shim_sceNetSocketClose, (uint64_t)c2) == 0);

    // Bulk data both ways: flow control (32 KiB send buffer, 64 KiB window) with blocking sends.
    p2p_transfer(c, s2, 300000, 1);
    p2p_transfer(s2, c, 100000, 2);
    // Lossy: retransmission, fast retransmit and recovery.
    Ps4P2PTransport *transport = ps4_net_p2p_acquire_transport();
    CHECK(transport);
    ps4_p2p_transport_set_outgoing_loss(transport, 0.1);
    p2p_transfer(c, s2, 60000, 3);
    ps4_p2p_transport_set_outgoing_loss(transport, 0);
    ps4_p2p_transport_release(transport);

    // Epoll on a stream: readable with data, EPOLLOUT-only waits do not see incoming data.
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, s2, ORBIS_NET_EPOLLIN, 0x5) == 0);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_ADD, l, ORBIS_NET_EPOLLIN, 0x6) == 0);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 0);
    CHECK(CALL(shim_sceNetSend, (uint64_t)c, BUFS, 5, 0) == 5);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 1000000) == 1);
    CHECK(out[0].events == ORBIS_NET_EPOLLIN && out[0].ident == (uint64_t)s2 && out[0].data == 0x5);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_MOD, s2, ORBIS_NET_EPOLLOUT, 0x7) == 0);
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 0) == 1 && out[0].events == ORBIS_NET_EPOLLOUT);
    CHECK(epoll_ctl(eid, ORBIS_NET_EPOLL_CTL_DEL, s2, 0, 0) == 0);
    // The kernel's read/write reach P2P streams too.
    GuestContext *ctx = ctx_for(0);
    char buf[16];
    CHECK(ps4_net_kernel_read(ctx, s2, buf, sizeof buf) == 5);
    CHECK(ps4_net_kernel_write(ctx, s2, "pong", 4) == 4);
    CHECK(ps4_net_kernel_read(ctx, c, buf, sizeof buf) == 4 && memcmp(buf, "pong", 4) == 0);

    // A second connection shows up on the listener's epoll registration.
    const int32_t c3 = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM_P2P, 0);
    CHECK(set_int_opt(c3, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_NBIO, 1) == 0);
    CHECK(CALL(shim_sceNetConnect, (uint64_t)c3, p2p_addr_at(VARS, lo, 2000, udp), 16) == ERR(ORBIS_NET_EINPROGRESS));
    CHECK(CALL(shim_sceNetEpollWait, (uint64_t)eid, VARS + 0x300, 4, 1000000) == 1);
    CHECK(out[0].events == ORBIS_NET_EPOLLIN && out[0].ident == (uint64_t)l);
    const int32_t s3 = CALL(shim_sceNetAccept, (uint64_t)l, 0, 0);
    CHECK(s3 >= 0);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)s3, BUFS, 16, ORBIS_NET_MSG_DONTWAIT) == ERR(ORBIS_NET_EAGAIN));

    // Shutdown(WR): the peer reads EOF after the data.
    CHECK(CALL(shim_sceNetSend, (uint64_t)c, BUFS, 3, 0) == 3);
    CHECK(CALL(shim_sceNetShutdown, (uint64_t)c, ORBIS_NET_SHUT_WR) == 0);
    CHECK(CALL(shim_sceNetSend, (uint64_t)c, BUFS, 3, 0) == ERR(ORBIS_NET_EPIPE));
    CHECK(CALL(shim_sceNetRecv, (uint64_t)s2, BUFS, 16, ORBIS_NET_MSG_WAITALL) == 3);
    CHECK(CALL(shim_sceNetRecv, (uint64_t)s2, BUFS, 16, 0) == 0);

    // Nobody listening: the transport answers the SYN with a RST.
    const int32_t c4 = CALL(shim_sceNetSocket, 0, ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM_P2P, 0);
    CHECK(CALL(shim_sceNetConnect, (uint64_t)c4, p2p_addr_at(VARS, lo, 2999, udp), 16) == ERR(ORBIS_NET_ECONNREFUSED));

    CHECK(CALL(shim_sceNetEpollDestroy, (uint64_t)eid) == 0);
    for (int i = 0; i < 6; i++) {
        const int32_t ids[6] = {c, s2, c3, s3, c4, l};
        CHECK(CALL(shim_sceNetSocketClose, (uint64_t)ids[i]) == 0);
    }
    ps4_net_p2p_stop();
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(mem != MAP_FAILED);
    int rc = 2;
    if (strcmp(argv[1], "basics") == 0) rc = test_basics();
    else if (strcmp(argv[1], "tcp") == 0) rc = test_tcp();
    else if (strcmp(argv[1], "abort") == 0) rc = test_abort();
    else if (strcmp(argv[1], "epoll") == 0) rc = test_epoll();
    else if (strcmp(argv[1], "udp") == 0) rc = test_udp();
    else if (strcmp(argv[1], "resolver") == 0) rc = test_resolver();
    else if (strcmp(argv[1], "kernel") == 0) rc = test_kernel();
    else if (strcmp(argv[1], "thread") == 0) rc = test_thread();
    else if (strcmp(argv[1], "p2p") == 0) rc = test_p2p();
    if (rc == 0) printf("PASS %s\n", argv[1]);
    return rc;
}
