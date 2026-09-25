#include "recomp_runtime.h"
#include "ps4_vfs.h"

extern void recomp_init_dispatch_table(void);

int main(int argc, char **argv) {
    printf("[ps4-recomp] Initializing runtime...\n");
    ps4_vfs_init(NULL);
    recomp_init_dispatch_table();

    const char *prog_name = (argc > 0 && argv[0]) ? argv[0] : "ps4_app";
    GuestContext *ctx = recomp_init_runtime_file("guest_image.bin", 0, prog_name);
    if (!ctx) {
        fprintf(stderr, "[ps4-recomp] Failed to allocate guest memory or load guest_image.bin\n");
        return 1;
    }

    if (strcmp("_start", "_start") != 0) {
        printf("[ps4-recomp] Calling global constructors (.init_array)...\n");
        // Run .init_array
        uint64_t init_arr[] = {
        0
        };
        size_t init_count = sizeof(init_arr) / sizeof(init_arr[0]);
        for (size_t i = 0; i < init_count; i++) {
            if (init_arr[i] != 0) {
                printf("[ps4-recomp] Running init constructor at 0x%llx...\n", (unsigned long long)init_arr[i]);
                recomp_call_guest(ctx, init_arr[i]);
            }
        }
    }

    printf("[ps4-recomp] Executing _start (0x47a0)...\n");
    recomp_dispatch(ctx, 0x47a0ULL);

    printf("[ps4-recomp] Execution complete.\n");
    recomp_free_runtime(ctx);
    return 0;
}
