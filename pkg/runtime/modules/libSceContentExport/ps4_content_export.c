// libSceContentExport module implementation following shadPS4 reference.
#include "ps4_content_export.h"
#include <stdio.h>
#include <string.h>
#ifndef ORBIS_OK
#define ORBIS_OK 0
#endif

#define ORBIS_CONTENT_EXPORT_ERROR_CANCELED   0x809D3001
#define ORBIS_CONTENT_EXPORT_ERROR_NOTACCEPT  0x809D3002
#define ORBIS_CONTENT_EXPORT_ERROR_BUSY       0x809D3003
#define ORBIS_CONTENT_EXPORT_ERROR_NOINIT     0x809D3004
#define ORBIS_CONTENT_EXPORT_ERROR_MULTIPLEINIT 0x809D3005
#define ORBIS_CONTENT_EXPORT_ERROR_NOMEM      0x809D3006
#define ORBIS_CONTENT_EXPORT_ERROR_INVALDPARAM 0x809D3016

typedef struct OrbisContentExportInitParam {
  uint64_t mallocfunc;
  uint64_t freefunc;
  uint64_t userdata;
  uint64_t bufsize;
  int64_t reserved0;
  int64_t reserved1;
} OrbisContentExportInitParam;

static int g_content_export_initialized = 0;

static int32_t _sceContentExportInit(GuestContext *ctx, uint64_t param_addr, uint8_t version) {
  if (g_content_export_initialized) {
    return ORBIS_CONTENT_EXPORT_ERROR_MULTIPLEINIT;
  }
  if (!param_addr || !ctx || !ctx->mem_base) {
    return ORBIS_CONTENT_EXPORT_ERROR_INVALDPARAM;
  }
  OrbisContentExportInitParam *param =
      (OrbisContentExportInitParam *)(ctx->mem_base + param_addr);
  if (!param->mallocfunc || !param->freefunc) {
    return ORBIS_CONTENT_EXPORT_ERROR_INVALDPARAM;
  }
  if (version == 1 && (param->reserved0 != 0 || param->reserved1 != 0 ||
                       (param->bufsize != 0 && param->bufsize < 0x100))) {
    return ORBIS_CONTENT_EXPORT_ERROR_INVALDPARAM;
  }
  g_content_export_initialized = 1;
  return ORBIS_OK;
}

void shim_sceContentExportInit(GuestContext *ctx) {
  ctx->rax = (uint64_t)_sceContentExportInit(ctx, ctx->rdi, 0);
  SHIM_RETURN();
}

void shim_sceContentExportInit2(GuestContext *ctx) {
  ctx->rax = (uint64_t)_sceContentExportInit(ctx, ctx->rdi, 1);
  SHIM_RETURN();
}

void shim_sceContentExportTerm(GuestContext *ctx) {
  if (!g_content_export_initialized) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_CONTENT_EXPORT_ERROR_NOINIT;
    SHIM_RETURN();
  }
  g_content_export_initialized = 0;
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}
