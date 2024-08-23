# OpenGL Stereo Test Program

This project is an OpenGL-based test program demonstrating stereo rendering and advanced 3D cursor implementation. It serves as a showcase for various OpenGL techniques and interactive 3D environment manipulation.

## Key Features

### Stereo Rendering
- Implements stereo rendering for an immersive 3D experience.
- Adjustable stereo separation and convergence settings.

### Advanced Camera System
- Orbit camera mode for smooth object inspection.
- Panning and zooming capabilities.
- Animated camera transitions for a more fluid user experience.
- Dynamic speed adjustment based on scene complexity and distance to objects.

### 3D Cursor System
- Dual cursor implementation:
  1. Sphere Cursor:
     - 3D sphere that follows the mouse in world space.
     - Adjustable size, color, and transparency.
     - Multiple scaling modes (Normal, Fixed, Constrained Dynamic, Logarithmic).
     - Optional inner sphere for enhanced depth perception.
  2. Fragment Shader Cursor:
     - 2D cursor rendered in the fragment shader.
     - Customizable appearance with inner and outer rings.

### Object Manipulation
- Load and manipulate 3D models in real-time.
- Translate, rotate, and scale objects interactively.
- Apply custom textures and material properties to objects.
- Create and delete simple geometric shapes (e.g., cubes) dynamically.

### Scene Management
- Save and load scenes, preserving object positions, properties, and camera settings.
- Manage multiple objects within the scene.

### Lighting System
- Dynamic point light generation from emissive objects.
- Supports multiple light sources for complex scene illumination.

### User Interface
- ImGui-based interface for easy parameter adjustment and scene control.
- Toggle-able GUI for unobstructed view of the 3D environment.

## Controls

- Left Mouse Button: Orbit camera / Select and move objects (in selection mode)
- Middle Mouse Button: Pan camera
- Right Mouse Button: Rotate camera
- Scroll Wheel: Zoom in/out
- WASD: Move camera
- Ctrl: Enter selection mode(Hold Ctrl and press Left Mouse Button over an object to select it)
- Delete: Remove selected object
- G: Toggle GUI visibility

## Getting Started

1. Clone the repository
2. Run the Visual Studio Solution

## Notes for Developers

- The camera logic is implemented in the `Camera` class, handling orbiting, panning, and smooth transitions.
- Cursor rendering is split between the sphere cursor (rendered as a 3D object) and the fragment shader cursor (rendered in 2D).
- Object editing is performed through the GUI or by direct manipulation in the 3D space when in selection mode.


##Used Libraries

Libraries and Dependencies
This project relies on the following libraries:

OpenGL: Core graphics API
GLFW: Window creation and management
GLAD: OpenGL function loader
GLM (OpenGL Mathematics): Mathematics library for graphics
ImGui: User interface library
stb_image: Image loading library
tinyobjloader: Wavefront .obj file loader
json.hpp: JSON for Modern C++
portable-file-dialogs: Cross-platform file dialog library

##Known Issues

Normal map loading is currently not functioning correctly. Therefore the UI option is disabled.
The Windows cursor sometimes remains visible over and object when transitioning directly to a model from the UI.

##Planned Improvements

Fix normal map loading and implementation.
Add support for reflection maps to enhance material realism.
Refactor 3D cursor logic into a separate file for better code organization.
Move UI setup code to a dedicated file to improve modularity.
Implement additional cursor types for various use cases.
Add shadow rendering capabilities for more realistic lighting.
Introduce global illumination using techniques like Voxel Cone Tracing.
General code modularization to improve maintainability and extensibility.

We welcome contributions and suggestions to improve this test program!