# Building PhysX on AMD Instinct (ROCm/HIP)

This branch adds an AMD ROCm/HIP GPU backend to the PhysX 5.6 GPU simulation
stack. The GPU code (61 device translation units, 490 kernels) builds into
`libPhysXGpu_64.so` and is validated on AMD Instinct MI300X (gfx942) and
MI355X (gfx950), and cross-compiles for MI450 (gfx1250). It uses **public
ROCm 10.0.0** only -- no internal registry, index, or container.

The AMD path is guarded behind the `PX_USE_HIP` CMake option; the upstream CUDA
and CPU build paths are unchanged.

## 1. Prerequisites

| Component | Version |
|---|---|
| AMD Instinct GPU | MI300X (gfx942), MI355X/MI350X (gfx950), MI450 (gfx1250, sim) |
| ROCm | 10.0.0 (public pip wheels) |
| OS | Ubuntu 24.04 |
| Python | 3.12 |
| CMake | 3.28+ (4.x works with the compile-object override below) |

Install the amdgpu driver and grant GPU access per the public
[Install AMD ROCm 10.0.0](https://rocm.docs.amd.com/en/docs-10.0.0/install/rocm.html)
guide, then:

```bash
sudo apt update
sudo apt install -y libatomic1 libquadmath0 git build-essential cmake python3.12 python3.12-venv
sudo usermod -a -G render,video $LOGNAME # log out/in to apply
rocminfo | grep -i 'Marketing Name' # your AMD Instinct GPU is listed
```

## 2. Install ROCm 10.0.0 (public pip wheels)

```bash
python3.12 -m venv .venv && source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install --index-url https://stable.repo.amd.com/rocm/whl-next/ "rocm[libraries,devel,device-gfx942]"
```

Select the arch extra for your target: `device-gfx942` (MI300X),
`device-gfx950` (MI355X/MI350X), `device-gfx1250` (MI450); add several to target
multiple archs.

## 3. Initialize the ROCm devel tree

The `rocm[devel]` wheel ships its development tree (HIP headers, the `hip-lang`
CMake package, `amdclang++`, and device bitcode) compressed. Expand it once with
`rocm-sdk init`, then ask the SDK for the single canonical ROCm root that CMake's
`enable_language(HIP)` needs -- **no manual symlink/merge overlay is required**:

```bash
source .venv/bin/activate
rocm-sdk init # expands the devel tree (run once per venv)
rocm-sdk test # all checks pass
export MERGED="$(rocm-sdk path --root)"
echo "ROCm root: $MERGED"
# sanity: all three must resolve under the single root
test -f "$MERGED/include/hip/hip_runtime.h" && echo "HIP headers OK"
test -x "$MERGED/lib/llvm/bin/amdclang++" && echo "amdclang++ OK"
test -f "$MERGED/lib/cmake/hip-lang/hip-lang-config.cmake" && echo "hip-lang CMake package OK"
```

> **Why `rocm-sdk init` matters:** a fresh `pip install "rocm[...,devel]"` records the
> devel wheel but does not materialize its `_rocm_sdk_devel` tree until you run
> `rocm-sdk init`. Without it, `enable_language(HIP)` fails with
> _"does not contain the HIP runtime CMake package ... hip-lang-config.cmake"_.
> Re-run `rocm-sdk init` after adding or removing a `device-gfx*` wheel.

## 4. Build the GPU stack

Set `-DCMAKE_HIP_ARCHITECTURES` to your target arch (`gfx942` / `gfx950` / `gfx1250`):

```bash
cmake -S physx/source/compiler/cmakegpu \
 -B build-hip \
 -DPX_USE_HIP=ON \
 -DPX_BUILDGPU=ON \
 -DTARGET_BUILD_PLATFORM=linux \
 -DCMAKE_BUILD_TYPE=release \
 -DCMAKE_HIP_ARCHITECTURES=gfx942 \
 -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
 -DCMAKE_HIP_COMPILER="$MERGED/lib/llvm/bin/amdclang++" \
 -DCMAKE_HIP_COMPILER_ROCM_ROOT="$MERGED" \
 -DPHYSX_ROOT_DIR="$PWD/physx" \
 -DPX_OUTPUT_LIB_DIR="$PWD/build-hip/lib" \
 -DPX_OUTPUT_BIN_DIR="$PWD/build-hip/bin" \
 '-DCMAKE_HIP_COMPILE_OBJECT=<CMAKE_HIP_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -x hip -o <OBJECT> -c <SOURCE>'

cmake --build build-hip -j"$(nproc)" --target PhysXGpu
export PHYSX_SO=$(find "$PWD/build-hip" -name libPhysXGpu_64.so | head -1)
ls -lh "$PHYSX_SO"
```

This builds all 7 GPU sub-libraries (Broadphase, Narrowphase, Solver, Articulation,
SimulationController, Common, CudaContextManager) into `libPhysXGpu_64.so`.

**Build gotcha (public ROCm pip venv + CMake 4.x):** CMake 4.x cannot auto-detect
the HIP compiler ID from the pip SDK merged root (there is no `hipconfig` binary in
the venv), so it drops the `-x hip` flag. The explicit
`-DCMAKE_HIP_COMPILE_OBJECT='... -x hip ...'` above forces it; keep it. A system
CMake 3.28 works without the override.

For `gfx1250` (MI450) add `-DCMAKE_SUPPRESS_REGENERATION=ON` (a `cmake --build`
re-configure otherwise drops the PhysXGpu target on that arch).

## 5. Verify (with expected output)

These three checks need no GPU and prove the library is a genuine AMD GPU artifact
carrying real gfx942 device code:

```bash
# A) the embedded GPU code-object fatbin section is present:
readelf -S "$PHYSX_SO" | grep -o '\.hip_fatbin'
# Expected: .hip_fatbin

# B) the library links the HIP runtime:
ldd "$PHYSX_SO" | grep amdhip64
# Expected: libamdhip64.so.7 => .../libamdhip64.so.7

# C) all 61 GPU translation units are present as gfx942 code objects:
"$MERGED/lib/llvm/bin/llvm-objdump" --offloading "$PHYSX_SO" | grep -c 'amdgcn-amd-amdhsa--gfx942'
# Expected: 61
```

Expected combined output:

```
.hip_fatbin
	libamdhip64.so.7 => /.../_rocm_sdk_devel/lib/libamdhip64.so.7
61
```

`61` is the count of GPU translation units migrated to HIP -- one gfx942 code object
per TU. Seeing `.hip_fatbin`, the `libamdhip64.so.7` linkage, and `61` confirms the
full GPU stack built for AMD Instinct. On a GPU node the library additionally loads
and dispatches its kernels on real silicon (the validated on-silicon load+dispatch
ladder).

## Known limitation on CDNA (gfx942/gfx950)

SDF (Signed Distance Field) mesh collision is **not supported** on CDNA data-center
GPUs: they have no hardware 3D-texture sampling unit, and PhysX samples SDF meshes
via `tex3D`. The HIP path traps loudly if an SDF sample is attempted (it never
returns a silently-wrong distance) and compiles out the host texture-object
creation. Every other GPU physics capability -- rigid-body dynamics, articulations,
the constraint solver, broadphase/narrowphase collision, PBD particles/fluids, and
FEM cloth/soft bodies -- is unaffected. A software-trilinear sampler over a linear
device copy of the grid is the planned follow-up.

## Porting notes

The port is a direct CUDA-to-HIP kernel migration through a `cuda_to_hip.h`
compatibility shim: every 32-lane warp collective is pinned to the logical 32-lane
subgroup (CDNA's hardware wave is 64 lanes), PTX inline asm is rewritten to HIP
intrinsics, and the CUDA driver-API host seam is remapped to HIP with `CUdeviceptr`
aliased to a 64-bit integer address.
