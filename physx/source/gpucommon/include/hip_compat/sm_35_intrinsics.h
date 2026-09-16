// Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// PhysX GPU HIP/ROCm compatibility shim: CUDA SM-versioned intrinsics header
// <sm_35_intrinsics.h> maps to the HIP runtime + PhysX HIP compat layer (which supplies __ldg,
// the width-32 __shfl_* wrappers, atomics, etc.).
#ifndef PXG_HIP_COMPAT_sm_35_intrinsics_H
#define PXG_HIP_COMPAT_sm_35_intrinsics_H
#include "cuda_to_hip.h"
#endif
