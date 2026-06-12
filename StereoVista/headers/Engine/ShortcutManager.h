#pragma once

#include <GLFW/glfw3.h>
#include <json.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace StereoVista {

// All rebindable actions in the application
enum class ShortcutAction {
  // View/Display Controls
  ToggleGUI,       // G - Show/hide GUI
  ToggleFPS,       // Toggle FPS counter
  ToggleWireframe, // Toggle wireframe mode
  ToggleRadar,     // Toggle radar/mini-map
  ToggleZeroPlane, // Toggle zero plane visibility

  // Camera Controls
  CenterView,              // C - Center on cursor/selected/scene
  ToggleZoomToCursor,      // Toggle zoom to cursor behavior
  ToggleOrbitAroundCursor, // Toggle orbit around cursor
  ToggleSpaceMouseMode,    // Toggle SpaceMouse navigation mode (CAD/Drone)
  ResetCamera,             // Home - Reset camera to scene default position

  // Lighting
  CycleLighting,  // L - Cycle lighting modes
  ToggleShadows,  // K - Toggle shadows
  ToggleHDR,      // Toggle HDR rendering
  ToggleBloom,    // Toggle bloom effect
  TogglePCSS,     // Toggle soft shadows (PCSS)
  ToggleSunLight, // Toggle sun/directional light

  // Materials & Rendering
  TogglePBR, // Toggle PBR materials

  // VCT (Voxel Cone Tracing)
  ToggleVoxelViz,            // V - Toggle voxel visualization
  ToggleVCTIndirectDiffuse,  // Toggle VCT indirect diffuse
  ToggleVCTIndirectSpecular, // Toggle VCT indirect specular
  ToggleVCTDirectLight,      // Toggle VCT direct lighting
  ToggleVCTSoftShadows,      // Toggle VCT soft shadows

  // Raytracing
  ToggleRaytracing,       // Toggle raytracing
  ToggleIndirectLighting, // Toggle indirect lighting (RT)
  ToggleEmissiveLighting, // Toggle emissive lighting
  ToggleBVH,              // Toggle BVH acceleration
  ClearIrradianceCache,   // Clear irradiance cache (forces recomputation)

  // 3D Cursor
  ToggleSphereCursor, // Toggle 3D sphere cursor
  ToggleCircleCursor, // Toggle 2D circle cursor
  TogglePlaneCursor,  // Toggle surface plane cursor

  // Window Management
  OpenSettings,        // Open settings window
  OpenCursorSettings,  // Open cursor settings
  OpenBrushTool,       // Open brush tool window
  OpenMeasurementTool, // Open measurement tool window

  // File Operations
  ImportModel,      // Open import model dialog
  ImportPointCloud, // Open import point cloud dialog
  SaveScene,        // Open save scene dialog
  LoadScene,        // Open load scene dialog

  // Object Manipulation
  DeleteObject, // Delete - Delete selected object

  // Reserved for future actions
  ACTION_COUNT
};

// Represents a single key binding (key + modifiers or mouse button + modifiers)
struct KeyBinding {
  int keyCode;     // GLFW key code (GLFW_KEY_*) or -1 if mouse button
  int mouseButton; // GLFW mouse button (GLFW_MOUSE_BUTTON_*) or -1 if keyboard
  bool ctrl;       // Requires Ctrl modifier
  bool alt;        // Requires Alt modifier
  bool shift;      // Requires Shift modifier

  KeyBinding()
      : keyCode(-1), mouseButton(-1), ctrl(false), alt(false), shift(false) {}

  KeyBinding(int key, bool ctrl = false, bool alt = false, bool shift = false)
      : keyCode(key), mouseButton(-1), ctrl(ctrl), alt(alt), shift(shift) {}

  static KeyBinding MouseButton(int button, bool ctrl = false, bool alt = false,
                                bool shift = false) {
    KeyBinding binding;
    binding.mouseButton = button;
    binding.keyCode = -1;
    binding.ctrl = ctrl;
    binding.alt = alt;
    binding.shift = shift;
    return binding;
  }

  bool isValid() const { return keyCode != -1 || mouseButton != -1; }

  bool isEmpty() const { return !isValid(); }

  bool matches(int key, int mods) const {
    if (keyCode == -1)
      return false;
    return keyCode == key && ctrl == ((mods & GLFW_MOD_CONTROL) != 0) &&
           alt == ((mods & GLFW_MOD_ALT) != 0) &&
           shift == ((mods & GLFW_MOD_SHIFT) != 0);
  }

  bool matchesMouse(int button, int mods) const {
    if (mouseButton == -1)
      return false;
    return mouseButton == button && ctrl == ((mods & GLFW_MOD_CONTROL) != 0) &&
           alt == ((mods & GLFW_MOD_ALT) != 0) &&
           shift == ((mods & GLFW_MOD_SHIFT) != 0);
  }

  bool operator==(const KeyBinding &other) const {
    return keyCode == other.keyCode && mouseButton == other.mouseButton &&
           ctrl == other.ctrl && alt == other.alt && shift == other.shift;
  }

  bool operator!=(const KeyBinding &other) const { return !(*this == other); }

  // Convert to human-readable string (e.g., "Ctrl+G", "Ctrl+Click", "Delete")
  std::string toString() const;

  // Convert to/from JSON
  nlohmann::json toJson() const;
  static KeyBinding fromJson(const nlohmann::json &j);
};

// A profile containing all shortcut bindings
class ShortcutProfile {
public:
  std::string name;
  std::map<ShortcutAction, std::vector<KeyBinding>>
      bindings; // Up to 2 bindings per action

  ShortcutProfile(const std::string &name = "Default");

  // Get bindings for an action (returns empty vector if none)
  const std::vector<KeyBinding> &getBindings(ShortcutAction action) const;

  // Set bindings for an action (max 2 bindings)
  void setBinding(ShortcutAction action, const KeyBinding &binding,
                  int slot = 0);

  // Remove a binding
  void clearBinding(ShortcutAction action, int slot);

  // Check if a binding is already used by another action
  std::optional<ShortcutAction>
  findConflict(const KeyBinding &binding, ShortcutAction excludeAction) const;

  // Convert to/from JSON
  nlohmann::json toJson() const;
  static ShortcutProfile fromJson(const nlohmann::json &j);
};

// Main shortcut manager - handles profiles, persistence, and action lookup
class ShortcutManager {
public:
  ShortcutManager();
  ~ShortcutManager();

  // Profile management
  void addProfile(const ShortcutProfile &profile);
  void deleteProfile(const std::string &name);
  void renameProfile(const std::string &oldName, const std::string &newName);
  void setActiveProfile(const std::string &name);
  const std::string &getActiveProfileName() const { return activeProfileName; }
  ShortcutProfile *getActiveProfile();
  const ShortcutProfile *getActiveProfile() const;
  const std::vector<std::string> &getProfileNames() const {
    return profileNames;
  }

  // Action lookup
  std::optional<ShortcutAction> getActionForKey(int key, int mods) const;
  std::optional<ShortcutAction> getActionForMouse(int button, int mods) const;

  // Get human-readable name for an action
  static std::string getActionName(ShortcutAction action);
  static std::string getActionDescription(ShortcutAction action);

  // Default bindings
  static ShortcutProfile createDefaultProfile();
  void resetActiveProfileToDefaults();

  // Persistence
  bool saveToFile(const std::string &filepath) const;
  bool loadFromFile(const std::string &filepath);
  bool importProfileFromFile(const std::string &filepath);
  bool exportProfileToFile(const std::string &profileName,
                           const std::string &filepath) const;

private:
  std::vector<ShortcutProfile> profiles;
  std::vector<std::string> profileNames;
  std::string activeProfileName;
  int activeProfileIndex;

  void updateProfileNames();
  int findProfileIndex(const std::string &name) const;
};

} // namespace StereoVista
