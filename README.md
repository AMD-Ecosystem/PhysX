# NVIDIA PhysX

<details>
<summary>Copyright & License</summary>

Copyright (c) 2008-2025 NVIDIA Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of NVIDIA CORPORATION nor the names of its
 contributors may be used to endorse or promote products derived
 from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

</details>

Please also see license files in the root folder and in the respective subfolders.

## Content

This repo contains:

| Directory | Description |
|---|---|
| [`ovphysx/`](ovphysx/) | ovphysx — C API with Python bindings for USD physics simulation with DLPack tensor interop (`pip install ovphysx`) |
| [`physx/`](physx/) | PhysX SDK — real-time physics simulation engine |

## Runs on AMD Instinct (ROCm/HIP)

This branch adds an **AMD ROCm/HIP GPU backend** for the PhysX 5.6 GPU simulation
stack, so PhysX GPU physics runs on **AMD Instinct** data-center GPUs, not only on
CUDA. The entire GPU stack (61 device translation units, 490 kernels) builds and
links into `libPhysXGpu_64.so` and is validated on real silicon.

| AMD Instinct GPU | Arch | ROCm | Status |
|---|---|---|---|
| MI300X | gfx942 | 10.0.0 | Validated (build + on-GPU module-load/dispatch) |
| MI355X / MI350X | gfx950 | 10.0.0 | Validated (build + on-GPU module-load/dispatch) |
| MI450 | gfx1250 | 10.0.0 | Functional simulator (cross-compile) |

The AMD path is guarded behind the `PX_USE_HIP` CMake option; the upstream CUDA
and CPU build paths are preserved unchanged. Build it against **public ROCm 10.0.0**
(installed from the public AMD pip index, no internal registry):

```bash
# 1. Public ROCm 10.0.0 base + a supported Python
python3.12 -m venv .venv && source .venv/bin/activate
python -m pip install --index-url https://stable.repo.amd.com/rocm/whl-next/ "rocm[libraries,devel,device-gfx942]"
rocm-sdk init # expand the devel tree (run once)
export MERGED="$(rocm-sdk path --root)"

# 2. Build the GPU stack with the HIP backend (see INSTALL_ROCm.md for details + gfx950/gfx1250)
cmake -S physx/source/compiler/cmakegpu -B build-hip \
 -DPX_USE_HIP=ON -DPX_BUILDGPU=ON -DTARGET_BUILD_PLATFORM=linux \
 -DCMAKE_HIP_ARCHITECTURES=gfx942 -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
 -DCMAKE_HIP_COMPILER="$MERGED/lib/llvm/bin/amdclang++" \
 -DCMAKE_HIP_COMPILER_ROCM_ROOT="$MERGED" \
 -DPHYSX_ROOT_DIR="$PWD/physx" \
 -DPX_OUTPUT_LIB_DIR="$PWD/build-hip/lib" -DPX_OUTPUT_BIN_DIR="$PWD/build-hip/bin"
cmake --build build-hip -j"$(nproc)" --target PhysXGpu
```

Full step-by-step build, verify (with expected output), and known-limitation
notes are in **[`INSTALL_ROCm.md`](INSTALL_ROCm.md)**.

> **Known limitation on CDNA (gfx942/gfx950):** SDF (Signed Distance Field) mesh
> collision is gated off -- CDNA data-center GPUs have no hardware 3D-texture
> sampling unit. All other GPU physics (rigid bodies, articulations, the solver,
> broadphase/narrowphase, PBD particles/fluids, FEM cloth and soft bodies) is
> unaffected. See `INSTALL_ROCm.md`.

### ovphysx

| Release | Compatibility |
|---|---|
| [0.8](https://github.com/NVIDIA-Omniverse/PhysX/tree/ovphysx-0.8.0) | PhysX SDK 5.13.0 |
| [0.7](https://github.com/NVIDIA-Omniverse/PhysX/tree/ovphysx-0.7.0) | PhysX SDK 5.12.0 |
| [0.6](https://github.com/NVIDIA-Omniverse/PhysX/tree/ovphysx-0.6.4) | PhysX SDK 5.11.0 |
| [0.5](https://github.com/NVIDIA-Omniverse/PhysX/tree/ovphysx-0.5.11) | PhysX SDK 5.10.0 |
| [0.4](https://github.com/NVIDIA-Omniverse/PhysX/tree/ovphysx-0.4.13) | PhysX SDK 5.9.0 |
| [0.3](https://github.com/NVIDIA-Omniverse/PhysX/tree/ovphysx-0.3.7) | PhysX SDK 5.9.0 |
| [0.2](https://github.com/NVIDIA-Omniverse/PhysX/tree/ovphysx-0.2.9) | PhysX SDK 5.8.0 |

### Omniverse PhysX Extensions and PhysX SDK

| Release | Compatibility |
|---|---|
| [110.1 and 5.9.0](https://github.com/NVIDIA-Omniverse/PhysX/tree/110.1-omni-and-physx-5.9.0) | [IsaacSim 6.0.0](https://github.com/isaac-sim/IsaacSim/tree/v6.0.0) |
| [110.0 and 5.8.0](https://github.com/NVIDIA-Omniverse/PhysX/tree/110.0-omni-and-physx-5.8.0) | [IsaacSim 6.0.0-dev2](https://github.com/isaac-sim/IsaacSim/tree/v6.0.0-dev2) |
| [109.0 and 5.7.0](https://github.com/NVIDIA-Omniverse/PhysX/tree/109.0-omni-and-physx-5.7.0) | [IsaacSim 6.0.0-dev](https://github.com/isaac-sim/IsaacSim/tree/v6.0.0-dev) |
| [107.3 and 5.6.1](https://github.com/NVIDIA-Omniverse/PhysX/tree/107.3-omni-and-physx-5.6.1) | [IsaacSim 5.1.0](https://github.com/isaac-sim/IsaacSim/tree/v5.1.0) |

Additional simulation libraries:

| Directory | Description |
|---|---|
| [`blast/`](blast/) | Blast SDK — destruction and fracture simulation |
| [`flow/`](flow/) | Flow SDK — fluid and fire simulation |

## Support

* Please use GitHub [Discussions](https://github.com/NVIDIA-Omniverse/PhysX/discussions/) for questions and comments.
* GitHub [Issues](https://github.com/NVIDIA-Omniverse/PhysX/issues) should only be used for bug reports or documentation issues.
* You can also ask questions in the NVIDIA Omniverse #physics [Discord Channel](https://discord.com/invite/XWQNJDNuaC).


<!-- physics-component-licenses-begin -->
## Component licenses

These licenses apply to the listed components; other components retain their own licenses.

- `ovphysx/` source: [Apache License 2.0](ovphysx/LICENSE.txt), with the bundled-component exceptions in [third-party notices](ovphysx/THIRD_PARTY_NOTICES.md).
- `physx/` source: [Apache License 2.0](physx/LICENSE.md), with the bundled-component exceptions in [third-party notices](physx/THIRD_PARTY_NOTICES.md).
- ovphysx binaries: [NVIDIA Omniverse agreements](ovphysx/licenses/LICENSE-binary.txt).
- Omniverse Extensions dependencies: [NVIDIA agreements](ovphysx/tools/internal-licenses/Omniverse-Extensions-LICENSE.txt).
- OpenUSD-derived code: [Tomorrow Open Source Technology License 1.0](ovphysx/tools/internal-licenses/OpenUSD-LICENSE.txt).
- DLPack: [Apache License 2.0, upstream contributor attribution](ovphysx/tools/internal-licenses/dlpack-LICENSE.txt).
- Repoman bootstrap tooling: [MIT](ovphysx/tools/internal-licenses/repoman-LICENSE.txt).
- V-HACD code: [BSD 3-Clause](ovphysx/tools/internal-licenses/vhacd-LICENSE.txt).
<!-- physics-component-licenses-end -->
