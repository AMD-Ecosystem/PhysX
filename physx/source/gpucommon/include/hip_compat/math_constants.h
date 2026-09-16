// Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// PhysX GPU HIP/ROCm compatibility shim: CUDA <math_constants.h> peers.
#ifndef PXG_HIP_COMPAT_MATH_CONSTANTS_H
#define PXG_HIP_COMPAT_MATH_CONSTANTS_H
#include <cfloat>
#ifndef CUDART_INF_F
#define CUDART_INF_F     __int_as_float(0x7f800000)
#endif
#ifndef CUDART_NAN_F
#define CUDART_NAN_F     __int_as_float(0x7fffffff)
#endif
#ifndef CUDART_INF
#define CUDART_INF       __longlong_as_double(0x7ff0000000000000ULL)
#endif
#ifndef CUDART_PI_F
#define CUDART_PI_F      3.141592654f
#endif
#endif
