## SPDX-FileCopyrightText: Copyright (c) 2008-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0
## Modifications Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.

# ---------------------------------------------------------------------------
# ROCm/HIP build option (PX_USE_HIP).
#
# When ON, the PhysX GPU stack targets AMD ROCm/HIP instead of NVIDIA CUDA.
# The GPU compilation model (validated against public ROCm 10.0.0 on gfx942):
#
# * DEVICE kernels (.cu) compile with the HIP language front-end:
# enable_language(HIP)
# set_source_files_properties(${CU_SOURCES} PROPERTIES LANGUAGE HIP)
# target_compile_options(<tgt> PRIVATE
# $<$<COMPILE_LANGUAGE:HIP>:--offload-arch=${CMAKE_HIP_ARCHITECTURES}
# -ffp-contract=on>)
# NOTE: -fgpu-rdc is intentionally OMITTED. PhysX uses per-TU independent
# driver-API fatbins (no cross-TU device linking). -fgpu-rdc requires an
# explicit device-link step (clang++ --hip-link) that CMake's HIP shared-lib
# link does not run, and enabling it masks latent invalid PTX until the
# deferred codegen step -- two independent defects. Leave it off.
# with the compat shim force-included on every GPU TU:
# target_compile_options(<tgt> PRIVATE -include cuda_to_hip.h)
# target_include_directories(<tgt> PRIVATE
# ${PHYSX_ROOT_DIR}/source/gpucommon/include/hip_compat
# ${PHYSX_ROOT_DIR}/source/gpucommon/include)
#
# * HOST seam files (CudaContextManager.cpp, CudaKernelWrangler.cpp,
# PxgGeometryManager.cpp, ...) compile as PLAIN C++ (NOT -x hip) with
# -D__HIP_PLATFORM_AMD__ and the HIP include path, and link -lamdhip64.
# IMPORTANT: compiling these .cpp with -x hip would define __HIPCC__ and
# flip PX_CUDA_COMPILER=1, which makes shared headers treat them as device
# compilation and hide host-only class bodies (KernelWrangler). They must
# go through the host compiler, exactly as they do under the CUDA build.
#
# * amdhip64 replaces the CUDA driver/runtime libs; -lamdhip64 links the HIP
# runtime (which IS the driver on ROCm).
# ---------------------------------------------------------------------------
# PX_USE_HIP is declared in the parent CMakeLists.txt (cmakegpu/CMakeLists.txt)
# before project() so that the right GPU language is selected there.
# Here we complete the HIP setup: locate the compiler and call enable_language(HIP).
IF(PX_USE_HIP)
	# ---------------------------------------------------------------------------
	# TheRock super-project detection.
	#
	# When PhysX is built as a component of the ROCm super-project TheRock
	# (github.com/ROCm/TheRock), the AMD clang toolchain and a resolvable ROCm
	# CMake package (hip-lang / hip::host) are ALREADY supplied to this configure
	# via the injected _toolchain.cmake + _init.cmake dependency provider. In that
	# case NONE of the venv-specific compiler/bitcode probing below is needed (or
	# correct): enable_language(HIP) finds the compiler through the toolchain, and
	# find_package(hip) resolves the runtime + include dirs canonically. We detect
	# the super-project either by an explicit -DPX_THEROCK=ON, by the standard
	# THEROCK_AMDGPU_* arch vars, or by a caller-supplied CMAKE_HIP_COMPILER (the
	# TheRock toolchain sets this). This keeps the standalone pip-venv build path
	# (the ELSE branch) working UNCHANGED while letting TheRock drive the build the
	# canonical way.
	# ---------------------------------------------------------------------------
	OPTION(PX_THEROCK "PhysX is being built inside the ROCm TheRock super-project (skip venv-specific HIP discovery, resolve via find_package(hip))" OFF)
	IF(DEFINED THEROCK_AMDGPU_FAMILIES OR DEFINED THEROCK_AMDGPU_TARGETS OR DEFINED ENV{THEROCK_BINARY_DIR})
		SET(PX_THEROCK ON)
	ENDIF()

	IF(PX_THEROCK)
		# --- Canonical TheRock / ROCm-CMake resolution path ---------------------
		# The toolchain already selected the AMD clang compiler; just enable HIP.
		# Arch comes from CMAKE_HIP_ARCHITECTURES, which TheRock populates from
		# THEROCK_AMDGPU_FAMILIES/TARGETS -- do NOT pin gfx here.
		ENABLE_LANGUAGE(HIP)
		# Resolve the HIP runtime + include dirs the canonical way. TheRock's
		# _init.cmake rewrites this find_package to the super-project-supplied
		# package; hip::host carries the include dirs the host-seam .cpp need.
		FIND_PACKAGE(hip CONFIG REQUIRED)
		SET(PX_HIP_RUNTIME_TARGET hip::host CACHE INTERNAL "canonical HIP runtime target under TheRock")
		# host-seam .cpp pull HIP headers transitively through hip::host; leave
		# PX_HIP_INCLUDE_DIR empty (the include dir is on the imported target).
		SET(PX_HIP_INCLUDE_DIR "" CACHE INTERNAL "ROCm include dir (empty under TheRock; carried by hip::host)")
		MESSAGE(STATUS "PX_USE_HIP: TheRock super-project build; HIP language enabled, arch=${CMAKE_HIP_ARCHITECTURES}, runtime target=hip::host")

		# Provide defaults for the PhysX compile-def cache vars for a standalone
		# GPU-only configure (mirrors the ELSE branch below).
		SET(ARCH_CODE_LIST "" CACHE INTERNAL "ARCH_CODE_LIST (empty for HIP builds)")
		IF(NOT DEFINED PHYSX_LINUX_COMPILE_DEFS)
			SET(PHYSX_LINUX_COMPILE_DEFS "" CACHE INTERNAL "Base PhysX preprocessor definitions (HIP standalone)")
		ENDIF()
		IF(NOT DEFINED PHYSX_LINUX_DEBUG_COMPILE_DEFS)
			SET(PHYSX_LINUX_DEBUG_COMPILE_DEFS "PX_DEBUG=1;PX_CHECKED=1;PX_SUPPORT_PVD=1" CACHE INTERNAL "Debug PhysX preprocessor definitions")
		ENDIF()
		IF(NOT DEFINED PHYSX_LINUX_CHECKED_COMPILE_DEFS)
			SET(PHYSX_LINUX_CHECKED_COMPILE_DEFS "NDEBUG;PX_CHECKED=1;PX_SUPPORT_PVD=1" CACHE INTERNAL "Checked PhysX preprocessor definitions")
		ENDIF()
		IF(NOT DEFINED PHYSX_LINUX_PROFILE_COMPILE_DEFS)
			SET(PHYSX_LINUX_PROFILE_COMPILE_DEFS "NDEBUG;PX_PROFILE=1;PX_SUPPORT_PVD=1" CACHE INTERNAL "Profile PhysX preprocessor definitions")
		ENDIF()
		IF(NOT DEFINED PHYSX_LINUX_RELEASE_COMPILE_DEFS)
			SET(PHYSX_LINUX_RELEASE_COMPILE_DEFS "NDEBUG;PX_SUPPORT_PVD=0;PX_SUPPORT_OMNI_PVD=0" CACHE INTERNAL "Release PhysX preprocessor definitions")
		ENDIF()

	ELSE()
	# ---------------------------------------------------------------------------
	# Standalone pip-venv HIP compiler discovery (UNCHANGED).
	# Prefer an explicit -DCMAKE_HIP_COMPILER=<path>; fall back to the venv
	# ROCm SDK (_rocm_sdk_core) or a ROCM_PATH environment variable.
	# DO NOT call find_package(HIP) here -- the venv SDK has no HIPConfig.cmake.
	# ---------------------------------------------------------------------------
	IF(NOT DEFINED CMAKE_HIP_COMPILER)
		# Try ROCM_PATH env first
		IF(DEFINED ENV{ROCM_PATH})
			SET(_HIP_COMPILER_CANDIDATE "$ENV{ROCM_PATH}/lib/llvm/bin/amdclang++")
		ENDIF()
		# Try to find amdclang++ next to the active Python interpreter (venv pattern)
		IF(NOT EXISTS "${_HIP_COMPILER_CANDIDATE}")
			FIND_PROGRAM(_PYTHON_BIN python3 python HINTS "${CMAKE_BINARY_DIR}" NO_DEFAULT_PATH)
			IF(_PYTHON_BIN)
				EXECUTE_PROCESS(
					COMMAND ${_PYTHON_BIN} -c
						"import _rocm_sdk_core, os; print(os.path.join(os.path.dirname(_rocm_sdk_core.__file__), 'lib', 'llvm', 'bin', 'amdclang++'))"
					OUTPUT_VARIABLE _HIP_COMPILER_CANDIDATE
					OUTPUT_STRIP_TRAILING_WHITESPACE
					ERROR_QUIET)
			ENDIF()
		ENDIF()
		IF(EXISTS "${_HIP_COMPILER_CANDIDATE}")
			SET(CMAKE_HIP_COMPILER "${_HIP_COMPILER_CANDIDATE}" CACHE FILEPATH "HIP compiler (amdclang++)")
			MESSAGE(STATUS "PX_USE_HIP: auto-discovered HIP compiler: ${CMAKE_HIP_COMPILER}")
		ELSE()
			MESSAGE(FATAL_ERROR
				"PX_USE_HIP=ON but could not find amdclang++.\n"
				"Pass -DCMAKE_HIP_COMPILER=<path/to/amdclang++> or set ROCM_PATH.")
		ENDIF()
	ELSE()
		MESSAGE(STATUS "PX_USE_HIP: using caller-supplied CMAKE_HIP_COMPILER=${CMAKE_HIP_COMPILER}")
	ENDIF()

	# Derive the ROCm root from the HIP compiler path so the compiler can find its
	# device libraries. amdclang++ lives at <ROCM>/lib/llvm/bin/amdclang++, so
	# walking up three levels gives <ROCM>. This must be set BEFORE enable_language(HIP)
	# so that CMake's own HIP compiler probe passes --rocm-path correctly.
	GET_FILENAME_COMPONENT(_PX_DETECT_HIP_BIN "${CMAKE_HIP_COMPILER}" DIRECTORY) # .../lib/llvm/bin
	GET_FILENAME_COMPONENT(_PX_DETECT_HIP_LLVM "${_PX_DETECT_HIP_BIN}" DIRECTORY) # .../lib/llvm
	GET_FILENAME_COMPONENT(_PX_DETECT_HIP_LIB "${_PX_DETECT_HIP_LLVM}" DIRECTORY) # .../lib
	GET_FILENAME_COMPONENT(_PX_DETECT_ROCM_ROOT "${_PX_DETECT_HIP_LIB}" DIRECTORY) # .../<ROCM>
	# The venv ROCm SDK stores device bitcode at <ROCM>/lib/llvm/amdgcn/bitcode/
	# rather than the standard <ROCM>/amdgcn/bitcode/, so --rocm-path alone does not
	# work. Use --rocm-device-lib-path pointing directly at the bitcode dir
	# (and also --rocm-path for any other discovery the compiler needs).
	SET(_PX_DETECT_BITCODE_STD "${_PX_DETECT_ROCM_ROOT}/amdgcn/bitcode")
	SET(_PX_DETECT_BITCODE_VENV "${_PX_DETECT_HIP_LLVM}/amdgcn/bitcode")
	IF(EXISTS "${_PX_DETECT_BITCODE_STD}/opencl.bc")
		SET(_PX_DETECT_BITCODE_DIR "${_PX_DETECT_BITCODE_STD}")
	ELSEIF(EXISTS "${_PX_DETECT_BITCODE_VENV}/opencl.bc")
		SET(_PX_DETECT_BITCODE_DIR "${_PX_DETECT_BITCODE_VENV}")
	ELSE()
		# Fallback: let cmake find it; --rocm-path alone is the best we can do
		UNSET(_PX_DETECT_BITCODE_DIR)
	ENDIF()
	IF(DEFINED _PX_DETECT_BITCODE_DIR)
		SET(_PX_HIP_FLAGS_INIT "--rocm-path=${_PX_DETECT_ROCM_ROOT} --rocm-device-lib-path=${_PX_DETECT_BITCODE_DIR}")
	ELSE()
		SET(_PX_HIP_FLAGS_INIT "--rocm-path=${_PX_DETECT_ROCM_ROOT}")
	ENDIF()
	IF(NOT DEFINED CMAKE_HIP_FLAGS_INIT OR CMAKE_HIP_FLAGS_INIT STREQUAL "")
		SET(CMAKE_HIP_FLAGS_INIT "${_PX_HIP_FLAGS_INIT}" CACHE INTERNAL "Initial HIP flags with ROCm device library path")
	ENDIF()
	SET(CMAKE_HIP_FLAGS "${CMAKE_HIP_FLAGS_INIT}" CACHE STRING "HIP compiler flags" FORCE)
	MESSAGE(STATUS "PX_USE_HIP: ROCm root = ${_PX_DETECT_ROCM_ROOT}, bitcode = ${_PX_DETECT_BITCODE_DIR}")
	# Also export ROCM_PATH so hipconfig and rocm_agent_enumerator can find libs
	SET(ENV{ROCM_PATH} "${_PX_DETECT_ROCM_ROOT}")
	SET(ENV{HIP_PATH} "${_PX_DETECT_ROCM_ROOT}")

	# Force -x hip so that amdclang++ compiles .cu files as HIP (not CUDA).
	# CMake 4.3's unknown-compiler-id path omits -x hip from the compile rule;
	# the venv amdclang++ is not identified by CMake (no HIPConfig.cmake), so
	# CMAKE_HIP_COMPILER_ID stays empty and CMake falls back to the generic rule
	# that does not include the language flag. The per-source LANGUAGE HIP
	# property tells CMake to USE the HIP compiler but doesn't automatically add
	# -x hip; the extension .cu is not in CMAKE_HIP_SOURCE_FILE_EXTENSIONS, so
	# amdclang++ defaults to CUDA mode and rejects gfx942 as a CUDA arch.
	# Setting CMAKE_HIP_COMPILE_OBJECT here overrides the generated rule to force
	# -x hip before any target is defined.
	IF(NOT CMAKE_HIP_COMPILE_OBJECT)
		SET(CMAKE_HIP_COMPILE_OBJECT
			"<CMAKE_HIP_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -x hip -o <OBJECT> -c <SOURCE>"
			CACHE STRING "HIP compile object rule (force -x hip for .cu files)")
	ENDIF()

	# Enable the HIP language (must happen before any target is created).
	ENABLE_LANGUAGE(HIP)
	MESSAGE(STATUS "PX_USE_HIP: HIP language enabled, arch=${CMAKE_HIP_ARCHITECTURES}")

	# ---------------------------------------------------------------------------
	# ROCm include directory for the HOST-seam .cpp files.
	# The .cu TUs (LANGUAGE HIP) automatically receive the ROCm headers via the
	# hip-lang CMake package, but the host-seam .cpp files compile as plain CXX
	# (deliberately -- see the header comment above; -x hip would flip
	# PX_CUDA_COMPILER=1 and hide host-only class bodies). Those .cpp still pull
	# <hip/hip_runtime.h> through cuda_to_hip.h / the vector_types shim, so they
	# need the ROCm include path added explicitly. Derive it from the HIP
	# compiler location: <ROCM>/lib/llvm/bin/amdclang++ -> <ROCM>/include.
	# CMAKE_HIP_COMPILER_ROCM_ROOT (set by enable_language(HIP)) is authoritative
	# when available; otherwise walk up from the compiler path.
	IF(DEFINED CMAKE_HIP_COMPILER_ROCM_ROOT AND EXISTS "${CMAKE_HIP_COMPILER_ROCM_ROOT}/include/hip/hip_runtime.h")
		SET(PX_HIP_INCLUDE_DIR "${CMAKE_HIP_COMPILER_ROCM_ROOT}/include" CACHE INTERNAL "ROCm include dir for host-seam .cpp")
	ELSE()
		GET_FILENAME_COMPONENT(_PX_HIP_BIN_DIR "${CMAKE_HIP_COMPILER}" DIRECTORY) # .../lib/llvm/bin
		GET_FILENAME_COMPONENT(_PX_HIP_LLVM_DIR "${_PX_HIP_BIN_DIR}" DIRECTORY) # .../lib/llvm
		GET_FILENAME_COMPONENT(_PX_HIP_LIB_DIR "${_PX_HIP_LLVM_DIR}" DIRECTORY) # .../lib
		GET_FILENAME_COMPONENT(_PX_HIP_ROOT "${_PX_HIP_LIB_DIR}" DIRECTORY) # .../<ROCM>
		SET(PX_HIP_INCLUDE_DIR "${_PX_HIP_ROOT}/include" CACHE INTERNAL "ROCm include dir for host-seam .cpp")
	ENDIF()
	IF(NOT EXISTS "${PX_HIP_INCLUDE_DIR}/hip/hip_runtime.h")
		MESSAGE(WARNING "PX_USE_HIP: could not locate hip/hip_runtime.h under ${PX_HIP_INCLUDE_DIR}; host-seam .cpp may fail to find HIP headers. Pass -DPX_HIP_INCLUDE_DIR=<ROCm>/include.")
	ELSE()
		MESSAGE(STATUS "PX_USE_HIP: host-seam ROCm include dir = ${PX_HIP_INCLUDE_DIR}")
	ENDIF()

	# Suppress CUDA-only compile features by recording an empty CUDA arch list.
	# This prevents the cuda_std_11 feature request from failing when CUDA language
	# is not enabled. The per-module .cmake files guard those calls with PX_USE_HIP.
	SET(ARCH_CODE_LIST "" CACHE INTERNAL "ARCH_CODE_LIST (empty for HIP builds)")

	# Provide defaults for PHYSX_LINUX_COMPILE_DEFS if this is a standalone GPU
	# build (not preceded by the CPU cmake path that normally sets these).
	IF(NOT DEFINED PHYSX_LINUX_COMPILE_DEFS)
		SET(PHYSX_LINUX_COMPILE_DEFS "" CACHE INTERNAL "Base PhysX preprocessor definitions (HIP standalone)")
	ENDIF()
	IF(NOT DEFINED PHYSX_LINUX_DEBUG_COMPILE_DEFS)
		SET(PHYSX_LINUX_DEBUG_COMPILE_DEFS "PX_DEBUG=1;PX_CHECKED=1;PX_SUPPORT_PVD=1" CACHE INTERNAL "Debug PhysX preprocessor definitions")
	ENDIF()
	IF(NOT DEFINED PHYSX_LINUX_CHECKED_COMPILE_DEFS)
		SET(PHYSX_LINUX_CHECKED_COMPILE_DEFS "NDEBUG;PX_CHECKED=1;PX_SUPPORT_PVD=1" CACHE INTERNAL "Checked PhysX preprocessor definitions")
	ENDIF()
	IF(NOT DEFINED PHYSX_LINUX_PROFILE_COMPILE_DEFS)
		SET(PHYSX_LINUX_PROFILE_COMPILE_DEFS "NDEBUG;PX_PROFILE=1;PX_SUPPORT_PVD=1" CACHE INTERNAL "Profile PhysX preprocessor definitions")
	ENDIF()
	IF(NOT DEFINED PHYSX_LINUX_RELEASE_COMPILE_DEFS)
		SET(PHYSX_LINUX_RELEASE_COMPILE_DEFS "NDEBUG;PX_SUPPORT_PVD=0;PX_SUPPORT_OMNI_PVD=0" CACHE INTERNAL "Release PhysX preprocessor definitions")
	ENDIF()

	ENDIF(PX_THEROCK) # end TheRock vs standalone-venv HIP setup split

ELSE()

IF(NOT DEFINED PHYSX_LINUX_COMPILE_DEFS)
	MESSAGE(FATAL_ERROR "PhysXGpu requires the PhysX compile defs, and they're not defined when they need to be.")
ENDIF()

IF (NOT DEFINED PHYSX_CXX_FLAGS)
	MESSAGE(FATAL_ERROR "PhysXGpu requires the PhysX CXX flags, and they're not defined when they need to be.")
ENDIF()

# Get the CXX Flags from the Cached variables set by the PhysX CMakeLists
SET(CMAKE_CXX_FLAGS "${PHYSX_CXX_FLAGS}")

SET(CMAKE_CXX_FLAGS_DEBUG   "${PHYSX_CXX_FLAGS_DEBUG}")
SET(CMAKE_CXX_FLAGS_CHECKED ${PHYSX_CXX_FLAGS_CHECKED})
SET(CMAKE_CXX_FLAGS_PROFILE ${PHYSX_CXX_FLAGS_PROFILE})
SET(CMAKE_CXX_FLAGS_RELEASE ${PHYSX_CXX_FLAGS_RELEASE})

# CUDA flags specific to Linux:
IF(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
	SET(MSSE2_OPTIONS "")
	# Add the diagnostic suppression flag specifically for aarch64
	SET(AARCH64_DIAG_SUPPRESS_FLAGS "-diag-suppress 20054")
ELSE()
	SET(MSSE2_OPTIONS "-msse2,-mfpmath=sse,-m64,")
	SET(AARCH64_DIAG_SUPPRESS_FLAGS "")
ENDIF()

# CUDA flags specific to linux:
SET(CUDA_SUPPRESS_WARNINGS "-Wno-unused-function -Wno-inconsistent-missing-override -Wno-parentheses -Wno-unknown-warning-option")

SET(PHYSX_CUDA_FLAGS "${PHYSX_CUDA_FLAGS} ${CUDA_SUPPRESS_WARNINGS} ${AARCH64_DIAG_SUPPRESS_FLAGS}")

SET(PHYSX_CUDA_FLAGS_DEBUG   "${CUDA_DEBUG_FLAG} --compiler-options=-Wall,-O3,-fPIC,${MSSE2_OPTIONS}-fvisibility=hidden ${PX_CUDA_DEBUG_DEVICE_FLAGS}" CACHE INTERNAL "PhysX Debug CUDA Flags")
SET(PHYSX_CUDA_FLAGS_CHECKED "${CUDA_NDEBUG_FLAG} -lineinfo --compiler-options=-Wall,-O3,-fPIC,${MSSE2_OPTIONS}-fvisibility=hidden" CACHE INTERNAL "PhysX Checked CUDA Flags")
SET(PHYSX_CUDA_FLAGS_PROFILE "${CUDA_NDEBUG_FLAG} -lineinfo --compiler-options=-Wall,-O3,-fPIC,${MSSE2_OPTIONS}-fvisibility=hidden" CACHE INTERNAL "PhysX Profile CUDA Flags")
SET(PHYSX_CUDA_FLAGS_RELEASE "${CUDA_NDEBUG_FLAG} -lineinfo --compiler-options=-Wall,-O3,-fPIC,${MSSE2_OPTIONS}-fvisibility=hidden" CACHE INTERNAL "PhysX Release CUDA Flags")

# These flags are local to the directory the CMakeLists.txt is in, so don't get carried over to OTHER CMakeLists.txt (thus the CACHE variables above)
SET(CMAKE_CUDA_FLAGS ${PHYSX_CUDA_FLAGS})

SET(CMAKE_CUDA_FLAGS_DEBUG   ${PHYSX_CUDA_FLAGS_DEBUG})
SET(CMAKE_CUDA_FLAGS_CHECKED ${PHYSX_CUDA_FLAGS_CHECKED})
SET(CMAKE_CUDA_FLAGS_PROFILE ${PHYSX_CUDA_FLAGS_PROFILE})
SET(CMAKE_CUDA_FLAGS_RELEASE ${PHYSX_CUDA_FLAGS_RELEASE})

ENDIF(PX_USE_HIP)


# SET(GENERATED_GPU_CUDA_FILES "")

# Include all of the projects
INCLUDE(PhysXBroadphaseGpu.cmake)
INCLUDE(PhysXCommonGpu.cmake)
INCLUDE(PhysXNarrowphaseGpu.cmake)
INCLUDE(PhysXSimulationControllerGpu.cmake)
INCLUDE(PhysXSolverGpu.cmake)
INCLUDE(PhysXCudaContextManager.cmake)
INCLUDE(PhysXArticulationGpu.cmake)
INCLUDE(PhysXGpuDependencies.cmake)
INCLUDE(PhysXGpu.cmake)	 # must be last

SET_PROPERTY(TARGET PhysXBroadphaseGpu PROPERTY FOLDER "PhysX SDK/GPU")
SET_PROPERTY(TARGET PhysXCommonGpu PROPERTY FOLDER "PhysX SDK/GPU")
SET_PROPERTY(TARGET PhysXNarrowphaseGpu PROPERTY FOLDER "PhysX SDK/GPU")
SET_PROPERTY(TARGET PhysXSimulationControllerGpu PROPERTY FOLDER "PhysX SDK/GPU")
SET_PROPERTY(TARGET PhysXArticulationGpu PROPERTY FOLDER "PhysX SDK/GPU")
SET_PROPERTY(TARGET PhysXSolverGpu PROPERTY FOLDER "PhysX SDK/GPU")
SET_PROPERTY(TARGET PhysXGpuDependencies PROPERTY FOLDER "PhysX SDK/GPU")
SET_PROPERTY(TARGET PhysXGpu PROPERTY FOLDER "PhysX SDK/GPU")
SET_PROPERTY(TARGET PhysXCudaContextManager PROPERTY FOLDER "PhysX SDK/GPU")	

SET(PHYSXDISTRO_LIBS PhysXGpu)

# Only install the legacy PhysXSDK export set in standalone mode (PX_ENABLE_INSTALL not defined).
# The FetchContent entry point (physx/CMakeLists.txt) uses its own PhysXTargets export set.
IF(NOT DEFINED PX_ENABLE_INSTALL)
	INSTALL(
		TARGETS ${PHYSXDISTRO_LIBS}
		EXPORT PhysXSDK
		DESTINATION $<$<CONFIG:debug>:${PX_ROOT_LIB_DIR}/debug>$<$<CONFIG:release>:${PX_ROOT_LIB_DIR}/release>$<$<CONFIG:checked>:${PX_ROOT_LIB_DIR}/checked>$<$<CONFIG:profile>:${PX_ROOT_LIB_DIR}/profile>
	)
ENDIF()
	
