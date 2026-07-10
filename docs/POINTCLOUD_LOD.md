# Point-Cloud LOD for the Compute Rasterizer — Research & Design

> **Status: research/design doc (no code yet).** Answers `TODO.md` §F ("re-implement
> LOD selection feeding the compute path") with a concrete, staged plan derived from
> the Schütz line of work and the Magnopus production write-up. Sources are linked in
> §7; read them before implementing a stage. Companion plans: `SLPK_ROADMAP.md`
> (§4.2 streamer, §4.4 traversal, Phase S3) — Stage 3 below deliberately reuses them.

---

## 1. Where we stand

The renderer (`PointCloudPass` + `pointcloud_common.glsl`) is an implementation of
Schütz et al. 2022, *Software Rasterization of 2 Billion Points in Real Time*:

- points partitioned into **batches of 10 240** (`kComputeBatchSize`), one compute
  workgroup per batch, per-batch AABB **frustum cull** in the workgroup prologue;
- coordinates quantised **10/20/30-bit** across three streams; the prologue picks a
  **precision level** from the batch's projected size and decodes 1–3 streams;
- 64-bit `atomicMin` framebuffer (standard path) or HQS depth+accumulate, resolved
  into the scene pass; single-pass multi-view (decode once, project per eye);
- the loader (`PointCloudLoader.cpp`) **Morton-sorts** points, cuts the sorted
  sequence into batches (tight AABBs), then **shuffles whole batches** keeping each
  batch's internal Morton order (the 2021 vertex-order paper's layout: locality
  without atomic hot-spotting).

What this already gives us is *bandwidth* LOD: a far-away batch reads 1 instead of
3 coordinate streams, and the early-depth reject skips most doomed atomics. What it
does **not** reduce is *work per point*: every point of every frustum-visible batch
is still read, decoded, clip-tested and projected every frame, even when the whole
10 240-point batch lands on a dozen pixels. And nothing exists for clouds that
exceed VRAM (`TODO.md` §F, the unported GL `OctreePointCloudManager`).

So "an LOD system" is really two problems:

| Problem | Goal | Solved by |
|---|---|---|
| **In-core density LOD** | process ~O(pixels covered) points, not O(points loaded) | Stages 1–2 |
| **Out-of-core hierarchy** | render clouds larger than VRAM, bounded memory | Stage 3 |

---

## 2. What the literature offers

### 2.1 Schütz, Krösl, Wimmer 2019 — *Real-Time Continuous LOD Rendering of Point Clouds* (IEEE VR)

The foundational CLOD paper (built for VR — directly relevant to our stereo/XR
path). No octree at render time: a **flat buffer** where every point carries a
small **level attribute** (its would-be octree depth, assigned once at build), plus
an implicit random component. Each frame (or amortised over frames) a **reduction
compute pass** filters the full cloud into a small "visible" vertex buffer:

```glsl
// filter_points.cs (essential-samples repo, m-schuetz/ieeevr_2019_clod)
float level        = float((v.colors & 0xFF000000) >> 24);   // per-point LOD level
float pointSpacing = rootSpacing / pow(2.0, level);           // spacing this point represents
float targetSpacing = (d * CLOD) / (1000 * max(1 - 0.7 * dc, 0.3));
//     d  = distance to camera,  dc = distance from screen centre (foveation term)
if (pointSpacing < targetSpacing) return;                     // discard
int i = atomicAdd(drawParameters.count, 1);                   // keep → compact
targetBuffer[i] = v;
```

Points are also culled against an **extended frustum** so head rotation between
(amortised) rebuilds doesn't reveal holes; the render shader derives point size
from the local target spacing. Reported: 86 M points reduced to ~5 M in ~5.45 ms on
a GTX 1080, i.e. ~17 M points/ms; density changes are per-point and continuous —
**no popping**, unlike chunk-based (Potree-style) LOD.

### 2.2 Magnopus 2026 — *How We Render Extremely Large Point Clouds* (the "Magnumopus" post)

The most useful reference for us because their base renderer **is our renderer**:
they built the Schütz-2022 quantised-batch (QB) compute rasterizer (same 10 240
batches, same three-precision-buffer quantisation, same batch culling) and then
bolted the 2019 CLOD on top as a hybrid ("QB+CLOD"). Key implementation facts:

- **LOD assignment at import**, 2 bits per point (levels 0–3): counting grids of
  1 m / 0.5 m / 0.25 m; per cell the point **closest to the cell centre** becomes
  the proxy for that level, its colour replaced by the **average colour** of the
  cell (anti-aliasing); everything not picked is level 3 (full detail). Level is
  stored in the colour alpha channel. A **randomised, normalised per-point weight**
  smooths the level transitions (continuous blend instead of 4 hard shells).
- **The reduction pass outputs a second cloud in the *same* QB format** (three
  quantised position buffers + colour buffer + batches), so the unchanged QB
  rasterizer renders the reduced cloud. Batch bounding boxes are tested first,
  each thread evaluates many points (batch-strided loop, like ours).
- **Double-buffered + amortised**: an upper bound of points evaluated per frame, a
  resume cursor for the next frame; the front LOD buffer renders while the back one
  fills, swap on completion. LOD buffer capped at 125 M points (allocation limit).
- **Honest results** (RTX 3080 Laptop, 160 M-point LA scan): QB+CLOD wins on some
  views (9.2 → 2.0 → 0.7 ms across zoom levels), loses on others, and costs the
  duplicate buffer memory. Their conclusion: worthwhile for their VR use case, but
  not a universal win over plain QB — the reduction pass itself touches all points
  unless amortised.

### 2.3 Schütz, Kerbl, Klaus, Wimmer 2023 — *GPU-Accelerated LOD Generation for Point Clouds* (CGF)

Answers "who builds the LOD structure, and when": construction of a **hybrid
voxel–point layered structure** (octree; leaves ≤ 50 k original points; inner nodes
hold ~128² surface **voxels** sampled on a 128³ per-node grid, stored as vertex
lists) entirely on the GPU at **1–4 G points/s** on an RTX 3090 — LOD build stops
being a preprocessing step and becomes part of *loading* (80–400× the CPU
converters). Two phases, both portable to Vulkan compute in principle:

1. **Split** into leaves via a counting-grid pyramid (project into 256³ counters,
   merge <T cells upward, allocate, re-project to insert; recurse into over-full
   cells with 16³ sub-grids);
2. **Bottom-up voxel sampling** — one workgroup per node, children's points/voxels
   projected into the 128³ grid; sampling strategies from "first come" (32 KB
   shared-memory bitmask) to neighbourhood-weighted colour averaging.

Rendering uses a **replacing scheme**: cull the octree against the frustum, draw
nodes whose projected box exceeds ~100 px by descending to children, and discard a
node's voxels in octants where a drawn child replaces them. Voxel coordinates are
small local integers — cheap to store and **stream** (this is also I3S's model).
Caveat: reference implementation is CUDA (cooperative groups / single persistent
kernel); a Vulkan port would use level-by-level dispatches instead.

### 2.4 Schütz, Kerbl, Wimmer 2024 — *SimLOD: Simultaneous LOD Generation and Rendering*

The follow-up folds construction into **incremental streaming**: as points arrive
from disk they are inserted into a GPU-resident octree (inner-node voxels on a
128³ grid, chunked linked lists for growth) while the renderer draws whatever the
tree currently holds — up to 580 M points/s ingested (RTX 4090 + PCIe-5 SSD),
~16 B/point. This is the nicest UX (no wait, no converter) but it is deeply
CUDA-shaped (persistent kernels, dynamic allocation, cooperative groups), in-core
only today, and would replace our loader/streaming path rather than extend it.
Treat as the long-term north star, not the first implementation.

### 2.5 Out-of-core context

Potree / Schütz 2020 (*Fast Out-of-Core Octree Generation*) is the classic
offline-converter + node-streaming model; **I3S/SLPK point-cloud scene layers are
the same model as a standardised format**, and `SLPK_ROADMAP.md` already plans the
streamer (§4.2), SSE traversal (§4.4) and PCSL rendering (Phase S3) — with node
segments living inside `PointCloudGpu` and visibility flipped by editing the batch
list. `TODO.md` §F explicitly says out-of-core LOD falls out of that work.

---

## 3. Recommended plan — three stages, cheapest first

### Stage 0 — measure (prerequisite)

`TODO.md` §G already says it: **Tracy first.** The 2022-paper pipeline is bandwidth
+ atomic bound; our early-depth reject already removes most redundant atomics.
Before adding buffers, get per-pass GPU timings (geometry vs lookup vs resolve, per
view count) so Stages 1–2 have a baseline and an accept/reject criterion.

### Stage 1 — in-shader batch density LOD (no new data, ~a day)

The observation that makes this nearly free: **within each batch, points are in
Morton order** (loader phase 2 sorts globally, shuffles *whole batches* only). A
Morton-ordered sequence visits space along the Z-curve, so taking **every k-th
point of a batch is approximately a uniform spatial subsample** of that batch —
i.e. each batch already *is* a continuous LOD structure, for free.

The workgroup prologue already projects the batch AABB for `pcPrecisionLevel`.
Extend it: estimate the batch's projected pixel area `A`, pick a target point count
`n_target ≈ ρ · A` (ρ = points-per-pixel budget, default ~2; clamp to
`batch.numPoints`), and have the strided point loop sample `n_target` indices
spread across the batch instead of all of them:

```glsl
// prologue: keep = n_target / numPoints  (fold the max over visible views)
uint step = max(1u, uint(float(batch.numPoints) / float(nTarget)));
// loop: localIndex = (i * WG + lid) * step  (+ hash(localIndex) % step jitter
//        to break Z-curve aliasing);  bail when localIndex >= numPoints
```

Because `pcBatchSpacing` derives splat spacing from `numPoints`, passing the
*effective* count (`n_target`) into it makes the **adaptive splat radius grow
automatically** as density drops — holes self-heal, matching the CLOD "point size
from spacing" rule. HQS is unaffected (fewer, larger contributions inside the same
depth window).

Properties: zero memory, zero loader change, per-batch-continuous (no popping —
`n_target` varies smoothly with distance), works per view (fold like the precision
level: most demanding visible view wins), and it composes with everything later —
Stage 3's node segments are just batches, so they inherit it. Limitations:
granularity is the batch AABB (a batch straddling near+far gets the near view's
density), and the subsample is "every k-th along the Z-curve", not a proxy-point
set with filtered colours — expect slight colour speckle at extreme reduction.
Add `pointsPerPixel` (0 = off) to `PointCloudSettings` and the debug panel.

**Gate:** Tracy shows geometry-pass time scaling with on-screen density, not cloud
size; zoomed-out city scan drops from O(all points) to O(pixels); no visible
popping while dollying; stereo eyes agree.

### Stage 2 — CLOD reduction pass (Magnopus/Schütz-2019 hybrid) — optional, evaluate after Stage 1

Full per-point CLOD, worth building only if Stage 1 + Tracy still show a gap
(Magnopus measured the hybrid as a *sometimes*-win over plain QB — our Stage 1
captures much of the same reduction without the memory bill). Design, adapted to
our architecture:

1. **Import-time level assignment** (in `PointCloudLoader`, after the Morton sort —
   the sort makes grid bucketing cheap): counting grids at 1 / 0.5 / 0.25 m (scale
   by cloud extent), proxy = point nearest the cell centre, proxy colour = cell
   average; store the level (2 bits) + a per-point random byte. Storage: a sixth
   section in `PointCloudGpu` (`u8[points]`, +1 B/point) — do **not** steal the
   RGBA alpha, it carries intensity. `PointCloudGpuAddresses` grows one pointer;
   the dispatch struct has spare padding.
2. **Reduction compute pass** (new `pointcloud_reduce.comp`, dispatched on the
   async compute queue before the rasterize passes): per batch — extended-frustum
   cull, then per point the 2019 acceptance test (`pointSpacing = rootSpacing /
   2^(level + rand·1.0)` blended, `targetSpacing = d · CLOD/1000` with the min `d`
   across views so one reduction serves both eyes) — accepted points are
   **re-emitted in the existing five-section QB layout** into a pass-owned LOD
   cloud: per-source-batch `atomicAdd` cursors keep batch structure (output batch
   inherits the source AABB; quantisation is already relative to that AABB, so the
   packed coordinates copy through unchanged — the reduction is a compacting copy,
   exactly the 2019 paper's "cache-friendly buffer-to-buffer copy").
3. **Double-buffer + amortise** (Magnopus): front LOD cloud renders while the back
   one fills under a per-frame evaluated-points budget with a resume cursor; swap
   on completion; rebuild triggered by camera delta exceeding the extended-frustum
   margin. The rasterize dispatch consumes the LOD cloud's batch counts via
   `vkCmdDispatchIndirect` (count written by the reduction pass) so no readback.
4. **Budgeting:** cap the LOD cloud (e.g. 32–128 M points, user setting); if the
   reduction would overflow, raise `CLOD` and re-run (the 2019 paper's adaptive
   loop) — this doubles as a hard **point budget** control, which Stage 1 alone
   cannot guarantee.

Cost: one extra QB-format cloud ×2 (double buffer) in VRAM + 1 B/point levels on
every source cloud. Win over Stage 1: true per-point granularity, filtered proxy
colours (no speckle), foveation term for XR, and a global point budget.

### Stage 3 — hierarchical, out-of-core LOD (the real §F fix) — via the I3S streamer, not a new octree

Follow `SLPK_ROADMAP.md`: S2's streamer (thread pool, priorities, LRU, upload
budgets) + §4.4's SSE traversal + S3's PCSL path give node-granular LOD where
**node residency/visibility is just batch-list editing** — the compute rasterizer
needs no changes, and Stages 1–2 keep working *inside* resident nodes. What this
doc adds on top of the roadmap:

- **Local files join the same system:** at import, cook LAS/LAZ/PLY/… into the
  same node/segment representation the I3S path uses (an octree of leaf segments,
  each segment = whole batches). Build it with the **Schütz-2023 algorithm** —
  counting-grid split + bottom-up voxel/proxy sampling — which is expressible in
  our RHI (BDA + atomics + level-by-level dispatches replace CUDA cooperative
  groups), or CPU-side first (we already Morton-sort every cloud at load; the
  counting pyramid is a cheap extension) with the GPU build as a later ⚡ item.
  Cache the cooked tree next to the source file (the GL disk-cache idea, kept).
- **Inner-node proxies:** adopt the 2023 hybrid — inner nodes hold coarse
  quantised proxies/voxels *in QB batch format* (a voxel is just a point whose
  splat radius equals the sampling cell). Coarse batches render through the
  unchanged pipeline; the replacing-scheme traversal swaps a parent batch set for
  its children's when the projected size passes the threshold (~100 px rule).
  Popping between node levels is masked by Stage 1's continuous density ramp.
- **SimLOD later:** once the segment/traversal machinery exists, SimLOD-style
  incremental insertion during streaming load is an upgrade to the *cooking* step,
  not a new renderer. Revisit when either a Vulkan port precedent exists or we
  accept a CUDA interop path for NVIDIA only.

**Gate** (matches roadmap S3): billion-point cloud roams out-of-core at full frame
rate with HQS; VRAM bounded under a hard budget while roaming.

---

## 4. Integration map

| Piece | Where | Stage |
|---|---|---|
| Projected-area → `n_target`, strided-jittered sampling, effective-count spacing | `assets/shaders_vk/pointcloud_common.glsl` prologue + loop | 1 |
| `pointsPerPixel` setting + panel toggle | `FrameSubmission.h` (`PointCloudSettings`), debug panel | 1 |
| Per-point level+rand section (u8), proxy selection & colour filtering at import | `PointCloudLoader.cpp` (post-Morton), `PointCloudGpu` (+1 section), `pointcloud_types.h` | 2 |
| `pointcloud_reduce.comp`, LOD-cloud double buffer, indirect dispatch, budgets | `passes/PointCloudPass` (+ new pass-owned buffers), async-compute record path | 2 |
| Node cooking for local files (2023 split/sample), disk cache | new `Loaders/` step; shares batch/quantise code | 3 |
| SSE traversal → batch-list edits, per-node segments, LRU/eviction | `SLPK_ROADMAP.md` §4.2/§4.4/S3 machinery | 3 |

House rules that already fit: everything stays BDA/zero-descriptor, the reduction
pass rides the async compute queue and timeline chain, shared structs live in
`pointcloud_types.h`, reverse-Z/multi-view conventions unchanged.

---

## 5. Defaults to start from

| Parameter | Default | Source |
|---|---|---|
| Stage 1 density budget ρ | 2 points/pixel (0 = off) | tune with Tracy; HQS may want 3–4 |
| CLOD factor | 1.0-ish, exposed as a slider | Schütz 2019 (quality/perf dial) |
| LOD grid resolutions | 1 / 0.5 / 0.25 m equivalents, extent-scaled | Magnopus |
| LOD-cloud cap | 32–128 M points, double-buffered | Magnopus hit limits at 125 M |
| Reduction amortisation | ~100–200 M points evaluated/frame | Magnopus resume-cursor scheme |
| Node replace threshold | projected box ≈ 100 px | Schütz 2023 |
| Leaf size | ≤ 50 k points (≈ 5 batches) | Schütz 2023 |

---

## 6. Risks / open questions

- **Z-curve stride aliasing (Stage 1):** every-k-th sampling along Morton order can
  show structured patterns at some k; the index-hash jitter should kill it —
  verify visually on flat walls/roofs.
- **Reduction pass cost (Stage 2):** un-amortised it reads every point — the exact
  failure mode Magnopus measured. Ship it amortised-by-default or not at all.
- **Batch cursors vs. output ordering (Stage 2):** per-source-batch `atomicAdd`
  keeps batches contiguous but serialises on hot batches; if that shows up in
  Tracy, switch to workgroup-local compaction (subgroup ballot + one global add
  per workgroup — pairs with the §G subgroup-ops item).
- **Stereo divergence:** all stages fold per-view demands with min/max like the
  existing precision level — but XR reprojection headroom may want the extended
  frustum margin exposed as a setting.
- **Intensity alpha:** level bits must NOT displace intensity (unlike Magnopus we
  use A); hence the sixth section. Cheap, but touches the loader/GPU layout —
  keep it behind the Stage 2 decision.

---

## 7. Sources

- Schütz, Krösl, Wimmer 2019, *Real-Time Continuous Level of Detail Rendering of
  Point Clouds*, IEEE VR — [paper page](https://www.cg.tuwien.ac.at/research/publications/2019/schuetz-2019-CLOD/) ·
  [essential code](https://github.com/m-schuetz/ieeevr_2019_clod)
- Magnopus 2026, *How We Render Extremely Large Point Clouds* —
  [magnopus.com](https://www.magnopus.com/blog/how-we-render-extremely-large-point-clouds) ·
  [Medium mirror](https://medium.com/xrlo-extended-reality-lowdown/how-we-render-extremely-large-point-clouds-bdc1c1688dbf)
- Schütz, Kerbl, Klaus, Wimmer 2023, *GPU-Accelerated LOD Generation for Point
  Clouds*, CGF 42(8) — [paper page](https://www.cg.tuwien.ac.at/research/publications/2023/SCHUETZ-2023-LOD/) ·
  [arXiv](https://arxiv.org/abs/2302.14801) · [CudaLOD code](https://github.com/m-schuetz/CudaLOD)
- Schütz, Kerbl, Wimmer 2024, *SimLOD: Simultaneous LOD Generation and Rendering*,
  PACMCGIT — [arXiv](https://arxiv.org/abs/2310.03567) ·
  [code](https://github.com/m-schuetz/SimLOD) · [ACM](https://dl.acm.org/doi/10.1145/3651287)
- Schütz, Mandlburger, Otepka, Wimmer 2020, *Fast Out-of-Core Octree Generation
  for Massive Point Clouds* (Potree converter) —
  [paper page](https://www.cg.tuwien.ac.at/research/publications/2020/SCHUETZ-2020-MPC/)
- Base renderer papers (already implemented): Schütz, Kerbl, Wimmer 2021
  *Rendering Point Clouds with Compute Shaders and Vertex Order Optimization*
  ([arXiv](https://arxiv.org/abs/2104.07526)) and 2022 *Software Rasterization of
  2 Billion Points in Real Time* ([compute_rasterizer](https://github.com/m-schuetz/compute_rasterizer))
