# StereoVista — OpenGL → Vulkan Migration Plan (design & rationale)

> Status: **in progress** (Phase 0 done; Phase 1 code+CI done, user verify
> pending). Branch: `StereoVista-vulkan`.
> This document holds the **design rationale** (RHI layering, decisions,
> per-system reuse/rewrite table). For **live progress** — what's done, what's
> next, and the session log — see **`docs/VULKAN_MIGRATION_STATUS.md`**, which is
> the file every new session should read first. This plan is intentionally
> detailed but **not binding**: if a better approach emerges, take it and record
> the change in the status file.

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

### Compatibility target
Must run on **Windows 10 & 11** on **any modern NVIDIA or AMD GPU** — not a single
known device. Target **Vulkan 1.3**; keep the required feature set to things that
are core/broadly available (see §3 #5/#7 and status §4); **degrade gracefully**
(esp. stereo present, which is workstation‑GPU‑only) and validate on both vendors.

### Guiding principles (apply to every phase)
- **Rewrite better, don't translate.** This is a chance to modernize. Produce
  **deeply optimized, idiomatic, native Vulkan** — single‑pass multiview stereo,
  bindless/descriptor‑indexed materials, dynamic rendering, timeline semaphores,
  proper sync2 barriers, VMA suballocation, GPU‑driven / indirect draws where
  they help. The OpenGL code is the reference for **behaviour**, not for **how**.
  **No stale copy‑paste.** A large rewrite or a change of planned direction is
  acceptable — expected, even — when it yields a better Vulkan design.
- **When unsure, dig deeper — don't guess.** Read more of the source, and use the
  **Internet** (Khronos Vulkan spec/registry, Vulkan‑Samples, vendor docs) to
  confirm API/extension behaviour and best practice. If a decision is the user's
  to make (scope/priorities/trade‑offs), **ask the user** a concrete question.

### Non‑Goals (deferred — DELETED now, re‑implemented natively in Vulkan later)
Per the owner's decision (status §0b), these are **deleted from the tree** as the
rewrite proceeds (git history is the reference) and re‑implemented afterwards using
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

## 2.12 Dependency / library disposition (OpenGL‑specific vs. keep)

Not every dependency is OpenGL‑specific. Some are **removed**, some are **kept**,
some are **kept but reconfigured**. Verify against `StereoVista.vcxproj` before
touching — current state confirmed 2026‑07‑02.

| Dependency | Role | Disposition |
|---|---|---|
| **GLAD** (`headers/libs/glad.c`, `dependencies/include/glad`, `KHR/khrplatform.h`) | GL function loader | **REMOVE.** Replace with a Vulkan loader — **volk** (recommended) or the SDK static loader. Delete glad.c + glad/KHR headers and the `<ClCompile Include="…glad.c">`. |
| **`opengl32.lib`** | GL lib | **REMOVE** from `<AdditionalDependencies>` (both Debug+Release). |
| **ImGui GL3 backend** (`imgui_impl_opengl3.*`) | GUI render backend | **REMOVE.** Replace with `imgui_impl_vulkan`. **Keep** `imgui_impl_glfw`. |
| **`Engine::Shader`** (runtime `glCompileShader` of GLSL) | shader compile | **REPLACE.** Compile GLSL→SPIR‑V (shaderc) into an RHI `ShaderModule`; GLSL sources gain explicit `layout(set,binding)` + push constants. |
| **GLFW** (`glfw3.lib` / `glfw3_mt.lib`) | windowing + input | **KEEP** — API‑agnostic. Init with `GLFW_NO_API`, create the surface via `glfwCreateWindowSurface`. **Drop** the ~24 GL‑context calls: `GLFW_CONTEXT_VERSION_*`, `GLFW_OPENGL_PROFILE`, **`GLFW_STEREO`** (Vulkan stereo is a swapchain property, not a pixel format), `glfwMakeContextCurrent`, `glfwSwapBuffers` (→ `vkQueuePresentKHR`), `glfwSwapInterval` (→ present mode), `glfwGetProcAddress` for GL. |
| **GLM** | math | **KEEP but reconfigure.** Vulkan clip space differs from GL: **define `GLM_FORCE_DEPTH_ZERO_TO_ONE`** (Vulkan depth is `[0,1]`, not `[-1,1]`) and handle the **inverted Y** (negate `proj[1][1]` or flip the viewport). Audit *every* projection matrix (main, shadow, point‑cloud, cursors, XR). **Not currently set — this is a real correctness item, not cosmetic.** |
| **OpenXR loader** (`openxr_loader.lib`) | VR | **KEEP loader; SWAP graphics binding.** GL → Vulkan: `XR_USE_GRAPHICS_API_VULKAN`, `XR_KHR_vulkan_enable2`, swapchain images as `VkImage`. |
| Assimp, LASzip, HDF5+HighFive, stb_image, TDxNavLib, nlohmann/json, portable‑file‑dialogs | loaders / input / util | **KEEP** — all graphics‑API‑agnostic. Only the *GPU upload* side of the loaders changes (Phase 4). |

**New dependencies to add** (self‑integrate per §7b): Vulkan SDK, **VMA**,
**shaderc/glslang**, and optionally **volk**, **vk‑bootstrap**, **SPIRV‑Reflect**.

**Sequencing caveat:** the moment the window is created with `GLFW_NO_API` there
is **no GL context**, so any still‑live GL call crashes. GL‑dependent systems must
be **ported or temporarily stubbed** as the context switches — you can't run the
old `renderEye` GL path and the new Vulkan path against the same window. Keep old
GL files compiling only until their system is ported, then delete them.

## 3. Key Vulkan Decisions (resolve these before coding)

| # | Topic | Recommendation | Why |
|---|-------|----------------|-----|
| 1 | Vulkan version | **1.3** (dynamic rendering, sync2, timeline semaphores core) | Removes render‑pass/framebuffer boilerplate; cleaner code |
| 2 | Memory management | **VMA** (AMD Vulkan Memory Allocator) | Do not hand‑roll allocation; industry standard |
| 3 | Shaders | Keep **GLSL**, compile to **SPIR‑V** with **shaderc**/glslang; runtime + offline | Reuse existing GLSL with minimal edits (explicit `layout(set,binding)`, push constants) |
| 4 | Descriptors | **Descriptor indexing / partially‑bound** (bindless‑lite) for textures; per‑frame descriptor pools | Scales for many models/textures; simplifies material binding |
| 5 | 64‑bit point‑cloud atomics | `VK_KHR_shader_atomic_int64` + `shaderBufferInt64Atomics` (core 1.2), SPIR‑V `Int64Atomics`, GLSL `GL_EXT_shader_atomic_int64` | Direct 1:1 map of the existing rasterizer; verify device support at startup, feature‑gate |
| 6 | Frames in flight | **2**, timeline semaphores, per‑frame command pool + descriptor pool | Standard double‑buffering |
| 7 | Quad‑buffer stereo | **Runtime‑detected, not assumed.** Stereo *rendering* (`VK_KHR_multiview`, core 1.1) is always available. Stereo *present* (swapchain `imageArrayLayers = 2`, needs surface `maxImageArrayLayers >= 2`) is generally **workstation‑GPU‑only** (Quadro/Radeon Pro + stereo display); consumer GeForce/Radeon usually don't expose it. Abstract behind `StereoTarget`: use the stereo swapchain when present, else **fall back to mono** (optionally side‑by‑side) — mirroring the GL app's stereo→mono fallback | Compat target is *any* modern NVIDIA/AMD GPU on Win10/11, so stereo present must be optional; multiview single‑pass render is still cleaner/faster than GL's twice‑per‑frame `renderEye` |
| 8 | XR | OpenXR with `XR_USE_GRAPHICS_API_VULKAN`, Vulkan swapchain images as `VkImage` | Required; rework `XRSession` |
| 9 | ImGui | `imgui_impl_vulkan` (+ existing glfw backend) | Official, supports multi‑viewport |
| 10 | Windowing | Keep **GLFW** (no GL context: `GLFW_NO_API`), create `VkSurfaceKHR` | Minimal disruption to input/callbacks |
| 11 | Build | Add Vulkan SDK, VMA, shaderc to `dependencies/`; wire into `.vcxproj`; SPIR‑V build step | Windows/MSVC only, matches project |
| 12 | Validation | Validation layers + `VK_EXT_debug_utils` in Debug builds | Non‑negotiable for a port of this size |
| 13 | GLM clip space | `GLM_FORCE_DEPTH_ZERO_TO_ONE` globally + handle inverted‑Y (flip `proj[1][1]` or viewport) | Vulkan NDC ≠ GL NDC; without this, depth test + culling are wrong everywhere. Not currently set |
| 14 | Vulkan loader | **volk** (or SDK static loader) replacing GLAD | Faster dispatch, per‑device function pointers; removes the last GL loader |

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

### Phase 0 — Toolchain & build setup (no spikes)
- **Start the real migration directly — no throwaway spikes.** The two risks the
  spikes targeted are resolved by the Vulkan spec and become normal Phase‑1 code:
  `shaderBufferInt64Atomics` is **guaranteed on any Vulkan 1.2+ device** (core,
  non‑optional — only the *shared* int64 variant is optional), and quad‑buffered
  stereo (`imageArrayLayers = 2`) + `VK_KHR_multiview` (core since 1.1) are
  exercised for real when the swapchain (Phase 1) and stereo present (Phase 7)
  are built. Target **Vulkan 1.3**; detect + enable these features in `Device`
  init and fail loudly if absent.
- Self‑integrate (per §7b) **Vulkan SDK**, **VMA**, **shaderc/glslang**, **volk**
  into `StereoVista.vcxproj`/`.filters`; add a GLSL→SPIR‑V build step; validation
  layers in Debug. Do the §2.12 dependency surgery (remove GLAD/`opengl32.lib`/
  ImGui‑GL3; add volk; define `GLM_FORCE_DEPTH_ZERO_TO_ONE`).
- Deliverable: CI green with the Vulkan toolchain linked and the GL loader gone.

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
  - `uint64_t` framebuffer SSBO via `shaderBufferInt64Atomics` (enabled in
    `Device` init; guaranteed at Vulkan 1.2+).
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
- Implement `StereoTarget` for native quad‑buffer stereo (`imageArrayLayers = 2`)
  and mono; drive both eyes single‑pass via `VK_KHR_multiview` (the renderer is
  already multiview‑aware from Phase 3).
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

## 6b. Native-Vulkan Playbook — do it *better* than GL (suggestions, not mandates)

> **Purpose.** The rewrite exists to make these systems *cleaner, faster,
> expandable, and idiomatically Vulkan* — not to translate GL line-by-line. This
> section is the shared reference the per-phase agents cite; it is **nudges, not
> rules**. If a simpler path fits a phase, take it and record why. Grounded in
> current best practice: LunarG "So you want to write a Vulkan Renderer in 2025"
> (Vulkanised 2025), the Khronos **Vulkan-Guide**, **Vulkan-Samples**, and the
> Schütz et al. 2022 point-cloud paper. Per-phase pointers live in
> `VULKAN_MIGRATION_STATUS.md §2` and reference the letters/numbers below.

### A. Cross-cutting practices — adopt early (cheap now, expensive to retrofit)

1. **Scalar block layout** (`GL_EXT_scalar_block_layout`, core 1.2 — **already
   enabled at the device level**, `Device.cpp` `enable12.scalarBlockLayout`, with a
   graceful fall-off if a driver lacks it). LunarG's explicit recommendation over
   std140/std430: `layout(scalar)` uses C-like rules, so a **single POD header shared
   by C++ and GLSL** matches 1:1 with no vec4-padding gymnastics. So the remaining
   work is purely shader-side: adopt for the material/camera/light/draw structs in
   Phase 3 (retrofitting every shader later is costly). The current `ViewUniforms`
   (`Renderer.h`) is std140 — fine for the demo, but scalar is the target going forward.
2. **Buffer Device Address + `buffer_reference`** (`bufferDeviceAddress`, already
   required/enabled in `rhi::Device`). Pass GPU pointers via push constants instead of
   binding many SSBOs; combine with scalar layout. Kills descriptor churn for
   per-object / per-batch / GPU-driven data — a direct win for the point-cloud
   renderer's *5 SSBOs bound per cloud* and for any indirect-draw path.
3. **Bindless / descriptor indexing** (core 1.2; the `Pipeline` builder already
   supports `bindingOverride` with `PARTIALLY_BOUND` / `UPDATE_AFTER_BIND` /
   `VARIABLE_COUNT`). One large texture array bound once per frame; materials store
   *indices*, not descriptors. This is the Phase 3 material path — the GL
   `material.sampler2D textures[16]`-per-draw model (§C.8) is exactly what it replaces.
4. **Dynamic rendering** — **DONE**; keep it (no `VkRenderPass`/`VkFramebuffer`). If a
   later deferred/read-within-pass need appears, `VK_KHR_dynamic_rendering_local_read`
   (1.4) is the tiler-friendly answer, but desktop-first means it is not needed now.
5. **sync2 + timeline semaphores** — **DONE**; keep the one-timeline-paces-the-CPU
   pattern. Add a second timeline + an async-compute queue only when point-cloud / GI
   compute justifies overlap (§B).
6. **Extended dynamic state** (`VK_EXT_extended_dynamic_state` 1/2/3; EDS1 core 1.3).
   Make cull mode, front face, depth compare, topology, and blend *dynamic* to collapse
   pipeline permutations — fewer objects, less first-use stutter. The builder already
   makes viewport/scissor dynamic; widen this as passes multiply.
7. **Vulkan-Profiles** (`VP_KHR_roadmap_2024` + Profiles-Library). LunarG's recommended
   way to *pick and validate* the required feature/extension set. Consider expressing
   `Device`'s hard-requirement list as a profile JSON (single source of truth, testable)
   rather than a hand-maintained C++ list. Optional but tidy.
8. **GPU-driven rendering** (when draw/instance counts grow): `vkCmdDrawIndexedIndirectCount`
   + a compute cull pass that writes draw commands *and* the count, with
   `shaderDrawParameters` (`gl_DrawID`/`BaseInstance`, core 1.1 — **already enabled** in
   `Device.cpp`, with a graceful fall-off) indexing per-draw data from one big buffer.
   Turns "N draws + N binds" into one indirect draw. Great fit for many models, instanced
   cursors, and batched tool overlays.
9. **`VK_EXT_device_generated_commands`** (advanced, optional): full GPU-side command
   generation (binds, push constants, draws, dispatch, RT, mesh). Only if a later phase
   wants Nanite-style autonomy — not for the first milestone.
10. **Shader language: Slang is now first-class** (Khronos-hosted since late 2024,
    shipping in the Vulkan SDK; Vulkan-Samples ship Slang variants). Modules, generics,
    interfaces, and cleaner bindless ergonomics, one source → SPIR-V. We already compile
    GLSL→SPIR-V via shaderc. **Keep GLSL for the mechanical ports** (the reference shaders
    are GLSL), but Slang is a legitimate choice for **new** subsystems (material system,
    GI) — decide per subsystem, not globally.
11. **Pipeline cache on disk** — **DONE**. Optionally precompile/warm known variants to
    avoid first-use hitches. `VK_EXT_shader_object` or graphics-pipeline-libraries cut the
    variant explosion further, but "jury still out" (LunarG) — defer.
12. **Debug-utils labels** — object naming is **DONE**; add
    `vkCmdBeginDebugUtilsLabelEXT` scopes around each pass so RenderDoc/Nsight captures are
    self-documenting. Verification here is manual/visual, so this pays for itself.
13. **VMA** — **DONE**. Keep the `MemoryUsage` intent enum. Big point clouds → dedicated
    allocations / custom pools; per-frame streams → one persistently-mapped ring (Phase 4),
    not per-object re-allocation.

### B. Native-Vulkan feature opportunities (capability-gated — detect + fall back)

- **Multiview single-pass stereo** (`VK_KHR_multiview`, core 1.1) — already the renderer's
  spine. Phase 7 just adds the 2nd view + a stereo swapchain, so GL's twice-per-frame
  `renderEye` and its `g_sharedPassesDone` shared-pass hack simply disappear.
- **Mesh + task shaders** (`VK_EXT_mesh_shader`): for **models**, meshlet rendering with
  per-meshlet cull/LOD in a task shader is the modern geometry path and pairs with GPU-driven
  culling (Phase 3+/9). For **point clouds**, note the caveat: Schütz's atomicMin *compute*
  rasterizer is 10-100x faster than the hardware point pipeline and is **not** beaten by mesh
  shaders for pixel-sized points — keep compute for Phase 5; consider mesh shaders only for
  splat/surfel rendering. Capability-gate (many iGPUs lack mesh shaders).
- **Ray query** (`VK_KHR_ray_query` + `VK_KHR_acceleration_structure`): the clean,
  hardware-accelerated replacement for the deferred GI — ray-traced **shadows / AO /
  reflections from inside the existing fragment or compute shaders**, no separate RT
  pipeline. This is the Phase 9 path for Radiance/DDGI/AO in place of the old SSBO software
  BVH; falls back to shadow maps where RT is absent.
- **Ray-tracing pipelines** (`VK_KHR_ray_tracing_pipeline`): full multi-bounce GI if ray
  query is not enough. BLAS/TLAS build from the same mesh buffers, and the planned two-level
  BVH rework maps directly onto TLAS/BLAS.
- **Variable-rate shading** (`VK_KHR_fragment_shading_rate`): cheap wins for stereo/foveated
  XR (Phase 7) and the expensive point-cloud resolve.
- **Subgroup ops** (core 1.1): replace shared-memory reductions in the point-cloud early-Z /
  HQS passes with `subgroupMin` / ballot — often a large compute win. Query `subgroupSize`
  (NVIDIA 32, AMD 64).
- **Async compute queue**: run point-cloud / GI compute concurrently with graphics via a
  second queue + timeline. Detect a compute-capable family; fall back to the graphics queue.

### C. OpenGL reference bugs & smells — FIX, don't port

The GL shaders/renderer carry real defects. When porting, **fix these; do not faithfully
reproduce them.**

1. **Point-cloud shaders use NVIDIA-only extensions** (`GL_NV_shader_atomic_int64`,
   `GL_NV_gpu_shader5`). These **will not run on AMD** — a direct breach of the
   "any modern NVIDIA/AMD GPU" target. Port to core `shaderBufferInt64Atomics` +
   `GL_EXT_shader_atomic_int64` only (already required/enabled in `Device`). *(Phase 5)*
2. **Point-cloud depth math is GL clip space** (`ndc.z*0.5+0.5`, an `x/y/z in [-1,1]`
   clip test, `gl_FragDepth` + `GL_LESS`). Rework for Vulkan **[0,1] depth + reverse-Z
   GREATER** (house convention, `Renderer/Projection.h`). Copied verbatim it yields a
   wrong / flickering depth test. *(Phase 5)*
3. **`fragmentShader.glsl` is a ~2,180-line uber-shader mixing two lighting models** —
   Blinn-Phong (`CalcDirLight`/`CalcPointLight`/`CalcSpotLight`) *and* a separate
   Cook-Torrance PBR path (`calculatePBRLighting`) that partly contradict each other.
   Ship **one** correct metallic-roughness PBR path; delete the Blinn-Phong duplication.
   *(Phase 3)*
4. **Spot-light shadows are a non-functional stub**: `SpotShadowCalculation` loops
   accumulating `shadow += 0.0` and returns 0 — pure wasted ALU; spot lights never
   shadow. Implement a real spot shadow (a cheap 2D shadow map) or drop the call.
   Don't port the stub. *(Phase 3)*
5. **Per-light attenuation is hard-coded** (`1/(1+0.09d+0.032d^2)`) while the
   `PointLight`/`SpotLight` structs carry *unused* `linear`/`quadratic` fields. Wire the
   real per-light params, or move to physically-based inverse-square + a range cutoff.
   *(Phase 3)*
6. **Tone mapping was baked into the mesh fragment shader** (`applyToneMapping`, sRGB via
   `pow(1/2.2)`). Already correctly relocated to `TonemapPass` (Phase 2, exact sRGB OETF).
   The Phase-3 lit pass must output **linear HDR only** — do not re-introduce
   tonemap/sRGB there. Sample albedo through **sRGB-format image views** (hardware decode),
   not a manual `pow`.
7. **Shadow bias is a pile of magic numbers** (distance fades, "edge softness"
   `mix(shadow, shadow*0.8, ...)`, hand-tuned constants). Replace with principled
   normal-offset + slope-scaled bias and a single PCF (optionally PCSS) path; reverse-Z
   lets the bias be smaller. *(Phase 3)*
8. **Uniform-heavy binding model** (`material.textures[16]`, `hasTexture` as a *float*
   compared `>0.5`, dozens of `bool` uniforms). Replace with a bindless material struct
   (texture indices + a flags bitfield) fetched by descriptor index / BDA. *(Phase 3)*
9. **Overlays use two divergent shader paths** — cursors load GLSL files via
   `Engine::loadShader`; tools/gizmo/plugins compile inline GLSL via
   `compileOverlayProgram`. Unify onto **one** RHI overlay renderer (dynamic vertex buffer
   + a line/tri pipeline) behind the plugin seam. *(Phase 6)*

### D. Libraries worth adopting (in addition to §7b)

Prefer a well-maintained library over a from-scratch rewrite when it clearly wins (self-integrate
per §7b — vendor everything, wire the `.vcxproj`, build green on CI, avoid GPL):

- **meshoptimizer** (MIT) — meshlet building for mesh shaders, vertex-cache optimization,
  vertex/index quantization + encoding, and LOD simplification. Directly useful for Phase 3
  model upload and any GPU-driven / mesh-shader path.
- **Slang** (compiler + SDK) — see §A.10; a modern shading language for *new* subsystems.
- **KTX-Software / libktx** (Apache-2.0) — GPU-compressed (BCn) textures with mip chains
  and `VK_KHR` transcoding; better than raw stb + runtime blit for large material sets
  (Phase 3). Keep stb for the odd loose PNG.
- **Tracy** (permissive) — CPU+GPU frame profiler with Vulkan instrumentation; since
  verification is manual/visual, this is the fastest way to *see* where frame time goes.
- (already flagged in §7b: **VMA**, **shaderc/glslang**, **volk**, **vk-bootstrap**,
  **SPIRV-Reflect** — all in use or considered.)

---

## 7. Risks & Mitigations
- **Quad‑buffer stereo present is workstation‑GPU‑only.** On the broad
  Win10/11 + any‑modern‑NVIDIA/AMD target, most consumer GPUs won't expose a
  stereo surface. → `StereoTarget` **detects at runtime** and falls back to mono
  (optionally side‑by‑side); the multiview render path is unaffected. Not a
  blocker — same behaviour class as the current GL app's stereo→mono fallback.
- **int64 atomics availability** — **guaranteed at Vulkan 1.2+** (`shaderBufferInt64Atomics`
  is core), and the current GL app already uses GL int64 atomics on this hardware,
  so effectively a non‑risk. → Enable + assert in `Device` init; only if a future
  target GPU lacks it, document a fallback (32‑bit depth + separate index) before Phase 5.
- **Scope creep from GI systems.** → Explicitly deferred; feature‑gate the
  lighting‑mode UI so only Shadow Mapping is selectable in the first Vulkan build.
- **Two huge files (`main.cpp`, `GUI.cpp`).** → Decompose incrementally, not in
  one commit; land the new `Application` skeleton early (Phase 1) and migrate
  behaviour into it phase by phase.
- **Regression risk (no test suite).** → Keep a manual verification checklist per
  phase (screenshots of `office.scene`, a reference LAS cloud, each tool).

---

## 7b. Using & integrating third‑party libraries
Prefer a well‑maintained library over reinventing it whenever it **greatly
simplifies or optimizes** the work. Strong candidates for this migration:
- **VMA** (`vk_mem_alloc.h`) — memory allocation. *(planned)*
- **shaderc** / **glslang** — GLSL→SPIR‑V. *(planned)*
- **volk** — dynamic Vulkan function loader (fast, avoids linking the static
  loader for everything). *(consider)*
- **vk‑bootstrap** — instance/physical‑device/device/swapchain boilerplate.
  *(consider — but keep the RHI thin; don't leak it above the RHI)*
- **SPIRV‑Reflect** — reflect descriptor/push‑constant layouts from SPIR‑V so
  pipeline layouts aren't hand‑maintained. *(consider)*

**Integration is the agent's job — do it completely, no manual steps left for the
user:**
1. Download **all** required files (headers, `.lib`, `.dll`, LICENSE) into the
   repo under the existing vendoring layout — third‑party headers in
   `headers/libs/<name>/`, prebuilt binaries in `dependencies/include|lib|bin`.
2. Wire into `StereoVista.vcxproj`(+`.filters`): additional include dirs, library
   dirs, additional dependencies (per‑config Debug/Release), and a **post‑build
   copy** for any runtime `.dll` (mirror the existing `assimp-vc143-mt.dll` /
   `LASzip64.dll` copy steps).
3. A fresh `git clone` must build **green on CI** with no external install beyond
   the CI‑provided Vulkan SDK. Prefer header‑only libs where practical.
4. Keep each library's LICENSE; avoid copyleft (GPL/LGPL) that would encumber the
   app. Record every added library (name, version, why) in the status file §4.

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
2. **Phase 0** — wire the Vulkan toolchain (SDK/VMA/shaderc/volk) into the
   `.vcxproj`, do the §2.12 dependency surgery, set `GLM_FORCE_DEPTH_ZERO_TO_ONE`,
   add the SPIR‑V build step; get CI green. **No spikes.**
3. **Phase 1** — stand up the `Application` + RHI `Device` (Vulkan 1.3, feature
   enablement) + `Swapchain` + ImGui‑Vulkan hello‑triangle.
</content>
</invoke>
