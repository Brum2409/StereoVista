# StereoVista - CLAUDE.md

> ## Vulkan branch (`StereoVista-vulkan`)
> This branch is the **native Vulkan 1.3** rewrite of StereoVista. The OpenGL 4.6
> app was migrated to Vulkan phase by phase; this file describes the **current
> Vulkan architecture**. A few app features are not yet ported and the advanced
> GI systems are deferred (see **Migration state** at the bottom) — the forward
> work list (remaining ports, known bugs, optimization, deletions) lives in
> **`docs/TODO.md`**. **Docs lag the code — read the actual source before touching
> a system.** On `main` (feature-frozen during the migration) the app is still
> OpenGL; that history — and the migration's design rationale / session log —
> lives in git.

## Project Overview

StereoVista is a **Vulkan 1.3** desktop application for interactive 3D visualization
with native stereo output. It renders 3D models and point clouds with a physically
based (metallic-roughness) forward pipeline, shadow mapping (directional sun +
point-light cubemaps), a compute-based point-cloud rasterizer, and a unified
overlay renderer for 3D cursors and tools. Stereo is first-class: a single-pass
**multiview** renderer drives mono, native **quad-buffer** 3D-display present, a
**side-by-side** preview, and **OpenXR** HMD output from the same path.

**Platform:** Windows only (MSVC toolchain, v143 / VS2022)
**Language:** C++17 / GLSL (compiled to SPIR-V)
**Build System:** Visual Studio 2022 (`StereoVista.sln`) — MSBuild only

---

## Build Instructions

Open `StereoVista.sln` in Visual Studio 2022 and build, or from the command line:

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
    StereoVista.sln /p:Configuration=Release /p:Platform=x64 /m
```

- **Recommended config:** `Release | x64` (also builds `Debug | x64`).
- **Output:** `bin/x64/Release/StereoVista.exe`.
- **The Vulkan toolchain is fully vendored** — a fresh clone builds as-is with no
  SDK install (Vulkan-Headers + volk + VMA + shaderc + glslc + SPIRV-Reflect live
  in the repo). End users need only a Vulkan-capable GPU driver. Validation layers
  are requested only if present (install them via the Vulkan SDK / vkconfig for
  Debug diagnostics; `SV_VULKAN_VALIDATION=1` is set in Debug|x64).
- **Shaders** in `assets/shaders_vk/` are compiled to SPIR-V by a per-shader
  `CustomBuild` step (vendored `glslc --target-env=vulkan1.3`) into
  `$(OutDir)assets\shaders_vk\*.spv`; `ShaderCompiler` also compiles at runtime via
  shaderc when a `.spv` is absent.
- Post-build steps copy required DLLs (`assimp-vc143-mt.dll`, `LASzip64.dll`,
  `shaderc_shared.dll`) to the output directory.

**No automated test suite exists.** Verification is manual/visual by running the
application.

---

## Repository Structure

```
StereoVista/
├── src/                    # C++ implementation files
│   ├── App/                # main.cpp (thin entry) + Application (main loop, wiring, debug UI)
│   ├── Platform/           # Window (GLFW, no-API + VkSurface), asset-path resolution
│   ├── RHI/                # ONLY layer that touches Vulkan: Device, Swapchain, Buffer,
│   │                       #   Texture, Pipeline (+SPIRV-Reflect), DescriptorAllocator,
│   │                       #   Barrier, UploadRing, ShaderCompiler, VMA glue
│   ├── Renderer/           # Renderer, MaterialSystem, MeshBuffer, PointCloudGpu,
│   │   └── passes/         #   Shadow / Forward / Skybox / PointCloud / Overlay / Tonemap
│   ├── Scene/              # interim scene host (office.scene loader, primitives,
│   │                       #   Assimp import, picking)
│   ├── Cursors/            # 3D cursor system (Base + Sphere/Plane/Fragment), GPU-free
│   ├── Tools/              # TransformGizmo, MeasurementTool, ClipPlaneTool (overlay-drawn)
│   ├── Plugins/            # GL-free plugin system + Examples/CrosshairPlugin, MeasurementPlugin
│   ├── Core/               # Camera (glm), UndoManager (generic command stack)
│   ├── Loaders/            # PointCloudLoader (LAS/LAZ/PLY/HDF5/XYZ/PCB parsers + RHI upload)
│   │   └── Slpk/           #   SLPK/I3S (namespace i3s::): SlpkArchive (mmap ZIP64 + hash
│   │                       #   index), I3SLayer (1.6 / 1.7+ / PCSL node trees, materials,
│   │                       #   statistics), I3SGeometry (draco + raw decode), I3STexture
│   │                       #   (jpg/png + KTX2/basis→BC7), I3SPointCloud (LEPCC points),
│   │                       #   GeoAnchor (WGS84↔ECEF↔ENU) — see docs/SLPK_IMPLEMENTATION_PLAN.md
│   └── Engine/             # XRSession (OpenXR/Vulkan), Screenshot, StbImageImpl,
│                           #   + EXCLUDED GL-era reference (see Migration state)
├── headers/                # Header files (mirrors src/ structure)
│   └── libs/               # Vendored: imgui (docking + backends), volk, vma, spirv_reflect,
│                           #   json, stb_image, 3dconnexion, portable-file-dialogs,
│                           #   libdeflate (decompress subset), md5, draco (decoder subset)
├── assets/
│   ├── shaders_vk/         # Vulkan GLSL (→ SPIR-V): mesh, depth_sun/point, skybox,
│   │                       #   pointcloud_*, overlay, tonemap, fullscreen, preview_lit
│   │                       #   + shared C++/GLSL structs (scene_types.h, pointcloud_types.h,
│   │                       #   overlay_types.h) and pointcloud_common.glsl
│   └── models/             # Default 3D models
├── dependencies/           # Pre-built third-party libs (GLFW, GLM, Assimp, HDF5, LASzip,
│   ├── include/            #   Vulkan-Headers, shaderc, OpenXR) + glslc under tools/
│   ├── lib/  bin/  tools/
├── StereoVista/            # Runtime data loaded by the executable at runtime
│   └── office.scene        # Example scene file
├── fonts/  skybox/
├── StereoVista.sln
└── StereoVista/StereoVista.vcxproj
```

Wire every source add/remove into `StereoVista.vcxproj` **and** `.vcxproj.filters`
(CI and MSBuild only compile what's listed).

---

## Key Dependencies

| Library | Purpose | Location |
|---|---|---|
| **Vulkan 1.3** + **volk** | Graphics API + loader (replaces GLAD) | headers `dependencies/include/vulkan`; `headers/libs/volk` |
| **VMA** | GPU memory allocation | `headers/libs/vma` |
| **shaderc** + **glslc** | GLSL→SPIR-V (runtime + offline build step) | `dependencies/{include,lib,bin,tools}` |
| **SPIRV-Reflect** | Reflect descriptor/push layouts from SPIR-V | `headers/libs/spirv_reflect` |
| GLFW | Windowing + input (init `GLFW_NO_API`, `VkSurfaceKHR`) | `dependencies/` |
| GLM | Math (**`GLM_FORCE_DEPTH_ZERO_TO_ONE`** project-wide) | `dependencies/` |
| Assimp | 3D model loading (OBJ, FBX, GLTF, …) | `dependencies/` |
| ImGui (**docking**, v1.91.1) | GUI + docking/multi-viewport, `imgui_impl_vulkan` + `imgui_impl_glfw` | `headers/libs/imgui/` |
| nlohmann/json | JSON parsing | `headers/libs/json.h` |
| stb_image | Image loading | `headers/libs/stb_image.h` |
| HDF5 + HighFive | Point cloud HDF5 format | `dependencies/` |
| LASzip | LAS/LAZ point cloud format | `dependencies/` |
| OpenXR loader | VR (bound to Vulkan via `XR_KHR_vulkan_enable2`) | `dependencies/` |
| TDxNavLib | 3DConnexion SpaceMouse (not yet wired — see Migration state) | `dependencies/` |
| portable-file-dialogs | Native file dialogs | `headers/libs/` |
| **libdeflate** v1.25 (decompress-only subset) | SLPK gzip/deflate inflate | `headers/libs/libdeflate/` |
| **md5-c** (public domain) | SLPK `@specialIndexFileHASH128@` path hashing | `headers/libs/md5/` |
| **draco** v1.5.7 (decoder-only subset) | SLPK/I3S compressed geometry decode | `headers/libs/draco/` |

Added libraries are vendored under `headers/libs/` + `dependencies/` and wired into the `.vcxproj` (include/lib dirs + post-build DLL copy).

---

## Architecture

### Layering — the RHI is the only layer that includes `<vulkan.h>`

```
App          Application: main loop (poll → update → render → present), wiring, debug UI
Systems      Scene · Cursors · Tools · Plugins · Loaders · XRSession
Renderer     Renderer + passes/ (Shadow, Forward, Skybox, PointCloud, Overlay, Tonemap)
             · MaterialSystem · MeshBuffer · PointCloudGpu · FrameSubmission (scene input)
RHI (Vulkan) Device · Swapchain · Buffer · Texture · Pipeline · DescriptorAllocator
             · Barrier · UploadRing · ShaderCompiler
Platform     Window (GLFW, no-API + surface) · Paths
```

Rule: **nothing above the RHI includes a Vulkan header** — systems speak in RHI
handles (`rhi::Buffer`, `rhi::Texture`, `rhi::Pipeline`, …). `Application` owns the
systems as members (no global soup); `FrameSubmission` is the renderer's per-frame
scene input (cameras, draw list, lights, sky, clip planes, overlay, depth queries).

### House rendering conventions (`headers/Renderer/Projection.h` — enforced)

All projections come from the `renderer::` factories: **[0,1] depth**
(`GLM_FORCE_DEPTH_ZERO_TO_ONE`), **reverse-Z** (near→1, far→0,
`VK_COMPARE_OP_GREATER`, clear depth 0.0) for float-depth precision, and a
**Y-flip baked into the projection** which keeps the image upright *and* preserves
GL winding — CCW-authored meshes stay **`VK_FRONT_FACE_COUNTER_CLOCKWISE`**. Do not
hand-roll projections above the RHI. Shared GPU structs live in ONE header included
by both C++ and GLSL with `layout(scalar)` (`assets/shaders_vk/scene_types.h` ↔
`Renderer/GpuTypes.h`, static_asserted). Materials are **bindless** (a texture
array bound once/frame; materials store indices + a flags bitfield). Per-object /
per-batch data rides push constants and **buffer device addresses** (BDA), not
per-draw descriptor churn.

### Rendering pipeline (single-pass multiview)

`Renderer::recordFrame` sequences **pass objects** over the `FrameSubmission`:
directional + point-light **shadow** passes → **forward PBR** (Cook-Torrance) →
**point-cloud** compute resolve (between forward and skybox, depth-correct) →
**skybox** → **tonemap** resolve to the backbuffer → **overlay** (depth-tested vs
the scene) → **ImGui**. Both eyes are drawn in one pass via `VK_KHR_multiview`
(per-view camera array indexed by `gl_ViewIndex`); there is no twice-per-frame
`renderEye`. Stereo is additive: `StereoMode {Off, QuadBuffer, SideBySide}` sets the
view count and the backbuffer eye layout; **OpenXR** retargets the same multiview
target to per-eye HMD images. **Only Shadow-Mapping lighting ships** (see Migration
state).

- **Shadows:** sun = 4K texel-snapped ortho depth map; points = D32 **cube array**
  rendered in ONE multiview pass per light (viewMask 0x3F, `gl_ViewIndex` = face,
  no geometry shader, real reverse-Z depth). Normal-offset + slope-scaled bias +
  per-light PCF/PCSS.
- **Point clouds:** Schütz compute software rasterizer — `uint64` framebuffer +
  `atomicMin` (`shaderBufferInt64Atomics`), standard + HQS, all-BDA (zero
  descriptor sets per cloud), streamed via a persistently-mapped `UploadRing`.
- **Overlay:** ONE dynamic-vertex-buffer renderer (`OverlayDrawList` → `OverlayPass`)
  — lines expand to screen-space quads in the VS; per-batch dynamic depth
  test/write/compare-op collapses the old two GL overlay shader paths.

### Plugin system (GL-free)

Tools are **static (compile-time) plugins**: a `Plugins::Plugin` subclass
(`headers/Plugins/Plugin.h`) overrides only the hooks it needs — `onBuildOverlay(ctx)`
(append world-space geometry to `ctx.overlay()` **once per frame**; one build feeds
both stereo eyes), `onRenderUI`, `onRenderMenu`, and `bool`-returning
`onMouseButton`/`onScroll`/`onKey` — and self-registers with `REGISTER_PLUGIN(Type)`
(`headers/Plugins/PluginRegistry.h`) in its `.cpp`. `Plugins::PluginContext` exposes
services: `scene::Scene&`, `Camera&`, `OverlayDrawList&`, `core::UndoManager&`,
depth-cursor / mouse-ray / model picking + selection, mouse/mods/viewport,
`viewProj()`, and toasts. `MainPluginContext` (a friend of `Application`) implements
it; `Plugins::PluginManager` (owned by `Application`, no globals) drives the hooks
from the loop. `CrosshairPlugin` (`Plugins/Examples/`) is the copy-me template;
`MeasurementPlugin` owns and drives `Tools::MeasurementTool`. Adding a tool = one
header + one `.cpp` + a `REGISTER_PLUGIN` line + both files in the
`.vcxproj`/`.filters`. **Full guide: `docs/PLUGINS.md`.**

### Namespaces

`app::` (Application) · `rhi::` (Vulkan RHI) · `renderer::` (renderer + passes) ·
`scene::` (interim scene host) · `Cursor::` · `Tools::` · `Plugins::` ·
`core::` (UndoManager) · `Platform::` (Window/Paths) · `Engine::` (XRSession,
Screenshot, point-cloud data types in `Data.h`).

---

## Code Conventions

**Naming:** Classes `PascalCase`; functions/variables `camelCase`; constants/enums
`UPPER_SNAKE_CASE`; member variables mixed — follow the file you're editing.

**File organization:** headers in `headers/`, implementations in `src/`, matching
directory structure; vendored third-party headers isolated in `headers/libs/`;
Vulkan GLSL in `assets/shaders_vk/`.

**General:**
- C++17 is used and expected. **Rewrite better, don't stale-translate GL** — if a
  system carried a bug in the OpenGL version, fix it rather than port it. Bugs
  still open (sphere cursor, cursor-sync jump, camera gaps) are in `docs/TODO.md`.
- Blocks binding the shared `scene_types.h` structs must be `layout(..., scalar)`.
  Variable indexing of the bindless texture array needs
  `#extension GL_EXT_nonuniform_qualifier`.
- After adding a shader, confirm its `.spv` appears in
  `$(OutDir)assets\shaders_vk\` — MSBuild has been observed to not fail the build
  on a glslc error.
- ImGui is the **docking branch** (v1.91.1) on `imgui_impl_vulkan` + `imgui_impl_glfw`
  (docking + multi-viewport). `Application` runs `UpdatePlatformWindows()` /
  `RenderPlatformWindowsDefault()` each frame for dragged-out OS windows. The
  project-local `imgui_style.cpp`, `imgui_sytle.h`, `imgui_incl.h`,
  `IconsFontAwesome5.h`, and both backends must be preserved across ImGui updates.
  Note the ImGui-Vulkan `pColorAttachmentFormats` pointer-lifetime trap (kept in a
  long-lived `Application` member) documented in the status doc §4.

---

## Runtime Configuration Files (cwd-relative)

- `pipeline_cache.bin` — disk-persisted `VkPipelineCache` (driver-validated;
  gitignored; recreated if corrupt).
- `imgui.ini` — ImGui docking layout.
- `StereoVista/screenshots/*.png` — screenshot output.

> The GL-era `preferences.json` / `shortcuts.json` / `cursor_presets.json` are **not
> yet read/written** by the Vulkan app — settings live in the debug panel with
> in-memory defaults. Preferences/shortcuts/cursor-preset persistence returns with
> the GUI + SceneManager port (see Migration state).

---

## Development Notes

- Verification is **manual/visual** (no unit tests). The owner runs each milestone
  on the stereo GPU; give concrete run/verify steps at a phase boundary.
- Build **both** configs green (Release + Debug x64) and wire file adds/removes into
  the `.vcxproj`(+`.filters`).
- Windows-only (VS project files, MSVC pragmas, Win32 window handles / DLL loading).
- Point cloud formats: XYZ/TXT and ASCII PLY via the generic text parser; **binary
  PLY** via the header-driven `loadFromBinaryPLY` (x/y/z + optional rgb/intensity,
  any scalar type, streamed as fixed-stride records); LAS/LAZ via LASzip; HDF5 via
  HighFive; native `.pcb`. HDF5 needs `libhdf5.lib`; LAZ needs `laszip64.lib`.

### Migration state (what's done / deferred / not yet ported)

- **Done (Phases 0–7):** Vulkan bootstrap + RHI, forward PBR + shadow mapping,
  loaders → RHI upload, compute point-cloud pipeline, camera/cursors/tools/plugins
  on the overlay renderer, multiview stereo (quad-buffer + SBS + mono) and OpenXR.
  **Phase 8** removed the dead/superseded OpenGL scaffolding and rewrote these docs.
- **Deferred to a later native-Vulkan phase (Phase 9):** the advanced GI /
  post-processing — Voxel Cone Tracing, DDGI, BVH ray-traced Radiance, Bloom, SSAO.
  These were deleted from the tree (git history is the reference); the app ships
  **Shadow Mapping only**, and the VCT/Radiance lighting-mode options are dropped.
- **Not yet ported (tracked as a separate scope):** the full production GUI (the
  Vulkan app currently drives everything through a debug panel), `SceneManager`
  scene save/load/merge + hierarchy, preferences/shortcuts/cursor-preset
  persistence, 3DConnexion SpaceMouse, camera snapshots, cursor sync, the
  `CursorPreview3D` thumbnail, and out-of-core point-cloud LOD (`OctreePointCloudManager`).
  Their **OpenGL sources remain in-tree but `ExcludedFromBuild`** (in `src/Gui`,
  `src/Core`, `src/Engine`, `src/Cursors`) as the behaviour reference until each is
  re-implemented on Vulkan, then deleted. See `docs/TODO.md` for
  the plan and status.
