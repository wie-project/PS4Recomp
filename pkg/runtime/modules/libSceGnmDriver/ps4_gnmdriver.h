// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4 libSceGnmDriver HLE implementation.
 * Provides AMD GCN (Liverpool) PM4 command buffer initialization, hardware state sequences,
 * ring buffer allocation, and GNM runtime driver routines.
 */

#ifndef PS4_GNMDRIVER_H
#define PS4_GNMDRIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
#define ORBIS_GNM_OK                                  0
#define ORBIS_GNM_ERROR_FAILURE                       ((int32_t)0x80d10000)
#define ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED        ((int32_t)0x80d10001)
#define ORBIS_GNM_ERROR_INVALID_ARGS                  ((int32_t)0x80d11000)
#define ORBIS_GNM_ERROR_INVALID_VALUE                 ((int32_t)0x80d11000)
#define ORBIS_GNM_ERROR_CAPTURE_RAZOR_NOT_LOADED      ((int32_t)0x80d12000)

// PM4 Opcodes (AMD GCN Type-3 Packets)
#define PM4_IT_NOP                        0x10
#define PM4_IT_SET_BASE                   0x11
#define PM4_IT_CLEAR_STATE                0x12
#define PM4_IT_INDEX_BUFFER_SIZE          0x13
#define PM4_IT_DISPATCH_DIRECT            0x15
#define PM4_IT_DISPATCH_INDIRECT          0x16
#define PM4_IT_DRAW_INDIRECT              0x24
#define PM4_IT_DRAW_INDEX_INDIRECT        0x25
#define PM4_IT_INDEX_BASE                 0x26
#define PM4_IT_DRAW_INDEX_2               0x27
#define PM4_IT_CONTEXT_CONTROL            0x28
#define PM4_IT_INDEX_TYPE                 0x2A
#define PM4_IT_DRAW_INDIRECT_MULTI        0x2C
#define PM4_IT_DRAW_INDEX_AUTO            0x2D
#define PM4_IT_NUM_INSTANCES              0x2F
#define PM4_IT_DRAW_INDEX_OFFSET_2        0x35
#define PM4_IT_DRAW_INDEX_INDIRECT_MULTI  0x38
#define PM4_IT_WAIT_REG_MEM               0x3C
#define PM4_IT_EVENT_WRITE                0x46
#define PM4_IT_EVENT_WRITE_EOP            0x47
#define PM4_IT_SET_CONFIG_REG             0x68
#define PM4_IT_SET_CONTEXT_REG            0x69
#define PM4_IT_SET_SH_REG                 0x76
#define PM4_IT_SET_UCONFIG_REG            0x79

#define PM4_TYPE3_HEADER(opcode, count, shader_type, pred) \
    ((uint32_t)(0xc0000000u | (((uint32_t)(count) & 0x3fffu) << 16) | (((uint32_t)(opcode) & 0xffu) << 8) | (((uint32_t)(shader_type) & 1u) << 1) | ((uint32_t)(pred) & 1u)))

#define PM4_HW_INIT_PACKET_SIZE 0x100u
#define PM4_CTX_INIT_PACKET_SIZE_400 0x100u

// Base address and sizing constants
#define GNM_TESSELLATION_RING_SIZE 0x800000u // 8 MB

// Core lifecycle & initialization
void ps4_gnmdriver_init(GuestContext *ctx);
void ps4_gnmdriver_destroy(void);

// Ring buffer and capability queries
uint64_t sceGnmGetTheTessellationFactorRingBufferBaseAddress(void);
int32_t sceGnmGetOffChipTessellationBufferSize(void);
uint32_t sceGnmGetGpuCoreClockFrequency(void);
int32_t sceGnmAreSubmitsAllowed(void);
int32_t sceGnmGetNumTcaUnits(void);
int32_t sceGnmLogicalTcaUnitToPhysical(uint32_t tca);
int32_t sceGnmLogicalCuIndexToPhysicalCuIndex(uint32_t cu);
int32_t sceGnmLogicalCuMaskToPhysicalCuMask(int64_t unk, int32_t logical_cu_mask);
void sceGnmFlushGarlic(void);
int32_t sceGnmGetShaderStatus(void);
uint32_t sceGnmGetProtectionFaultTimeStamp(void);
int32_t sceGnmGetEqTimeStamp(void);
int32_t sceGnmGetGpuBlockStatus(void);
int32_t sceGnmGetGpuInfoStatus(void);
int32_t sceGnmGetPhysicalCounterFromVirtualized(void);

// Hardware state initialization
uint32_t sceGnmDrawInitDefaultHardwareState(uint32_t *cmdbuf, uint32_t size);
uint32_t sceGnmDrawInitDefaultHardwareState175(uint32_t *cmdbuf, uint32_t size);
uint32_t sceGnmDrawInitDefaultHardwareState200(uint32_t *cmdbuf, uint32_t size);
uint32_t sceGnmDrawInitDefaultHardwareState350(uint32_t *cmdbuf, uint32_t size);
uint32_t sceGnmDrawInitToDefaultContextState(uint32_t *cmdbuf, uint32_t size);
uint32_t sceGnmDrawInitToDefaultContextState400(uint32_t *cmdbuf, uint32_t size);
uint32_t sceGnmDispatchInitDefaultHardwareState(uint32_t *cmdbuf, uint32_t size);
int32_t sceGnmResetVgtControl(uint32_t *cmdbuf, uint32_t size);
int32_t sceGnmSetVgtControl(uint32_t *cmdbuf, uint32_t size, uint32_t vgt_control);
int32_t sceGnmSetGsRingSizes(uint32_t *cmdbuf, uint32_t size, uint32_t ring_sizes);
int32_t sceGnmSetWaveLimitMultipliers(uint32_t *cmdbuf, uint32_t size, uint32_t mult);

// Draw and Dispatch commands
int32_t sceGnmDrawIndex(uint32_t *cmdbuf, uint32_t size, uint32_t index_count, uint64_t index_addr, uint32_t flags, uint32_t type);
int32_t sceGnmDrawIndexAuto(uint32_t *cmdbuf, uint32_t size, uint32_t index_count, uint32_t flags);
int32_t sceGnmDrawIndexOffset(uint32_t *cmdbuf, uint32_t size, uint32_t index_offset, uint32_t index_count, uint32_t flags);
int32_t sceGnmDrawIndexIndirect(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t shader_stage, uint32_t vertex_sgpr_offset, uint32_t instance_sgpr_offset, uint32_t flags);
int32_t sceGnmDrawIndirect(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t shader_stage, uint32_t vertex_sgpr_offset, uint32_t instance_sgpr_offset, uint32_t flags);
int32_t sceGnmDrawIndexIndirectCountMulti(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t max_count, uint64_t count_addr, uint32_t shader_stage, uint32_t vertex_sgpr_offset, uint32_t instance_sgpr_offset, uint32_t flags);
int32_t sceGnmDrawIndirectCountMulti(void);
int32_t sceGnmDrawIndexIndirectMulti(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t max_count, uint32_t shader_stage, uint32_t vertex_sgpr_offset, uint32_t instance_sgpr_offset, uint32_t flags);
int32_t sceGnmDrawIndexMultiInstanced(void);
int32_t sceGnmDrawOpaqueAuto(void);

int32_t sceGnmDispatchDirect(uint32_t *cmdbuf, uint32_t size, uint32_t threads_x, uint32_t threads_y, uint32_t threads_z, uint32_t flags);
int32_t sceGnmDispatchIndirect(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t flags);
int32_t sceGnmDispatchIndirectOnMec(uint32_t *cmdbuf, uint32_t size, uint64_t args, uint32_t modifier);

// Shader binding commands
int32_t sceGnmSetVsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *vs_regs, uint32_t shader_modifier);
int32_t sceGnmUpdateVsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *vs_regs, uint32_t shader_modifier);
int32_t sceGnmSetEmbeddedVsShader(uint32_t *cmdbuf, uint32_t size, uint32_t shader_id, uint32_t shader_modifier);
int32_t sceGnmSetPsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *ps_regs);
int32_t sceGnmSetPsShader350(uint32_t *cmdbuf, uint32_t size, const uint32_t *ps_regs);
int32_t sceGnmUpdatePsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *ps_regs);
int32_t sceGnmUpdatePsShader350(uint32_t *cmdbuf, uint32_t size, const uint32_t *ps_regs);
int32_t sceGnmSetEmbeddedPsShader(uint32_t *cmdbuf, uint32_t size, uint32_t shader_id, uint32_t shader_modifier);
int32_t sceGnmSetGsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *gs_regs);
int32_t sceGnmUpdateGsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *gs_regs);
int32_t sceGnmSetHsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *hs_regs, uint32_t param4);
int32_t sceGnmUpdateHsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *hs_regs, uint32_t ls_hs_config);
int32_t sceGnmSetEsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *es_regs, uint32_t shader_modifier);
int32_t sceGnmSetLsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *ls_regs, uint32_t shader_modifier);
int32_t sceGnmSetCsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *cs_regs);
int32_t sceGnmSetCsShaderWithModifier(uint32_t *cmdbuf, uint32_t size, const uint32_t *cs_regs, uint32_t modifier);
int32_t sceGnmGetShaderProgramBaseAddress(void);

// Submission & Flip
int32_t sceGnmSubmitCommandBuffers(GuestContext *ctx, uint32_t count, const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes, const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes);
int32_t sceGnmSubmitCommandBuffersForWorkload(GuestContext *ctx, uint32_t workload, uint32_t count, const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes, const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes);
int32_t sceGnmSubmitAndFlipCommandBuffers(GuestContext *ctx, uint32_t count, const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes, const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes, uint32_t vo_handle, uint32_t buf_idx, uint32_t flip_mode, int64_t flip_arg);
int32_t sceGnmSubmitAndFlipCommandBuffersForWorkload(GuestContext *ctx, uint32_t workload, uint32_t count, const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes, const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes, uint32_t vo_handle, uint32_t buf_idx, uint32_t flip_mode, int64_t flip_arg);
int32_t sceGnmSubmitDone(void);
int32_t sceGnmRequestFlipAndSubmitDone(void);
int32_t sceGnmRequestFlipAndSubmitDoneForWorkload(void);

// Markers
int32_t sceGnmInsertDingDongMarker(uint32_t *cmdbuf, uint32_t size);
int32_t sceGnmInsertPopMarker(uint32_t *cmdbuf, uint32_t size);
int32_t sceGnmInsertPushMarker(uint32_t *cmdbuf, uint32_t size, const char *marker);
int32_t sceGnmInsertPushColorMarker(uint32_t *cmdbuf, uint32_t size, const char *marker, uint32_t color);
int32_t sceGnmInsertSetMarker(uint32_t *cmdbuf, uint32_t size, const char *marker);
int32_t sceGnmInsertSetColorMarker(void);
int32_t sceGnmInsertThreadTraceMarker(void);
int32_t sceGnmInsertWaitFlipDone(uint32_t *cmdbuf, uint32_t size, int32_t vo_handle, uint32_t buf_idx);

// Resource registration & ownership
int32_t sceGnmRegisterOwner(void *handle, const char *name);
int32_t sceGnmRegisterResource(void *res_handle, void *owner_handle, const void *addr, size_t size, const char *name, int res_type, uint64_t user_data);
int32_t sceGnmUnregisterResource(void *res_handle);
int32_t sceGnmUnregisterOwnerAndResources(void *owner_handle);
int32_t sceGnmUnregisterAllResourcesForOwner(void *owner_handle);
int32_t sceGnmSetResourceUserData(void *res_handle, uint64_t user_data);
int32_t sceGnmGetResourceUserData(void *res_handle, uint64_t *user_data);
int32_t sceGnmGetResourceBaseAddressAndSizeInBytes(void *res_handle, uint64_t *base_addr, uint64_t *size);
int32_t sceGnmGetResourceName(void *res_handle, char *name, size_t max_len);
int32_t sceGnmGetResourceType(void *res_handle, int *res_type);
int32_t sceGnmGetOwnerName(void *owner_handle, char *name, size_t max_len);
int32_t sceGnmGetResourceShaderGuid(void *res_handle, uint64_t *guid);
int32_t sceGnmFindResourcesPublic(void);
int32_t sceGnmQueryResourceRegistrationUserMemoryRequirements(size_t *required_size);
int32_t sceGnmSetResourceRegistrationUserMemory(void *addr, size_t size);
int32_t sceGnmRegisterGdsResource(void);

// Workloads & DingDong
int32_t sceGnmBeginWorkload(uint32_t workload_stream, uint64_t *workload);
int32_t sceGnmEndWorkload(uint64_t workload);
int32_t sceGnmCreateWorkloadStream(uint64_t param1, uint32_t *workload_stream);
int32_t sceGnmDestroyWorkloadStream(void);
void sceGnmDingDong(uint32_t gnm_vqid, uint32_t next_offs_dw);
void sceGnmDingDongForWorkload(uint32_t gnm_vqid, uint32_t next_offs_dw, uint64_t workload_id);

// Compute queue & event management
int32_t sceGnmMapComputeQueue(uint32_t pipe_id, uint32_t queue_id, uint64_t ring_base_addr, uint32_t ring_size_dw, uint32_t *read_ptr_addr);
int32_t sceGnmMapComputeQueueWithPriority(uint32_t pipe_id, uint32_t queue_id, uint64_t ring_base_addr, uint32_t ring_size_dw, uint32_t *read_ptr_addr, uint32_t pipePriority);
int32_t sceGnmUnmapComputeQueue(uint32_t pipe_id, uint32_t queue_id);
int32_t sceGnmComputeWaitSemaphore(void);
int32_t sceGnmComputeWaitOnAddress(uint32_t *cmdbuf, uint32_t size, uint64_t addr, uint32_t mask, uint32_t cmp_func, uint32_t ref);
int32_t sceGnmGetLastWaitedAddress(void);
int32_t sceGnmAddEqEvent(uint64_t eq, uint64_t id, void *udata);
int32_t sceGnmDeleteEqEvent(uint64_t eq, uint64_t id);
int32_t sceGnmGetEqEventType(const void *ev);

// Diagnostics & Validation (Retail behavior)
int32_t sceGnmValidateGetVersion(void);
bool sceGnmValidateOnSubmitEnabled(void);
int32_t sceGnmValidateGetDiagnosticInfo(void);
int32_t sceGnmValidateGetDiagnostics(void);
int32_t sceGnmValidateResetState(void);
int32_t sceGnmValidateDisableDiagnostics(void);
int32_t sceGnmValidateDisableDiagnostics2(void);
int32_t sceGnmValidateDrawCommandBuffers(void);
int32_t sceGnmValidateDispatchCommandBuffers(void);
int32_t sceGnmValidationRegisterMemoryCheckCallback(void);
int32_t sceRazorIsLoaded(void);
int32_t sceRazorCaptureImmediate(void);
int32_t sceRazorCpuJobManagerSequence(void);
int32_t sceRazorCpuJobManagerJob(void);
int32_t sceRazorCpuJobManagerDispatch(void);
bool sceGnmDriverTraceInProgress(void);
bool sceGnmDriverCaptureInProgress(void);
int32_t sceGnmDriverTriggerCapture(void);
bool sceGnmIsUserPaEnabled(void);
int32_t sceGnmDebugHardwareStatus(void);
int32_t sceGnmSetupMipStatsReport(void);
int32_t sceGnmDisableMipStatsReport(void);
int32_t sceGnmRequestMipStatsReportAndReset(void);

// Shims
void shim_sceGnmGetTheTessellationFactorRingBufferBaseAddress(GuestContext *ctx);
void shim_sceGnmGetOffChipTessellationBufferSize(GuestContext *ctx);
void shim_sceGnmGetGpuCoreClockFrequency(GuestContext *ctx);
void shim_sceGnmAreSubmitsAllowed(GuestContext *ctx);
void shim_sceGnmGetNumTcaUnits(GuestContext *ctx);
void shim_sceGnmLogicalTcaUnitToPhysical(GuestContext *ctx);
void shim_sceGnmLogicalCuIndexToPhysicalCuIndex(GuestContext *ctx);
void shim_sceGnmLogicalCuMaskToPhysicalCuMask(GuestContext *ctx);
void shim_sceGnmFlushGarlic(GuestContext *ctx);
void shim_sceGnmGetShaderStatus(GuestContext *ctx);
void shim_sceGnmGetProtectionFaultTimeStamp(GuestContext *ctx);
void shim_sceGnmGetEqTimeStamp(GuestContext *ctx);
void shim_sceGnmGetGpuBlockStatus(GuestContext *ctx);
void shim_sceGnmGetGpuInfoStatus(GuestContext *ctx);
void shim_sceGnmGetPhysicalCounterFromVirtualized(GuestContext *ctx);

void shim_sceGnmDrawInitDefaultHardwareState(GuestContext *ctx);
void shim_sceGnmDrawInitDefaultHardwareState175(GuestContext *ctx);
void shim_sceGnmDrawInitDefaultHardwareState200(GuestContext *ctx);
void shim_sceGnmDrawInitDefaultHardwareState350(GuestContext *ctx);
void shim_sceGnmDrawInitToDefaultContextState(GuestContext *ctx);
void shim_sceGnmDrawInitToDefaultContextState400(GuestContext *ctx);
void shim_sceGnmDispatchInitDefaultHardwareState(GuestContext *ctx);
void shim_sceGnmResetVgtControl(GuestContext *ctx);
void shim_sceGnmSetVgtControl(GuestContext *ctx);
void shim_sceGnmSetGsRingSizes(GuestContext *ctx);
void shim_sceGnmSetWaveLimitMultipliers(GuestContext *ctx);

void shim_sceGnmInsertDingDongMarker(GuestContext *ctx);
void shim_sceGnmInsertPopMarker(GuestContext *ctx);
void shim_sceGnmInsertPushMarker(GuestContext *ctx);
void shim_sceGnmInsertPushColorMarker(GuestContext *ctx);
void shim_sceGnmInsertSetMarker(GuestContext *ctx);
void shim_sceGnmInsertSetColorMarker(GuestContext *ctx);
void shim_sceGnmInsertThreadTraceMarker(GuestContext *ctx);
void shim_sceGnmInsertWaitFlipDone(GuestContext *ctx);

void shim_sceGnmRegisterOwner(GuestContext *ctx);
void shim_sceGnmRegisterResource(GuestContext *ctx);
void shim_sceGnmUnregisterResource(GuestContext *ctx);
void shim_sceGnmUnregisterOwnerAndResources(GuestContext *ctx);
void shim_sceGnmUnregisterAllResourcesForOwner(GuestContext *ctx);
void shim_sceGnmSetResourceUserData(GuestContext *ctx);
void shim_sceGnmGetResourceUserData(GuestContext *ctx);
void shim_sceGnmGetResourceBaseAddressAndSizeInBytes(GuestContext *ctx);
void shim_sceGnmGetResourceName(GuestContext *ctx);
void shim_sceGnmGetResourceType(GuestContext *ctx);
void shim_sceGnmGetOwnerName(GuestContext *ctx);
void shim_sceGnmGetResourceShaderGuid(GuestContext *ctx);
void shim_sceGnmFindResourcesPublic(GuestContext *ctx);
void shim_sceGnmQueryResourceRegistrationUserMemoryRequirements(GuestContext *ctx);
void shim_sceGnmSetResourceRegistrationUserMemory(GuestContext *ctx);
void shim_sceGnmRegisterGdsResource(GuestContext *ctx);

void shim_sceGnmBeginWorkload(GuestContext *ctx);
void shim_sceGnmEndWorkload(GuestContext *ctx);
void shim_sceGnmCreateWorkloadStream(GuestContext *ctx);
void shim_sceGnmDestroyWorkloadStream(GuestContext *ctx);
void shim_sceGnmDingDong(GuestContext *ctx);
void shim_sceGnmDingDongForWorkload(GuestContext *ctx);

void shim_sceGnmMapComputeQueue(GuestContext *ctx);
void shim_sceGnmMapComputeQueueWithPriority(GuestContext *ctx);
void shim_sceGnmUnmapComputeQueue(GuestContext *ctx);
void shim_sceGnmComputeWaitSemaphore(GuestContext *ctx);
void shim_sceGnmComputeWaitOnAddress(GuestContext *ctx);
void shim_sceGnmGetLastWaitedAddress(GuestContext *ctx);
void shim_sceGnmAddEqEvent(GuestContext *ctx);
void shim_sceGnmDeleteEqEvent(GuestContext *ctx);
void shim_sceGnmGetEqEventType(GuestContext *ctx);

void shim_sceGnmValidateGetVersion(GuestContext *ctx);
void shim_sceGnmValidateOnSubmitEnabled(GuestContext *ctx);
void shim_sceGnmValidateGetDiagnosticInfo(GuestContext *ctx);
void shim_sceGnmValidateGetDiagnostics(GuestContext *ctx);
void shim_sceGnmValidateResetState(GuestContext *ctx);
void shim_sceGnmValidateDisableDiagnostics(GuestContext *ctx);
void shim_sceGnmValidateDisableDiagnostics2(GuestContext *ctx);
void shim_sceGnmValidateDrawCommandBuffers(GuestContext *ctx);
void shim_sceGnmValidateDispatchCommandBuffers(GuestContext *ctx);
void shim_sceGnmValidationRegisterMemoryCheckCallback(GuestContext *ctx);
void shim_sceRazorIsLoaded(GuestContext *ctx);
void shim_sceRazorCaptureImmediate(GuestContext *ctx);
void shim_sceRazorCpuJobManagerSequence(GuestContext *ctx);
void shim_sceRazorCpuJobManagerJob(GuestContext *ctx);
void shim_sceRazorCpuJobManagerDispatch(GuestContext *ctx);
void shim_sceGnmDriverTraceInProgress(GuestContext *ctx);
void shim_sceGnmDriverCaptureInProgress(GuestContext *ctx);
void shim_sceGnmDriverTriggerCapture(GuestContext *ctx);
void shim_sceGnmIsUserPaEnabled(GuestContext *ctx);
void shim_sceGnmDebugHardwareStatus(GuestContext *ctx);
void shim_sceGnmSetupMipStatsReport(GuestContext *ctx);
void shim_sceGnmDisableMipStatsReport(GuestContext *ctx);
void shim_sceGnmRequestMipStatsReportAndReset(GuestContext *ctx);

void shim_sceGnmDrawIndex(GuestContext *ctx);
void shim_sceGnmDrawIndexAuto(GuestContext *ctx);
void shim_sceGnmDrawIndexOffset(GuestContext *ctx);
void shim_sceGnmDrawIndexIndirect(GuestContext *ctx);
void shim_sceGnmDrawIndirect(GuestContext *ctx);
void shim_sceGnmDrawIndexIndirectCountMulti(GuestContext *ctx);
void shim_sceGnmDrawIndirectCountMulti(GuestContext *ctx);
void shim_sceGnmDrawIndexIndirectMulti(GuestContext *ctx);
void shim_sceGnmDrawIndexMultiInstanced(GuestContext *ctx);
void shim_sceGnmDrawOpaqueAuto(GuestContext *ctx);

void shim_sceGnmDispatchDirect(GuestContext *ctx);
void shim_sceGnmDispatchIndirect(GuestContext *ctx);
void shim_sceGnmDispatchIndirectOnMec(GuestContext *ctx);

void shim_sceGnmSetVsShader(GuestContext *ctx);
void shim_sceGnmUpdateVsShader(GuestContext *ctx);
void shim_sceGnmSetEmbeddedVsShader(GuestContext *ctx);
void shim_sceGnmSetPsShader(GuestContext *ctx);
void shim_sceGnmSetPsShader350(GuestContext *ctx);
void shim_sceGnmUpdatePsShader(GuestContext *ctx);
void shim_sceGnmUpdatePsShader350(GuestContext *ctx);
void shim_sceGnmSetEmbeddedPsShader(GuestContext *ctx);
void shim_sceGnmSetGsShader(GuestContext *ctx);
void shim_sceGnmUpdateGsShader(GuestContext *ctx);
void shim_sceGnmSetHsShader(GuestContext *ctx);
void shim_sceGnmUpdateHsShader(GuestContext *ctx);
void shim_sceGnmSetEsShader(GuestContext *ctx);
void shim_sceGnmSetLsShader(GuestContext *ctx);
void shim_sceGnmSetCsShader(GuestContext *ctx);
void shim_sceGnmSetCsShaderWithModifier(GuestContext *ctx);
void shim_sceGnmGetShaderProgramBaseAddress(GuestContext *ctx);
void shim_sceGnmSubmitCommandBuffers(GuestContext *ctx);
void shim_sceGnmSubmitCommandBuffersForWorkload(GuestContext *ctx);
void shim_sceGnmSubmitAndFlipCommandBuffers(GuestContext *ctx);
void shim_sceGnmSubmitAndFlipCommandBuffersForWorkload(GuestContext *ctx);
void shim_sceGnmSubmitDone(GuestContext *ctx);
void shim_sceGnmRequestFlipAndSubmitDone(GuestContext *ctx);
void shim_sceGnmRequestFlipAndSubmitDoneForWorkload(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_GNMDRIVER_H
