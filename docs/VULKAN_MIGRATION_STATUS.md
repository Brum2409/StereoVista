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
| **Current phase** | Phase 3 ☑ **DONE (user-verified 2026-07-03; Phase 2 verify folded in).** Pass-based renderer (Shadow→Forward→Skybox→Tonemap over a real `FrameSubmission` scene input), bindless materials, `office.scene` loads, sun + point-light shadows with world-unit biases + rotated-Vogel PCF + PCSS contact hardening. Next: **Phase 4 — Loading system → RHI upload** |
| **What builds** | Vulkan app: `office.scene` (or a built-in default scene) rendered with ONE metallic-roughness PBR path into the multiview HDR target — 4K sun shadow map (texel-snapped ortho fit) + up to 4 point-light depth cube maps (all 6 faces in ONE multiview pass each), skybox (cubemap/equirect/solid/gradient), tonemap resolve + ImGui in one backbuffer pass, PNG screenshot. Fly camera (RMB-look + WASDQE). Old GL sources `ExcludedFromBuild` |
| **Vulkan code present** | `RHI/` (Device+pipeline cache, Swapchain, ShaderCompiler, Buffer, Texture, Pipeline+reflection, DescriptorAllocator, Barrier, VMA glue), `Platform/` (Window, Paths), `Renderer/` (Renderer, MaterialSystem, MeshBuffer, FrameSubmission/GpuTypes, `passes/` Tonemap+Shadow+Forward+Skybox), `Scene/` (office.scene loader, primitives, Assimp importer), `App/Application`; shared C++/GLSL structs in `assets/shaders_vk/scene_types.h`; `Engine/Screenshot` re-enabled (PNG writer, API-agnostic) |
| **Last updated** | 2026-07-03 — Phases 2+3 closed out (user ran the build; Peter-Panning bias bug fixed, shadow quality pass landed: Vogel PCF + PCSS both light types, caster range cull) |

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
- ☑ **Phase 2 — RHI hardening** (buffers/images/pipelines/descriptors, HDR target + tonemap, screenshot readback; verified through the Phase 3 scene run 2026-07-03 — the Phase 2 triangle demo was deleted as planned)
- ☑ **Phase 3 — Mesh forward PBR + shadow mapping** (Shadow-Mapping lighting only; user-verified 2026-07-03. Includes the post-first-run shadow quality pass: world-unit depth biases, rotated-Vogel PCF, PCSS contact hardening on sun AND point lights, caster range culling — see session log)
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

> **Native-Vulkan nudges.** Each phase below carries a **`→ Nudges`** line
> pointing into the **Native-Vulkan Playbook** (`docs/VULKAN_MIGRATION.md §6b`) by
> letter/number: **§A** cross-cutting practices (scalar layout, BDA, bindless,
> dynamic state…), **§B** capability-gated features (mesh shaders, ray query,
> subgroups, async compute, VRS), **§C** OpenGL bugs/smells to **fix, not port**,
> **§D** libraries worth adopting. These are **suggestions that nudge, not
> mandates** — if a simpler path fits the phase, take it and record why. Several
> §A items are already wired at the device level (see §4 / Device.cpp): so the
> phase work is often just *using* an already-enabled feature.

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

### Phase 2 — RHI hardening  ◐ (code done 2026-07-03; user verify pending)
- **Goal:** the abstraction the rest of the port builds on.
- **IMPLEMENTED — read the source, not this summary:**
  - `rhi::Buffer` / `rhi::Texture` (`RHI/Buffer.*`, `RHI/Texture.*`): VMA-backed
    move-only RAII; `MemoryUsage::{GpuOnly,HostUpload,HostReadback}` intent enum;
    staged uploads via `Device::immediateSubmit` (loading-time; Phase 4 adds the
    streaming ring); texture upload does buffer→image copy + full **mip-chain
    blit generation** (+format-feature fail-loud check), ends SHADER_READ_ONLY.
  - `rhi::Pipeline` + builders (`RHI/Pipeline.*`): graphics+compute from SPIR-V.
    **Descriptor-set & push-constant layouts are REFLECTED via vendored
    SPIRV-Reflect** and merged across stages (shaders are the single source of
    truth). Overrides only for what SPIR-V can't express: `bindingOverride`
    (runtime-array capacity + PARTIALLY_BOUND/UPDATE_AFTER_BIND/VARIABLE_COUNT
    flags for Phase 3 bindless materials) and `externalSetLayout` (share a set
    layout object across pipelines). Push ranges merge into ONE range (union
    bytes ∪ stages). Defaults = house conventions (CCW front face, reverse-Z
    GREATER). Viewport/scissor always dynamic. Fed by `Device`'s new
    **disk-persisted VkPipelineCache** (`pipeline_cache.bin`, cwd-relative like
    all runtime files; driver validates the blob itself).
  - `rhi::DescriptorAllocator` (`RHI/DescriptorAllocator.*`): growable pool
    list (ratio table × setsPerPool, doubles up to 1024, OUT_OF_POOL/FRAGMENTED
    → new pool), `reset()` recycles; supports variable-count allocation.
    One per `FrameContext`, reset at slot reuse (replaced Phase 1's fixed
    pools). Plus `DescriptorWriter` (batched vkUpdateDescriptorSets).
  - `RHI/Barrier.h`: shared sync2 `imageBarrier`/`bufferBarrier`/`cmdBarrier`
    helpers (Renderer's local copies deleted).
  - **Tonemap resolve** (`Renderer/passes/TonemapPass.*`, first pass object of
    the Phase 3 pass-based layout): fullscreen-triangle resolve of scene layer
    N (texelFetch, 1:1) with exposure + operator push constants, writing
    sRGB-encoded LDR to the backbuffer — **replaces the placeholder blit**.
    ImGui renders in the SAME backbuffer render pass after the fullscreen draw
    (primitive order guarantees blending correctness; one pass, no extra
    barrier). Operators: Reinhard / ACES(default) / Uncharted2 / **real
    minimal-AgX** / Khronos PBR Neutral + exact sRGB OETF — the old shader's
    fake AgX & fake Tony-McMapface were NOT copied (McMapface returns as a
    proper 3D LUT with post-FX). Debug panel gained exposure slider + operator
    combo.
  - **Screenshot**: `Engine/Screenshot.{h,cpp}` rewritten API-agnostic (PNG
    writer + stereo combine + timestamped path; GL capture trio deleted) and
    re-enabled in the build. Capture is now `Renderer::requestScreenshot(path)`:
    backbuffer (tonemapped scene + UI) → `vkCmdCopyImageToBuffer2` into a
    HostReadback `rhi::Buffer` (swapchain gains TRANSFER_SRC usage, probed via
    `supportedUsageFlags` → `Swapchain::supportsCapture()`), completion
    **polled on the frame timeline** (no stall), BGRA→RGB convert + PNG write;
    finished-or-dropped cleanly on shutdown. Debug-panel button + status line.
  - Demo scene upgraded to exercise everything at runtime: triangle now draws
    from a **staged-upload vertex+index buffer** through the **reflected
    pipeline** with a **mipmapped procedural checker texture** (sRGB, aniso
    sampler), HDR vertex intensities (top corner 8×, right 0.25×) so operator/
    exposure changes are visible.
- **✎ vs plan:** no `CommandContext` wrapper — the Renderer records raw
  vkCmd* + rhi helpers (a command abstraction now would be speculative; the
  Phase 6 plugin overlay API is the real seam). Pipeline "cache" interpreted as
  VkPipelineCache-on-disk (real win) rather than a handle registry.
- **Exit:** clear→draw→tonemap→present works; screenshot saves. **Builds green
  locally (Release+Debug x64); awaiting the user's visual run.**
- **Read:** `headers/RHI/*.h` (each header documents its contract),
  `src/Renderer/Renderer.cpp` (frame shape), `assets/shaders_vk/tonemap.frag`.

### Phase 3 — Mesh forward PBR + shadow mapping  ◐ (code done 2026-07-03, user verify pending)
- **Goal:** `office.scene` meshes render lit + shadowed (mono), matching GL.
- **✎ LANDED (2026-07-03)** — what was actually built (deviations noted):
  - **Restructure first, as planned:** `Renderer::recordFrame` now only sequences
    pass objects + frame-graph barriers. `ShadowPass`/`ForwardPass`/`SkyboxPass`
    mirror `TonemapPass`; the Phase 2 triangle scaffold (VB/IB, checker,
    `trianglePipeline_`, `triangle.vert/frag`) is **deleted**. The Renderer's
    scene input is `renderer::FrameSubmission` (per-view cameras + draw list +
    sun/point lights + sky + toggles) built by the app each frame.
  - **Shared GPU structs (A.1):** `assets/shaders_vk/scene_types.h` is ONE header
    included by both GLSL (`layout(scalar)`) and C++ (`Renderer/GpuTypes.h`
    aliases + static_asserts). `ViewUniforms` std140 is gone; `scalarBlockLayout`
    was promoted to a **hard device requirement** (with `imageCubeArray`).
  - **Descriptors:** set 0 = one shared per-frame set (FrameData UBO, light +
    material SSBOs, sun/point shadow samplers, sky textures) written once per
    frame; set 1 = `MaterialSystem`'s persistent **bindless** set (shared
    sampler + PARTIALLY_BOUND/UPDATE_AFTER_BIND texture2D[4096]). Materials
    store texture **indices** (`SV_INVALID_TEXTURE` = absent) — the GL
    `material.textures[16]`/hasTexture-as-float model is gone (C.8). Per-draw
    data rides 116-byte push constants.
  - **Lighting (§C fixes):** ONE metallic-roughness Cook-Torrance path (no
    Blinn-Phong duplication, C.3); spot-shadow stub deleted, spot lights not
    ported (C.4); real per-light linear/quadratic attenuation from the scene
    file (C.5); lit pass is linear-HDR only, albedo through sRGB views (C.6);
    normal-offset (shadow-texel-footprint scaled) + slope bias + one PCF path
    per light type, small static reverse-Z rasterizer bias on the casters (C.7).
  - **Shadows:** sun = 4K D32 map over a texel-snapped ortho fit of the draw
    list's world bounds (port of `calculateLightSpaceMatrix`); points = D32
    **cube array** (4 lights × 6 faces), each light rendered in ONE multiview
    pass (viewMask 0x3F, `gl_ViewIndex` = face) — replaces the GL geometry-shader
    broadcast AND its early-Z-killing linear-distance `gl_FragDepth` write; the
    receiver reconstructs reverse-Z face depth analytically. Casters are
    depth-only pipelines with **no fragment stage** (builder now allows that).
  - **Skybox:** fullscreen triangle at reverse-Z far, GREATER_OR_EQUAL no-write,
    ray from per-view `invViewProj` — no cube VB, multiview-native; modes
    cubemap / equirect HDR (RGBA16F with mips) / solid / gradient.
  - **Scene host (interim):** `scene::Scene` (`headers/Scene/Scene.h`) loads
    office.scene-style JSON (primitives + `localPath` models via Assimp,
    point lights, camera pose) — the render-relevant subset only; the full
    SceneManager (undo/save/merge/point clouds) returns in later phases.
    Primitive factories ported verbatim (bitangent → tangent.w sign, half the
    vertex data). `Platform/Paths.h` resolves assets from cwd / exe dir / repo.
  - **App:** fly camera (RMB-look + WASDQE + Shift), debug panel gained sun/
    ambient/shadow/sky/point-light controls. `Application::run` builds the
    `FrameSubmission`; `renderFrame(submission, uiDrawData)` is the new API.
  - **Gotcha for later shaders:** indexing the bindless `uTextures[]` with a
    variable requires `#extension GL_EXT_nonuniform_qualifier` even when the
    index is dynamically uniform (no `nonuniformEXT` needed) — glslang rejects
    it otherwise, and the first MSBuild run did NOT fail on the glslc error
    (exe still linked); check `$(OutDir)assets\shaders_vk\*.spv` exists when
    adding shaders.
- **How:** Vulkan vertex/index upload for `Mesh` (reuse Assimp import as-is);
  camera UBO; per-object push constants / dynamic UBO; materials via descriptor
  indexing; port `core/fragmentShader.glsl` + `shadowMapping*` +
  `simpleDepth*` + point-shadow (cubemap/`pointShadow*`) + skybox to Vulkan
  GLSL. `renderEye()` (`main.cpp:5335`, a ~600-line monolith) becomes a
  **pass-based renderer** (shadow pass → forward pass → skybox), not a straight
  port.
- **⚠ FIRST restructure the already-ported Renderer — don't pile onto the demo
  scaffold** (audited 2026-07-03). The RHI foundation
  (`Device`/`Swapchain`/`Buffer`/`Texture`/`Pipeline`/`DescriptorAllocator`/
  `Barrier`, the one-timeline frame loop, the layered multiview scene target,
  `TonemapPass`, screenshot readback, ImGui) is **sound and needs no rewrite —
  build on it.** The one thing that must change *before* adding lighting: today
  `Renderer::recordFrame` **inlines the entire scene pass** (barriers +
  begin-rendering + descriptor writes + the hardcoded triangle draw), and the
  Renderer has **no scene input** — `updateViewUniforms` fabricates a swaying
  camera and draws a compiled-in triangle. `TonemapPass` is a proper pass object;
  the scene rendering is not. Bolt shadow/forward/skybox on as more inline blocks
  and you rebuild the `renderEye()` monolith this migration exists to kill. So:
  1. **Extract passes as objects mirroring `TonemapPass`** — `ShadowPass`
     (directional + point cubemap), `ForwardPass` (PBR), `SkyboxPass` — and make
     `recordFrame` drive a **pass list**, not inline blocks.
  2. **Give the Renderer a real per-frame scene input** (camera matrices + a draw
     list + lights) so `renderFrame` renders the actual scene; retire
     `createTriangleScene` and `updateViewUniforms`'s hardcoded sway.
  3. **Delete the Phase 2 demo scaffold** (triangle VB/IB, `checkerTexture_`,
     `materialSampler_`, `trianglePipeline_`) once the mesh path draws — it exists
     only to exercise the RHI and is explicitly marked "replaced in Phase 3."
  4. **Migrate `ViewUniforms` (`Renderer.h`) std140 → `layout(scalar)`** while you
     own that struct (playbook A.1) — cheaper now than after N shaders bind it.
- **⚠ Design for multiview NOW (so Phase 7 stereo is additive, not a rewrite):**
  make the color/depth target a **layered** image and the camera UBO a
  **per-view array** indexed by `gl_ViewIndex` (`VK_KHR_multiview`). Mono = 1
  view/layer today; stereo just enables the 2nd view + a stereo swapchain later.
  Don't bake a single view/projection into shaders or the pass. Also set
  `GLM_FORCE_DEPTH_ZERO_TO_ONE` + fix inverted-Y on all projections here.
- **→ Nudges (playbook §6b):** this is the richest phase for "do it better."
  - **Already enabled at the device level — just *use* it** (Device.cpp:351-369):
    `scalarBlockLayout` (**A.1** — author material/camera/light structs as a
    **single POD header shared by C++ and GLSL** with `layout(scalar)`; no
    std140 vec4-padding), `bufferDeviceAddress` (**A.2** — per-object data via a
    GPU pointer in push constants, not a UBO-per-draw), and the full
    descriptor-indexing/bindless flag set + `runtimeDescriptorArray` (**A.3/C.8**
    — one texture array bound once/frame; materials store *indices* + a flags
    bitfield, replacing the GL `material.textures[16]` + `hasTexture`-as-float
    model). The `Pipeline` builder already takes `bindingOverride`
    (PARTIALLY_BOUND/VARIABLE_COUNT) for the bindless array.
  - **Fix, don't port (§C):** ship **one** metallic-roughness PBR path and
    **delete the Blinn-Phong duplication** (**C.3**); drop the do-nothing
    `SpotShadowCalculation` stub or implement a real 2D spot shadow (**C.4**);
    wire real per-light attenuation params instead of the hard-coded constants
    (**C.5**); keep the lit pass **linear-HDR only** — no tonemap/sRGB `pow` (it
    lives in `TonemapPass`), sample albedo through **sRGB image views** (**C.6**);
    replace the magic-number shadow bias with principled normal-offset +
    slope-scaled bias + one PCF path — reverse-Z lets the bias be smaller
    (**C.7**).
  - **Consider (capability-gate / optional):** mesh+task shaders for models with
    per-meshlet cull/LOD (**§B**); widen dynamic state as passes multiply
    (**A.6**); `vkCmdBeginDebugUtilsLabelEXT` scopes per pass for RenderDoc/Nsight
    (**A.12**); **meshoptimizer** for meshlets/vertex-cache/LOD and **KTX/libktx**
    for BCn textures (**§D**); **Slang** is a legitimate choice for the *new*
    material subsystem (**A.10**). `shaderDrawParameters` is also already enabled
    for a later GPU-driven indirect path (**A.8**).
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
- **→ Nudges (playbook §6b):** for the streaming path, use **one
  persistently-mapped VMA ring** the worker writes into each frame, **not
  per-object re-allocation** (**A.13**); give **big static clouds dedicated
  allocations / a custom VMA pool** (**A.13**). Set up per-batch data for **BDA
  access** (a GPU pointer per batch) so Phase 5 can drop the 5-SSBOs-per-cloud
  binding churn (**A.2**).
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
- **→ Nudges (playbook §6b):**
  - **Fix, don't port (§C):** the GL shaders use **NVIDIA-only extensions**
    (`GL_NV_shader_atomic_int64`, `GL_NV_gpu_shader5`) that **break on AMD** —
    port to core `shaderBufferInt64Atomics` + `GL_EXT_shader_atomic_int64` only
    (already required/enabled in `Device`) (**C.1**); the depth math is **GL clip
    space** (`ndc.z*0.5+0.5`, `[-1,1]` clip test, `GL_LESS`) — rework for Vulkan
    **[0,1] depth + reverse-Z GREATER** (house convention) or it flickers (**C.2**).
  - **Do it better (§A/§B):** pass the per-batch buffers via **BDA** instead of
    5 SSBOs bound per cloud (**A.2**); replace shared-memory reductions in the
    early-Z / HQS passes with **subgroup ops** (`subgroupMin`/ballot) (**§B**);
    run the point-cloud compute on an **async-compute queue + 2nd timeline** to
    overlap with graphics (detect a compute family; fall back to graphics)
    (**A.5/§B**); **VRS** can cheapen the expensive resolve (**§B**). **Keep the
    Schütz atomicMin compute rasterizer** — it beats both the hardware point
    pipeline and mesh shaders for pixel-sized points; mesh shaders only pay off
    for splat/surfel rendering (**§B**).
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
- **→ Nudges (playbook §6b):** collapse the **two divergent overlay shader
  paths** (cursors' file-loaded GLSL vs tools/plugins' inline
  `compileOverlayProgram`) into **one** RHI overlay renderer — a dynamic vertex
  buffer + line/tri pipelines behind the plugin seam (**C.9**). If overlay
  primitive counts grow (many cursors/handles/measurements), batch them into an
  **instanced / GPU-driven indirect draw** (`shaderDrawParameters` already
  enabled) rather than a draw-per-widget (**A.8**).
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
- **→ Nudges (playbook §6b):** the renderer was built **multiview-aware from
  Phase 3**, so this phase is **additive** — add the 2nd view + the stereo
  swapchain and GL's twice-per-frame `renderEye` and its `g_sharedPassesDone`
  shared-pass hack simply **disappear** (**§B**). **VRS** is a cheap win for
  stereo / foveated XR (**§B**). Capability-gate stereo *present* and fall back
  to mono (§4) — render path is unaffected.
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
- **→ Nudges (playbook §6b):** prefer **`VK_KHR_ray_query`** — hardware ray-traced
  shadows/AO/reflections **from inside the existing fragment/compute shaders**, no
  separate RT pipeline — over the old SSBO software BVH; fall back to shadow maps
  where RT is absent (**§B**). Use full **`VK_KHR_ray_tracing_pipeline`** only for
  multi-bounce GI; **BLAS/TLAS builds from the same mesh buffers and maps directly
  onto the planned two-level BVH rework (`[[two-level-bvh-rework]]`)** (**§B**).
  **Slang** is a strong fit for these *new* GI subsystems (**A.10**); **Tracy** is
  the fastest way to see where the compute/RT frame time goes (**§D**).

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
1. **Close out Phases 2+3 together**: the user runs the build and works the
   Phase 3 checklist in §5 (it subsumes the still-open Phase 2 tonemap/
   screenshot items — the Phase 2 triangle demo no longer exists); fix whatever
   the run surfaces, then flip Phase 2 and 3 to ☑ and commit/push the milestone
   to `StereoVista-vulkan`.
2. **Phase 4 — Loading system → RHI upload**: keep every parser (Assimp already
   rides `Scene/ModelImporter`; LASzip, HDF5/HighFive, PLY, XYZ, `.pcb` still
   GL-excluded) and replace only the GL buffer calls with `rhi::Buffer` +
   staging. **Preserve the progressive streaming contract** (worker thread +
   per-frame `updateStreaming` fills); see the Phase 4 `→ Nudges` (persistently
   mapped VMA ring, dedicated allocations for big clouds, per-batch BDA prep
   for Phase 5).

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
  - **SPIRV-Reflect @ vulkan-sdk-1.4.350.1** (matches the vendored header line) —
    `headers/libs/spirv_reflect/` (spirv_reflect.h/.c + bundled
    include/spirv/unified1/spirv.h, Apache-2.0) — descriptor-set/push-constant
    reflection so pipeline layouts come from the shaders instead of
    hand-maintained C++ mirrors (`rhi::Pipeline` builders).
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
- **Tonemap policy (Phase 2, per owner's no-stale-copy rule):** operator enum =
  Reinhard/ACES(default)/Uncharted2/AgX/KhronosPBRNeutral. The GL shader's
  "AgX" and "Tony McMapface" were admitted approximations — AgX is now the real
  minimal-AgX (inset/outset matrices + log2 sigmoid); McMapface is dropped until
  it ships as its actual 3D LUT (Phase 9 post-FX). Display encode is the exact
  piecewise sRGB OETF (old code used pow(1/2.2)) — a tiny shadow-tone delta vs
  GL screenshots is expected and correct. When preferences land (Phase 3+),
  remap the stored `toneMapOperator` int (old 5=McMapface → ACES).
- **Backbuffer pass layout (Phase 2):** tonemap fullscreen draw and ImGui render
  in ONE dynamic-rendering pass on the swapchain image (loadOp DONT_CARE — the
  fullscreen triangle covers every pixel; within-pass primitive order makes
  ImGui blend over the resolved scene legally). The scene target transitions
  COLOR_ATTACHMENT → SHADER_READ_ONLY between the passes; acquire semaphore now
  waits at COLOR_ATTACHMENT_OUTPUT (was TRANSFER for the old blit).
- **Screenshot readback design (Phase 2):** capture = swapchain image after UI
  (needs TRANSFER_SRC image usage — probed via surface `supportedUsageFlags`,
  universally present on desktop; `Swapchain::supportsCapture()`), copied into a
  HostReadback buffer inside the frame's command buffer, completed by polling
  the frame timeline value (zero added latency/stalls), then converted
  BGRA/RGBA→RGB and written by the retained pure-CPU PNG encoder. Rows come out
  top-to-bottom — the GL-era vertical flip is gone. Clean-viewer (UI-less) and
  stereo captures return with later phases.
- **VkPipelineCache is persisted** to cwd-relative `pipeline_cache.bin`
  (runtime files in this app are cwd-relative: preferences.json, imgui.ini,
  screenshots/). The driver itself validates/rejects foreign blobs; corrupt
  file → silently recreated empty. Now gitignored.
- **Quad-buffer stereo: RUNTIME-DETECTED, not assumed.** Stereo *rendering* (2 layers,
  `VK_KHR_multiview`, core 1.1) is available everywhere. Stereo *presentation* to a
  3D display (swapchain `imageArrayLayers = 2`, needs surface `maxImageArrayLayers >= 2`)
  is generally exposed **only on workstation GPUs (Quadro / Radeon Pro) with a stereo
  display + driver stereo enabled** — consumer GeForce/Radeon usually do NOT expose it.
  → Detect at runtime; **fall back to mono present** (default on consumer GPUs), like the
  current GL app's stereo→mono fallback. Optionally offer side-by-side. The renderer
  stays multiview-capable regardless; only the present path is conditional.
- **Phase 3 (2026-07-03): `scalarBlockLayout` + `imageCubeArray` promoted to HARD
  device requirements** (fail-loud in the `kRequiredFeatures` table). Every scene
  shader binds the shared `scene_types.h` structs with `layout(scalar)`; the point
  shadows need a CUBE_ARRAY sampler. Both are universal on the modern NVIDIA/AMD
  Windows drivers we target (Vulkan 1.2 core features).
- **Phase 3: one GPU-struct source of truth.** `assets/shaders_vk/scene_types.h`
  is included by BOTH GLSL and C++ (`Renderer/GpuTypes.h` static_asserts every
  sizeof). Grow it instead of writing per-shader UBO structs; blocks binding these
  structs must be `layout(..., scalar)`.
- **Phase 3: point-shadow cube faces render via multiview (viewMask 0x3F), one
  pass per light, NO geometry shader and NO linear-distance `gl_FragDepth`** —
  real reverse-Z hardware depth, receiver reconstructs the reference depth
  analytically. Cube-face projections come from `Projection.h`'s
  `perspectiveCubeFace`/`cubeFaceView` (deliberately no Y-flip — see the comment
  there before "fixing" it).
- **Phase 3: depth-only pipelines have no fragment stage** — the pipeline builder
  accepts an empty fragment SPIR-V for shadow casters (early-Z fast path).
- **Phase 3: spot lights were NOT ported** (the GL `SpotShadowCalculation` was a
  do-nothing stub, playbook C.4). If spots return later, implement a real 2D spot
  shadow; the scene loader currently ignores the `spotLights` JSON array.
- **Phase 3: `scene::Scene` is an interim host** for the render-relevant subset
  of office.scene (primitives, `localPath` models via Assimp, point lights,
  camera pose). Per-mesh JSON texture overrides, point clouds, clip planes,
  undo/save return with the full SceneManager port in Phase 6/8 — don't grow
  scene features onto `scene::Scene` beyond what rendering needs.
- **glslang gotcha: variable indexing of a bindless texture array needs
  `#extension GL_EXT_nonuniform_qualifier`** even for dynamically uniform
  indices (no `nonuniformEXT` call needed). Also note the MSBuild CustomBuild
  glslc step did NOT fail the build on a shader error once (exe still linked) —
  after adding shaders, confirm `$(OutDir)assets\shaders_vk\<name>.spv` exists.
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

**Phase 3 (PENDING — run these; subsumes the Phase 2 items)** — build
`Release|x64` from `StereoVista.sln`, run `bin\x64\Release\StereoVista.exe` (or
start from VS with the project dir as cwd — both work; assets resolve either way):
- [ ] Console shows `Loaded scene ...office.scene (N models, M point lights)`
      and the **office scene renders**: floor, table + chairs, shelf, vase,
      plant etc., colored per the scene file — not black, not magenta, no
      missing geometry (if the scene file is missing you get the 4-primitive
      default scene instead; that also counts, but say which one you saw)
- [ ] **Camera**: hold RMB + move mouse to look, WASD to move, Q/E down/up,
      Shift = fast. Motion is smooth, no inverted axes, pitch clamps before
      flipping over
- [ ] **Sun shadows**: with Sun enabled, models cast crisp directional shadows
      onto the floor; edges are slightly soft (3×3 PCF), no gross acne
      (dark striping on lit faces) and no heavy peter-panning (shadows
      detached from object bases); dragging **Sun direction** moves them live;
      shadows don't crawl/shimmer when only the camera moves (texel snapping)
- [ ] **Point-light shadows**: the ceiling-light point lights throw shadows in
      all directions (check under the table); toggling a light's
      **Casts shadows** checkbox adds/removes its shadow
- [ ] **Shadows toggle** off → all shadows vanish, scene stays lit
- [ ] **Sky**: combo offers Cubemap (repo `skybox/` textures) + Solid +
      Gradient; the background changes accordingly and is only visible where
      no geometry is (never through objects); sky intensity slider works
- [ ] **PBR sanity**: in the default scene (rename office.scene temporarily to
      see it) the sphere is metallic (mirror-ish highlights), the cube matte;
      in the office scene the Ceiling_Light primitive glows (emissive)
- [ ] **Tonemap (Phase 2 carry-over)**: Exposure slider brightens/darkens the
      SCENE but not the GUI; switching operators (ACES vs Reinhard vs AgX)
      visibly changes highlight rolloff
- [ ] **Screenshot (Phase 2 carry-over)**: Save screenshot → status shows
      `saved screenshots/...png`, PNG opens with correct colors + UI included
- [ ] Resize/maximize/minimize/restore work; drag-out ImGui viewports render;
      exit is clean; console has no `[vulkan][error]` spam; second run finds
      `pipeline_cache.bin`
- [ ] FPS in the panel is reasonable for your GPU with the office scene
      (double-digit minimum; report the number + GPU so we can baseline)

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
- **2026-07-03 (later) — Two fixes from the user's first Phase 3 run.**
  (1) Startup fatal `Pipeline 'forward PBR': set 1 binding 1 is a runtime
  array…`: the pipeline builder demanded a `bindingOverride()` capacity for
  every reflected runtime array, including ones living in sets replaced by
  `externalSetLayout` (mesh.frag's unsized `uTextures[]` in the
  MaterialSystem's set 1, whose reflected bindings `buildLayouts` discards
  anyway). Fix in `RHI/Pipeline.cpp`: skip the capacity check for external
  sets — the external layout is authoritative; a duplicated capacity could
  only drift. (2) User-reported **Peter Panning** (shadows detached, worse
  farther from the light): both receiver depth biases in mesh.frag were raw
  NDC constants. In reverse-Z *perspective* depth that constant is worth
  `ref·z²(far−near)/(near·far)` world units — tens of cm a few meters from a
  point light, growing **quadratically**; for the sun it scales with the
  fitted ortho depth range. Reworked: point bias now applied to face-space z
  in WORLD units (texel-footprint-scaled) *before* the ref projection; sun
  bias converted through new `FrameData.sunWorldToDepth = 1/(far−near)`
  (repurposed pad0; ShadowPass writes it); point normal offset trimmed
  1.5+2.0s → 1.0+1.5s texels. Builds green, all `.spv` fresh. Noted for
  later: user plans a **realtime ray-traced lighting mode** in a later phase
  (GL "Radiance" successor) — shadow code stays behind pass objects, nothing
  more needed now.
- **2026-07-03 — Phase 3 implemented (two sessions; builds green, user verify
  pending).** Restructure first, as the audit demanded: `recordFrame` now drives
  pass objects (`ShadowPass`/`ForwardPass`/`SkyboxPass` mirroring `TonemapPass`)
  over a new `FrameSubmission` scene input; the Phase 2 triangle scaffold and
  `triangle.vert/frag` are deleted; `ViewUniforms` std140 replaced by the shared
  scalar-layout `scene_types.h` (C++ static_asserts in `GpuTypes.h`). Landed:
  bindless `MaterialSystem` (sampler + texture2D[4096], PARTIALLY_BOUND +
  UPDATE_AFTER_BIND), `MeshBuffer` (tangent.w sign instead of a bitangent
  stream), one Cook-Torrance PBR path (C.3), per-light attenuation (C.5),
  linear-HDR-only lit pass with sRGB texture views (C.6), normal-offset +
  slope-scaled bias + single PCF path per light type (C.7), spot stub dropped
  (C.4), 4K texel-snapped sun map + point-shadow **cube array rendered via
  multiview 0x3F** (no geometry shader, real reverse-Z depth; depth-only
  pipelines with no fragment stage), multiview-native skybox (ray from
  `invViewProj`; cubemap/equirect/solid/gradient), interim `scene::Scene`
  office.scene loader + primitive factories + Assimp importer (textures into
  the bindless registry), `Platform/Paths` asset resolution, app-side fly
  camera + lighting/sky debug panel. Device: `scalarBlockLayout` +
  `imageCubeArray` now hard requirements. Fixed en route: mesh.frag needed
  `GL_EXT_nonuniform_qualifier` for variable (dynamically uniform) bindless
  indexing — caught because the missing `.spv` was noticed, NOT by MSBuild,
  which had reported success despite the glslc error (see §4 gotcha). Next:
  user runs the §5 Phase 3 checklist (subsumes Phase 2's), then commit the
  milestone and start Phase 4.
- **2026-07-03 — Audit: does any already-ported Vulkan code need rewriting?**
  Read the landed Phase 0-2 code (RHI + `Renderer` + `Application` + passes).
  **Verdict: no RHI-layer rewrite** — Device/Swapchain/Buffer/Texture/Pipeline/
  DescriptorAllocator/Barrier, the one-timeline frame loop + sync2 barriers, the
  layered multiview scene target, `TonemapPass`, the screenshot copy-to-buffer,
  and the ImGui integration are all sound and extend cleanly into Phase 3. **One
  restructure is required before Phase 3 lighting**, now written into the Phase 3
  detail (first ⚠ bullet) + §3: `Renderer::recordFrame` **inlines the whole scene
  pass** and the Renderer has **no scene input** (`updateViewUniforms` fakes a
  swaying camera + a compiled-in triangle). Extract `ShadowPass`/`ForwardPass`/
  `SkyboxPass` as objects like `TonemapPass`, drive a pass list, and feed a real
  camera + draw list + lights — otherwise Phase 3 rebuilds the `renderEye()`
  monolith. Also flagged: delete the Phase 2 demo scaffold as the mesh path
  lands, and migrate `ViewUniforms` std140 → scalar (A.1). Docs only.
- **2026-07-03 — Native-Vulkan Playbook authored (planning pass, no code).**
  Deep-read the current RHI + GL reference and researched current best practice
  (LunarG "Vulkan Renderer in 2025", Khronos Vulkan-Guide/-Samples, Schütz 2022),
  then wrote **`MIGRATION.md §6b`** — a *suggestions-not-mandates* playbook in four
  parts: **§A** cross-cutting practices to adopt early (scalar block layout, BDA +
  `buffer_reference`, bindless/descriptor-indexing, extended dynamic state,
  GPU-driven indirect, Vulkan-Profiles, Slang, debug-utils labels…), **§B**
  capability-gated features (multiview single-pass stereo, mesh/task shaders, ray
  query, RT pipelines, VRS, subgroups, async compute), **§C** nine concrete
  **OpenGL bugs/smells to FIX not port** (NV-only PC atomics that break AMD; GL
  clip-space PC depth; the 2,180-line uber-shader mixing Blinn-Phong + PBR; the
  do-nothing spot-shadow stub; hard-coded attenuation; baked-in tonemap;
  magic-number shadow bias; `material.textures[16]` binding model; two divergent
  overlay shader paths), and **§D** libraries (meshoptimizer, KTX/libktx, Tracy,
  Slang). Then **wired a `→ Nudges` line into every upcoming phase (3–7, 9) of §2**
  pointing at the relevant §6b letters/numbers, so the per-phase agent actually
  gets nudged from the read-me-first file. Verified the playbook's load-bearing
  claims against source: `bufferDeviceAddress`, `descriptorIndexing` (+ runtime
  array / partially-bound / variable-count / non-uniform-indexing),
  `scalarBlockLayout`, and `shaderDrawParameters` are **already enabled** in
  `Device.cpp` (with graceful fall-off); the `Pipeline` builder already exposes
  `bindingOverride` — so several §A nudges are device-ready and only need shader-
  side use (noted inline). Corrected §A.1/§A.8 wording to say "already enabled."
  **No behavioural code changed; docs only.**
- **2026-07-03 — Phase 2 implemented (RHI hardening); builds green; user verify
  pending.** New RHI: `Buffer`/`Texture` (VMA RAII, MemoryUsage intent, staged
  uploads, mip-chain blit generation), `Pipeline` + Graphics/Compute builders
  with **SPIRV-Reflect** (vendored @ vulkan-sdk-1.4.350.1) reflecting set/push
  layouts from SPIR-V — overrides only for bindless capacities and shared set
  layouts; `DescriptorAllocator` (growable pools, per-frame reset — Phase 1's
  fixed pools gone) + `DescriptorWriter`; shared sync2 helpers in
  `RHI/Barrier.h`; disk-persisted VkPipelineCache on `Device`. Renderer: blit
  → **TonemapPass** (fullscreen texelFetch resolve of scene layer 0, exposure +
  operator push constants, exact sRGB OETF) with ImGui in the same backbuffer
  pass; demo triangle now rides a staged VB/IB + mipmapped sRGB checker with
  HDR vertex intensities. Old shader's fake AgX replaced with real minimal-AgX;
  fake Tony-McMapface dropped (§4 tonemap policy). **Screenshot** ported:
  `Engine/Screenshot` now API-agnostic (PNG writer kept, GL capture deleted,
  file re-enabled); `Renderer::requestScreenshot` copies the presented
  backbuffer to a HostReadback buffer (swapchain TRANSFER_SRC probed),
  completion polled on the frame timeline, saved via the old writer — no
  stalls; debug-panel button + status. Debug panel: exposure slider + tonemap
  combo. Build wiring: all new files in vcxproj+filters, fullscreen.vert /
  tonemap.frag CustomBuild entries, `pipeline_cache.bin` +
  `StereoVista/screenshots/` gitignored. ✎ deltas: no CommandContext wrapper
  (raw vkCmd* + rhi helpers stay); pipeline-cache-on-disk instead of a handle
  registry. Fixed during build: friend-access on Pipeline privates from a
  namespace-local helper (MSVC C2248) — helper now returns layouts, builders
  assign. **Local build green Release+Debug x64. Next: user runs §5 Phase 2
  checklist; on pass → mark ☑, commit milestone, start Phase 3.**
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
