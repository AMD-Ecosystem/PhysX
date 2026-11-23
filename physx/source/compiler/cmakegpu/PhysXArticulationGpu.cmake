## SPDX-FileCopyrightText: Copyright (c) 2008-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0
## Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.

#
# Build PhysXArticulationGpu common
#

SET(PHYSX_SOURCE_DIR ${PHYSX_ROOT_DIR}/source)
SET(GPUARTICULATION_SOURCE_DIR ${PHYSX_SOURCE_DIR}/gpuarticulation/src)

# Include here after the directories are defined so that the platform specific file can use the variables.
include(${PHYSX_ROOT_DIR}/${PROJECT_CMAKE_FILES_DIR}/${TARGET_BUILD_PLATFORM}/PhysXArticulationGpu.cmake)


# setup grouping
# articulation
SET(PHYXGPU_ARTICULATION_HEADERS
	${PHYSX_SOURCE_DIR}/gpuarticulation/include/PxgArticulationCore.h
	${PHYSX_SOURCE_DIR}/gpuarticulation/include/PxgArticulationCoreDesc.h
	${PHYSX_SOURCE_DIR}/gpuarticulation/include/PxgArticulationCoreKernelIndices.h
)
SOURCE_GROUP("articulation include" FILES ${PHYXGPU_ARTICULATION_HEADERS})

SET(PHYXGPU_ARTICULATION_CUDA_KERNELS
	${GPUARTICULATION_SOURCE_DIR}/CUDA/articulationDirectGpuApi.cu
	${GPUARTICULATION_SOURCE_DIR}/CUDA/forwardDynamic2.cu
	${GPUARTICULATION_SOURCE_DIR}/CUDA/internalConstraints2.cu
	${GPUARTICULATION_SOURCE_DIR}/CUDA/inverseDynamic.cu
)
SOURCE_GROUP("articulation kernels/CUDA" FILES ${PHYXGPU_ARTICULATION_CUDA_KERNELS})

SET(PHYXGPU_ARTICULATION_CUDA_INCLUDE
	${GPUARTICULATION_SOURCE_DIR}/CUDA/articulationDynamic.cuh
	${GPUARTICULATION_SOURCE_DIR}/CUDA/articulationImpulseResponse.cuh
)
SOURCE_GROUP("articulation kernels cuda include" FILES ${PHYXGPU_ARTICULATION_CUDA_INCLUDE})

SET(PHYXGPU_ARTICULATION_SOURCE
	${GPUARTICULATION_SOURCE_DIR}/PxgArticulationCore.cpp
)
SOURCE_GROUP("articulation src" FILES ${PHYXGPU_ARTICULATION_SOURCE})

ADD_LIBRARY(PhysXArticulationGpu ${PHYSXARTICULATIONGPU_LIBTYPE}
	# ARTICULATION
	${PHYXGPU_ARTICULATION_HEADERS}
	${PHYXGPU_ARTICULATION_CUDA_KERNELS}
	${PHYXGPU_ARTICULATION_CUDA_INCLUDE}
	${PHYXGPU_ARTICULATION_SOURCE}
)

TARGET_INCLUDE_DIRECTORIES(PhysXArticulationGpu
	PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
	PRIVATE ${PHYSX_ROOT_DIR}/include
	PRIVATE ${PHYSX_SOURCE_DIR}/common/include
	PRIVATE ${PHYSX_SOURCE_DIR}/common/src
	PRIVATE ${PHYSX_SOURCE_DIR}/physxgpu/include
	PRIVATE ${PHYSX_SOURCE_DIR}/geomutils/include
	PRIVATE ${PHYSX_SOURCE_DIR}/geomutils/src
	PRIVATE ${PHYSX_SOURCE_DIR}/geomutils/src/contact
	PRIVATE ${PHYSX_SOURCE_DIR}/geomutils/src/pcm
	PRIVATE ${PHYSX_SOURCE_DIR}/geomutils/src/mesh
	PRIVATE ${PHYSX_SOURCE_DIR}/lowlevel/api/include
	PRIVATE ${PHYSX_SOURCE_DIR}/lowlevel/api/include/windows
	PRIVATE ${PHYSX_SOURCE_DIR}/lowlevel/common/include/pipeline
	PRIVATE ${PHYSX_SOURCE_DIR}/lowlevel/common/include/utils
	PRIVATE ${PHYSX_SOURCE_DIR}/lowlevelaabb/include
	PRIVATE ${PHYSX_SOURCE_DIR}/lowleveldynamics/include
	PRIVATE ${PHYSX_SOURCE_DIR}/lowleveldynamics/shared
	PRIVATE ${PHYSX_SOURCE_DIR}/lowleveldynamics/src
	PRIVATE ${PHYSX_SOURCE_DIR}/lowlevel/software/include
	PRIVATE ${PHYSX_SOURCE_DIR}/gpusolver/include
	PRIVATE ${PHYSX_SOURCE_DIR}/gpusolver/src/CUDA
	PRIVATE ${PHYSX_SOURCE_DIR}/gpubroadphase/include
	PRIVATE ${PHYSX_SOURCE_DIR}/gpunarrowphase/include
	PRIVATE ${PHYSX_SOURCE_DIR}/gpucommon/include
	PRIVATE ${PHYSX_SOURCE_DIR}/gpucommon/src/CUDA
	PRIVATE ${PHYSX_SOURCE_DIR}/gpusimulationcontroller/include
	PRIVATE ${PHYSX_SOURCE_DIR}/gpusimulationcontroller/src/CUDA
	PRIVATE ${PHYSX_SOURCE_DIR}/gpuarticulation/include
	PRIVATE ${PHYSX_SOURCE_DIR}/cudamanager/include
)

SET_TARGET_PROPERTIES(PhysXArticulationGpu PROPERTIES
	OUTPUT_NAME PhysXArticulationGpu
)


IF(PHYSXARTICULATIONGPU_LIBTYPE STREQUAL "STATIC")
	SET_TARGET_PROPERTIES(PhysXArticulationGpu PROPERTIES
		ARCHIVE_OUTPUT_NAME_DEBUG "PhysXArticulationGpu_static"
		ARCHIVE_OUTPUT_NAME_CHECKED "PhysXArticulationGpu_static"
		ARCHIVE_OUTPUT_NAME_PROFILE "PhysXArticulationGpu_static"
		ARCHIVE_OUTPUT_NAME_RELEASE "PhysXArticulationGpu_static"
	)
ENDIF()

IF(PHYSXARTICULATIONGPU_COMPILE_PDB_NAME_DEBUG)
	SET_TARGET_PROPERTIES(PhysXArticulationGpu PROPERTIES
		COMPILE_PDB_NAME_DEBUG "${PHYSXARTICULATIONGPU_COMPILE_PDB_NAME_DEBUG}"
		COMPILE_PDB_NAME_CHECKED "${PHYSXARTICULATIONGPU_COMPILE_PDB_NAME_CHECKED}"
		COMPILE_PDB_NAME_PROFILE "${PHYSXARTICULATIONGPU_COMPILE_PDB_NAME_PROFILE}"
		COMPILE_PDB_NAME_RELEASE "${PHYSXARTICULATIONGPU_COMPILE_PDB_NAME_RELEASE}"
	)
ENDIF()


TARGET_COMPILE_DEFINITIONS(PhysXArticulationGpu
	PRIVATE ${PHYSXARTICULATIONGPU_COMPILE_DEFS}
)

# Since we are setting the C++ standard explicitly for Linux
# we need to do this for CUDA as well.
IF(TARGET_BUILD_PLATFORM STREQUAL "linux" AND NOT PX_USE_HIP)
	TARGET_COMPILE_FEATURES(PhysXArticulationGpu PRIVATE cuda_std_11)
ENDIF()

TARGET_COMPILE_OPTIONS(PhysXArticulationGpu PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:${ARCH_CODE_LIST}>)

# ROCm/HIP build: retarget .cu kernel TUs to the HIP front-end.
IF(PX_USE_HIP)
	SET_SOURCE_FILES_PROPERTIES(${PHYXGPU_ARTICULATION_CUDA_KERNELS} PROPERTIES LANGUAGE HIP)
	TARGET_COMPILE_OPTIONS(PhysXArticulationGpu PRIVATE
		$<$<COMPILE_LANGUAGE:HIP>:
			--offload-arch=${CMAKE_HIP_ARCHITECTURES}
			-ffp-contract=on
			# -fgpu-rdc removed: PhysX loads each kernel as an independent runtime
			# module (driver-API), so no cross-TU device link is needed; non-RDC
			# matches the CUDA per-.cu fatbin model and avoids the shared-lib HIP
			# device-link seam (undefined __hip_gpubin_handle_*).
			-include cuda_to_hip.h
			-Wno-unused-function
			-Wno-inconsistent-missing-override>)
	TARGET_INCLUDE_DIRECTORIES(PhysXArticulationGpu PRIVATE
		${PHYSX_SOURCE_DIR}/gpucommon/include/hip_compat
		${PHYSX_SOURCE_DIR}/gpucommon/include
		${PX_HIP_INCLUDE_DIR})
	# Host seam .cpp files: compile as plain C++ but expose HIP platform macro
	TARGET_COMPILE_OPTIONS(PhysXArticulationGpu PRIVATE
		$<$<COMPILE_LANGUAGE:CXX>:-D__HIP_PLATFORM_AMD__ -include cuda_to_hip.h>)
ENDIF(PX_USE_HIP)

IF(PX_GENERATE_SOURCE_DISTRO)
	LIST(APPEND SOURCE_DISTRO_FILE_LIST ${PHYXGPU_ARTICULATION_HEADERS})
	LIST(APPEND SOURCE_DISTRO_FILE_LIST ${PHYXGPU_ARTICULATION_SOURCE})
	LIST(APPEND SOURCE_DISTRO_FILE_LIST ${PHYXGPU_ARTICULATION_CUDA_INCLUDE})
	LIST(APPEND SOURCE_DISTRO_FILE_LIST ${PHYXGPU_ARTICULATION_CUDA_KERNELS})
ENDIF()

# enable -fPIC so we can link static libs with the editor
SET_TARGET_PROPERTIES(PhysXArticulationGpu PROPERTIES
	POSITION_INDEPENDENT_CODE TRUE
)
