# StereoVista — SLPK / I3S Support Roadmap

> **Scope:** full professional-grade support for Esri **Scene Layer Packages
> (`.slpk`)** / **OGC I3S** — loading, rendering, tools, editing, and export —
> as a flagship feature of the Vulkan app. This document is the researched
> plan: the format facts, what the best existing products do (and what we
> copy), what **nobody** does (and where we lead), the Vulkan-native
> architecture, and a phased build order with acceptance gates.
> Companion docs: `docs/TODO.md` (general work list), `docs/PLUGINS.md`, and
> **`docs/SLPK_IMPLEMENTATION_PLAN.md`** — the grounded, file-level agent
> handbook for building phases S0–S3 (integration map into the existing code,
> library vendoring, milestone checklists, acceptance gates).
> Spec: [Esri i3s-spec](https://github.com/Esri/i3s-spec) ·
> [OGC I3S 1.3 Community Standard](https://docs.ogc.org/cs/17-014r9/17-014r9.html).

Legend: 🎯 differentiator (nobody else has it) · 📋 parity (industry-standard,
must have) · ⚡ performance · 🧪 acceptance gate (manual/visual — no test suite).

---

## 0. Why this wins — the market gap

I3S/SLPK is one of the two open standards for massive 3D geo content (the
other is Cesium 3D Tiles) and the native output of ArcGIS — the dominant GIS.
Cities, surveyors, and BIM teams sit on terabytes of SLPKs. Yet the desktop
tooling is remarkably thin:

| Product | Platform | SLPK support | Weakness we exploit |
|---|---|---|---|
| **ArcGIS Pro** | Desktop | Full (create + view) | $$$ per-seat license, heavyweight, no in-place package editing |
| **ArcGIS Earth** | Desktop, free | View only | Limited tools, no editing/export, online-oriented |
| **Scene Viewer / JS SDK** | Browser | Full streaming view | Browser perf ceiling, needs a portal/service, no local files without setup |
| **CesiumJS** | Browser | View via JIT I3S→glTF transcode ([blog](https://cesium.com/blog/2023/02/06/cesiumjs-adds-support-for-indexed-3d-scene-layers-i3s/)) | Same browser ceiling; conversion overhead per node |
| **I3S Explorer** ([i3s.loaders.gl](https://i3s.loaders.gl/)) | Browser, OSS | View + excellent debug UI | Viewer only, needs served data |
| **Skyline TerraExplorer** | Desktop | View ([site](https://www.skylinesoft.com/terraexplorer-for-desktop/)) | $$$, aging UX |
| **QGIS / open-source GIS** | Desktop | Effectively none | — |

Three structural gaps, all confirmed by research:

1. **No fast, offline-first, affordable desktop SLPK viewer exists.** Everything
   is either a browser client (needs a server / portal) or a multi-thousand-dollar
   GIS suite. A native Vulkan app that opens a 50 GB `.slpk` from a
   double-click is an empty niche.
2. **Nobody can edit an SLPK.** In the entire Esri ecosystem the package is
   read-only — updates mean *recreating* the package from source data
   ([Esri docs](https://pro.arcgis.com/en/pro-app/latest/help/mapping/layer-properties/edit-a-scene-layer-with-associated-feature-layer.htm)).
   In-place editing (transforms, attributes, materials, pruning, re-save) is
   an unowned capability.
3. **Nobody renders I3S in native stereo or desktop VR.** Our quad-buffer /
   side-by-side / OpenXR multiview pipeline is unique in this space —
   "walk through your city model in VR / on a 3D display, straight from the SLPK".

Plus one convergence that is pure luck: our **Schütz compute point-cloud
rasterizer** is the fastest published way to draw pixel-sized points, and I3S
**point cloud scene layers** hand us exactly the paged octree it needs for
out-of-core LOD (`docs/TODO.md` §F falls out of this work for free).

---

## 1. Format primer — what we must implement

Facts that shape the architecture (spec: [i3s-spec](https://github.com/Esri/i3s-spec),
[full format doc](https://github.com/Esri/i3s-spec/blob/master/format/Indexed%203d%20Scene%20Layer%20Format%20Specification.md)):

- **SLPK = ZIP archive, STORE-only** (no zip-level compression; ZIP64 for >2 GB),
  with individual resources **gzipped inside** (`.json.gz`, `.bin.gz`; jpg/png
  stored raw). v1.7+ adds an **MD5 hash index** (`@specialIndexFileHASH128@`)
  after the central directory → **O(1) resource lookup without scanning**.
  This is what makes "instant open" possible: memory-map the file, read the
  hash index, touch nothing else.
- **Node pages** (v1.7+): the bounding-volume hierarchy is a **flat array of
  fixed-size JSON pages** (`nodePages/<page>.json.gz`). Each node carries an
  **OBB** (center + half-extents + quaternion), a **LOD threshold**
  (`maxScreenThresholdSQ` — projected-OBB-area metric,
  [lodSelection](https://github.com/Esri/i3s-spec/blob/master/docs/1.8/lodSelection.cmn.md)),
  child indices, and mesh/texture/attribute resource ids. Traversal never
  touches geometry. v1.6 instead has per-node `3dNodeIndexDocument.json` +
  string paths (`0/0-1/0-1-3`) — support both (1.6 packages are everywhere).
- **Geometry:** v1.7+ = **Draco-compressed** buffers (position, normal, uv0,
  color, uv-region, feature-id); v1.6 = raw interleaved binary. Triangles,
  **CCW winding** — matches our `VK_FRONT_FACE_COUNTER_CLOCKWISE` convention
  exactly, no flip needed.
- **Positions are per-node-relative:** vertex x/y/z are **deltas from the
  node's bounding-sphere center**, which lives in a geodetic CRS (global mode:
  WGS84 lon/lat/ellipsoidal-height; local mode: a projected CRS). Small floats
  on the GPU by construction — the precision problem is ours only on the CPU
  side (§4.3).
- **Textures:** JPEG/PNG, legacy DDS, and (v1.2 OGC / 1.8 Esri) **KTX2 Basis
  Universal** ([Khronos blog](https://www.khronos.org/blog/ktx2.0-support-in-i3s-v1.2-puts-the-whole-world-in-your-hands)) —
  ETC1S and UASTC. UASTC→**BC7** transcode is near-free (UASTC is a BC7
  subset). Textures are **atlases**; sub-mesh rects ride the `uv-region`
  vertex attribute.
- **Materials:** metallic-roughness PBR (glTF-style) — maps 1:1 onto our
  `gpu::MaterialData` (baseColor + metallic/roughness factors + albedo /
  normal / metallic-roughness / AO / emissive textures).
- **Attributes:** per-node, **per-field binary columns** in geometry order
  (no per-feature lookup needed), described by `attributeStorageInfo`, with
  **layer-wide statistics** (min/max/histogram/most-frequent) in
  `statistics/` — statistics drive symbology UIs basically for free.
- **Point cloud profile** ([PCSL 2.0](https://github.com/Esri/i3s-spec/blob/master/docs/2.0/pcsl_ReadMe.md)):
  leaf-only nodes, geometry = **LEPCC**-compressed XYZ
  ([Esri/lepcc](https://github.com/Esri/lepcc), ~10× at 1 cm), attributes =
  lepcc-rgb / lepcc-intensity / raw columns (class code, returns, GPS time,
  scan angle…). OBB-only bounds.

**Layer types & priority:** **3D Object** and **Integrated Mesh** (same mesh
profile — one implementation) → **Point Cloud** → **Building Scene Layer**
(BIM; a 3D-object layer + sublayer/discipline/category JSON on top) →
**Point** (features + symbols) → **Voxel** (niche; defer indefinitely).

**Version matrix to support:** read **1.6 → 1.8** (= OGC 1.0–1.3) for mesh
profiles, **PCSL 2.0** for point clouds. Write (Phase S5): 1.8-conformant.
Esri validation tooling: [i3s_converter](https://github.com/Esri/i3s-spec/blob/master/i3s_converter/i3s_converter_ReadMe.md)
round-trips are our conformance check.

---

## 2. What the best products do — and what we copy

Researched feature sets worth cloning outright (📋 = goes on our roadmap):

**ArcGIS Scene Viewer / Pro** ([building scene layers](https://pro.arcgis.com/en/pro-app/latest/help/mapping/layer-properties/building-scene-layer-in-arcgis-pro.htm),
[explore BSL](https://doc.arcgis.com/en/arcgis-online/get-started/explore-building-scene-layers.htm)):
- 📋 **Slice tool** to reveal occluded interiors — we already have
  `FrameSubmission::clipPlanes` + `ClipPlaneTool`; extend to I3S draws (§S4).
- 📋 **Building Explorer**: filter by **discipline** (architectural /
  structural / mechanical / electrical), **category** (walls, windows, HVAC…),
  and **level**; non-matching elements hidden or ghosted as wireframe. THE
  feature BIM users pay for (§S4/S6).
- 📋 **Attribute pop-ups** on click (feature-id → attribute columns).
- 📋 **Attribute-driven filtering & coloring** (statistics-driven ramps,
  class-code palettes, value ranges).
- 📋 **Daylight/shadow analysis**: date/time slider driving the sun — our
  `SunState` + PCSS shadows make this a UI-only feature; add a solar-position
  model (lat/long/date→direction; the SLPK tells us where on Earth it is!).
- 📋 **Measure / line-of-sight / elevation profile** — measurement exists
  (`MeasurementTool`); LOS and profile are cheap overlay tools on top of
  depth picking.
- 📋 **Swipe/compare** two layers (great for before/after surveys).

**CesiumJS** ([I3S support](https://cesium.com/blog/2023/02/06/cesiumjs-adds-support-for-indexed-3d-scene-layers-i3s/),
[PR #9634](https://github.com/CesiumGS/cesium/pull/9634)):
- 📋 The **decode pipeline shape**: worker-pool Draco decode, placeholder
  children registered immediately so the LOD system sees the tree before
  content arrives, direct KTX2 consumption. We do the same with real threads
  instead of web workers — and skip their I3S→glTF transcode entirely
  (decode straight into our GPU layouts; that's our structural speed edge
  over every wrapper implementation).

**I3S Explorer** ([i3s.loaders.gl](https://i3s.loaders.gl/), OSS):
- 📋 **Inspector/debug mode**: OBB visualization, per-node info on hover,
  wireframe, UV/texture debug views, **memory-usage HUD**, LOD-level
  coloring. Cheap for us (overlay renderer + ImGui) and beloved by exactly
  the professional users we target. Copy the whole concept (§S4).

**ArcGIS Earth:** 📋 the **drag-and-drop → flying at your data in seconds**
UX bar. Open-dialog *and* drop target; auto-frame the layer; progressive
refinement visible immediately; never a modal "Loading…" wall.

**loaders.gl tile-converter** ([docs](https://loaders.gl/docs/modules/tile-converter/cli-reference/tile-converter)):
- 📋 Two-way **I3S ↔ 3D Tiles** batch conversion exists as a Node CLI. We
  don't need to beat it at batch conversion on day one — but native
  import/export (§S5) makes us the interchange *app*, and 3D Tiles read
  support later (§S6) reuses ~80% of the I3S machinery (same BVH+SSE model).

---

## 3. What nobody does — our differentiators 🎯

Ordered by (impact ÷ effort):

1. 🎯 **Instant open.** Double-click a 50 GB SLPK → first pixels **< 1 s**,
   interactive full-quality refinement after. Achievable: mmap + hash-index
   (no archive scan) + render-as-you-decode. Every competitor either uploads
   to a service first or scans/converts. This single demo sells the app.
2. 🎯 **Native stereo + VR I3S.** Quad-buffer, SBS, and OpenXR out of the
   same multiview renderer — unique, and it's *our existing* pipeline; I3S
   content just rides along.
3. 🎯 **Hybrid scenes with unified tools.** SLPK mesh + raw LAS/LAZ cloud +
   OBJ/GLTF models in ONE scene, one lighting/shadow model, measure/clip
   *across* them. GIS packages silo these; we already have all three
   renderers.
4. 🎯 **SLPK editing.** In-place package operations no one else offers:
   re-anchor/transform layer, edit feature attributes, swap/retint materials,
   delete features/subtrees, then **re-save a valid SLPK** (STORE-zip +
   hash-index rewrite is mechanical). Even "rotate this survey 2° and save"
   is impossible in ArcGIS without re-cooking from source.
5. 🎯 **Fastest I3S point clouds anywhere.** LEPCC → our compute rasterizer
   (uint64 atomicMin + HQS). Browser viewers draw PCSL as GL point sprites;
   we'll be an order of magnitude past that, with out-of-core LOD from the
   I3S octree itself.
6. 🎯 **Offline-first, no ecosystem tax.** No portal, no service, no license
   server. (Serving I3S over HTTP *from* the app is a later bonus — §S6.)
7. 🎯 **Export freedom.** Selection/layer → LAS/PLY (exporters exist), glTF,
   OBJ; point clouds → new SLPK (writer, §S5). The format jail-break tool.

---

## 4. Architecture — the fastest load & render path

### 4.1 Module layout (follows the house layering — nothing above the RHI sees Vulkan)

```
src/Loaders/Slpk/
  SlpkArchive        mmap'd ZIP64 reader: central dir / @specialIndexFileHASH128@
                     hash-index; returns raw spans; zero-copy for STORE'd entries;
                     gzip-inflate on caller's thread (libdeflate)
  I3SLayer           3dSceneLayer.json model: layer type, CRS, attributeStorageInfo,
                     statistics, texture/material/geometry definitions, version quirks (1.6–1.8)
  I3SNodeTree        node pages → flat node array {OBB, lodThresholdSQ, children,
                     resource ids, state}; v1.6 index-documents normalized into the
                     same flat form at load
  I3SDecoders        draco (mesh), lepcc (points), Basis/KTX2→BC7 (textures),
                     jpg/png (stb, exists), attribute-column readers
  I3SStreamer        thread-pool pipeline + LRU cache + budgets (§4.2)
  SlpkWriter         (S5) STORE-zip + gzip + hash-index emitter; draco/lepcc encode
src/Scene/I3SSceneLayer.*   scene-side object: layer transform/anchor, per-node
                     GPU residency, DrawItem/PointCloudDrawItem emission, picking hooks
```

New vendored deps (all Apache-2/BSD/MIT — no license friction):
**draco** (decode+encode), **lepcc**, **basis_universal transcoder**
(transcoder only — small), **libdeflate** (fastest gzip inflate; zlib works
but is ~3× slower), and later **Esri i3s-lib** ([repo](https://github.com/Esri/i3s-lib)) —
note it is **write-only today** ("reading/validating planned"), so it
shortcuts our *writer*, not our reader. Reader is ours (that's the moat).
Wire everything into the `.vcxproj` + `.filters` per house rules.

### 4.2 Load pipeline ⚡ (the "industry-leading fastest" answer)

The structural reasons we can beat everyone: (a) no server round-trips —
mmap'd local file, the OS page cache is our tile server; (b) no intermediate
format — decoders write **directly into our GPU vertex/point layouts**
(CesiumJS pays an I3S→glTF transcode per node; ArcGIS clients pay
service/portal overhead); (c) real threads + one upload ring instead of
worker-message serialization.

```
main thread        traversal → wants(node) ──┐             ┌→ residency flip (atomic)
                                             ▼             │
IO stage          mmap read (~0 cost, page cache) → span   │
inflate stage     libdeflate gzip → CPU buffer             │   N worker threads,
decode stage      draco / lepcc / basisu→BC7 / stb        ─┤   work-stealing pool,
upload stage      persistent staging → UploadRing → VkBuffer/VkImage (frame's cmd buf)
```

- **Open** (`SlpkArchive::open`): mmap; parse EOCD64 + hash index; parse
  `3dSceneLayer.json` + node page 0; queue root nodes. Target: **< 100 ms**
  to a framed, rendering (coarse) scene on NVMe.
- **Priority queue**: requests ordered by screen-space error contribution
  (biggest visual win first), **re-prioritized every frame**, **cancellable**
  (camera moved away → drop before decode). Prefetch one LOD ring beyond the
  cut using camera velocity.
- **Budgets**: per-frame upload budget (e.g. 64–128 MB/frame through the
  ring, amortized), decoded-node CPU cache and GPU residency caps with **LRU
  eviction** (VMA budget queries = the ceiling). Never stall the render
  thread on IO/decode — a node is either resident (drawn) or its finest
  resident ancestor is drawn instead.
- **Draco note:** decode is the hot cost (~ms per node). Thread pool scales
  it; positions dequantize straight into node-local float — no double pass.
- **Texture note:** ETC1S/UASTC KTX2 → **BC7** via the basisu transcoder
  (UASTC→BC7 is a fast repack; see the
  [Vulkan basisu sample](https://docs.vulkan.org/samples/latest/samples/performance/texture_compression_basisu/README.html)),
  upload as `VK_FORMAT_BC7_*` — 6–8× less VRAM and bandwidth than RGBA8.
  JPEG/PNG albedo: decode → RGBA8 first (simple), optional runtime BC7
  compression later (⚡ §6).

### 4.3 Precision (global datasets on float GPUs)

The classic geo problem ([good writeup](https://reearth.engineering/posts/high-precision-rendering-en/)):
ECEF/world coordinates overflow float32 → jitter. I3S already stores vertices
**node-relative** (small floats). Our scheme, mirroring what
`PointCloudLoader::loadFromLAS` already does with `globalCenter`:

- Pick a **scene anchor** per layer (root OBB center), keep it in
  `glm::dvec3`. Convert node centers geodetic→ECEF→**local ENU at the
  anchor** in double on the decode thread, **once per node**.
- GPU sees: node-relative float vertices + a per-node float `model` matrix
  in anchor space. Camera works in anchor space. Multiple layers/clouds
  co-register by anchor deltas in double. No shader changes, no doubles on
  GPU, no RTC uniforms needed at typical layer extents (city-scale ≪ float
  limits when anchored).
- Geodetic→ECEF/ENU math is ~50 lines (WGS84 ellipsoid); vendoring a full
  PROJ is **not** needed for global-mode layers (WGS84 only). Local-mode
  layers with projected CRS: treat as metric local space directly (anchor =
  offset), warn on exotic CRS until/unless we add PROJ later.

### 4.4 Render path ⚡

- **Traversal** (per frame, CPU, ~µs on flat arrays): frustum-cull OBBs →
  compare projected OBB area vs `maxScreenThresholdSQ` → select the resident
  cut; **hysteresis** band (switch-in/out thresholds differ ~15%) so nodes
  don't flicker at boundaries. Emit `DrawItem`s (mesh nodes) /
  `PointCloudDrawItem`s (PCSL) into the existing `FrameSubmission` — the
  forward-PBR, shadow, and point-cloud passes need **zero structural change**
  to start (they already consume arbitrary draw lists with per-draw bounds).
- **Materials**: I3S PBR → `gpu::MaterialData` 1:1; atlas textures →
  `MaterialSystem::addTexture` bindless indices (4096-slot array — fine, but
  add **texture eviction/slot-reuse** so long sessions with churning nodes
  don't leak slots; today nothing is ever removed).
- **uv-regions**: add an optional vertex attribute + a `MaterialData` flag
  bit; the mesh VS/FS remaps UVs into the atlas rect (one madd) —
  batch-friendly, keeps atlases intact.
- **Multiview stereo & shadows**: nothing special — I3S draws are just
  draws. Sun shadow map covers the visible cut (the texel-snapped ortho fit
  already consumes draw bounds). Point-light cubemaps unaffected.
- **PCSL**: LEPCC-decoded nodes stream into per-node segments of the
  existing quantized point buffers; traversal flips node visibility by
  updating the batch list — the compute rasterizer already handles
  multi-batch clouds. Out-of-core LOD = the same traversal as meshes
  (this **is** `TODO.md` §F, delivered via I3S first).
- Scale-up items (when node counts × 2 eyes bite, profile first — Tracy):
  GPU frustum/occlusion culling + `vkCmdDrawIndexedIndirectCount` over
  per-node draws, BDA-indexed material/instance tables (no per-node
  descriptors — house style already), transfer-queue uploads overlapping
  graphics, optional meshoptimizer re-index after draco decode (draco output
  vertex-cache order is mediocre).

---

## 5. Phased build order

Effort scale: **S** ≈ days · **M** ≈ 1–2 weeks · **L** ≈ 3–5 weeks ·
**XL** ≈ 6+ weeks (single dev, focused).

### Phase S0 — Foundations: read the package (M)
- `SlpkArchive` (mmap ZIP64 + STORE fast-path + hash index + libdeflate),
  `I3SLayer` JSON model, `I3SNodeTree` (node pages **and** 1.6 documents →
  one flat form), WGS84→ECEF→ENU anchor math.
- Vendor + wire: libdeflate, draco, lepcc, basisu transcoder.
- Debug-panel "SLPK Inspector" tab: open a package, dump layer JSON summary,
  node counts per level, walk nodes, show OBBs via the overlay renderer
  (no geometry yet). *This inspector then grows into the §S4 user feature.*
- 🧪 Gate: open Esri sample SLPKs (1.6 **and** 1.7/1.8, from
  [i3s-spec sample data](https://github.com/Esri/i3s-spec)) + one ArcGIS-Pro-cooked
  package; OBB cloud renders and matches the dataset's real-world shape;
  open time for a multi-GB package < 200 ms.

### Phase S1 — Render mesh layers, correct first (L)
- Draco decode → `MeshData`-shaped GPU upload (position/normal/uv/color +
  feature-ids); JPEG/PNG textures; materials → bindless; uv-region support;
  per-node model matrices in anchor space; draws into `FrameSubmission`.
- Fixed-depth LOD first (load tree to a chosen level under a memory cap —
  no streaming yet), then the SSE traversal from §4.4 with load-on-demand
  (blocking queue OK in this phase).
- Scene integration: I3S layer as a scene object (transform, show/hide,
  frame-on-load); picking via existing depth-pick → node + feature-id.
- 🧪 Gate: Esri sample 3D-object + integrated-mesh SLPKs render correctly
  (compare vs Scene Viewer screenshots): textures, normals/lighting, no
  seams/cracks between LODs at rest; stereo + XR sanity pass; correct
  picking. Sun shadows on I3S geometry.

### Phase S2 — Streaming at scale ⚡ (L)
- The full §4.2 pipeline: thread pool, priority + cancellation, prefetch,
  LRU eviction (CPU + GPU + texture slots), per-frame upload budgets,
  hysteresis; KTX2/Basis→BC7; Tracy vendored + zones for every stage.
- 🧪 Gate (the demo): cold-open a ≥ 20 GB city SLPK → first pixels < 1 s,
  smooth fly-through at 60+ FPS mono / 90 FPS stereo on the target GPU, no
  frame > 20 ms from loader activity, VRAM stays under budget while roaming.

### Phase S3 — Point cloud scene layers (M)
- LEPCC xyz/rgb/intensity decode; PCSL node tree (leaf-only) through the
  same streamer; per-node segments in `PointCloudGpu`; attribute columns
  (class, intensity, returns…) as per-point bytes.
- Rendering modes: RGB / intensity ramp / classification palette /
  elevation ramp — statistics-driven defaults (the SLPK ships min/max +
  histograms; use them for ramp bounds out of the box).
- 🧪 Gate: billion-point PCSL roams out-of-core at full frame rate with HQS;
  classification palette matches ArcGIS defaults; memory stays bounded.

### Phase S4 — Tools & UX: match then pass Scene Viewer (L)
- 📋 Attribute pop-ups (click → feature attributes panel); attribute
  **filters** (hide / ghost non-matching) and **color-by-attribute** for
  mesh layers; per-feature tint rides `DrawItem::tint` / a feature mask.
- 📋 Slice/section on I3S (clip planes already reach mesh + points);
  measurement/LOS/elevation-profile on I3S surfaces (depth-pick already
  works — mostly wiring + a profile overlay).
- 📋 **Daylight**: solar position from the layer's geolocation + date/time
  UI → `SunState`; PCSS gives correct soft shadow spread for free.
- 📋 Swipe/compare between two layers (scissored double-submit — cheap with
  our pass structure).
- 🎯 **Inspector mode** (grown from S0): OBB/LOD-color/wireframe overlays,
  per-node stats on hover, streaming + memory HUD (copy the I3S Explorer
  concept, natively).
- 📋 Layer panel UX: drag-drop open, per-layer visibility/opacity, frame,
  legend from statistics. (Rides the §D GUI work in `TODO.md` — do them
  together.)
- 🧪 Gate: side-by-side vs Scene Viewer on the same package — every tool
  it has works here, plus inspector + hybrid-scene measurement it can't do.

### Phase S5 — Editing & export 🎯 (L–XL)
- `SlpkWriter`: STORE-zip + gzip + hash-index emitter (evaluate
  [Esri i3s-lib](https://github.com/Esri/i3s-lib) as the encode backend —
  Apache-2, ships draco/basis/lepcc encoders — vs. writing our own around
  the same vendored codecs; own writer likely wins since we already carry
  the codecs and i3s-lib's profile coverage is elevation-focused today).
- Editing operations, undo-integrated (`core::UndoManager`), staged as a
  **non-destructive edit layer** replayed on save:
  layer re-anchor/transform bake → feature attribute edits → material/
  texture replace + retint → feature/subtree delete (tree + geometry
  rewrite) → merge multiple SLPKs' layers into one package.
- Export: visible/selected I3S → LAS/PLY (exporters exist) / glTF / OBJ;
  loaded point cloud (any source) → **new PCSL SLPK** (our LAS→SLPK cooker =
  ArcGIS-Pro-free package creation — 🎯 huge for survey shops).
- 🧪 Gate: edited/created packages open clean in ArcGIS Earth/Pro and pass
  Esri's [validation tooling](https://github.com/Esri/i3s-spec/blob/master/i3s_converter/i3s_converter_ReadMe.md);
  round-trip (open → edit → save → reopen) is lossless outside the edit.

### Phase S6 — Leadership & ecosystem (XL, à la carte)
- 📋 **Building Scene Layer**: sublayers/disciplines/categories model +
  Building-Explorer UI (filters/levels/ghosting) — enterprise-BIM headline.
- 🎯 Mesh **3D-object SLPK creation** from imported models (Assimp scene →
  cooked LODs via meshoptimizer simplification → draco → SLPK).
- I3S **web service** streaming (read `SceneServer` REST endpoints — same
  node/page model over HTTP with a disk cache) and/or **serve** loaded
  layers over localhost for browser clients.
- **3D Tiles read** support — reuses the whole streamer/traversal (SSE
  model is near-identical); makes us the universal geo-mesh desktop viewer.
- ⚡ GPU-driven culling + indirect draws, transfer-queue/async-compute
  overlap, sparse residency for mega-textures, VRS in stereo/foveated XR.
- Point (feature symbol) layers; Voxel layers only on real demand.

**Dependency spine:** S0 → S1 → S2 → {S3, S4} parallel → S5 → S6.
S3 can start after S2's streamer exists; S4's UI items can trail S1/S2
incrementally (inspector first — it accelerates our own debugging).

---

## 6. Performance targets (measure with Tracy from S2 on)

| Metric | Target | Baseline to beat |
|---|---|---|
| Cold open → first pixels (NVMe, 20 GB SLPK) | **< 1 s** | ArcGIS Pro: ~10–60 s to usable |
| Package open/index only | < 100 ms | — |
| Steady fly-through, city IM layer | 60 FPS mono / 90 FPS stereo | browser viewers: 30–60 mono |
| Frame-time spike from streaming | never > 4 ms on render thread | — |
| Draco decode throughput | saturate cores; ≥ 500 nodes/s/8-core | — |
| VRAM | hard budget, LRU, zero growth while roaming | Pro OOMs / stutters |
| PCSL | ≥ 1 B points out-of-core, HQS on, full rate | nothing comparable exists |

---

## 7. UX principles (the "professional feel" checklist)

1. **Never block.** No modal loads; everything streams; UI thread sacred.
2. **Progressive always**: coarse→fine visibly; framed camera immediately.
3. **Zero-config open**: double-click / drag-drop / associate `.slpk`;
   sensible defaults from package statistics (ramps, palettes, sun from
   geolocation + "now").
4. **Direct manipulation**: click = attributes; hover (inspector) = node;
   all tools work on all layer kinds identically (hybrid scenes).
5. **Honest feedback**: streaming HUD (nodes queued/decoded/resident, MB/s),
   toasts for degraded paths (unknown CRS, unsupported sub-features), never
   silent failure.
6. **Escape hatches**: every layer exportable; every edit undoable; original
   package never touched until explicit save/save-as.

---

## 8. Risks & open questions

- **Version fragmentation** (1.6 legacy paths, `textureSetDefinition`
  variants, DDS legacy): mitigate with the S0 normalization layer + a
  package zoo gathered early (Esri samples, ArcGIS Pro exports at each
  version, tile-converter output).
- **ETC1S quality on normal maps** is known-poor — prefer UASTC path;
  fall back gracefully (flat normals + toast) rather than artifacting.
- **Exotic projected CRS in local-mode packages** without vendoring PROJ:
  start WGS84 + common UTM/WebMercator, add PROJ only if user demand shows.
- **Attribute cardinality** (millions of features × many columns): keep
  columns memory-mapped/lazy; only materialize fields the UI touches.
- **i3s-lib writer maturity**: it may not cover mesh profiles we need —
  budget for our own writer (codecs are vendored anyway).
- **Esri sample availability/licensing for demos**: collect
  redistributable demo packages early (OGC/Esri samples, own LAS→SLPK
  cooker output once S5 lands).

---

## 9. Sources

- Spec & format: [Esri/i3s-spec](https://github.com/Esri/i3s-spec) ·
  [format specification](https://github.com/Esri/i3s-spec/blob/master/format/Indexed%203d%20Scene%20Layer%20Format%20Specification.md) ·
  [OGC I3S 1.3](https://docs.ogc.org/cs/17-014r9/17-014r9.html) ·
  [lodSelection](https://github.com/Esri/i3s-spec/blob/master/docs/1.8/lodSelection.cmn.md) ·
  [nodePageDefinition](https://github.com/Esri/i3s-spec/blob/master/docs/1.7/nodePageDefinition.cmn.md) ·
  [PCSL 2.0](https://github.com/Esri/i3s-spec/blob/master/docs/2.0/pcsl_ReadMe.md)
- Libraries: [Esri/i3s-lib](https://github.com/Esri/i3s-lib) ·
  [Esri/lepcc](https://github.com/Esri/lepcc) ·
  [google/draco](https://github.com/google/draco) ·
  [basis_universal](https://github.com/BinomialLLC/basis_universal) ·
  [KTX2 in I3S (Khronos)](https://www.khronos.org/blog/ktx2.0-support-in-i3s-v1.2-puts-the-whole-world-in-your-hands) ·
  [Vulkan Basis sample](https://docs.vulkan.org/samples/latest/samples/performance/texture_compression_basisu/README.html) ·
  [melowntech/libslpk](https://github.com/melowntech/libslpk)
- Implementations studied: [CesiumJS I3S](https://cesium.com/blog/2023/02/06/cesiumjs-adds-support-for-indexed-3d-scene-layers-i3s/) ·
  [Cesium I3S PR](https://github.com/CesiumGS/cesium/pull/9634) ·
  [loaders.gl I3S](https://loaders.gl/docs/modules/i3s) ·
  [I3S Explorer](https://i3s.loaders.gl/) ·
  [tile-converter](https://loaders.gl/docs/modules/tile-converter/cli-reference/tile-converter)
- Product features: [Building scene layer (Pro)](https://pro.arcgis.com/en/pro-app/latest/help/mapping/layer-properties/building-scene-layer-in-arcgis-pro.htm) ·
  [Explore BSL](https://doc.arcgis.com/en/arcgis-online/get-started/explore-building-scene-layers.htm) ·
  [BSL filters](https://pro.arcgis.com/en/pro-app/latest/help/mapping/layer-properties/filter-a-building-scene-layer.htm) ·
  [Scene layer editing limits](https://pro.arcgis.com/en/pro-app/latest/help/mapping/layer-properties/edit-a-scene-layer-with-associated-feature-layer.htm) ·
  [TerraExplorer](https://www.skylinesoft.com/terraexplorer-for-desktop/)
- Precision: [high-precision rendering in global scenes](https://reearth.engineering/posts/high-precision-rendering-en/)
