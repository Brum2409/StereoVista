# StereoVista - CLAUDE.md

## Project Overview

StereoVista is an OpenGL 4.6 desktop application for interactive 3D visualization with native stereo (quad-buffer) rendering support. It handles 3D models and point clouds with advanced rendering features including PBR, SSAO, bloom, three switchable lighting modes (shadow mapping, voxel cone tracing, and BVH ray-traced radiance), DDGI (dynamic diffuse global illumination), and 3DConnexion SpaceMouse navigation.

**Platform:** Windows only (MSVC toolchain)  
**Language:** C++17 / GLSL  
**Build System:** Visual Studio 2019+ (`StereoVista.sln`)

---

## Build Instructions

Open `StereoVista.sln` in Visual Studio 2019 or newer and build. There is no CMake or Makefile — MSBUILD only.

- **Recommended config:** `Release | x64`
- **Output:** `bin/x64/Release/StereoVista.exe`
- Post-build steps automatically copy required DLLs (`assimp-vc143-mt.dll`, `LASzip64.dll`) to the output directory.

**No automated test suite exists.** Verification is manual/visual by running the application.

---

## Repository Structure

```
StereoVista/
├── src/                    # C++ implementation files
│   ├── Core/               # Camera, SceneManager, UndoManager, CursorSynchronizer, Voxalizer
│   ├── Engine/             # Rendering subsystems (BVH, DDGIVolume, Bloom, SSAO,
│   │                       #   ComputePointCloudRenderer, Octree, shaders, buffers, input)
│   ├── Cursors/            # 3D cursor system (base classes, types, presets)
│   │   ├── Base/
│   │   └── Types/
│   ├── Gui/                # ImGui interface and 3D cursor preview
│   ├── Loaders/            # Model and point cloud loaders
│   ├── Tools/              # BrushTool
│   └── main.cpp            # Application entry point and main render loop (~7,000 lines)
├── headers/                # Header files (mirrors src/ structure)
│   ├── Core/
│   ├── Engine/
│   ├── Cursors/
│   ├── Gui/
│   ├── Loaders/
│   ├── Tools/
│   └── libs/               # Vendored third-party headers (ImGui, Assimp, 3DConnexion, HDF5, etc.)
├── assets/
│   ├── shaders/            # GLSL shaders organized by subsystem
│   │   ├── core/
│   │   ├── cursors/
│   │   ├── bloom/
│   │   ├── ssao/
│   │   ├── skybox/
│   │   └── voxelization/
│   └── models/             # Default 3D models
├── dependencies/           # Pre-built third-party libraries (GLFW, GLAD, GLM, Assimp, HDF5, LASzip)
│   ├── include/
│   ├── lib/
│   └── bin/
├── StereoVista/            # Runtime data (loaded by executable at runtime)
│   ├── preferences.json    # User settings (auto-saved on exit)
│   ├── shortcuts.json      # Keyboard shortcut mappings
│   ├── cursor_presets.json # Saved 3D cursor configurations
│   └── office.scene        # Example scene file
├── fonts/
├── skybox/
├── StereoVista.sln
└── StereoVista/StereoVista.vcxproj
```

---

## Key Dependencies

| Library | Purpose | Location |
|---|---|---|
| OpenGL 4.6 + GLAD | Graphics API | `dependencies/` |
| GLFW | Windowing and input | `dependencies/` |
| GLM | Math library | `dependencies/` |
| Assimp | 3D model loading (OBJ, FBX, GLTF, …) | `dependencies/` |
| ImGui | Immediate-mode GUI | `headers/libs/imgui/` |
| nlohmann/json | JSON parsing | `headers/libs/json.h` |
| stb_image | Image loading | `headers/libs/stb_image.h` |
| HDF5 + HighFive | Point cloud HDF5 format | `dependencies/` |
| LASzip | LAS/LAZ point cloud format | `dependencies/` |
| TDxNavLib | 3DConnexion SpaceMouse | `dependencies/` |
| portable-file-dialogs | Cross-platform file dialogs | `headers/libs/` |

---

## Architecture

### Core Patterns

- **Manager pattern:** `CursorManager`, `SceneManager`, `ShortcutManager`
- **Renderer pattern:** `BloomRenderer`, `SSAORenderer`, `ComputePointCloudRenderer`
- **Enum-based configuration:** `SkyboxType`, `LightingMode`, `CursorScalingMode` in `headers/Gui/GuiTypes.h`
- **JSON persistence:** Preferences, cursor presets, and scene files serialized to JSON at runtime

### Rendering Pipeline

- Stereo rendering via `renderEye()` — called twice per frame with separate left/right projection matrices (asymmetric-frustum). View-independent passes (sun/point-light shadow maps and the DDGI probe-atlas update) are generated once on the first eye each frame and reused for the second eye, guarded by the per-frame `g_sharedPassesDone` flag in `main.cpp`. Per-eye work (SSAO geometry pass, the main lit draw, and all texture/uniform binding) still runs for both eyes.
- Configurable convergence point and eye separation for quad-buffer stereo displays
- Three lighting modes (`GUI::LightingMode` in `headers/Gui/GuiTypes.h`), cycled at runtime with the **L** key:
  - **Shadow Mapping** — shadow-mapped direct lighting (directional sun + point-light cubemaps); optional DDGI indirect bounce via the "Enable Indirect Lighting" toggle
  - **Voxel Cone Tracing (VCT)** — scene voxelized into a 3D texture each frame; single-pass cone tracing for diffuse GI, soft shadows, and reflections
  - **Radiance** — BVH-accelerated ray-traced lighting (configurable bounces / samples-per-pixel / ray distance) with optional DDGI diffuse GI
- **DDGI** (`Engine::DDGIVolume`): world-space probe ray tracing against the scene BVH into octahedral irradiance/visibility atlases with temporal hysteresis; shared by both the Radiance and Shadow Mapping modes. The probe grid is sized to the BVH root AABB each frame.
- **BVH** (`Engine::BVH`): built from scene triangles (triangle + node SSBOs) and consumed by Radiance ray tracing and DDGI probe tracing; rebuilt only when the scene changes.
- Point clouds: GPU compute-based renderer (`Engine::ComputePointCloudRenderer`) with octree management (`OctreePointCloudManager`) for large clouds, streamed/batch-uploaded by the loader.
- Post-processing: Bloom/HDR, SSAO, tone mapping

### Namespaces

- `Engine::` — core types and rendering utilities (`headers/Engine/Core.h`, `headers/Engine/Data.h`)
- `GUI::` — GUI types and enums (`headers/Gui/GuiTypes.h`)

---

## Code Conventions

**Naming:**
- Classes: `PascalCase`
- Functions/variables: `camelCase`
- Constants/enums: `UPPER_SNAKE_CASE`
- Member variables: `PascalCase` or `camelCase` (mixed; follow the style in the file being edited)

**File organization:**
- Headers in `headers/`, implementations in `src/`, matching directory structure
- Vendored third-party headers isolated in `headers/libs/`
- GLSL shaders in `assets/shaders/` organized by subsystem

**General:**
- C++17 features are used and expected
- ImGui is used directly (no abstraction layer) for all GUI panels
- Shader programs are managed through the `Shader` class (`headers/Engine/Shader.h`)
- GPU buffers and texture units are abstracted in `Buffers` (`headers/Engine/Buffers.h`)

---

## Runtime Configuration Files

These files are read/written by the application at runtime (not at build time):

- `StereoVista/preferences.json` — camera, stereo, lighting, GUI settings (auto-saved on exit)
- `StereoVista/shortcuts.json` — keyboard shortcut bindings (editable in-app)
- `StereoVista/cursor_presets.json` — named 3D cursor configurations

Do not modify these during a running session as changes may be overwritten on exit.

---

## Development Notes

- `main.cpp` is intentionally large (~7,000 lines) and contains the main loop, GLFW callbacks, and high-level rendering orchestration. Refactoring it requires care to preserve rendering state dependencies.
- There is no unit test infrastructure. When making rendering changes, visual verification is required.
- The project is Windows-only due to Visual Studio project files, MSVC-specific pragmas, and Windows API usage (window handles, DLL loading).
- 3DConnexion SpaceMouse features require `TDxNavLib.lib` and the 3DConnexion driver installed on the target machine.
- Point cloud formats: XYZ/TXT and **ASCII** PLY are read by the generic text parser (`PointCloudLoader::loadPointCloudFile`); LAS/LAZ via LASzip; HDF5 via HighFive; and the native `.pcb` binary format. **Binary PLY is not supported** — the loader detects a binary header and reports a clear error rather than mis-parsing it as text. HDF5 support requires `libhdf5.lib`; LAZ requires `laszip64.lib`.
