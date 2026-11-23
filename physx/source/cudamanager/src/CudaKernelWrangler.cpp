// SPDX-FileCopyrightText: Copyright (c) 2008-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include <stdio.h>

#include "foundation/PxPreprocessor.h"
#include "foundation/PxAssert.h"
#include "foundation/PxErrorCallback.h"
#include "foundation/PxString.h"

// from the point of view of this source file the GPU library is linked statically
#ifndef PX_PHYSX_GPU_STATIC
	#define PX_PHYSX_GPU_STATIC
#endif
#include "PxPhysXGpu.h"

#if PX_LINUX && PX_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
#include <cuda.h>
#if PX_LINUX && PX_CLANG
#pragma clang diagnostic pop
#endif

#include <texture_types.h>
#include <vector_types.h>

#include "cudamanager/PxCudaContextManager.h"
#include "cudamanager/PxCudaContext.h"

#include "CudaKernelWrangler.h"

using namespace physx;

const char* KernelWrangler::getCuFunctionName(uint16_t funcIndex) const
{
	// PT: this is wrong, functions there are not listed in the same order as mCuFunctions
	//return gFunctionTable[funcIndex].functionName;
	return mKernelNames[funcIndex];
}

KernelWrangler::KernelWrangler(PxCudaContextManager& cudaContextManager, PxErrorCallback& errorCallback, const char** funcNames, uint16_t numFuncs)
	: mError(false)
	, mKernelNames(NULL)
	, mCuFunctions("CuFunctions")
	, mCudaContextManager(cudaContextManager)
	, mCudaContext(cudaContextManager.getCudaContext())
	, mErrorCallback(errorCallback)
{
	// PT: TODO: consider revisiting this code so that the function order is kept the same between mCuFunctions and gFunctionTable.
	// That way the initial getCuFunctionName implementation could be kept and we could decouple the code from the external funcNames array again.

	// PT: proper names defined in PxgKernelWrangler.cpp. We assume the data there remains valid for the lifetime of the app and we don't need to do a copy.
	PX_ASSERT(funcNames);
	mKernelNames = funcNames;

	// matchup funcNames to CUDA modules, get CUfunction handles
	CUmodule* cuModules = cudaContextManager.getCuModules();
	PxKernelIndex* cuFunctionTable = PxGpuGetCudaFunctionTable();
	const PxU32 cuFunctionTableSize = PxGpuGetCudaFunctionTableSize();
#if defined(__HIP_PLATFORM_AMD__) && !defined(__HIPCC__)
	const PxU32 cuModuleTableSize = PxGpuGetCudaModuleTableSize();
#endif

	mCuFunctions.resize(numFuncs, NULL);

	if (mCudaContextManager.tryAcquireContext())
	{
		for (uint32_t i = 0; i < numFuncs; ++i)
		{
			// search through all known functions
			for (uint32_t j = 0; ; ++j)
			{
				if (j == cuFunctionTableSize)
				{
					// printf("Could not find registered CUDA function '%s'.\n", funcNames[i]);

					char buffer[256];
					Pxsnprintf(buffer, 256, "Could not find registered CUDA function '%s'.", funcNames[i]);
					mErrorCallback.reportError(PxErrorCode::eINTERNAL_ERROR, buffer, PX_FL);
					mError = true;
					break;
				}


				if (!Pxstrcmp(cuFunctionTable[j].functionName, funcNames[i]))
				{
#if defined(__HIP_PLATFORM_AMD__) && !defined(__HIPCC__)
					// ROCm/HIP: the per-function moduleIndex recorded during
					// static init is NOT reliable. -fgpu-rdc is disabled, so each
					// .cu TU produces an INDEPENDENT fatbin and __hipRegisterFatBinary
					// is called once per TU; but the fatCubinHandle the HIP runtime
					// hands to __hipRegisterFunction is opaque, so we cannot map a
					// function back to the s_moduleTable index of its own TU (the
					// old code registered every function under moduleIndex 0, which
					// can only resolve kernels living in the FIRST loaded module).
					// Instead, resolve the kernel by scanning ALL loaded modules
					// with hipModuleGetFunction and taking the first hit. The lookup
					// is O(numFuncs * numModules) once at startup and is robust to
					// however the toolchain distributes kernels across TUs.
					PxCUresult ret = PxCUresult(CUDA_ERROR_NOT_FOUND);
					for (PxU32 m = 0; m < cuModuleTableSize; ++m)
					{
						if (!cuModules[m]) continue;
						ret = mCudaContext->moduleGetFunction(&mCuFunctions[i], cuModules[m], funcNames[i]);
						if (ret == CUDA_SUCCESS && mCuFunctions[i])
							break;
						mCuFunctions[i] = NULL;
					}
					// Drain the sticky HIP error latched by each module MISS
					// (hipErrorNotFound). With 61 modules x ~396 functions the
					// ctor exits with a stale hipErrorNotFound that would be
					// misattributed to the next unrelated hip* call -- same
					// fault class as the module-load loop drain in
					// CudaContextManager.cpp. (plan Risk #3.)
					hipGetLastError();
#else
					PxCUresult ret = mCudaContext->moduleGetFunction(&mCuFunctions[i], cuModules[cuFunctionTable[j].moduleIndex], funcNames[i]);
#endif
					if (ret != CUDA_SUCCESS)
					{
						char buffer[256];
						Pxsnprintf(buffer, 256, "Could not find CUDA module containing function '%s'.", funcNames[i]);
						mErrorCallback.reportError(PxErrorCode::eINTERNAL_ERROR, buffer, PX_FL);
						mError = true;
						// return;
					}
					break;
				}
			}
		}
		mCudaContextManager.releaseContext();
	}
	else 
	{
		char buffer[256];
		Pxsnprintf(buffer, 256, "Failed to acquire the cuda context.");
		mErrorCallback.reportError(PxErrorCode::eINTERNAL_ERROR, buffer, PX_FL);
		mError = true;
	}
}

/*
 * Workaround hacks for using nvcc --compiler output object files
 * without linking with CUDART.  We must implement our own versions
 * of these functions that the object files are hard-coded to call into.
 * These calls are all made _before_ main() during static initialization
 * of this DLL.
 */

// Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
// ROCm/HIP: this entire block re-implements the NVIDIA CUDA-RUNTIME (cudart)
// ABI that nvcc-produced object files are hard-coded to call during static
// initialization (__cudaRegisterFatBinary / cudaLaunch / cudaCreateChannelDesc
// / __cudaPopCallConfiguration / ...). On ROCm the GPU objects are produced by
// amdclang, do NOT emit calls to these NVIDIA cudart symbols, and PhysX
// populates its module table explicitly via the HIP driver API
// (hipModuleLoadDataEx in CudaContextManager). So the whole cudart-intercept
// block is dead code on HIP and is compiled out (it would otherwise need the
// full cudart type set: cudaError_t, cudaChannelFormatDesc, CUDARTAPI, ...).
// See projects/PhysX/notes.md and KB cudart-fatbin-abi-stubs-nvcc-only.
#if !defined(__HIP_PLATFORM_AMD__) && !defined(__HIPCC__)

#include <driver_types.h>

#if PX_WINDOWS_FAMILY
#define CUDARTAPI __stdcall
#endif

struct uint3;
struct dim3;

extern "C"
void** CUDARTAPI __cudaRegisterFatBinary(void* fatBin)
{
	return PxGpuCudaRegisterFatBinary(fatBin);
}

extern "C"
void CUDARTAPI __cudaRegisterFatBinaryEnd(void ** /*fatCubinHandle*/)
{
}

extern "C"
void CUDARTAPI __cudaUnregisterFatBinary(void** fatCubinHandle)
{
	// jcarius: not ideal because the module may still be loaded
	PxGpuGetCudaModuleTable()[(int)(size_t) fatCubinHandle] = 0;
}

extern "C"
void CUDARTAPI __cudaRegisterTexture(void**, const struct textureReference*, const void**, const char*, int, int, int)
{
}

extern "C" void CUDARTAPI __cudaRegisterVar(void**, char*, char*, const char*, int, int, int, int)
{
}


extern "C" void CUDARTAPI __cudaRegisterShared(void**, void**)
{
}

extern "C"
void CUDARTAPI __cudaRegisterFunction(void** fatCubinHandle, const char*, 
	char*, const char* deviceName, int, uint3*, uint3*, dim3*, dim3*, int*)
{
	PxGpuCudaRegisterFunction((int)(size_t) fatCubinHandle, deviceName);
}

/* These functions are implemented just to resolve link dependencies */

extern "C"
cudaError_t CUDARTAPI cudaLaunch(const char* entry)
{
	PX_UNUSED(entry);
	return cudaSuccess;
}

extern "C"
cudaError_t CUDARTAPI cudaLaunchKernel( const void* , dim3 , dim3 , void** , size_t , cudaStream_t  ) 
{
	return cudaSuccess;
}

extern "C"
cudaError_t CUDARTAPI cudaSetupArgument(const void*, size_t, size_t)
{
	return cudaSuccess;
}

extern "C"
struct cudaChannelFormatDesc CUDARTAPI cudaCreateChannelDesc(
    int x, int y, int z, int w, enum cudaChannelFormatKind f)
{
	struct cudaChannelFormatDesc desc;
	desc.x = x;
	desc.y = y;
	desc.z = z;
	desc.w = w;
	desc.f = f;
	return desc;
}

extern "C" 
cudaError_t CUDARTAPI __cudaPopCallConfiguration(
  dim3         *,
  dim3         *,
  size_t       *,
  void         *)
{
	return cudaSuccess;
}

#endif // !__HIP_PLATFORM_AMD__ (NVIDIA cudart-ABI intercept block)

// ---------------------------------------------------------------------------
// ROCm/HIP module-table registration via __hipRegisterFatBinary interception.
//
// On NVIDIA/CUDA, nvcc-compiled objects call the cudart ABI stubs above
// (__cudaRegisterFatBinary / __cudaRegisterFunction) during static init of the
// shared library, populating PhysX's s_moduleTable / s_FunctionTable BEFORE
// main() runs.  CudaContextManager then calls cuModuleLoadDataEx on each entry.
//
// On HIP, amdclang-compiled objects call the HIP runtime's own
// __hipRegisterFatBinary / __hipRegisterFunction during static init.  Those
// calls go directly into libamdhip64's internal tables; PhysX's tables remain
// empty.  The HIP runtime manages kernel dispatch via those internal tables
// (through the hipLaunchKernel host-stub ABI), but PhysX drives kernels via
// the *driver* API (hipModuleLoadDataEx / hipModuleGetFunction /
// hipModuleLaunchKernel) -- for which it needs its own module handles.
//
// APPROACH: define OUR OWN __hipRegisterFatBinary / __hipRegisterFunction
// that the linker PREFERS over the one in libamdhip64 (strong definition here
// vs. the runtime's exported one in the DSO).  Our wrappers:
//   1. Forward to the real HIP runtime implementation (via dlsym RTLD_NEXT) so
//      the HIP runtime still tracks the kernels for its own dispatch path.
//   2. Also populate PhysX's s_moduleTable so CudaContextManager can load the
//      fatbins with hipModuleLoadDataEx for the driver-API path.
//
// IMPORTANT: this file is compiled as PLAIN C++ (NOT -x hip) so __HIPCC__ is
// NOT defined.  The HIP path guard is __HIP_PLATFORM_AMD__ which IS set by the
// host-seam build step via -D__HIP_PLATFORM_AMD__ on the C++ compiler.
//
// The interception works because ld.lld resolves __hipRegisterFatBinary to the
// FIRST strong definition -- ours (in the shared library being built) wins over
// libamdhip64's exported weak/versioned one when both are visible.
// ---------------------------------------------------------------------------
#if defined(__HIP_PLATFORM_AMD__) && !defined(__HIPCC__)

#include <dlfcn.h>
#include <hip/hip_runtime_api.h>

// Forward-declare the HIP runtime types we need.
// The __hip_fatbin_header is an opaque structure; the data pointer passed to
// __hipRegisterFatBinary is the __CLANG_OFFLOAD_BUNDLE__ header or the raw
// HSACO image depending on the compilation mode.

// ROCm/HIP: store the fatbin image pointers in s_moduleTable so
// CudaContextManager can call hipModuleLoadDataEx on each one.
// Call the real runtime's __hipRegisterFatBinary via dlsym(RTLD_NEXT) so the
// HIP runtime also tracks the kernels for its host-stub dispatch path.
extern "C"
void** __hipRegisterFatBinary(const void* data)
{
	// Forward to the real HIP runtime first so the HIP runtime's own
	// dispatch path is not broken (hipLaunchKernel host stubs still work).
	typedef void** (*RealFn)(const void*);
	static RealFn realFn = NULL;
	if (!realFn)
	{
		realFn = (RealFn) dlsym(RTLD_NEXT, "__hipRegisterFatBinary");
	}
	void** hipHandle = realFn ? realFn(data) : NULL;

	// Store the raw `data` pointer in PhysX's module table.
	//
	// `data` is the __hip_fatbin_wrapper pointer that amdclang stores in the
	// .hip_fatbin section for this TU:
	//   struct { uint32_t magic(0x48495046); uint32_t version; const void* binary; void* reserved; }
	// where `binary` points to the actual __CLANG_OFFLOAD_BUNDLE__ data.
	// CudaContextManager.cpp unwraps the struct (checks the magic, takes the
	// inner `binary` pointer) and calls hipModuleLoadData(&hipMod, binary_ptr),
	// which parses the bundle and extracts the gfx942 HSACO -- analogous to
	// cuModuleLoadDataEx for CUDA fatbins on NVIDIA.
	//
	// NOTE: we do NOT store the hipHandle here because the HIP runtime loads
	// the HSACO lazily (the hipModule_t pointed to by hipHandle is null until
	// the first real kernel dispatch through hipLaunchKernel).
	PxGpuCudaRegisterFatBinary(const_cast<void*>(data));

	// Return the HIP runtime's handle (also passed to __hipRegisterFunction
	// below as fatCubinHandle; we forward it to the real runtime there).
	return hipHandle;
}

extern "C"
void __hipRegisterFunction(
    void** fatCubinHandle,
    const void* hostFunction,
    char* deviceFunction,
    const char* deviceName,
    int threadLimit,
    uint3* tid,
    uint3* bid,
    dim3* bDim,
    dim3* gDim,
    int* wSize)
{
	(void)hostFunction; (void)deviceFunction; (void)threadLimit;
	(void)tid; (void)bid; (void)bDim; (void)gDim; (void)wSize;

	// Forward to the real HIP runtime.
	typedef void (*RealFn)(void**, const void*, char*, const char*, int, uint3*, uint3*, dim3*, dim3*, int*);
	static RealFn realFn = NULL;
	if (!realFn)
	{
		realFn = (RealFn) dlsym(RTLD_NEXT, "__hipRegisterFunction");
	}
	if (realFn)
	{
		realFn(fatCubinHandle, hostFunction, deviceFunction, deviceName,
		       threadLimit, tid, bid, bDim, gDim, wSize);
	}

	// The fatCubinHandle the HIP runtime hands us is OPAQUE -- it cannot be
	// mapped back to the s_moduleTable index of the TU this kernel lives in.
	// Because -fgpu-rdc is disabled, each .cu TU is an INDEPENDENT fatbin, so
	// there is no single "monolithic" module and a fixed moduleIndex (e.g. 0)
	// would only ever resolve kernels in the first loaded module. We therefore
	// register every function with a SENTINEL moduleIndex (-1, "unknown") and
	// KernelWrangler resolves the handle by scanning ALL loaded modules with
	// hipModuleGetFunction (see the __HIP_PLATFORM_AMD__ branch in this file's
	// PxgKernelWrangler ctor). The moduleIndex is intentionally unused on HIP.
	// (KB hip-kernel-resolve-scan-all-modules-no-fgpu-rdc.)
	PxGpuCudaRegisterFunction(-1, deviceName);
}

extern "C"
void __hipRegisterVar(
    void** fatCubinHandle,
    void* hostVar,
    const char* deviceAddress,
    const char* deviceName,
    int ext, size_t size, int constant, int global)
{
	(void)fatCubinHandle; (void)hostVar; (void)deviceAddress;
	(void)deviceName; (void)ext; (void)size; (void)constant; (void)global;
	// No equivalent in PhysX's module API; forward to runtime only.
	typedef void (*RealFn)(void**, void*, const char*, const char*, int, size_t, int, int);
	static RealFn realFn = NULL;
	if (!realFn) realFn = (RealFn) dlsym(RTLD_NEXT, "__hipRegisterVar");
	if (realFn) realFn(fatCubinHandle, hostVar, deviceAddress, deviceName, ext, size, constant, global);
}

extern "C"
void __hipUnregisterFatBinary(void** fatCubinHandle)
{
	// Forward to the real HIP runtime.
	typedef void (*RealFn)(void**);
	static RealFn realFn = NULL;
	if (!realFn) realFn = (RealFn) dlsym(RTLD_NEXT, "__hipUnregisterFatBinary");
	if (realFn) realFn(fatCubinHandle);
	// The module table entry is zeroed in CudaContextManager on shutdown; no action needed here.
}

#endif // __HIP_PLATFORM_AMD__ && !__HIPCC__
