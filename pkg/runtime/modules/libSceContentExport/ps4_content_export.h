#ifndef PS4_CONTENT_EXPORT_H
#define PS4_CONTENT_EXPORT_H

#include "recomp_runtime.h"

void shim_sceContentExportInit(GuestContext *ctx);
void shim_sceContentExportInit2(GuestContext *ctx);
void shim_sceContentExportTerm(GuestContext *ctx);

#endif // PS4_CONTENT_EXPORT_H
