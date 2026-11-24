// Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Copyright (c) 2008-2026 NVIDIA Corporation. All rights reserved.

// ---------------------------------------------------------------------------
// PhysX GPU HIP/ROCm compatibility shim (cuda_to_hip.h)
//
// Included (via a forced -include on the HIP build) by every PhysX GPU
// translation unit -- both host .cpp seam files (CudaContextManager,
// PxgKernelWrangler) and device .cu/.cuh kernel sources. It is a pure no-op
// unless __HIP_PLATFORM_AMD__ is defined, so the CUDA (nvcc) build is
// byte-for-byte unchanged. On HIP it:
//   1. Pulls in the HIP runtime + HIP driver-API headers.
//   2. Aliases the CUDA driver-API types PhysX uses (CUdeviceptr, CUmodule,
//      CUfunction, CUresult, CUstream, CUevent, CUcontext, CUdevice, ...) to
//      their hip* peers, and the CUDA_ERROR_* / CU_* enum values PhysX tests.
//   3. WIDTH-PINS the warp collectives to the 32-lane logical warp. PhysX's
//      GPU stack is compile-time 32-lane-hardwired (WARP_SIZE=32, per-warp
//      __shared__ scratch sized to 32 lanes, RadixSort bucket geometry, ...).
//      On CDNA (wave64) each physical 64-lane wave is treated as two
//      independent 32-lane logical warps: every __shfl_*_sync is forced to
//      width=32 so a shuffle stays inside its 32-lane subgroup, and the
//      sync mask is widened to the wave-portable 64-bit ~0ull.
// ---------------------------------------------------------------------------

#ifndef PXG_CUDA_TO_HIP_H
#define PXG_CUDA_TO_HIP_H

// Trigger point: __HIPCC__ is a compiler builtin under -x hip (unlike
// __HIP_PLATFORM_AMD__, which is only defined by hip_runtime.h itself -- so
// guarding on it here would create a chicken-and-egg where our hip_runtime.h
// include is skipped and threadIdx/blockIdx stay undeclared).
#if defined(__HIPCC__) || defined(__HIP_PLATFORM_AMD__)

// PhysX ships its own float2/3/4, int2/3/4 arithmetic operator overloads in
// cutil_math.h (67 of them, "since these are not provided as standard by
// CUDA"). HIP's HIP_vector_type provides its OWN member operator+=/+/*/...,
// so both a member and a non-member overload become viable for the same type
// and every vec += vec is "ambiguous". Disabling HIP's NATIVE vector member
// operators (__HIP_USE_NATIVE_VECTOR__ 0) keeps float4 a plain aggregate and
// lets PhysX's own operators be the single set -- matching the CUDA build's
// behaviour exactly. Must be set BEFORE hip_runtime.h / hip_vector_types.h.
#ifndef __HIP_USE_NATIVE_VECTOR__
#define __HIP_USE_NATIVE_VECTOR__ 0
#endif

#include <hip/hip_runtime.h>
#include <hip/hip_runtime_api.h>

// CUDA_VERSION is defined below, so PxCudaTypes.h skips its own declarations, including the
// PxSimpleTypes.h include that host seam files rely on.
#include "foundation/PxSimpleTypes.h"

// ---------------------------------------------------------------------------
// 1. Driver-API type + enum aliases (host seam).
// PhysX loads every kernel as a runtime module image via the CUDA driver API
// (cuModuleLoadDataEx / cuModuleGetFunction / cuLaunchKernel). The HIP driver
// API is a 1:1 peer; alias the handle/typedef names PhysX spells so the host
// seam and the DevicePointer templates compile unchanged.
// ---------------------------------------------------------------------------

// CUdeviceptr is an INTEGER address type in CUDA (`typedef unsigned long long
// CUdeviceptr` on a 64-bit host -- see PxCudaTypes.h). PhysX relies on that:
// it does integer arithmetic on device pointers (e.g. the 128-byte alignment
// round-up `(mPtr + 127) & ~127` in PxgCudaBuffer.h) and
// `reinterpret_cast<CUdeviceptr>(rawPtr)` at ~180 sites. HIP's hipDeviceptr_t
// is `void*`, on which none of that integer arithmetic is legal. So we alias
// CUdeviceptr to the CUDA-faithful integer type and cast to hipDeviceptr_t
// only at the driver-API boundary (the cuMem*/cuMemcpy* wrappers below), which
// is where the HIP runtime actually wants a void*.
typedef unsigned long long CUdeviceptr;
typedef hipModule_t      CUmodule;
typedef hipFunction_t    CUfunction;
typedef hipStream_t      CUstream;
typedef hipEvent_t       CUevent;
typedef hipCtx_t         CUcontext;
typedef hipDevice_t      CUdevice;
typedef hipError_t       CUresult;
typedef hipJitOption     CUjit_option;
typedef hipFunction_attribute CUfunction_attribute;
typedef unsigned long long PxCUdeviceptr;
typedef hipTextureObject_t CUtexObject;
typedef hipSurfaceObject_t CUsurfObject;
typedef hipArray_t       CUarray;
typedef hipGraphicsResource_t CUgraphicsResource;
typedef hipArray_Format   CUarray_format;

// cudaArray-format enum values (SDF texture upload path in PxgGeometryManager;
// on HIP the SDF texture path itself is compiled out -- CDNA has no image hw --
// but the enum names must still resolve so the surrounding code compiles).
#ifndef CU_AD_FORMAT_FLOAT
#define CU_AD_FORMAT_FLOAT           HIP_AD_FORMAT_FLOAT
#endif
#ifndef CU_AD_FORMAT_UNSIGNED_INT8
#define CU_AD_FORMAT_UNSIGNED_INT8   HIP_AD_FORMAT_UNSIGNED_INT8
#endif
#ifndef CU_AD_FORMAT_UNSIGNED_INT16
#define CU_AD_FORMAT_UNSIGNED_INT16  HIP_AD_FORMAT_UNSIGNED_INT16
#endif

// Event-creation flag PhysX passes to cuEventCreate.
#ifndef CU_EVENT_DISABLE_TIMING
#define CU_EVENT_DISABLE_TIMING      hipEventDisableTiming
#endif
#ifndef CU_EVENT_DEFAULT
#define CU_EVENT_DEFAULT             hipEventDefault
#endif

// Stream-creation flag PhysX passes to cuStreamCreate / streamCreateWithPriority.
#ifndef CU_STREAM_NON_BLOCKING
#define CU_STREAM_NON_BLOCKING       hipStreamNonBlocking
#endif
#ifndef CU_STREAM_DEFAULT
#define CU_STREAM_DEFAULT            hipStreamDefault
#endif

// Stream-priority query. The CUDA driver API takes the (implicit current)
// context; HIP queries the device directly. Both write [leastPriority,
// greatestPriority] and share the (int*, int*) signature, so a name alias is
// exact -- PhysX only ever calls it as cuCtxGetStreamPriorityRange(&lo, &hi).
#ifndef cuCtxGetStreamPriorityRange
#define cuCtxGetStreamPriorityRange  hipDeviceGetStreamPriorityRange
#endif

// Driver result codes PhysX explicitly tests against.
#ifndef CUDA_SUCCESS
#define CUDA_SUCCESS                        hipSuccess
#endif
#ifndef CUDA_ERROR_NO_BINARY_FOR_GPU
#define CUDA_ERROR_NO_BINARY_FOR_GPU        hipErrorNoBinaryForGpu
#endif
#ifndef CUDA_ERROR_NOT_FOUND
#define CUDA_ERROR_NOT_FOUND                hipErrorNotFound
#endif
#ifndef CUDA_ERROR_INVALID_VALUE
#define CUDA_ERROR_INVALID_VALUE            hipErrorInvalidValue
#endif
#ifndef CUDA_ERROR_OUT_OF_MEMORY
#define CUDA_ERROR_OUT_OF_MEMORY            hipErrorOutOfMemory
#endif
#ifndef CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES
#define CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES  hipErrorLaunchOutOfResources
#endif
#ifndef CUDA_ERROR_UNKNOWN
#define CUDA_ERROR_UNKNOWN                  hipErrorUnknown
#endif
#ifndef CUDA_ERROR_NOT_READY
#define CUDA_ERROR_NOT_READY                hipErrorNotReady
#endif

// Context-creation flags PhysX ORs when creating its primary context.
#ifndef CU_CTX_LMEM_RESIZE_TO_MAX
#define CU_CTX_LMEM_RESIZE_TO_MAX           hipDeviceLmemResizeToMax
#endif
#ifndef CU_CTX_SCHED_BLOCKING_SYNC
#define CU_CTX_SCHED_BLOCKING_SYNC          hipDeviceScheduleBlockingSync
#endif
#ifndef CU_CTX_MAP_HOST
#define CU_CTX_MAP_HOST                     hipDeviceMapHost
#endif

// PhysX's PxCudaContext.h uses `#ifdef CUDA_VERSION` to choose the strongly-
// typed PxCUenum<CUresult>/<CUjit_option> wrappers (which give the correct
// enum conversion operators) over the loose PxCUenum<PxU32> fallback. On a HIP
// build there is no <cuda.h> to define CUDA_VERSION, so define it here (to the
// ROCm-era CUDA-compat level PhysX's MIN_CUDA_VERSION test expects) so the
// seam picks the typed wrappers and PxCUresult<->hipError_t conversions work.
#ifndef CUDA_VERSION
#define CUDA_VERSION 12000
#endif

// JIT-option enum values (PhysX passes none at its main load site, but a few
// diagnostic callers reference these names).
#ifndef CU_JIT_MAX_REGISTERS
#define CU_JIT_MAX_REGISTERS                hipJitOptionMaxRegisters
#endif
#ifndef CU_JIT_INFO_LOG_BUFFER
#define CU_JIT_INFO_LOG_BUFFER              hipJitOptionInfoLogBuffer
#endif
#ifndef CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES
#define CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES   hipJitOptionInfoLogBufferSizeBytes
#endif
#ifndef CU_JIT_ERROR_LOG_BUFFER
#define CU_JIT_ERROR_LOG_BUFFER             hipJitOptionErrorLogBuffer
#endif
#ifndef CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES
#define CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES  hipJitOptionErrorLogBufferSizeBytes
#endif

// Function-attribute names PhysX queries via cuFuncGetAttribute.
#ifndef CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK
#define CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK hipFuncAttributeMaxThreadsPerBlock
#endif
#ifndef CU_FUNC_ATTRIBUTE_NUM_REGS
#define CU_FUNC_ATTRIBUTE_NUM_REGS          hipFuncAttributeNumRegs
#endif
#ifndef CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES
#define CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES hipFuncAttributeSharedSizeBytes
#endif

// Device-attribute enum names PhysX queries (compute-cap, SM count, etc.).
#ifndef CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR
#define CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR hipDeviceAttributeComputeCapabilityMajor
#endif
#ifndef CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR
#define CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR hipDeviceAttributeComputeCapabilityMinor
#endif
#ifndef CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT
#define CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT hipDeviceAttributeMultiprocessorCount
#endif
#ifndef CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK
#define CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK hipDeviceAttributeMaxThreadsPerBlock
#endif
#ifndef CU_DEVICE_ATTRIBUTE_INTEGRATED
#define CU_DEVICE_ATTRIBUTE_INTEGRATED       hipDeviceAttributeIntegrated
#endif
#ifndef CU_DEVICE_ATTRIBUTE_PCI_BUS_ID
#define CU_DEVICE_ATTRIBUTE_PCI_BUS_ID       hipDeviceAttributePciBusId
#endif
#ifndef CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY
#define CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY hipDeviceAttributeCanMapHostMemory
#endif
#ifndef CU_DEVICE_ATTRIBUTE_CLOCK_RATE
#define CU_DEVICE_ATTRIBUTE_CLOCK_RATE       hipDeviceAttributeClockRate
#endif
#ifndef CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK
#define CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK hipDeviceAttributeMaxSharedMemoryPerBlock
#endif
#ifndef CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_MULTIPROCESSOR
#define CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_MULTIPROCESSOR hipDeviceAttributeMaxSharedMemoryPerMultiprocessor
#endif

// ---------------------------------------------------------------------------
// Driver-API entry-point name remap (host seam). PhysX's CudaContextManager /
// PxgKernelWrangler call the CUDA driver API directly (cuModuleLoadDataEx,
// cuLaunchKernel, cuMemAlloc, cuCtx*, cuStream*, cuEvent*, ...). The HIP
// driver API is a 1:1 peer; remap the 43 cu* symbols PhysX uses to their hip*
// names so the host seam links against -lamdhip64. Names that are NOT a straight cu->hip rename are given explicitly.
// ---------------------------------------------------------------------------
#define cuInit                       hipInit
#define cuDriverGetVersion           hipDriverGetVersion
#define cuDeviceGet                  hipDeviceGet
#define cuDeviceGetName              hipDeviceGetName
#define cuDeviceGetAttribute         hipDeviceGetAttribute
#define cuDeviceTotalMem             hipDeviceTotalMem
#define cuDeviceGetCount             hipGetDeviceCount
#define cuCtxCreate                  hipCtxCreate
#define cuCtxDestroy                 hipCtxDestroy
#define cuCtxGetDevice               hipCtxGetDevice
#define cuCtxPushCurrent             hipCtxPushCurrent
#define cuCtxPopCurrent              hipCtxPopCurrent
#define cuCtxSynchronize             hipCtxSynchronize
#define cuModuleLoadDataEx           hipModuleLoadDataEx
#define cuModuleGetFunction          hipModuleGetFunction
#define cuModuleUnload               hipModuleUnload
#define cuLaunchKernel               hipModuleLaunchKernel
#define cuFuncGetAttribute           hipFuncGetAttribute
#define cuOccupancyMaxActiveBlocksPerMultiprocessor hipModuleOccupancyMaxActiveBlocksPerMultiprocessor
#define cuMemGetInfo                 hipMemGetInfo
// cuStreamCreate(phStream, flags) is the 2-arg CUDA driver form; its HIP peer
// is hipStreamCreateWithFlags (hipStreamCreate takes no flags arg).
#define cuStreamCreate               hipStreamCreateWithFlags
#define cuStreamCreateWithPriority   hipStreamCreateWithPriority
#define cuStreamDestroy              hipStreamDestroy
#define cuStreamQuery                hipStreamQuery
#define cuStreamSynchronize          hipStreamSynchronize
#define cuStreamWaitEvent            hipStreamWaitEvent
#define cuEventCreate                hipEventCreateWithFlags
#define cuEventDestroy               hipEventDestroy
#define cuEventQuery                 hipEventQuery
#define cuEventRecord                hipEventRecord
#define cuEventSynchronize           hipEventSynchronize
// Memory-host-alloc flags PhysX ORs.
#ifndef CU_MEMHOSTALLOC_PORTABLE
#define CU_MEMHOSTALLOC_PORTABLE     hipHostMallocPortable
#endif
#ifndef CU_MEMHOSTALLOC_DEVICEMAP
#define CU_MEMHOSTALLOC_DEVICEMAP    hipHostMallocMapped
#endif
#ifndef CU_MEMHOSTALLOC_WRITECOMBINED
#define CU_MEMHOSTALLOC_WRITECOMBINED hipHostMallocWriteCombined
#endif

// ---------------------------------------------------------------------------
// 1b. Driver memory-API boundary wrappers (host seam).
//
// PhysX spells device addresses as the integer CUdeviceptr (aliased to
// `unsigned long long` above, CUDA-faithful). The HIP driver memory API wants
// hipDeviceptr_t (`void*`). Rather than casting at the ~180 PhysX call sites,
// we provide inline wrappers for the handful of driver entry points that take
// or return a device pointer, casting integer<->void* exactly at this one
// boundary. Names match the CUDA driver spellings so CudaContextManager.cpp /
// PxgKernelWrangler compile unchanged.
//
// Plain host inline functions (they call host runtime entry points). They are
// `inline` and never ODR-used from device code, so a .cu's device pass simply
// discards them; the host pass / the host .cpp seam links against them.
static inline hipError_t cuMemAlloc(CUdeviceptr* dptr, size_t bytesize)
{
	void* p = 0;
	hipError_t e = hipMalloc(&p, bytesize);
	*dptr = reinterpret_cast<CUdeviceptr>(p);
	return e;
}
static inline hipError_t cuMemFree(CUdeviceptr dptr)
{
	return hipFree(reinterpret_cast<void*>(dptr));
}
static inline hipError_t cuMemHostAlloc(void** pp, size_t bytesize, unsigned int flags)
{
	return hipHostMalloc(pp, bytesize, flags);
}
static inline hipError_t cuMemFreeHost(void* p)
{
	return hipHostFree(p);
}
static inline hipError_t cuMemHostGetDevicePointer(CUdeviceptr* pdptr, void* p, unsigned int flags)
{
	void* dp = 0;
	hipError_t e = hipHostGetDevicePointer(&dp, p, flags);
	*pdptr = reinterpret_cast<CUdeviceptr>(dp);
	return e;
}
static inline hipError_t cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t n)
{
	return hipMemcpyDtoH(dstHost, reinterpret_cast<void*>(srcDevice), n);
}
static inline hipError_t cuMemcpyDtoHAsync(void* dstHost, CUdeviceptr srcDevice, size_t n, hipStream_t s)
{
	return hipMemcpyDtoHAsync(dstHost, reinterpret_cast<void*>(srcDevice), n, s);
}
static inline hipError_t cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t n)
{
	return hipMemcpyHtoD(reinterpret_cast<void*>(dstDevice), srcHost, n);
}
static inline hipError_t cuMemcpyHtoDAsync(CUdeviceptr dstDevice, const void* srcHost, size_t n, hipStream_t s)
{
	return hipMemcpyHtoDAsync(reinterpret_cast<void*>(dstDevice), srcHost, n, s);
}
static inline hipError_t cuMemcpyDtoD(CUdeviceptr dstDevice, CUdeviceptr srcDevice, size_t n)
{
	return hipMemcpyDtoD(reinterpret_cast<void*>(dstDevice), reinterpret_cast<void*>(srcDevice), n);
}
static inline hipError_t cuMemcpyDtoDAsync(CUdeviceptr dstDevice, CUdeviceptr srcDevice, size_t n, hipStream_t s)
{
	return hipMemcpyDtoDAsync(reinterpret_cast<void*>(dstDevice), reinterpret_cast<void*>(srcDevice), n, s);
}
// CUDA cuMemcpyPeerAsync takes src/dst CUcontext; HIP's peer copy takes device
// ordinals. PhysX only needs the async peer copy; resolve each context to its
// device ordinal via hipCtxGetDevice.
static inline hipError_t cuMemcpyPeerAsync(CUdeviceptr dstDevice, hipCtx_t dstCtx,
	CUdeviceptr srcDevice, hipCtx_t srcCtx, size_t n, hipStream_t s)
{
	int dstOrd = 0, srcOrd = 0;
	hipCtx_t prev = 0;
	hipCtxGetCurrent(&prev);
	hipCtxSetCurrent(dstCtx); hipCtxGetDevice(&dstOrd);
	hipCtxSetCurrent(srcCtx); hipCtxGetDevice(&srcOrd);
	hipCtxSetCurrent(prev);
	return hipMemcpyPeerAsync(reinterpret_cast<void*>(dstDevice), dstOrd,
		reinterpret_cast<void*>(srcDevice), srcOrd, n, s);
}
static inline hipError_t cuMemsetD8(CUdeviceptr dst, unsigned char v, size_t n)
{
	return hipMemsetD8(reinterpret_cast<void*>(dst), v, n);
}
static inline hipError_t cuMemsetD16(CUdeviceptr dst, unsigned short v, size_t n)
{
	return hipMemsetD16(reinterpret_cast<void*>(dst), v, n);
}
static inline hipError_t cuMemsetD32(CUdeviceptr dst, unsigned int v, size_t n)
{
	return hipMemsetD32(reinterpret_cast<void*>(dst), (int)v, n);
}
static inline hipError_t cuMemsetD8Async(CUdeviceptr dst, unsigned char v, size_t n, hipStream_t s)
{
	return hipMemsetD8Async(reinterpret_cast<void*>(dst), v, n, s);
}
static inline hipError_t cuMemsetD32Async(CUdeviceptr dst, unsigned int v, size_t n, hipStream_t s)
{
	return hipMemsetD32Async(reinterpret_cast<void*>(dst), (int)v, n, s);
}


// ---------------------------------------------------------------------------
// 2. Device-side warp-collective WIDTH-PIN (device seam).
//
// PhysX calls __shfl_*_sync mostly in the 3-argument form
// (__shfl_xor_sync(FULL_MASK, val, delta)) with no explicit width, which on
// HIP defaults to warpSize (64) -- shuffling ACROSS the two 32-lane logical
// warps that share a wave64 and silently corrupting every warp reduction /
// scan. We wrap the whole __shfl_*_sync family so the 3-arg forms inject
// width=WARP_SIZE(=32) and the 4-arg forms honour the caller's width, and
// widen the sync mask to the wave-portable 64-bit ~0ull.
//
// Implemented as inline function templates in an internal namespace + macros
// that redirect the call sites, so we do NOT redeclare the native _sync
// intrinsics (which would collide with the amd_hip_bf16.h default-argument
// overloads).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Type-alignment specifiers -- needed for BOTH host and device compilation, so
// they live OUTSIDE the __HIPCC__ device-only block below (the host-seam .cpp
// pull in PxgConstraintPrep.h etc. as plain C++ and must see them too).
//
// CUDA's __builtin_align__(n) has no HIP builtin, and __align__(n) is defined
// by HIP only for DEVICE compilation (amd_hip_runtime.h gates it on
// __HIP_DEVICE_COMPILE__), so it is ABSENT when these structs are compiled in
// the host .cpp. Map both to the portable Clang/GCC aligned-type attribute so
// PhysX's alignment contract (e.g. __builtin_align__(16) PxgMaterialContactData)
// holds on both paths.
#ifndef __align__
#define __align__(n) __attribute__((aligned(n)))
#endif
// Do NOT guard __builtin_align__ with #ifndef -- Clang reports the reserved
// builtin name as already "defined" to the preprocessor, so an #ifndef would
// skip our macro and leave the raw builtin, which is not a valid type-attribute
// spelling in host C++ ("expected unqualified-id"). Undef then define.
#undef __builtin_align__
#define __builtin_align__(n) __attribute__((aligned(n)))

#if defined(__HIPCC__)

namespace physx { namespace hipcompat {

// Logical warp width the whole PhysX GPU stack is built around.
#ifndef PXG_LOGICAL_WARP_WIDTH
#define PXG_LOGICAL_WARP_WIDTH 32
#endif

// __shfl_sync ------------------------------------------------------------
template <typename T>
__device__ __forceinline__ T shflSync(unsigned long long, T v, int srcLane, int width = PXG_LOGICAL_WARP_WIDTH)
{
	return __shfl(v, srcLane, width);
}
// __shfl_xor_sync --------------------------------------------------------
template <typename T>
__device__ __forceinline__ T shflXorSync(unsigned long long, T v, int laneMask, int width = PXG_LOGICAL_WARP_WIDTH)
{
	return __shfl_xor(v, laneMask, width);
}
// __shfl_up_sync ---------------------------------------------------------
template <typename T>
__device__ __forceinline__ T shflUpSync(unsigned long long, T v, unsigned int delta, int width = PXG_LOGICAL_WARP_WIDTH)
{
	return __shfl_up(v, delta, width);
}
// __shfl_down_sync -------------------------------------------------------
template <typename T>
__device__ __forceinline__ T shflDownSync(unsigned long long, T v, unsigned int delta, int width = PXG_LOGICAL_WARP_WIDTH)
{
	return __shfl_down(v, delta, width);
}

// Vote intrinsics. HIP __ballot returns a 64-bit wave vote with bits at
// ABSOLUTE wave-lane positions (0..63). PhysX, however, is width-32-pinned:
// it consumes the ballot with subgroup-local lane ids (threadIdx.x & 31, i.e.
// 0..31) and 32-bit overloads of lowestSetIndex(__ffs)/__popc. To keep those
// call sites correct on wave64 we return a SUBGROUP-LOCAL 32-bit mask: take
// the 32 ballot bits belonging to this thread's 32-lane subgroup (the low
// half for lanes 0..31, the high half for lanes 32..63) and shift them down
// to bit 0. This makes ballot bit i correspond to logical lane i (0..31),
// exactly matching the CUDA build's 32-lane FULL_MASK semantics.
//
// PARTIAL-MASK CORRECTNESS: the returned value is ANDed with the lower 32 bits
// of `mask` so that only the lanes named in the mask participate in the vote.
// For FULL_MASK (0xFFFFFFFF) this AND is a no-op, preserving the prior behavior.
// Without this AND, partial-mask callers (e.g. cudaBox.cu groupMask=0xf<<k,
// gjk.cuh mask_bestDist, reduction.cuh syncMask) would silently get votes from
// ALL 32 subgroup lanes instead of just the designated ones.
__device__ __forceinline__ unsigned int ballotSync(unsigned long long mask, int pred)
{
	unsigned long long b = __ballot(pred);
	const unsigned int laneInWave = __lane_id();
	const unsigned int subgroupBase = laneInWave & ~31u;      // 0 or 32
	// Subgroup-local 32-bit ballot (bits 0..31 = logical lane 0..31)
	const unsigned int vote = (unsigned int)((b >> subgroupBase) & 0xFFFFFFFFull);
	// AND with the lower-32 bits of mask: restrict to the named lanes only.
	return vote & (unsigned int)(mask & 0xFFFFFFFFull);
}
// any/all vote. Build from the SUBGROUP-LOCAL masked ballot so partial-mask
// sites are correct and wave64 cross-subgroup leakage is prevented.
//   anySync(mask, pred) => at least one mask lane voted true
//   allSync(mask, pred) => all mask lanes voted true
// This is the general form of CUDA __any_sync/__all_sync.
// For FULL_MASK sites the mask AND is a no-op; for partial masks (e.g. a
// 4-lane group mask in cudaBox.cu, or a bestDist result mask in gjk.cuh)
// only the named lanes count -- matching CUDA semantics.
__device__ __forceinline__ int anySync(unsigned long long mask, int pred)
{
	const unsigned int vote = ballotSync(mask, pred);
	const unsigned int m32  = (unsigned int)(mask & 0xFFFFFFFFull);
	return (vote & m32) != 0u ? 1 : 0;
}
__device__ __forceinline__ int allSync(unsigned long long mask, int pred)
{
	const unsigned int vote = ballotSync(mask, pred);
	const unsigned int m32  = (unsigned int)(mask & 0xFFFFFFFFull);
	// All mask lanes must have voted true.
	return (vote & m32) == m32 ? 1 : 0;
}

// __syncwarp: PhysX calls both __syncwarp() and __syncwarp(mask) with a
// 32-bit FULL_MASK. HIP's native __syncwarp requires a 64-bit mask and only
// exists on newer ROCm. A wave executes in lockstep, so the convergence point
// is a wave barrier, which is correct (and conservatively stronger) for the
// 32-lane logical warps that share a wave64. The barrier alone only stops the
// compiler from moving code across it; PhysX also relies on __syncwarp to make
// one lane's shared-memory writes visible to the other lanes, so wrap it in
// wavefront-scope release and acquire fences (as HIP's own __syncwarp does).
// Overloads cover the 0-arg and masked forms.
__device__ __forceinline__ void syncWarp()
{
	__builtin_amdgcn_fence(__ATOMIC_RELEASE, "wavefront");
	__builtin_amdgcn_wave_barrier();
	__builtin_amdgcn_fence(__ATOMIC_ACQUIRE, "wavefront");
}
__device__ __forceinline__ void syncWarp(unsigned long long)
{
	syncWarp();
}

}} // namespace physx::hipcompat

// Redirect the CUDA _sync spellings to the width-pinned wrappers. Because the
// wrappers are overload-resolved templates with a default width argument,
// both the 3-arg and 4-arg call forms in PhysX resolve correctly.
#define __shfl_sync(mask, ...)       ::physx::hipcompat::shflSync((unsigned long long)(mask), __VA_ARGS__)
#define __shfl_xor_sync(mask, ...)   ::physx::hipcompat::shflXorSync((unsigned long long)(mask), __VA_ARGS__)
#define __shfl_up_sync(mask, ...)    ::physx::hipcompat::shflUpSync((unsigned long long)(mask), __VA_ARGS__)
#define __shfl_down_sync(mask, ...)  ::physx::hipcompat::shflDownSync((unsigned long long)(mask), __VA_ARGS__)
#define __ballot_sync(mask, pred)    ::physx::hipcompat::ballotSync((unsigned long long)(mask), (pred))
#define __any_sync(mask, pred)       ::physx::hipcompat::anySync((unsigned long long)(mask), (pred))
#define __all_sync(mask, pred)       ::physx::hipcompat::allSync((unsigned long long)(mask), (pred))
#define __syncwarp(...)              ::physx::hipcompat::syncWarp(__VA_ARGS__)

// __ldg is a plain load on CDNA (no read-only texture cache path needed).
#ifndef __ldg
#define __ldg(ptr) (*(ptr))
#endif


// __launch_bounds__ second parameter differs: CUDA = MIN_BLOCKS_PER_SM,
// HIP = MIN_WARPS_PER_EU. HIP already provides its own variadic
// __launch_bounds__ macro (amd_hip_runtime.h) that accepts the 2-arg form and
// treats the second value as MIN_WARPS_PER_EU, so we do NOT redefine it
// (a redefinition here shadows HIP's and breaks the attribute -> "unknown
// type name '__launch_bounds__'"). The CUDA min-blocks value carried through
// is only an occupancy HINT (never a correctness contract -- the hard bound
// is the first arg, max-threads-per-block); an incorrect hint at most costs
// occupancy. The HIP equivalent of a CUDA min-blocks value is
// MIN_WARPS_PER_EU = (MIN_BLOCKS * MAX_THREADS) / (warpSize * 2).

#endif // __HIPCC__

#endif // __HIP_PLATFORM_AMD__

#endif // PXG_CUDA_TO_HIP_H
