# StereoVista — TODO / Roadmap (Vulkan branch)

> This is the forward-looking work list for the Vulkan app. It **replaces** the
> old `docs/VULKAN_MIGRATION*.md` (the migration itself is done; its design
> rationale + session history live in git). **Guiding rule: improve, don't copy.**
> The OpenGL sources (still in-tree, `ExcludedFromBuild`) are the *behaviour*
> reference — when you port a system, fix the old bugs and use the better Vulkan
> pattern rather than transliterating GL. Delete each GL reference file once its
> replacement lands (see §7).

Legend: 🐞 bug · ✳️ missing feature · ⚡ optimization · 🧹 cleanup/delete.
Rough priority: **A → B → C** are the visible regressions you hit today; **D–F**
are the large feature re-ports; **G** is optimization; **H** is deletion.

---

## A. Known bugs / regressions (root-caused — fix at the source)

### A1. 🐞 Sphere cursor not rendering
The sphere cursor's visibility *is* wired (`Application.cpp:141` `setVisible(cursorType_==0)`),
so this is a render-path bug, not a toggle. Candidates, most-likely first — capture
one frame in RenderDoc to pick the culprit rather than guessing:
- **Winding vs. the fixed front face.** `SphereCursor::appendTo` does the GL
  two-pass transparency trick (`kCullFront` with depth-write ON, then `kCullBack`
  with depth-write OFF). `OverlayPass` sets cull mode dynamically but leaves the
  **front face fixed at the builder default `VK_FRONT_FACE_COUNTER_CLOCKWISE`**. If
  `SphereCursor::generateMesh`'s UV-sphere is CW-wound, the two passes are swapped:
  the back-face pass writes depth that self-occludes the front-face pass (drawn
  depth-write OFF) under the `Occluded` GEQUAL reverse-Z test → sphere vanishes.
  **Root fix:** verify the winding once; make the sphere front face explicit.
- **Lit-sphere fragment alpha.** Check `overlay.frag`'s SPHERE branch — the
  `(transparency, edgeSoftness, centerAlpha, isInner)` math may resolve to ~0 alpha
  (the `centerAlpha` collapse in `SphereCursor.cpp:20` is a GL-shader shortcut that
  may not match the ported fragment).
- **`m_positionValid` gate.** `appendTo` early-returns unless the depth-pick set a
  valid position — shared with plane/fragment. If *those* also don't show, it's the
  depth-pick (see A2/A3); if only the sphere fails, it's the sphere path above.
- **✳️ Improve, don't copy:** the sphere is a CPU-transformed 2k-vertex mesh with a
  fragile two-pass cull. Consider drawing it as an **analytic ray-marched billboard**
  (like the existing `DISC`/`RINGS` overlay kinds) — no winding pitfalls, no CPU
  mesh, correct depth from the sphere equation, cheaper.

### A2. 🐞 3D cursor "jumps" while tracking
`CursorManager::updateCursorPosition` has **two inconsistent reconstructions**:
- On a geometry hit it reconstructs at the **stale readback-rect centre pixel** with
  the **stale `invViewProj`** (`CursorManager.cpp:154–160`) — the cursor is anchored
  to where the mouse *was* when the query was queued (a frame of lag).
- On the background-cache path it reconstructs at the **current mouse NDC** with the
  current camera (`:188`).
Flipping between "hit" and "cache" snaps between two different screen positions →
the jump. A single centre-pixel sample also jumps across depth discontinuities
(object silhouettes).
- **Root fix:** always reconstruct at the **current mouse NDC** using the *sampled*
  depth (screen-space then tracks the mouse with zero lag; only the depth value is
  one frame stale, which is invisible). Unify both paths on this. Sample a small
  **NxN neighbourhood** and take the nearest/median depth to kill silhouette jumps.
- **Related:** `keepLastDepthOnBackground` defaults **off** and nothing enables it,
  so the anti-flicker last-depth cache never runs — wire it on + expose it (see C2).

### A3. ✅ ~~Zoom-to-cursor doesn't zoom to the surface point~~ (fixed)
Fixed as prescribed: the scroll path in `updateCamera` now refreshes
`UpdateCursorInfo` from the current pick (valid flag included, so a miss can't go
stale) right before `ProcessMouseScroll`. The same change fixed **scroll zoom
being entirely dead**: `updateCamera` runs after `ImGui::Render()`, whose
`EndFrame` zeroes `io.MouseWheel` — the wheel is now captured in `run()` before
`Render` (`wheelThisFrame_`).

### A4. ✅ ~~Camera is missing its adaptive behaviour~~ (fixed)
Fixed as prescribed: `Application::updateCameraDepth` feeds the centre-of-screen
distance every frame from the async depth readback (1×1 centre rect, no stall)
→ `UpdateDistanceToObject` + `AdjustMovementSpeed` (gated on the `adaptiveSpeed`
setting, default ON like GL). The readback now carries its pick-viewport index
so a viewport switch pauses the feed instead of poisoning the other camera.
`AdjustMovementSpeed` was rewritten with a constant-TIME exponential response
(the GL ramp compounded per FRAME — fps-dependent and seconds-slow on large
speed gaps), the empty-space test no longer uses float equality, and the
distance curve scales with the real scene bounds via `Camera::sceneSize` (the
GL "largest model dimension" only measured the first model's untransformed
vertices). **Scroll zoom was redesigned distance-proportional**: each step
covers a fraction of the live distance to the zoom target (recomputed per
frame → exponential ease-in/out), replacing the GL scheme that applied a
distance factor twice and scaled by the possibly-stale fly `MovementSpeed`
(0.01× crawl near geometry, ~20× blast over background);
`CalculateScrollFactor` is gone. `speedFactor` now actually applies (fly, both
modes, and zoom; the old code overwrote `MovementSpeed` with a Shift×4 boost —
Shift now flies DOWN and Space UP, GL parity). Selection is **Ctrl+left-click**
again (plain LMB is orbit-only). `MouseSensitivity` and the smooth-scroll
tunables were already live settings.

---

## B. Camera & navigation — finish the port (improve where you can)

- ✳️ **Centering / framing:** ✅ **double-click-to-centre is wired** (ImGui
  double-click detection with spatial slop — the GL check was time-only; empty
  space centres on the background-plane point; the completion callback warps
  the mouse to the viewport centre only while it's still over the 3D view).
  Still open: **F** (frame selection), **C** (centre on cursor), **Home**
  (reset) via a state animation to the fitted view.
- ✳️ **Standard views:** numpad 1/3/7/5 (front/right/top/iso) + Ctrl-variants
  (back/left/bottom) via a state animation to the fitted view.
- ✅ ~~**Smooth-scroll settings**~~: exposed in the Settings panel (Camera tab →
  Scrolling: smooth toggle, momentum, deceleration, max velocity).
- ✅ ~~**Frame-rate independence**~~: `AdjustMovementSpeed` uses a constant-time
  exponential response, `UpdateScrolling` integrates the notch momentum with
  `deltaTime`, and the non-smooth scroll path is a fixed fraction of the zoom
  reference distance per notch (event-driven). See A4.
- ✳️ **SpaceMouse (3DConnexion):** the GL `SpaceMouseInput`/`ThreeDConnexionSync`
  (`Engine/`, excluded) drive the camera via TDxNavLib — portable, no GL. Re-wire to
  the Vulkan `Camera` (`Synchronize*Quaternion*` helpers already exist for the
  hand-off).

---

## C. 3D cursors — restore settings, caching, presets (see A1/A2 first)

- ✳️ **C1 — appearance settings** (all exist on the cursor classes, none exposed):
  sphere (color/transparency/edgeSoftness/centerTransparency/innerSphere+color+factor/
  fixedRadius), fragment ring (inner/outer radius, border, colors), plane
  (diameter, color), plus **show 3D cursor**.
- ✳️ **C2 — scaling + caching** (`BaseCursor`/`CursorManager` support them; unused):
  scaling modes (normal / fixed / constrained-dynamic / logarithmic), base size,
  min/max diff; and the **background last-depth cache** (`keepLastDepthOnBackground`
  + timed/distance mode + time/distance) — enable + expose (also mitigates A2).
- ✳️ **C3 — orbit-centre marker** config (show / always-show / color / radius) —
  `CursorManager` has the API, no UI.
- ✳️ **C4 — cursor presets:** re-port `cursor_presets.json` (GL `CursorPresetManager`)
  — save/load named cursor configs.
- ✳️ **C5 — `CursorPreview3D` thumbnail:** the render-to-texture preview for the
  settings panel. The `preview_lit` shaders + `FrameSubmission::recordAux` hook are
  already in place — wire it when the cursor-settings panel lands.
- 🧹 `CursorManager::setCapturedCursorPositionWithSync` is a dead no-op stub from the
  old sync system — remove it (and the `enableSync` param) when touching this.

---

## D. GUI — replace the debug panel with the real settings GUI

> **Superseded:** the full GUI/UX remake plan — including everything in this section
> plus the UI-facing parts of §C/§E — now lives in **`docs/UI_REDESIGN.md`** (coherence
> contracts, pass roadmap, Status Board). Do GUI work from that plan; this section
> stays only as the original gap analysis.

Today everything is driven from one interim **debug panel** in `Application.cpp`
(`buildUi`); the old structured GUI (`Gui/GUI.cpp`, ~8.8k excluded lines) is the
reference. This is the single biggest gap.
- ✳️ Port the panels as **`Gui/panels/*`** (don't rebuild one 8k-line file): Settings
  (camera / stereo / mouse+scroll / lighting / sky), 3D-cursor settings (§C),
  per-object + per-mesh material panels, sun/light controls, scene hierarchy.
- ✅ **Preferences persistence** (`preferences.json`): done in UI redesign Pass 0
  (`Gui/Preferences.h` serializes `Gui::Settings`; GL files migrate their
  overlapping subset; the GL `ApplicationPreferences` blob stayed dead).
- ✅ **Shortcuts** (`shortcuts.json`): done in UI redesign Pass 0
  (`Core/Shortcuts.h` — rebindable bindings → command ids; GL profile files
  migrate). The binding *editor UI* lands with the redesign's Settings pass.
- ✅ `PluginContext::preferences()` restored in UI redesign Pass 0 (returns the
  new `Gui::Settings`).

---

## E. Scene & data systems — port from the excluded GL sources

- ✳️ **SceneManager:** scene **save/load/merge**, the scene **hierarchy**, multi-object
  management, and scene-op undo. The Vulkan app has only a read-only `scene::Scene`
  (loads `office.scene`). This unblocks tool/measurement persistence too.
- ✳️ **SnapshotManager:** camera bookmarks/snapshots (state animation restore already
  exists on `Camera` via `StartStateAnimation`).
- ✳️ **Spot lights + spot shadows:** the GL spot path was a non-functional stub
  (`SpotShadowCalculation` returned 0) — if wanted, implement a *real* 2D spot shadow
  rather than porting the stub. The scene loader currently ignores `spotLights`.
- ✳️ **Stereo/XR follow-ups:** stereo screenshots (currently left-eye only) + anaglyph
  mode; VR controllers/hand input; per-eye HMD 3D-cursor picking; HMD recenter /
  world-scale UX.

---

## F. Point clouds

- ✳️ **Out-of-core LOD:** the GL `OctreePointCloudManager` (octree LOD + disk cache
  for clouds that exceed VRAM) is excluded/unported — the compute rasterizer handles
  clouds that fit. Re-implement LOD selection feeding the compute path.
  **Note:** the SLPK/I3S plan delivers this via the I3S node-tree streamer —
  see `docs/SLPK_ROADMAP.md` (Phase S3) before building a standalone octree.
  **Research & staged design (in-shader density LOD → CLOD reduction pass →
  hierarchical/out-of-core via the I3S streamer): `docs/POINTCLOUD_LOD.md`.**
  Stage 1 (per-batch density LOD in the rasterizer prologue, points-per-pixel
  budget, panel toggle) is ✅ implemented — it applies to every batch source
  (flat clouds + I3S pool pages); Stages 2–3 remain.
- ⚡ See G for the deferred compute-path perf items.

---

## F2. SLPK / I3S support (flagship feature — separate roadmap)

Full professional support for Esri Scene Layer Packages / OGC I3S (loading,
streaming LOD rendering, tools, editing, export) is planned as its own phased
roadmap: **`docs/SLPK_ROADMAP.md`** (market research, format primer, Vulkan
streaming architecture, phases S0–S6 with acceptance gates).

---

## G. Optimization pass (folded in from the old native-Vulkan playbook)

Several were deliberately deferred during the migration — revisit when profiling
justifies them (add **Tracy** first, below, so decisions are data-driven).

**Done (point-cloud perf fix):** the per-frame `PointCloudDispatch` array used to
live in HOST-VISIBLE memory and was read through BDA by every geometry workgroup
(whole 400-byte struct) and by the colour-lookup pass per pixel — uncached PCIe
traffic in the hottest loops, and the main regression vs the GL uniforms path.
It is now staged and copied DEVICE-LOCAL at the top of `recordCompute`. The
point-cloud buffer references are also `restrict` now (glslang otherwise emits
`AliasedPointer`, blocking load hoisting across the framebuffer atomics), the
dispatch struct stride is 16-byte aligned (400 B), and the lookup dispatch uses
a 2D grid so 8K-class targets stay under `maxComputeWorkGroupCount[0]`.
**Done (async compute queue):** the point-cloud compute (dispatch copy + clears
+ rasterize/HQS + lookup) now runs on a second queue — a dedicated compute
family when the GPU has one, else a second graphics-family queue — overlapped
with the same frame's upload/shadow/forward work. The frame is three
submissions chained by two new timelines (upload + compute; full diagram in
`Renderer.h`); cross-queue buffers (cloud storage, per-pixel buffers, dispatch
data) are `VK_SHARING_MODE_CONCURRENT` via `BufferDesc::shareGraphicsCompute`,
so there are zero queue-family ownership transfers (release/acquire helpers for
EXCLUSIVE resources exist in `RHI/Barrier.h` for the RT phase). Single-queue
GPUs and the debug toggle (Point Clouds panel; status in Diagnostics) fall back
to the old inline recording, decided per frame. For RT/GI: grab
`Device::computeQueue()` / `immediateSubmitCompute()`.
**Done (single-pass multi-view point-cloud geometry):** the rasterize / HQS
depth / HQS colour dispatches used to run once PER VIEW — in stereo/XR every
point was fetched, bit-unpacked and clip-tested twice. One dispatch per cloud
now covers all views: the shader reads the point streams (the pass's dominant
memory traffic) and decodes once, then projects + writes per eye
(`pointcloud_common.glsl` per-view prologue; `PointCloudComputePush.viewCount`;
the dispatch array stays cloud-major/view-minor for the lookup pass, the
geometry shader strides `SV_PC_DISPATCH_STRIDE` from the pushed view-0 struct
to its siblings). Atomic traffic is unchanged (both eyes must be written);
stream reads, decode ALU, per-batch cull/LOD prologue and the vkCmdDispatch
count halve. The decode precision level is the most precise any visible view
requests. Mono compiles to the same work as before (view loop of 1).
- ⚡ **Subgroup ops** in the HQS colour-accumulate pass (`subgroupMin`/ballot) instead
  of per-pixel atomics — a large compute win on dense clouds.
- ⚡ **Variable-rate shading** (`VK_KHR_fragment_shading_rate`) on the point-cloud
  resolve and for stereo/foveated XR.
- ⚡ **Timeline-deferred cloud destruction:** unload currently `waitIdle`s
  (`PointCloudGpu::destroy`) — retire the allocation on the frame timeline instead.
- ⚡ **GPU-driven draws:** `vkCmdDrawIndexedIndirectCount` + a compute cull pass for
  meshes (`shaderDrawParameters` is already enabled); batch overlay widgets into an
  instanced/indirect draw as counts grow.
- ⚡ **Mesh/task shaders** for models (per-meshlet cull/LOD) — capability-gated. Keep
  the Schütz atomicMin *compute* rasterizer for point clouds (mesh shaders don't beat
  it for pixel-sized points).
- ⚡ **meshoptimizer** (meshlets / vertex-cache / quantization / LOD) for model upload;
  **KTX / BCn** compressed textures for large material sets (keep stb for loose PNGs).
- ⚡ **Sparse buffers** for clouds that exceed VRAM (GL sparse was dropped, not ported).
- ⚡ **Tracy** CPU+GPU profiler (vendored + wired) — verification is visual, so this is
  the fastest way to see where frame time goes. **Do this first.**
- ⚡ Extended dynamic state / pipeline-cache warming to cut first-use hitches as passes
  multiply.

---

## H. Deferred GI / post-FX — re-add natively (was "Phase 9")

> **Superseded by a dedicated roadmap:** the full researched, phased,
> multi-agent build-out — IBL, GTAO, SSR, procedural sky + volumetric clouds +
> froxel fog, and the RT tier (LOD-driven acceleration structures for
> photogrammetry, ray-traced shadows/AO/reflections, DDGI) with a hardware
> fallback ladder — now lives in **`docs/RENDERING_ROADMAP.md`**. Do rendering
> work from that plan; the list below stays as the original gap analysis.

Re-implement with modern native Vulkan (inline `ray_query`, compute, async
builds) rather than any legacy SSBO/FBO approach:
- ✳️ **SSAO** and **Bloom/HDR bloom** as Vulkan **compute** passes.
- ✳️ **Ray-traced shadows / AO / reflections** via **`VK_KHR_ray_query`** from inside
  the existing fragment/compute shaders (fall back to shadow maps where RT is absent)
  — replaces the old **BVH Radiance** software path.
- ✳️ **DDGI** and **multi-bounce GI** via `VK_KHR_ray_tracing_pipeline`; BLAS/TLAS
  build from the same mesh buffers (maps onto the planned two-level BVH rework).
- ✳️ **Voxel Cone Tracing** if still wanted, or supersede it with the RT path above.
- ✳️ Re-add the **lighting-mode** UI (Shadow / VCT / Radiance) once >1 mode exists;
  until then the `LightingMode` enum's VCT/Radiance entries are inert (see §H below).

---

## I. What still needs deleting 🧹

- **Now:** `docs/VULKAN_MIGRATION.md` + `docs/VULKAN_MIGRATION_STATUS.md` (superseded
  by this file) — being removed with this change.
- **As each feature above lands, delete its excluded GL reference:** `Gui/GUI.cpp`+
  `Gui/Gui.h` (→ D), `Core/SceneManager` (→ E), `Core/SnapshotManager` (→ E),
  `Core/CursorSynchronizer`+`CursorSyncState.h` (→ A2 root fix / cursor sync),
  `Engine/ShortcutManager` (→ D), `Engine/SpaceMouseInput`+`Engine/ThreeDConnexionSync`
  (→ B), `Cursors/CursorPresets` (→ C4), `Gui/CursorPreview3D` (→ C5),
  `Engine/OctreePointCloudManager`+`Utils/octree.h` (→ F).
- **`headers/Gui/GuiTypes.h`** (still compiled for cursor enums): prune the dead GL
  fields — the `LightingMode` VCT/Radiance entries, SpaceMouse modes, and the rest of
  the GL `ApplicationPreferences` struct — once the Vulkan prefs/enums have a home (D).
- **Dead stub:** `CursorManager::setCapturedCursorPositionWithSync` (§C).
- Once **all** Group B files are ported and deleted, the `Engine/` folder holds only
  `XRSession` / `Screenshot` / `StbImageImpl` — fold those into `Renderer/` (or an
  `XR/`) and retire `Engine/`.
