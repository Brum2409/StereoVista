# StereoVista UI/UX Redesign — Master Plan

**Status: living document.** This is the guideline for the full GUI remake. It is executed
in multiple agent passes (see [§14 Pass Plan](#14-pass-plan)). Each pass updates the
[Status Board](#15-status-board) at the bottom.

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

---

## 2. Where we are today (code inventory)

The GUI was already *partially* modernized — reuse this, don't rebuild it:

| Asset | Where | State |
|---|---|---|
| Theme system, 7 themes + semantic palette `g_StyleColors` (primary/accent/success/…) | `headers/libs/imgui/imgui_sytle.h`, `imgui_style.cpp` (`ApplyGuiTheme`, swatches API) | **Keep & extend** |
| Fonts: regular/bold/header/small/mono + FontAwesome 5 icon font, DPI + user scale (0.5–2.0×) | `imgui_style.cpp` (`g_Fonts`, `g_GuiScale`), `IconsFontAwesome5.h` | **Keep** |
| Custom widgets: `DrawToggleSwitch`, `DrawNavItem`, `DrawSectionHeader`, `DrawPanelTitle`, `DrawInlineIcon`, `MenuBarSeparator`, tag chips (snapshots) | `src/Gui/GUI.cpp:498-668`, `7339` | **Extract into UiKit** (Pass 0) |
| Toasts (bottom-center, typed) | `GUI.cpp` (`GUI::ShowToast`, `renderToasts`) | **Keep** |
| Overlays: perf (bottom-right), point-cloud streaming (bottom-left), empty-scene hint, measurement labels, view-mode toolbar (top-left), gizmo toolbar (top-right) | `GUI.cpp:203-420`, `894-1224`, `7968` | **Restyle/unify** (Pass 7) |
| Settings window: sidebar nav, 7 categories | `GUI.cpp:3155-6193` (`SettingsCategory`, `kNavEntries`) | **Overhaul** (Pass 5) |
| Scene Hierarchy: fixed left panel, search, visibility, per-source-scene grouping, meshes, inline Properties child | `GUI.cpp:2426-3097` | **Replace** with Outliner + Inspector (Passes 1–2) |
| Manipulation panels per type (model/mesh/point cloud/sun/point light/spot light/brush cluster) | `GUI.cpp:8095-8850` | **Become Inspector editors** (Pass 2) |
| Undo: gesture-grained `PanelEditTracker`, full UndoManager | `GUI.cpp:460-493`, `Core/UndoManager` | **Keep — mandatory pattern** |
| Plugin system: `PluginContext`, menu/UI/input/viewport hooks, `REGISTER_PLUGIN` | `headers/Plugins/*`, `docs/PLUGINS.md` | **Keep, extend** (Pass 6) |
| Shortcuts: rebindable `ShortcutAction` enum + `shortcuts.json` + editor in Settings | `headers/Engine/ShortcutManager.h` | **Keep, feed from CommandRegistry** |
| Scene persistence v2 (metadata, environment, sun, save options, load report, recents) | `headers/Core/SceneManager.h` | **Extend to v3** (groups, names) |
| Tool windows: Brush, Measurement (plugin), Section Planes, Snapshots (search/tags/thumbnails), Scene Manager, Cursor Settings (4 tabs) | `GUI.cpp:6199-7965` | **Unify** (Pass 6) |
| ImGui **docking branch** v1.91.1, viewports (OS drag-out) enabled; **no root DockSpace yet** — hierarchy is a pinned window, floats dock only onto each other | `imgui_style.cpp:135-141`, CLAUDE.md | **Add AppShell DockSpace** (Pass 0) |
| Viewport reservation: `g_dockLeftWidth`/`g_dockTopHeight` → `g_viewportX/TopInset/Width/Height` + offscreen-target resize | `Gui.h:74`, `main.cpp:4586-4619` | **Generalize to 4-sided insets** (Pass 0) |
| Stereo GUI contract: ImGui frame built once on the **left** eye, draw data replayed for the right eye | `GUI.cpp:1618-1623` (`renderGUI(isLeftEye,…)`) | **Must be preserved** |

Selection state today is three globals in `main.cpp:271-291` (`SelectedType` enum +
`currentSelectedIndex` + `currentSelectedMeshIndex`) — single selection only.

Window-open state is `bool show*Window` globals (`main.cpp:262-270`).

**Build reality:** MSVC-only (`StereoVista.sln`), no CI, no automated tests. Agent passes
usually cannot compile — see [§13 Engineering guardrails](#13-engineering-guardrails).

---

## 3. The target experience (narrative)

**First launch.** A clean window: menu bar, Outliner (left, mostly empty), viewport with a
subtle grid/skybox, Inspector (right, friendly empty state), thin status bar. Centered in
the viewport floats the **Welcome Hub** — a calm card, not a modal: recent scenes as
thumbnail cards, one big "Import files…" affordance ("or just drop files anywhere"),
a row of primitive buttons, and "Press `Ctrl+K` for anything". It disappears the moment
the scene has content and comes back on `File ▸ New`.

**Import.** The user drops seven files: three LAS tiles, an FBX, a PLY, an HDR and a PNG.
No questions. The status bar shows progress; LAS tiles are grouped and georeferenced
together (existing behavior), the PLY is sniffed (ASCII vs binary is already handled), the
HDR is offered as environment ("Set as environment? [Yes] [Just import]" — one toast with
buttons, remembered if the user ticks "always"), the PNG gets a helpful toast ("Images
can be used as textures — select a model to apply"). Everything lands in the Outliner,
selected; the camera frames the new content because the scene was empty.

**Organize.** In the Outliner the user lasso/Ctrl-selects the LAS tiles, presses `Ctrl+G` —
"Group: *Survey North*" (inline rename active immediately). Eye icons toggle visibility,
per-group too. Drag objects between groups. `F2` renames anything, including lights.
Right-click gives the expected verbs: Frame, Isolate, Duplicate, Group, Rename, Delete —
all undoable.

**Inspect.** Selecting a point cloud shows the Inspector: name + type badge, Transform,
Display (point size, splatting…), Info (point count, source file), Export — and a clearly
separated, differently-tinted **"Global — Point Cloud Rendering"** card (EDL, HQS, Morton
resort) marked as affecting *all* clouds, with a "Open in Settings" link. Multi-select
shows shared properties and applies edits to all (one undo step).

**Find anything.** `Ctrl+K`: typing "shad" lists *Toggle Shadows* (command), *Shadow
quality → Settings ▸ Rendering* (setting, deep-link), *Shadow catcher plane* (scene
object). Typing "sep" finds *Eye Separation*. Enter executes / navigates / selects.

**Tools.** Measure, Section, Brush are **modes**, not windows: activating one (toolbar,
menu, palette or shortcut) highlights it in the toolbar, shows its options as a "Tool"
card at the top of the Inspector, drives viewport interaction, and `Esc` exits. Their
output (measurements, planes, clusters) lives in the Outliner like everything else.
Every panel can still be popped out to its own OS window and docked anywhere.

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
  draggable out of the OS window (multi-viewport stays enabled). Tool/aux panels
  (Snapshots, Cursor, plugins…) default to docking as tabs with the Inspector.
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
  contextual hint slot. It is a window docked into a bottom node with tab bar hidden.

Panel identity: keep stable ImGui window *names* — `"Outliner"`, `"Inspector"`,
`"Settings"`, `"Snapshots"`, … — since dock layout persistence keys on them.

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
  // Semantic object styling — THE source of truth for icons+colors everywhere
  enum class ObjectKind { Model, Mesh, PointCloud, Sun, PointLight, SpotLight,
                          Group, Measurement, ClipPlane, BrushCluster, Snapshot,
                          Environment, Camera, Tool, Setting, Command, File };
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
`Gui.h`'s `g_dockLeftWidth/g_dockTopHeight` as deprecated aliases until Pass 7 removes
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
    void run(const std::string& id);
    // shortcut text comes from ShortcutManager at draw time (single source)
  };
}
```

Pass 0 registers the existing menu actions (file ops, create primitives, view toggles,
window toggles, standard views, lighting cycle…) and rewires the menu bar to render from
it. `ShortcutManager` actions map onto command ids (add a small `commandId ↔
ShortcutAction` table) so the palette can display live keybindings. Plugins get
`PluginContext::registerCommand(...)` in Pass 6.

### 5.4 Project file discipline

Every new file must be added to **both** `StereoVista/StereoVista.vcxproj` (`<ClCompile>`
/ `<ClInclude>`) and `StereoVista.vcxproj.filters` (matching `Gui`/`Core` filter groups) —
the plugin docs' checklist (`docs/PLUGINS.md` §9) applies to all GUI files too.

---

## 6. Outliner (Pass 1) — the heart

Replaces the object list of `GUI.cpp:2426-2980`. New files `headers/Gui/Outliner.h` +
`src/Gui/Outliner.cpp`. Window name `"Outliner"` (docked left by default).

### 6.1 Unified item model

```cpp
// One reference type for ANYTHING selectable in the app (also used by Inspector,
// gizmo, palette). Replaces the SelectedType/currentSelectedIndex/MeshIndex trio.
struct SceneItemRef {
  enum class Kind { None, Model, Mesh, PointCloud, Sun, PointLight, SpotLight,
                    BrushCluster, Measurement, ClipPlane, Group, Environment };
  Kind kind = Kind::None;
  int  index = -1;        // index in its owning container
  int  sub   = -1;        // mesh index for Kind::Mesh, else -1
  bool operator==(const SceneItemRef&) const = default;
};

class Selection {                    // new: headers/Core/Selection.h (+ .cpp)
  std::vector<SceneItemRef> items;   // ordered; primary = items.back()
  // click/ctrl-click/shift-range semantics; signals: onChanged callbacks
};
```

Migration strategy: `Selection` is authoritative; the legacy globals
(`currentSelectedType/Index/MeshIndex`) become a *mirror of the primary item*, updated by
`Selection` on every change, so gizmo/picking/main.cpp code keeps working until each
consumer migrates (finish by Pass 7). Grep checklist for consumers:
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
3. **Annotations & tools output** — measurements (name = "P1→P2 3.42 m"), section planes,
   brush clusters. Selecting one selects it in its tool too (e.g.
   `brushTool.setActiveCluster`, existing behavior).

Row anatomy: `[chevron] [type icon·color] [name] …… [badges] [👁] [🔒]`.
- Eye = visibility (models/meshes/clouds/sun/lights/planes have `visible`/`enabled`
  already; add `visible` to measurements & clip planes if missing — check
  `Engine::Measurement`/`ClipPlane` in `Engine/Data.h`).
- Lock (new `bool locked` on objects): locked items ignore viewport picking/gizmo.
- Hover reveals the eye/lock; keep rows 26–28 px, full-row hit target, no checkbox column
  (the current 60 px checkbox column at `GUI.cpp:2474` goes away).

Interactions (all undoable through existing `Engine::Undo` entry points; add new record
types where missing — group ops, rename):

- Multi-select: click / `Ctrl` toggle / `Shift` range. Drag = move into/out of groups,
  reorder inside a group.
- Inline rename: `F2` / double-click on name (models, clouds, lights — lights need a
  `name` field, see 6.3; fallback naming "Point Light 3" stays as default value).
- Context menu (right-click, works on multi-selection): Frame `F` · Isolate ·
  Show/Hide · Lock · Duplicate `Ctrl+D` · Group `Ctrl+G` / Ungroup · Rename `F2` ·
  Export… (clouds already have Export) · Delete `Del`.
- **Isolate** (new command `view.isolate`): hide everything except selection; re-run to
  restore (store the pre-isolate visibility snapshot; surface as a status-bar chip
  "Isolated · click to exit" so users never get "lost").
- Type filter chips under the search field: `[◼ Models] [◼ Clouds] [◼ Lights] [◼ Annot.]`
  (multi-toggle); search stays the existing `searchMatches` substring filter, upgraded to
  also match type nouns ("light" finds all lights).
- Drop target: dropping files onto the Outliner imports them (same path as viewport drop).

### 6.3 Data model changes (Engine)

- `Engine::SceneGroup { int id; std::string name; bool visible=true; bool locked=false;
  int parentId=-1; }` + `std::vector<SceneGroup> groups` in `Engine::Scene`; objects get
  `int groupId = -1` (models, point clouds, lights, measurements, clip planes, brush
  clusters). One level of nesting is enough initially (parentId reserved).
- `name` fields for `PointLight`/`SpotLight` (default from index at creation).
- Scene format **v3**: bump `kSceneFormatVersion`, write `groups` + `groupId` + light
  names; loader must keep reading v1/v2 forever (defaults: no groups). Update
  `SceneInfo`/load report counts. Snapshots (`Core/SnapshotManager`) must survive
  unchanged.

### 6.4 Acceptance (Pass 1)

- Every object type listed above appears in the tree; nothing exists only in a tool window.
- Multi-select works with gizmo (transform applies to all — extend
  `Tools::TransformGizmo` to operate on the selection set; single-object behavior
  unchanged) and with Delete/Duplicate/visibility as batch ops (single undo step each).
- Groups: create/rename/dissolve/drag-membership/visibility/persist in `.scene` (v3),
  reload correctly, old scenes still load.
- Search + filters never leave the panel empty without an explanatory `EmptyState`.

---

## 7. Inspector (Pass 2) — always the right controls

New files `headers/Gui/Inspector.h` + `src/Gui/Inspector.cpp`. Window `"Inspector"`,
docked right. The existing `render*ManipulationPanel` functions (`GUI.cpp:8095-8850`)
move here (mostly verbatim first, restyled second) and register per-kind editors:

```cpp
// Inspector dispatch: kind → editor. Editors receive the full selection so they
// can do mixed editing. Tools contribute a "Tool card" section when active.
void RenderInspector(Selection& sel);
```

Layout top-to-bottom:
1. **Header card**: type icon (colored), editable name, type noun badge, quick actions
   (visibility, lock, frame). Multi-select: "3 objects (2 Models, 1 Light)".
2. **Tool card** (only while a tool is active): the active tool's options — see Pass 6.
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
- The mesh sub-panel (`renderMeshManipulationPanel`) becomes the editor for
  `Kind::Mesh`.
- Sun/Environment editors absorb `renderSunManipulationPanel` + the skybox controls
  (object-ish parts) from Settings ▸ Environment.

---

## 8. Command palette & global search (Pass 3)

New files `headers/Gui/CommandPalette.h` + `src/Gui/CommandPalette.cpp`.

- **Open:** `Ctrl+K` (register in ShortcutManager; also `Ctrl+P` alias), menu-bar search
  icon, status-bar hint. Centered top overlay (like VS Code), 560 px wide, dimmed scrim,
  `Esc` closes, fully keyboard navigable.
- **Sources (providers)**, merged and ranked:
  1. **Commands** — from CommandRegistry (icon, title, category, live shortcut, enabled).
  2. **Scene objects** — every Outliner item (icon+color; Enter = select, `Shift+Enter` =
     select+frame).
  3. **Settings** — an index built in Pass 5 (`SettingsField { label, category, keywords }`);
     Enter opens Settings at that category and flash-highlights the row.
  4. **Recent scenes** — open/merge.
  5. **Help** — "Shortcuts reference", links (docs), theme picker.
- **Fuzzy match:** reuse and generalize `snapshotFuzzyMatch` (`GUI.cpp:7267`) into
  `UiKit::FuzzyMatch(needle, haystack, &score, outHighlights)` with subsequence scoring +
  highlight ranges rendered bold/accent.
- Empty query shows: 5 recent commands (persisted), current selection actions, tips.
- Prefix filters (discoverable via placeholder): `>` commands only, `#` objects only,
  `:` settings only.

The palette is *the* global search the brief asks for; individual panels keep their local
search fields (Outliner, Snapshots, Settings) but all use `UiKit::SearchInput` and the
same fuzzy matcher, so behavior feels identical everywhere.

---

## 9. Smart import & Welcome Hub (Pass 4)

### 9.1 ImportService — one entry point, zero questions

New files `headers/Core/ImportService.h` + `src/Core/ImportService.cpp`. Consolidates the
three import paths that exist today (menu model / menu point-cloud / drop handler at
`GUI.cpp:1689-1728`, `1733-1806`) into one:

```cpp
namespace Core {
  struct ImportPlan {            // what we DETECTED, not what we ask
    enum class Action { Model, PointCloud, PointCloudLASGroup, Scene,
                        EnvironmentHDR, Texture, Unknown };
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
- **Menu:** `File ▸ Import Files… (Ctrl+I)` with one combined file-dialog filter (keep
  "3D Models"/"Point Clouds" as sub-filters in the dialog). The two old entries collapse.
- **Scene files:** keep the replace/merge/ask behavior (`SceneLoadingBehavior` pref), but
  the ask-dialog gains "☑ Remember my choice" writing that pref (the pref & dialog exist —
  `GUI.cpp:1949-1997` — add the checkbox, remove the tip text).
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
- The old tiny hint text remains as the GUI-hidden variant (it also serves `showGui==false`).

---

## 10. Settings overhaul (Pass 5)

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

## 11. Tools & plugins coherence (Pass 6)

**Tool = mode + Outliner output + Inspector card.** Introduce a tiny `ToolManager`
(`headers/Tools/ToolManager.h`) tracking the single active tool id
(`none|measure|section|brush`), with activate/deactivate commands registered in the
registry (shortcuts stay: `P` for section planes etc. — see `ShortcutManager.h`).

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
- **Snapshots** panel keeps its identity (search/tags/thumbnails are good), restyled with
  UiKit; default dock: tab with Inspector; snapshots also become palette results
  ("Restore snapshot …").
- **Scene Manager** window dissolves: recents/info → Welcome Hub + File menu; save
  options → Settings ▸ Scenes (new small category or under Display — implementer's
  choice); toasts already report save/load.
- **PluginContext extensions:** `registerCommand(...)`, `inspector()` hook as above,
  `ObjectKind`-styled toast icons. Update `docs/PLUGINS.md` and the `CrosshairPlugin`
  template to demonstrate the new hooks; plugin windows get a UiKit style guide section.
  Existing plugin API must keep working unchanged (hooks are additive).

---

## 12. Viewport, overlays, status bar, hints (Pass 7)

- **Unified viewport toolbar:** merge the two floating strips (`renderViewModeToolbar`,
  `renderGizmoViewportToolbar`) into one adaptive top-center strip inside the viewport
  hole: gizmo segmented control | view dropdown (standard views + Frame `F` + Reset) |
  shading dropdown (lighting mode, wireframe, unlit) | camera dropdown (speed, FOV quick
  slider) | right side: layout reset + hide-UI. Overflow collapses into `⋯` when narrow
  (measure available width; this is the "layout optimized" requirement — no clipped
  buttons ever).
- **Status bar** (from Pass 0 shell) completes: left = scene stats (objects, total
  points — reuse `formatPointCount`), selection summary; center = background progress
  (point-cloud streaming overlay folds in here, keeping its per-file detail as a hover
  popup); right = active tool chip, lighting-mode chip (click = cycle, matches `L`),
  stereo indicator (quad-buffer active?), FPS (click toggles the detailed perf overlay).
- **Overlays restyle** with tokens: perf overlay, radar scope frame, measurement labels
  (`drawMeasurementLabels`) get the semantic colors; toasts gain action buttons
  (`HintToast`) and stack limit 3 + "+2 more".
- **HintEngine** (the "alive" layer): `headers/Gui/Hints.h` — rate-limited (≥1 per
  session gap), context-triggered, dismiss-forever (persisted set in prefs), all behind
  `preferences.enableHints` (default on). Launch set of hints:
  1. First model imported → "Press `F` to frame the selection."
  2. FPS < 20 for 10 s with an expensive feature on → "Radiance ray count is high for
     this scene — [Lower it] [Keep]" (performance guardian; never auto-changes).
  3. Huge point cloud + HQS off → suggest High-Quality Shading.
  4. 5+ ungrouped objects → "Select related objects and press `Ctrl+G` to group them."
  5. User opened Settings 3× in a session → "`Ctrl+K` can jump straight to any setting."
- Legacy inset aliases (`g_dockLeftWidth`/`g_dockTopHeight`) removed; all consumers on
  `g_viewportInsets`.

---

## 13. Engineering guardrails (every pass)

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
3. **Preferences:** every new UI state that should survive restart goes through
   `savePreferences`/`loadPreferences` in `main.cpp` with a default that reproduces
   today's behavior when the key is missing.
4. **Decompose as you go:** each pass extracts its area out of `GUI.cpp` into the new
   `src/Gui/*.cpp` files; `GUI.cpp` shrinks every pass and ends as the thin `renderGUI`
   orchestrator. Never leave two live copies of a panel.
5. **Manual test checklist per pass** (run on Windows when possible; otherwise state
   clearly in the PR that visual verification is pending): launch clean (no
   `preferences.json`), launch with legacy prefs, import each format (obj/gltf/fbx, xyz,
   ascii+binary ply, las multi-tile, laz, h5, pcb), drop-import, dock/undock/float/drag-out
   every panel, reset layout, resize window to 800×600 and 4K, toggle `G`, cycle `L`,
   undo/redo a session of edits, save+reload a v2 scene and a new v3 scene, quad-buffer
   stereo smoke test if hardware allows.
6. **Copywriting:** sentence-case labels, verbs on buttons, tooltips explain *why* not
   just *what* (the `DrawHelpMarker` texts are good — keep that bar), no jargon in
   first-level UI ("Point size" not "Splat radius clamp"), units on every number.

---

## 14. Pass Plan

Order matters (foundation → heart → intelligence → polish), but passes 3–6 are largely
parallelizable after Pass 2. Sizes: S < 300 LoC, M < 1200, L < 3000 touched.

| # | Pass | Contents (details in section) | Size | Depends on |
|---|---|---|---|---|
| 0 | **Foundation** | UiKit extraction+tokens, AppShell dockspace + 4-side insets, status-bar shell, CommandRegistry + menu rewire, vcxproj wiring | L | — |
| 1 | **Outliner** | §6: item model, Selection, groups (scene v3), multi-select, rename, context menus, filters, isolate, lock | L | 0 |
| 2 | **Inspector** | §7: panel extraction, per-kind editors, multi-edit, global cards, per-section reset, empty states | L | 1 |
| 3 | **Command palette** | §8: palette UI, providers (commands/objects/settings/recents), fuzzy lib, menu search icon | M | 0 (better after 1) |
| 4 | **Smart import + Hub** | §9: ImportService, unified menu/drop/dialog, HDR/texture handling, Welcome Hub, remembered choices | M | 0 |
| 5 | **Settings** | §10: search index, defaults framework, resets + modified dots, re-grouping (camera/cursor), theme cards | L | 0 |
| 6 | **Tools coherence** | §11: ToolManager, Inspector tool cards, plugin hook additions, Snapshots/SceneManager disposition, PLUGINS.md update | M | 2 |
| 7 | **Viewport & alive** | §12: unified toolbar, status bar completion, overlay restyle, HintEngine, remove legacy insets | M | 0 (toolbar), 6 (tool chip) |
| 8 | **Harmony audit** | Full sweep: spacing/icon/color audit vs UiKit, copy audit, empty-state audit, perf (no per-frame allocs in new panels), docs (CLAUDE.md GUI section rewrite, PLUGINS.md), kill dead code | M | all |

Each pass's PR description must include: what changed vs this plan (deviations + why),
the manual-test checklist status, and the Status Board update.

---

## 15. Status Board

Keep this table truthful — it is the coordination point between agent passes.

| Pass | State | Branch/PR | Notes & deviations |
|---|---|---|---|
| Plan | ✅ done | `claude/gui-redesign-ux-hadxj2` | This document. |
| 0 Foundation | ⬜ not started | | |
| 1 Outliner | ⬜ not started | | |
| 2 Inspector | ⬜ not started | | |
| 3 Palette | ⬜ not started | | |
| 4 Import + Hub | ⬜ not started | | |
| 5 Settings | ⬜ not started | | |
| 6 Tools | ⬜ not started | | |
| 7 Viewport & alive | ⬜ not started | | |
| 8 Harmony | ⬜ not started | | |

### Decisions log

- 2026-07-12 — Two-panel heart confirmed: Outliner (left) + Inspector (right) as separate
  dockable windows over a passthru-central-node DockSpace; scene keeps rendering directly
  to the backbuffer (no render-to-texture).
- 2026-07-12 — CommandRegistry chosen as the coherence mechanism (menus, palette,
  shortcuts, toolbars all consume it).
- 2026-07-12 — No capability removal anywhere; re-grouping + progressive disclosure only.
