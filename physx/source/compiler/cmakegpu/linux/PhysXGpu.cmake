## Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions
## are met:
##  * Redistributions of source code must retain the above copyright
##    notice, this list of conditions and the following disclaimer.
##  * Redistributions in binary form must reproduce the above copyright
##    notice, this list of conditions and the following disclaimer in the
##    documentation and/or other materials provided with the distribution.
##  * Neither the name of NVIDIA CORPORATION nor the names of its
##    contributors may be used to endorse or promote products derived
##    from this software without specific prior written permission.
##
## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
## EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
## IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
## PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
## CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
## EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
## PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
## PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
## OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
## (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
## OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
##
## Copyright (c) 2008-2026 NVIDIA Corporation. All rights reserved.
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
IF(PX_USE_HIP AND PX_THEROCK)
	# --- Canonical TheRock resolution: link the imported hip::host target -------
	# find_package(hip) (done in the platform CMakeLists) provides hip::host, whose
	# INTERFACE carries the amdhip64 link + the ROCm include dirs and RPATH. No
	# GLOB / FIND_LIBRARY of libamdhip64.so.* is needed; the super-project resolves
	# it. dl is still needed by the __hipRegisterFatBinary interceptors in
	# CudaKernelWrangler.cpp.
	LIST(APPEND PHYSXGPU_PRIVATE_PLATFORM_LINKED_LIBS ${PX_HIP_RUNTIME_TARGET} dl)
ELSEIF(PX_USE_HIP)
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
ENDIF(PX_USE_HIP)
