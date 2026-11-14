// Host test for the NpWebApi / NpInGameMessage handle and push-event callback lifecycles.
// Build and run (from the repository root):
//   clang -std=gnu11 -I pkg/runtime/core -I pkg/runtime/modules/libSceNp -Wl,-undefined,dynamic_lookup \
//     tests/np_handles_test/main.c pkg/runtime/modules/libSceNp/ps4_np_extras.c \
//     -o /tmp/np_handles_test && /tmp/np_handles_test

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "ps4_np.h"
#include "recomp_context.h"

// The tested shims run no guest code: an empty dispatch table makes any guest call abort.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

// Shims from ps4_np_extras.c (it has no header of its own).
void shim_sceNpWebApiCreateHandle(GuestContext *ctx);
void shim_sceNpWebApiDeleteHandle(GuestContext *ctx);
void shim_sceNpInGameMessageCreateHandle(GuestContext *ctx);
void shim_sceNpInGameMessageDeleteHandle(GuestContext *ctx);
void shim_sceNpWebApiRegisterServicePushEventCallback(GuestContext *ctx);
void shim_sceNpWebApiUnregisterServicePushEventCallback(GuestContext *ctx);

#define WEBAPI_HANDLE_NOT_FOUND ((int32_t)0x8055290D)
#define WEBAPI_OUT_OF_MEMORY ((int32_t)0x80552901)
#define WEBAPI_INVALID_ARGUMENT ((int32_t)0x80552902)
#define WEBAPI_CALLBACK_NOT_FOUND ((int32_t)0x8055290F)
#define IGM_HANDLE_NOT_FOUND ((int32_t)0x80552B05)
#define IGM_OUT_OF_MEMORY ((int32_t)0x80552B01)
#define CAPACITY 256

enum { kStack = 0x1000, kMem = 0x2000 };

static GuestContext g_ctx;

static int32_t call(void (*shim)(GuestContext *), uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    g_ctx.rsp = kStack - 8;
    g_ctx.rdi = a;
    g_ctx.rsi = b;
    g_ctx.rdx = c;
    g_ctx.rcx = d;
    shim(&g_ctx);
    assert(g_ctx.rsp == kStack);
    return (int32_t)g_ctx.rax;
}

// Create/delete pairs with one owner context id argument and one object id argument.
static void check_lifecycle(void (*create)(GuestContext *), void (*destroy)(GuestContext *),
                            int32_t not_found, int32_t out_of_memory, uint64_t create_cb) {
    const int32_t owner = 0x10001, other = 0x20001;
    int32_t a = call(create, owner, 7, create_cb, 0);
    int32_t b = call(create, owner, 7, create_cb, 0);
    assert(a > 0 && b > 0 && a != b);
    // The object belongs to the context it was created under.
    assert(call(destroy, other, a, 0, 0) == not_found);
    assert(call(destroy, owner, a, 0, 0) == 0);
    assert(call(destroy, owner, a, 0, 0) == not_found); // deleted once
    assert(call(destroy, owner, 0, 0, 0) == not_found);
    assert(call(destroy, owner, -1, 0, 0) == not_found);
    assert(call(destroy, owner, b, 0, 0) == 0);

    // Capacity: a full table fails with the library's OUT_OF_MEMORY; freeing makes room again,
    // and a new id is never one that is still live.
    int32_t ids[CAPACITY];
    for (int i = 0; i < CAPACITY; i++) {
        ids[i] = call(create, owner, 7, create_cb, 0);
        assert(ids[i] > 0);
        for (int j = 0; j < i; j++) assert(ids[j] != ids[i]);
    }
    assert(call(create, owner, 7, create_cb, 0) == out_of_memory);
    assert(call(destroy, owner, ids[10], 0, 0) == 0);
    int32_t again = call(create, owner, 7, create_cb, 0);
    assert(again > 0);
    for (int i = 0; i < CAPACITY; i++) assert(i == 10 || ids[i] != again);
    ids[10] = again;
    for (int i = 0; i < CAPACITY; i++) assert(call(destroy, owner, ids[i], 0, 0) == 0);
}

int main(void) {
    g_ctx.mem_size = kMem;
    g_ctx.mem_base = calloc(1, kMem);
    assert(g_ctx.mem_base);

    check_lifecycle(shim_sceNpWebApiCreateHandle, shim_sceNpWebApiDeleteHandle, WEBAPI_HANDLE_NOT_FOUND,
                    WEBAPI_OUT_OF_MEMORY, 0);
    check_lifecycle(shim_sceNpInGameMessageCreateHandle, shim_sceNpInGameMessageDeleteHandle,
                    IGM_HANDLE_NOT_FOUND, IGM_OUT_OF_MEMORY, 0);
    // Service push-event callbacks: (titleUserCtxId, filterId, cb, arg); a NULL callback is refused.
    assert(call(shim_sceNpWebApiRegisterServicePushEventCallback, 0x10001, 7, 0, 0) == WEBAPI_INVALID_ARGUMENT);
    check_lifecycle(shim_sceNpWebApiRegisterServicePushEventCallback,
                    shim_sceNpWebApiUnregisterServicePushEventCallback, WEBAPI_CALLBACK_NOT_FOUND,
                    WEBAPI_OUT_OF_MEMORY, 0x4000);

    free(g_ctx.mem_base);
    printf("[np-handles-test] PASSED\n");
    return 0;
}
