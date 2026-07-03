# StereoVista → Vulkan — LIVING MIGRATION STATUS

> ## 🚦 READ ME FIRST (every new Claude session)
> This file is the **shared memory** for the OpenGL→Vulkan migration, which runs
> across many separate Claude Code sessions. Read it to understand *where we are*.
>
> **Golden rules:**
> 1. **This file is a helper, not a contract.** The plan below is a best guess.
>    If you find a better approach while working, **take it** — then record the
>    change here.
> 2. **Do NOT trust this file as ground truth about the code.** Before you touch
>    a system, **read the actual source yourself** (files change; this file lags).
>    Use it only for orientation and history.
> 3. **This is a rewrite, not a translation. Make it BETTER — deeply optimized,
>    idiomatic, native Vulkan.** Do **not** stale-copy-paste the OpenGL logic
>    into Vulkan. If something can be done better the Vulkan way (single-pass
>    multiview stereo, GPU-driven rendering, bindless descriptors, timeline
>    semaphores, async compute, `vkCmdDrawIndirect`, dynamic rendering, proper
>    barriers, VMA suballocation, etc.), **do it that way** — even if it means a
>    large rewrite or changing the planned direction. The OpenGL code is the
>    reference for *behaviour*, not for *how*.
> 4. **If you are unsure about anything — Vulkan APIs, extension support, the best
>    pattern, how the existing system behaves — DIG DEEPER: read more source, and
>    use the Internet (official Vulkan spec/registry, Khronos samples, vendor
>    docs).** Do not guess. If it is a product/architecture decision only the user
>    can make (scope, trade-offs, priorities), **ask the user** with a concrete
>    question rather than assuming.
> 5. **Prefer good libraries over reinventing.** If a well-maintained library
>    greatly simplifies or optimizes the work (e.g. **VMA** for allocation,
>    **shaderc/glslang** for GLSL→SPIR-V, **volk** for a Vulkan loader,
>    **vk-bootstrap** for instance/device setup, **SPIRV-Reflect** for descriptor
>    reflection), **use it** instead of hand-rolling. When you do, **fully
>    integrate it yourself**: download *every* required file (headers, `.lib`,
>    `.dll`, license) into the repo under the existing vendoring layout
>    (`headers/libs/…`, `dependencies/include|lib|bin`), wire it into
>    `StereoVista.vcxproj`(+`.filters`) — include dirs, lib dirs, additional
>    dependencies, and a post-build copy for any runtime `.dll` (mirror how
>    `assimp-vc143-mt.dll` / `LASzip64.dll` are handled) — and confirm it builds
>    **green on CI**. No "install this SDK manually" hand-waving: the checkout must
>    build as-is. Keep licenses; avoid GPL. Note each added library in §4.
> 6. **Update this file when you finish work.** Move tasks between sections, note
>    what you actually did, what changed vs. the plan, and what's next. Append a
>    dated entry to the **Session Log**.
> 7. The deep design rationale (RHI layering, decisions, per-system reuse/rewrite
>    table) lives in **`docs/VULKAN_MIGRATION.md`** — read it once for context.
>
> **Branch:** `StereoVista-vulkan` — this *is* the Vulkan branch. Sessions may
> develop on per-session `claude/...` working branches (CI runs on those too),
> but every green milestone is pushed to `StereoVista-vulkan`.

---

## 0. Current status at a glance

| | |
|---|---|
| **Current phase** | Phase 1 ☑ **DONE — user-verified 2026-07-03** (triangle + drag-out ImGui viewports render after the winding/format-lifetime fixes). Next: **Phase 2 — RHI hardening** |
| **What builds** | New Vulkan app skeleton (Window/Device/Swapchain/Renderer/ImGui-Vulkan + multiview hello-triangle). Old GL sources are `ExcludedFromBuild` (in-tree as behaviour reference); deferred GI subsystems deleted |
| **Vulkan code present** | `headers/RHI + src/RHI` (Device, Swapchain, ShaderCompiler, VMA glue), `Platform/Window`, `Renderer` (multiview scene target, timeline sync), `App/Application` |
| **Last updated** | 2026-07-03 — Phase 1 verification fixes + close-out |

Legend: ☐ not started · ◐ in progress · ☑ done · ✎ changed from original plan

---

## 0b. Working agreement (user decisions — 2026-07-02)
These are settled by the project owner. Follow them unless the user changes them.
- **Deferred features → DELETE now, rebuild fresh later.** Do **not** port VCT
  (`Voxalizer`, `voxelization/*`), DDGI (`DDGIVolume`, `ddgi*`), BVH Radiance
  (`BVH`, `BVHDebug`), Bloom (`BloomRenderer`), or SSAO (`SSAORenderer`). Remove
  their `.cpp/.h`, shaders, and `.vcxproj` entries as the rewrite proceeds. **Git
  history is the reference** for the Phase 9 native re-implementation — no need to
  keep the code in-tree. Also drop the VCT/Radiance `LightingMode` options for now
  (ship only **Shadow Mapping**).
- **`main` is feature-frozen** during the migration, so the Vulkan branch isn't
  chasing a moving target. Don't re-port new OpenGL features (there shouldn't be any).
- **The USER verifies each phase.** CI only compiles (no GPU); the agent can't run
  the Windows app. The user has the stereo GPU and runs each milestone build,
  reports visual results/bugs, and the agent iterates from that feedback. So: at a
  phase boundary, get CI green, then hand the user clear run/verify steps and wait
  for their result before declaring the phase done.
- **Partial interim state is fine.** The branch may be less capable than `main`
  mid-migration (missing post-FX/GI, some tools not yet ported). Just keep each
  phase **compiling green on CI**; don't invest in bridging to keep every feature
  live at every step.

---

## 1. Phase board (suggested order — reorder if you have a better idea)

- ☑ **Phase 0 — Toolchain & build setup** (vendored Vulkan-Headers/volk/VMA/shaderc+glslc in, GLAD/`opengl32.lib`/ImGui-GL3/`Engine::Shader` out; deferred GI systems deleted; SPIR-V build step)
- ☑ **Phase 1 — Core Vulkan bootstrap + `Application` skeleton** (window, RHI Device/Swapchain, multiview triangle, ImGui-Vulkan; user-verified 2026-07-03 after fixing the front-face convention + ImGui viewport format lifetime — see session log)
- ☐ **Phase 2 — RHI hardening** (buffers/images/pipelines/descriptors, HDR target + tonemap)
- ☐ **Phase 3 — Mesh forward PBR + shadow mapping** (Shadow-Mapping lighting only)
- ☐ **Phase 4 — Loading system → RHI upload** (parsers reused; streaming preserved)
- ☐ **Phase 5 — Compute point-cloud pipeline** (Schütz rasterizer + HQS; biggest rewrite)
- ☐ **Phase 6 — Camera, cursors, tools, plugins** (overlay renderer)
- ☐ **Phase 7 — Stereo (`StereoTarget`) + Vulkan OpenXR**
- ☐ **Phase 8 — Decompose `main.cpp`/`GUI.cpp`, delete GL scaffolding, update docs**
- ☐ **Phase 9 (later, out of current scope) — re-add deferred features natively**

> **Deferred → DELETE now** (per §0b; re-implemented natively in Vulkan in Phase 9,
> git history is the reference): Voxel Cone Tracing (`Voxalizer`, `voxelization/*`),
> DDGI (`DDGIVolume`, `ddgi*`), BVH Radiance + `BVHDebug`, Bloom (`BloomRenderer`),
> SSAO (`SSAORenderer`). Ship only **Shadow Mapping**; drop the VCT/Radiance
> lighting modes for now.

---

## 2. Per-phase detail (goal · how it could be done · exit · what to read)

### Phase 0 — Toolchain & build setup  ☑ (2026-07-02)
- **DONE — deltas vs the plan:** the toolchain is **vendored, not
  SDK-installed** (see §4): Vulkan-Headers + volk + VMA + shaderc_shared +
  glslc live in the repo, the CI SDK-install step was removed, and the old GL
  sources went `ExcludedFromBuild` immediately (GLAD's removal makes them
  uncompilable — "old GL code stays compiling" was never possible). Deferred
  GI subsystems (Voxalizer/DDGI/BVH/BVHDebug/Bloom/SSAO + shaders) deleted per
  §0b. `GLFW_INCLUDE_NONE` added to the required defines (see §4). SPIR-V
  build step = per-shader `CustomBuild` items running vendored glslc into
  `$(OutDir)assets\shaders_vk`.
- **No throwaway spikes.** Go straight into the real migration. The old plan had
  two spikes; both are unnecessary because the risks they were meant to de-risk
  are resolved **by the Vulkan spec** and become normal code in Phase 1:
  - `shaderBufferInt64Atomics` (the point-cloud rasterizer's hard dependency) is
    **guaranteed on any Vulkan 1.2+ device** — it's a core feature, not optional
    (only the *shared*-memory int64 variant is optional). Target **Vulkan 1.3**
    and it's there. Just enable + assert it in `Device` init.
  - Quad-buffered stereo is native (`imageArrayLayers = 2`) and `VK_KHR_multiview`
    is core since Vulkan 1.1 — validated for real when the swapchain (Phase 1) and
    stereo present (Phase 7) are built, not in a throwaway.
- **Goal:** the project builds green on CI with the full Vulkan toolchain wired in
  and the OpenGL-specific deps removed — ready for real Vulkan code.
- **How:** self-integrate (per §golden-rule-5 / `MIGRATION.md §7b`) **Vulkan SDK**,
  **VMA**, **shaderc/glslang**, **volk** into `StereoVista.vcxproj`(+`.filters`);
  add a GLSL→SPIR-V build step; validation layers in Debug. Do the §2c dependency
  surgery: **remove GLAD + `opengl32.lib` + ImGui GL3 backend**, add volk;
  **define `GLM_FORCE_DEPTH_ZERO_TO_ONE`** project-wide. (Old GL render code can
  stay compiling for now; it stops running once Phase 1 creates the no-GL window.)
- **Exit:** CI green with the Vulkan toolchain linked and GL loader removed.
- **Read:** `StereoVista/StereoVista.vcxproj` (include/lib dirs, deps, post-build
  copy), `docs/VULKAN_MIGRATION.md §2.12 + §7b`, `headers/libs/imgui/backends/*`.

### Phase 1 — Core bootstrap + `Application`  ☑ (2026-07-03, user-verified)
- **Implemented (2026-07-02):** `Platform::Window` (GLFW_NO_API, raw mouse
  motion, resize→GUI-scale hook), `rhi::Device` (volk; validation layer +
  debug messenger when `SV_VULKAN_VALIDATION` and the layer exists; robust
  multi-GPU pick with per-candidate rejection reasons; fail-loud required
  features incl. shaderInt64+int64 buffer atomics, multiview, timeline
  semaphores, bufferDeviceAddress, descriptor indexing, dynamicRendering,
  sync2; VMA; `immediateSubmit`), `rhi::Swapchain` (FIFO mono; probes
  `maxImageArrayLayers` = the Phase 7 stereo-present seam; per-image present
  semaphores), `renderer::Renderer` (2 FIF paced by ONE timeline semaphore;
  per-frame transient command pool + reset descriptor pool; sync2 barriers;
  **layered multiview scene target** RGBA16F+D32 with per-view camera UBO
  array indexed by `gl_ViewIndex` — mono = viewMask 0b1; blit → swapchain,
  ImGui pass on the backbuffer), `rhi::ShaderCompiler` (precompiled `.spv`
  else runtime shaderc w/ includes), `app::Application` (loop, ImGui-Vulkan
  init/shutdown, resize/minimize handling, debug panel), triangle shaders
  under `assets/shaders_vk/`. Old input callbacks are NOT yet rewired (they
  live in excluded main.cpp and come back with camera/tools phases); ImGui's
  GLFW backend chains its own.
- **Exit:** CI green ✅; user verified on the RTX 3050 Ti laptop (2026-07-03):
  swaying triangle renders, panels dock/undock/drag out to OS windows,
  ~600 fps uncapped (Immediate) — after the verification-round fixes in the
  session log (front-face convention, ImGui viewport format lifetime,
  Suboptimal-present hardening).
- **Goal:** a window with Vulkan + ImGui drawing, and the new app skeleton.
- **How:** `Platform::Window` on GLFW with `GLFW_NO_API` + `VkSurfaceKHR` (keep
  existing input callbacks). RHI `Device` (instance/physical/logical device,
  queues, VMA, volk) targeting **Vulkan 1.3** with **feature detection &
  enablement** — require and enable `shaderBufferInt64Atomics` (guaranteed at
  1.2+), multiview, descriptor indexing, dynamic rendering, timeline semaphores;
  **fail loudly with a clear message** if any required feature is absent (this
  replaces the old capability spike). `Swapchain`, 2 frames-in-flight,
  command/descriptor pools. `Application` class owns the loop
  (poll→update→render→present). Hello-triangle through RHI + `ShaderCompiler`.
  Swap ImGui backend to **`imgui_impl_vulkan`** (keep `imgui_impl_glfw`);
  preserve project-local `imgui_style*.*`, `imgui_incl.h`, `IconsFontAwesome5.h`,
  docking + multi-viewport.
- **Exit:** window opens, ImGui panels dock/undock/drag-out, triangle renders.
- **Read:** `src/Engine/Window.cpp`, `src/main.cpp:3485-3900` (init order),
  `headers/libs/imgui/backends/*`, `headers/Gui/*`.

### Phase 2 — RHI hardening  ☐
- **Goal:** the abstraction the rest of the port builds on.
- **How:** finalize `Buffer`/`Texture` (VMA + staged uploads), `Pipeline` (gfx +
  compute from SPIR-V + descriptor-layout spec + dynamic-rendering formats),
  growable `DescriptorAllocator`, sync2 barriers, HDR offscreen color+depth
  target, minimal **tonemap** resolve. Port `Screenshot` readback to a Vulkan
  copy-to-buffer (replaces `glReadPixels`).
- **Exit:** clear→draw→tonemap→present works; screenshot saves.
- **Read:** `src/Engine/Screenshot.cpp`, `src/Engine/Shader.cpp`,
  `headers/Engine/Buffers.h`.

### Phase 3 — Mesh forward PBR + shadow mapping  ☐
- **Goal:** `office.scene` meshes render lit + shadowed (mono), matching GL.
- **How:** Vulkan vertex/index upload for `Mesh` (reuse Assimp import as-is);
  camera UBO; per-object push constants / dynamic UBO; materials via descriptor
  indexing; port `core/fragmentShader.glsl` + `shadowMapping*` +
  `simpleDepth*` + point-shadow (cubemap/`pointShadow*`) + skybox to Vulkan
  GLSL. `renderEye()` (`main.cpp:5335`, a ~600-line monolith) becomes a
  **pass-based renderer** (shadow pass → forward pass → skybox), not a straight
  port.
- **⚠ Design for multiview NOW (so Phase 7 stereo is additive, not a rewrite):**
  make the color/depth target a **layered** image and the camera UBO a
  **per-view array** indexed by `gl_ViewIndex` (`VK_KHR_multiview`). Mono = 1
  view/layer today; stereo just enables the 2nd view + a stereo swapchain later.
  Don't bake a single view/projection into shaders or the pass. Also set
  `GLM_FORCE_DEPTH_ZERO_TO_ONE` + fix inverted-Y on all projections here.
- **Exit:** lit + shadow-mapped models on screen, mono (1-view).
- **Read:** `src/Loaders/ModelLoader.cpp`, `headers/Loaders/ModelLoader.h`,
  `src/main.cpp:5335-5950` (renderEye body), `assets/shaders/core/*`.

### Phase 4 — Loading system → RHI upload  ☐
- **Goal:** all loaders push data through the RHI; nothing GL remains in loaders.
- **How:** keep every parser (Assimp, LASzip, HDF5/HighFive, PLY, XYZ, `.pcb`)
  untouched. Replace only `glGen*`/`glBufferData`/`glBufferSubData`/sparse
  `glBufferStorage` with `rhi::Buffer` + staging. **Preserve the progressive
  streaming contract**: `PointCloudStream` worker thread + `updateStreaming()`
  filling pre-allocated buffers per frame (now `VkBuffer`s).
- **Exit:** models + streamed clouds upload correctly (verified in Phase 5 draw).
- **Read:** `src/Loaders/PointCloudLoader.cpp` (streaming ~L200-240, ~L2770-2910),
  `headers/Engine/Data.h` (`PointCloud`, `ComputeBatch`),
  `src/Engine/OctreePointCloudManager.cpp`.

### Phase 5 — Compute point-cloud pipeline  ☐  ← **biggest rewrite**
- **Goal:** dense LAS/LAZ clouds render (standard + HQS) with clipping,
  composited against mesh depth.
- **How:** port the Schütz software rasterizer to Vulkan compute:
  `uint64_t` framebuffer SSBO + `atomicMin` via `shaderBufferInt64Atomics`
  (enabled in `Device` init; guaranteed at 1.2+); port `pointcloud_rasterize.comp`,
  HQS depth/colour/resolve,
  `pointcloud_color_lookup.comp`, and the fullscreen resolve writing depth —
  adding explicit `layout(set,binding)` + push constants. Replace
  `glMemoryBarrier(GL_ALL_BARRIER_BITS)` with sync2 barriers and the SSBO clear
  with `vkCmdFillBuffer`. Clip planes as a push-constant/UBO array.
- **Exit:** standard + HQS render, clipping works, depth-correct vs meshes.
- **Read:** `headers/Engine/ComputePointCloudRenderer.h` (full design in header),
  `src/Engine/ComputePointCloudRenderer.cpp`, `assets/shaders/core/pointcloud_*`.

### Phase 6 — Camera, cursors, tools, plugins  ☐
- **Goal:** interaction parity (cursors, gizmo, all tools) on Vulkan overlays.
- **How:** Camera reused almost verbatim — only abstract its 2 GL touch points
  (`glReadPixels` depth read + `glfwGetTime`). Add an **overlay renderer**
  (dynamic vertex buffer + line/tri pipelines) and expose it through
  `PluginContext` (replace `compileOverlayProgram`). Note **two** current overlay
  patterns to unify: cursors load GLSL files via `Engine::loadShader`; tools/
  gizmo/plugins compile inline GLSL via `compileOverlayProgram`. Port
  `SphereCursor`/`PlaneCursor`/`FragmentCursor` + `CursorPreview3D` (render-to-
  texture thumbnail), then `TransformGizmo`, `MeasurementTool`, `ClipPlaneTool`,
  `BrushTool`, `CrosshairPlugin`, `MeasurementPlugin`.
- **Exit:** cursors + tools + gizmo behave as today.
- **Read:** `headers/Core/Camera.h`, `src/Cursors/**`, `src/Tools/**`,
  `headers/Plugins/PluginContext.h`, `docs/PLUGINS.md`.

### Phase 7 — Stereo + XR  ☐
- **Goal:** quad-buffer stereo display and OpenXR HMD both render, **with a clean
  mono fallback on GPUs/displays that don't support stereo present** (the common
  case on consumer NVIDIA/AMD — see §4 compatibility target).
- **How:** implement `StereoTarget` with **runtime detection**: if the surface
  reports `maxImageArrayLayers >= 2`, use a native stereo swapchain
  (`imageArrayLayers = 2`); otherwise fall back to **mono present** (optionally
  side-by-side). The renderer stays multiview-capable either way. **Render both eyes in a single
  pass with `VK_KHR_multiview`** (view index selects per-eye projection/view via
  a UBO array) instead of porting `renderEye`'s twice-per-frame calls — the
  "view-independent passes once per frame" idea (`g_sharedPassesDone`) becomes
  unnecessary for the main pass since both views are drawn together. Rework
  `XRSession` to `XR_USE_GRAPHICS_API_VULKAN` (Vulkan swapchain `VkImage`s
  instead of GL textures); multiview also fits HMD stereo cleanly.
- **Exit:** stereo display + HMD both correct.
- **Read:** `src/Engine/XRSession.cpp`, `headers/Engine/XRSession.h`,
  `src/main.cpp:4680-5180` (eye-dispatch loop).

### Phase 8 — Decompose & cleanup  ☐
- **Goal:** professional, expandable structure; no GL left.
- **How:** retire globals in `main.cpp` into owning systems; split `GUI.cpp`
  panels into `Gui/panels/*`; delete GL scaffolding, GLAD, `opengl32.lib`;
  update `CLAUDE.md`, `README.md`, `docs/PLUGINS.md`. See target layout in
  `docs/VULKAN_MIGRATION.md §4.2`.

### Phase 9 — Re-add deferred features (LATER)  ☐
SSAO + Bloom as Vulkan compute; VCT, DDGI, Radiance via `VK_KHR_ray_query` / RT
pipelines. Out of scope until Phases 0–8 are solid.

---

## 2c. OpenGL-specific dependencies — swap / remove / keep — **EXECUTED 2026-07-02**
Status: GLAD deleted (glad.c + glad/KHR headers), `opengl32.lib` dropped from
all configs, `imgui_impl_opengl3*` deleted, runtime-GLSL `Engine::Shader`
deleted, volk vendored as the loader, `GLM_FORCE_DEPTH_ZERO_TO_ONE` (+
`GLFW_INCLUDE_NONE`, `VK_NO_PROTOTYPES`, `IMGUI_IMPL_VULKAN_USE_VOLK`) defined
project-wide, GL-era sources `ExcludedFromBuild`. Original matrix kept below
for reference:
The project links some **GL-specific** libraries that must go, keeps others, and
keeps one that needs **reconfiguration**. Full matrix + rationale in
`docs/VULKAN_MIGRATION.md §2.12`; the essentials:
- **REMOVE:** GLAD (`headers/libs/glad.c` + glad/`KHR` headers), `opengl32.lib`,
  ImGui `imgui_impl_opengl3.*`, and the runtime-GLSL `Engine::Shader`.
- **REPLACE loader:** add **volk** (or SDK loader) in GLAD's place; shaders → SPIR-V (shaderc).
- **KEEP (API-agnostic):** GLFW (init `GLFW_NO_API` + surface; drop the ~24
  GL-context calls incl. `GLFW_STEREO`/`glfwSwapBuffers`), Assimp, LASzip,
  HDF5/HighFive, stb_image, TDxNavLib, json, portable-file-dialogs.
- **KEEP + RECONFIGURE:** **GLM** — Vulkan clip space differs from GL. Define
  **`GLM_FORCE_DEPTH_ZERO_TO_ONE`** and fix the inverted Y on **every** projection
  matrix. **Currently NOT set — real correctness bug if missed.**
- **KEEP loader, SWAP binding:** OpenXR loader → `XR_USE_GRAPHICS_API_VULKAN`.
- ⚠️ **Sequencing:** once the window is `GLFW_NO_API` there is no GL context —
  any live GL call crashes. Port or stub GL systems as the context switches; don't
  run old-GL and new-Vulkan paths on the same window.

## 3. Next up (start here)
1. **Phase 2 — RHI hardening**: `Buffer`/`Texture` (VMA + staged uploads),
   `Pipeline` abstraction (SPIR-V + descriptor-layout spec + dynamic-rendering
   formats — consider SPIRV-Reflect), growable `DescriptorAllocator`, HDR
   tonemap pass replacing the Renderer's placeholder blit-to-swapchain,
   `Screenshot` readback. The triangle pass in `Renderer.cpp` shows the frame
   skeleton to build on (timeline sync, per-frame pools, layered target).
2. Continue into **Phase 3+** per the board.

Note for Phase 5: `shaderBufferInt64Atomics` + `shaderInt64` are **required and
enabled** in `rhi::Device` (fail-loud with a clear message). See §4 for the
spec-status correction — optional per spec, universal on modern NVIDIA/AMD.

---

## 4. Decisions & notes (fill in as you learn)
- **COMPATIBILITY TARGET (owner):** must run on **Windows 10 & 11** on **any modern
  NVIDIA or AMD GPU** — NOT a single known device. So: no exotic optional features
  in the required set; degrade gracefully; test both NVIDIA and AMD driver behaviour
  where possible. Target **Vulkan 1.3** (covers ~last-decade NVIDIA Kepler+/AMD GCN+
  on current drivers). Everything the required set needs is core 1.1–1.3 (see below).
- Vulkan version: **1.3** (dynamic rendering, sync2, timeline semaphores core).
- Memory: **VMA**. Shaders: GLSL → SPIR-V via **shaderc**, offline + runtime.
- int64 buffer atomics — **spec-claim CORRECTED (2026-07-02):**
  `shaderBufferInt64Atomics` is **NOT unconditionally mandatory** in core 1.2/1.3
  (it is promoted-optional from `VK_KHR_shader_atomic_int64`, and absent even
  from the `VP_KHR_roadmap_2022` profile — verified against Khronos
  Vulkan-Profiles). It **is** universally supported by modern NVIDIA/AMD Windows
  drivers, i.e. by the entire compatibility target. Consequence unchanged:
  `rhi::Device` **requires + enables** it (with `shaderInt64`) and fails loudly
  naming the missing feature. If an exotic GPU ever needs support, design a
  32-bit fallback before Phase 5.
- **Vulkan toolchain is fully VENDORED (hermetic build; no SDK install):**
  a fresh clone + MSVC builds as-is, locally and on CI. The CI workflow no
  longer installs the Vulkan SDK (drift-prone `latest` + undocumented
  stripdown). End users need only a Vulkan-capable driver; developers who want
  validation layers install them via the SDK/vkconfig (the app requests the
  layer only if present).
- **Added libraries (name — version — why):**
  - **Vulkan-Headers v1.4.350** — `dependencies/include/vulkan|vk_video` — C API headers (matches current LunarG SDK line).
  - **volk 1.4.350** — `headers/libs/volk` (volk.h/volk.c, MIT) — Vulkan loader replacing GLAD; device-level entry points loaded via `volkLoadDevice` (skips loader trampolines).
  - **VulkanMemoryAllocator v3.4.0** — `headers/libs/vma` (MIT) — all GPU memory; dynamic functions from volk (`VMA_DYNAMIC_VULKAN_FUNCTIONS=1`), impl unit `src/RHI/VmaUsage.cpp`.
  - **shaderc (shaderc_shared)** — Google CI VS2022 x64 release artifact 2026-06-30 (Apache-2.0) — runtime GLSL→SPIR-V in `rhi::ShaderCompiler`; headers `dependencies/include/shaderc`, `dependencies/lib/x64/shaderc_shared.lib`, `dependencies/bin/shaderc_shared.dll` (post-build copy).
  - **glslc.exe** (same artifact) — `dependencies/tools` — offline SPIR-V custom build step (`--target-env=vulkan1.3`) into `$(OutDir)assets\shaders_vk`.
  - **imgui_impl_vulkan @ v1.91.1-docking** — `headers/libs/imgui/backends` — render backend replacing `imgui_impl_opengl3`; volk mode (`IMGUI_IMPL_VULKAN_USE_VOLK`), dynamic rendering, multi-viewport.
  - Licenses collected under `dependencies/licenses/` + alongside vendored sources.
- **House rendering conventions (Renderer/Projection.h — enforced from Phase 1):**
  every projection comes from the `renderer::` factories: Vulkan **[0,1] depth**
  (`GLM_FORCE_DEPTH_ZERO_TO_ONE` project-wide), **REVERSE-Z** (near→1, far→0,
  `VK_COMPARE_OP_GREATER`, clear depth 0.0) for float-depth precision, and the
  **Y-flip baked into the projection** — which keeps the image upright and
  **preserves GL's winding**: CCW-authored meshes stay
  **`VK_FRONT_FACE_COUNTER_CLOCKWISE`** in every raster pipeline. ⚠ Corrected
  2026-07-03 — this bullet originally said `VK_FRONT_FACE_CLOCKWISE`, which
  back-culled everything (the first user run had no triangle). The front-face
  test runs in framebuffer space (as seen on screen); a projection-baked flip
  does not change on-screen orientation. Only the negative-viewport-height
  flip method toggles winding (it acts inside the viewport transform the test
  observes) — we do not use it. Matches vulkan-tutorial's Y-flip guidance.
  Do not hand-roll projections above the RHI.
- **ImGui Vulkan backend pointer-lifetime trap (bit us 2026-07-03):**
  `ImGui_ImplVulkan_InitInfo` is copied by value, but
  `PipelineRenderingCreateInfo.pColorAttachmentFormats` stays a raw pointer
  that the backend dereferences **lazily** — when the first dragged-out
  viewport creates the shared secondary-viewport pipeline. Pointing it at a
  stack local makes secondary OS windows render black (garbage/UNDEFINED
  format pipeline; main window unaffected because its pipeline is built during
  init). It now points at the long-lived `Application::imguiColorFormat_`.
- **WSI pacing tools (debug panel, added 2026-07-03):** per-frame wait
  breakdown (`slot` = CPU wait on the frame slot's previous GPU submission,
  `acquire`, `present`), a swapchain-recreation counter, and a **present-mode
  selector** (FIFO default = vsync/GL parity; Mailbox/Immediate for preference
  and for diagnosing driver-side FIFO pacing). `Swapchain::present` returns
  `PresentResult` and the app recreates on Suboptimal **only if the size
  actually changed** — a persistent-SUBOPTIMAL driver must not trap the loop
  in waitIdle+recreate (reads as a hard fps cap + laggy input).
- **Test GPU facts (RTX 3050 Ti Laptop, Optimus hybrid, driver Vulkan
  1.4.341):** `maxImageArrayLayers = 1` (mono present, as predicted for
  consumer GeForce); windowed 1920×1055 uncapped (Immediate) ≈ **600 fps —
  that is normal and present-path-bound** (DWM composition + dGPU→iGPU
  cross-adapter copy ≈ 1–1.5 ms/frame; the Phase-1 GPU work itself is
  ~0.1–0.3 ms). Do not read windowed hello-triangle fps as GPU capacity. A
  hard 30 fps (2× vblank) was seen under FIFO in the first run — recheck FIFO
  after the Suboptimal hardening; if it persists it is the hybrid-GPU FIFO
  path, and Mailbox is the tear-free alternative.
- **Swapchain format:** UNORM (`B8G8R8A8_UNORM` preferred) + `SRGB_NONLINEAR`,
  FIFO (vsync, matching the GL app's swap-interval-1). ImGui writes raw colors;
  scene gamma is handled by the Phase 2 tonemap, not the backbuffer format.
- **GLFW_INCLUDE_NONE** is defined project-wide: without it `glfw3.h` includes
  `<GL/gl.h>` (and thus `windows.h` + min/max macros) — found via local clang
  syntax-check before it could break MSVC.
- **gitignore trap (learned the hard way):** root `.gitignore` ignores `bin/`,
  `x64/`, `*.exe` *anywhere*, which silently dropped vendored binaries from the
  first Phase 0 commit (CI custom-build exit code 3 = glslc.exe missing).
  Negations for `dependencies/bin|lib/x64|tools` are now in `.gitignore`; when
  vendoring binaries, always verify with `git ls-files` afterwards.
- **Quad-buffer stereo: RUNTIME-DETECTED, not assumed.** Stereo *rendering* (2 layers,
  `VK_KHR_multiview`, core 1.1) is available everywhere. Stereo *presentation* to a
  3D display (swapchain `imageArrayLayers = 2`, needs surface `maxImageArrayLayers >= 2`)
  is generally exposed **only on workstation GPUs (Quadro / Radeon Pro) with a stereo
  display + driver stereo enabled** — consumer GeForce/Radeon usually do NOT expose it.
  → Detect at runtime; **fall back to mono present** (default on consumer GPUs), like the
  current GL app's stereo→mono fallback. Optionally offer side-by-side. The renderer
  stays multiview-capable regardless; only the present path is conditional.
- _(record: extensions/features actually found on the test GPUs, driver quirks, notes)_

---

## 4b. Continuous Integration
- Workflow: `.github/workflows/ci-vulkan-migration.yml` runs on pushes to
  `StereoVista-vulkan` **and `claude/**` session branches** (triggers fixed
  2026-07-02 — they pointed at a dead branch name and never fired) plus PRs to
  `StereoVista-vulkan`/`main`. It builds **Debug + Release x64** on
  `windows-latest` and does **not** publish releases (that's `msbuild.yml`,
  main-only).
- **No Vulkan SDK on CI anymore** — the toolchain is vendored (see §4); the
  SDK-install step was removed for speed and determinism.
- Compiler errors appear as **inline annotations** (MSVC problem matcher) and the
  full MSBuild **text + `.binlog`** are uploaded as artifacts (even on failure) —
  download them to see every error, or open the `.binlog` in the MSBuild
  Structured Log Viewer.
- **After pushing, check the CI result** — this repo has no local Windows build in
  the agent environment, so CI is the primary signal that the code compiles. Wire
  new Vulkan sources into `StereoVista.vcxproj`(+`.filters`) so CI exercises them.
- Tip: a Linux-side `clang++ -std=c++17 -fsyntax-only` against the vendored
  headers (defines: `VK_NO_PROTOTYPES GLFW_INCLUDE_NONE IMGUI_IMPL_VULKAN_USE_VOLK
  GLM_FORCE_DEPTH_ZERO_TO_ONE`) catches most errors before burning a CI round;
  `glslangValidator --target-env vulkan1.3` checks the shaders.

## 5. Verification checklist (the USER runs these — see §0b)
CI only compiles; the user runs each milestone on the stereo GPU and reports back.
At each phase boundary: get CI green, then give the user concrete run/verify steps
and wait for their result before marking the phase done. Capture before/after
screenshots where useful.

**Phase 1 (VERIFIED 2026-07-03)** — build `Release|x64` (or `Debug|x64` for
validation layers) from `StereoVista.sln`, run `bin\x64\<config>\StereoVista.exe`:
- [x] Window "StereoVista" opens; console shows `[vulkan] using GPU: …` and
      `surface maxImageArrayLayers = …` (2 = stereo-present capable)
- [x] RGB triangle sways on the dark viewport and never disappears mid-sway
      (vanishing = broken COUNTER_CLOCKWISE front-face convention)
- [x] Debug panel shows GPU/driver/Vulkan version/swapchain/stereo capability
- [x] Panel docks, undocks, and drags OUT of the main window (own OS window)
- [ ] Resize/maximize/minimize/restore work; exit is clean *(not explicitly
      re-tested after the fixes — spot-check in passing during Phase 2)*
- [ ] Debug build: no `[vulkan][error]` validation messages in the console
      *(validation layer not installed on the test machine; run once via
      vkconfig/SDK when convenient)*

Full app-level checklist (later phases):
- [ ] App launches, ImGui docks/undocks/drags-out
- [ ] `office.scene` models render lit + shadow-mapped
- [ ] A reference LAS/LAZ cloud renders (standard **and** HQS), streams in
- [ ] Clip planes affect meshes + clouds
- [ ] 3D cursors + `CursorPreview3D` thumbnail
- [ ] TransformGizmo, Measurement, ClipPlane, Brush tools + a plugin overlay
- [ ] Camera orbit/pan/zoom-to-cursor, standard views
- [ ] Quad-buffer stereo output; OpenXR HMD
- [ ] Screenshot + scene save/load + undo

---

## 6. Session log (append newest at top; keep entries short)
- **2026-07-03 — Phase 1 user verification: three bugs fixed, phase CLOSED.**
  First real run reported: no triangle, dragged-out ImGui windows black, hard
  30 fps. Root causes and fixes: (1) **the winding convention was inverted** —
  with the Y-flip baked into the projection, CCW-authored faces STAY
  counter-clockwise under Vulkan's framebuffer-space front-face test (only the
  negative-viewport-height flip method toggles winding). Pipeline switched to
  `VK_FRONT_FACE_COUNTER_CLOCKWISE`; Projection.h and §4 corrected — the
  2026-07-02 static-audit claim of a "validated" CLOCKWISE convention was
  wrong. (2) **Black secondary viewports**: dangling
  `PipelineRenderingCreateInfo.pColorAttachmentFormats` (stack local captured
  by the backend's by-value InitInfo copy, dereferenced lazily on first
  drag-out) → now a long-lived `Application` member (§4 gotcha). (3) **30 fps
  lock**: no structural fault found in the frame loop; hardened the
  Suboptimal-present path (recreate only when the size changed — a
  persistent-SUBOPTIMAL driver could otherwise trap the loop in
  waitIdle+recreate at exactly half refresh) and added diagnostics: per-frame
  wait breakdown (slot/acquire/present) + swapchain-recreation counter +
  present-mode selector (FIFO/Mailbox/Immediate) in the debug panel. Also
  enabled `separateDepthStencilLayouts` (mandatory-supported at 1.2+; required
  for the depth-only `DEPTH_ATTACHMENT_OPTIMAL` layout the renderer already
  used). **User re-verified**: triangle sways, drag-out viewports render,
  ~600 fps uncapped Immediate at 1920×1055 on the Optimus RTX 3050 Ti —
  assessed as normal/present-path-bound (§4 test-GPU facts). **Phase 1 done →
  next: Phase 2 (RHI hardening).**
- **2026-07-02 — Phase 1 static audit (same session, follow-up).** Re-verified
  the implementation without a GPU: CI green on Debug+Release x64 for the full
  head (both branches at the same commit); zero GL-context calls left in
  compiled code; vcxproj ⇄ filters item lists match exactly; reverse-Z +
  Y-flip projections validated numerically (near→1, far→0, view +y → NDC −y;
  infinite-far variant correct); `imgui_impl_vulkan` 1.91.1 confirmed to run
  secondary (dragged-out) viewports through the dynamic-rendering path; the
  ImGui GLFW backend chains input callbacks and does not touch our
  framebuffer-size callback; WSI semaphore lifecycle re-checked (per-slot
  acquire semaphores gated by the frame timeline, per-image present
  semaphores, early-out on OUT_OF_DATE without slot advance). Known
  deliberate gaps: app-level input callbacks (key/mouse/scroll/drop) return
  with the systems that consume them (Phase 6); fonts/ is not copied to the
  output dir (matches the old app); rendering pauses while the main window is
  minimized (matches the upstream ImGui Vulkan example). Added a migration
  banner to CLAUDE.md so future sessions aren't misled by the GL-era
  description. **Only remaining Phase 1 exit item: the user's visual run.**
- **2026-07-02 — Phase 0 done + Phase 1 implemented (first coding session).**
  Vendored the full Vulkan toolchain (Vulkan-Headers/volk 1.4.350, VMA 3.4.0,
  shaderc_shared + glslc — §4) and made the build hermetic (CI SDK install
  removed; CI triggers fixed — they pointed at a dead branch). Executed the §2c
  surgery: GLAD/`opengl32.lib`/ImGui-GL3/`Engine::Shader` deleted, deferred GI
  subsystems (VCT/DDGI/BVH/Bloom/SSAO + shaders) deleted, GL-era sources
  `ExcludedFromBuild` as in-tree reference, `GLM_FORCE_DEPTH_ZERO_TO_ONE` +
  `GLFW_INCLUDE_NONE` + `VK_NO_PROTOTYPES` project-wide. Built the Phase 1
  skeleton: `Platform::Window` (GLFW_NO_API), `rhi::Device` (fail-loud 1.3
  feature set incl. int64 buffer atomics; robust GPU pick), `rhi::Swapchain`
  (mono FIFO; `maxImageArrayLayers` stereo-present probe = Phase 7 seam),
  `renderer::Renderer` (2 FIF on one timeline semaphore, sync2, per-frame
  pools, **layered multiview scene target + per-view camera UBO array** —
  stereo will be additive), `rhi::ShaderCompiler` (.spv-or-shaderc),
  `app::Application` + ImGui 1.91.1 on `imgui_impl_vulkan` (docking +
  multi-viewport; project-local style/font files preserved, font rebuild
  routed through the Vulkan backend). House conventions established in
  `Renderer/Projection.h`: [0,1] depth + **reverse-Z** + Y-flip-in-projection
  (⇒ `VK_FRONT_FACE_CLOCKWISE`). Corrected the int64-atomics spec claim (§4).
  Gotchas recorded in §4 (gitignore binary trap, GLFW_INCLUDE_NONE).
  **Next:** user runs the build and verifies window/ImGui/triangle; then
  Phase 2 (RHI hardening: Buffer/Texture/Pipeline/DescriptorAllocator, HDR
  tonemap, screenshot readback).
- **2026-07-02 — compatibility target set.** Owner: app must run on Win10/11 on
  **any modern NVIDIA/AMD GPU**, not a specific device. Consequence: quad-buffer
  stereo *present* is workstation-GPU-only, so it's now **runtime-detected with a
  mono fallback** (multiview render path unaffected). Updated §4, Phase 7, and the
  design-doc decision table/goals/risks. Required feature set stays core 1.1–1.3.
- **2026-07-02 — working agreement settled (§0b).** Owner decisions: (1) deferred
  features (VCT/DDGI/Radiance/Bloom/SSAO) are **deleted now**, not kept as
  reference — git history covers Phase 9; ship only Shadow Mapping. (2) `main` is
  **feature-frozen** during the migration. (3) the **user verifies each phase** on
  the stereo GPU (CI only compiles). (4) **partial interim state is fine** as long
  as each phase compiles green. Updated deferred notes, verification section, and
  design-doc non-goals accordingly.
- **2026-07-02 — dropped spikes; start real migration.** Removed the throwaway
  Phase 0 spikes. Researched: `shaderBufferInt64Atomics` is **guaranteed at Vulkan
  1.2+** (core, not optional) and `VK_KHR_multiview` is core since 1.1, so the
  point-cloud-atomics and stereo risks are resolved by spec — they become normal
  feature-enablement in the real `Device` init (Phase 1, fail-loud if absent).
  Phase 0 is now pure toolchain/build setup (Vulkan SDK/VMA/shaderc/volk in, GL
  loader out, GLM depth flag). Phase 1 does real bootstrap. No PoC code.
- **2026-07-02 — order refinement (final planning pass).** Kept the phase order
  (it's dependency-sound) but made the renderer **multiview-aware from Phase 3**
  (layered target + per-view camera UBO array via `gl_ViewIndex`) so Phase 7
  stereo is additive, not a rewrite; folded the GLM clip-space fix into Phase 3;
  noted Phase 0 spikes are throwaway. Planning is considered complete — next
  session starts Phase 0 implementation.
- **2026-07-02 — dependency disposition.** Added an explicit GL-specific
  library swap/remove/keep matrix (new §2c here + `MIGRATION.md §2.12`). Verified
  against the .vcxproj: `opengl32.lib` linked (remove), GLAD vendored (remove →
  volk), ImGui GL3 backend (→ vulkan), ~24 GL-context GLFW calls to drop. Flagged
  the **GLM clip-space** issue: `GLM_FORCE_DEPTH_ZERO_TO_ONE` is NOT set and must
  be, plus inverted-Y on all projections. Noted the no-GL-context sequencing trap.
- **2026-07-02 — library policy.** Added guidance: prefer good libraries that
  greatly simplify/optimize (VMA, shaderc/glslang, volk, vk-bootstrap,
  SPIRV-Reflect); the agent must fully self-integrate them (download all headers/
  .lib/.dll/license into the vendoring layout, wire into the .vcxproj incl.
  post-build DLL copy, build green on CI — no manual install steps).
- **2026-07-02 — CI setup.** Added `.github/workflows/ci-vulkan-migration.yml`:
  Debug+Release x64 compile-check on `windows-latest` with the Vulkan SDK
  pre-installed, inline MSVC error annotations, and build logs (`.log`+`.binlog`)
  uploaded as artifacts. No release publishing. CI is the primary build signal
  since the agent environment can't compile the Windows/MSVC project.
- **2026-07-02 — planning (feedback pass).** Corrected the stereo approach:
  Vulkan supports quad-buffered stereo **natively** (swapchain
  `imageArrayLayers = 2` + single-pass `VK_KHR_multiview`) — better than GL's
  twice-per-frame `renderEye`; downgraded Spike B from "biggest unknown" to a
  caps validation (confirmed via Vulkan spec). Added the core philosophy to both
  docs: **native/optimized rewrite, no stale copy-paste, dig deeper / use the
  Internet when unsure, ask the user for product decisions.**
- **2026-07-02 — planning.** Deep-read the OpenGL codebase; authored
  `docs/VULKAN_MIGRATION.md` (design/rationale) and this living status file.
  Confirmed: `renderEye()` (main.cpp:5335) is a ~600-line monolith to decompose
  into passes; overlays use two GLSL patterns (file-loaded cursors vs inline-
  compiled tools/plugins); XR is bound to the GL/WGL context; int64-atomics
  check already present in the compute PC renderer; GLFW quad-buffer stereo with
  a mono fallback. No Vulkan code written yet. **Next:** Phase 0 spikes.
</content>
