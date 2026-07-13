# StereoVista — Rendering Quality Roadmap (GI, RT, Volumetrics)

> **Scope:** a fresh, native-Vulkan build-out of the engine's lighting and
> image quality — image-based lighting, ambient occlusion, reflections,
> multi-bounce global illumination, ray-traced shadows/AO/reflections, a
> procedural atmospheric sky with volumetric clouds, and volumetric fog / god
> rays — engineered **performance-first** with a real fallback ladder so the
> app still runs (and looks decent) on older, non-RT hardware. Visualization is
> the product: **temporal stability, correctness and predictable frame time
> matter more than filmic flash.**
>
> **This document is direction, not law.** It is written for **multiple agents
> working in parallel**. An agent may deviate from, re-order, or re-design any
> part mid-implementation **without asking**, as long as it (a) honors the
> Coherence Contracts in §3, (b) keeps every quality tier's fallback working,
> and (c) preserves the house rendering rules (reverse-Z, [0,1] depth, multiview
> stereo, bindless, BDA, async compute). When an agent changes the plan, it
> updates the Status Board (§10) and this doc.
>
> **Implementation philosophy — fully integrate proven libraries, don't rewrite.**
> Every work package names a canonical open-source implementation (§9).
> **Prefer vendoring and fully wiring up the real library/SDK over
> reimplementing it** — proven code is lower-risk and better-tested than a
> from-scratch rewrite. Only hand-roll a system when no suitable library exists,
> or when the only available one can't be made cross-vendor (C6). When a library
> *is* adopted, integrate it properly (not a token subset): download + extract
> into `headers/libs/` and/or `dependencies/`, wire include/lib dirs and every
> source file into `StereoVista.vcxproj` **and** `.vcxproj.filters`, and
> post-build-copy any runtime DLL, exactly like the existing vendored deps.
> CI/MSBuild compile only what is listed. **The one hard gate on every library is
> C6: it must run on all GPU vendors (AMD, Intel, NVIDIA).**
>
> Companion docs: `docs/TODO.md` §H (the gap this replaces), `CLAUDE.md`
> (architecture + house rules), `docs/SLPK_ROADMAP.md` (the streaming LOD system
> the RT layer rides on), `docs/POINTCLOUD_LOD.md`.

Legend: 🎯 headline quality win · ⚡ performance-critical · 🧱 foundation others
depend on · 🔀 fallback path · 🧪 acceptance gate (manual/visual — no test suite).

---

## 0. Goals, non-goals, and what "quality" means here

**Goals**
- A single **Rendering Quality** ladder (Low → Ultra + Auto) that scales one
  scene from an Intel iGPU to an RT GPU, degrading feature-by-feature, never
  crashing or stalling — the current shadow-mapping look is the floor, not a
  regression.
- **No-RT tier that genuinely looks good:** IBL + AO + SSR + procedural sky +
  volumetric clouds + volumetric fog, all without ray tracing.
- **RT tier that adds correctness:** ray-traced shadows, AO, reflections, and
  multi-bounce diffuse GI — *without wrecking frame time on photogrammetry-scale
  geometry.*
- **Stereo/VR-first:** every technique is multiview-correct; world-space signals
  (probes, sky/atmosphere LUTs, the TLAS) are computed once and sampled by both
  eyes.

**Non-goals (for now)**
- No multi-bounce GI on the no-RT tier (decided): IBL is the indirect-diffuse
  ceiling without RT. (A no-RT SSGI/voxel path is explicitly *out* — not worth
  the failure modes.)
- Point clouds stay **out of the lighting/AS pipeline** (decided) — they keep
  the Schütz compute rasterizer and are composited by depth; they neither cast
  nor receive RT/GI/volumetric lighting in v1.
- Progressive "converge when static" path-traced reference mode is **low
  priority** (Phase 7, optional).
- Full ReSTIR path tracing is a *future* direction, not a v1 target (§9 note).

---

## 1. Constraints that shape the design (read first)

These come from the actual code (`headers/Renderer/*`, `src/RHI/Device.cpp`,
`headers/Loaders/Slpk/*`) and from the product:

1. **Single-pass multiview stereo** (`Renderer.h`). The scene is drawn once into
   a layered target, camera is a per-view array indexed by `gl_ViewIndex`.
   ⇒ **Prefer world-space techniques** (probes, TLAS, atmosphere LUTs) — free for
   the second eye. Screen-space passes (SSAO, SSR, TAA) **cost per eye**; keep
   them lean and reduced-res.
2. **Forward renderer, no G-buffer today.** Ambient is a flat term in
   `mesh.frag`; there is **no IBL**. ⇒ Biggest ROI is Phase 1, and we add a
   **thin G-buffer + depth prepass** (§3) to unlock screen-space + temporal + a
   cheap primary hit for hybrid RT. Forward shading stays.
3. **RT is already scaffolded, minimally and correctly.** `Device` enables
   `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` (+ `deferred_host_ops`)
   when present (`rayTracingSupported()`); **every `MeshBuffer` already carries
   `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY` + `SHADER_DEVICE_ADDRESS`**
   usage. We use **inline ray queries** (no RT pipeline / SBT) — enough for
   shadows, AO, reflections, and GI probe updates from existing frag/compute
   shaders. These are **cross-vendor Khronos extensions** — the RT tier targets
   AMD (RDNA2+), Intel Arc, and NVIDIA RTX equally, with **no vendor-proprietary
   extension as a hard dependency** (contract C6).
4. **Massive, out-of-core, LOD-streamed geometry** (SLPK/I3S; photogrammetry with
   very high triangle counts). Residency churns every frame with timeline-based
   deferred destruction (`MaterialSystem` graveyard, `MeshBuffer::createForStreaming`).
   ⇒ The **AS layer must ride the streamer's LOD** and be **budget-managed** (§5).
   This is the make-or-break performance decision.
5. **Async compute queue** exists (`Device::computeQueue()`,
   `immediateSubmitCompute()`) and is earmarked for AS builds + GI compute.
6. **Performance is a first-class feature.** Every RT/volumetric signal runs
   reduced-res + temporally accumulated + denoised; ray/step budgets are capped;
   nothing blocks the graphics queue. Add **Tracy** early (§7) so this is
   data-driven.

---

## 2. Target frame graph

The **Ultra** path (everything on). Each tier below removes stages from the
right-hand column; the geometry/shading spine never changes.

```
 async compute queue                    graphics queue
 ───────────────────                    ──────────────
                                        [ upload ring flush ]
 BLAS builds/compaction (throttled) ──► [ shadow maps (sun + point cubes) ]   🔀 RT-off fallback
 TLAS refit (resident instances)   ──►
                                        [ DEPTH + thin G-BUFFER prepass ]  🧱  (normal, rough, motion)
 atmosphere LUTs (transmittance,        [ (forward pass depth = EQUAL) ]
   multiscatter, sky-view, aerial) ──►
 froxel fog: inject + scatter      ──►
 GTAO  (compute, from G-buffer)    ◄──  depth/normal   🔀 no-RT AO
 RT sun-shadow mask (ray_query)    ◄──  depth          🔀 → shadow maps
 RT AO (ray_query, world-space)    ◄──  G-buffer       🔀 → GTAO
 DDGI probe update (ray_query)     ──►                 🔀 → IBL only
 RT reflections (ray_query)        ◄──  G-buffer       🔀 → SSR → IBL spec
 denoise + temporal (shared)       ◄──►
                                        [ FORWARD SHADING (multiview) ]
                                          direct: RT-mask or PCSS shadows
                                          indirect diffuse: DDGI or IBL irradiance
                                          indirect specular: RT-refl or SSR or IBL prefilter
                                          × AO (RTAO or GTAO)
                                        [ point-cloud compute resolve ]  (unlit, depth-composited)
                                        [ procedural SKY + volumetric CLOUDS ]
                                        [ volumetric FOG composite (froxel) ]
                                        [ BLOOM (HDR down/up) ]
                                        [ TAA resolve ]  (per eye)
                                        [ TONEMAP → sRGB ]
                                        [ OVERLAY (depth-tested) + ImGui ]
                                        [ present / XR eye resolve ]
```

Queue rule: AS builds, atmosphere/fog/GI/AO/refl compute ride the **async
compute** queue and chain into the scene batch by timeline semaphores, exactly
like the point-cloud compute does today (`Renderer.h` class comment). RT signals
consumed by forward shading must be denoised and ready before the shading pass
waits on the compute timeline.

---

## 3. Coherence Contracts (the "don't collide" section)

Parallel agents MUST agree on these. Changing a contract is allowed but must be
done in this doc + announced on the Status Board so dependents adapt.

### C1 — Descriptor sets (additive; sets 0/1 stay as they are)
- **set 0** — per-frame (existing): FrameData UBO, lights SSBO, materials SSBO,
  shadow maps, sky cube/equirect. *May gain a handful of scalars in FrameData
  (C4).*
- **set 1** — bindless materials (existing, unchanged).
- **set 2 — Environment / GI** (NEW, world-space, shared by both eyes & compute):
  IBL irradiance cube · IBL prefiltered-specular cube · BRDF LUT · atmosphere
  LUTs (transmittance 2D, sky-view 2D, aerial-perspective 3D) · froxel scattering
  volume (3D) · DDGI irradiance + visibility atlases + DDGI constants ·
  **`accelerationStructureEXT` TLAS**. Partially-bound; a dummy fills any slot a
  tier leaves empty (same discipline as the sky dummies).
- **set 3 — Screen-space** (NEW, per-view targets): depth · G-buffer normal ·
  G-buffer roughness/metallic/flags · motion vectors · AO result · reflection
  result · sun shadow mask · indirect/GI buffer · history buffers.

Forward shading binds set 0/1/2/3; compute effect passes bind set 2/3 as needed.

### C2 — Thin G-buffer formats (written in the depth prepass)
Keep it **thin** (bandwidth dominates on big scenes). Baseline:
- Depth: `D32_SFLOAT`, reverse-Z (existing convention).
- Normal: `R16G16_SNORM` — **octahedral-encoded world normal**.
- Material: `R8G8B8A8_UNORM` — roughness, metallic, materialFlags, spare.
- Motion: `R16G16_SFLOAT` — screen-space UV motion (this-frame → prev-frame),
  computed from `prevViewProj` (C4). Dynamic objects add their own delta later.

The prepass is multiview. The forward pass switches to depth-test `EQUAL`,
depth-write off (a straight overdraw win on photogrammetry). Provide a
`GBUFFER_ONLY` spec-constant variant of `mesh.vert`/a trimmed fragment so the
prepass is cheap.

### C3 — RenderQuality (the toggle spine)
One struct drives everything; `Auto` fills it from device caps + a GPU tier
heuristic; `rayTracingSupported()==false` forces every RT field to its 🔀 fallback.
```
struct RenderQuality {
  enum Tier { Low, Medium, High, Ultra, Auto };
  bool  ibl;                       // Phase 1
  enum  Ao   { Off, GTAO, RTAO }   ao;
  enum  Refl { Off, SSR, RT }      refl;
  enum  Shadow { Map, RT }         shadow;
  enum  Gi   { Off, IBL, DDGI }    gi;
  bool  clouds, atmosphere, fog;   // Phase 2
  bool  bloom, taa;
  float rtResolveScale;            // 0.5 / 0.25 for RT+SSR signals
  uint  cloudUpdateFraction;       // 1/4, 1/16 checkerboard
  // ... budgets: AS memory cap, rays/pixel, fog steps, cloud steps
};
```
Forward-shader permutations are selected by **specialization constants**
(`SPEC_IBL`, `SPEC_DDGI`, `SPEC_RT_SHADOW`, `SPEC_AO_TEX`, `SPEC_REFL_TEX`) — build
only the handful of combinations a tier actually uses; cheap per-frame toggles
stay uniform branches.

### C4 — FrameData / FrameSubmission additions
- FrameData gains per-view `prevViewProj` (motion vectors), `frameIndex` /
  temporal jitter, and small blocks for atmosphere params, fog params, and DDGI
  constants (or reference them via BDA to keep the UBO small).
- `FrameSubmission` gains `RenderQuality`, `AtmosphereState` (sun/planet/turbidity),
  `CloudState` (coverage/weather/wind/height), `FogState` (density/color/aniso).
- TAA jitter is baked into the projection **in the `renderer::projection`
  factories only** (house rule) — never hand-rolled per pass.

### C5 — House rules that still bind (from CLAUDE.md)
Reverse-Z, `GLM_FORCE_DEPTH_ZERO_TO_ONE`, Y-flip-in-projection (CCW front face),
shared C++/GLSL structs in ONE `layout(scalar)` header + `static_assert`,
bindless textures, per-object data via push constants / BDA, `.spv` verified in
`$(OutDir)`, every new source wired into `.vcxproj` + `.filters`. Nothing above
the RHI includes a Vulkan header except the new AS manager (which lives in RHI).

### C6 — Cross-vendor baseline + optional vendor accelerators (hard constraint)
**The baseline must run, and be actively optimized, on ALL GPU vendors — AMD
(RDNA2+), Intel Arc, and NVIDIA RTX.** It is built only on cross-vendor Khronos
extensions — `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
`VK_KHR_deferred_host_operations`, `bufferDeviceAddress` — using **inline
`ray_query`** (the RT-pipeline extension is deliberately not used; ray_query has
the broadest driver coverage). Every feature must run correctly *and perform well*
through this baseline **with no vendor-exclusive extension as a hard dependency**;
the app is fully functional and looks correct on a GPU that exposes only the KHR
set. "Optimized for all cards" is a requirement, not an aspiration — profile and
tune the baseline per-vendor (subgroup width, wave/subgroup ops, memory access
patterns), not just on NVIDIA.

**Vendor-specific features are welcome — as optional, runtime-detected
accelerators, never as the only path.** If a card offers more (NVIDIA Shader
Execution Reordering, Opacity/Displacement Micromaps, the RTXGI v2 Neural Radiance
Cache, cooperative-vector / tensor paths; AMD or Intel equivalents), **enable it as
a progressive-enhancement fast-path or quality boost behind an identical
cross-vendor fallback**, detected at runtime (`vkEnumerateDeviceExtensionProperties`
/ feature query) and defaulting safely off where absent. The contract is only:
(a) the KHR baseline alone looks correct and runs well on every vendor, and (b) no
vendor path is ever *required*. So NVIDIA users can get extra speed/quality while
AMD/Intel users still get the full feature — that asymmetry is fine and expected.

Consequences for the reference implementations (§9): NVIDIA-**authored** libraries
(NRD, RTXGI-DDGI) are **fully vendored and integrated as the primary path** — safe
because their cores are plain SPIR-V compute + standard KHR `ray_query` and
genuinely run cross-vendor (NRD's ReBLUR/ReLAX/SIGMA, RTXGI-DDGI's `DDGIVolume`
have no hard NVIDIA-hardware dependency). Their optional vendor sub-features (RTXGI
v2's Neural Radiance Cache, any NRD vendor fast-paths) may be **exposed as opt-in
accelerators** on capable cards behind the cross-vendor default. A homegrown
replacement (e.g. SVGF) is the plan only if a needed library's *baseline* can't be
built cross-vendor. **Acceptance requires each integrated library, and the RT tier
as a whole, to run, look correct, and perform well on at least one AMD *and* one
NVIDIA GPU** (Intel Arc too where available).

---

## 4. Quality tiers & fallback matrix

| Feature | Low (iGPU / very old) | Medium (no-RT dGPU, GTX 10xx) | High (RTX 2060 / RX 6600) | Ultra |
|---|---|---|---|---|
| Direct shadows | Shadow map (PCF) | Shadow map (PCSS) | **RT mask** (denoised) | RT mask |
| AO | Off / cheap SSAO | **GTAO** | **RT AO** | RT AO |
| Indirect diffuse | Flat ambient | **IBL irradiance** | IBL | **DDGI** (cascaded) |
| Indirect specular | Off | **IBL prefilter + SSR** | **RT reflections** + SSR near | RT reflections |
| Sky | Gradient/cubemap | **Procedural atmosphere** | Atmosphere | Atmosphere |
| Clouds | Off | **Volumetric (¼-res)** | Volumetric (½-res) | Volumetric |
| Fog / god rays | Off | **Froxel fog** (map-shadowed) | Froxel (RT-shadowed) | Froxel (RT) |
| Bloom / TAA | Bloom | Bloom + TAA | Bloom + TAA | Bloom + TAA |

`Auto` picks a column from `rayTracingSupported()` + VRAM + a coarse GPU-class
guess, then the user can nudge individual rows. Every 🔀 downgrade is exercised
and must produce a correct (if plainer) image.

---

## 5. ⚡ RT strategy for photogrammetry-scale scenes (the critical section)

Brute-force RT dies on terabyte photogrammetry. The whole RT layer is built so
**ray cost tracks on-screen coverage, not scene size**, and never stalls the
frame. Non-negotiable principles:

1. **Ride the streamer's LOD.** Build each tile's BLAS from the **exact I3S/SLPK
   node the rasterizer selected** (screen-space-error driven — the streamer
   already computed it). Distant photogrammetry uses coarse BLAS automatically;
   ray work is bounded by what's visible. Refine/coarsen swaps the tile's BLAS
   the same way residency already swaps its mesh.
2. **Build once, trace fast, compact.** Streamed tiles are static once resident:
   build with `PREFER_FAST_TRACE`, then **compact** (query compacted size → copy
   → free scratch/original; ~40–55% memory back). No per-frame BLAS rebuilds.
   Only genuinely moving/edited objects get `ALLOW_UPDATE` + refit.
3. **TLAS refit, not rebuild.** One instance per resident node; **refit** every
   frame from transforms (KB-scale, async). Full rebuild only when the residency
   set changes (a tile paged in/out) — still cheap.
4. **Opaque fast traversal.** Mark photogrammetry geometry `VK_GEOMETRY_OPAQUE_BIT`
   so ray_query never breaks traversal for an any-hit test. Alpha-masked I3S
   vegetation (a small minority) does an alpha test in the query candidate loop
   only where its material flag says so.
5. **Budget-managed residency.** AS memory shares the VRAM budget
   (`Device::deviceLocalBudget()`, SLPK §6.5) with a configurable cap. Over
   budget, or for tiles beyond a distance/size threshold: **skip the BLAS** —
   those tiles simply don't contribute to RT secondary rays and fall back to
   raster + probe/screen-space. Degrade, never stall.
6. **Reduced-res, 1 spp, denoised, temporal.** Every RT signal (shadow mask, AO,
   reflections, GI probe rays) is traced at ½–¼ res, one ray per pixel, then
   temporally accumulated + spatially denoised (§7). RT cost decouples from
   display resolution and from stereo primary fill.
7. **Async, throttled builds.** BLAS builds + compaction run on
   `computeQueue()` / `immediateSubmitCompute()`, overlapped with graphics, with
   a **builds-per-frame cap** so paging a big tile can't spike the frame. Retire
   AS through the **frame-timeline graveyard** pattern the `MaterialSystem`
   already uses.
8. **Skip the negligible.** No BLAS for sub-pixel/tiny draws; point clouds are
   excluded entirely.

Net effect: a 50 GB photogrammetry city ray-traces at the cost of the few
hundred MB of geometry actually on screen at the current LOD. The AS manager is
the single most performance-sensitive component in this roadmap — profile it
first (Tracy), and treat its memory cap + build throttle as shipping controls.

---

## 6. Phased build order

Each phase ships something visible on its own and leaves every tier's fallback
intact. Phases 1–3 are the no-RT product; 4–6 are the RT upgrade; 0 is shared
plumbing. Within a phase, ⟂ marks work packages that can proceed in parallel.

### Phase 0 — Spine & foundations 🧱
Small, unblocks everyone. Do first.
- **WP0.1 ⟂ RenderQuality + settings + auto-detect** (C3). New `RenderConfig`;
  wire into `Gui::Settings`/`SettingsIndex`/`Preferences` and the existing
  Settings panel + command registry. GPU-tier heuristic + `rayTracingSupported()`.
- **WP0.2 ⟂ HDR post chain refactor.** Insert-points between forward and tonemap
  (fog composite, bloom, TAA), ping-pong HDR targets; must preserve mono /
  quad-buffer / side-by-side / docked-viewport / XR paths.
- **WP0.3 Depth + thin G-buffer prepass** (C2) + `prevViewProj` in FrameData;
  forward pass → depth `EQUAL`. Depends on nothing but touches the scene pass.
- **WP0.4 Temporal reprojection + TAA resolve** (motion-vector history). Shared
  substrate for clouds, SSR, and all RT denoise. After WP0.3.
- **WP0.5 ⟂ Tracy CPU+GPU profiler** (also `docs/TODO.md` §G). Vendor + wire; add
  zones to the frame graph. Do this early — "highly optimized" needs numbers.

🧪 **Gate:** quality dropdown switches tiers live; G-buffer + motion vectors
visualized in a debug view; TAA stable on a static and a moving camera in stereo;
Tracy shows per-pass GPU time.

### Phase 1 — Environment lighting & AO 🎯 (no-RT, universal, top ROI)
- **WP1.1 ⟂ IBL** — irradiance cube + prefiltered-specular mip cube + BRDF LUT
  from the current environment; replace flat ambient in `mesh.frag` with IBL
  diffuse + specular. Runtime bake (Sascha Willems `pbribl` pattern); expose a
  re-bake hook for the Phase-2 dynamic sky. Ref §9.
- **WP1.2 GTAO** — XeGTAO compute port from the G-buffer; spatial+temporal
  filter; multiply the indirect term. Ref §9.
- **WP1.3 ⟂ Bloom** — dual-filter (down/up) HDR bloom in the WP0.2 chain. Ref §9.

🧪 **Gate:** metallic/rough materials read the environment; scene is grounded by
AO; bloom on highlights — all at target fps on a GTX-1060-class GPU in stereo.

### Phase 2 — Procedural sky, volumetric clouds, volumetric fog 🎯
The volumetric pillar (the headline "why we want volumetrics"). 2.1 → 2.2; 2.3 ⟂.
- **WP2.1 Procedural atmosphere** (Hillaire) — transmittance + multiscatter +
  sky-view + aerial-perspective LUTs (compute); new `SkyMode::Physical` samples
  sky-view; drives sun radiance/color and **dynamic IBL re-bake** (WP1.1). Ref §9.
- **WP2.2 Volumetric clouds** (Nubis / Frostbite) — Perlin-Worley shape + Worley
  detail 3D noise volumes (generate offline via a tool or at load), weather map,
  energy-conserving raymarch, cloud self-shadow + ground shadow, **¼-res +
  temporal reprojection** (WP0.4), composited with atmosphere + aerial
  perspective. The perf-sensitive one — checkerboard update + step budget. Ref §9.
- **WP2.3 ⟂ Froxel volumetric fog** (Wronski) — view-frustum 3D texture; inject
  media + light (sun with shadow-map/RT shadow, local lights, ambient/GI);
  temporal integration; scatter/transmittance march; HDR composite. God rays fall
  out of this. Ref §9.

🧪 **Gate:** sun moves → sky + ambient update; clouds drift, self-shadow, and
shadow the ground; fog + light shafts through geometry; clouds within step
budget at ¼-res in stereo.

### Phase 3 — Screen-space reflections 🔀 (no-RT reflections)
- **WP3.1 SSR** — stochastic Hi-Z screen-space reflections from the G-buffer,
  importance-sampled by roughness, temporally stabilized (WP0.4), **fade to IBL
  prefilter on ray miss / screen exit**. Becomes the near-field layer under RT
  reflections later. Ref §9.

🧪 **Gate:** glossy floor/water reflections, stable under motion, graceful
fade at screen edges; no-RT tier now visually complete.

### Phase 4 — RT foundation + shadows + AO 🧱⚡ (gated on `rayTracingSupported()`)
- **WP4.1 Acceleration-structure manager** (§5) — BLAS build/compaction/graveyard
  on async compute, TLAS refit, streaming + LOD integration, budget cap + build
  throttle, TLAS into set 2. Lives in `RHI/` (may include Vulkan headers). **The
  performance-critical WP.** Ref §9 (nvpro AS + vk_ray_query).
- **WP4.2 RT shadow mask** — ray_query sun (and optional point) shadow rays →
  denoised mask (SIGMA-style / temporal); forward samples the mask. 🔀 → shadow
  maps.
- **WP4.3 RT AO** — ray_query world-space hemisphere AO → denoised; 🔀 → GTAO.

🧪 **Gate:** RT shadows + AO on a ≥50-M-triangle photogrammetry scene *within
frame budget* on RTX-2060-class (Tracy-verified: AS memory under cap, no build
spikes); **the same scene runs correctly on an AMD RDNA2+ GPU** (C6 cross-vendor
check); flip the GPU/quality to no-RT and confirm the scene falls back cleanly.

### Phase 5 — RT reflections 🎯
- **WP5.1** ray_query reflection rays from the G-buffer; shade hits via the
  **bindless materials** + IBL/DDGI for the hit's own indirect; ReBLUR-style
  denoise; composite **over SSR** near-field; reduced-res + budget. 🔀 → SSR → IBL.

🧪 **Gate:** accurate off-screen reflections; denoised stable; SSR handles the
contact/near field; cost bounded by `rtResolveScale`.

### Phase 6 — RT diffuse GI (DDGI, cascaded) 🎯
- **WP6.1 DDGI** — irradiance + visibility probe atlases, ray_query probe updates
  on async compute, **cascaded/clipmap grid centered on the camera** for
  city-scale coverage; forward samples probe irradiance for indirect diffuse
  (spatially-varying bounce replacing IBL's constant ambient). World-space →
  shared across eyes; converges when static. 🔀 → IBL. Ref §9 (RTXGI-DDGI).

🧪 **Gate:** color bleeding + filled shadows that update as geometry/sun move; no
boiling under motion; stereo-consistent; graceful cascade transitions on a large
scene.

### Phase 7 — Progressive reference mode (optional, low priority)
- **WP7.1** accumulate ray_query samples while the camera/scene is static → a
  clean converged image; reset on movement. A "park and inspect" viz mode.

---

## 7. Cross-cutting workstreams (owned continuously)

- **Denoising & temporal library** ⚡ — shared by shadows/AO/reflections/GI/clouds.
  **Primary path: fully vendor + integrate NVIDIA NRD** (ReBLUR for AO/refl/GI,
  SIGMA for shadows; Vulkan integration via `nvpro-samples/vk_denoise_nrd`) — it
  is proven, and its core is plain cross-vendor SPIR-V compute (runs on AMD/Intel;
  verify on AMD as part of the WP). A homegrown SVGF-class denoiser on the WP0.4
  temporal base is the **fallback only** if NRD integration hits a wall. Decide at
  the start of Phase 4.
- **Quality/settings spine** — WP0.1 is never "done"; every phase registers its
  toggles, budgets, and `Auto` heuristics here.
- **Profiling (Tracy)** ⚡ — GPU zones on every pass; a HUD budget line per tier.
- **Stereo/multiview correctness** — every new pass is multiview or runs per-view;
  world-space signals shared across eyes; screen-space signals reduced-res per eye.
- **Library vendoring** — anything pulled in is self-contained and wired into the
  `.vcxproj`/`.filters` + post-build DLL copy (see banner).

---

## 8. Risks & perf watchpoints

- **AS memory + build hitching on paging** (photogrammetry). Mitigate: §5
  (compaction, cap, throttle, LOD-ride). Watch with Tracy from day one of Phase 4.
- **Stereo doubling of screen-space passes.** Keep SSR/GTAO/RT-refl reduced-res;
  push everything possible to world-space.
- **Temporal ghosting/boiling** — unacceptable for a viz tool. Motion vectors for
  *everything* that moves (incl. dynamic transforms), disocclusion rejection,
  variance-guided denoise.
- **Cloud cost** — the classic frame-time trap. ¼-res + checkerboard + step
  budget + temporal reproject; never full-res per eye.
- **Dynamic-IBL re-bake cost** when the sun moves — amortize (re-bake over N
  frames / only on meaningful sun delta).
- **Pipeline permutation explosion** — keep spec-constant combos to the tiers
  actually shipped; lean on the disk pipeline cache.

---

## 9. Reference implementations to adapt (copy proven code)

Port mechanically first (HLSL/WGSL/WebGPU → GLSL for Vulkan 1.3, `layout(scalar)`,
reverse-Z, multiview), then optimize. Check each license before vendoring.

| Subsystem | Canonical reference | Notes / port target |
|---|---|---|
| **IBL** | [SaschaWillems/Vulkan `pbribl`](https://github.com/SaschaWillems/Vulkan) · [Google Filament](https://github.com/google/filament) (+ `tools/cmgen`) | Vulkan-native runtime bake of BRDF LUT + irradiance + prefilter cubes. Filament for the SH/prefilter math + an optional offline path. |
| **GTAO** | [Intel XeGTAO](https://github.com/GameTechDev/XeGTAO) (MIT) | HLSL → GLSL compute; production, runs on iGPUs. |
| **Bloom** | [SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan) bloom + Jimenez "Next Gen Post Processing" (COD:AW) | Dual-filter down/up HDR bloom. |
| **Procedural sky** | Hillaire, "A Scalable and Production Ready Sky & Atmosphere" — [WebGPU port](https://github.com/JolifantoBambla/webgpu-sky-atmosphere) · [Shadertoy slSXRW](https://www.shadertoy.com/view/slSXRW) | LUT compute passes; readable WebGPU/GLSL to port. |
| **Volumetric clouds** | Schneider "Nubis" (Horizon Zero Dawn) + Hillaire "Physically Based Sky, Atmosphere & Cloud in Frostbite" · [Nubis-style Shadertoy MdGfzh](https://www.shadertoy.com/view/MdGfzh) · Hillaire `TileableVolumeNoise` | Perlin-Worley + Worley volumes, energy-conserving raymarch. |
| **Froxel volumetric fog** | Wronski, "Volumetric Fog" (SIGGRAPH 2014) · a readable engine impl (e.g. Godot's volumetric fog) | View-frustum 3D texture inject/scatter compute. |
| **SSR** | [Filament](https://github.com/google/filament) stochastic SSR / standard Hi-Z trace | Roughness importance sampling; fade to IBL. |
| **RT AS + ray_query** | [nvpro-samples/vk_raytracing_tutorial_KHR](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR) · [AnKi minimalist AS-only RT](https://anki3d.org/minimalist-ray-tracing-leveraging-only-acceleration-structures/) | BLAS/TLAS build + compaction + inline ray_query patterns. |
| **RT denoise** | [NVIDIA-RTX/NRD](https://github.com/NVIDIA-RTX/NRD) · [nvpro-samples/vk_denoise_nrd](https://github.com/nvpro-samples/vk_denoise_nrd) | **Fully vendor + integrate** ReBLUR (AO/refl/GI) + SIGMA (shadows). Cross-vendor SPIR-V compute; verify on AMD (C6). Homegrown SVGF only if it hits a wall. |
| **DDGI** | [NVIDIAGameWorks/RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI) (`DDGIVolume`, Vulkan) · [RTXGI v2](https://github.com/NVIDIA-RTX/RTXGI) | **Fully vendor + integrate** the `DDGIVolume` + probe-update shaders (standard `ray_query`); add cascades for scale. v2's Neural Radiance Cache = optional NVIDIA-only accelerator behind the cross-vendor DDGI default (C6). |
| **Meshlets/quantization (opt.)** | [meshoptimizer](https://github.com/zeux/meshoptimizer) | If mesh LOD/vertex-cache work helps AS build cost. |

*Vendor neutrality (C6):* several libraries are NVIDIA-authored. That is fine —
we **fully vendor and integrate them** because their cores are cross-vendor SPIR-V
compute / standard KHR `ray_query`; we never take a proprietary extension as a
hard dependency, and expose vendor-locked sub-features **only as optional,
runtime-detected accelerators behind the cross-vendor default** (so a stronger
card can do more). Everything here targets AMD, Intel, and NVIDIA equally, and is
optimized for all of them; acceptance verifies on AMD **and** NVIDIA.

*Future direction (not v1):* ReSTIR DI/GI (Ouyang et al., HPG 2021,
[paper](https://d1qx31qr3h6wln.cloudfront.net/publications/ReSTIR%20GI.pdf)) for
many-light + higher-quality RT lighting once the AS + denoise base is solid.

---

## 10. Status Board

Update on every meaningful change (per UI_REDESIGN.md convention).

| Phase | WP | State | Owner | Notes |
|---|---|---|---|---|
| 0 | 0.1 RenderQuality/settings | ☐ not started | | spine |
| 0 | 0.2 HDR post chain | ☐ | | |
| 0 | 0.3 G-buffer prepass | ☐ | | |
| 0 | 0.4 Temporal/TAA | ☐ | | |
| 0 | 0.5 Tracy | ☐ | | do early |
| 1 | 1.1 IBL | ☐ | | top ROI |
| 1 | 1.2 GTAO | ☐ | | |
| 1 | 1.3 Bloom | ☐ | | |
| 2 | 2.1 Atmosphere | ☐ | | |
| 2 | 2.2 Clouds | ☐ | | perf-sensitive |
| 2 | 2.3 Froxel fog | ☐ | | |
| 3 | 3.1 SSR | ☐ | | |
| 4 | 4.1 AS manager | ☐ | | ⚡ critical |
| 4 | 4.2 RT shadows | ☐ | | |
| 4 | 4.3 RT AO | ☐ | | |
| 5 | 5.1 RT reflections | ☐ | | |
| 6 | 6.1 DDGI | ☐ | | |
| 7 | 7.1 Reference mode | ☐ | | optional |

Legend: ☐ not started · ◐ in progress · ✅ landed + gate passed.
