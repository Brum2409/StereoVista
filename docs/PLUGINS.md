# StereoVista Plugin System

StereoVista's interactive tools are built on a lightweight, **compile-time
plugin framework**. A plugin is a self-contained C++ class that opts in to the
host hooks it needs and registers itself with one macro. Adding a new tool is a
local change — **one header + one source file + one `REGISTER_PLUGIN` line** —
with no edits to `main.cpp` or `GUI.cpp` (the only project-wide step is listing
the two files in the Visual Studio project).

> **Loading model:** plugins are statically compiled into the executable (no
> DLLs). This gives them safe, direct access to the shared OpenGL context, the
> ImGui context and the scene state — none of which survive a DLL boundary
> cleanly on MSVC.

---

## 1. Architecture at a glance

```
                       ┌──────────────────────────────┐
   main.cpp  ────────▶ │        PluginManager         │ owns/borrows plugins
   (integration        │  init / render / UI / menu / │ and fans every host
    points)            │  input dispatch              │ call out to them
                       └──────────────┬───────────────┘
                                      │ passes a
                                      ▼
                       ┌──────────────────────────────┐
                       │        PluginContext         │ services API:
                       │  scene / camera / picking /  │ implemented in main.cpp
                       │  preferences / undo / toast  │ over the app globals
                       │  / compileOverlayProgram     │
                       └──────────────┬───────────────┘
                                      │ handed to every hook
                                      ▼
                       ┌──────────────────────────────┐
   REGISTER_PLUGIN ──▶ │           Plugin             │ your tool: override
   (self-register)     │  info() + lifecycle/render/  │ the hooks you need
                       │  ui/menu/input hooks         │
                       └──────────────────────────────┘
```

| File | Role |
|---|---|
| `headers/Plugins/Plugin.h` | Abstract `Plugin` base + `PluginInfo`. All hooks have no-op defaults. |
| `headers/Plugins/PluginContext.h` | The services API handed to every hook. Concrete impl is in `main.cpp`. |
| `headers/Plugins/PluginRegistry.h` | `REGISTER_PLUGIN(Type)` macro + the static factory registry. |
| `headers/Plugins/PluginManager.h` | Owns plugins; the host calls into it at the integration points. |
| `src/Plugins/PluginManager.cpp` | Manager + registry + `compileOverlayProgram` + default menu entry. |
| `headers/Plugins/Examples/CrosshairPlugin.*` | **Copy-me** example exercising the whole API. |
| `headers/Plugins/MeasurementPlugin.*` | Worked migration of the existing MeasurementTool (adapter pattern). |

---

## 2. Plugin lifecycle

Hooks fire in this order; **every one is optional** except `info()`.

```
onRegister(ctx)            once, when added to the manager (no GL work yet)
onInitializeGL(ctx)        once, GL context valid — create VAOs/shaders here
   ── per frame ──
onUpdate(ctx, dt)          once per frame, before rendering
onRenderViewport(ctx, …)   ONCE PER EYE, with that eye's projection/view
onRenderUI(ctx)            once per frame (ImGui pass) — draw your windows
onRenderMenu(ctx)          once per frame, inside the Tools menu
   ── on input ──
onMouseButton/onScroll/onKey(ctx, …)   return true to CONSUME the event
   ── enable toggling ──
onEnable() / onDisable()   on an actual isEnabled() transition
   ── shutdown ──
onShutdownGL()             once at exit, GL context still valid — free resources
```

`onRenderViewport` is called **twice per frame** (once per stereo eye). Keep it
stateless with respect to the matrices passed in.

---

## 3. The `PluginContext` API

Every hook receives a `Plugins::PluginContext&`. Highlights:

```cpp
// Scene & core services
Engine::Scene&               scene();
const Camera&                camera();
glm::vec3                    cameraPosition();
GUI::ApplicationPreferences& preferences();
Engine::UndoManager&         undo();

// Picking / 3D cursor
PickRay  mouseRay();                 // world-space ray under the OS cursor
bool     cursorWorldPos(glm::vec3&); // synchronized 3D cursor (false if invalid)
RayHit   raycastModels();            // nearest model hit along mouseRay()

// Input / viewport
glm::vec2 mousePos();                // window pixels
int       keyMods();                 // current GLFW_MOD_* bitmask
glm::vec2 viewportSize();            // 3D viewport, pixels

// Feedback
void toast(const std::string&, ToastLevel = ToastLevel::Info);

// GL convenience — compile+link a tiny overlay program (0 on failure)
GLuint compileOverlayProgram(const char* vs, const char* fs, const char* label);
```

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
    void onRenderUI(PluginContext& ctx) override;
    // …override only the hooks you need…
};
}
```

**Step 2 — Source** (`src/Plugins/Examples/MyToolPlugin.cpp`):

```cpp
#include "Plugins/Examples/MyToolPlugin.h"
#include "Plugins/PluginRegistry.h"
#include "imgui/imgui.h"

namespace Plugins {
PluginInfo MyToolPlugin::info() const {
    PluginInfo m;
    m.id = "stereovista.mytool";
    m.name = "My Tool";
    m.description = "Does a useful thing.";
    return m;
}
void MyToolPlugin::onRenderUI(PluginContext& ctx) {
    if (!windowOpen()) return;            // toggled by the default menu entry
    if (ImGui::Begin(info().name.c_str(), &windowOpen())) {
        if (ImGui::Button("Hi")) ctx.toast("Hello!", ToastLevel::Success);
    }
    ImGui::End();
}
}
REGISTER_PLUGIN(Plugins::MyToolPlugin);   // ← self-registration
```

**Step 3 — Project file.** Add the two files to `StereoVista/StereoVista.vcxproj`
(a `<ClCompile>` and a `<ClInclude>` item) and to `…vcxproj.filters`. MSBuild
only compiles files listed in the project; this is the **one** non-local step.

Rebuild — "My Tool" now appears in the **Tools** menu, with its window, automatically.

> The default `onRenderMenu` adds a checkbox menu item bound to `windowOpen()`.
> Override `onRenderMenu` for a custom submenu.

---

## 5. Input & event consumption

- The host queries plugins **before** its own built-in handling in the GLFW
  mouse-button, scroll and key callbacks.
- The **first** plugin whose hook returns `true` consumes the event; the host
  then early-outs and skips selection / navigation / shortcuts.
- Return `false` for events you don't handle so they propagate normally.
- Key events are dispatched on **`GLFW_PRESS`** (matching where the dispatch is
  wired in `key_callback`). Mouse-button and scroll events are dispatched for
  all actions; filter on `action` inside your hook.
- Respect modifiers: by convention Ctrl/Alt clicks are reserved for the
  gizmo / object selection, so bail out (`return false`) when those are held
  unless your tool specifically wants them.

---

## 6. Rendering notes

- World-space overlays go in `onRenderViewport` and must **not write depth**
  (`glDepthMask(GL_FALSE)`), because the 3D-cursor system samples scene depth
  from the same buffer. Save/restore any GL state you change.
- Build self-contained shaders with `ctx.compileOverlayProgram(...)` instead of
  duplicating the `glCreateShader`/link boilerplate. Free the program (and your
  VAOs/VBOs) in `onShutdownGL`.
- All GL hooks run on the render thread with a current context. Don't touch GL
  from `onRegister` (no context guaranteed yet).

---

## 7. Migrating an existing tool (worked example)

`MeasurementPlugin` (`headers/Plugins/MeasurementPlugin.*`) shows the
recommended **adapter** pattern: rather than rewriting `Tools::MeasurementTool`,
it wraps the existing global instance and forwards the Plugin hooks to it.

- The tool's editable data already lives in `Engine::Scene::measurements`, so
  scene save/load and `SnapshotManager` are untouched by the migration.
- `main.cpp` keeps the `measurementTool` global; the adapter references it via
  `extern`. The adapter self-registers with `REGISTER_PLUGIN`, so it joins the
  pipeline like any other plugin.
- All of the tool's former hand-wired call sites — the per-eye render, the Tools
  menu item, the ImGui window dispatch, and the left/right-click + Enter/Delete/
  Backspace input handling — were removed from `main.cpp`/`GUI.cpp` and now flow
  through the manager via the adapter's hooks.

Use the same approach to migrate `BrushTool`, `ClipPlaneTool`, etc.: create a
`*Plugin` adapter, `REGISTER_PLUGIN` it, route its render/UI/menu/input through
the hooks, and delete the now-duplicated direct call sites.

---

## 8. Host integration points (for reference)

The manager is driven from these spots in `main.cpp` / `GUI.cpp`:

| Where | Call |
|---|---|
| After GL init, before the main loop (`main.cpp`) | `loadRegisteredPlugins`, `initializeAllGL` |
| Per frame (`main.cpp` loop) | `update` |
| Per eye, in `renderEye` (`main.cpp`) | `renderViewport` |
| GLFW callbacks (`main.cpp`) | `dispatchMouseButton` / `dispatchScroll` / `dispatchKey` |
| Tools menu (`GUI.cpp`) | `renderMenu` |
| ImGui window pass (`GUI.cpp`) | `renderUI` |
| At shutdown (`main.cpp`) | `shutdownAllGL` |

`g_pluginManager` and `g_pluginContext` are globals in `main.cpp` (the context
is a `MainPluginContext` forwarding to the app globals), `extern`-declared where
GUI.cpp needs them.

---

## 9. Build & manual test checklist

There is no automated test suite (Windows/MSVC, visual verification).

1. Build `Release | x64`; confirm the new `Plugins\*` files compile and link.
2. Launch the app → **Tools** menu lists **"Crosshair (Example)"** and
   **"Measure"** below the built-in tool entries.
3. **Crosshair:** open it, tick *Enabled*; a crosshair tracks the 3D cursor.
   Left-click drops a marker (+ "Marker dropped" toast). Color/size sliders
   update live. *Clear markers* empties the list.
4. **Measurement (regression):** place points (left-click), finish (right-click
   / Enter), undo a point (Backspace), cancel (Delete); labels, CSV export,
   scene save+reload, and undo/redo snapshots all behave as before.
5. Confirm **Brush**, **Section Planes** and the **transform gizmo** are
   unaffected (Ctrl/Alt interactions still work).

---

## 10. Notes & future work

- **MSVC self-registration:** the app is one executable, so plugin object files
  are always linked and their `REGISTER_PLUGIN` initializers run. If the plugins
  are ever moved into a separate static library, use `/WHOLEARCHIVE` (or an
  explicit symbol reference) so the registrars aren't stripped.
- Reserved but not yet wired: per-plugin JSON persistence hooks
  (`onSceneSave`/`onSceneLoad`) — today plugins persist via the scene itself.
- Dynamic DLL hot-loading is intentionally **not** supported (see the loading
  model note at the top).
