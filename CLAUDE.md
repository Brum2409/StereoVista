# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

StereoVista is a C++ OpenGL 4.6 application designed for advanced stereo 3D visualization and manipulation of 3D models and point clouds. The application follows a modular architecture with clear separation of concerns across different subsystems.

## Build and Testing Workflow

**IMPORTANT**: The user handles all building and testing of the application themselves. Do not attempt to run build commands or suggest testing procedures. Instead:

1. **Focus on code changes only** - Provide code modifications without build instructions
2. **Wait for build feedback** - The user will build the project and report any compilation errors
3. **Address build errors** - When the user pastes build errors, analyze and fix the specific issues
4. **Iterative improvement** - Work through build errors one by one until the project compiles successfully

### Build Information (For Reference Only)
- Built with Visual Studio 2019 or newer using `StereoVista.sln`
- Build configurations: Debug x64, Release x64 (Win32 configurations also available)
- All dependencies are pre-built and included in the `dependencies/` folder
- Key dependencies: GLFW, OpenGL, Assimp, GLM, ImGui, STB

## Architecture Overview

### Core Components

1. **Engine Layer** (`headers/Engine/`, `src/Engine/`)
   - `Core.h` - Main engine definitions and OpenGL setup
   - `Window.h/.cpp` - GLFW window management
   - `Shader.h/.cpp` - OpenGL shader compilation and management
   - `Buffers.h/.cpp` - OpenGL buffer objects (VAO, VBO, EBO)
   - `Input.h/.cpp` - Input handling and event management
   - `Data.h` - Common data structures and types

2. **Core System** (`headers/Core/`, `src/Core/`)
   - `Camera.h` - Camera system with multiple control modes (orbit, pan, free-fly)
   - `SceneManager.h/.cpp` - Scene serialization/deserialization, model and point cloud management
   - `Voxalizer.h/.cpp` - Voxel Cone Tracing implementation for global illumination

3. **Cursor System** (`headers/Cursors/`, `src/Cursors/`)
   - `Base/CursorManager.h/.cpp` - Centralized cursor management
   - `Base/Cursor.h/.cpp` - Base cursor class with common functionality
   - `Types/SphereCursor.h/.cpp` - 3D sphere cursor with multiple scaling modes
   - `Types/FragmentCursor.h/.cpp` - Fragment shader-based circular cursor
   - `Types/PlaneCursor.h/.cpp` - Plane cursor that follows surface geometry
   - `CursorPresets.h/.cpp` - JSON-based cursor preset system

4. **Asset Loaders** (`headers/Loaders/`, `src/Loaders/`)
   - `ModelLoader.h/.cpp` - Assimp-based 3D model loading (OBJ, FBX, GLTF, etc.)
   - `PointCloudLoader.h/.cpp` - Point cloud loading with chunked visualization

5. **GUI System** (`headers/Gui/`, `src/Gui/`)
   - `GUI.h/.cpp` - ImGui-based user interface
   - `GuiTypes.h` - GUI-specific data structures and enums

### Key Features

- **Stereo Rendering**: Quad-buffer stereo support with configurable separation/convergence
- **Advanced Camera**: Physics-based smooth scrolling, multiple control modes
- **Lighting**: Choice between Shadow Mapping and Voxel Cone Tracing for global illumination
- **Material System**: PBR workflow with full texture support
- **Scene Management**: Complete scene save/load functionality

## Common Development Tasks

### Adding New Models
Models are loaded via the File menu or programmatically through `ModelLoader`. The application supports all formats handled by Assimp.

### Shader Development
Shaders are located in `assets/shaders/` and organized by feature:
- `core/` - Main rendering shaders
- `cursors/` - Cursor-specific shaders
- `skybox/` - Skybox rendering
- `voxelization/` - Voxel cone tracing shaders

### Cursor System Extension
To add new cursor types:
1. Derive from `Cursor::BaseCursor`
2. Implement virtual methods (initialize, render, update)
3. Register in `CursorManager`
4. Add GUI controls in cursor settings

### Configuration Management
- User preferences are stored in `preferences.json`
- Cursor presets are stored in `cursor_presets.json`
- Both files are automatically managed by the application

## Key Files to Understand

- `src/main.cpp` - Application entry point and main render loop
- `src/main.cpp:renderEye()` - Core stereo rendering pipeline
- `headers/Core/SceneManager.h` - Scene data structures
- `headers/Cursors/Base/CursorManager.h` - Cursor system interface
- `headers/Engine/Core.h` - Engine-wide definitions and OpenGL setup

## Development Notes

- The application uses C++17 standard
- OpenGL 4.6 is required for advanced rendering features
- All rendering is performed in the main thread
- The cursor system uses depth buffer sampling for 3D positioning
- Voxel cone tracing is implemented as a single-pass algorithm with dynamic voxelization
- Scene transformations use GLM for all mathematical operations

## Stereo Rendering Pipeline

The stereo rendering happens in `renderEye()` function with separate left/right eye passes:
1. Calculate asymmetric frustum matrices for each eye
2. Render scene objects (models, point clouds, cursors)
3. Apply post-processing effects (voxelization, shadows)
4. Present to appropriate buffer (GL_BACK_LEFT/GL_BACK_RIGHT)

## Asset Pipeline

- Models: Loaded via Assimp, converted to internal Engine::Model format
- Point Clouds: Support for XYZ, PLY, PCB formats with chunked loading
- Textures: STB-based image loading for all material maps
- Shaders: Runtime compilation with error reporting