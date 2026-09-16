// Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause
// PhysX GPU HIP/ROCm compatibility shim: maps CUDA's <texture_types.h> onto the
// HIP texture types. Placed on the HIP build's include path so PhysX's existing
// `#include <texture_types.h>` sites resolve to HIP without per-file edits.
// A no-op passthrough on a real CUDA build (this dir is only on the HIP path).
#ifndef PXG_HIP_COMPAT_TEXTURE_TYPES_H
#define PXG_HIP_COMPAT_TEXTURE_TYPES_H
#include "cuda_to_hip.h"
#include <hip/hip_texture_types.h>
#endif
