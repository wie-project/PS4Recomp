// Subsystems recomp_runtime.c references that the boundary tests do not use.
#include "recomp_runtime.h"
#include "ps4_log.h"
#include "ps4_sysmodule.h"
#include "ps4_vfs.h"
#include "ps4_syscalls.h"

volatile uint64_t g_ps4_videoout_flip_count;

void ps4_aio_destroy(void) {}
void ps4_ajm_destroy(void) {}
void ps4_direct_mem_destroy(void) {}
void ps4_direct_mem_on_unmap(GuestContext *ctx, uint64_t addr, size_t size) {
  (void)ctx;
  (void)addr;
  (void)size;
}
void ps4_equeue_destroy(void) {}
void ps4_event_flag_destroy(void) {}
void ps4_gnmdriver_destroy(void) {}
void ps4_keyboard_destroy(void) {}
void ps4_log_drain_sync(double timeout_s) { (void)timeout_s; }
void ps4_metal_screen_destroy(void) {}
void ps4_sync_destroy(void) {}
void ps4_vfs_destroy(void) {}
const char *ps4_vfs_get_app_root(void) { return "."; }
void ps4_videoout_destroy(void) {}
int ps4_host_setting_pro_mode(void) { return 0; }
// ps4_sysmodule.c resolves PRX paths for sceKernelLoadStartModule, which the tests do not call.
int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
  (void)guest_path;
  (void)host_path;
  (void)host_path_sz;
  return -1;
}
