// Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause
// PhysX GPU HIP/ROCm compatibility shim: maps the CUDA driver-API header
// <cuda.h> onto the HIP runtime + the PhysX HIP compat layer. Placed on the
// HIP build's include path so PhysX's existing `#include "cuda.h"` /
// `#include <cuda.h>` sites (28 of them) resolve to HIP without per-file edits.
// A no-op passthrough on a real CUDA build (this dir is only on the HIP path).
#ifndef PXG_HIP_COMPAT_CUDA_H
#define PXG_HIP_COMPAT_CUDA_H
#include "cuda_to_hip.h"
#endif
