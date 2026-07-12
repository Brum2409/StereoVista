# StereoVista UI/UX Redesign — Master Plan

**Status: living document.** This is the guideline for the full GUI remake. It is executed
in multiple agent passes (see [§15 Pass Plan](#15-pass-plan)). Each pass updates the
[Status Board](#16-status-board) at the bottom.

> **For implementing agents:** this plan is a *guideline, not a contract*. If you find a
> better-fitting solution while implementing a pass, build it — without asking — as long as
> it serves the North Star below. When you deviate, note it in the Status Board so later
> passes stay coherent. Never leave the app in a broken or half-migrated state at the end
> of a pass: every pass must end with a working, coherent application.

---

## 1. North Star

StereoVista should feel like **one intelligent instrument**, not a collection of windows.

A new user who has never seen the program must be productive in the first minute: the
layout matches what every modern 3D tool taught them (scene tree on the left, properties
on the right, viewport in the middle), the program figures out intent on its own (drop any
file — it just loads), and anything can be found from one place (global command palette).
A power user must lose nothing: every option the program has stays reachable — organized,
searchable, and progressively disclosed instead of removed.

### Design principles (the constitution)

1. **Familiar by default.** Copy the layout conventions of Unity/Blender/Unreal where they
   exist: Outliner left, Inspector right, viewport center, status strip bottom, `Ctrl+K`
   palette. Innovation goes into *removing friction*, not into novel layouts users must learn.
2. **One heart: the scene.** *Every* loaded or created thing lives in the Outliner — models,
   meshes, point clouds (all formats), lights, sun, environment, measurements, section
   planes, brush clusters, groups. No orphan object lists in tool windows.
3. **The inspector is always right.** Whatever is selected — object, light, tool, nothing —
   the Inspector shows exactly the relevant controls, and clearly separates *this object's*
   properties from *global* settings (which are labeled as such and deep-link into Settings).
4. **Don't ask, know.** Detect file types on import, remember answered dialogs ("always do
   this"), auto-frame the first import, suggest instead of interrupt. Every dialog we delete
   is a feature.
5. **Progressive disclosure, never amputation.** Common controls visible; advanced controls
   one deliberate click away (collapsed "Advanced" sections, persisted per panel). All
   capabilities remain; nothing is stripped.
6. **One design system.** All panels, tools, overlays and plugins use the same tokens,
   icons, spacing, semantic colors and widgets (UiKit). A tool written next year must look
   like it shipped with the first release.
7. **Alive, quietly.** The app reacts: toasts confirm, hints teach (rate-limited, dismissible
   forever), empty states explain, badges count, the status bar talks. Intelligence never
   nags and can always be turned off.
8. **Everything is a command.** Every action is registered once (CommandRegistry) and
   surfaces uniformly in menus, palette, shortcuts and toolbars. That is what makes the
   program feel coherent instead of "many windows with their own logic".
9. **Built for growth.** The roadmap is large — many more editing/measurement/export
   tools, cooperative editing, a web viewer, live viewport-capture sources. The UI must
   absorb all of it *by registration, not by redesign*: object kinds, tools, commands,
   importers/exporters and inspector editors are all data-driven registries with a
   documented "add one" recipe. See [§13 Built for what's coming](#13-built-for-whats-coming).

---

## 2. Where we are today (code inventory)

The GUI was already *partially* modernized — reuse this, don't rebuild it:

| Asset | Where | State |
|---|---|---|
| Theme system, 7 themes + semantic palette `g_StyleColors` (primary/accent/success/…) | `headers/libs/imgui/imgui_sytle.h`, `imgui_style.cpp` (`ApplyGuiTheme`, swatches API) | **Keep & extend** |
| Fonts: regular/bold/header/small/mono + FontAwesome 5 icon font, DPI + user scale (0.5–2.0×) | `imgui_style.cpp` (`g_Fonts`, `g_GuiScale`), `IconsFontAwesome5.h` | **Keep** |
| Custom widgets: `DrawToggleSwitch`, `DrawNavItem`, `DrawSectionHeader`, `DrawPanelTitle`, `DrawInlineIcon`, `MenuBarSeparator`, tag chips (snapshots) | `src/Gui/GUI.cpp:498-668`, `7339` | **Extract into UiKit** (Pass 0) |
| Toasts (bottom-center, typed) | `GUI.cpp` (`GUI::ShowToast`, `renderToasts`) | **Keep** |
| Overlays: perf (bottom-right), point-cloud streaming (bottom-left), empty-scene hint, measurement labels, view-mode toolbar (top-left), gizmo toolbar (top-right) | `GUI.cpp:203-420`, `894-1224`, `7968` | **Restyle/unify** (Pass 8) |
| Settings window: sidebar nav, 7 categories | `GUI.cpp:3155-6193` (`SettingsCategory`, `kNavEntries`) | **Overhaul** (Pass 6) |
| Scene Hierarchy: fixed left panel, search, visibility, per-source-scene grouping, meshes, inline Properties child | `GUI.cpp:2426-3097` | **Replace** with Outliner + Inspector (Passes 1–2) |
| Manipulation panels per type (model/mesh/point cloud/sun/point light/spot light/brush cluster) | `GUI.cpp:8095-8850` | **Become Inspector editors** (Pass 2) |
| Undo: gesture-grained `PanelEditTracker`, full `UndoManager`, Edit-menu history list | `GUI.cpp:460-493`, `2035`, `Core/UndoManager` | **Keep — grows into global History** (Pass 3) |
| Snapshots: named camera/scene/tool-state snapshots with thumbnails, tags, fuzzy search, restore flags — the system carried over from the original GL build | `Core/SnapshotManager`, `GUI.cpp:7252-7965` | **First-class citizen — restyle & integrate, never regress** (Pass 3) |
| Plugin system: `PluginContext`, menu/UI/input/viewport hooks, `REGISTER_PLUGIN` | `headers/Plugins/*`, `docs/PLUGINS.md` | **Keep, extend** (Pass 7) |
| Shortcuts: rebindable `ShortcutAction` enum + `shortcuts.json` + editor in Settings | `headers/Engine/ShortcutManager.h` | **Keep, feed from CommandRegistry** |
| Scene persistence v2 (metadata, environment, sun, save options, load report, recents) | `headers/Core/SceneManager.h` | **Extend to v3** (ids, groups, names) |
| Tool windows: Brush, Measurement (plugin), Section Planes, Snapshots, Scene Manager, Cursor Settings (4 tabs) | `GUI.cpp:6199-7965` | **Unify** (Passes 3, 7) |
| ImGui **docking branch** v1.91.1, viewports (OS drag-out) enabled; **no root DockSpace yet** — hierarchy is a pinned window, floats dock only onto each other | `imgui_style.cpp:135-141`, CLAUDE.md | **Add AppShell DockSpace** (Pass 0) |
| Viewport reservation: `g_dockLeftWidth`/`g_dockTopHeight` → `g_viewportX/TopInset/Width/Height` + offscreen-target resize | `Gui.h:74`, `main.cpp:4586-4619` | **Generalize to 4-sided insets** (Pass 0) |
| Stereo GUI contract: ImGui frame built once on the **left** eye, draw data replayed for the right eye | `GUI.cpp:1618-1623` (`renderGUI(isLeftEye,…)`) | **Must be preserved** |

Selection state today is three globals in `main.cpp:271-291` (`SelectedType` enum +
`currentSelectedIndex` + `currentSelectedMeshIndex`) — single selection only, index-based
(no stable identity — see §6.1/§13.4 for why that must change).

Window-open state is `bool show*Window` globals (`main.cpp:262-270`).

**Build reality:** MSVC-only (`StereoVista.sln`), no CI, no automated tests. Agent passes
usually cannot compile — see [§14 Engineering guardrails](#14-engineering-guardrails-every-pass).

---

## 3. The target experience (narrative)

**First launch.** A clean window: menu bar, Outliner (left, mostly empty), viewport with a
subtle grid/skybox, Inspector (right, friendly empty state), thin status bar. Centered in
the viewport floats the **Welcome Hub** — a calm card, not a modal: recent scenes as
thumbnail cards, one big "Import files…" affordance ("or just drop files anywhere"),
a row of primitive buttons, and "Press `Ctrl+K` for anything". It disappears the moment
the scene has content and comes back on `File ▸ New`. If the last session ended
unexpectedly, the hub leads with a "Restore last session" card instead of a scary dialog.

**Import.** The user drops seven files: three LAS tiles, an FBX, a PLY, an HDR and a PNG.
No questions. The status bar shows progress; the LAS tiles are grouped and georeferenced
together (existing behavior) **and land as one Outliner group auto-named from their common
filename prefix**; the PLY is sniffed (ASCII vs binary is already handled); the HDR is
offered as environment ("Set as environment? [Yes] [Just import]" — one toast with
buttons, remembered if the user ticks "always"); the PNG gets a helpful toast ("Images
can be used as textures — drop it onto a model to apply"). Everything lands in the
Outliner with cleaned-up display names, selected; the camera frames the new content
because the scene was empty.

**Organize.** In the Outliner the user Ctrl-selects the LAS tiles, presses `Ctrl+G` —
"Group: *Survey North*" (inline rename active immediately). Eye icons toggle visibility,
per-group too. Drag objects between groups. `F2` renames anything, including lights.
Right-click gives the expected verbs: Frame, Isolate, Select Similar, Duplicate, Group,
Rename, Export, Delete — all undoable.

**Inspect.** Selecting a point cloud shows the Inspector: name + type badge, Transform,
Display (point size, splatting…), Info (point count, source file), Export — and a clearly
separated, differently-tinted **"Global — Point Cloud Rendering"** card (EDL, HQS, Morton
resort) marked as affecting *all* clouds, with a "Open in Settings" link. Multi-select
shows shared properties and applies edits to all (one undo step).

**Time travel.** `Ctrl+Z` works on *everything* — imports, group operations, renames,
tool output, visibility — and the **History** panel shows the whole session as a labeled,
icon-coded timeline: click any entry to jump there, with a marker on the last-saved state.
For bigger jumps there are **Snapshots**: one click stores a named state with a thumbnail
(camera, scene, tool state — the system from the original GL build, kept whole), restored
from the panel, the palette, or the hub. Before a destructive scene replace, the app
quietly offers to snapshot first.

**Find anything.** `Ctrl+K`: typing "shad" lists *Toggle Shadows* (command), *Shadow
quality → Settings ▸ Rendering* (setting, deep-link), *Shadow catcher plane* (scene
object). Typing "sep" finds *Eye Separation*. Typing "north" finds the *Survey North*
group and the *North façade* snapshot. Enter executes / navigates / selects; results the
user picks often float upward over time.

**Tools.** Measure, Section, Brush are **modes**, not windows: activating one (toolbar,
menu, palette or shortcut) highlights it in the toolbar, shows its options as a "Tool"
card at the top of the Inspector, drives viewport interaction, and `Esc` exits. Their
output (measurements, planes, clusters) lives in the Outliner like everything else.
Every panel can still be popped out to its own OS window and docked anywhere. Next
year's tools — point-cloud cropping, mesh cleanup, batch export — appear in the same
toolbar, palette and Inspector automatically, because tools are registry entries.

**Settings.** One window, searchable. Each category and each section has a reset-to-default
affordance; modified values carry a subtle accent dot. Nothing was removed — advanced
blocks are collapsed but present.

---

## 4. Information architecture — the App Shell

```
┌───────────────────────────────────────────────────────────────────────────┐
│ Menu bar:  File  Edit  Create  Select  View  Tools  Help        [🔍 Ctrl+K]│
├──────────────┬────────────────────────────────────────────┬───────────────┤
│              │  Viewport toolbar (overlay, top of hole):  │               │
│   OUTLINER   │  [gizmo: select/move/rot/scale] [view ▾]   │   INSPECTOR   │
│              │  [shading ▾] [camera ▾]        [⛶ layout]  │               │
│  search      │                                            │  Tool card    │
│  filter chips│                                            │  (when a tool │
│  ┌─ tree ──┐ │            3D VIEWPORT                     │   is active)  │
│  │ objects │ │      (passthru central dock node —         │  ──────────── │
│  │ groups  │ │       GL renders in this hole)             │  Selection    │
│  │ lights  │ │                                            │  sections     │
│  │ tools'  │ │   [Welcome Hub floats here when empty]     │  ──────────── │
│  │ output  │ │                                            │  Global cards │
│  └─────────┘ │  toasts (bottom-center)   perf (bottom-r.) │  (labeled)    │
├──────────────┴────────────────────────────────────────────┴───────────────┤
│ Status bar: 12 objects · 3.2M pts │ Selected: Tree_04 │ ShadowMap │ 144fps│
└───────────────────────────────────────────────────────────────────────────┘
```

- **Root DockSpace** over the main viewport with `ImGuiDockNodeFlags_PassthruCentralNode`.
  Outliner docks left, Inspector right, both resizable, closable, floatable, and still
  draggable out of the OS window (multi-viewport stays enabled). Aux panels (History,
  Snapshots, Cursor, plugins…) default to docking as tabs with the Inspector.
- The **central node's rect** is published to the renderer each frame (replaces
  `g_dockLeftWidth`/`g_dockTopHeight` — see §5.2). The 3D scene keeps rendering directly
  to the backbuffer through the "hole"; no render-to-texture change.
- **Default layout** built once with `DockBuilder*` (first run or `View ▸ Reset Layout`);
  afterwards ImGui's `imgui.ini` persists user arrangements automatically.
- **Menu bar** simplifies: `Camera` and `Cursor` menus dissolve (their *settings* belong to
  Settings/Inspector; their *actions* become commands reachable from View menu, palette and
  viewport toolbar). A right-aligned search icon opens the palette. Menus render from the
  CommandRegistry, so labels/shortcuts/enabled-state stay consistent everywhere.
- **Status bar** (new, thin, bottom): scene stats, selection summary, active tool, lighting
  mode chip, background progress (point-cloud streaming), FPS (click → perf overlay),
  contextual hint slot. The far-right segment is a *reserved slot* for future presence
  avatars / sync state (cooperative editing, §13.4). It is a window docked into a bottom
  node with tab bar hidden.

Panel identity: keep stable ImGui window *names* — `"Outliner"`, `"Inspector"`,
`"History"`, `"Snapshots"`, `"Settings"`, … — since dock layout persistence keys on them.

---

## 5. Pass 0 — Foundation (UiKit, AppShell, CommandRegistry)

Everything else builds on this pass. Deliverables:

### 5.1 UiKit — one design system

New files `headers/Gui/UiKit.h` + `src/Gui/UiKit.cpp` (namespace `UiKit`). Move the
existing custom widgets out of `GUI.cpp` verbatim (they are good), then extend.

**Tokens** (all pre-multiplied by `g_GuiScale.currentScale` via helpers):

```cpp
namespace UiKit {
  // Spacing scale (px @1.0): 2 4 6 8 12 16 24; radii: 4 (inner), 8 (cards/pills)
  float S(float v);                    // scale helper: v * g_GuiScale.currentScale
  // Semantic object styling — THE source of truth for icons+colors everywhere.
  // NOTE: append-only enum (persisted indirectly); new kinds arrive with future
  // tools (§13) — everything consuming ObjectKind must handle unknown kinds sanely.
  enum class ObjectKind { Model, Mesh, PointCloud, Sun, PointLight, SpotLight,
                          Group, Measurement, ClipPlane, BrushCluster, Snapshot,
                          Environment, Camera, Tool, Setting, Command, File,
                          LiveCapture /* reserved, §13.6 */ };
  struct ObjectStyle { const char* icon; ImVec4 color; const char* noun; };
  const ObjectStyle& StyleFor(ObjectKind k);   // e.g. {ICON_FA_CLOUD, teal, "Point Cloud"}
}
```

Distinct hue per kind (from the active theme's palette, not hardcoded): models=blue,
point clouds=teal, lights=amber, groups=violet, measurements=green, sections=red,
snapshots=pink. Used by Outliner rows, Inspector headers, palette results, toasts,
status bar — this consistency is a large part of "everything belongs together".

**Widgets** (each ~ a screenful, draw-list based like the existing ones):
`ToggleSwitch`, `NavItem`, `SectionHeader`, `PanelTitle`, `InlineIcon` (moved) — plus new:
`SearchInput` (icon + hint + clear button, the hierarchy one generalized), `Chip`
(filter/tag, toggleable), `Badge` (count pill), `Card`/`EndCard` (rounded child with
padding; variant `GlobalCard` with accent tint + "GLOBAL" tag), `IconButton`,
`SegmentedControl`, `EmptyState` (big icon + title + hint + optional action button),
`PropertyRow` helpers (label left, widget right, consistent column split),
`ResetGlyph` (small ↺ shown on hover when value ≠ default; returns clicked),
`Dot` (modified indicator), `HintToast` (toast with action buttons).

**Motion:** tiny and consistent — 120–150 ms ease-out for hover fills, toast slide,
palette open, hub fade. One helper `Anim01(id, target, speed)` storing state in an
`ImGuiStorage`. No animation on data widgets (sliders etc.).

### 5.2 AppShell — dockspace, layout, viewport contract

New files `headers/Gui/AppShell.h` + `src/Gui/AppShell.cpp`:

```cpp
namespace GUI {
  struct ViewportInsets { float left=0, top=0, right=0, bottom=0; };
  extern ViewportInsets g_viewportInsets;      // replaces g_dockLeftWidth/TopHeight
  void BeginAppShell();   // menu bar + DockSpaceOverViewport(Passthru) + first-run layout
  void EndAppShell();     // status bar; publish insets from the central node rect
  void ResetDefaultLayout();                   // DockBuilder: L 22% / R 26% / bottom 26px
}
```

Publication: after submitting the dockspace, `DockBuilderGetCentralNode(dockspaceId)`
gives `Pos/Size`; convert to window-relative insets. `main.cpp:4586-4619` changes from
two insets to four (`g_viewportX/Y/Width/Height` already exist; add right/bottom math).
Keep the ≥64 px clamps and the offscreen-target resize block exactly as-is. Keep
`Gui.h`'s `g_dockLeftWidth/g_dockTopHeight` as deprecated aliases until Pass 8 removes
the last consumer (check `radar`, screenshot and mouse-picking code paths that offset by
the viewport rect).

Compatibility rules (already proven in this codebase, keep them): `WindowRounding = 0`
while viewports are enabled; GUI built once per frame on the left eye and replayed on the
right (`renderGUI(isLeftEye)` early-out stays); platform windows rendered after the main
GUI via the existing `renderImGuiPlatformWindows` lambda in `main.cpp`.

During Pass 0 the *existing* Scene Hierarchy window is docked into the left node and
Settings/tool windows keep working as floaters — users see the new shell immediately,
content migrates in later passes.

### 5.3 CommandRegistry — everything is a command

New files `headers/Core/CommandRegistry.h` + `src/Core/CommandRegistry.cpp`:

```cpp
namespace Core {
  struct Command {
    std::string id;            // "file.import", "view.frame_selected", "tool.measure"
    std::string title;         // "Import Files…"
    std::string category;      // "File", "View", "Tools", "Settings", …
    const char* icon = nullptr;              // FA glyph
    std::function<void()> action;
    std::function<bool()> enabled  = nullptr; // null = always
    std::function<bool()> checked  = nullptr; // for toggles (menu checkmark)
    std::string keywords;                     // extra fuzzy-match terms
  };
  class CommandRegistry {   // singleton, mirrors PluginManager style
    void add(Command c);  const std::vector<Command>& all() const;
    void run(const std::string& id);          // also bumps frecency (§8)
    // shortcut text comes from ShortcutManager at draw time (single source)
  };
}
```

Pass 0 registers the existing menu actions (file ops, create primitives, view toggles,
window toggles, standard views, lighting cycle…) and rewires the menu bar to render from
it. `ShortcutManager` actions map onto command ids (add a small `commandId ↔
ShortcutAction` table) so the palette can display live keybindings. Plugins get
`PluginContext::registerCommand(...)` in Pass 7.

Design note for the future: `run(id)` is the single choke-point through which every user
action flows. Keep it that way — a later operation log for cooperative editing (§13.4)
and macro/scripting support both hang off this one function.

### 5.4 Project file discipline

Every new file must be added to **both** `StereoVista/StereoVista.vcxproj` (`<ClCompile>`
/ `<ClInclude>`) and `StereoVista.vcxproj.filters` (matching `Gui`/`Core` filter groups) —
the plugin docs' checklist (`docs/PLUGINS.md` §9) applies to all GUI files too.

---

## 6. Outliner (Pass 1) — the heart

Replaces the object list of `GUI.cpp:2426-2980`. New files `headers/Gui/Outliner.h` +
`src/Gui/Outliner.cpp`. Window name `"Outliner"` (docked left by default).

### 6.1 Unified item model — with stable identity

```cpp
// One reference type for ANYTHING selectable in the app (also used by Inspector,
// gizmo, palette). Replaces the SelectedType/currentSelectedIndex/MeshIndex trio.
struct SceneItemRef {
  enum class Kind { None, Model, Mesh, PointCloud, Sun, PointLight, SpotLight,
                    BrushCluster, Measurement, ClipPlane, Group, Environment };
  Kind      kind = Kind::None;
  uint64_t  id   = 0;         // the object's persistent ObjectId (authoritative)
  int       index = -1;       // cached index in its owning container (fast path)
  int       sub   = -1;       // mesh index for Kind::Mesh, else -1
  bool operator==(const SceneItemRef&) const = default;
};

class Selection {                    // new: headers/Core/Selection.h (+ .cpp)
  std::vector<SceneItemRef> items;   // ordered; primary = items.back()
  // click/ctrl-click/shift-range semantics; signals: onChanged callbacks
};
```

**Stable ObjectIds (do this in Pass 1 — it is cheap now and expensive later).** Every
persisted scene object (model, point cloud, light, measurement, clip plane, brush
cluster, group) gets a `uint64_t id`, unique per scene, assigned at creation/import from
a monotonic counter stored in the scene (`Engine::Scene::nextObjectId`), serialized in
v3, and regenerated for v1/v2 files at load. Vector indices remain the hot-path accessor
but every *reference that outlives a frame* (selection, undo records, snapshots, future
collaboration deltas, palette results) resolves by id. This single decision is what makes
global undo/redo across structural changes (§13.1), snapshot object references (§13.2),
cooperative editing (§13.4) and the web viewer (§13.5) tractable — retrofitting ids
after those features exist would touch everything twice.

Migration strategy: `Selection` is authoritative; the legacy globals
(`currentSelectedType/Index/MeshIndex`) become a *mirror of the primary item*, updated by
`Selection` on every change, so gizmo/picking/main.cpp code keeps working until each
consumer migrates (finish by Pass 8). Grep checklist for consumers:
`currentSelectedType` appears in `main.cpp` (picking, gizmo, F-frame, delete shortcuts)
and throughout `GUI.cpp`.

### 6.2 Everything in the tree

Sections in order (each a collapsible root with count badge, using `StyleFor` icon/color):

1. **Environment** — sun + skybox/environment as selectable items (Inspector edits them;
   sun manipulation panel already exists, environment settings move here from Settings ▸
   Environment *as the object view* — the global category remains in Settings).
2. **Scene objects** — models (expandable to meshes), point clouds, lights. Grouping:
   - **User groups** (new, see 6.3) — primary organization, drag-and-drop.
   - Items imported from a `.scene` keep their current auto-group by `sourceScenePath`
     (`GUI.cpp:2509+`) rendered as a group with a "linked file" marker.
   - Multi-file imports (e.g. LAS tiles) arrive pre-grouped (§9.1).
3. **Annotations & tools output** — measurements (name = "P1→P2 3.42 m"), section planes,
   brush clusters. Selecting one selects it in its tool too (e.g.
   `brushTool.setActiveCluster`, existing behavior). This section grows with every future
   tool — its content is provided per-kind, not hardcoded (§13.3).

Row anatomy: `[chevron] [type icon·color] [name] …… [badges] [👁] [🔒]`.
- Eye = visibility (models/meshes/clouds/sun/lights/planes have `visible`/`enabled`
  already; add `visible` to measurements & clip planes if missing — check
  `Engine::Measurement`/`ClipPlane` in `Engine/Data.h`).
- Lock (new `bool locked` on objects): locked items ignore viewport picking/gizmo.
- The badge slot is a small stack (count, "linked file", …) — treat it as extensible; it
  will later host "live source" (§13.6) and "being edited by …" (§13.4) badges.
- Hover reveals the eye/lock; keep rows 26–28 px, full-row hit target, no checkbox column
  (the current 60 px checkbox column at `GUI.cpp:2474` goes away).

Interactions (all undoable through existing `Engine::Undo` entry points; add new record
types where missing — group ops, rename, lock):

- Multi-select: click / `Ctrl` toggle / `Shift` range. Drag = move into/out of groups,
  reorder inside a group.
- Inline rename: `F2` / double-click on name (models, clouds, lights — lights need a
  `name` field, see 6.3; fallback naming "Point Light 3" stays as default value).
- Context menu (right-click, works on multi-selection): Frame `F` · Isolate ·
  **Select Similar ▸** (same type / same source file / same material) · Show/Hide ·
  Lock · Duplicate `Ctrl+D` · Group `Ctrl+G` / Ungroup · Rename `F2` ·
  Export… (§11.2) · Delete `Del`.
- **Isolate** (new command `view.isolate`): hide everything except selection; re-run to
  restore (store the pre-isolate visibility snapshot; surface as a status-bar chip
  "Isolated · click to exit" so users never get "lost").
- Type filter chips under the search field: `[◼ Models] [◼ Clouds] [◼ Lights] [◼ Annot.]`
  (multi-toggle); search stays the existing `searchMatches` substring filter, upgraded to
  also match type nouns ("light" finds all lights).
- Drop target: dropping files onto the Outliner imports them (same path as viewport drop).

### 6.3 Data model changes (Engine)

- `uint64_t id` on every persisted object + `Engine::Scene::nextObjectId` (see 6.1).
- `Engine::SceneGroup { uint64_t id; std::string name; bool visible=true;
  bool locked=false; uint64_t parentId=0; }` + `std::vector<SceneGroup> groups` in
  `Engine::Scene`; objects get `uint64_t groupId = 0` (models, point clouds, lights,
  measurements, clip planes, brush clusters). One level of nesting is enough initially
  (parentId reserved).
- `name` fields for `PointLight`/`SpotLight` (default from index at creation).
- **Relative asset paths:** v3 stores model/point-cloud/HDR source paths *relative to the
  scene file* (absolute kept as fallback field). This makes scenes portable across
  machines — a prerequisite for cooperative editing and the web viewer (§13.4–5), and it
  fixes broken scenes when folders move. Loader tries relative-to-scene first, then
  absolute, then filename-in-scene-dir; report misses in the existing `SceneLoadReport`.
- Scene format **v3**: bump `kSceneFormatVersion`, write ids + `groups` + `groupId` +
  light names + relative paths; loader must keep reading v1/v2 forever (defaults: fresh
  ids, no groups). Update `SceneInfo`/load report counts. Snapshots
  (`Core/SnapshotManager`) must survive unchanged.

### 6.4 Acceptance (Pass 1)

- Every object type listed above appears in the tree; nothing exists only in a tool window.
- Multi-select works with gizmo (transform applies to all — extend
  `Tools::TransformGizmo` to operate on the selection set; single-object behavior
  unchanged) and with Delete/Duplicate/visibility as batch ops (single undo step each).
- Groups: create/rename/dissolve/drag-membership/visibility/persist in `.scene` (v3),
  reload correctly, old scenes still load (and get ids assigned).
- Search + filters never leave the panel empty without an explanatory `EmptyState`.

---

## 7. Inspector (Pass 2) — always the right controls

New files `headers/Gui/Inspector.h` + `src/Gui/Inspector.cpp`. Window `"Inspector"`,
docked right. The existing `render*ManipulationPanel` functions (`GUI.cpp:8095-8850`)
move here (mostly verbatim first, restyled second) and register per-kind editors:

```cpp
// Inspector dispatch: kind → editor, via a registry (not a switch) so future
// kinds/tools can add editors without touching Inspector.cpp (§13.3).
// Editors receive the full selection so they can do mixed editing.
void RenderInspector(Selection& sel);
void RegisterInspectorEditor(SceneItemRef::Kind k, InspectorEditorFn fn);
```

Layout top-to-bottom:
1. **Header card**: type icon (colored), editable name, type noun badge, quick actions
   (visibility, lock, frame). Multi-select: "3 objects (2 Models, 1 Light)".
2. **Tool card** (only while a tool is active): the active tool's options — see Pass 7.
3. **Per-kind sections** (CollapsingHeaders, state persisted per kind in prefs):
   Transform / Material / Textures / Display / Info / Export … exactly the sections that
   exist today, restyled with UiKit (`PropertyRow`, reset glyphs on hover per section).
4. **Global cards** (clearly separated, tinted, "GLOBAL" tag): contextually relevant
   global settings — point cloud selected → "Point Cloud Rendering (all clouds)": EDL,
   splatting, HQS, base size (`preferences.edlSettings`, `pointSplatSettings`,
   `pointCloudQuality`, `pointCloudBaseSize`); light selected → shadow quality summary;
   each card ends with "Open in Settings →" (deep-links via command
   `settings.open:<category>`). Editing here edits the same preference (one source of
   truth), so mark rows with the settings-modified dot too.
5. **Empty state** (nothing selected): friendly panel — "Select something in the scene or
   Outliner", plus the 3 most useful contextual actions (Import, Create, Recent scenes).

Rules:
- Multi-edit: common kind → shared widgets apply to all (loop + one undo gesture via
  `PanelEditTracker` extended to selection sets); mixed kinds → header + Transform only
  (position delta applies relatively).
- Every numeric row supports the existing Ctrl+click-to-type; drag speed defaults sane.
- Per-section **reset**: hover shows `ResetGlyph` in the section header → restores that
  section of the object to its creation/import defaults where meaningful (Transform → identity,
  Material → import values) — always undoable, never silent.
- **Texture drag-apply:** dropping an image file onto a model's Textures section (or onto
  the model in the viewport, Pass 5) assigns it — pick the slot by filename convention
  (`*_normal`, `*_rough`, `*_ao`, else albedo), with an undoable toast "Applied as normal
  map — [Change slot]". Small feature, large "it just knows" payoff.
- The mesh sub-panel (`renderMeshManipulationPanel`) becomes the editor for
  `Kind::Mesh`.
- Sun/Environment editors absorb `renderSunManipulationPanel` + the skybox controls
  (object-ish parts) from Settings ▸ Environment.

---

## 8. Command palette & global search (Pass 4)

New files `headers/Gui/CommandPalette.h` + `src/Gui/CommandPalette.cpp`.

- **Open:** `Ctrl+K` (register in ShortcutManager; also `Ctrl+P` alias), menu-bar search
  icon, status-bar hint. Centered top overlay (like VS Code), 560 px wide, dimmed scrim,
  `Esc` closes, fully keyboard navigable.
- **Sources (providers)**, merged and ranked — providers are a small registry so future
  features (snapshots already, later: collaborators, help articles) plug in:
  1. **Commands** — from CommandRegistry (icon, title, category, live shortcut, enabled).
  2. **Scene objects** — every Outliner item (icon+color; Enter = select, `Shift+Enter` =
     select+frame). Resolves via ObjectId, so results stay valid while the list changes.
  3. **Settings** — an index built in Pass 6 (`SettingsField { label, category, keywords }`);
     Enter opens Settings at that category and flash-highlights the row.
  4. **Snapshots** — "Restore snapshot …" with thumbnail in the result row.
  5. **Recent scenes** — open/merge.
  6. **Help** — "Shortcuts reference", links (docs), theme picker.
- **Fuzzy match:** reuse and generalize `snapshotFuzzyMatch` (`GUI.cpp:7267`) into
  `UiKit::FuzzyMatch(needle, haystack, &score, outHighlights)` with subsequence scoring +
  highlight ranges rendered bold/accent.
- **Frecency:** `CommandRegistry::run` records use; ranking blends match score with
  recency+frequency (simple decayed counter persisted in prefs). Empty query shows the
  top-frecency commands, current selection actions, and tips.
- Prefix filters (discoverable via placeholder): `>` commands only, `#` objects only,
  `:` settings only.

The palette is *the* global search the brief asks for; individual panels keep their local
search fields (Outliner, Snapshots, History, Settings) but all use `UiKit::SearchInput`
and the same fuzzy matcher, so behavior feels identical everywhere.

---

## 9. Smart import, Welcome Hub & session safety (Pass 5)

### 9.1 ImportService — one entry point, zero questions

New files `headers/Core/ImportService.h` + `src/Core/ImportService.cpp`. Consolidates the
three import paths that exist today (menu model / menu point-cloud / drop handler at
`GUI.cpp:1689-1728`, `1733-1806`) into one:

```cpp
namespace Core {
  struct ImportPlan {            // what we DETECTED, not what we ask
    enum class Action { Model, PointCloud, PointCloudLASGroup, Scene,
                        EnvironmentHDR, Texture, Unknown
                        /* future: LiveCapture (§13.6), … — append only */ };
    Action action; std::string path; std::string reason; // "by extension", "sniffed ASCII PLY"
  };
  std::vector<ImportPlan> Plan(const std::vector<std::string>& paths); // pure, testable
  void Execute(std::vector<ImportPlan> plans);   // toasts, undo, selection, framing
}
```

- **Detection:** extension table first (`.obj/.fbx/.3ds/.gltf/.glb` → Model; point-cloud
  set as today incl. `.las/.laz` grouping; `.scene` → Scene flow; **new:** `.hdr/.exr` →
  EnvironmentHDR, images → Texture). For ambiguous text (`.txt/.ply` without extension
  hints) sniff the first bytes (the binary-PLY reader already parses headers — reuse).
  Unknown → one warning toast listing the file, never a modal.
- **Duplicate awareness:** if a planned path is already loaded in the scene (match by
  absolute path), don't silently double-load — one toast: "*bridge.obj* is already in the
  scene — [Add another copy] [Skip]" (batch-aware: "3 of 7 already loaded…").
- **Auto-grouping:** any multi-file import lands as one Outliner group, auto-named from
  the files' longest common filename prefix ("survey_north_01…04.las" → "survey_north"),
  falling back to the folder name. One undo step for the whole import.
- **Display names:** default object names are prettified (strip extension, `_`/`-` → space)
  while the untouched source path stays in Inspector ▸ Info. "scan_final_v2.ply" reads as
  "scan final v2" in the tree.
- **Menu:** `File ▸ Import Files… (Ctrl+I)` with one combined file-dialog filter (keep
  "3D Models"/"Point Clouds" as sub-filters in the dialog). The two old entries collapse.
- **Scene files:** keep the replace/merge/ask behavior (`SceneLoadingBehavior` pref), but
  the ask-dialog gains "☑ Remember my choice" writing that pref (the pref & dialog exist —
  `GUI.cpp:1949-1997` — add the checkbox, remove the tip text). Before **Replace**, offer
  an automatic safety snapshot (§13.2) — pref-gated, default on, one line in the dialog.
- **EnvironmentHDR:** action-toast "Use *sunset.hdr* as environment? [Set environment]
  [Dismiss]" (+ "always" checkbox → new pref `importHdrAsEnvironment` tri-state).
- **After any import:** select imported items; if scene *was* empty → frame all
  (`frameSelectedObject()` exists, add `frameAll`); spawn animation prefs respected;
  aggregate toast ("Imported 3 point clouds · 41.2M points").
- All entry points (drop, menu, hub, Outliner-drop, palette) call this service. Drop while
  GUI hidden already re-shows the GUI (`GUI.cpp:1632-1637`) — keep.

### 9.2 Welcome Hub

New files `headers/Gui/WelcomeHub.h` + `src/Gui/WelcomeHub.cpp`. Replaces
`renderEmptySceneHint` (`GUI.cpp:1167`).

- Shown when `scene is empty && no files importing`; centered floating card in the
  viewport hole (non-modal, doesn't steal viewport input outside its rect), fades 150 ms.
- Content: app icon + name; **Recent scenes** as up-to-4 thumbnail cards (thumbnail from
  scene's snapshot if present — `SnapshotManager` stores thumbnails; else icon), each with
  name + object counts from `Engine::loadSceneInfo` (exists); **[Import Files…]** primary
  button + "or drop files anywhere"; primitive row (Cube/Sphere/Plane/…); footer:
  "`Ctrl+K` anything · `G` hide UI · Shortcuts reference".
- **Recovery card:** when an autosave newer than the last clean exit exists (§9.3), the
  hub leads with "Restore last session — *office.scene*, 14 objects, 2 min ago
  [Restore] [Discard]".
- The old tiny hint text remains as the GUI-hidden variant (it also serves `showGui==false`).

### 9.3 Autosave & crash recovery (new)

Small, loved, and cheap with the pieces we have:
- Timer-based autosave (default every 5 min, pref-able, off-able) of the current scene to
  `StereoVista/autosave/<scene-or-untitled>.autosave.scene` using the existing
  `saveScene` with geometry *references* (scenes store paths, not blobs — cheap even for
  huge clouds). Skip while streaming loads are in flight.
- Write a `session.lock` on start, remove on clean exit; stale lock + autosave present →
  hub recovery card (never a modal interrogation on startup).
- Status bar whispers "Autosaved · 14:02" for 3 s. Manual `Ctrl+S` clears the autosave.

---

## 10. Settings overhaul (Pass 6)

Keep the window + sidebar categories (they're right); make it searchable, resettable,
honest about state. Extract to `headers/Gui/SettingsWindow.h` + `src/Gui/SettingsWindow.cpp`
(move `renderSettingsWindow` + friends out of `GUI.cpp`).

- **Defaults framework:** `static const GUI::ApplicationPreferences kDefaultPrefs{};` is
  the single source of defaults (the struct already encodes them as initializers —
  `GuiTypes.h:123-463`). Helpers: `bool IsModified(field)`, per-category
  `ResetCategory(cat)` copying member-subsets from `kDefaultPrefs`, `Reset All…`
  (confirm dialog). After any reset: re-apply side effects (the `settingsChanged` +
  mirror-global pattern in `renderSettingsWindow` — e.g. `::currentLightingMode`,
  `updateSkybox()` — must run; centralize as `ApplyPreferences(categoryMask)`).
- **Row registry for search:** wrap rows in a tiny helper that registers
  `{label, category, keywords}` into the settings index (used by palette + in-window
  search). In-window `SearchInput` filters live: matching categories glow in the nav,
  content shows only matching sections, match highlight via `FuzzyMatch` ranges.
- **Modified dots + row reset:** rows where `IsModified` show an accent `Dot`; hover
  reveals `ResetGlyph` per row. Section headers get "Reset section" in a `⋯` menu.
- **Re-grouping** (nothing removed): `Camera` menu's sliders (speed/sensitivity/orbit
  modes, `GUI.cpp:2237-2301`) → Settings ▸ Camera & 3D (menu keeps only actions);
  Cursor Settings window content becomes Settings ▸ **3D Cursor** category (its 4 tabs
  become sections; the cursor *preview* (`CursorPreview3D`) embeds at top — it already
  renders to a texture, verify it draws correctly inside a dockable window on any
  monitor); `ShowIconTestWindow` and other dev leftovers move behind a hidden Developer
  category (palette command `dev.icons`).
- **Theme picker** stays in Display, upgraded with the existing swatch API
  (`GetGuiThemeSwatches`) to show 7 theme cards + light/dark quick toggle.
- Every category ends with a quiet footer: "Reset <Category> to defaults".

---

## 11. Tools, export & plugins coherence (Pass 7)

### 11.1 Tools are registry entries, not hardcoded modes

**Tool = mode + Outliner output + Inspector card.** Introduce `ToolManager`
(`headers/Tools/ToolManager.h`) — a **registry**, not an enum: tools self-describe
(`{ id, name, icon, shortcutAction, category }`) and at most one is active. The viewport
toolbar, Tools menu, palette entries and the status-bar chip all render from the
registry, so the *next ten tools* (point-cloud cropping/cleaning, mesh cleanup, batch
annotation, alignment — see §13.3) appear everywhere automatically with zero UI edits.
Activate/deactivate are commands; `Esc` deactivates; shortcuts stay (`P` for section
planes etc. — see `ShortcutManager.h`).

- **Measurement** (already a plugin): activation via toolbar/palette; options
  (snapping, units, label size…) render in the Inspector Tool card through a new
  optional plugin hook `onRenderInspector(PluginContext&)`; measurements list lives in
  the Outliner (Annotations). The floating window remains available (docked tab next to
  Inspector by default) for users who prefer it — same widgets, one implementation.
- **Section planes:** same treatment; planes are Outliner items (selectable → Inspector
  edits plane transform/extent; gizmo moves them — `ClipPlaneTool` + `TransformGizmo`
  already interoperate; verify).
- **Brush:** same; clusters already appear in the hierarchy — they move into Annotations
  with the standard row anatomy.
- **Scene Manager** window dissolves: recents/info → Welcome Hub + File menu; save
  options → Settings ▸ Scenes (new small category or under Display — implementer's
  choice); toasts already report save/load.

### 11.2 Export center (new)

"Exporting" grows beyond point clouds, so build the mirror of ImportService now:
`headers/Core/ExportService.h` + `src/Core/ExportService.cpp` — a registry of exporters
`{ ObjectKind, format, extension, fn }`. Initially registered: point-cloud export
(exists today in the manipulation panel, `GUI.cpp:8544`), screenshots / stereo
screenshots (exist), scene save-as. Surfaces:
- `File ▸ Export…` — selection-aware (exports what's selected; whole scene when nothing is).
- Outliner context menu **Export…** — works on multi-selection (batch export, one folder
  dialog, progress in the status bar).
- Inspector Export sections call the same service.
Future exporters (mesh via Assimp, web-viewer package §13.5, measurement CSV) become
one registration each.

### 11.3 Plugins

**PluginContext extensions:** `registerCommand(...)`, `registerTool(...)`,
`onRenderInspector(...)` hook, `ObjectKind`-styled toast icons. Update `docs/PLUGINS.md`
and the `CrosshairPlugin` template to demonstrate the new hooks; plugin windows get a
UiKit style guide section. Existing plugin API must keep working unchanged (hooks are
additive).

---

## 12. Viewport, overlays, status bar, hints (Pass 8)

- **Unified viewport toolbar:** merge the two floating strips (`renderViewModeToolbar`,
  `renderGizmoViewportToolbar`) into one adaptive top-center strip inside the viewport
  hole: gizmo segmented control | view dropdown (standard views + Frame `F` + Reset) |
  shading dropdown (lighting mode, wireframe, unlit) | camera dropdown (speed, FOV quick
  slider) | tools segment (from ToolManager) | right side: layout reset + hide-UI.
  Overflow collapses into `⋯` when narrow (measure available width; this is the "layout
  optimized" requirement — no clipped buttons ever).
- **Status bar** (from Pass 0 shell) completes: left = scene stats (objects, total
  points — reuse `formatPointCount`), selection summary; center = background progress
  (point-cloud streaming overlay folds in here, keeping its per-file detail as a hover
  popup); right = active tool chip, lighting-mode chip (click = cycle, matches `L`),
  stereo indicator (quad-buffer active?), FPS (click toggles the detailed perf overlay),
  reserved presence slot (§13.4).
- **Scene intelligence service** (`headers/Core/SceneStats.h`): cheap per-second stats —
  object/triangle/point counts, estimated VRAM, draw calls, streaming state. Feeds the
  status bar (hover = breakdown popup), the hub cards, and the performance-guardian
  hints below. One implementation, no per-frame recomputation.
- **Shortcut overlay:** hold `F1` → translucent cheat-sheet overlay of the *current*
  keymap (grouped like the Settings ▸ Shortcuts reference, live-generated from
  ShortcutManager so it is never stale). Release to dismiss; `Shift+F1` pins it.
- **Overlays restyle** with tokens: perf overlay, radar scope frame, measurement labels
  (`drawMeasurementLabels`) get the semantic colors; toasts gain action buttons
  (`HintToast`) and stack limit 3 + "+2 more".
- **HintEngine** (the "alive" layer): `headers/Gui/Hints.h` — rate-limited (≥1 per
  session gap), context-triggered, dismiss-forever (persisted set in prefs), all behind
  `preferences.enableHints` (default on). Launch set of hints:
  1. First model imported → "Press `F` to frame the selection."
  2. FPS < 20 for 10 s with an expensive feature on (from SceneStats) → "Radiance ray
     count is high for this scene — [Lower it] [Keep]" (performance guardian; never
     auto-changes anything).
  3. Huge point cloud + HQS off → suggest High-Quality Shading.
  4. 5+ ungrouped objects → "Select related objects and press `Ctrl+G` to group them."
  5. User opened Settings 3× in a session → "`Ctrl+K` can jump straight to any setting."
  6. First measurement placed → "Measurements live in the Outliner under Annotations."
- Legacy inset aliases (`g_dockLeftWidth`/`g_dockTopHeight`) removed; all consumers on
  `g_viewportInsets`.

---

## 13. Built for what's coming

Known roadmap beyond this redesign: **more editing / measurement / export tools**,
**cooperative editing**, a **web viewer**, and **live viewport-capture sources** from
other programs. None of these are built now — but the redesign lays their rails, and two
of them (History, Snapshots) are near-term enough to be their own pass (Pass 3).

### 13.1 Global undo/redo + History panel (Pass 3 — near-term)

Goal: `Ctrl+Z` is *never* wrong. Two work items:

1. **Coverage audit & close the gaps.** Grep every user-visible mutation and route it
   through `Engine::UndoManager`. Known-uncovered today (verify each): visibility & lock
   toggles, group create/rename/membership, object rename, measurement add/delete,
   clip-plane add/delete (`addClipPlaneAtCursor`), brush-stroke cluster edits, snapshot
   *restore* (restoring a snapshot must itself be one undoable step), import batches as
   single steps, environment/sun edits (sun tracker exists). Undo records reference
   objects by **ObjectId** (§6.1), not index, so records survive reordering/deletion.
   Settings stay *not* undoable by design — they are resettable instead (§10).
2. **History panel** (`headers/Gui/HistoryPanel.h` + `src/Gui/HistoryPanel.cpp`, window
   `"History"`, default docked as a tab with the Inspector): chronological list — kind
   icon (UiKit `StyleFor`), human label ("Moved 3 objects", "Imported survey_north (4
   files)"), relative time — click an entry to jump to that state (implemented as
   repeated undo/redo on the existing stack; no new engine needed), marker line on the
   last-saved state, search field, depth limit pref (default 200). The Edit-menu history
   list (`GUI.cpp:2035`) becomes a thin "recent 5 + Open History" view of the same data.

This pass also hardens the *gesture* pattern: everything new must use `PanelEditTracker`
(or batch equivalents) — one undo entry per user intent, never per frame.

### 13.2 Snapshots as first-class states (Pass 3 — near-term)

The existing `Core::SnapshotManager` — named snapshots with thumbnails, tags, fuzzy
search, and selective restore flags (camera / scene / tool state), carried over from the
original GL build — is the **reference implementation**; the redesign must keep 100 % of
its capability. Pass 3 work:
- Restyle the panel with UiKit (cards + tag chips it already half-has), default dock as a
  tab with the Inspector, palette provider ("Restore snapshot …", §8), hub thumbnails (§9.2).
- **Safety snapshots:** one-click "Snapshot now" in the status bar; automatic offer
  before scene Replace (§9.1) and before "Reset All settings" — pref-gated, default on.
- Snapshot restore is undoable (one History entry, §13.1).
- Future (do not build now, keep the door open): A/B compare (split-view slider between
  current state and a snapshot), snapshot notes/annotations, export snapshot as image +
  scene-state file. The panel layout should leave room for a compare affordance per card.

### 13.3 Many more tools (editing, measurement, export, file editing)

The recipe a future tool follows — this is the extensibility contract; Passes 0–8 must
keep each step true:
1. Register a **tool** (`ToolManager`, §11.1) → toolbar, Tools menu, palette, status chip.
2. Register **commands** (§5.3) → shortcuts (ShortcutManager), palette, menus.
3. If it produces objects: add an **ObjectKind** (append-only, §5.1), an Outliner
   Annotations provider (§6.2), an **Inspector editor** (registry, §7), undo record
   types (by ObjectId), and v3+ serialization for persistence.
4. If it imports/exports: register an **ImportPlan action** (§9.1) / **exporter** (§11.2).
5. UI built from **UiKit** only; docs example: `docs/PLUGINS.md` + `CrosshairPlugin`.

A tool that follows the recipe touches *zero* existing UI files. Anticipated entrants
(size the registries' ergonomics for them): point-cloud crop/clean (section-plane +
brush infra reuse), mesh cleanup/decimation, alignment/registration tool, annotation
notes, measurement chains/areas/volumes, batch exporters, file "edit & re-save" round
trips (edit a cloud, write it back to LAS/PLY).

### 13.4 Cooperative editing (provisions only)

What we lay down now (cheap), so collaboration is an engine project later, not a UI
rewrite:
- **Stable ObjectIds everywhere** (§6.1) — the non-negotiable prerequisite.
- **Single action choke-point** — user intent flows through `CommandRegistry::run` and
  undo records; together they already form an *operation log* shape (op, target ids,
  before/after). A sync layer can serialize exactly this.
- **Relative-path, self-describing scene v3** (§6.3) — shareable project files.
- **Reserved UI slots**: status-bar presence segment (§4), Outliner row badge stack
  ("being edited by…", §6.2), toast pattern for conflicts ("Ana moved *Tree_04* —
  [Keep theirs] [Keep mine]"). Do not build the features; just don't design the slots away.

### 13.5 Web viewer (provisions only)

- Scene v3 stays plain, self-describing JSON with relative asset paths — a web client can
  parse it without our C++.
- `Export ▸ Web package…` reserved as a future exporter registration (§11.2): folder/zip
  of scene JSON + assets (+ later a JS viewer shell).
- Document UiKit tokens (colors per ObjectKind, spacing, type scale) in this file's
  design-system section as *the* palette, so the web UI can mirror the desktop language.
- Snapshot thumbnails already give a web gallery for free — keep them small PNGs on disk.

### 13.6 Live viewport-capture sources (provisions only)

"Load rendered objects from other programs via viewport captures" means objects whose
content *updates from an external feed* (shared-texture/Spout-style capture, or
RGB+depth grabs re-projected as textured quads / point sets). Provisions:
- `ObjectKind::LiveCapture` reserved (§5.1); Outliner badge "live" reserved (§6.2).
- `ImportPlan::Action` is append-only; a `LiveCapture` action slots in later (§9.1).
- The Outliner/Inspector must never assume objects are static: no caching of names/counts
  across frames beyond what's invalidated on change (this is true of ImGui anyway — keep
  it true).
- Renderer-side (out of UI scope): such sources are just another object type feeding the
  existing model/point-cloud draw paths.

---

## 14. Engineering guardrails (every pass)

1. **Windows/MSVC only; agents usually cannot compile.** Therefore: small, reviewable
   commits; move code wholesale before restyling it; prefer additive files over surgery
   in `main.cpp`; when touching `main.cpp`/`GUI.cpp`, keep `extern` contracts and
   signatures stable (`renderGUI(bool, ImGuiViewportP*, ImGuiWindowFlags, Shader*)` stays).
   Write C++17 that compiles under `/std:c++17` MSVC — no designated initializers with
   out-of-order fields, no GNU extensions, mind `std::filesystem` usage patterns already
   in the file. Double-check every new file is in `.vcxproj` **and** `.filters`.
2. **Never break:** stereo left/right GUI replay, `g_sharedPassesDone` shared passes,
   multi-viewport drag-out (+ GL context backup/restore lambda), GUI-hidden mode (`G`),
   GUI scale 0.5–2.0×, all 7 themes (test light *and* dark), preferences/shortcuts/scene
   JSON backward compatibility (old files must load with sane defaults; never reorder
   persisted enum values — see the warning in `imgui_sytle.h:58-60`), plugin API,
   3DConnexion sync, undo for every user-visible mutation.
3. **Identity & undo discipline (new):** every persisted object type gets an ObjectId;
   every reference that outlives a frame resolves by id; every user-visible mutation is
   one undoable step. New features that skip these are rejected in review.
4. **Preferences:** every new UI state that should survive restart goes through
   `savePreferences`/`loadPreferences` in `main.cpp` with a default that reproduces
   today's behavior when the key is missing.
5. **Decompose as you go:** each pass extracts its area out of `GUI.cpp` into the new
   `src/Gui/*.cpp` files; `GUI.cpp` shrinks every pass and ends as the thin `renderGUI`
   orchestrator. Never leave two live copies of a panel.
6. **Manual test checklist per pass** (run on Windows when possible; otherwise state
   clearly in the PR that visual verification is pending): launch clean (no
   `preferences.json`), launch with legacy prefs, import each format (obj/gltf/fbx, xyz,
   ascii+binary ply, las multi-tile, laz, h5, pcb), drop-import, dock/undock/float/drag-out
   every panel, reset layout, resize window to 800×600 and 4K, toggle `G`, cycle `L`,
   undo/redo a session of edits **including a History-panel jump**, save+reload a v2
   scene and a new v3 scene, autosave recovery flow, quad-buffer stereo smoke test if
   hardware allows.
7. **Copywriting:** sentence-case labels, verbs on buttons, tooltips explain *why* not
   just *what* (the `DrawHelpMarker` texts are good — keep that bar), no jargon in
   first-level UI ("Point size" not "Splat radius clamp"), units on every number.

---

## 15. Pass Plan

Order matters (foundation → heart → time-travel → intelligence → polish), but passes 4–7
are largely parallelizable after Pass 2. Sizes: S < 300 LoC, M < 1200, L < 3000 touched.

| # | Pass | Contents (details in section) | Size | Depends on |
|---|---|---|---|---|
| 0 | **Foundation** | UiKit extraction+tokens, AppShell dockspace + 4-side insets, status-bar shell, CommandRegistry + menu rewire, vcxproj wiring | L | — |
| 1 | **Outliner + data model** | §6: **ObjectIds**, item model, Selection, groups (scene v3, relative paths), multi-select, rename, context menus, filters, isolate, lock | L | 0 |
| 2 | **Inspector** | §7: panel extraction, per-kind editor registry, multi-edit, global cards, per-section reset, texture drag-apply, empty states | L | 1 |
| 3 | **History & Snapshots** | §13.1–13.2: undo coverage audit (by ObjectId), History panel, Snapshots restyle/integration, safety snapshots | M | 1 |
| 4 | **Command palette** | §8: palette UI, providers (commands/objects/settings/snapshots/recents), fuzzy lib, frecency, menu search icon | M | 0 (better after 1+3) |
| 5 | **Smart import + Hub + autosave** | §9: ImportService (dupes, auto-group, prettified names), unified menu/drop/dialog, HDR/texture handling, Welcome Hub, autosave & recovery | M | 0 (hub thumbnails benefit from 3) |
| 6 | **Settings** | §10: search index, defaults framework, resets + modified dots, re-grouping (camera/cursor), theme cards | L | 0 |
| 7 | **Tools, export & plugins** | §11: ToolManager registry, Inspector tool cards, ExportService + Export center, plugin hook additions, Scene Manager disposition, PLUGINS.md update | M | 2 |
| 8 | **Viewport & alive** | §12: unified toolbar, status bar completion, SceneStats, F1 shortcut overlay, overlay restyle, HintEngine, remove legacy insets | M | 0 (toolbar); 7 (tool chip) |
| 9 | **Harmony audit** | Full sweep: spacing/icon/color audit vs UiKit, copy audit, empty-state audit, §13.3 recipe verification (walk a mock tool through it), perf (no per-frame allocs in new panels), docs (CLAUDE.md GUI section rewrite, PLUGINS.md), kill dead code | M | all |

Each pass's PR description must include: what changed vs this plan (deviations + why),
the manual-test checklist status, and the Status Board update.

---

## 16. Status Board

Keep this table truthful — it is the coordination point between agent passes.

| Pass | State | Branch/PR | Notes & deviations |
|---|---|---|---|
| Plan | ✅ done | `claude/gui-redesign-ux-hadxj2` | This document. Rev 2: roadmap/future-proofing integrated (ids, History/Snapshots pass, registries, export center, autosave, §13 provisions). |
| 0 Foundation | ⬜ not started | | |
| 1 Outliner + data model | ⬜ not started | | |
| 2 Inspector | ⬜ not started | | |
| 3 History & Snapshots | ⬜ not started | | |
| 4 Palette | ⬜ not started | | |
| 5 Import + Hub + autosave | ⬜ not started | | |
| 6 Settings | ⬜ not started | | |
| 7 Tools, export & plugins | ⬜ not started | | |
| 8 Viewport & alive | ⬜ not started | | |
| 9 Harmony | ⬜ not started | | |

### Decisions log

- 2026-07-12 — Two-panel heart confirmed: Outliner (left) + Inspector (right) as separate
  dockable windows over a passthru-central-node DockSpace; scene keeps rendering directly
  to the backbuffer (no render-to-texture).
- 2026-07-12 — CommandRegistry chosen as the coherence mechanism (menus, palette,
  shortcuts, toolbars all consume it).
- 2026-07-12 — No capability removal anywhere; re-grouping + progressive disclosure only.
- 2026-07-12 — **Stable ObjectIds** on all persisted objects from Pass 1 (prerequisite
  for global undo, snapshots-by-reference, cooperative editing, web viewer).
- 2026-07-12 — History & Snapshots promoted to their own early pass (Pass 3) per product
  priority; existing SnapshotManager (from the GL build) is the reference — no regressions.
- 2026-07-12 — Tools/importers/exporters/inspector-editors are registries with a
  documented recipe (§13.3); ExportService added as the mirror of ImportService.
- 2026-07-12 — Provisions (slots only, no implementation) reserved for cooperative
  editing, web viewer, and live viewport-capture sources (§13.4–13.6).
