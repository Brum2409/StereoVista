# StereoVista UI/UX Redesign — Master Plan

**Living document** for the full GUI remake, executed in agent passes
([§18 Pass Plan](#18-pass-plan)); each pass updates the [Status Board](#19-status-board).

> **How to read this as an implementing agent — the only rule that matters:**
> Only three things are binding: the **Coherence Contracts (§3)**, the **Engineering
> Guardrails (§17)**, and your pass's **acceptance criteria**. Everything else in this
> document is a *strong default* — you are expected to make most detailed decisions
> yourself and to replace any suggestion with something better without asking, as long
> as the big picture stays intact. Note deviations in the Status Board so later passes
> stay coherent. Never end a pass with a broken or half-migrated app.

---

## 1. North Star

StereoVista should feel like **one intelligent, polished instrument**, not a collection
of windows. A first-time user is productive in a minute because the layout matches what
every modern 3D tool taught them and the program figures out intent on its own. A power
user loses nothing: every capability stays, organized and searchable instead of removed.

### Principles

1. **Familiar by default** — Outliner left, Inspector right, viewport center, status bar
   bottom, `Ctrl+K` palette. Innovate on friction, not on layout conventions.
2. **One heart** — *everything* loaded or created lives in the Outliner. No orphan lists.
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
   *by registration, not redesign* (§16).
10. **Polished to the pixel** — micro-interactions, subtle motion and quality-of-life
    details everywhere (§15). Delight without distraction.

---

## 2. Where we are today (code inventory)

The GUI is already *partially* modernized — reuse this, don't rebuild it:

| Asset | Where | Disposition |
|---|---|---|
| Theme system: 7 themes, semantic palette `g_StyleColors`, swatch API | `imgui_sytle.h` / `imgui_style.cpp` | Keep & extend |
| Fonts (regular/bold/header/small/mono + FontAwesome 5), DPI + user scale 0.5–2.0× | `imgui_style.cpp`, `IconsFontAwesome5.h` | Keep |
| Custom widgets: toggle switch, nav item, section header, panel title, inline icon, menu-bar separator, tag chips | `src/Gui/GUI.cpp:498-668`, `7339` | Extract into UiKit (P0) |
| Toasts (`GUI::ShowToast`) | `GUI.cpp` | Keep |
| Overlays: perf, point-cloud streaming, empty-scene hint, measurement labels, view-mode + gizmo toolbars | `GUI.cpp:203-420`, `894-1224`, `7968` | Restyle/unify (P8) |
| Settings window: sidebar nav, 7 categories | `GUI.cpp:3155-6193` | Overhaul (P6) |
| Scene Hierarchy: fixed left panel + inline Properties child | `GUI.cpp:2426-3097` | Replace with Outliner + Inspector (P1–2) |
| Per-type manipulation panels (model/mesh/cloud/sun/lights/cluster) | `GUI.cpp:8095-8850` | Become Inspector editors (P2) |
| Undo: gesture-grained `PanelEditTracker`, `UndoManager`, Edit-menu history | `GUI.cpp:460-493`, `2035`, `Core/UndoManager` | Grows into global History (P3) |
| Snapshots: named states w/ thumbnails, tags, fuzzy search, restore flags (from the original GL build) | `Core/SnapshotManager`, `GUI.cpp:7252-7965` | First-class; restyle, never regress (P3) |
| Plugin system (`PluginContext`, `REGISTER_PLUGIN`) | `headers/Plugins/*`, `docs/PLUGINS.md` | Keep, extend (P7) |
| Rebindable shortcuts + editor | `Engine/ShortcutManager.h` | Keep; feed from CommandRegistry |
| Scene persistence v2 (metadata, environment, save options, load report, recents) | `Core/SceneManager.h` | Extend to v3 (P1) |
| Tool windows: Brush, Measurement (plugin), Section Planes, Snapshots, Scene Manager, Cursor Settings | `GUI.cpp:6199-7965` | Unify (P3, P7) |
| ImGui docking branch v1.91.1 + multi-viewport (OS drag-out); **no root DockSpace yet** | `imgui_style.cpp:135-141` | Add AppShell DockSpace (P0) |
| Viewport reservation `g_dockLeftWidth/TopHeight` → `g_viewportX/…` + offscreen resize | `Gui.h:74`, `main.cpp:4586-4619` | Generalize to 4-sided insets (P0) |
| Stereo contract: ImGui frame built once (left eye), replayed right | `GUI.cpp:1618-1623` | **Preserve** |

Selection today: three globals, single-selection, index-based (`main.cpp:271-291`).
Window-open state: `bool show*Window` globals (`main.cpp:262-270`).
**Build reality:** MSVC-only, no CI, no tests — agents usually cannot compile (§17).

---

## 3. Coherence Contracts (binding)

These are what make the program feel like one piece. Every pass upholds all of them:

- **C1 — UiKit only.** All UI is built from UiKit tokens/widgets (§6.1). No ad-hoc
  colors, spacing or one-off widgets in feature code.
- **C2 — One style per object kind.** Icons + colors + nouns come from
  `UiKit::StyleFor(ObjectKind)` everywhere: Outliner, Inspector, palette, toasts,
  History, status bar.
- **C3 — Everything selectable is a `SceneItemRef`; every persisted object has a stable
  `ObjectId`** (§7.1). References that outlive a frame resolve by id, never by index.
- **C4 — Every user-visible mutation is one undoable step** (gesture-grained), recorded
  by ObjectId. No exceptions; settings are instead resettable (§12).
- **C5 — Every action is a Command** (§6.3) and runs through `CommandRegistry::run` —
  the single choke-point that later powers macros and the AI agent (§16.4).
- **C6 — Registries, not switches.** Tools, Inspector editors, importers, exporters,
  palette providers and Outliner section content are registered, so new features plug in
  without editing existing UI files (§16.1 recipe).
- **C7 — Docking freedom.** Every panel docks, floats, resizes and drags out to an OS
  window; layouts persist; `View ▸ Reset Layout` always recovers. Stable window names.
- **C8 — Nothing asks twice.** Any dialog that can recur carries "remember my choice";
  intelligence is suggestion-shaped (toast/hint with buttons), never a modal
  interruption, and all of it is opt-out.
- **C9 — Layout never breaks.** Panels adapt from 800×600 to 4K and GUI scale 0.5–2.0×:
  overflow collapses into menus, text truncates with tooltips, nothing clips or overlaps.
- **C10 — Capability is sacred.** Reorganize, relabel, fold — but never remove an
  ability the program has today.

---

## 4. The target experience (short narrative)

- **First launch:** shell + empty-state panels; a calm **Welcome Hub** card floats in the
  viewport: recent scenes with thumbnails, "Import files… (or drop anywhere)",
  primitives row, `Ctrl+K` hint. After a crash it leads with "Restore last session".
- **Import:** drop any mix of files — no questions. LAS tiles group themselves under a
  prefix-derived name; an HDR offers itself as environment via action-toast; unknown
  files get one warning toast. Scene was empty → camera frames the result.
- **Organize:** multi-select, `Ctrl+G` group (inline rename active), drag between
  groups, eye/lock per row, right-click for Frame / Isolate / Select Similar /
  Duplicate / Export / Delete — all undoable.
- **Inspect:** selection drives the Inspector; global settings appear as clearly-tinted
  "GLOBAL" cards that deep-link into Settings. Multi-edit applies to all selected.
- **Time travel:** `Ctrl+Z` is never wrong; the History panel is a labeled timeline with
  click-to-jump; Snapshots capture named states with thumbnails; the app offers a safety
  snapshot before destructive operations.
- **Find anything:** `Ctrl+K` — commands, objects, settings, snapshots, recent scenes,
  ranked by fuzzy match + frecency.
- **Tools are modes:** activate from toolbar/palette/shortcut, options appear as a Tool
  card in the Inspector, output lives in the Outliner, `Esc` exits.
- **Settings:** one searchable window; per-row/section/category reset; modified-dots.

---

## 5. The App Shell

```
┌───────────────────────────────────────────────────────────────────────────┐
│ Menu bar:  File  Edit  Create  Select  View  Tools  Help        [🔍 Ctrl+K]│
├──────────────┬────────────────────────────────────────────┬───────────────┤
│   OUTLINER   │   Viewport toolbar (overlay, top center)   │   INSPECTOR   │
│  search      │                                            │  Tool card    │
│  filter chips│            3D VIEWPORT                     │  Selection    │
│  tree:       │      (passthru central dock node —         │  sections     │
│   objects,   │       GL renders in this hole)             │  ──────────── │
│   groups,    │                                            │  Global cards │
│   lights,    │   [Welcome Hub floats here when empty]     │  (labeled)    │
│   tool output│  toasts (bottom-center)   perf (bottom-r.) │               │
├──────────────┴────────────────────────────────────────────┴───────────────┤
│ Status: 12 objects · 3.2M pts │ Tree_04 │ ⛏ Measure │ ShadowMap │ 144 fps │
└───────────────────────────────────────────────────────────────────────────┘
```

- **Root DockSpace** over the main viewport, `ImGuiDockNodeFlags_PassthruCentralNode`.
  Outliner left, Inspector right; aux panels (History, Snapshots, plugins…) default to
  tabs beside the Inspector. Default layout built with `DockBuilder*` on first run /
  `View ▸ Reset Layout`; `imgui.ini` persists user arrangements.
- **Viewport contract:** publish the central node's rect as four insets
  (`GUI::ViewportInsets{left,top,right,bottom}` replacing `g_dockLeftWidth/TopHeight`);
  `main.cpp:4586-4619` consumes them (keep the ≥64 px clamps and offscreen-target
  resize). Keep the old globals as deprecated aliases until P8.
- **Menu bar** renders from the CommandRegistry. `Camera`/`Cursor` menus dissolve —
  settings go to Settings/Inspector, actions stay as commands. Right-aligned palette icon.
- **Status bar**: thin bottom dock-node window — scene stats, selection summary,
  background progress, active-tool chip, lighting chip, FPS; far-right slot reserved for
  future presence/sync (§16). 
- **Preserved mechanics:** `WindowRounding = 0` with viewports; GUI built once per frame
  on the left eye and replayed for the right (`renderGUI(isLeftEye)` early-out);
  platform windows rendered via the existing lambda in `main.cpp`.

---

## 6. Foundation systems (Pass 0)

### 6.1 UiKit — `headers/Gui/UiKit.h` + `src/Gui/UiKit.cpp`

Move the existing custom widgets over verbatim, then extend. Contents:

- **Tokens:** spacing scale (2/4/6/8/12/16/24 px @1.0, via `S(v)` scale helper), radii
  (4 inner / 8 cards), motion standards (§15).
- **`ObjectKind` + `StyleFor(kind)`** → `{icon, themed color, noun}` for: Model, Mesh,
  PointCloud, Sun, PointLight, SpotLight, Group, Measurement, ClipPlane, BrushCluster,
  Snapshot, Environment, Tool, Setting, Command, File (+ reserved: LiveCapture).
  Append-only; consumers must tolerate unknown kinds.
- **Widgets** (existing five, plus): `SearchInput`, `Chip`, `Badge`, `Card`/`GlobalCard`
  (accent-tinted, "GLOBAL" tag), `IconButton`, `SegmentedControl`, `EmptyState`,
  `PropertyRow`, `ResetGlyph` (hover-revealed ↺ when ≠ default), `Dot` (modified),
  `HintToast` (toast with action buttons), `FuzzyMatch` (generalized from
  `snapshotFuzzyMatch`, `GUI.cpp:7267`, with highlight ranges), `Anim01` (§15).

### 6.2 AppShell — `headers/Gui/AppShell.h` + `src/Gui/AppShell.cpp`

`BeginAppShell()` (menu bar + dockspace + first-run layout) / `EndAppShell()` (status
bar + insets publication) / `ResetDefaultLayout()`. During P0 the *existing* Scene
Hierarchy docks into the left node and all other windows keep working — the shell lands
first, content migrates in later passes.

### 6.3 CommandRegistry — `headers/Core/CommandRegistry.h/.cpp`

```cpp
struct Command {
  std::string id;                 // "file.import", "view.frame_selected"
  std::string title, category, keywords;
  const char* icon = nullptr;
  std::function<void()> action;
  std::function<bool()> enabled, checked;   // optional
};
```

P0 registers the existing menu actions and rewires the menu bar to render from the
registry; map command ids ↔ `ShortcutAction` so live keybindings display everywhere.
`run(id)` records frecency (§10) and is the future macro/AI choke-point — keep it the
only way actions execute (C5).

### 6.4 Project discipline

Every new file goes into **both** `StereoVista.vcxproj` and `.vcxproj.filters`
(checklist in `docs/PLUGINS.md` §9).

---

## 7. Outliner (Pass 1) — the heart

Replaces the object list of `GUI.cpp:2426-2980`. New: `headers/Gui/Outliner.h` +
`src/Gui/Outliner.cpp`, window `"Outliner"`.

### 7.1 Identity & selection (contract C3)

```cpp
struct SceneItemRef {
  enum class Kind { None, Model, Mesh, PointCloud, Sun, PointLight, SpotLight,
                    BrushCluster, Measurement, ClipPlane, Group, Environment };
  Kind kind = Kind::None;
  uint64_t id = 0;      // persistent ObjectId (authoritative)
  int index = -1, sub = -1;   // cached container index / mesh index
};
class Selection { /* ordered multi-select; primary = last; onChanged callbacks */ };
```

**ObjectIds now, not later:** every persisted object (model, cloud, light, measurement,
plane, cluster, group) gets a `uint64_t id` from a per-scene monotonic counter,
serialized in v3, regenerated for old files at load. This unlocks global undo, snapshot
references, cooperative editing and the web viewer (§16) — retrofitting would touch
everything twice. Legacy globals (`currentSelectedType/Index/MeshIndex`) become a mirror
of the primary item until every consumer migrates (done by P8).

### 7.2 The tree

Sections (collapsible roots, count badges): **Environment** (sun, skybox — selectable
objects), **Scene objects** (models→meshes, clouds, lights; user groups + auto-groups
for `.scene` sources and multi-file imports), **Annotations & tool output**
(measurements, section planes, brush clusters — provided per-kind via registry, C6).

Row: `[chevron] [kind icon·color] [name] … [badge stack] [👁] [🔒]` — hover-revealed
eye/lock, full-row hit target, ~26 px rows. Badge stack is extensible (linked-file now;
"live"/"edited by" later). Add `locked` (ignored by picking/gizmo) and `visible` where
missing (measurements/planes — check `Engine/Data.h`).

Interactions (all undoable, batch = one step): multi-select (Ctrl/Shift), drag into/out
of groups + reorder, inline rename (`F2`/double-click; lights gain `name` fields),
context menu (Frame `F` · Isolate · Select Similar ▸ type/source/material · Show/Hide ·
Lock · Duplicate `Ctrl+D` · Group `Ctrl+G`/Ungroup · Rename · Export… · Delete `Del`),
type-filter chips, search (matches type nouns too), files dropped on the panel import,
isolate shows an exit chip in the status bar.

### 7.3 Data model & scene v3

`Engine::SceneGroup { id, name, visible, locked, parentId }` + `groups` in
`Engine::Scene`; `groupId` on objects; light names; **relative asset paths**
(scene-relative primary, absolute fallback, misses reported via `SceneLoadReport`) —
portability for sharing/web later. Bump `kSceneFormatVersion` to 3; v1/v2 load forever;
snapshots must survive unchanged.

**Acceptance:** every object type in the tree; multi-select works with gizmo
(`Tools::TransformGizmo` extended to selection sets) and batch ops; groups round-trip
through v3; old scenes load with fresh ids; empty results always show an `EmptyState`.

---

## 8. Inspector (Pass 2) — always the right controls

New: `headers/Gui/Inspector.h` + `src/Gui/Inspector.cpp`, window `"Inspector"`. Move the
`render*ManipulationPanel` functions (`GUI.cpp:8095-8850`) here — wholesale first,
restyle second — behind a **per-kind editor registry** (C6).

Top-to-bottom: **Header card** (icon, editable name, kind badge, quick actions; "3
objects (2 Models, 1 Light)" for multi) → **Tool card** (active tool's options, P7) →
**per-kind sections** (today's sections, restyled: Transform/Material/Textures/Display/
Info/Export; collapsed state persisted per kind) → **Global cards** (tinted, "GLOBAL"
tag: e.g. cloud selected → EDL/splatting/HQS/base size; light → shadow quality; each
with "Open in Settings →" deep-link; edits write the same preference — one source of
truth) → **EmptyState** with 3 contextual actions when nothing is selected.

Rules: multi-edit applies shared widgets to all (one undo gesture — extend
`PanelEditTracker` to selection sets); mixed kinds → header + relative Transform only;
per-section `ResetGlyph` (to creation/import defaults, undoable); texture drag-apply
onto Textures section or viewport with slot detection by filename suffix (`*_normal`,
`*_rough`, `*_ao`, else albedo) + "[Change slot]" toast; sun/environment editors absorb
`renderSunManipulationPanel` and the object-ish half of Settings ▸ Environment.

---

## 9. History & Snapshots (Pass 3) — time travel

**Global undo (C4):** audit every user-visible mutation and route it through
`UndoManager` with ObjectId-based records. Known gaps to verify/close: visibility &
lock toggles, group create/rename/membership, renames, measurement + clip-plane
add/delete, brush cluster edits, snapshot *restore*, import batches (one step),
sun/environment edits. Settings stay non-undoable by design (resettable instead).

**History panel** (`headers/Gui/HistoryPanel.h/.cpp`, window `"History"`, default tab
beside Inspector): icon-coded, human-labeled timeline ("Moved 3 objects", "Imported
survey_north (4 files)"), relative times, click-to-jump (repeated undo/redo on the
existing stack — no new engine), last-saved marker, search, depth pref (default 200).
Edit-menu history (`GUI.cpp:2035`) becomes "recent 5 + Open History".

**Snapshots:** the existing `Core::SnapshotManager` (thumbnails, tags, fuzzy search,
selective restore flags — from the original GL build) is the reference implementation;
keep 100 % capability. Restyle with UiKit; default dock beside Inspector; palette
provider; hub thumbnails; one-click "Snapshot now" in the status bar; **safety
snapshots** offered before scene Replace and Reset-All (pref-gated, default on);
restore is one undoable History entry. Leave visual room per card for a future A/B
compare affordance (§16).

---

## 10. Command palette & global search (Pass 4)

New: `headers/Gui/CommandPalette.h/.cpp`. `Ctrl+K` (+ menu-bar icon), centered overlay,
dimmed scrim, fully keyboard-driven. **Providers are a registry** (C6): commands (with
live shortcuts + enabled state), scene objects (by ObjectId; Enter select,
`Shift+Enter` frame), settings index (deep-link + flash-highlight the row, P6),
snapshots (thumbnail rows), recent scenes, help. Ranking = `FuzzyMatch` score blended
with **frecency** (decayed use counters persisted in prefs; `CommandRegistry::run`
records). Empty query = top-frecency + selection actions + a tip. Prefix filters: `>`
commands, `#` objects, `:` settings. Local search fields everywhere else reuse
`SearchInput` + `FuzzyMatch` so search *feels identical* across the app.

---

## 11. Smart import, Welcome Hub, autosave (Pass 5)

**ImportService** (`headers/Core/ImportService.h/.cpp`) — the one entry point for menu
(`File ▸ Import Files…`, `Ctrl+I`, single combined filter), drop (window + Outliner),
hub and palette. `Plan(paths) → [{action, path, reason}]` (pure, testable) then
`Execute`:

- Detection: extension table (models `.obj/.fbx/.3ds/.gltf/.glb`; clouds incl. LAS/LAZ
  grouping; `.scene`; **new:** `.hdr/.exr` → environment offer, images → texture),
  content-sniffing for ambiguous text formats (reuse the binary-PLY header parser).
  Unknown → one warning toast, never a modal.
- Duplicate awareness: already-loaded path → "[Add another copy] [Skip]" toast, batch-aware.
- Multi-file imports land as one group auto-named from the longest common filename
  prefix (fallback: folder name); one undo step.
- Display names prettified (strip extension, `_`/`-` → space); source path in Info.
- Scene files keep replace/merge/ask (`SceneLoadingBehavior`), dialog gains "remember my
  choice" (C8) and the safety-snapshot offer (§9).
- After import: select; frame-all if scene was empty; aggregate toast.

**Welcome Hub** (`headers/Gui/WelcomeHub.h/.cpp`, replaces `renderEmptySceneHint`):
non-modal centered card when the scene is empty — recent scenes as thumbnail cards
(snapshot thumbnails / `loadSceneInfo` counts), Import button + drop hint, primitives
row, footer shortcuts. Leads with a **Restore-last-session card** when a stale
`session.lock` + autosave exist.

**Autosave:** timer-based (default 5 min, pref-able) scene autosave to
`StereoVista/autosave/` (scenes store references, so this is cheap; skip while
streaming); `session.lock` on start, removed on clean exit; status bar whispers
"Autosaved · 14:02".

---

## 12. Settings overhaul (Pass 6)

Extract to `headers/Gui/SettingsWindow.h/.cpp`. Keep the sidebar categories; add:

- **Defaults framework:** `static const GUI::ApplicationPreferences kDefaultPrefs{}`
  (the struct's initializers are the defaults — `GuiTypes.h:123-463`). `IsModified`,
  per-row reset (`ResetGlyph`), per-section reset (`⋯` menu), per-category footer
  button, global "Reset All…" (confirm + safety snapshot). Resets re-apply side effects
  — centralize the existing mirror-global pattern as `ApplyPreferences(categoryMask)`.
- **Search:** rows self-register `{label, category, keywords}` into the settings index
  (shared with the palette); in-window search filters sections live with match
  highlighting; matching categories glow in the nav.
- **Modified dots** on rows differing from default.
- **Re-grouping (C10 — nothing removed):** Camera menu's sliders → Settings ▸ Camera &
  3D; Cursor Settings window → Settings ▸ 3D Cursor category (tabs → sections,
  `CursorPreview3D` embedded — verify it renders inside a dockable window); dev
  leftovers (icon test window) behind a hidden Developer category (`dev.icons` command).
- Theme picker upgraded with the existing swatch API into theme cards.

---

## 13. Tools, export & plugins (Pass 7)

**ToolManager** (`headers/Tools/ToolManager.h`) — a **registry** (C6): tools
self-describe `{id, name, icon, shortcutAction, category}`; at most one active;
activate/deactivate are commands; `Esc` exits. Toolbar segment, Tools menu, palette and
status chip all render from it — future tools appear everywhere with zero UI edits.

- Measurement (plugin), Section Planes, Brush become modes: options in the Inspector
  Tool card (new optional plugin hook `onRenderInspector`), output in the Outliner,
  existing windows remain as dockable alternatives (same widgets, one implementation).
- **ExportService** (`headers/Core/ExportService.h/.cpp`) mirrors ImportService: a
  registry of exporters per kind/format. Initially: point-cloud export (exists,
  `GUI.cpp:8544`), screenshots/stereo screenshots, scene save-as. Surfaces: `File ▸
  Export…` (selection-aware), Outliner context Export (batch, one folder dialog,
  status-bar progress), Inspector Export sections. Future: mesh via Assimp, measurement
  CSV, web package (§16).
- Scene Manager window dissolves: recents/info → Hub + File menu; save options →
  Settings.
- **Plugins:** additive `PluginContext` extensions — `registerCommand`, `registerTool`,
  `onRenderInspector`, kind-styled toasts. Update `docs/PLUGINS.md` + `CrosshairPlugin`
  template. Existing plugin API keeps working unchanged.

---

## 14. Viewport, status bar, hints (Pass 8)

- **Unified viewport toolbar** (merges the two floating strips): gizmo segmented control
  | view ▾ | shading ▾ | camera ▾ | tools segment (from ToolManager) | layout/hide-UI.
  Width-aware overflow into `⋯` (C9).
- **Status bar completes:** stats (reuse `formatPointCount`), selection summary,
  streaming progress (fold the overlay in; per-file detail on hover), tool chip,
  lighting chip (click = cycle, matches `L`), stereo indicator, FPS (click → perf
  overlay), autosave whisper.
- **SceneStats service** (`headers/Core/SceneStats.h`): cheap per-second object/tri/
  point/VRAM/draw-call stats → status bar popup, hub cards, performance hints.
- **Shortcut overlay:** hold `F1` → translucent live-generated keymap cheat sheet
  (`Shift+F1` pins).
- **Overlay restyle** with tokens (perf, radar frame, measurement labels); toasts gain
  action buttons, stack limit 3 + "+2 more".
- **HintEngine** (`headers/Gui/Hints.h`): context-triggered, rate-limited, dismiss-
  forever, all behind `preferences.enableHints`. Launch set: frame-selected tip after
  first import; performance guardian (FPS low + expensive feature on → "[Lower it]
  [Keep]", never auto-changes); HQS suggestion for huge clouds; grouping tip at 5+
  ungrouped objects; palette tip after 3× Settings opens; "measurements live in the
  Outliner" after first measurement.
- Remove the legacy inset aliases; all consumers on `ViewportInsets`.

---

## 15. Polish, motion & quality-of-life (every pass + audit in Pass 9)

The difference between "works" and "feels crafted". Binding spirit, free execution.

**Motion standards** (in UiKit; the app already redraws every frame, so animation is
free — keep the math trivial):
- 120–150 ms ease-out for micro (hover fills, chevron rotation, eye/lock fade-in,
  toggle knob); 200–250 ms for panel-level (palette open, hub fade, toast slide);
  a *small* overshoot is allowed only on palette/toast entrance.
- Never animate data values, drag responses, or anything during a drag; no motion on
  the 3D viewport's account. `preferences.reduceMotion` disables all of it (C8 spirit).
- `Anim01(id, target)` is the only mechanism — no bespoke timers.

**Micro-delight (the tasteful "gamification"):** completions feel rewarding, not gamey —
import progress ends with a brief success tick + count-up in the toast; the save command
pulses the title-bar dirty-dot away; History jump flashes the affected rows; palette
Enter gives a 1-frame accent flash on the executed row; group creation "swallows" rows
with a 150 ms collapse. No points, badges or streaks — this is a pro instrument.

**QoL catalog** (each tiny; sprinkle through passes, audit in P9):
- Right-click any slider/field → Reset / Copy / Paste value.
- Hovering an Outliner row subtly highlights that object in the viewport; double-click
  = frame. (Hook into the existing selection-outline render path.)
- Unsaved-changes dot in the title bar (extend `UpdateWindowTitleForScene`); toast
  `[Undo]` button on destructive actions.
- Truncated text *always* tooltips its full value; tooltips show the live shortcut.
- Every file dialog remembers its last folder per context (import/export/scene).
- Scroll positions, expanded sections and filter states persist per panel.
- `Enter`/`Esc` consistent in every dialog; first field auto-focused.
- Numeric fields: unit suffixes, existing Ctrl+click-to-type everywhere, `Alt`-drag =
  fine-grained scrub.
- Copy buttons on Info rows (paths, point counts, world positions).
- Drag-and-drop wherever plausible: files → anywhere, texture → model, objects → group,
  snapshot card → viewport to restore.

---

## 16. Built for what's coming (provisions, not features)

Roadmap: more editing/measurement/export tools; **macros**; **AI features** (a global
agent that can modify the scene/settings and create macros); cooperative editing; a web
viewer; live viewport-capture sources. Build none of it now — but never design its
rails away:

1. **More tools** — the recipe (verify it stays true every pass): register a tool
   (§13) + commands (§6.3); if it produces objects: new ObjectKind + Outliner provider +
   Inspector editor + undo records + v3 serialization; if it moves files: importer/
   exporter registration. A recipe-following tool touches zero existing UI files.
2. **History/Snapshots extensions** — A/B snapshot compare, snapshot notes/export
   (§9 leaves room).
3. **Cooperative editing** — needs: ObjectIds (C3), the command/undo choke-points
   (C4/C5, an operation-log shape already), portable v3 scenes (§7.3), reserved UI
   slots (status-bar presence segment, Outliner badge stack, conflict-toast pattern).
4. **Macros & AI agent** — both are *drivers of the same surfaces users use*: commands
   (C5 — later gains optional parameterized variants), the settings index (§12 — every
   setting addressable by id), ImportService/ExportService, Selection. A macro is a
   recorded command sequence; the AI agent is a producer of commands/macros with the
   same permissions UI (previewable, undoable — C4 makes agent actions safe by
   construction). Keep these surfaces complete and scriptable-shaped; add no UI now.
5. **Web viewer** — v3 stays plain, self-describing JSON with relative paths; snapshot
   thumbnails double as gallery assets; `Export ▸ Web package` is one future exporter
   registration; UiKit tokens are documented (§6.1) so a web UI can mirror the language.
6. **Live capture sources** — reserved `ObjectKind::LiveCapture` + "live" badge +
   ImportPlan action; Outliner/Inspector never assume objects are static.

---

## 17. Engineering guardrails (binding)

1. **MSVC-only, agents usually can't compile:** small reviewable commits; move code
   wholesale before restyling; additive files over surgery in `main.cpp`/`GUI.cpp`;
   keep `extern` contracts and `renderGUI(bool, ImGuiViewportP*, ImGuiWindowFlags,
   Shader*)` stable; C++17 as MSVC accepts it (no out-of-order designated initializers,
   no GNU extensions); every new file in `.vcxproj` **and** `.filters`.
2. **Never break:** stereo left/right GUI replay; `g_sharedPassesDone`; multi-viewport
   drag-out (+ GL context backup/restore); GUI-hidden mode (`G`); GUI scale 0.5–2.0×;
   all 7 themes (light *and* dark); JSON backward compatibility for preferences/
   shortcuts/scenes (missing keys → today's behavior; never reorder persisted enums —
   warning in `imgui_sytle.h:58-60`); plugin API; 3DConnexion sync; undo coverage (C4).
3. **Identity & undo discipline:** ObjectIds on every persisted type; frame-outliving
   references resolve by id; one undoable step per user intent (`PanelEditTracker`
   pattern or batch equivalent).
4. **Preferences:** new persistent UI state goes through `savePreferences`/
   `loadPreferences` with behavior-preserving defaults.
5. **Decompose as you go:** each pass extracts its area from `GUI.cpp` into the new
   files; never two live copies of a panel; `GUI.cpp` ends as the thin orchestrator.
6. **Manual test checklist** (on Windows when possible; otherwise say so in the PR):
   clean launch + legacy-prefs launch; import every format (obj/gltf/fbx, xyz,
   ascii+binary ply, multi-tile las, laz, h5, pcb) + drop-import; dock/undock/float/
   drag-out every panel; reset layout; 800×600 and 4K; toggle `G`; cycle `L`; an
   undo/redo session incl. History jump; v2 scene load + v3 round-trip; autosave
   recovery; quad-buffer stereo smoke test if hardware allows.
7. **Copy:** sentence case, verbs on buttons, tooltips explain *why*, units on every
   number, no jargon at first level.

---

## 18. Pass Plan

Foundation → heart → time-travel → intelligence → polish. Passes 4–7 parallelize after
Pass 2. Sizes: S < 300, M < 1200, L < 3000 LoC touched.

| # | Pass | Spec | Size | Needs |
|---|---|---|---|---|
| 0 | Foundation: UiKit, AppShell + insets, status-bar shell, CommandRegistry + menu rewire | §5–6 | L | — |
| 1 | Outliner + data model: ObjectIds, Selection, groups, scene v3, multi-select, rename, isolate, lock | §7 | L | 0 |
| 2 | Inspector: editor registry, multi-edit, global cards, resets, texture drag-apply | §8 | L | 1 |
| 3 | History & Snapshots: undo audit, History panel, snapshot restyle + safety snapshots | §9 | M | 1 |
| 4 | Command palette: providers, fuzzy + frecency, prefix filters | §10 | M | 0 (best after 1+3) |
| 5 | Import + Hub + autosave: ImportService, dupes/auto-group/pretty names, hub, recovery | §11 | M | 0 |
| 6 | Settings: search index, defaults framework, resets + dots, re-grouping | §12 | L | 0 |
| 7 | Tools & export: ToolManager registry, tool cards, ExportService, plugin hooks | §13 | M | 2 |
| 8 | Viewport & alive: unified toolbar, status bar completion, SceneStats, F1 overlay, HintEngine | §14 | M | 0; 7 for tool chip |
| 9 | Harmony audit: §15 polish/QoL sweep, spacing/icon/copy audit, recipe verification (mock tool through §16.1), perf, docs, dead code | §15 | M | all |

Every pass PR states: deviations from this plan (and why), manual-test status, Status
Board update.

---

## 19. Status Board

Keep this truthful — it is the coordination point between passes.

| Pass | State | Branch/PR | Notes & deviations |
|---|---|---|---|
| Plan | ✅ done | `claude/gui-redesign-ux-hadxj2` | Rev 3: slimmed for implementers (binding = §3/§17/acceptance only); added §15 polish/motion/QoL and §16.4 macros+AI provisions. |
| 0 Foundation | ⬜ not started | | |
| 1 Outliner + data model | ⬜ not started | | |
| 2 Inspector | ⬜ not started | | |
| 3 History & Snapshots | ⬜ not started | | |
| 4 Palette | ⬜ not started | | |
| 5 Import + Hub + autosave | ⬜ not started | | |
| 6 Settings | ⬜ not started | | |
| 7 Tools & export | ⬜ not started | | |
| 8 Viewport & alive | ⬜ not started | | |
| 9 Harmony | ⬜ not started | | |

### Decisions log

- 2026-07-12 — Outliner + Inspector as separate dockable panels over a passthru-central-
  node DockSpace; scene keeps rendering directly to the backbuffer.
- 2026-07-12 — CommandRegistry is the coherence mechanism (menus/palette/shortcuts/
  toolbars; later macros + AI agent drive the same choke-point).
- 2026-07-12 — No capability removal anywhere (C10).
- 2026-07-12 — Stable ObjectIds from Pass 1 (prerequisite for global undo, snapshots,
  collaboration, web viewer).
- 2026-07-12 — History & Snapshots promoted to Pass 3; the GL-build SnapshotManager is
  the reference — no regressions.
- 2026-07-12 — Tools/editors/importers/exporters/providers are registries with a
  documented recipe; ExportService mirrors ImportService.
- 2026-07-12 — Rev 3: plan restructured — only §3 contracts, §17 guardrails and pass
  acceptance are binding; the rest are strong defaults implementers may improve on.
  Added motion/QoL layer (§15, incl. `reduceMotion` pref) and macros/AI provisions
  (§16.4).
