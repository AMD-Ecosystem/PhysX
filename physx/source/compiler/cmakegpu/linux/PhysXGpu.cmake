## SPDX-FileCopyrightText: Copyright (c) 2008-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0
## Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.

#
# Build PhysXGpu
#

IF(PX_GENERATE_GPU_STATIC_LIBRARIES)
	SET(PHYSXGPU_LIBTYPE STATIC)
	SET(PXPHYSXGPU_LIBTYPE_DEFS 
		PX_PHYSX_GPU_STATIC;PX_PHYSX_STATIC_LIB;
	)
ELSE()
	SET(PHYSXGPU_LIBTYPE SHARED)
	SET(PXPHYSXGPU_LIBTYPE_DEFS 
		PX_PHYSX_GPU_EXPORTS;
	)
ENDIF()

# Use generator expressions to set config specific preprocessor definitions
SET(PHYSXGPU_COMPILE_DEFS

	# Common to all configurations
	${PHYSX_LINUX_COMPILE_DEFS};${PXPHYSXGPU_LIBTYPE_DEFS};

	$<$<CONFIG:debug>:${PHYSX_LINUX_DEBUG_COMPILE_DEFS};>
	$<$<CONFIG:checked>:${PHYSX_LINUX_CHECKED_COMPILE_DEFS};>
	$<$<CONFIG:profile>:${PHYSX_LINUX_PROFILE_COMPILE_DEFS};>
	$<$<CONFIG:release>:${PHYSX_LINUX_RELEASE_COMPILE_DEFS};>
)

SET(PHYSXGPU_LINK_FLAGS " ")
SET(PHYSXGPU_LINK_FLAGS_DEBUG " ")
SET(PHYSXGPU_LINK_FLAGS_CHECKED " ")
SET(PHYSXGPU_LINK_FLAGS_PROFILE " ")
SET(PHYSXGPU_LINK_FLAGS_RELEASE " ")

IF(PX_GENERATE_GPU_STATIC_LIBRARIES)
	SET(PHYSXGPU_PLATFORM_OBJECT_FILES
		$<TARGET_OBJECTS:PhysXBroadphaseGpu>
		$<TARGET_OBJECTS:PhysXNarrowphaseGpu>
		$<TARGET_OBJECTS:PhysXSimulationControllerGpu>
		$<TARGET_OBJECTS:PhysXSolverGpu>
		$<TARGET_OBJECTS:PhysXCommonGpu>
		$<TARGET_OBJECTS:PhysXCudaContextManager>
		$<TARGET_OBJECTS:PhysXArticulationGpu>
	)
ELSE()
	SET(PHYSXGPU_PRIVATE_PLATFORM_LINKED_LIBS
		PhysXBroadphaseGpu
		PhysXNarrowphaseGpu
		PhysXSimulationControllerGpu
		PhysXSolverGpu
		PhysXCommonGpu
		PhysXCudaContextManager
		PhysXArticulationGpu
	)
ENDIF()

SET(PHYSXGPU_PLATFORM_SOURCES
	${PHYSXGPU_PLATFORM_OBJECT_FILES}
)

# ROCm/HIP: link amdhip64 (the HIP runtime, which is also the GPU driver on ROCm).
IF(PX_USE_HIP)
	# Derive the ROCm lib dir from CMAKE_HIP_COMPILER:
	#   amdclang++ lives in <rocm>/lib/llvm/bin; amdhip64 lives in <rocm>/lib.
	# The pip venv layout ships libamdhip64.so.7 (versioned, NO unversioned symlink).
	# FIND_LIBRARY cannot match versioned sonames so we use GLOB to find it.
	GET_FILENAME_COMPONENT(_HIP_LIB_DIR "${CMAKE_HIP_COMPILER}" DIRECTORY)
	GET_FILENAME_COMPONENT(_ROCM_LIB_DIR "${_HIP_LIB_DIR}/../../.." ABSOLUTE)
	FILE(GLOB _AMDHIP64_CANDIDATES
		"${_ROCM_LIB_DIR}/lib/libamdhip64.so"
		"${_ROCM_LIB_DIR}/lib/libamdhip64.so.*"
		"${_ROCM_LIB_DIR}/lib64/libamdhip64.so"
		"${_ROCM_LIB_DIR}/lib64/libamdhip64.so.*")
	IF(_AMDHIP64_CANDIDATES)
		LIST(GET _AMDHIP64_CANDIDATES 0 AMDHIP64_LIB)
		MESSAGE(STATUS "PX_USE_HIP: linking amdhip64 from ${AMDHIP64_LIB}")
		# Record the amdhip64 dir so the top-level PhysXGpu.cmake can set the
		# runtime RPATH AFTER the PhysXGpu target is created (this file is
		# included BEFORE ADD_LIBRARY(PhysXGpu), so the target does not exist yet).
		GET_FILENAME_COMPONENT(_AMDHIP64_DIR "${AMDHIP64_LIB}" DIRECTORY)
		SET(PX_HIP_RUNTIME_RPATH_DIR "${_AMDHIP64_DIR}" CACHE INTERNAL "amdhip64 dir for PhysXGpu RPATH")
	ELSE()
		# Standard /opt/rocm install: symlink should exist
		FIND_LIBRARY(AMDHIP64_LIB NAMES amdhip64)
		IF(AMDHIP64_LIB)
			MESSAGE(STATUS "PX_USE_HIP: linking amdhip64 from ${AMDHIP64_LIB}")
		ELSE()
			MESSAGE(WARNING "PX_USE_HIP: amdhip64 not found anywhere; linking -lamdhip64 by name")
			SET(AMDHIP64_LIB amdhip64)
		ENDIF()
	ENDIF()
	# Append amdhip64 to the platform linked libs so PhysXGpu.cmake picks it up.
	LIST(APPEND PHYSXGPU_PRIVATE_PLATFORM_LINKED_LIBS ${AMDHIP64_LIB})
	# dl (for dlsym(RTLD_NEXT)) used by the __hipRegisterFatBinary interceptors
	# in CudaKernelWrangler.cpp to forward to the real HIP runtime.
	LIST(APPEND PHYSXGPU_PRIVATE_PLATFORM_LINKED_LIBS dl)
ENDIF()
