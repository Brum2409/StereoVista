# StereoVista — OpenGL → Vulkan Migration Plan

> Status: **planning**. Branch: `claude/opengl-vulkan-migration-yqc277`.
> This document is the source of truth for the migration. It is intentionally
> detailed so the work can be picked up in independent phases.

---

## 1. Goals & Non‑Goals

### Goals
- Replace the OpenGL 4.6 backend with **Vulkan 1.3** while preserving the
  application's behaviour and feature set for everything *except* the advanced GI
  systems (see non‑goals).
- Port, in order: **rendering core → mesh/model rendering → loading system →
  compute point‑cloud pipeline (streaming + rasterizer + HQS) → camera → 3D
  cursors → tools → plugins → stereo/XR**.
- Use the migration as an opportunity to **decompose `main.cpp` (~9,800 lines)**
  and `GUI.cpp` (~8,800 lines) into a clean, layered, expandable architecture.
- Introduce a thin **Render Hardware Interface (RHI)** so high‑level systems
  never touch raw Vulkan, keeping the door open for later backends and for the
  advanced features we defer.

### Non‑Goals (deferred — will be re‑implemented natively in Vulkan later)
These are **removed from the first Vulkan build** and re‑added afterwards using
native Vulkan capabilities (compute / ray‑tracing pipelines):
- **Voxel Cone Tracing** (`Voxalizer`, `voxelization/*`, VCT shaders).
- **DDGI** (`DDGIVolume`, `ddgi*` shaders).
- **BVH ray‑traced Radiance** lighting + `BVHDebug` (will use `VK_KHR_ray_query` /
  ray‑tracing pipelines instead of the SSBO software BVH).
- **Heavy post FX**: Bloom/HDR bloom, SSAO. (A minimal HDR target + tonemap is
  kept so the image pipeline is correct; SSAO/Bloom come back as Vulkan compute
  passes.)

> The lighting mode enum stays, but only **Shadow Mapping** (direct light +
> shadow maps) ships in the first Vulkan milestone. VCT/Radiance are greyed out
> until re‑ported.

---

## 2. Current Architecture Inventory (what exists today)

Everything below is OpenGL‑bound unless noted. Line counts indicate effort.

### 2.1 Platform / context
- **`Engine::Window`** (`Window.cpp`) — GLFW init, **OpenGL 4.6 core** context,
  GLAD load, callbacks. Quad‑buffer stereo requested via `glfwWindowHint(GLFW_STEREO)`
  in `main.cpp:3500`.
- **`Engine::Input`** (`Input.cpp`) — GLFW callback fan‑out. Portable (no GL).

### 2.2 Rendering core (in `main.cpp`)
- **`renderEye()`** — the heart of the frame; called twice per frame for stereo
  (`GL_BACK_LEFT` / `GL_BACK_RIGHT`), once per XR eye (into an XR FBO), or once
  mono. Drives: shadow/DDGI shared passes (guarded by `g_sharedPassesDone`),
  SSAO geometry pass, skybox, model draw, point‑cloud composite, overlays,
  post‑chain.
- **HDR offscreen FBO** + tonemap/FXAA resolve, **shadow map FBO** (directional)
  and **depth cubemap FBO** (point lights).
- **`Engine::Shader`** (`Shader.cpp`) — GLSL program wrapper, `use()` +
  `setX()` uniform setters with a location cache. Compute‑shader constructor.
- **`Engine::Buffers`** — thin VAO/VBO/EBO helpers.
- **`Engine::BloomRenderer`, `Engine::SSAORenderer`** — FBO‑based post passes
  *(deferred)*.

### 2.3 Mesh / model system
- **`Engine::Model` / `Mesh` / `Texture`** (`ModelLoader.cpp`, 1,155 lines) —
  Assimp import (OBJ/FBX/GLTF…), PBR material fields, per‑mesh VAO/VBO/EBO,
  `Draw(Shader&)`. Procedural factories (cube/sphere/cylinder/plane/torus).
  **CPU import logic is portable; only GPU upload + `Draw` change.**

### 2.4 Point‑cloud pipeline (the crown jewel — mostly rewrite)
- **`Engine::ComputePointCloudRenderer`** (`ComputePointCloudRenderer.cpp`, 650
  lines + 222‑line header) — Schütz *compute software rasterizer*:
  - `uint64_t` framebuffer SSBO, `atomicMin` depth sort (needs
    `GL_ARB_gpu_shader_int64` + `GL_EXT/NV_shader_atomic_int64`).
  - Per‑batch frustum cull, 10/20/30‑bit packed coordinates (5 SSBOs per cloud).
  - **HQS** 3‑pass (depth → colour‑accumulate → resolve) for anti‑aliased dense
    clouds.
  - Clip‑plane support, per‑cloud colour lookup, fullscreen resolve writing
    `gl_FragDepth`.
  - Shaders: `pointcloud_rasterize.comp`, `pointcloud_hqs_depth.comp`,
    `pointcloud_hqs_color.comp`, `pointcloud_color_lookup.comp`,
    `pointcloud_resolve.*`, `pointcloud_hqs_resolve.frag`.
- **`OctreePointCloudManager`** (995 lines) — LOD/streaming octree (GL_POINTS
  fallback path + disk cache). CPU/octree logic portable; GL VBO bits change.
- **`PointCloudLoader`** (3,005 lines) — LAS/LAZ (LASzip), binary/ASCII PLY,
  HDF5 (HighFive), XYZ/TXT, native `.pcb`. **Progressive streaming**: worker
  thread fills pre‑allocated SSBOs via `glBufferSubData` / sparse
  `glBufferStorage` each frame (`updateStreaming()`). **Parsers are portable;
  the GPU staging/upload path is rewritten for Vulkan.**

### 2.5 Camera
- **`Camera`** (`Camera.h`, 792 lines) — almost pure `glm` math (orbit, pan,
  smooth scroll, animation, quaternion orientation). **Only GL touch points:**
  `glReadPixels` for centre‑depth and `glfwGetTime`. Trivial to abstract.

### 2.6 3D cursors
- **`CursorManager`** + `SphereCursor` / `PlaneCursor` / `FragmentCursor`
  (`Cursors/**`) — each owns GL programs + VAOs and draws an overlay; ray‑pick
  against scene depth. **`CursorPreview3D`** renders a cursor to an FBO for the
  GUI thumbnail.

### 2.7 Tools & plugins
- **Tools**: `BrushTool`, `ClipPlaneTool`, `MeasurementTool`, `TransformGizmo`
  — all do **immediate‑style overlay drawing** (dynamic line/tri VBOs + small
  shaders). Must become proper vertex‑buffer + pipeline draws in Vulkan.
- **Plugin system** (`Plugins/**`, `docs/PLUGINS.md`) — `Plugin` subclasses with
  `onRenderViewport(eye)`, `onRenderUI`, input hooks; `PluginContext` exposes a
  `compileOverlayProgram` GL helper. **`PluginContext` is the seam** — reshape
  it to expose an RHI overlay API instead of GL program handles.

### 2.8 GUI
- **ImGui docking + multi‑viewport**, `imgui_impl_opengl3` + `imgui_impl_glfw`.
  Swap the render backend to **`imgui_impl_vulkan`**; keep `imgui_impl_glfw`.
  Project‑local `imgui_style*.cpp/h`, `imgui_incl.h`, `IconsFontAwesome5.h`
  must be preserved.

### 2.9 Stereo & XR
- **Quad‑buffer stereo** via GLFW `GL_STEREO` + `GL_BACK_LEFT/RIGHT`.
- **`XRSession`** (`XRSession.cpp`, 687 lines) — OpenXR bound to the **GL**
  context (`XR_USE_GRAPHICS_API_OPENGL`), per‑eye GL swapchain textures. Must be
  reworked to `XR_USE_GRAPHICS_API_VULKAN`.

### 2.10 Support systems (mostly CPU / portable)
- `SceneManager` (JSON scene I/O), `UndoManager`, `SnapshotManager`,
  `ShortcutManager`, `SpaceMouseInput` / `ThreeDConnexionSync` (SpaceMouse),
  `CursorSynchronizer`, `Screenshot` (`glReadPixels` → rewrite the readback),
  `StbImageImpl`.

### 2.11 GL footprint
Raw GL calls appear in **~33 files**. The vast majority collapse into the RHI;
only the point‑cloud renderer, post passes, and overlays carry real per‑backend
logic.

---

## 3. Key Vulkan Decisions (resolve these before coding)

| # | Topic | Recommendation | Why |
|---|-------|----------------|-----|
| 1 | Vulkan version | **1.3** (dynamic rendering, sync2, timeline semaphores core) | Removes render‑pass/framebuffer boilerplate; cleaner code |
| 2 | Memory management | **VMA** (AMD Vulkan Memory Allocator) | Do not hand‑roll allocation; industry standard |
| 3 | Shaders | Keep **GLSL**, compile to **SPIR‑V** with **shaderc**/glslang; runtime + offline | Reuse existing GLSL with minimal edits (explicit `layout(set,binding)`, push constants) |
| 4 | Descriptors | **Descriptor indexing / partially‑bound** (bindless‑lite) for textures; per‑frame descriptor pools | Scales for many models/textures; simplifies material binding |
| 5 | 64‑bit point‑cloud atomics | `VK_KHR_shader_atomic_int64` + `shaderBufferInt64Atomics` (core 1.2), SPIR‑V `Int64Atomics`, GLSL `GL_EXT_shader_atomic_int64` | Direct 1:1 map of the existing rasterizer; verify device support at startup, feature‑gate |
| 6 | Frames in flight | **2**, timeline semaphores, per‑frame command pool + descriptor pool | Standard double‑buffering |
| 7 | Quad‑buffer stereo | **Hardest problem.** Use `VK_KHR_display` stereo present *or* NVIDIA `VK_KHR_swapchain` `imageArrayLayers=2` where supported; fall back to two swapchains / side‑by‑side. Abstract behind a `StereoTarget` so callers stay unaware | Desktop quad‑buffer stereo is vendor‑specific; must be isolated early as a spike |
| 8 | XR | OpenXR with `XR_USE_GRAPHICS_API_VULKAN`, Vulkan swapchain images as `VkImage` | Required; rework `XRSession` |
| 9 | ImGui | `imgui_impl_vulkan` (+ existing glfw backend) | Official, supports multi‑viewport |
| 10 | Windowing | Keep **GLFW** (no GL context: `GLFW_NO_API`), create `VkSurfaceKHR` | Minimal disruption to input/callbacks |
| 11 | Build | Add Vulkan SDK, VMA, shaderc to `dependencies/`; wire into `.vcxproj`; SPIR‑V build step | Windows/MSVC only, matches project |
| 12 | Validation | Validation layers + `VK_EXT_debug_utils` in Debug builds | Non‑negotiable for a port of this size |

---

## 4. Target Architecture

### 4.1 Layering (new)
```
┌──────────────────────────────────────────────────────────────┐
│ Application            App lifecycle, main loop, wiring        │  <- replaces main.cpp
├──────────────────────────────────────────────────────────────┤
│ Systems         SceneRenderer · PointCloudSystem · CursorSys   │
│                 · ToolSystem · PluginHost · GuiSystem · XR      │
├──────────────────────────────────────────────────────────────┤
│ Renderer        FrameGraph‑lite · passes (shadow, forward,     │
│                 pointcloud, overlay, tonemap) · StereoTarget    │
├──────────────────────────────────────────────────────────────┤
│ RHI (Vulkan)    Device · Swapchain · Buffer · Image · Pipeline │  <- ONLY layer that
│                 · DescriptorSet · CommandContext · Shader(SPIR‑V)│     touches Vulkan
├──────────────────────────────────────────────────────────────┤
│ Platform        Window (GLFW, no API) · Input · Clock          │
└──────────────────────────────────────────────────────────────┘
```

Rule: **nothing above the RHI includes `<vulkan.h>`.** Systems speak in RHI
handles (`rhi::Buffer`, `rhi::Texture`, `rhi::Pipeline`, `rhi::CommandContext`).

### 4.2 Proposed source layout
```
src/
  App/            Application.cpp, main.cpp (thin: build App, run)
  Platform/       Window, Input, Clock            (moved from Engine)
  RHI/            Device, Swapchain, Buffer, Image, Pipeline,
                  DescriptorAllocator, ShaderModule, CommandContext, Upload,
                  vk_mem_alloc glue, debug utils
  Renderer/       Renderer, FrameContext, StereoTarget, passes/*, MaterialSystem
  Scene/          (existing Core/SceneManager, Undo, Snapshot — unchanged CPU)
  PointCloud/     ComputePointCloudRenderer (Vulkan), streaming upload
  Loaders/        Model + PointCloud parsers (unchanged) + Vulkan upload adapters
  Cursors/ Tools/ Plugins/   (logic reused; draw paths reworked onto RHI overlay)
  Gui/            GuiSystem (imgui_impl_vulkan), panels split out of GUI.cpp
```
`main.cpp` shrinks to a few dozen lines. The current global soup in `main.cpp`
(sun, lights, shadow FBOs, BVH/DDGI buffers, plugin context…) is redistributed
into owning systems.

### 4.3 RHI surface (minimum viable)
- `Device` (instance, physical/logical device, queues, VMA, feature detection).
- `Swapchain` / `StereoTarget` (present, resize, per‑eye targets).
- `Buffer`, `Image`/`Texture` (VMA‑backed; usage flags; staged uploads).
- `Pipeline` (graphics + compute; created from SPIR‑V + a small descriptor
  layout spec; dynamic rendering formats).
- `DescriptorAllocator` (growable pools, per‑frame reset).
- `CommandContext` (records a frame; `bindPipeline/bindDescriptors/draw/dispatch/
  barrier/beginRendering`).
- `ShaderCompiler` (GLSL→SPIR‑V via shaderc, with disk cache + hot reload in Debug).

---

## 5. Phased Migration Plan (ordered)

Each phase should compile, run, and be visually verifiable before the next. The
ordering front‑loads the risky spikes (bootstrap, stereo, int64 atomics).

### Phase 0 — Setup & spikes (foundation)
- New branch (this one). Add Vulkan SDK, **VMA**, **shaderc/glslang** to
  `dependencies/`; update `.vcxproj`/`.filters`; add a SPIR‑V build step.
- **Spike A:** headless device‑capability probe — confirm on the target GPU:
  `shaderBufferInt64Atomics`, descriptor indexing, dynamic rendering, timeline
  semaphores, and the **stereo present** path. Record results here.
- **Spike B:** quad‑buffer stereo present proof‑of‑concept (clear left eye red /
  right eye blue on the stereo display). This de‑risks the single scariest item.
- Deliverable: `docs/VULKAN_MIGRATION.md` (this file) + capability report.

### Phase 1 — Core bootstrap + new App skeleton
- `Platform::Window` with `GLFW_NO_API` + `VkSurfaceKHR`; keep input callbacks.
- RHI `Device` + `Swapchain`; frames‑in‑flight; command/descriptor pools.
- `Application` class owning the loop (poll → update → render → present).
- **Hello‑triangle** through the RHI + `ShaderCompiler`.
- **ImGui on Vulkan** (`imgui_impl_vulkan`), docking + multi‑viewport, existing
  style files preserved. GUI renders even before the scene does.
- Exit criterion: window opens, ImGui panels dock/undock, triangle draws.

### Phase 2 — RHI hardening
- Finalise `Buffer`/`Texture` with staged uploads, `Pipeline` cache,
  `DescriptorAllocator`, barriers/sync2, dynamic‑rendering render targets, HDR
  offscreen color target + depth, and a **tonemap** resolve pass.
- Port `Screenshot` readback (Vulkan copy‑to‑buffer instead of `glReadPixels`).

### Phase 3 — Mesh/model forward rendering (Shadow‑Mapping mode only)
- Vulkan upload for `Mesh` (vertex/index buffers) — reuse Assimp import as‑is.
- Camera UBO; per‑object push constants / dynamic UBO; material via descriptor
  indexing (albedo/normal/…); **forward PBR** pipeline (port
  `fragmentShader.glsl` / `shadowMapping*` to Vulkan GLSL).
- **Directional shadow map** + **point‑light depth cubemap** passes.
- Skybox pass (equirect→cubemap or sample equirect directly).
- Exit criterion: `office.scene` models render lit + shadowed, mono, matching GL.

### Phase 4 — Loading system integration
- Route all loaders (`ModelLoader`, `PointCloudLoader`) through RHI upload.
- Keep parsers untouched; replace only `glGen*/glBufferData/glBufferSubData`
  with `rhi::Buffer` + staging. Preserve the **progressive streaming** contract
  (`PointCloudStream`, `updateStreaming()`), now filling `VkBuffer`s.

### Phase 5 — Compute point‑cloud pipeline (largest rewrite)
- Port the Schütz rasterizer to a **Vulkan compute pipeline**:
  - `uint64_t` framebuffer SSBO via `VK_KHR_shader_atomic_int64` (gate on Spike A;
    provide a documented fallback if a target GPU lacks it).
  - Port `pointcloud_rasterize.comp` (explicit `set/binding`, push constants for
    MVP/imageSize/cloudID/splat), the **HQS** depth/colour/resolve passes, the
    per‑cloud colour‑lookup pass, and the fullscreen **resolve** writing depth.
  - Barriers replace `glMemoryBarrier(GL_ALL_BARRIER_BITS)`; clear‑buffer via
    `vkCmdFillBuffer`.
  - Clip planes as push‑constant/UBO array.
- Wire streaming buffers (Phase 4) into per‑batch SSBO bindings.
- Exit criterion: dense LAS/LAZ cloud renders (standard + HQS) with clipping,
  composited correctly against mesh depth.

### Phase 6 — Camera, cursors, tools, plugins (overlays)
- Camera: abstract the depth read (RHI) + clock; otherwise reuse verbatim.
- Add an **overlay renderer** (dynamic vertex buffer + line/tri pipelines) to the
  RHI‑backed `PluginContext` (replace `compileOverlayProgram`).
- Port `SphereCursor`/`PlaneCursor`/`FragmentCursor` + `CursorPreview3D`
  (render‑to‑texture for the GUI thumbnail).
- Port `TransformGizmo`, `MeasurementTool`, `ClipPlaneTool`, `BrushTool`,
  `CrosshairPlugin`, `MeasurementPlugin` onto the overlay renderer.
- Exit criterion: cursors + all tools + gizmo behave as today.

### Phase 7 — Stereo & XR
- Implement `StereoTarget` for quad‑buffer stereo (from Spike B) and mono; make
  `renderEye`'s successor a per‑view loop over the stereo target.
- Rework `XRSession` to `XR_USE_GRAPHICS_API_VULKAN` (Vulkan swapchain images).
- Exit criterion: quad‑buffer stereo display + OpenXR HMD both render.

### Phase 8 — main.cpp decomposition & cleanup
- Retire the last globals; move state into owning systems; delete GL scaffolding.
- Split `GUI.cpp` panels into files under `Gui/panels/` (can start earlier,
  finishes here).
- Update `CLAUDE.md`, `README.md`, `docs/PLUGINS.md` for the Vulkan architecture.

### Phase 9 (later, out of scope now) — re‑add deferred features natively
- SSAO + Bloom as Vulkan compute passes.
- Voxel Cone Tracing, DDGI, and Radiance via `VK_KHR_ray_query` / RT pipelines.

---

## 6. Reuse vs Rewrite (quick reference)

| System | Verdict | Notes |
|--------|---------|-------|
| SceneManager / Undo / Snapshot / Shortcuts / SpaceMouse | **Reuse as‑is** | CPU/JSON only |
| Model/PointCloud **parsers** | **Reuse** | Swap GPU upload only |
| Camera | **Reuse** | Abstract 2 GL calls |
| Cursors / Tools / Plugins **logic** | **Reuse logic, rewrite draw** | Onto overlay RHI |
| `Engine::Shader` / `Buffers` | **Replace** | Become RHI `ShaderModule` / `Buffer` |
| ComputePointCloudRenderer | **Rewrite** | Vulkan compute + int64 atomics |
| BloomRenderer / SSAORenderer | **Defer**, then rewrite | Vulkan compute later |
| Voxelizer / DDGIVolume / BVH / BVHDebug | **Defer**, then native Vulkan | RT/compute later |
| Window / Input | **Rework** | GLFW no‑API + surface |
| XRSession | **Rework** | Vulkan graphics binding |
| GUI backend | **Replace** | imgui_impl_vulkan |
| main.cpp | **Rewrite/decompose** | Into App + systems |

---

## 7. Risks & Mitigations
- **Quad‑buffer stereo in Vulkan is vendor‑specific.** → Spike B in Phase 0;
  isolate behind `StereoTarget`; keep side‑by‑side fallback.
- **int64 atomics availability.** → Spike A gate; document fallback (e.g. 32‑bit
  depth + separate index, or split‑atomic) before committing the rewrite.
- **Scope creep from GI systems.** → Explicitly deferred; feature‑gate the
  lighting‑mode UI so only Shadow Mapping is selectable in the first Vulkan build.
- **Two huge files (`main.cpp`, `GUI.cpp`).** → Decompose incrementally, not in
  one commit; land the new `Application` skeleton early (Phase 1) and migrate
  behaviour into it phase by phase.
- **Regression risk (no test suite).** → Keep a manual verification checklist per
  phase (screenshots of `office.scene`, a reference LAS cloud, each tool).

---

## 8. Tooling / dependencies to add
- Vulkan SDK (headers, `vulkan-1.lib`, validation layers).
- **VMA** (`vk_mem_alloc.h`) — vendored in `headers/libs/`.
- **shaderc** (or glslang) for GLSL→SPIR‑V, plus an offline SPIR‑V build step.
- OpenXR already vendored (`dependencies/include/openxr`) — reuse loader,
  switch graphics binding.
- `.vcxproj` / `.filters`: add new folders, SPIR‑V custom build, link changes
  (`vulkan-1.lib`, shaderc); drop `opengl32.lib`/GLAD once GL is fully removed.

---

## 9. Immediate next steps
1. Land this plan (done).
2. Phase 0 Spike A (capability probe) + Spike B (stereo present PoC) — these two
   decide items #5 and #7 above and unblock everything else.
3. Stand up the `Application` + RHI `Device`/`Swapchain` + ImGui‑Vulkan
   hello‑triangle (Phase 1).
</content>
</invoke>
