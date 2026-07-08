# SLPK / I3S — Implementation Plan (agent handbook)

> **Audience:** the coding agent implementing SLPK support. This is the
> *grounded* companion to `docs/SLPK_ROADMAP.md` (strategy/market/why): every
> integration point below names the real API in this repo, and every milestone
> ends in a commit and a manual acceptance gate. Read, in order, before writing
> code: **`CLAUDE.md`** → **`docs/SLPK_ROADMAP.md`** (§1 format primer, §4
> architecture) → this file. Skim `docs/PLUGINS.md` only when you touch tools.
>
> Track progress by checking the `- [ ]` boxes in this file (commit the edit
> with the milestone). A later session resumes at the first unchecked box.

---

## 0. Working rules for the agent

- **Branch/commits:** work on your designated branch. Commit at every
  milestone boundary (M0, M1, …) at minimum; more often is fine. Clear
  messages, imperative mood.
- **Environment reality:** you are likely developing on Linux; the project
  builds ONLY with MSVC v143 on Windows (`StereoVista.sln`). You cannot run
  the build — so the bar is **MSVC-clean, warning-conscious code**:
  no GCC-isms (`__attribute__`, VLAs, `#pragma GCC`), no POSIX calls in app
  code (file mapping goes through a `Platform::` wrapper, §4.1), use
  `#pragma warning(push/disable)` around vendored includes as done for
  existing libs, C++17 only. Double-check every added file compiles
  standalone in your head (includes complete, no order dependence).
- **Wire everything:** every added/removed source file goes into
  `StereoVista/StereoVista.vcxproj` **and** `StereoVista.vcxproj.filters`
  (MSBuild compiles only what is listed). Vendored library sources too.
- **Libraries: download and FULLY integrate** (§3). Vendor real pinned
  sources into the repo — no half-integrations, no "user installs X",
  no new DLLs, no CMake. Include upstream LICENSE files. Verify current
  release tags online before pinning; record what you pinned in §3's table.
- **House invariants** (violations = rework): nothing above `src/RHI`
  includes a Vulkan header; projections only from `renderer::Projection.h`
  factories (reverse-Z, [0,1] depth, Y-flip in projection); CCW front faces
  (I3S is CCW too — do not flip winding); GPU-shared structs live in one
  scalar-layout header included from both C++ and GLSL; bindless materials +
  push constants/BDA, no per-draw descriptor churn; namespaces per layer
  (`i3s::` is yours for the new code, loaders sit with `Engine::`/`Loaders`).
- **No test suite exists.** Verification is manual/visual by the owner on
  Windows. Each milestone's gate below lists exactly what to tell the owner
  to run/look at — put that in the milestone commit message body.
- **Improve, don't copy** (house rule): where this plan and reality diverge,
  read the actual source first, then do the better thing and update this doc
  in the same commit.

---

## 1. What you build on — the integration map (real APIs)

| Existing piece | Where | Contract you rely on | Constraint to respect |
|---|---|---|---|
| Scene host | `headers/Scene/Scene.h` | `scene::Model{position,rotationDeg,scale,visible,meshes[]}`, `ModelMesh{MeshBuffer,materialIndex,bounds}`; `Scene::computeWorldBounds()` | Interim host: an I3S layer becomes a NEW top-level object (§4.2), not a `Model` with 100k meshes |
| Assimp import pattern | `src/Scene/ModelImporter.cpp` | The reference flow: stb decode → `rhi::Texture::create/upload` → `materials->addTexture(std::move(tex))` → `gpu::MaterialData` → `addMaterial` | Copy the *pattern* for M1 textures, not the code |
| Bindless materials | `headers/Renderer/MaterialSystem.h` | `addTexture`→index, `addMaterial`→index, 4096-slot UPDATE_AFTER_BIND array, table re-uploaded per frame (edits are live) | **No eviction exists** — M2 must add slot reuse (§6.5) before churning textures |
| Material GPU struct | `assets/shaders_vk/scene_types.h` `MaterialData` (64 B) | baseColor, metallic/roughness factors, emissive, normalScale + albedo/normal/metallic/roughness/ao texture indices | I3S PBR maps 1:1; uv-region needs a flags bit + new field pair (§5 M1) |
| Mesh GPU upload | `headers/Renderer/MeshBuffer.h` | `create(device, MeshData)` = staged blocking upload; `Vertex{pos,normal,uv,tangent}` 48 B, static-asserted | Blocking = fine in M1 (loader thread), M2 fills device-local buffers via ring instead (§6.3) |
| Per-frame scene input | `headers/Renderer/FrameSubmission.h` | `DrawItem{mesh,model,normalMatrix,materialIndex,castsShadows,tint,worldBoundsCenter/Radius}`; `clipPlanes`; `pointClouds` | Emit one `DrawItem` per resident node mesh. `worldBounds*` feeds the sun-shadow fit — fill it from the node OBB |
| Upload ring | `headers/RHI/UploadRing.h` | Persistently-mapped ring; `stage(dstBuffer, off, data, size)` + mark/rollback; flushed once/frame by Renderer; **main thread only**; full ring → retry next frame (backpressure) | **Buffer copies only** — M2 adds `stageImage` (§6.4). 64 MB (`Renderer::kUploadRingBytes`) — raise when profiling says so |
| Worker→pump pattern | `headers/Loaders/PointCloudLoader.h` | `beginLoad*Progressive` (background threads) + `updateStreaming` pumped per frame on the main thread + `StreamProgress` for UI | The I3S streamer follows this exact shape (§6.1) — it is the house answer to "threads never touch Vulkan" |
| Double-precision anchor | `PointCloudLoader::loadFromLAS(..., const glm::dvec3* globalCenter)` | Shared global center across LAS tiles, floats GPU-side | Same idea generalized: per-layer geodetic anchor in `dvec3` (§7) |
| Device | `headers/RHI/Device.h` | Single graphics queue; `immediateSubmit` blocks (loading-time only, never per-frame) | No transfer queue yet — do NOT add one in M0–M3; §6 works within one queue, async queue is a later ⚡ item |
| Textures | `headers/RHI/Texture.h` | `upload()` = blocking immediateSubmit, generates mips **by blit** | Blit mip-gen is illegal on BC formats → add `uploadMips()` (all mips supplied, still blocking) in M2 alongside ring image copies (§6.4) |
| Renderer | `headers/Renderer/Renderer.h` | `renderFrame(FrameSubmission)`; `materials()`; `uploadRing()`; depth picking via `FrameSubmission::depthQueries` → `depthSamples()` | Zero renderer changes needed for M1. M2 touches only UploadRing + Texture in RHI |
| Point-cloud GPU | `headers/Renderer/PointCloudGpu.h` | ONE device-local buffer: `[ComputeBatch[]][xyz4b][xyz8b][xyz12b][rgba]`, BDA addresses, fixed capacity, `kBatchStride`=32 | PCSL pool = one big `PointCloudGpu`; node visibility = re-uploading the compacted batch array via ring (tiny), §5 M3 |
| Picking | `src/Scene/ScenePicking.cpp` | Depth-pick world point → `pickModelAtPoint` AABB walk | Extend with an I3S hook: world point → layer → node OBB walk → feature-id from the picked node's triangle ranges (M1 basic, M4 exact) |
| Overlay/debug draw | `headers/Renderer/OverlayDrawList.h` + plugins | World-space lines/shapes appended per frame | Inspector OBB rendering (M0) uses this — no new render pass |
| JSON / images | `headers/libs/json.h` (nlohmann), `stb_image` | Already vendored | Use for all I3S JSON + jpg/png decode |
| Debug UI | `app::Application::buildUi` (`src/App/Application.cpp`) | ImGui debug panel, owns systems as members | Add an "SLPK" tab there; per-frame pump call sits next to `PointCloudLoader::updateStreaming` call site |

---

## 2. Scope of this plan

Milestones **M0–M3** implement roadmap phases **S0–S3** (open → render mesh
layers → streaming at scale → point-cloud layers). M4 lists the cheap tool
wins that fall out. Editing/export (S5) and BSL/services (S6) are explicitly
out of scope here — plan them in a follow-up once M0–M3 are real.

---

## 3. Libraries — download, pin, vendor, integrate

**Strategy:** source-vendored static code, compiled inside `StereoVista.vcxproj`
exactly like imgui/volk/spirv_reflect (`headers/libs/…` + filters). No DLLs,
no CMake, no prebuilt `.lib`s (you can't produce Windows binaries here).
For each: fetch the pinned release tarball, strip to the needed subset, keep
`LICENSE`, add sources to the `.vcxproj` + `.filters` under a `libs/<name>`
filter, add the include dir to `AdditionalIncludeDirectories` (line ~130 of
the vcxproj) **for both Debug and Release x64 configs**.

| Lib | Repo | Pin (verify latest online, then record here) | Subset to vendor | Notes / defines |
|---|---|---|---|---|
| **libdeflate** | github.com/ebiggers/libdeflate | v1.2x → **v1.25** (2025-11-01) ✅ vendored | **Decompress-only** subset (improved on the plan): `lib/{adler32,crc32,utils,deflate_decompress,gzip_decompress,zlib_decompress}.c` + `lib/x86/cpu_features.c` + headers, `libdeflate.h`, `common_defs.h`, `COPYING` | C code; gzip + raw deflate decompress. Compile as C (vcxproj does per-file `CompileAs` automatically by extension). Prefer it over zlib: ~2–3× faster inflate. NOTE: no new include dirs needed — internal includes are file-relative and app code uses `<libdeflate/libdeflate.h>` via the existing `headers/libs` dir |
| **draco** | github.com/google/draco | v1.5.x → `____` | **Decoder only**: `src/draco/` minus `compression/encode*`, `io/`, `maya/`, `unity/`, tools, tests | Needs a hand-written `draco/draco_features.h`: define `DRACO_MESH_COMPRESSION_SUPPORTED`, `DRACO_STANDARD_EDGEBREAKER_SUPPORTED`, `DRACO_ATTRIBUTE_VALUES_DEDUPLICATION_SUPPORTED`, `DRACO_ATTRIBUTE_INDICES_DEDUPLICATION_SUPPORTED`. ~140 files; wrap includes in `#pragma warning(push/disable)` at OUR call sites, not by editing vendored files |
| **basis_universal transcoder** | github.com/BinomialLLC/basis_universal | v1.x/v2.x → `____` | `transcoder/basisu_transcoder.{cpp,h}` + its headers + `zstd/zstddeclib.c` (single-file zstd decode — KTX2 payloads are often zstd-supercompressed) | Define `BASISD_SUPPORT_KTX2=1`, `BASISD_SUPPORT_KTX2_ZSTD=1`. Transcode targets used: BC7 (`cTFBC7_RGBA`), BC5 (normals, optional), RGBA32 fallback |
| **lepcc** (M3, defer vendoring until then) | github.com/Esri/lepcc | master @ `____` | `src/*.cpp,h` (it is small) | Apache-2.0. Decoder entry points: `lepcc_getBlobInfo`, `lepcc_decodeXYZ`, `lepcc_decodeRGB`, `lepcc_decodeIntensity` |
| **MD5 (tiny)** | any public-domain single-file (e.g. the RFC 1321 reference or `md5-c`) | github.com/Zunawe/md5-c @ **f3529b6** (Unlicense) ✅ vendored | `md5.{c,h}` + `UNLICENSE` under `headers/libs/md5/` | Only used to hash resource paths for the SLPK `@specialIndexFileHASH128@` index (§4.1). Keep optional: central-directory lookup is the always-works path. NOTE: `md5.h` has no `extern "C"` guard — wrap the include at C++ call sites |

- [ ] All libs fetched at pinned tags, subset-vendored under `headers/libs/<name>/`,
      licenses kept, wired into vcxproj + filters + include dirs (both configs),
      pins recorded in the table above.
      *(M0 status: libdeflate + md5 done ✅ — vendored, wired into vcxproj +
      filters, gcc-compile-checked; no include-dir edits were needed, see table
      notes. draco / basisu / lepcc remain for M1 / M2 / M3.)*

*(draco encode, basis encode, and i3s-lib come only with the future
export/edit milestone — do not vendor them now.)*

---

## 4. New module layout

### 4.1 `src/Loaders/Slpk/` + `headers/Loaders/Slpk/` (namespace `i3s::`)

```
SlpkArchive.{h,cpp}     ZIP64 reader over a Platform file mapping.
                        open(path) → parse EOCD/EOCD64 + central directory into
                        a path→{offset,size,method} map (STORE entries = zero-copy
                        spans into the mapping; the rare DEFLATE entry inflates).
                        read(path) → std::vector<uint8_t> (gunzips *.gz via
                        libdeflate transparently: "x.json" finds "x.json.gz").
                        Optional fast path: parse @specialIndexFileHASH128@
                        (md5(path)→offset pairs, sorted) when present.
                        THREAD-SAFE for concurrent read() (mapping is immutable).
SlpkTypes.h             POD model of the parsed JSON: LayerInfo (type, version,
                        CRS, store, attributeStorageInfo[], statistics refs,
                        textureSetDefinitions, geometryDefinitions), NodeInfo
                        (OBB center dvec3 + halfSize vec3 + quat, lodThresholdSQ,
                        parent/children indices, mesh {geometry id, material id,
                        texture id, attribute ids, vertex/feature counts}).
I3SLayer.{h,cpp}        Parses 3dSceneLayer.json(.gz) → LayerInfo. Handles
                        version differences 1.6→1.8 in ONE place (normalize:
                        1.6 per-node 3dNodeIndexDocument tree walk and 1.7+
                        nodePages/*.json.gz both produce the same flat
                        std::vector<NodeInfo>). Fail loudly w/ version+reason.
I3SGeometry.{h,cpp}     Geometry decode: draco buffer → interleaved
                        renderer::Vertex vector + index buffer + featureIds +
                        uv-regions; v1.6 raw binary path (header-described,
                        positions float3 + normal + uv + color + faceRange).
                        Tangents: I3S has none — generate per-triangle or leave
                        flat (normalScale 0) for M1; meshoptimizer-quality
                        tangents are NOT worth it for photogrammetry meshes.
I3STexture.{h,cpp}      Texture decode: jpg/png via stb (exists) → RGBA8 mips
                        CPU-side; ktx2/basis via transcoder → BC7 mip chain.
                        Output: {VkFormat, extent, mip data spans} — RHI-free.
I3SAttributes.{h,cpp}   attributeStorageInfo-driven column readers (string /
                        numeric arrays per node) + layer statistics JSON.
I3SStreamer.{h,cpp}     M2: thread pool + request lifecycle + caches (§6).
GeoAnchor.{h,cpp}       WGS84 geodetic↔ECEF↔local-ENU (double precision, ~60
                        lines, no PROJ). anchor = dvec3; nodeLocalToAnchor(
                        nodeCenterGeodetic) → dmat4 → cast to mat4 (§7).
```

`Platform::` addition: `headers/Platform/FileMapping.h` — RAII read-only
memory map (Win32 `CreateFileMappingW`/`MapViewOfFile`; keep the tiny POSIX
`mmap` variant under `#ifdef` so you can sanity-run parsing logic locally if
you choose — Windows path is the shipped one).

### 4.2 `src/Scene/I3SSceneLayer.{h,cpp}` (namespace `scene::`)

The scene-side object (parallel to `scene::Model`, NOT inside it):

```cpp
class I3SSceneLayer {
    // identity + placement
    std::string sourcePath;  bool visible = true;
    glm::dvec3 anchorGeodetic;        // layer anchor (root OBB center)
    glm::mat4  layerTransform{1.0f};  // user transform in anchor space
    // data
    i3s::LayerInfo info;  std::vector<i3s::NodeInfo> nodes;
    // per-node GPU residency (M1: MeshBuffer + material index; M2: cache slots)
    // per-frame:
    void select(const renderer::ViewCamera&, VkExtent2D, float lodScale,
                std::vector<uint32_t>& outVisible);       // SSE traversal §6.2
    void submit(renderer::FrameSubmission&);              // emit DrawItems
};
```

`scene::Scene` gains `std::vector<std::unique_ptr<I3SSceneLayer>> i3sLayers;`
(pointer-stable; layers hold GPU resources) + inclusion in
`computeWorldBounds()`. `Application` gains: open path (file dialog filter
`*.slpk` + drag-drop), per-frame `select/submit` calls where models are
submitted today, the streamer pump next to `PointCloudLoader::updateStreaming`,
and the SLPK tab in `buildUi` (M0 inspector grows here).

---

## 5. Milestones

### M0 — Read the package + inspector (no geometry rendering)

- [x] §3 vendoring for libdeflate + md5 (draco/basisu can land here or with M1).
- [x] `Platform::FileMapping`, `SlpkArchive` (EOCD→EOCD64→central directory;
      STORE zero-copy spans; gzip-transparent `read()`; hash-index fast path
      optional), `I3SLayer` + node normalization (1.6 tree docs AND 1.7+ node
      pages → one flat `NodeInfo` array), `GeoAnchor`.
- [x] Wire "Open SLPK…" into the ~~debug panel~~ **production GUI** (File menu
      + dockable "Scene Layers" panel — the debug panel was already replaced
      by `Gui::GuiSystem`/panels when M0 started) + window **drag-drop**
      (new GLFW drop callback on `Platform::Window`, routes .slpk / models /
      point clouds); parse on a worker thread (`std::thread` + atomic done
      flag, adopted by a main-thread pump — UI never blocks).
- [x] Inspector v0 in the **Scene Layers panel**: layer summary (type, version,
      CRS, node count/levels, texture/geometry defs, attribute fields),
      node-level slider (+ cumulative toggle + box cap), and **OBBs drawn via
      `OverlayDrawList`** (color by tree level) in anchor space; camera
      auto-frames the layer bounds on open (and widens far-plane/fly-speed
      for city-scale layers).
- [ ] 🧪 Gate: owner opens (a) an Esri sample 1.6 SLPK, (b) a 1.7/1.8 one
      (see §9): OBB cloud appears < 1 s even for multi-GB files, shape matches
      the real dataset, level slider walks the hierarchy. Commit: `M0: SLPK
      archive reader + node tree + inspector`.
      *(Code complete & pushed — awaiting the owner's Windows run; exact
      steps are in the M0 commit message. A later session should NOT
      re-implement M0; start M1 and fold in any gate feedback.)*

#### M0 field notes (reality vs. plan — read before M1)

- **Production GUI already exists.** `Gui::Services` facade + panel files
  (`src/Gui/panels/*`) replaced the interim debug panel; the SLPK UI is a
  proper panel (`SlpkPanel.cpp`, window title `Gui::Windows::Slpk`). New
  app-touching operations go through `Gui::Services` (`openSlpkDialog`,
  `slpkLoadsInFlight`, `frameI3SLayer`, `unloadI3SLayer`); the layer objects
  themselves are reached via `services.scene().i3sLayers`.
- **Hash index facts (validated against an ArcGIS-cooked package):** the
  md5 keys hash the resource path **exactly as stored in the zip**
  (mixed case — do NOT lowercase before hashing); the record table is *not*
  reliably memcmp-sorted (verify + sort on load); the speculative locate
  (last entry before the central directory, probe extra-field lengths)
  works — `SlpkArchive` opens O(1) without touching the central directory
  and falls back to a full CD parse when anything mismatches.
- **PCSL node pages already parse in M0** (cheap win): `store.index`
  paging with both `nodesPerPage` (2.0) and `nodePerIndexBlock` (1.x)
  keys, `density-threshold` LOD metric, implicit `firstChild`/`childCount`
  ranges. M3 starts from a working PCSL tree.
- **OBB conventions:** quaternion is `[x, y, z, w]` and orients the box in
  **ECEF** for geographic layers (Cesium/loaders.gl interpretation —
  validated: DA12's node cloud coheres around its anchor); projected-CRS
  layers use the CRS frame directly. ENU → app-world axis map is
  `app = (east, up, -north)` (`kEnuToApp` in `I3SSceneLayer.cpp`).
- **Test coverage:** a scratch harness (gcc + ASan/UBSan, POSIX branch of
  `FileMapping`) exercises archive/layer/tree/anchor/scene-layer/overlay on
  the §9 packages — real v1.7 + generated v1.6 + PCSL 2.0
  (`StereoVista/testdata/README.md`, `make_synthetic_slpk.py`). No public
  small v1.6 or PCSL download was found; synthetics follow the spec shapes.

### M1 — Render mesh layers correctly (blocking loads, bounded budget)

- [ ] Vendor draco (decoder subset, §3). `I3SGeometry` (draco + 1.6 raw) →
      `renderer::MeshData`-shaped output + featureIds + uv-regions.
- [ ] `I3STexture` jpg/png path (atlas → RGBA8 + full mip chain computed
      CPU-side or via existing blit path — blit is fine for RGBA8 in M1).
- [ ] uv-region support: extend `Vertex`… **no** — do NOT touch the shared
      48-byte `Vertex`. Instead: per-node second vertex stream is overkill for
      M1; **fold the uv-region remap into the UVs at decode time** (positions
      of atlas sub-rects are static — remap uv into the rect once on the CPU).
      Only if a package uses `uv-region` with wrap semantics does the shader
      path matter — detect and toast "wrap uv-regions unsupported yet" (§8 risk).
- [ ] Materials: I3S material defs → `gpu::MaterialData` (baseColor, factors,
      albedo/normal/metallicRoughness/AO indices via `MaterialSystem`).
- [ ] `I3SSceneLayer` M1 loading policy: walk tree from root, load nodes
      breadth-first **on a loader thread** (blocking `MeshBuffer::create` +
      `Texture::upload` are called from… they touch Vulkan → **hand off to
      main thread**: loader thread decodes into CPU structs, main thread
      per-frame drains a ready-queue and creates GPU objects under a
      time/byte budget — this IS the §6.1 pipeline minus eviction/priority,
      build it in that shape from day one) until a node/VRAM budget is hit
      (default ~2 GB or N nodes, debug-panel sliders).
- [ ] SSE selection (§6.2) picking the best loaded cut each frame →
      `submit()` emits `DrawItem`s (model = anchorSpace * nodeLocal, bounds
      from OBB → `worldBoundsCenter/Radius`).
- [ ] Picking hook: extend `pickModelAtPoint`-style resolution with I3S:
      depth-pick world point → layer → deepest resident node whose OBB
      contains it → show node + (if featureIds decoded) feature id + M1-level
      attribute readout in the inspector.
- [ ] 🧪 Gate: Esri sample 3D-object AND integrated-mesh SLPKs render with
      correct textures/lighting vs Scene Viewer screenshots; no cracks at
      rest; sun shadows fall on I3S geometry; stereo SBS sanity check; VRAM
      stays under the budget slider. Commit: `M1: I3S mesh layers render`.

### M2 — Streaming at scale (the performance milestone)

Everything in §6 (design details below). Task list:

- [ ] Vendor basisu transcoder (+zstddeclib). `I3STexture` KTX2→BC7 path.
- [ ] RHI: `Texture::uploadMips(spans…)` (blocking variant, BC-safe — no
      blit; used at load time) **and** `UploadRing::stageImage` + flush
      support for image copies (§6.4) for the streaming path.
- [ ] `MaterialSystem`: freeTexture(index)/slot-reuse free-list + LRU hooks
      (§6.5). Same for materials (or accept material-table growth — 64 B each,
      measure first).
- [ ] `I3SStreamer`: N worker threads (`max(2, hw/2 - 1)`), request states
      `Wanted→Reading→Decoding→Ready→Resident→Evictable`, per-frame
      re-prioritization by SSE contribution, cancellation (camera moved),
      prefetch ring (+1 LOD beyond the cut along camera velocity), CPU + GPU
      byte budgets with LRU eviction, per-frame GPU-create budget (ms + MB).
- [ ] Traversal hysteresis (split at T, merge at T×1.15) + "draw finest
      resident ancestor while children stream" (never show holes).
- [ ] Vendor + wire **Tracy** (TODO §G says do it first — this milestone is
      where it pays): zone the pipeline stages + a streaming HUD in the SLPK
      tab (queued/decoding/ready/resident, MB/s, ring occupancy, eviction).
- [ ] 🧪 Gate (the flagship demo): cold-open a ≥ 10 GB city SLPK → first
      pixels < 1 s; fly-through 60 FPS mono / 90 stereo on the owner's GPU;
      no loader-caused frame > ~20 ms (Tracy proof); roaming an hour leaks
      nothing (HUD counters flat). Commit: `M2: streaming LOD`.

### M3 — Point cloud scene layers (PCSL)

- [ ] Vendor lepcc (§3). PCSL branch in `I3SLayer` (leaf-only node model,
      OBB-only) + `I3SGeometry` lepcc-xyz/rgb/intensity decode.
- [ ] Pool residency: ONE `PointCloudGpu` per PCSL layer sized from layer
      metadata (or a few pools of fixed capacity); each resident node owns a
      segment of points + its `ComputeBatch` entries. Node quantization: reuse
      the existing 30-bit relative encoding — encode against the node OBB,
      batch carries the node→anchor transform (check
      `Engine::ComputeBatch`/`pointcloud_types.h` for the exact fields — the
      LAS path already does per-batch bounds).
- [ ] Traversal (same SSE machinery, `pointsPerMeter`-style density metric per
      spec) flips node visibility by **rebuilding the compacted ComputeBatch
      array** and ring-uploading it (32 B × batches — trivial) + updating
      `PointCloudDrawItem::numBatches`.
- [ ] Colorization: RGB / intensity ramp / classification palette /
      elevation ramp; bounds from the layer statistics JSON; palette editable
      in the SLPK tab. (RGBA channel already carries intensity in A — class
      code needs a per-point byte: add a 6th section to the pool buffer,
      shader change gated on a flag — mirror how HQS flags work in
      `pointcloud_common.glsl`.)
- [ ] 🧪 Gate: a large PCSL (≥ 100 M points) roams out-of-core at full frame
      rate with HQS on; classification palette matches ArcGIS defaults;
      VRAM flat while roaming. Commit: `M3: I3S point cloud layers`.

### M4 — Tool wins that fall out (each is small; do after M2)

- [ ] Clip planes already hit meshes + clouds (`FrameSubmission::clipPlanes`)
      — verify on I3S draws, add "slice layer" preset in the SLPK tab.
- [ ] Attribute pop-up: click → feature attributes (M1 picking + M2 attribute
      cache → small ImGui window). Feature filter/tint-by-attribute via
      `DrawItem::tint` first (per-node), per-feature mask later.
- [ ] Daylight: solar position (NOAA formulas, ~40 lines) from layer
      geolocation + date/time sliders → drive `SunState.direction`.
- [ ] Measurement/LOS on I3S surfaces: verify `MeasurementTool` +
      depth-picking work (they should — depth is depth); fix what doesn't.
- [ ] Inspector v1: wireframe toggle (dynamic polygon mode exists? check
      `rhi::Pipeline` dynamic state; else a debug pipeline), LOD-level tint,
      per-node hover info, memory HUD polish.

---

## 6. Streaming design (M2 details)

### 6.1 Pipeline shape (mirrors `PointCloudLoader` progressive pattern)

```
worker pool (no Vulkan!)                       main thread (owns all Vulkan)
─────────────────────────                      ──────────────────────────────
IO: SlpkArchive::read (mmap+gunzip)            pump (per frame, budgeted):
 → decode: draco / lepcc / basisu→BC7 / stb      drain Ready queue:
 → CPU NodePayload {vertices, indices,             create MeshBuffer / Texture
    mips, attributes}                              (or ring-fill, §6.3/6.4)
 → push Ready queue (mutex+vector, or              flip node Resident
    moodycamel-style SPSC if it shows          traversal → wants/evicts
    in Tracy — measure first)                  re-prioritize + cancel queue
```

Request keys: `(layerId, nodeIndex, resource)`. States advance atomically;
a cancelled request that already decoded is kept in the CPU cache (it was
paid for) unless over budget.

### 6.2 Traversal (per frame, per layer)

For each node from the roots: frustum-test OBB (anchor space, camera
relative); compute projected OBB screen area `A` (project the 8 corners,
2D convex hull area is overkill — use the spec metric: bounding-sphere
screen diameter for MBS thresholds, `PI/4·d²` comparison for
`maxScreenThresholdSQ`); if `A > thresholdSQ × lodScale` and children exist
→ want children, recurse into resident ones; else draw this node. Hysteresis:
split at `T`, merge at `1.15·T`. `lodScale` is the user quality slider
(default 1.0). Draw rule: deepest **resident** ancestor covers missing
children — never a hole. Output: draw list + want list (with `A` as
priority) + evict candidates (not wanted for > N frames, LRU order).

### 6.3 Node geometry through the ring (replaces blocking `MeshBuffer::create`)

Create device-local vertex/index `rhi::Buffer`s (no data) on the main
thread, then `UploadRing::stage` the payload into them across ≥ 1 frames
(mark/rollback per node — all-or-nothing, retry next frame on full ring;
this is exactly the point-cloud chunk pattern). Node becomes drawable when
its last copy is flushed. Keep `MeshBuffer::create` for M1/small nodes.

### 6.4 `UploadRing::stageImage` (new, keep it minimal)

`bool stageImage(const Texture& dst, uint32_t mip, VkExtent2D extent, const
void* data, size_t size)` — copies into ring memory, queues a
buffer→image region. `flush()` gains: for each touched image, one
UNDEFINED→TRANSFER_DST barrier (first touch), batched
`vkCmdCopyBufferToImage`, then →SHADER_READ_ONLY. Images uploaded this way
carry their full mip chain from the CPU (BC7 from KTX2 has it; RGBA8 mips
computed CPU-side by the decode worker — do NOT blit in the streaming path).
Texture object creation (vkCreateImage) stays on the main thread in the pump.

### 6.5 Budgets + eviction

- GPU budget: default = min(user slider, VMA budget × 0.7). Tracks bytes of
  node buffers + textures. Evict (destroy buffers, `freeTexture` slots) in
  LRU order until under budget; eviction of a node currently drawn is
  forbidden (it leaves the draw set first via traversal).
- `MaterialSystem::freeTexture(uint32_t)`: push index to a free-list;
  `addTexture` pops it first; the descriptor slot is rewritten on reuse
  (legal under UPDATE_AFTER_BIND) — destroy the `rhi::Texture` only after
  the frame timeline passes the last frame it was bound (defer via a
  small (texture, timelineValue) graveyard drained in the pump; the
  Renderer's frame timeline value is the fence).
- CPU decoded-payload cache: byte-capped LRU (default 1–2 GB).
- Per-frame pump budget: default 2 ms CPU or 32 MB staged, whichever first
  (sliders in the SLPK tab; Tracy zones prove the budget holds).

---

## 7. Precision & CRS (exact scheme)

- Parse `spatialReference` from layer JSON. Global mode (WGS84/4326):
  vertex payload = per-node offsets (float) from node MBS center given in
  lon(°)/lat(°)/height(m). Per node, on the decode worker, convert each
  vertex ONCE in double: geodetic(nodeCenter + delta·axisScale) → ECEF →
  ENU-at-anchor (all `double`), store float ENU positions relative to the
  **node center in ENU** (so floats stay small); node model matrix =
  translate(enuNodeCenter) (float, anchor space). Degrees→meters for the
  deltas: the axis scale at the node latitude (spec: x/y deltas are degrees
  — verify against the sample data in M0; the 1.6 store JSON declares
  offset/scale). **Normals**: global-mode normals are ECEF-oriented per
  spec version — rotate into ENU with the same basis (double→float, once).
- Local mode (projected CRS): treat as metric local space; anchor = root
  center; unknown/exotic CRS → load anyway + toast a warning (geometry is
  self-consistent; only geolocation-dependent tools disable).
- `GeoAnchor` owns all of this; nothing else in the app sees geodetic
  coordinates. Camera/tools stay in anchor space (existing float world).
  Co-registration of multiple layers/clouds: anchor deltas in double at
  layer level (`layerTransform` seeded from `anchorA − anchorB`).

---

## 8. Performance gates & known risks

Gates (Tracy-proven at M2, on the owner's stereo GPU): cold-open < 1 s to
first pixels on NVMe; fly-through 60/90 FPS; render-thread loader cost
< 2 ms/frame; VRAM flat while roaming; decode ≥ ~400 draco nodes/s on 8
cores. Risks carried from the roadmap: 1.6-era format quirks (normalize in
`I3SLayer`, keep version handling in ONE file); uv-region wrap semantics
(detect + toast, defer); ETC1S normal-map quality (prefer UASTC, fall back
to flat normal + toast); attribute columns can be huge (lazy, load-on-click
until M4 filters need columns resident); MSVC compile of vendored draco
(watch for `/bigobj` needs — set it on the draco files in the vcxproj if
the owner reports C1128).

---

## 9. Test data & verification playbook

- ~~Esri publishes sample SLPKs in/linked from i3s-spec~~ **Reality (M0):**
  the i3s-spec repo carries no sample packages. What we use instead —
  all documented in the committed **`StereoVista/testdata/README.md`**:
  a real v1.7 3D-object package (DA12_subset.slpk from the loaders.gl test
  suite), plus spec-faithful **generated** v1.6 mesh + PCSL 2.0 packages
  (`testdata/make_synthetic_slpk.py`, committed). Packages themselves are
  gitignored. For multi-GB visual gates the owner supplies an
  ArcGIS-cooked or ArcGIS-Online-downloaded package (see the README).
- Cross-check rendering against ArcGIS Scene Viewer (free, browser) with the
  same package published, or the I3S Explorer (i3s.loaders.gl) screenshots.
- Every milestone's commit message body: bullet list of exactly what the
  owner should click and expect to see (they verify on Windows; you cannot).

---

## 10. Agent kickoff prompt (copy-paste; also usable to resume)

```
You are implementing SLPK/I3S support in StereoVista (native Vulkan 1.3,
Windows/MSVC — read CLAUDE.md first and follow it strictly).

Plan documents (read both, in this order, before any code):
  1. docs/SLPK_ROADMAP.md   — strategy, format primer (§1), architecture (§4)
  2. docs/SLPK_IMPLEMENTATION_PLAN.md — YOUR handbook: grounded integration
     map, library vendoring table, milestones M0–M4 with checkboxes,
     streaming design, precision scheme, acceptance gates.

Task: implement the milestones IN ORDER, starting at the first unchecked
checkbox in docs/SLPK_IMPLEMENTATION_PLAN.md §5 (fresh start = M0). Work
milestone by milestone; within a session, complete as many milestones as
you can do WELL — quality and correctness over coverage.

Hard requirements:
- Vendor every library the plan's §3 table names for your milestone(s):
  download the real pinned sources (verify current release tags online,
  record the pin in the table), strip to the listed subset, keep LICENSE
  files, place under headers/libs/<name>/, and FULLY integrate: every
  source file wired into StereoVista/StereoVista.vcxproj AND
  StereoVista.vcxproj.filters (both Debug|x64 and Release|x64), include
  dirs added to both configs. No DLLs, no CMake, no half-integrations.
- Respect the house invariants (CLAUDE.md + plan §0): no Vulkan above the
  RHI layer, projections only via renderer::Projection.h, CCW front faces,
  scalar-layout shared GPU structs, bindless materials + push
  constants/BDA, worker threads never touch Vulkan (worker decode →
  main-thread pump, like PointCloudLoader progressive loading).
- You cannot run MSBuild here: write MSVC-clean C++17 (no GCC extensions,
  no POSIX in app code — Win32 file mapping behind Platform::FileMapping),
  double-check includes and the vcxproj wiring of every added file.
- Update the checkboxes in docs/SLPK_IMPLEMENTATION_PLAN.md as you complete
  items, and keep the plan honest: if the real code contradicts the plan,
  read the source, do the better thing, and amend the plan in the same
  commit ("improve, don't copy").
- Commit at every milestone boundary (more often is fine) with a message
  whose body lists the exact manual verification steps for the owner
  (Windows, visual checks — plan §9). Push to your designated branch.
- Download 2–3 small sample SLPKs (plan §9) into StereoVista/testdata/
  (gitignored; commit only testdata/README.md with the URLs) and use them
  to sanity-check your parsing logic where possible (pure-CPU code like
  the archive reader, JSON model, node tree, and geodetic math CAN be
  exercised here — a scratch test harness outside the repo is fine).

Definition of done for the session: the milestone(s) you took on are fully
implemented, wired, checkbox-updated, committed, and pushed, with owner
verification steps in the commit messages.
```

---

*Follow-up plans (write after M3 ships): editing/export milestone plan
(SlpkWriter, undo-integrated edit ops, LAS→SLPK cooker — roadmap S5), tools
& BSL milestone plan (roadmap S4/S6).*
