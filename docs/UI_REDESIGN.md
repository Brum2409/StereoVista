# StereoVista UI/UX Redesign — Master Plan (Vulkan branch)

**Living document** for the full GUI remake **on the `StereoVista-vulkan` branch** —
this is the active codebase; `main` is the feature-frozen OpenGL app whose GUI serves as
the *behaviour reference* (its sources sit in this tree as `ExcludedFromBuild` files).
The remake is executed in agent passes ([§18 Pass Plan](#18-pass-plan)); each pass
updates the [Status Board](#19-status-board). This plan **supersedes `docs/TODO.md` §D
(GUI)** and absorbs the UI-facing parts of TODO §C/§E; the rest of TODO.md (bugs,
optimization, SLPK roadmap) remains authoritative for non-UI work.

> **How to read this as an implementing agent — the only rule that matters:**
> Only three things are binding: the **Coherence Contracts (§3)**, the **Engineering
> Guardrails (§17)**, and your pass's **acceptance criteria**. Everything else is a
> *strong default* — you are expected to make most detailed decisions yourself and to
> replace any suggestion with something better without asking, as long as the big
> picture stays intact. Note deviations in the Status Board. Never end a pass with a
> broken or half-migrated app. Docs lag the code — read the actual source first.

---

## 1. North Star

StereoVista should feel like **one intelligent, polished instrument**, not a collection
of windows. A first-time user is productive in a minute because the layout matches what
every modern 3D tool taught them and the program figures out intent on its own. A power
user loses nothing: every capability stays, organized and searchable instead of removed.

### Principles

1. **Familiar by default** — Outliner left, Inspector right, viewport center, status bar
   bottom, `Ctrl+K` palette. Innovate on friction, not on layout conventions.
2. **One heart** — *everything* loaded or created lives in the Outliner: models, point
   clouds, **SLPK/I3S layers**, lights, environment, measurements, clip planes, groups.
   No orphan lists in tool panels (`PointCloudPanel` / `SlpkPanel` listings dissolve).
3. **The Inspector is always right** — exactly the relevant controls for the selection,
   with global settings clearly separated and labeled.
4. **Don't ask, know** — detect file types, remember answered dialogs, auto-frame first
   imports. Every deleted dialog is a feature.
5. **Progressive disclosure, never amputation** — advanced options fold away, nothing is
   stripped.
6. **One design system** — every panel, tool, overlay and plugin uses UiKit. A tool
   written next year must look like it shipped with the first release.
7. **Alive, quietly** — toasts confirm, hints teach (dismissible forever), empty states
   explain, the status bar talks. Intelligence never nags; all of it can be turned off.
8. **Everything is a command** — one registry feeds menus, palette, shortcuts, toolbars —
   and later, macros and an AI agent (§16).
9. **Built for growth** — new tools, object kinds, importers/exporters and editors arrive
   *by registration, not redesign* (§16). The deferred GI stack (Phase 9) and the SLPK
   roadmap (S-phases) must land inside this structure without reshaping it.
10. **Polished to the pixel** — micro-interactions, subtle motion and quality-of-life
    details everywhere (§15). Delight without distraction.

---

## 2. Where we are today (code inventory)

### Live on this branch (build on these)

| Asset | Where | Disposition |
|---|---|---|
| **GuiSystem**: dockspace host over the main viewport (passthru central node), main menu bar, DockBuilder default layout + Reset Layout, panel visibility flags, master GUI toggle | `headers/Gui/GuiSystem.h`, `src/Gui/GuiSystem.cpp` | **The App-Shell seed** — extend, don't replace (§5) |
| **Dockable Viewport panels** (plural): renderer's offscreen texture as ImGui image, size/input reported back via Services; XR falls back to fullscreen-through-the-central-node | `GuiSystem::drawViewportPanel`, `Gui::ViewportDisplay/ViewportPanelState` (`Services.h`) | Keep — already better than an insets contract |
| **`Gui::Services` facade** — panels never touch Application/Vulkan (mirrors `PluginContext`; concrete impl in `App/Application.cpp`) | `headers/Gui/Services.h` | Keep; grow as panels need |
| **Panels**: Scene (121 ln), Inspector (123), Settings (315), Cursor (179), PointClouds (122), ClipPlanes (84), Diagnostics (60), Slpk (656) — one window per file, titles in `Gui::Windows` | `headers/Gui/Panels.h`, `src/Gui/panels/*` | Scene/Inspector/Settings grow into the real thing; PointClouds + Slpk **dissolve** into them; Diagnostics feeds the status bar |
| **`Gui::Settings`** — clean, plain-data, serialization-ready settings struct (grouped; defaults = shipped behaviour). Explicitly: *grow this, never resurrect the GL `ApplicationPreferences` blob* | `headers/Gui/Settings.h` | The preferences foundation (§6.4, §12) |
| Theme system (7 themes + semantic `g_StyleColors` + swatch API), fonts (5 + FontAwesome), DPI/user scale | `imgui_sytle.h` / `imgui_style.cpp` | Keep & extend |
| ImGui docking v1.91.1 on `imgui_impl_vulkan` + glfw, multi-viewport OS drag-out, platform-window loop in `Application` | `headers/libs/imgui/` | Keep (see backend trap, §17) |
| **UndoManager** (generic command stack) — live but thinly used | `headers/Core/UndoManager.h`, `src/Core/UndoManager.cpp` | Grows into global History (§9) |
| Plugin system (GL-free): `onBuildOverlay` (once per frame feeds both eyes), `onRenderUI/Menu`, input hooks; `MeasurementPlugin` drives `MeasurementTool`; `PluginContext` currently has **no `preferences()`** | `headers/Plugins/*`, `docs/PLUGINS.md` | Keep, extend (§13); restore prefs access in P0 |
| Tools: TransformGizmo, MeasurementTool, ClipPlaneTool (overlay-drawn); ScenePicking | `src/Tools/*`, `src/Scene/ScenePicking.cpp` | Become registry-driven modes (§13) |
| Scene host: `scene::Scene` (interim, **read-only** `office.scene` loader), ModelImporter (Assimp), Primitives, PointCloudLoader (LAS/LAZ/PLY/HDF5/XYZ/PCB), full **SLPK/I3S stack** | `src/Scene/*`, `src/Loaders/*` | Pass 1 makes the scene editable/persistent (§7) |
| Renderer: single-pass **multiview** stereo (no per-eye GUI replay — ImGui drawn once at frame end), forward PBR + shadows, compute point clouds, overlay renderer, tonemap | `src/Renderer/*` | UI consumes via Services only |
| Toasts (`Services::toast` → Plugins::ToastLevel) | `Services.h` | Keep; UiKit restyle |

### Excluded GL reference (port *content* out; never re-include the files)

`ExcludedFromBuild` sources kept as the behaviour reference until re-implemented, then
deleted (CLAUDE.md "Migration state"):

| Reference | Where (this tree) | Fate under this plan |
|---|---|---|
| The full GL GUI (~8.8k lines): menu bar, scene hierarchy, settings content, per-object manipulation panels, toasts/overlays, custom widgets (toggle switch, nav item, section header… at `:498-668`), snapshot fuzzy-match (`:7267`) | `src/Gui/GUI.cpp` | Widget code → UiKit (P0); settings/panel *content* → Settings/Inspector (P2/P6); never compiled again |
| `SceneManager` (scene save/load/merge, undo integration, recents, save options, v2 format) | `src/Core/SceneManager.cpp` (GL-era header/impl) | Re-implemented for `scene::Scene` in P1 (v3 format, ids, groups) |
| `SnapshotManager` (named camera/scene/tool snapshots, thumbnails, tags, restore flags) + Snapshots window (`GUI.cpp:7252-7965`) | `src/Core/SnapshotManager.cpp` | Ported in P3 — **the** reference the product owner pointed at |
| `ShortcutManager` (rebindable actions, `shortcuts.json`, editor UI) | `src/Engine/ShortcutManager.cpp` | Re-implemented in P0 bound to command ids |
| Brush tool, cursor sync, `CursorPreview3D`, SpaceMouse wiring, octree LOD | various | Future ports via the §16.1 recipe (not UI passes' job) |

**Also true:** preferences/shortcuts/cursor-presets are **not persisted yet** (in-memory
defaults); GI/post-FX (VCT, DDGI, Radiance, Bloom, SSAO) are deleted pending a native
Phase 9 — the Settings/Inspector architecture must leave labeled room for their return.
Build: MSVC/VS2022 only, no CI, no tests; agents usually cannot compile (§17).

---

## 3. Coherence Contracts (binding)

- **C1 — UiKit only.** All UI is built from UiKit tokens/widgets (§6.1). No ad-hoc
  colors, spacing or one-off widgets in feature code.
- **C2 — One style per object kind.** Icons + colors + nouns come from
  `UiKit::StyleFor(ObjectKind)` everywhere: Outliner, Inspector, palette, toasts,
  History, status bar.
- **C3 — Everything selectable is a `SceneItemRef`; every persisted object has a stable
  `ObjectId`** (§7.1). References that outlive a frame resolve by id, never by index.
- **C4 — Every user-visible mutation is one undoable step** (gesture-grained), recorded
  by ObjectId through `core::UndoManager`. Settings are instead resettable (§12).
- **C5 — Every action is a Command** (§6.3) running through `CommandRegistry::run` —
  the single choke-point that later powers macros and the AI agent (§16.4).
- **C6 — Registries, not switches.** Tools, Inspector editors, importers, exporters,
  palette providers and Outliner section content are registered, so new features
  (SLPK phases, Phase-9 GI, next year's tools) plug in without editing existing UI
  files (§16.1 recipe).
- **C7 — Docking freedom.** Every panel docks, floats, resizes and drags out to an OS
  window; layouts persist; `View ▸ Reset Layout` recovers. Stable window titles via
  `Gui::Windows`. The dockable-Viewport + XR-fullscreen duality keeps working.
- **C8 — Nothing asks twice.** Recurring dialogs carry "remember my choice";
  intelligence is suggestion-shaped (action-toast/hint), never a modal interruption,
  and all of it is opt-out.
- **C9 — Layout never breaks.** 800×600 → 4K, GUI scale 0.5–2.0×: overflow collapses,
  text truncates with tooltips, nothing clips.
- **C10 — Capability is sacred.** Never remove an ability the branch has; GL features
  return via ports that land **inside** this structure (Outliner/Inspector/Settings/
  palette) — never as new orphan panels.

---

## 4. The target experience (short narrative)

- **First launch:** shell + empty-state panels; a calm **Welcome Hub** card in the
  viewport: recent scenes with thumbnails, "Import files… (or drop anywhere)",
  primitives row, `Ctrl+K` hint. After a crash: "Restore last session" card.
- **Import:** drop any mix — LAS tiles, GLB, PLY, **SLPK**, HDR — no questions. Tiles
  group themselves under a prefix-derived name; the HDR offers itself as environment;
  unknown files get one warning toast. Scene was empty → framed.
- **Organize:** multi-select, `Ctrl+G` group (inline rename), drag between groups,
  eye/lock per row, right-click: Frame / Isolate / Select Similar / Duplicate /
  Export / Delete — all undoable.
- **Inspect:** selection drives the Inspector — a point cloud shows Display/Info/
  Export; an I3S layer shows its streaming/LOD/daylight controls (today's SlpkPanel
  content); tinted "GLOBAL" cards deep-link into Settings.
- **Time travel:** `Ctrl+Z` is never wrong; the History panel is a labeled timeline
  with click-to-jump; Snapshots capture named states with thumbnails; the app offers a
  safety snapshot before destructive operations.
- **Find anything:** `Ctrl+K` — commands, objects, settings, snapshots, recents,
  ranked by fuzzy match + frecency.
- **Tools are modes:** activate from toolbar/palette/shortcut, options in the
  Inspector's Tool card, output in the Outliner, `Esc` exits.
- **Settings:** one searchable window; per-row/section/category reset; modified-dots.

---

## 5. The App Shell — grow `GuiSystem`

```
┌───────────────────────────────────────────────────────────────────────────┐
│ Menu bar:  File  Edit  Create  Select  View  Tools  Help        [🔍 Ctrl+K]│
├──────────────┬────────────────────────────────────────────┬───────────────┤
│   OUTLINER   │        VIEWPORT (dockable panel(s)         │   INSPECTOR   │
│  search      │        showing the renderer texture;       │  Tool card    │
│  filter chips│        toolbar overlaid top-center)        │  Selection    │
│  tree:       │                                            │  sections     │
│   objects,   │   [Welcome Hub floats here when empty]     │  ──────────── │
│   layers,    │                                            │  Global cards │
│   groups,    │  toasts (bottom-center)                    │  (labeled)    │
│   tool output│                                            │               │
├──────────────┴────────────────────────────────────────────┴───────────────┤
│ Status: 12 objects · 3.2M pts │ Tree_04 │ ⛏ Measure │ 144 fps │ Autosaved │
└──────────────────────────────────────────────────────────────────────────-┘
```

`GuiSystem` already provides the dockspace (passthru central node), menu bar, default
layout + reset, panel hosting, and real dockable Viewport panels (multi-viewport
capable, XR fullscreen fallback). **Pass 0 upgrades it** rather than replacing it:

- Menu bar renders from the **CommandRegistry** (§6.3); panel toggles become commands.
- New thin **status bar** (bottom dock node, tab bar hidden): scene stats, selection
  summary, background progress, active-tool chip, FPS (from `FrameDiagnostics` — the
  Diagnostics panel becomes its detail popup), autosave whisper; far-right slot
  reserved for future presence/sync (§16).
- Default layout: Outliner left / Inspector right / Viewport central; aux panels
  (History, Snapshots, plugins) as tabs beside the Inspector.
- Keyboard note: the master GUI toggle currently sits on `F1`
  (`Application.cpp:897`); the GL app used `G`, and §14 wants `F1` for the shortcut
  overlay — re-map when shortcuts land in P0 (implementer's call, note it).

### 5.1 Every window has a home (disposition map)

Today the branch splits everything into its own window. That was the right *mechanism*
(docking freedom, C7) with an interim *information architecture* — the remake keeps the
mechanism and fixes the architecture: fewer, deeper homes, any panel still poppable
into its own OS window.

| Window today (`Gui::Windows`) | Fate |
|---|---|
| `Viewport` 1..N (own camera each) | **Keep — the centerpiece** (§5.2) |
| `Scene` | Grows into the Outliner (P1) |
| `Inspector` | Grows into the real Inspector (P2) |
| `Settings` | Grows into searchable Settings (P6) |
| `3D Cursor` | Becomes a Settings category (P6) |
| `Point Clouds` | Dissolves: listing → Outliner, per-cloud → Inspector, globals → Settings/GLOBAL cards (P1–2) |
| `Clip Planes` | Dissolves: tool mode + Outliner items + Inspector tool card (planes in Outliner from P1; mode in P7) |
| `Performance` | Becomes the status bar's FPS detail popup (P8); still poppable as a window |
| `Scene Layers` (SLPK) | Dissolves: layers → Outliner, controls → Inspector layer editor, opening → ImportService (P1–2, may trail) |
| About | Stays under Help |
| *New:* History, Snapshots | Default tabs beside the Inspector (P3) |

### 5.2 Multi-viewport rules

Multiple 3D viewports are a first-class feature and must stay one: each Viewport panel
has its **own Camera** (`Application::viewportCamera(i)` / `activeCamera()`); index 0
is the primary and is never closable; extras are added/closed from the View menu
(close deferred to a frame boundary) and may live in dragged-out OS windows
(`ViewportPanelState.hostWindow`). Rules for all passes:

- **Per-viewport UI:** the §14 toolbar renders inside *each* viewport and acts on that
  viewport's camera (view/shading/camera dropdowns, Frame). The Welcome Hub shows in
  the **primary** viewport only.
- **Active viewport** = focused, else last-hovered (`ViewportPanelState`). Keyboard,
  menu and palette camera commands (`view.frame_selected`, standard views…) act on the
  active viewport via `activeCamera()` — never hardcode viewport 0. Pointer-local
  interactions (picking, gizmo, tools, 3D cursor) route to the viewport under the mouse.
- **Global, once (main window):** menu bar, status bar, palette, toasts, hints, dialogs.
- Per-viewport camera pose is session/scene state, not a Setting; Settings hold
  cross-viewport *defaults* (FOV, speed, planes). Snapshots must be explicit about
  multi-viewport capture (per-viewport cameras + active index recommended) — decide in
  P3, stay consistent.
- Viewport window titles stay stable per index (imgui.ini docking identity); respect
  the add/close lifecycle. Selection, tools and undo are **application-global** — one
  selection and one active tool across all viewports.

---

## 6. Foundation systems (Pass 0)

### 6.1 UiKit — `headers/Gui/UiKit.h` + `src/Gui/UiKit.cpp`

Port the proven widget code out of the excluded `GUI.cpp` (`:498-668`: toggle switch,
nav item, section header, panel title, inline icon; fuzzy matcher at `:7267`), then
extend. Contents:

- **Tokens:** spacing scale (2/4/6/8/12/16/24 px @1.0 via a scale helper), radii
  (4 inner / 8 cards), motion standards (§15).
- **`ObjectKind` + `StyleFor(kind)`** → `{icon, themed color, noun}`: Model, Mesh,
  PointCloud, **SceneLayer (I3S)**, Sun, PointLight, SpotLight, Group, Measurement,
  ClipPlane, Snapshot, Environment, Tool, Setting, Command, File (+ reserved:
  BrushCluster, LiveCapture). Append-only; consumers tolerate unknown kinds.
- **Widgets:** the ported five, plus `SearchInput`, `Chip`, `Badge`, `Card` /
  `GlobalCard` ("GLOBAL" tag), `IconButton`, `SegmentedControl`, `EmptyState`,
  `PropertyRow`, `ResetGlyph` (hover ↺ when ≠ default), `Dot` (modified),
  `HintToast` (action buttons), `FuzzyMatch` (+highlight ranges), `Anim01` (§15).

### 6.2 CommandRegistry — `headers/Core/CommandRegistry.h/.cpp`

```cpp
struct Command {
  std::string id;                 // "file.import", "view.frame_selected"
  std::string title, category, keywords;
  const char* icon = nullptr;
  std::function<void()> action;
  std::function<bool()> enabled, checked;   // optional
};
```

Register the existing menu/debug-panel actions; rewire `GuiSystem::drawMenuBar` to
render from the registry. `run(id)` records frecency (§10) and stays the only way
actions execute (C5) — the future macro/AI choke-point.

### 6.3 Shortcuts — rebindable, command-bound

Re-implement shortcuts natively (reference: excluded GL `ShortcutManager` +
`shortcuts.json`): a binding table `shortcut → command id`, editable in Settings ▸
Shortcuts (P6), persisted. Replaces the hardcoded keys in `Application`. Keep the GL
`shortcuts.json` format if practical (C10 spirit), else migrate on first load.

### 6.4 Preferences persistence — `Gui::Settings` ⇄ `preferences.json`

The struct is ready (`headers/Gui/Settings.h` — plain data, grouped, defaults =
shipped behaviour). Add load-on-start / save-on-exit (+ debounced save on change),
missing keys → defaults (guardrail 4). **Grow `Gui::Settings`; never resurrect the GL
`GUI::ApplicationPreferences` blob** (its header says the same). This unblocks: layout
prefs, palette frecency, hints-dismissed set, remembered dialog choices, autosave —
everything later passes persist. Restore `PluginContext::preferences()` on top.

### 6.5 Project discipline

Every new file goes into **both** `StereoVista.vcxproj` and `.vcxproj.filters`; build
both configs when possible (`docs/PLUGINS.md` §9 checklist applies to all GUI files).

---

## 7. Outliner + editable scene (Pass 1) — the heart

Grow `ScenePanel` (`src/Gui/panels/ScenePanel.cpp`) into the real Outliner, window
title `Gui::Windows::Scene`. This pass also makes the scene a first-class document —
the two are one piece of work.

### 7.1 Identity & selection (contract C3)

```cpp
struct SceneItemRef {
  enum class Kind { None, Model, Mesh, PointCloud, SceneLayer, Sun, PointLight,
                    SpotLight, Measurement, ClipPlane, Group, Environment };
  Kind kind = Kind::None;
  uint64_t id = 0;      // persistent ObjectId (authoritative)
  int index = -1, sub = -1;   // cached container index / mesh index
};
class Selection { /* ordered multi-select; primary = last; onChanged callbacks */ };
```

Every persisted object gets a `uint64_t id` from a per-scene monotonic counter,
serialized, regenerated for legacy files at load. References that outlive a frame
(selection, undo records, snapshots, palette results, future collaboration deltas)
resolve by id — this is what makes §9 and §16 tractable. The seed exists: grow the
app-owned `struct Selection { int model; int mesh; }` (`Application.h:335`) into this
multi-select, ObjectId-based one — `ScenePicking`, the gizmo and the Esc-clears-
selection handling (`Application.cpp:888`) move onto it (multi-select transforms apply
to the set; `Esc` exits the active tool first, then clears the selection).

### 7.2 Scene document (port of GL `SceneManager`, done natively)

`scene::Scene` is currently a read-only `office.scene` host. Add: save / save-as /
load / merge / new (replace-or-merge-or-ask flow, remembered — C8), recents, and a
**v3 format**: ids, user groups (`{id, name, visible, locked, parentId}` + `groupId`
on objects), light names, **relative asset paths** (scene-relative primary, absolute
fallback, misses reported), object display names. GL-era v1/v2 `.scene` files load
forever (fresh ids assigned). Scene mutations route through `core::UndoManager` (C4).
SLPK layers participate: an open I3S layer is a scene object (its source path/anchor
serialize; streaming state does not).

### 7.3 The tree

Sections (collapsible roots, count badges, per-kind provider registry — C6):
**Environment** (sun, sky — selectable objects) · **Scene objects** (models→meshes,
point clouds, **I3S layers**, lights; user groups + auto-groups for multi-file
imports) · **Annotations & tool output** (measurements, clip planes; grows per tool).

Row: `[chevron] [kind icon·color] [name] … [badge stack] [👁] [🔒]` — hover-revealed
controls, full-row hit target, ~26 px. Badge stack extensible (streaming %, linked
file; later "live"/"edited by"). Interactions (all undoable, batch = one step):
multi-select (Ctrl/Shift), drag into/out of groups, inline rename (`F2`/double-click),
context menu (Frame `F` · Isolate · Select Similar ▸ type/source/material · Show/Hide
· Lock · Duplicate `Ctrl+D` · Group `Ctrl+G`/Ungroup · Rename · Export… · Delete),
type-filter chips, search matching names + type nouns, files dropped on the panel
import, isolate exit-chip in the status bar.

**`PointCloudPanel` and `SlpkPanel` dissolve** (C10: capability moves, nothing lost):
their *listings* become Outliner rows; their *per-object controls* become Inspector
sections (P2); their *global* toggles become Settings entries / GLOBAL cards. SlpkPanel
is 656 lines — the largest migration; it may trail into P2, but no new features land
in the old panels after P1.

**Acceptance:** everything appears in one tree; multi-select works with gizmo, delete,
duplicate, visibility as batch ops; groups round-trip through v3; `office.scene` (v2)
still loads; empty search states explain themselves.

---

## 8. Inspector (Pass 2) — always the right controls

Grow `InspectorPanel` behind a **per-kind editor registry** (C6). Layout top-to-bottom:
**Header card** (icon, editable name, kind badge, quick actions; "3 objects (2 Models,
1 Light)") → **Tool card** (active tool's options, §13) → **per-kind sections**
(collapsed state persisted) → **Global cards** (tinted, "GLOBAL" tag, deep-link
"Open in Settings →") → **EmptyState** with 3 contextual actions.

Per-kind content: port the GL manipulation panels' *content* (excluded `GUI.cpp:
8095-8850`: transform, material, textures, display, info, export) onto the live
material/mesh systems; point-cloud sections from today's PointCloudPanel; **I3S layer
editor** from SlpkPanel (streaming, LOD budget, daylight/solar, attributes, anchor);
sun/environment editors absorb the debug-panel lighting/sky groups (they edit
`Gui::Settings` sun/sky state).

Rules: multi-edit applies shared widgets to all (one undo gesture — port the GL
`PanelEditTracker` idea onto `core::UndoManager`); mixed kinds → header + relative
Transform; per-section `ResetGlyph` (undoable); texture drag-apply with slot detection
by filename suffix (`*_normal`, `*_rough`, `*_ao`, else albedo) + "[Change slot]"
toast; every numeric row Ctrl+click-to-type.

---

## 9. History & Snapshots (Pass 3) — time travel

**Global undo (C4):** `core::UndoManager` is live but thinly used. Audit every
user-visible mutation and route it through the manager with ObjectId records: scene
ops (P1 lands most), visibility/lock, group ops, renames, measurement + clip-plane
add/delete, import batches (one step), sun/sky edits, snapshot restore. Gesture-
grained (one entry per user intent) — generalize the app's existing gizmo drag-undo
lifecycle (`Application.h:343-347`) into the shared pattern. Settings stay
non-undoable (resettable, §12). `LambdaUndoCommand` already carries `description()`
strings, so History labels come free — write them for humans.

**History panel** (window `"History"`, default tab beside Inspector): icon-coded,
human-labeled timeline ("Moved 3 objects", "Imported survey_north (4 files)"),
relative times, click-to-jump (repeated undo/redo on the existing stack), last-saved
marker, search, depth pref.

**Snapshots:** port the excluded GL `SnapshotManager` + Snapshots window (thumbnails,
tags, fuzzy search, selective restore: camera/scene/tools) natively — it is the
reference the product owner pointed at; keep 100 % of its capability. Integrations:
palette provider, hub thumbnails, one-click "Snapshot now" in the status bar,
**safety snapshots** offered before scene Replace / Reset-All (pref-gated, default
on), restore = one undoable step. Leave visual room per card for a future A/B compare.

---

## 10. Command palette & global search (Pass 4)

`Ctrl+K` (+ menu-bar icon), centered overlay, dimmed scrim, fully keyboard-driven.
**Providers are a registry** (C6): commands (live shortcut + enabled state), scene
objects (by ObjectId; Enter select, `Shift+Enter` frame), settings index (deep-link +
flash-highlight, P6), snapshots (thumbnail rows), recent scenes, help. Ranking =
`FuzzyMatch` score blended with **frecency** (decayed counters persisted via P0
prefs). Empty query = top-frecency + selection actions + a tip. Prefix filters: `>`
commands, `#` objects, `:` settings. Local search fields everywhere reuse
`SearchInput` + `FuzzyMatch` so search feels identical across the app.

---

## 11. Smart import, Welcome Hub, autosave (Pass 5)

**ImportService** (`headers/Core/ImportService.h/.cpp`) — one entry for menu
(`File ▸ Import Files…`, `Ctrl+I`, single combined filter), drop (window + Outliner),
hub and palette. `Plan(paths) → [{action, path, reason}]` (pure) then `Execute`:

- Detection: extension table (models `.obj/.fbx/.3ds/.gltf/.glb`; clouds
  `.las/.laz/.ply/.xyz/.txt/.pcb/.h5/.hdf5` with LAS-tile grouping; **`.slpk`** →
  I3S layer; `.scene`; `.hdr/.exr` → environment offer; images → texture),
  content-sniffing for ambiguous text. Unknown → one warning toast, never a modal.
- Duplicate awareness ("already loaded — [Add copy] [Skip]", batch-aware).
- Multi-file imports land as one auto-named group (longest common prefix, else folder).
- Display names prettified (strip extension, `_`/`-` → space); source path in Info.
- Scene files: replace/merge/ask with "remember my choice" + safety-snapshot offer.
- After import: select; frame-all if scene was empty; aggregate toast.

**Welcome Hub** (replaces the empty-scene gap): non-modal centered card when the scene
is empty — recent scenes as thumbnail cards (snapshot thumbnails / scene-info counts),
Import button + drop hint, primitives row, shortcut footer. Leads with a
**Restore-last-session card** when a stale `session.lock` + autosave exist.

**Autosave:** timer-based (default 5 min, pref-able) to `autosave/` (scenes store
references — cheap; skip while streaming); `session.lock` lifecycle; status-bar
whisper "Autosaved · 14:02".

---

## 12. Settings overhaul (Pass 6)

Grow `SettingsPanel` into the searchable, resettable Settings window (sidebar
categories like the GL reference, content from `Gui::Settings`):

- **Defaults framework:** `static const Gui::Settings kDefaults{}` — the struct's
  initializers are the defaults. `IsModified`, per-row `ResetGlyph`, per-section reset,
  per-category footer, "Reset All…" (confirm + safety snapshot). Resets re-apply side
  effects through the existing Services setters.
- **Search:** rows self-register `{label, category, keywords}` into the settings index
  (shared with the palette); live filtering with match highlighting; matching
  categories glow in the nav.
- **Modified dots** on rows differing from defaults.
- **Re-grouping (C10):** CursorPanel content → Settings ▸ 3D Cursor (embed
  `CursorPreview3D` when it gets ported — TODO §C); camera/stereo/mouse groups from
  the current SettingsPanel stay but restyled; a **placeholder-free** structure that
  has labeled room for Phase-9 GI/post-FX settings when they return (advanced blocks,
  not new windows).
- Theme picker upgraded with the existing swatch API into theme cards.
- Settings ▸ Shortcuts hosts the P0 binding editor.

---

## 13. Tools, export & plugins (Pass 7)

**ToolManager** — a **registry** (C6): tools self-describe `{id, name, icon,
shortcutAction, category}`; at most one active; activate/deactivate are commands;
`Esc` exits. Viewport-toolbar segment, Tools menu, palette and status chip all render
from it — future tools (brush port, point-cloud crop/clean, SLPK editing phases)
appear everywhere with zero UI edits.

- Measurement (plugin) and ClipPlaneTool become modes: options in the Inspector Tool
  card (new optional plugin hook `onRenderInspector(PluginContext&)`), output in the
  Outliner; the ClipPlanes panel dissolves into this (C10: same controls, better home).
- **ExportService** mirrors ImportService: exporters registered per kind/format.
  Initially: screenshots (exists; stereo screenshot modes are a TODO §E follow-up —
  register them here when done), point-cloud export (port from GL reference), scene
  save-as. Surfaces: `File ▸ Export…` (selection-aware), Outliner context Export
  (batch), Inspector Export sections.
- **Plugins:** additive `PluginContext` extensions — `registerCommand`,
  `registerTool`, `onRenderInspector`, kind-styled toasts (+`preferences()` restored
  in P0). Update `docs/PLUGINS.md` + `CrosshairPlugin`. Existing plugin API keeps
  working unchanged.

---

## 14. Viewport toolbar, status bar, hints (Pass 8)

- **Viewport toolbar** overlaid top-center of *each* Viewport panel, acting on that
  viewport's own camera (§5.2): gizmo segmented control | view ▾ (standard views,
  Frame `F`, reset) | shading ▾ | camera ▾ | tools segment (ToolManager) |
  layout/hide-UI. Width-aware overflow into `⋯` (C9).
- **Status bar completes:** stats, selection summary, streaming/import progress
  (per-file detail on hover), tool chip, stereo-mode chip, FPS (click → Performance
  panel), autosave whisper.
- **SceneStats service:** cheap per-second object/point/triangle/VRAM stats → status
  bar popup, hub cards, hints.
- **Shortcut overlay:** hold `F1` → translucent live-generated keymap cheat sheet
  (`Shift+F1` pins) — pairs with moving the GUI toggle off `F1` (§5).
- **HintEngine:** context-triggered, rate-limited, dismiss-forever, behind a master
  pref. Launch set: frame-selected tip after first import; performance guardian (FPS
  low + expensive setting on → "[Lower it] [Keep]", never auto-changes); grouping tip
  at 5+ ungrouped objects; palette tip after 3× Settings opens; "measurements live in
  the Outliner" after first measurement.

---

## 15. Polish, motion & quality-of-life (every pass + audit in Pass 9)

The difference between "works" and "feels crafted". Binding spirit, free execution.

**Motion standards** (in UiKit; the app redraws every frame, so animation is cheap —
keep the math trivial): 120–150 ms ease-out micro (hover fills, chevrons, eye/lock
fade, toggle knob); 200–250 ms panel-level (palette open, hub fade, toast slide);
small overshoot only on palette/toast entrance. Never animate data values or anything
during a drag. `Anim01(id, target)` is the only mechanism. A `reduceMotion` setting
disables all of it.

**Micro-delight (tasteful, not gamey):** import completion ticks with a count-up
toast; save pulses the title-bar dirty-dot away; History jumps flash affected rows;
palette Enter gives a 1-frame accent flash; grouping "swallows" rows with a 150 ms
collapse. No points, badges or streaks — this is a pro instrument.

**QoL catalog** (sprinkle through passes, audit in P9): right-click any slider/field →
Reset/Copy/Paste; hovering an Outliner row subtly highlights the object in the
viewport (selection-outline path), double-click = frame; unsaved-changes dot in the
title bar; toast `[Undo]` on destructive actions; truncated text always tooltips;
tooltips show live shortcuts; file dialogs remember last folder per context; scroll/
expand/filter state persists per panel; `Enter`/`Esc` consistent in dialogs, first
field focused; numeric fields with unit suffixes and `Alt`-drag fine scrub; copy
buttons on Info rows; drag-and-drop wherever plausible (files → anywhere, texture →
model, objects → group, snapshot card → viewport to restore).

---

## 16. Built for what's coming (provisions, not features)

Roadmap: more editing/measurement/export tools; SLPK phases S2–S6; Phase-9 GI;
**macros**; **AI features** (a global agent that can modify the scene/settings and
create macros); cooperative editing; a web viewer; live viewport-capture sources.
Build none of it now — never design its rails away:

1. **More tools — the recipe** (verify it stays true every pass): register a tool
   (§13) + commands (§6.2); if it produces objects: new ObjectKind + Outliner provider
   + Inspector editor + undo records + v3 serialization; if it moves files: importer/
   exporter registration. A recipe-following tool touches zero existing UI files.
2. **Phase-9 GI / post-FX return:** Settings categories and Inspector global cards
   absorb them as advanced blocks; lighting-mode selection returns as a shading-
   dropdown extension — no new windows.
3. **Cooperative editing:** needs ObjectIds (C3), the command/undo choke-points
   (C4/C5 — an operation-log shape already), portable v3 scenes (§7.2), reserved UI
   slots (status-bar presence segment, Outliner badge stack, conflict-toast pattern).
4. **Macros & AI agent:** both are drivers of the surfaces users use — commands (C5;
   later parameterized variants), the settings index (§12), ImportService/
   ExportService, Selection. A macro is a recorded command sequence; the AI agent
   produces commands/macros with the same preview/undo safety (C4 makes agent actions
   safe by construction). Keep these surfaces complete and scriptable-shaped.
5. **Web viewer:** v3 stays plain, self-describing JSON with relative paths; snapshot
   thumbnails double as gallery assets; `Export ▸ Web package` is one future exporter.
6. **Live capture sources:** reserved `ObjectKind::LiveCapture` + "live" badge +
   ImportPlan action; Outliner/Inspector never assume objects are static.

---

## 17. Engineering guardrails (binding)

1. **MSVC/VS2022 only; agents usually can't compile.** Small reviewable commits; port
   code wholesale before restyling; additive files over surgery in `Application.cpp`;
   C++17 as MSVC accepts it; every new file in `.vcxproj` **and** `.filters`; build
   Release *and* Debug x64 when a build is possible; after adding shaders confirm the
   `.spv` lands (MSBuild may not fail on glslc errors).
2. **The excluded GL sources are read-only references.** Port *content* out, then
   (eventually) delete — **never** flip `ExcludedFromBuild` back. "Rewrite better,
   don't stale-translate" (branch CLAUDE.md): fix GL bugs during the port instead of
   copying them. Don't resurrect the GL `ApplicationPreferences`/dead GL fields.
3. **Never break:** single-pass multiview stereo (GUI drawn once per frame — no
   per-eye GUI paths); the dockable-Viewport ⇄ XR-fullscreen duality; the
   multi-viewport contract (§5.2: per-viewport cameras, active-viewport command
   routing, primary never closable, stable titles); multi-viewport
   OS drag-out (+ the ImGui-Vulkan `pColorAttachmentFormats` lifetime trap — long-
   lived `Application` member); GUI-hidden mode; GUI scale 0.5–2.0×; all 7 themes
   (light *and* dark); `.scene` v1/v2 loading forever; plugin API (additive only);
   the Services layering (panels never include Vulkan/renderer headers — plain data
   through `Gui::Services`).
4. **Identity & undo discipline:** ObjectIds on every persisted type; frame-outliving
   references resolve by id; one undoable step per user intent.
5. **Persistence:** new UI state goes through the P0 `Gui::Settings` persistence with
   behavior-preserving defaults for missing keys.
6. **Docs lag code — read the source first.** After each pass, update the Status
   Board; keep CLAUDE.md's GUI notes truthful when your pass changes the picture.
7. **Manual test checklist** (on Windows when possible; otherwise say so in the PR):
   clean launch (no prefs) + legacy-file launch; import every format incl. `.slpk` +
   drop-import; dock/undock/float/drag-out every panel incl. extra Viewport panels;
   reset layout; 800×600 and 4K; GUI toggle; undo/redo session incl. History jump;
   `office.scene` (v2) load + v3 round-trip; autosave recovery; stereo (quad-buffer or
   SBS) + XR smoke test if hardware allows.
8. **Copy:** sentence case, verbs on buttons, tooltips explain *why*, units on every
   number, no jargon at first level.

---

## 18. Pass Plan

Foundation → heart → time-travel → intelligence → polish. Passes 4–7 parallelize
after Pass 2. Sizes: S < 300, M < 1200, L < 3000 LoC touched.

| # | Pass | Spec | Size | Needs |
|---|---|---|---|---|
| 0 | Foundation: UiKit, CommandRegistry + menu rewire, shortcuts, `Gui::Settings` persistence, status-bar shell | §5–6 | L | — |
| 1 | Outliner + editable scene: ObjectIds, Selection, scene save/load/merge + v3, groups, multi-select, dissolve PointCloud/Slpk listings | §7 | L | 0 |
| 2 | Inspector: editor registry, per-kind editors (incl. I3S layer), multi-edit, global cards, resets | §8 | L | 1 |
| 3 | History & Snapshots: undo audit, History panel, SnapshotManager port + integration | §9 | M | 1 |
| 4 | Command palette: providers, fuzzy + frecency, prefix filters | §10 | M | 0 (best after 1+3) |
| 5 | Import + Hub + autosave: ImportService (dupes/auto-group/pretty names), Welcome Hub, recovery | §11 | M | 1 |
| 6 | Settings: search index, defaults framework, resets + dots, re-grouping, shortcuts editor | §12 | L | 0 |
| 7 | Tools & export: ToolManager registry, tool cards, ExportService, plugin hooks | §13 | M | 2 |
| 8 | Viewport toolbar & alive: toolbar, status bar completion, SceneStats, F1 overlay, HintEngine | §14 | M | 0; 7 for tool chip |
| 9 | Harmony audit: §15 polish/QoL sweep, spacing/icon/copy audit, recipe verification (mock tool through §16.1), perf, docs, dead-code + delete fully-ported GL references | §15 | M | all |

Every pass PR states: deviations from this plan (and why), manual-test status, Status
Board update.

---

## 19. Status Board

Keep this truthful — it is the coordination point between passes.

| Pass | State | Branch/PR | Notes & deviations |
|---|---|---|---|
| Plan | ✅ done | `StereoVista-vulkan` | Rev 4.1: adapted to the Vulkan branch (GuiSystem/Services/panels reality; scene-system port folded into P1; snapshots/shortcuts/prefs ports slotted; supersedes TODO §D) + verified against source; §5.1 window disposition map and §5.2 multi-viewport rules (per-viewport cameras, active-viewport routing) made explicit. |
| 0 Foundation | ✅ done (unverified on Windows — agent could not build MSVC; g++ syntax-checked) | `claude/stereo-vista-gui-pass-0-k2qhfp` | UiKit (tokens, ObjectKind styles, GL widget ports + redesign set, scored `FuzzyMatch`, `Anim01` + `reduceMotion`); CommandRegistry + frecency; menus render registry commands (File/Edit/Help fully generic, View interleaves the dynamic theme/viewport blocks — every item still runs through `run()`); rebindable shortcuts in `core::ShortcutMap` (v2 shortcuts.json; GL v1 profile files migrate on load); `Gui::Settings` ⇄ preferences.json (v3; GL file's overlapping subset migrates once) + debounced autosave; `PluginContext::preferences()` restored (returns `Gui::Settings&`, not the GL blob); status-bar shell (stats/selection/activity/FPS→Performance). **Deviations:** status bar is viewport side-bar *chrome* (BeginViewportSideBar), not a bottom dock node — always at the bottom, can't be torn off, no imgui.ini/reset-layout interference; GUI toggle moved F1→G (GL parity key, F1 freed for the §14 overlay); `edit.delete_selected` ships unbound (GL used Delete, which plugins own here for measurement-cancel); panel visibility moved into `Settings::Ui::Panels` so it persists; a minimal Settings ▸ Interface tab (theme/scale/status bar/reduce motion) landed early to expose the new prefs (P6 restyles it); imgui_sytle.h no longer includes the Vulkan/GLFW backends (moved into imgui_style.cpp) so the Gui layer can use the style API without Vulkan. **Follow-up:** the live cursor system was decoupled from the GL `GuiTypes.h` (its three still-used types moved to the Vulkan-native `Cursors/CursorTypes.h`) — **zero compiled code includes a GL-era reference header now**; `GuiTypes.h` carries a do-not-include banner and dies with the remaining GL ports. |
| 1 Outliner + scene | ✅ done (in tree; unverified on Windows) | `StereoVista-vulkan` | Stable `uint64_t` ObjectIds (`scene::Scene::allocateId`/`ensureIds`); `scene::SceneItemRef`/`Selection` (ordered multi-select, `Scene/SceneItems.h`); `SceneDocument` v1/v2/v3 load + v3 save/merge (`Scene/SceneDocument.h`); user groups; the Outliner tree (`ScenePanel.cpp` + `Gui/Outliner.h` provider registry); item-op surface on `Gui::Services` (delete/duplicate/visible/locked/group/rename/frame/isolate, one undo each). Point clouds live on `scene::Scene::pointClouds`. |
| 2 Inspector | ✅ done (unverified on Windows — agent could not build MSVC; g++ `-std=c++17 -fsyntax-only` clean on every changed file) | `StereoVista-vulkan` | Per-kind **editor registry** (`Gui/Inspector.h` + `src/Gui/Inspector.cpp`) mirroring the Pass-1 Outliner provider registry (one editor per kind, last-wins so a plugin can override in P7). Shell (`panels/InspectorPanel.cpp`): header card (kind icon, editable name via `renameItem`, kind badge, mixed "N objects (…)" summary, Frame/Isolate/Duplicate/Hide/Delete quick actions), tool-card **stub** (awaits P7 ToolManager + `onRenderInspector`), per-kind dispatch, GLOBAL cards (tinted, "Open in Settings →" deep-link), `UiKit::EmptyState` + 3 contextual actions. Editors ported from the GL manipulation panels (`GUI.cpp:8086-8850`) onto the **live Vulkan material/mesh/scene systems** (content only): Model/Mesh (transform, material, textures, info), PointCloud (transform, point size, info, export), PointLight, Sun, Environment/Sky, and the **I3S SceneLayer** editor (`src/Gui/inspectors/LayerInspector.cpp`: streaming/LOD budgets, point colourization + palette, daylight/solar → app sun, section slice, OBB overlay, stats HUD). **Multi-edit** = `EditRow<T>` applies a shared widget to the whole selection and records ONE undo per gesture; gesture detected via panel-wide `IsAnyItemActive()` (the GL `PanelEditTracker` approach — `DragFloatN`'s per-item activation flags are unreliable) with per-object before-values re-resolved by ObjectId. Per-section `ResetGlyph` (`SectionReset<Snap>`, undoable). Collapse state persisted in `Settings::Ui::Inspector`. **Deviations:** (1) settings-bound editors (Sun/Sky, PC quality) are non-undoable + resettable (C4) — not on the undo stack; (2) **texture drag-apply** with filename-suffix slot detection is NEW (GL had explicit per-slot buttons only) — load routes through new `Services::pickTextureFile`/`applyMaterialTexture` (Vulkan upload in the app layer); reassigning a slot does not free the previous bindless texture (minor slot leak until scene reload — TODO for a P7/P9 sweep); (3) per-mesh material uses the model's shared bindless materials (no separate per-mesh PBR fields exist on the Vulkan mesh — the GL panel's per-mesh sliders were dead stubs); (4) point-cloud export routes through new `Services::exportPointCloud` → `PointCloudLoader` (the GL 4-format export); a full `ExportService` is still P7; (5) **SlpkPanel is kept** (package-open entry + global pump budgets + the exhaustive read-only Resources/Attributes/Nodes diagnostics) — its interactive per-layer editing moved to the Inspector, full dissolution trails (allowed by §7.3). New Services methods added: `pickTextureFile`, `applyMaterialTexture`, `exportPointCloud`. |
| 3 History & Snapshots | ✅ done (MSVC Release x64 compiles clean) | `StereoVista-vulkan` | **History panel** (`panels/HistoryPanel.cpp`, window `Windows::History`): the undo stack as a labeled, clickable timeline — past edits above a current-state marker, undone edits below (dimmed), click-to-jump replays `undo()`/`redo()` to that point, fuzzy search filter, last-saved divider. `core::UndoManager` grew timeline introspection (`undoDescriptions`/`redoDescriptions`/`markSaved`/`savedDepth`); `Application::saveScene` calls `markSaved()`. Labels come free from `LambdaUndoCommand::description()` — Pass 1's item ops and Pass 2's `EditRow` edits already write human strings ("Edit roughness", "Move objects"). **Snapshots** (`panels/SnapshotsPanel.cpp` + `src/App/Snapshots.cpp`, window `Windows::Snapshots`): named checkpoints of camera and/or scene — capture row (name + aspect toggles), fuzzy search over names *and* tags, per-snapshot cards with inline rename + comma-separated tag editing, **selective restore** (all / camera-only / scene-only), delete. Command `edit.snapshot_now`; both panels dock as tabs beside the Inspector; **safety snapshot** auto-captured before a destructive scene Replace (pref `files.safetySnapshotBeforeReplace`, default on, hooked into `openSceneFile`/`resolvePendingSceneOpen` — deliberately NOT inside `replaceSceneFromFile`, which snapshot-restore itself calls). **Improved on the GL original (not a stale port):** the GL `SnapshotManager` restored scene state *by index* and by its own admission "does not re-create objects that were deleted after the snapshot was taken" — a real capability bug. The native design instead serializes the whole scene through `scene::SceneDocument` (v3) to a `snapshots/` file and restores by **reload**, so deleted objects come back, ids/groups round-trip, and nothing resolves by index (C3). This is also forced by the architecture: a `Model`'s `MeshBuffer` is a move-only GPU resource, so an in-memory scene copy is impossible. **Deviations:** thumbnails and the per-snapshot color marker are deferred (they need renderer readback→ImGui-texture infra that doesn't exist yet — cards show aspect badges + tags + timestamp instead, and the card layout leaves room); snapshots are **session-scoped** (the `snapshots/` scene files persist, the metadata list does not) — cross-restart persistence rides along with autosave in Pass 5; History rows carry no relative timestamps (undo entries aren't timestamped — snapshots are); sun/sky edits stay non-undoable/resettable (C4 treats `Gui::Settings` as resettable) rather than being pushed onto the undo stack as §9 suggested. |
| 4 Palette | ✅ done (MSVC Release x64 builds + links clean) | `StereoVista-vulkan` | `Ctrl+K` command palette (`Gui/Palette.h` + `src/Gui/Palette.cpp`): centered overlay, dimmed scrim (its own window so the focused palette draws above it; click-away closes), fully keyboard-driven (↑↓ wrap-navigate, Enter run, **Shift+Enter** = alternate action / frame the object, Esc close). Content comes from a **provider registry** (C6) — `Palette::registerProvider` — with built-ins for commands, scene objects, snapshots and recent scenes; the Pass-6 settings index registers into the reserved `Source::Setting` slot. Ranking = `UiKit::FuzzyMatch` score blended with **command frecency** (already recorded per `CommandRegistry::run()` and persisted since Pass 0), with matched characters highlighted via `TextFuzzyHighlighted`. Prefix scoping: `>` commands, `#` objects, `:` settings. Command rows show their **live shortcut label** (`ShortcutMap::label`) and grey out when disabled; every command executes through `CommandRegistry::run` (C5) — the palette never calls an action directly. New command `palette.open` (default `Ctrl+K`, in the View menu); `GuiSystem::openPalette()` owns the open flag and the overlay draws last so the scrim dims every panel. **Nice reuse:** the object provider feeds off the Pass-1 **Outliner** provider registry (`Outliner::collect`), so any future tool that adds Outliner rows becomes palette-searchable for free — the §16.1 recipe compounding as intended. **Deviations:** the settings provider is a reserved seam (needs Pass 6's index); results cap at 60 rows; no "empty query = selection actions + tip" block yet (empty query ranks purely by frecency, which is the useful default). |
| 5 Import + Hub + autosave | ✅ done (MSVC Release x64 builds + links clean) | `StereoVista-vulkan` | **ImportService** (`Core/ImportService.h` + `src/Core/ImportService.cpp`): the PURE half — `planImport(paths) → ImportPlan` classifies every path (no Vulkan, no scene), plus `prettyName` and `groupNameFor` (longest-common-stem-prefix, folder fallback). The executor is `Application::importFiles` (`src/App/ImportOps.cpp`), the **one entry point** the File menu, drag-drop, the Welcome Hub and the palette all funnel into: skips duplicates, runs the existing loaders (LAS tiles still go to `beginLoadLASMultipleProgressive` in one call so they share a centre), then does what the old scattered paths never did — pretty display names, ONE auto-named group per multi-file batch (`groupItems`), selects the result, and frames it when the scene was empty. Unknown files raise one warning toast, never a modal (C8). **Bug fixed in the port:** the old `handleDroppedFiles` hard-wired `.ply` to point clouds, so dropping a PLY *mesh* silently became a cloud of its vertices — `planImport` now **sniffs the header** (`element face` > 0 ⇒ mesh) and sniffs `.txt` (≥3 floats on the first data line ⇒ XYZ cloud, else reported unknown instead of being fed to the cloud parser). `handleDroppedFiles` is now a 3-line forwarder. **Welcome Hub** (`panels/WelcomeHub.cpp`): non-modal card centered over the PRIMARY viewport while the scene is empty — import button, primitives row, recent scenes, `Ctrl+K` footer, "don't show again"; drawn as its own window *after* the viewport panel's `End()` so its buttons work and it never disturbs the image's hover/pick reporting. It **leads with a "Restore last session" card** after an unclean shutdown. New `create.cube/sphere/cylinder/plane/torus` commands + a registry-rendered **Create menu** — the Hub row and the palette run the *same* commands (C5), no second path. **Autosave + recovery:** rotating slots into `autosave/` (`files.autosaveEnabled/autosaveMinutes/autosaveSlots`, persisted), skipped while any cloud streams or an SLPK parse is in flight (never serialize a half-loaded scene); `session.lock` written at `init()` and removed at `shutdown()` — a crash leaves it behind, and that *is* the staleness signal (no pid liveness games). The lock stores the newest autosave path + stamp, so recovery needs no `file_time_type` arithmetic. Status-bar **autosave whisper**. Also **de-duplicated** the `SceneSaveState` fill that had been pasted into `saveScene` and `Snapshots.cpp` — both now call `Application::currentSaveState()`. **Deviations:** `.hdr/.exr` environments and dropped images are recognized and explained by a toast but not yet ingested (no sky/texture import path exists — texture apply already works from the Inspector); imports are not undoable yet (§9 wanted "import batches = one step") — deleting an import IS undoable, so nothing is lost, but the batch itself isn't on the stack; snapshot metadata still doesn't persist across restarts (the `snapshots/` scene files do). |
| 6 Settings | ✅ done (MSVC Release x64 builds + links clean) | `StereoVista-vulkan` | `SettingsPanel` rewritten from a 7-tab bar into the real thing: **sidebar categories** (`UiKit::NavItem` — Interface · Camera & navigation · 3D Cursor · Stereo · VR/OpenXR · Rendering · Environment · Point clouds · Files & autosave · Shortcuts) + a **global search** that filters rows across *every* category (hit-count badges in the nav; categories with no hit are skipped entirely so no dangling headers). **Defaults framework:** `static const Settings kDefaults{}` is the single source — every plain row carries a modified `Dot` + a per-row `ResetGlyph`, each category has a reset footer, and **"Reset all…"** confirms and takes a **safety snapshot** first (Pass 3). Rows whose value lives *outside* `Gui::Settings` (theme, GUI scale, tonemap, present mode, stereo mode, VR) are read and reset through their **Services setters**, because a reset must re-apply the side effect, not just poke a field. **Searchable settings index** (`Gui/SettingsIndex.h`) is shared with the palette — this fills the `Source::Setting` seam Pass 4 reserved, so `:` in `Ctrl+K` now searches settings and **deep-links** into the row (opens Settings → selects the category → flash-highlights it). **New surfaces (C10 — capability *gained*):** `pointCloud.*` and `render.asyncCompute` were previously editable only from the Point Clouds / Diagnostics panels and are now proper settings categories; the **shortcut editor** finally exists (per-command 2 slots, live capture with conflict-stealing + toast, per-row reset-to-default, global reset) — it needed two small additions: `ShortcutMap::defaults(id)` and `Services::capturePressedKey()` (the app owns the ImGui↔GLFW key translation, so the Gui layer still never includes GLFW; the reverse map is derived by *searching* the existing forward map, so there is no second table to drift). **3D Cursor re-grouped without duplication:** `CursorPanel`'s content was extracted into `drawCursorSettings(Services&)`, and BOTH the standalone 3D Cursor window and the Settings category render that one function. **Root-cause fix (not a workaround):** `renderer::SunState::enabled` defaults to `false`, but `Application` turned the sun on for a fresh profile — so the struct default was never the *shipped* default, and a literal `kDefaults` reset would have switched the sun **off**. The shipped truth now lives in `Gui::detail::defaultSun()` and the app's special case is gone, so `kDefaults` is honest. **Deviations:** `Cursor::CursorManager` appearance state is still neither in `Gui::Settings` nor persisted (a pre-existing gap — CLAUDE.md's "cursor-preset persistence not yet ported"), so the 3D Cursor category has no reset/dots for those rows; tonemap/present-mode reset targets are hard-coded (they have no `Settings` home yet); the settings index is a curated table of the main rows rather than auto-derived from the draw code (drift is possible — a Pass-9 audit item). |
| 7 Tools & export | ⬜ not started | | |
| 8 Viewport & alive | ⬜ not started | | |
| 9 Harmony | ⬜ not started | | |

### Decisions log

- 2026-07-12 — Plan authored against `main` (GL), then adapted to the active
  `StereoVista-vulkan` branch: `GuiSystem` + `Gui::Services` + `panels/` are the shell
  seed (dockable render-to-texture Viewport panels kept); the GL GUI (`GUI.cpp`,
  excluded from build) is the behaviour/widget reference only.
- 2026-07-12 — CommandRegistry is the coherence mechanism (menus/palette/shortcuts/
  toolbars; later macros + AI agent drive the same choke-point).
- 2026-07-12 — Stable ObjectIds from Pass 1 (prerequisite for global undo, snapshots,
  collaboration, web viewer). Scene-system port (save/load/merge, v3, groups) folded
  into Pass 1 — the Outliner needs an editable scene document.
- 2026-07-12 — History & Snapshots promoted to Pass 3; the excluded GL
  `SnapshotManager` is the reference — no capability regressions.
- 2026-07-12 — `PointCloudPanel` + `SlpkPanel` dissolve into Outliner/Inspector/
  Settings (C10: capability moves, nothing lost).
- 2026-07-12 — Registries everywhere (tools/editors/importers/exporters/providers)
  with a documented recipe; ExportService mirrors ImportService.
- 2026-07-12 — Only §3 contracts, §17 guardrails and pass acceptance are binding;
  everything else is a strong default implementers may improve on. Motion/QoL layer
  (§15, incl. `reduceMotion`) and provisions for Phase-9 GI, SLPK phases, macros/AI,
  collaboration, web viewer, live captures (§16).
- 2026-07-12 — Rev 4.1 after source verification: multi-viewport is first-class and
  stays (per-viewport cameras via `viewportCamera(i)`, active-viewport command
  routing, primary never closable — §5.2); every current window's fate is explicit
  (§5.1); Selection grows from the existing `Application::Selection` seed; History
  builds on `LambdaUndoCommand::description()` and the gizmo drag-undo pattern.
- 2026-07-12 — Pass 0 landed (see Status Board row for the deviation list).
  Notes for later passes: `Application::registerCommands()` is the one place
  commands + menu grouping + default bindings live; new persisted state goes
  through `Gui::Preferences` (one serialization line per field, missing keys
  keep defaults); frecency is already recorded per `run()` and persisted for
  the Pass-4 palette; the shortcuts editor (P6) edits `core::ShortcutMap`
  (`setBinding`/`findConflict`/`resetToDefaults` are ready);
  `ShortcutMap::normalizeKeyToLayout` must be applied to captured keys.
- 2026-07-12 — Pass 2 landed (Inspector). The **editor registry**
  (`Gui/Inspector.h` `registerEditor(kind, editor)` / `editorFor(kind)`) is the
  recipe C6 point for future kinds — a new tool that produces objects registers
  an editor and its Inspector content appears with zero edits to
  `InspectorPanel.cpp`. Notes for later passes: **multi-edit + single-undo** is
  `Inspector::EditRow<T>` (live-apply to the whole selection, capture pre-drag
  by ObjectId, commit one `core::UndoManager` entry once `IsAnyItemActive()`
  goes false — the reusable pattern for any future editable property);
  `Inspector::SectionReset<Snap>` is the undoable per-section reset. **P3
  (History)** — the Inspector's property edits already record human-labeled undo
  entries ("Edit roughness", "Move objects", …); audit sun/sky (currently
  non-undoable settings) + snapshot restore. **P6 (Settings)** — the GLOBAL
  cards' "Open in Settings →" deep-links (`openInSettings` reveals + focuses the
  Settings window) want the real flash-highlight + settings index; the
  point-cloud quality + lighting globals shown as GLOBAL cards should share the
  Settings rows. **P7 (Tools/export)** — the Inspector **tool-card stub** in
  `drawInspectorPanel` is where `onRenderInspector(PluginContext&)` and the
  ToolManager's active-tool options render; `Services::exportPointCloud` is the
  seed the `ExportService` generalizes; SlpkPanel finishes dissolving once its
  read-only diagnostics find a home. The texture-slot leak on reassign
  (`applyMaterialTexture` overwrites the index without `freeTexture`) is a P9
  cleanup.
