# Building PhysX for AMD GPUs (ROCm)

The PhysX SDK's GPU simulation stack (`libPhysXGpu_64.so`) builds for AMD GPUs with HIP when the `PX_USE_HIP` CMake option is on. The CUDA and CPU-only builds are unchanged. Only the Linux GPU build is supported so far.

Tested with ROCm 10.2 on an AMD Instinct MI300X (gfx942) under Ubuntu 24.04: the `SnippetGpuParity` checks, the GPU snippets and the pvddom capture tests pass. CDNA (wave64) and RDNA (wave32) GPUs use the same code; RDNA builds compile but have not been tested yet.

## Prerequisites

- ROCm 10.2, either a system install in `/opt/rocm` or the ROCm Python packages (`pip install "rocm[libraries,devel]"` from the AMD package index for your GPU, then `rocm-sdk init`). CMake finds the compiler through `ROCM_PATH`, `rocm-sdk` on `PATH`, or `/opt/rocm`, in that order.
- CMake 3.21 or newer and the other packages listed in [the Linux platform readme](physx/documentation/platformreadme/linux/README_LINUX.md), including the snippet packages (freeglut, GLU, OpenGL headers).

## Build the SDK, the GPU library and the snippets

```bash
cd physx
./generate_projects.sh linux-gcc-rocm
make -C compiler/linux-gcc-rocm-release -j"$(nproc)"
```

The `linux-gcc-rocm` preset is `linux-gcc` with `PX_USE_HIP` on. It builds for the GPUs present in the machine. To choose the GPU architectures, configure the SDK directly instead:

```bash
cmake -S physx -B build -DPHYSX_PRESET=linux-gcc -DPX_USE_HIP=ON -DCMAKE_HIP_ARCHITECTURES="gfx942;gfx1100" -DCMAKE_BUILD_TYPE=release
cmake --build build -j"$(nproc)"
```

The GPU library alone can still be configured from `physx/source/compiler/cmakegpu`:

```bash
cmake -S physx/source/compiler/cmakegpu -B build-gpu -DPX_USE_HIP=ON -DTARGET_BUILD_PLATFORM=linux -DCMAKE_BUILD_TYPE=release -DCMAKE_HIP_ARCHITECTURES=gfx942 -DPHYSX_ROOT_DIR="$PWD/physx" -DPX_OUTPUT_LIB_DIR="$PWD/build-gpu/lib" -DPX_OUTPUT_BIN_DIR="$PWD/build-gpu/bin"
cmake --build build-gpu -j"$(nproc)" --target PhysXGpu
```

## Check the GPU results

`SnippetGpuParity` runs fixed scenes on the GPU and compares them with the CPU simulation, or with physical invariants for the GPU-only features, and exits nonzero when a check fails or no GPU context can be created:

```bash
./bin/linux.x86_64/release/SnippetGpuParity_64
```

## Differences from the CUDA build

- SDF collision samples the signed distance fields in software from linear device memory instead of 3D textures, on every AMD GPU. Results match the texture path to within the texture unit's interpolation rounding.
- The snippets draw GPU data by copying it into mapped OpenGL buffers instead of using CUDA-OpenGL interop.
