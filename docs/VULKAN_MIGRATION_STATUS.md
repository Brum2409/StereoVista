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
> 5. **Update this file when you finish work.** Move tasks between sections, note
>    what you actually did, what changed vs. the plan, and what's next. Append a
>    dated entry to the **Session Log**.
> 6. The deep design rationale (RHI layering, decisions, per-system reuse/rewrite
>    table) lives in **`docs/VULKAN_MIGRATION.md`** — read it once for context.
>
> **Branch:** `claude/opengl-vulkan-migration-yqc277` — this *is* the Vulkan
> branch. All migration work lands here.

---

## 0. Current status at a glance

| | |
|---|---|
| **Current phase** | Phase 0 — Setup & spikes (**not started**) |
| **What builds** | Original OpenGL app (unchanged; MSVC/`StereoVista.sln`) |
| **Vulkan code present** | None yet |
| **Last updated** | 2026-07-02 — planning session |

Legend: ☐ not started · ◐ in progress · ☑ done · ✎ changed from original plan

---

## 1. Phase board (suggested order — reorder if you have a better idea)

- ☐ **Phase 0 — Setup & spikes** (de-risk the two scary unknowns)
- ☐ **Phase 1 — Core Vulkan bootstrap + `Application` skeleton** (hello triangle + ImGui)
- ☐ **Phase 2 — RHI hardening** (buffers/images/pipelines/descriptors, HDR target + tonemap)
- ☐ **Phase 3 — Mesh forward PBR + shadow mapping** (Shadow-Mapping lighting only)
- ☐ **Phase 4 — Loading system → RHI upload** (parsers reused; streaming preserved)
- ☐ **Phase 5 — Compute point-cloud pipeline** (Schütz rasterizer + HQS; biggest rewrite)
- ☐ **Phase 6 — Camera, cursors, tools, plugins** (overlay renderer)
- ☐ **Phase 7 — Stereo (`StereoTarget`) + Vulkan OpenXR**
- ☐ **Phase 8 — Decompose `main.cpp`/`GUI.cpp`, delete GL scaffolding, update docs**
- ☐ **Phase 9 (later, out of current scope) — re-add deferred features natively**

> **Deferred on purpose** (do NOT port yet — re-implemented later in native Vulkan):
> Voxel Cone Tracing (`Voxalizer`, `voxelization/*`), DDGI (`DDGIVolume`, `ddgi*`),
> BVH ray-traced Radiance + `BVHDebug`, and heavy post-FX (Bloom, SSAO). Keep the
> `LightingMode` enum but ship only **Shadow Mapping** in the first Vulkan build;
> grey out VCT/Radiance in the UI.

---

## 2. Per-phase detail (goal · how it could be done · exit · what to read)

### Phase 0 — Setup & spikes  ☐
- **Goal:** add tooling and settle the two decisions that gate everything.
- **How:**
  - Add to `dependencies/`: **Vulkan SDK**, **VMA** (`vk_mem_alloc.h`),
    **shaderc/glslang**. Wire into `StereoVista.vcxproj`(+`.filters`), add a
    SPIR-V build step, enable validation layers in Debug.
  - **Spike A (capabilities):** probe the target GPU for
    `shaderBufferInt64Atomics` (point-cloud rasterizer depends on it — see
    `ComputePointCloudRenderer::init` which already checks the GL equivalents),
    descriptor indexing, dynamic rendering, timeline semaphores, and the stereo
    present path. Write results into §4 below.
  - **Spike B (stereo — validation, not a blocker):** confirm the target GPU's
    Vulkan surface reports `maxImageArrayLayers >= 2`, then clear left eye red /
    right eye blue on the real quad-buffer stereo display. Vulkan supports
    quad-buffered stereo **natively**: create the swapchain with
    `imageArrayLayers = 2` and the presentation engine maps layer 0/1 to
    left/right eyes (no `GL_BACK_LEFT/RIGHT` hacks). Plan to render both eyes in
    a **single pass** with `VK_KHR_multiview` — this is strictly better than the
    OpenGL path, which runs `renderEye()` twice per frame. If the surface caps
    stereo out, fall back to two swapchains / side-by-side.
- **Exit:** both spikes pass (or a documented fallback chosen); SDK builds.
- **Read:** `src/Engine/ComputePointCloudRenderer.cpp` (int64 check + shader ext
  lines), `src/main.cpp:3485-3535` (GLFW_STEREO creation + mono fallback),
  `docs/VULKAN_MIGRATION.md §3`. Vulkan stereo refs: spec `VkSwapchainCreateInfoKHR`
  (`imageArrayLayers`) + `VK_KHR_multiview`.

### Phase 1 — Core bootstrap + `Application`  ☐
- **Goal:** a window with Vulkan + ImGui drawing, and the new app skeleton.
- **How:** `Platform::Window` on GLFW with `GLFW_NO_API` + `VkSurfaceKHR` (keep
  existing input callbacks). RHI `Device`+`Swapchain`, 2 frames-in-flight,
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
- **Exit:** lit + shadow-mapped models on screen, mono.
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
  `uint64_t` framebuffer SSBO + `atomicMin` via `VK_KHR_shader_atomic_int64`
  (gate on Spike A); port `pointcloud_rasterize.comp`, HQS depth/colour/resolve,
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
- **Goal:** quad-buffer stereo display and OpenXR HMD both render.
- **How:** implement `StereoTarget` (native Vulkan stereo swapchain,
  `imageArrayLayers = 2`, from Spike B) plus mono. **Render both eyes in a single
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

## 3. Next up (start here)
1. **Phase 0 / Spike A** — Vulkan device capability probe (esp. int64 atomics +
   stereo present). Record findings in §4.
2. **Phase 0 / Spike B** — quad-buffer stereo present proof-of-concept.
3. Then **Phase 1** bootstrap.

If Spike A shows the target GPU lacks `shaderBufferInt64Atomics`, decide and
document a fallback for the point-cloud framebuffer (e.g. 32-bit depth + separate
index buffer) **before** starting Phase 5.

---

## 4. Decisions & spike results (fill in as you learn)
- Vulkan version: **1.3** (proposed). — _confirm_
- Memory: **VMA**. — _confirm_
- Shaders: GLSL → SPIR-V via **shaderc**, runtime + offline. — _confirm_
- int64 atomics available on target GPU? **UNKNOWN — Spike A**
- Quad-buffer stereo: **native in Vulkan** — swapchain `imageArrayLayers = 2`
  (needs `maxImageArrayLayers >= 2`); render both eyes single-pass via
  `VK_KHR_multiview`. Confirm surface caps on the target GPU in **Spike B**.
- _(add device/driver names, extension support, benchmark notes here)_

---

## 4b. Continuous Integration
- Workflow: `.github/workflows/ci-vulkan-migration.yml` runs on every push to this
  branch (and PRs). It builds **Debug + Release x64** on `windows-latest` with the
  **Vulkan SDK** pre-installed (`VULKAN_SDK` set; VMA component included), and does
  **not** publish releases (that's `msbuild.yml`, main-only).
- Compiler errors appear as **inline annotations** (MSVC problem matcher) and the
  full MSBuild **text + `.binlog`** are uploaded as artifacts (even on failure) —
  download them to see every error, or open the `.binlog` in the MSBuild
  Structured Log Viewer.
- **After pushing, check the CI result** — this repo has no local Windows build in
  the agent environment, so CI is the primary signal that the code compiles.
  Wire new Vulkan sources into `StereoVista.vcxproj` (+ include dir
  `$(VULKAN_SDK)\Include`, lib `$(VULKAN_SDK)\Lib\vulkan-1.lib`) so CI exercises
  them.

## 5. Verification checklist (no automated tests exist — verify manually)
Re-check after each phase; capture before/after screenshots:
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
