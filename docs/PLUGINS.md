# StereoVista Plugin System

StereoVista's interactive tools are built on a lightweight, **compile-time
plugin framework**. A plugin is a self-contained C++ class that opts in to the
host hooks it needs and registers itself with one macro. Adding a new tool is a
local change — **one header + one source file + one `REGISTER_PLUGIN` line** —
with no edits to `Application.cpp` (the only project-wide step is listing the two
files in the Visual Studio project).

> **Loading model:** plugins are statically compiled into the executable (no
> DLLs), giving them direct, safe access to the scene, the ImGui context, and the
> shared overlay draw list — none of which survive a DLL boundary cleanly on MSVC.
>
> **GL-free (Vulkan rewrite):** plugins **own no GPU objects**. The old GL
> lifecycle (`onInitializeGL`/`onShutdownGL`) and the per-tool
> `compileOverlayProgram` helper are gone. World-space drawing is *descriptive*:
> a plugin appends primitives to one shared `renderer::OverlayDrawList` and the
> renderer transforms and draws them — once, for both stereo eyes.

---

## 1. Architecture at a glance

```
                       ┌──────────────────────────────┐
   Application  ─────▶ │        PluginManager         │ owns the plugins and fans
   (loop drives it)    │  update / buildOverlay / UI /│ every host call out to them
                       │  menu / input dispatch       │
                       └──────────────┬───────────────┘
                                      │ passes a
                                      ▼
                       ┌──────────────────────────────┐
                       │        PluginContext         │ services API:
                       │  scene / camera / overlay /  │ concrete MainPluginContext
                       │  picking / selection / undo /│ lives in App/Application.cpp
                       │  input / viewProj / toast    │ (a friend of Application)
                       └──────────────┬───────────────┘
                                      │ handed to every hook
                                      ▼
                       ┌──────────────────────────────┐
   REGISTER_PLUGIN ──▶ │           Plugin             │ your tool: override
   (self-register)     │  info() + lifecycle/overlay/ │ only the hooks you need
                       │  ui/menu/input hooks         │
                       └──────────────────────────────┘
```

| File | Role |
|---|---|
| `headers/Plugins/Plugin.h` | Abstract `Plugin` base + `PluginInfo`. All hooks have no-op defaults. |
| `headers/Plugins/PluginContext.h` | The services API handed to every hook. Concrete impl is `MainPluginContext` in `App/Application.cpp`. |
| `headers/Plugins/PluginRegistry.h` | `REGISTER_PLUGIN(Type)` macro + the static factory registry. |
| `headers/Plugins/PluginManager.h` | Owns the plugins; `Application` drives it from the loop (no globals). |
| `headers/Plugins/Examples/CrosshairPlugin.*` | **Copy-me** example exercising the whole API. |
| `headers/Plugins/MeasurementPlugin.*` | Worked migration: the plugin **owns and drives** `Tools::MeasurementTool`. |
| `headers/Renderer/OverlayDrawList.h` | The overlay geometry API plugins draw into. |

---

## 2. Plugin lifecycle

Hooks fire in this order; **every one is optional** except `info()`.

```
onRegister(ctx)           once, when added to the manager (wire scene data)
   ── per frame ──
onUpdate(ctx, dt)         once per frame, before rendering
onBuildOverlay(ctx)       once per frame — append world-space geometry to ctx.overlay()
onRenderUI(ctx)           once per frame (ImGui pass) — draw your windows
onRenderMenu(ctx)         once per frame, inside the Tools menu
   ── on input ──
onMouseButton/onScroll/onKey(ctx, …)   return true to CONSUME the event
   ── enable toggling ──
onEnable() / onDisable()  on an actual isEnabled() transition
```

`onBuildOverlay` is called **once per frame** (not per eye): you describe geometry
in world space and the renderer draws it for every eye. There are **no matrices to
handle** and **no GPU state to save/restore**.

---

## 3. The `PluginContext` API

Every hook receives a `Plugins::PluginContext&` (exact signatures in the header):

```cpp
// Scene, camera & core services
scene::Scene&      scene();
const Camera&      camera();
glm::vec3          cameraPosition();
core::UndoManager& undo();

// Persisted user settings (Gui/Settings.h; preferences.json persistence is
// automatic). Include Gui/Settings.h in your plugin .cpp to use it.
Gui::Settings&     preferences();

// Overlay (replaces the GL compileOverlayProgram) — append once per frame
renderer::OverlayDrawList& overlay();

// Picking / 3D cursor
PickRay mouseRay();                       // world-space ray under the OS cursor
bool    cursorWorldPos(glm::vec3& out);   // depth-picked 3D cursor pos (false if none)
bool    raycastModels(scene::RayHit& out);// model + sub-mesh under the cursor

// Selection (index into scene().models; mesh -1 = whole model)
int  selectedModel();  int selectedMesh();  void setSelection(int model, int mesh = -1);

// Input / viewport
glm::vec2 mousePos();      // window pixels
int       keyMods();      // current GLFW_MOD_* bitmask
glm::vec2 viewportSize(); // 3D viewport, pixels
glm::mat4 viewProj();     // proj*view this frame (world->screen for ImGui labels)

// Feedback
void toast(const std::string&, ToastLevel = ToastLevel::Info);
```

`preferences()` returns the live `Gui::Settings` (restored in UI redesign
Pass 0) — this is the Vulkan-native settings struct, **not** the GL
`ApplicationPreferences` blob. Reads are always safe; writes persist
automatically (debounced save + save-on-exit to `preferences.json`).

---

## 4. Add a new tool in 3 steps

**Step 1 — Header** (`headers/Plugins/Examples/MyToolPlugin.h`):

```cpp
#pragma once
#include "Plugins/Plugin.h"
namespace Plugins {
class MyToolPlugin : public Plugin {
public:
    PluginInfo info() const override;
    void onBuildOverlay(PluginContext& ctx) override;
    void onRenderUI(PluginContext& ctx) override;
    bool onMouseButton(PluginContext& ctx, int button, int action, int mods) override;
    // …override only the hooks you need…
private:
    std::vector<glm::vec3> m_points;
};
}
```

**Step 2 — Source** (`src/Plugins/Examples/MyToolPlugin.cpp`):

```cpp
#include "Plugins/Examples/MyToolPlugin.h"
#include "Plugins/PluginRegistry.h"
#include "Renderer/OverlayDrawList.h"
#include "imgui/imgui.h"
#include <GLFW/glfw3.h>

namespace Plugins {
PluginInfo MyToolPlugin::info() const {
    PluginInfo m; m.id = "stereovista.mytool"; m.name = "My Tool";
    m.description = "Does a useful thing."; return m;
}
void MyToolPlugin::onBuildOverlay(PluginContext& ctx) {
    for (auto& p : m_points)                       // world-space, both eyes reuse it
        ctx.overlay().marker(p, 8.0f, glm::vec4(1, 0.8f, 0, 1));
}
void MyToolPlugin::onRenderUI(PluginContext& ctx) {
    if (!windowOpen()) return;                     // toggled by the default menu entry
    if (ImGui::Begin(info().name.c_str(), &windowOpen())) {
        bool en = isEnabled();
        if (ImGui::Checkbox("Enabled", &en)) setEnabled(en);
        if (ImGui::Button("Clear")) m_points.clear();
    }
    ImGui::End();
}
bool MyToolPlugin::onMouseButton(PluginContext& ctx, int button, int action, int mods) {
    if (!isEnabled() || button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
        return false;
    if (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) return false;  // leave nav/select
    glm::vec3 hit;
    if (!ctx.cursorWorldPos(hit)) return false;
    m_points.push_back(hit);
    return true;                                   // consume: don't orbit / select
}
}
REGISTER_PLUGIN(Plugins::MyToolPlugin);            // ← self-registration
```

**Step 3 — Project file.** Add the two files to `StereoVista/StereoVista.vcxproj`
(a `<ClCompile>` and a `<ClInclude>` item) and to `…vcxproj.filters`. MSBuild
only compiles files listed in the project; this is the **one** non-local step.

Rebuild — "My Tool" now appears in the **Tools** menu, with its window, automatically.

> The default `onRenderMenu` adds a checkbox menu item bound to `windowOpen()`.
> Override `onRenderMenu` for a custom submenu.

---

## 5. Input & event consumption

- The manager queries plugins **before** the app's built-in handling; the **first**
  plugin whose hook returns `true` consumes the event and the app skips selection /
  navigation / shortcuts for it.
- A plugin that consumes a mouse **press** owns that button until its release
  (press-owns-button-until-release), so drags don't leak into orbit/select.
- The app dispatches mouse-button and scroll edges plus the action keys **Enter /
  KP-Enter / Delete / Backspace** to `onKey` (e.g. the MeasurementPlugin's finish /
  cancel / undo-point). Filter on `key`/`action` inside your hook.
- Respect modifiers: **Ctrl/Alt** clicks are reserved for the transform gizmo /
  object selection — bail out (`return false`) when those are held unless your tool
  specifically wants them. The gizmo has handle priority, so a plugin gets LMB only
  when no gizmo handle was hit.

---

## 6. Overlay rendering (`renderer::OverlayDrawList`)

Append primitives in `onBuildOverlay`; the renderer draws the whole list on the
backbuffer after tonemap, depth-testing against the scene. Everything becomes
triangles internally (lines expand to constant-width screen-space quads). Painter's
order = call order within a depth mode.

**Depth mode** (`OverlayDepth`) is per-primitive and replaces manual depth state:
- `Always` — no depth test, on top of everything (gizmo handles).
- `Occluded` (default) — visible only where not behind scene geometry.
- `Hidden` — draws **only** where geometry covers it — the x-ray "ghost" pass.

Drawing an annotation both solid-in-front and ghosted-behind is just two calls
(`Occluded` + `Hidden`) — no two-pass GL trick.

**Primitives:** `line`, `polyline` (open/closed), `triangle`, `triangles`,
`marker` (round screen-space point sprite), `disc` (camera-facing world disc),
`rings` (fragment-cursor ring pair), `background` (gradient backdrop), and
`sphereMesh` (a CPU-transformed lit sphere). Colors are `glm::vec4` (RGBA 0–1).
See `CrosshairPlugin::onBuildOverlay` for the canonical usage.

---

## 7. Migrating an existing tool (worked example)

`MeasurementPlugin` (`headers/Plugins/MeasurementPlugin.*`) shows the recommended
**owning** pattern: the plugin holds a `Tools::MeasurementTool` and drives *all* of
it through hooks — no host globals, no `extern`.

- `onBuildOverlay` appends the tool's lines/markers/area-fills + the live cursor
  preview; `onRenderUI` draws the settings window and world-space value labels
  (via `ctx.viewProj()` + the ImGui foreground draw list); `onRenderMenu` adds the
  "Measure" entry; `onMouseButton` places/finishes points; `onKey` handles
  Enter/Delete/Backspace.
- This **removed all measurement special-casing from `Application`** — the member,
  click branch, keyboard handling, overlay append, panel section, and labels all
  now flow through the manager via the plugin.

Use the same approach to migrate a tool: create a `*Plugin` that owns the tool,
`REGISTER_PLUGIN` it, route render/UI/menu/input through the hooks, and delete the
now-duplicated direct call sites from `Application`.

---

## 8. Host integration points (for reference)

`Application` (`src/App/Application.cpp`) owns `pluginManager_` + `pluginContext_`
(a `MainPluginContext`, a friend of `Application`) and drives the manager from the
loop — there are **no `g_plugin*` globals**:

| Where (in `Application`) | Manager call |
|---|---|
| Startup | plugins self-register via `REGISTER_PLUGIN`; the manager instantiates them |
| Per frame, after `updateCamera` | `update` |
| Inside `updateCursorAndOverlay` | `buildOverlay` |
| ImGui frame (`buildUi`) | `renderUI` + `renderMenu` (+ the toast overlay) |
| Polled input edges | `dispatchMouseButton` / `dispatchScroll` / `dispatchKey` |

---

## 9. Build & manual test checklist

There is no automated test suite (Windows/MSVC, visual verification).

1. Build `Release | x64`; confirm the new `Plugins\*` files compile and link.
2. Launch → **Plugins** in the debug panel lists **"Crosshair (Example)"** and
   **"Measure"**.
3. **Crosshair:** open it, tick *Enabled*; a crosshair tracks the 3D cursor.
   Left-click drops a marker (+ "Marker dropped" toast). Color/size sliders update
   live; *Clear markers* empties the list and does **not** orbit/select.
4. **Measurement (regression):** place points (left-click), finish (right-click /
   Enter), undo a point (Backspace), cancel (Delete); labels, area fills, x-ray,
   and CSV export behave.
5. Confirm the **transform gizmo** and **section planes** are unaffected (a plugin
   gets LMB only when no gizmo handle is hit; Ctrl/Alt still drive nav/select).

---

## 10. Notes & future work

- **MSVC self-registration:** the app is one executable, so plugin object files are
  always linked and their `REGISTER_PLUGIN` initializers run. If plugins are ever
  moved into a separate static library, use `/WHOLEARCHIVE` (or an explicit symbol
  reference) so the registrars aren't stripped.
- **No per-plugin persistence yet:** committed tool data lives on the tool/plugin
  for now; scene-level save/load returns with the SceneManager port.
- Dynamic DLL hot-loading is intentionally **not** supported (see the loading-model
  note at the top).
