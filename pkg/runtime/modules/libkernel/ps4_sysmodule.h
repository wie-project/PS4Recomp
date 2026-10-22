#ifndef PS4_SYSMODULE_H
#define PS4_SYSMODULE_H

#include "recomp_runtime.h"
#include <stddef.h>
#include <stdint.h>

#define ORBIS_SYSMODULE_FREETYPE_OL 0x009A
#define ORBIS_SYSMODULE_FREETYPE_OT 0x0099
#define ORBIS_SYSMODULE_FONT_FT 0x0098

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceSysmoduleLoadModule(uint16_t moduleId);
int32_t sceSysmoduleIsLoaded(uint16_t moduleId);
int32_t sceSysmoduleUnloadModule(uint16_t moduleId);
uint32_t sceSysmoduleLoadModuleInternal(uint32_t moduleId);
int32_t sceSysmoduleUnloadModuleInternal(uint32_t moduleId);

// Module Loading & Dynamic Symbol Resolution
typedef struct RecompModuleExport {
  const char *name;
  uint64_t addr;
} RecompModuleExport;

typedef struct RecompModuleSegment {
  uint64_t addr;
  uint64_t size;
  int32_t prot;
} RecompModuleSegment;

typedef struct RecompModuleUnwindInfo {
  const char *name;
  uint64_t start_addr;
  uint64_t end_addr;
  uint64_t seg0_addr;
  uint64_t seg0_size;
  uint64_t eh_frame_hdr_addr;
  uint64_t eh_frame_hdr_size;
  uint64_t eh_frame_addr;
  uint64_t eh_frame_size;
  uint32_t num_segments;
  RecompModuleSegment segments[4];
} RecompModuleUnwindInfo;

void recomp_module_register(const char *filename,
                            const RecompModuleExport *exports);
void recomp_module_register_init(const char *filename, const uint64_t *inits,
                                 size_t count);
void recomp_module_register_unwind_info(const RecompModuleUnwindInfo *info);
const RecompModuleUnwindInfo *recomp_module_find_by_addr(uint64_t addr);
// True when addr is in an executable segment of a registered guest module. Takes no lock
// (modules register before guest code runs), for crash reports and other signal context.
bool recomp_module_text_contains(uint64_t addr);
void recomp_register_guest_modules(void);
void recomp_module_start(GuestContext *ctx, int32_t handle);
uint64_t recomp_resolve_symbol(const char *module_name, const char *symbol);

// Module enumeration for sceKernelGetModuleList / sceKernelGetModuleInfo.
// Entry 0 is always the main executable (handle 0); registered PRXs follow
// (a handle is assigned on first enumeration, same one LoadStartModule returns).
typedef struct RecompModuleSnapshot {
  int32_t handle;
  char name[256];
  int has_range;
  uint64_t start_addr;
  uint64_t end_addr;
  uint64_t seg0_addr;
  uint64_t seg0_size;
  uint64_t eh_frame_hdr_addr;
  uint64_t eh_frame_hdr_size;
  uint64_t eh_frame_addr;
  uint64_t eh_frame_size;
  // The module's own segments as the emitter registered them (ELF program header flags in
  // prot: PF_X 1, PF_W 2, PF_R 4, as the console reports them in SceKernelModuleInfo).
  uint32_t num_segments;
  RecompModuleSegment segments[4];
} RecompModuleSnapshot;

int recomp_module_snapshot(RecompModuleSnapshot *out, int max);
// The one handle scheme of sceKernelGetModuleList, sceKernelGetModuleInfo, GetModuleInfo2 and
// GetModuleInfoInternal: the main executable is handle 0 (handle 1 also names it unless a PRX
// owns that handle); a PRX has the handle sceKernelLoadStartModule returned. 0 on success.
int recomp_module_info_by_handle(int32_t handle, RecompModuleSnapshot *out);
// The module whose address range contains addr (the innermost one), with the handle of the
// scheme above. 0 on success, -1 if no module contains addr.
int recomp_module_info_by_addr(uint64_t addr, RecompModuleSnapshot *out);
// Fills out[4] with the segments of m (registered ones, else text + data from its range);
// returns their number.
uint32_t recomp_module_snapshot_segments(const RecompModuleSnapshot *m, RecompModuleSegment out[4]);

int32_t sceKernelLoadStartModule(const char *name, size_t argc,
                                 const void *argv, uint32_t flags, void *opt,
                                 int *res);
int32_t sceKernelDlsym(int32_t handle, const char *symbol, void **addr);

int32_t sceSysmoduleLoadModuleInternalWithArg(uint32_t moduleId, int32_t argc, const void *argv, uint64_t unk, int32_t *res_out);

// Guest ABI shims
void shim_sceSysmoduleLoadModule(GuestContext *ctx);
void shim_sceSysmoduleIsLoaded(GuestContext *ctx);
void shim_sceSysmoduleUnloadModule(GuestContext *ctx);
void shim_sceSysmoduleLoadModuleInternal(GuestContext *ctx);
void shim_sceSysmoduleUnloadModuleInternal(GuestContext *ctx);
void shim_sceSysmoduleLoadModuleInternalWithArg(GuestContext *ctx);
void shim_sceKernelLoadStartModule(GuestContext *ctx);
void shim_sceKernelDlsym(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYSMODULE_H
