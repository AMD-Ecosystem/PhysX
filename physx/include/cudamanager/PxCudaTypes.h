// SPDX-FileCopyrightText: Copyright (c) 2008-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
#ifndef PX_CUDA_TYPES_H
#define PX_CUDA_TYPES_H

//type definitions to avoid forced inclusion of cuda.h
//if cuda.h is needed anyway, please include it before PxCudaContextManager.h, PxCudaContext.h or PxCudaTypes.h

#include "foundation/PxPreprocessor.h"

// On a ROCm/HIP build the CU* driver handle names are aliased to their hip*
// peers by the PhysX HIP compat header (cuda_to_hip.h, force-included).
// Pulling it here (and short-circuiting the CUDA forward-declarations below)
// gives every PxCudaContext / PxgKernelWrangler seam the hip* types without
// re-declaring them (which would be a "typedef redefinition with different
// types" error against the hipDeviceptr_t/hipModule_t aliases).
#if defined(__HIP_PLATFORM_AMD__) || defined(__HIPCC__)
// PxU32 and the other Px scalar types are used by PxCudaContext.h's PxCUenum
// template (and elsewhere on the host seam) immediately after this header. On
// the CUDA path they arrive via the PxSimpleTypes.h include in the #else branch
// below; the HIP branch must pull them too, otherwise a .cpp whose first heavy
// include is PxCudaContext.h (e.g. PxgAnisotropy.cpp, PxgParticleNeighborhood.cpp
// via PxgKernelLauncher.h) sees "unknown type name 'PxU32'". Include-guarded, so
// it is a no-op where the types are already present.
#include "foundation/PxSimpleTypes.h"
#include "cuda_to_hip.h"
#define CU_MEMHOSTALLOC_PORTABLE 0x01
#define CU_MEMHOSTALLOC_DEVICEMAP 0x02
#define CU_MEMHOSTALLOC_WRITECOMBINED 0x04
#else

#if PX_SUPPORT_GPU_PHYSX
#ifndef CUDA_VERSION

#include "foundation/PxSimpleTypes.h"

#if PX_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++98-compat-pedantic"
#endif

#if PX_P64_FAMILY
typedef unsigned long long CUdeviceptr;
#else
typedef unsigned int CUdeviceptr;
#endif

#if PX_CLANG
#pragma clang diagnostic pop
#endif

typedef int CUdevice;

typedef struct CUctx_st* CUcontext;
typedef struct CUmod_st* CUmodule;
typedef struct CUfunc_st* CUfunction;
typedef struct CUstream_st* CUstream;
typedef struct CUevent_st* CUevent;
typedef struct CUgraphicsResource_st* CUgraphicsResource;

#define CU_MEMHOSTALLOC_PORTABLE 0x01
#define CU_MEMHOSTALLOC_DEVICEMAP 0x02
#define CU_MEMHOSTALLOC_WRITECOMBINED 0x04

#else

PX_COMPILE_TIME_ASSERT(CU_MEMHOSTALLOC_PORTABLE == 0x01);
PX_COMPILE_TIME_ASSERT(CU_MEMHOSTALLOC_DEVICEMAP == 0x02);
PX_COMPILE_TIME_ASSERT(CU_MEMHOSTALLOC_WRITECOMBINED == 0x04);

#endif

#else
typedef struct CUstream_st* CUstream; // We declare some callbacks taking CUstream as an argument even when building with PX_SUPPORT_GPU_PHYSX = 0.
typedef struct CUevent_st* CUevent;
#endif // PX_SUPPORT_GPU_PHYSX

#endif // __HIP_PLATFORM_AMD__
#endif

