#ifndef PS4_SYSMODULE_H
#define PS4_SYSMODULE_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#define ORBIS_SYSMODULE_FREETYPE_OL 0x009A
#define ORBIS_SYSMODULE_FREETYPE_OT 0x0099
#define ORBIS_SYSMODULE_FONT_FT     0x0098

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceSysmoduleLoadModule(uint16_t moduleId);
int32_t sceSysmoduleIsLoaded(uint16_t moduleId);
int32_t sceSysmoduleUnloadModule(uint16_t moduleId);
uint32_t sceSysmoduleLoadModuleInternal(uint32_t moduleId);
int32_t sceSysmoduleUnloadModuleInternal(uint32_t moduleId);

// Module Loading & Dynamic Symbol Resolution
int32_t sceKernelLoadStartModule(const char *name, size_t argc, const void *argv, uint32_t flags, void *opt, int *res);
int32_t sceKernelDlsym(int32_t handle, const char *symbol, void **addr);

// Guest ABI shims
void shim_sceSysmoduleLoadModule(GuestContext *ctx);
void shim_sceSysmoduleIsLoaded(GuestContext *ctx);
void shim_sceSysmoduleUnloadModule(GuestContext *ctx);
void shim_sceSysmoduleLoadModuleInternal(GuestContext *ctx);
void shim_sceSysmoduleUnloadModuleInternal(GuestContext *ctx);
void shim_sceKernelLoadStartModule(GuestContext *ctx);
void shim_sceKernelDlsym(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYSMODULE_H
