#include "../../headers/Engine/ShortcutManager.h"
#include <fstream>
#include <sstream>

namespace StereoVista {

// Helper function to get key name from GLFW key code
static std::string getKeyName(int keyCode) {
  // Special keys
  switch (keyCode) {
  case GLFW_KEY_SPACE:
    return "Space";
  case GLFW_KEY_APOSTROPHE:
    return "'";
  case GLFW_KEY_COMMA:
    return ",";
  case GLFW_KEY_MINUS:
    return "-";
  case GLFW_KEY_PERIOD:
    return ".";
  case GLFW_KEY_SLASH:
    return "/";
  case GLFW_KEY_0:
    return "0";
  case GLFW_KEY_1:
    return "1";
  case GLFW_KEY_2:
    return "2";
  case GLFW_KEY_3:
    return "3";
  case GLFW_KEY_4:
    return "4";
  case GLFW_KEY_5:
    return "5";
  case GLFW_KEY_6:
    return "6";
  case GLFW_KEY_7:
    return "7";
  case GLFW_KEY_8:
    return "8";
  case GLFW_KEY_9:
    return "9";
  case GLFW_KEY_SEMICOLON:
    return ";";
  case GLFW_KEY_EQUAL:
    return "=";
  case GLFW_KEY_A:
    return "A";
  case GLFW_KEY_B:
    return "B";
  case GLFW_KEY_C:
    return "C";
  case GLFW_KEY_D:
    return "D";
  case GLFW_KEY_E:
    return "E";
  case GLFW_KEY_F:
    return "F";
  case GLFW_KEY_G:
    return "G";
  case GLFW_KEY_H:
    return "H";
  case GLFW_KEY_I:
    return "I";
  case GLFW_KEY_J:
    return "J";
  case GLFW_KEY_K:
    return "K";
  case GLFW_KEY_L:
    return "L";
  case GLFW_KEY_M:
    return "M";
  case GLFW_KEY_N:
    return "N";
  case GLFW_KEY_O:
    return "O";
  case GLFW_KEY_P:
    return "P";
  case GLFW_KEY_Q:
    return "Q";
  case GLFW_KEY_R:
    return "R";
  case GLFW_KEY_S:
    return "S";
  case GLFW_KEY_T:
    return "T";
  case GLFW_KEY_U:
    return "U";
  case GLFW_KEY_V:
    return "V";
  case GLFW_KEY_W:
    return "W";
  case GLFW_KEY_X:
    return "X";
  case GLFW_KEY_Y:
    return "Y";
  case GLFW_KEY_Z:
    return "Z";
  case GLFW_KEY_LEFT_BRACKET:
    return "[";
  case GLFW_KEY_BACKSLASH:
    return "\\";
  case GLFW_KEY_RIGHT_BRACKET:
    return "]";
  case GLFW_KEY_GRAVE_ACCENT:
    return "`";
  case GLFW_KEY_ESCAPE:
    return "Esc";
  case GLFW_KEY_ENTER:
    return "Enter";
  case GLFW_KEY_TAB:
    return "Tab";
  case GLFW_KEY_BACKSPACE:
    return "Backspace";
  case GLFW_KEY_INSERT:
    return "Insert";
  case GLFW_KEY_DELETE:
    return "Delete";
  case GLFW_KEY_RIGHT:
    return "Right";
  case GLFW_KEY_LEFT:
    return "Left";
  case GLFW_KEY_DOWN:
    return "Down";
  case GLFW_KEY_UP:
    return "Up";
  case GLFW_KEY_PAGE_UP:
    return "Page Up";
  case GLFW_KEY_PAGE_DOWN:
    return "Page Down";
  case GLFW_KEY_HOME:
    return "Home";
  case GLFW_KEY_END:
    return "End";
  case GLFW_KEY_CAPS_LOCK:
    return "Caps Lock";
  case GLFW_KEY_SCROLL_LOCK:
    return "Scroll Lock";
  case GLFW_KEY_NUM_LOCK:
    return "Num Lock";
  case GLFW_KEY_PRINT_SCREEN:
    return "Print Screen";
  case GLFW_KEY_PAUSE:
    return "Pause";
  case GLFW_KEY_F1:
    return "F1";
  case GLFW_KEY_F2:
    return "F2";
  case GLFW_KEY_F3:
    return "F3";
  case GLFW_KEY_F4:
    return "F4";
  case GLFW_KEY_F5:
    return "F5";
  case GLFW_KEY_F6:
    return "F6";
  case GLFW_KEY_F7:
    return "F7";
  case GLFW_KEY_F8:
    return "F8";
  case GLFW_KEY_F9:
    return "F9";
  case GLFW_KEY_F10:
    return "F10";
  case GLFW_KEY_F11:
    return "F11";
  case GLFW_KEY_F12:
    return "F12";
  case GLFW_KEY_KP_0:
    return "Numpad 0";
  case GLFW_KEY_KP_1:
    return "Numpad 1";
  case GLFW_KEY_KP_2:
    return "Numpad 2";
  case GLFW_KEY_KP_3:
    return "Numpad 3";
  case GLFW_KEY_KP_4:
    return "Numpad 4";
  case GLFW_KEY_KP_5:
    return "Numpad 5";
  case GLFW_KEY_KP_6:
    return "Numpad 6";
  case GLFW_KEY_KP_7:
    return "Numpad 7";
  case GLFW_KEY_KP_8:
    return "Numpad 8";
  case GLFW_KEY_KP_9:
    return "Numpad 9";
  case GLFW_KEY_KP_DECIMAL:
    return "Numpad .";
  case GLFW_KEY_KP_DIVIDE:
    return "Numpad /";
  case GLFW_KEY_KP_MULTIPLY:
    return "Numpad *";
  case GLFW_KEY_KP_SUBTRACT:
    return "Numpad -";
  case GLFW_KEY_KP_ADD:
    return "Numpad +";
  case GLFW_KEY_KP_ENTER:
    return "Numpad Enter";
  case GLFW_KEY_KP_EQUAL:
    return "Numpad =";
  case GLFW_KEY_LEFT_SHIFT:
    return "Left Shift";
  case GLFW_KEY_LEFT_CONTROL:
    return "Left Ctrl";
  case GLFW_KEY_LEFT_ALT:
    return "Left Alt";
  case GLFW_KEY_LEFT_SUPER:
    return "Left Super";
  case GLFW_KEY_RIGHT_SHIFT:
    return "Right Shift";
  case GLFW_KEY_RIGHT_CONTROL:
    return "Right Ctrl";
  case GLFW_KEY_RIGHT_ALT:
    return "Right Alt";
  case GLFW_KEY_RIGHT_SUPER:
    return "Right Super";
  case GLFW_KEY_MENU:
    return "Menu";
  default:
    return "Unknown";
  }
}

// KeyBinding implementation
std::string KeyBinding::toString() const {
  if (!isValid())
    return "Unbound";

  std::string result;

  // Add modifiers
  if (ctrl)
    result += "Ctrl+";
  if (alt)
    result += "Alt+";
  if (shift)
    result += "Shift+";

  // Add key or mouse button
  if (mouseButton != -1) {
    switch (mouseButton) {
    case GLFW_MOUSE_BUTTON_LEFT:
      result += "Left Click";
      break;
    case GLFW_MOUSE_BUTTON_RIGHT:
      result += "Right Click";
      break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      result += "Middle Click";
      break;
    default:
      result += "Mouse " + std::to_string(mouseButton + 1);
      break;
    }
  } else if (keyCode != -1) {
    result += getKeyName(keyCode);
  }

  return result;
}

nlohmann::json KeyBinding::toJson() const {
  nlohmann::json j;
  j["keyCode"] = keyCode;
  j["mouseButton"] = mouseButton;
  j["ctrl"] = ctrl;
  j["alt"] = alt;
  j["shift"] = shift;
  return j;
}

KeyBinding KeyBinding::fromJson(const nlohmann::json &j) {
  KeyBinding binding;
  if (j.contains("keyCode"))
    binding.keyCode = j["keyCode"];
  if (j.contains("mouseButton"))
    binding.mouseButton = j["mouseButton"];
  if (j.contains("ctrl"))
    binding.ctrl = j["ctrl"];
  if (j.contains("alt"))
    binding.alt = j["alt"];
  if (j.contains("shift"))
    binding.shift = j["shift"];
  return binding;
}

// ShortcutProfile implementation
ShortcutProfile::ShortcutProfile(const std::string &name) : name(name) {
  // Initialize empty bindings for all actions
  for (int i = 0; i < static_cast<int>(ShortcutAction::ACTION_COUNT); ++i) {
    bindings[static_cast<ShortcutAction>(i)] = std::vector<KeyBinding>();
  }
}

const std::vector<KeyBinding> &
ShortcutProfile::getBindings(ShortcutAction action) const {
  static const std::vector<KeyBinding> emptyBindings;
  auto it = bindings.find(action);
  if (it != bindings.end()) {
    return it->second;
  }
  return emptyBindings;
}

void ShortcutProfile::setBinding(ShortcutAction action,
                                 const KeyBinding &binding, int slot) {
  if (slot < 0 || slot > 1)
    return; // Only support 2 bindings per action

  auto &actionBindings = bindings[action];

  // Ensure the vector has enough space
  while (actionBindings.size() <= slot) {
    actionBindings.push_back(KeyBinding());
  }

  actionBindings[slot] = binding;
}

void ShortcutProfile::clearBinding(ShortcutAction action, int slot) {
  auto it = bindings.find(action);
  if (it != bindings.end() && slot >= 0 && slot < it->second.size()) {
    it->second[slot] = KeyBinding(); // Set to empty binding
  }
}

std::optional<ShortcutAction>
ShortcutProfile::findConflict(const KeyBinding &binding,
                              ShortcutAction excludeAction) const {
  if (!binding.isValid())
    return std::nullopt;

  for (const auto &[action, actionBindings] : bindings) {
    if (action == excludeAction)
      continue;

    for (const auto &existingBinding : actionBindings) {
      if (existingBinding == binding) {
        return action;
      }
    }
  }

  return std::nullopt;
}

nlohmann::json ShortcutProfile::toJson() const {
  nlohmann::json j;
  j["name"] = name;
  j["bindings"] = nlohmann::json::object();

  for (const auto &[action, actionBindings] : bindings) {
    std::string actionName = ShortcutManager::getActionName(action);
    nlohmann::json bindingsArray = nlohmann::json::array();

    for (const auto &binding : actionBindings) {
      if (binding.isValid()) {
        bindingsArray.push_back(binding.toJson());
      }
    }

    if (!bindingsArray.empty()) {
      j["bindings"][actionName] = bindingsArray;
    }
  }

  return j;
}

ShortcutProfile ShortcutProfile::fromJson(const nlohmann::json &j) {
  std::string profileName = j.value("name", "Imported");
  ShortcutProfile profile(profileName);

  if (j.contains("bindings") && j["bindings"].is_object()) {
    for (auto &[actionNameStr, bindingsArray] : j["bindings"].items()) {
      // Find action enum from name
      bool foundAction = false;
      for (int i = 0; i < static_cast<int>(ShortcutAction::ACTION_COUNT); ++i) {
        ShortcutAction action = static_cast<ShortcutAction>(i);
        if (ShortcutManager::getActionName(action) == actionNameStr) {
          // Load bindings for this action
          if (bindingsArray.is_array()) {
            int slot = 0;
            for (const auto &bindingJson : bindingsArray) {
              if (slot < 2) { // Max 2 bindings
                KeyBinding binding = KeyBinding::fromJson(bindingJson);
                profile.setBinding(action, binding, slot);
                slot++;
              }
            }
          }
          foundAction = true;
          break;
        }
      }
      // If action not found, it's gracefully ignored (missing shortcut
      // handling)
    }
  }

  return profile;
}

// ShortcutManager implementation
ShortcutManager::ShortcutManager() : activeProfileIndex(0) {
  // Create default profile
  ShortcutProfile defaultProfile = createDefaultProfile();
  profiles.push_back(defaultProfile);
  activeProfileName = defaultProfile.name;
  updateProfileNames();
}

ShortcutManager::~ShortcutManager() {}

ShortcutProfile ShortcutManager::createDefaultProfile() {
  ShortcutProfile profile("Default");

  // Set default bindings based on original hardcoded shortcuts
  profile.setBinding(ShortcutAction::ToggleGUI, KeyBinding(GLFW_KEY_G), 0);
  profile.setBinding(ShortcutAction::TakeScreenshot, KeyBinding(GLFW_KEY_F12),
                     0);
  profile.setBinding(ShortcutAction::CenterView, KeyBinding(GLFW_KEY_C), 0);
  profile.setBinding(ShortcutAction::CycleLighting, KeyBinding(GLFW_KEY_L), 0);
  profile.setBinding(ShortcutAction::ToggleShadows, KeyBinding(GLFW_KEY_K), 0);
  profile.setBinding(ShortcutAction::ToggleVoxelViz, KeyBinding(GLFW_KEY_V), 0);
  profile.setBinding(ShortcutAction::DeleteObject, KeyBinding(GLFW_KEY_DELETE),
                     0);
  profile.setBinding(ShortcutAction::ToggleSpaceMouseMode,
                     KeyBinding(GLFW_KEY_M, true, false, true),
                     0); // Ctrl+Shift+M
  profile.setBinding(ShortcutAction::ClearIrradianceCache,
                     KeyBinding(GLFW_KEY_I, true, false, true),
                     0); // Ctrl+Shift+I
  profile.setBinding(ShortcutAction::ResetCamera, KeyBinding(GLFW_KEY_HOME), 0);
  profile.setBinding(ShortcutAction::OpenMeasurementTool, KeyBinding(GLFW_KEY_M),
                     0);
  profile.setBinding(ShortcutAction::OpenClipPlaneTool, KeyBinding(GLFW_KEY_P),
                     0);
  profile.setBinding(ShortcutAction::Undo, KeyBinding(GLFW_KEY_Z, true), 0);
  profile.setBinding(ShortcutAction::Redo, KeyBinding(GLFW_KEY_Y, true), 0);
  profile.setBinding(ShortcutAction::Redo,
                     KeyBinding(GLFW_KEY_Z, true, false, true),
                     1); // Ctrl+Shift+Z

  // View shading
  profile.setBinding(ShortcutAction::ToggleUnlit, KeyBinding(GLFW_KEY_U), 0);

  // Transform gizmo (active while an object is selected)
  profile.setBinding(ShortcutAction::GizmoModeMove, KeyBinding(GLFW_KEY_1), 0);
  profile.setBinding(ShortcutAction::GizmoModeRotate, KeyBinding(GLFW_KEY_2), 0);
  profile.setBinding(ShortcutAction::GizmoModeScale, KeyBinding(GLFW_KEY_3), 0);
  profile.setBinding(ShortcutAction::GizmoToggleSpace, KeyBinding(GLFW_KEY_4), 0);

  // New actions start unbound - users can assign keys as needed

  return profile;
}

void ShortcutManager::addProfile(const ShortcutProfile &profile) {
  profiles.push_back(profile);
  updateProfileNames();
}

void ShortcutManager::deleteProfile(const std::string &name) {
  if (profiles.size() <= 1)
    return; // Keep at least one profile

  int index = findProfileIndex(name);
  if (index != -1) {
    profiles.erase(profiles.begin() + index);

    // If we deleted the active profile, switch to the first one
    if (name == activeProfileName) {
      activeProfileIndex = 0;
      activeProfileName = profiles[0].name;
    } else if (index < activeProfileIndex) {
      activeProfileIndex--;
    }

    updateProfileNames();
  }
}

void ShortcutManager::renameProfile(const std::string &oldName,
                                    const std::string &newName) {
  int index = findProfileIndex(oldName);
  if (index != -1) {
    profiles[index].name = newName;
    if (activeProfileName == oldName) {
      activeProfileName = newName;
    }
    updateProfileNames();
  }
}

void ShortcutManager::setActiveProfile(const std::string &name) {
  int index = findProfileIndex(name);
  if (index != -1) {
    activeProfileIndex = index;
    activeProfileName = name;
  }
}

ShortcutProfile *ShortcutManager::getActiveProfile() {
  if (activeProfileIndex >= 0 && activeProfileIndex < profiles.size()) {
    return &profiles[activeProfileIndex];
  }
  return nullptr;
}

const ShortcutProfile *ShortcutManager::getActiveProfile() const {
  if (activeProfileIndex >= 0 && activeProfileIndex < profiles.size()) {
    return &profiles[activeProfileIndex];
  }
  return nullptr;
}

int ShortcutManager::normalizeKeyToLayout(int key, int scancode) {
  // glfwGetKeyName returns the character the physical key produces in the
  // current layout (e.g. on QWERTZ the GLFW_KEY_Y position is labeled "z").
  // When 'key' is a known token GLFW ignores 'scancode' and looks the name up
  // from the layout directly, so this works for both real key events and the
  // GUI capture path (which has no scancode).
  const char *name = glfwGetKeyName(key, scancode);
  if (name && name[0] != '\0' && name[1] == '\0') {
    char c = name[0];
    if (c >= 'a' && c <= 'z')
      return GLFW_KEY_A + (c - 'a');
    if (c >= 'A' && c <= 'Z')
      return GLFW_KEY_A + (c - 'A');
    if (c >= '0' && c <= '9')
      return GLFW_KEY_0 + (c - '0');
  }
  return key;
}

std::optional<ShortcutAction> ShortcutManager::getActionForKey(int key,
                                                               int mods) const {
  const ShortcutProfile *profile = getActiveProfile();
  if (!profile)
    return std::nullopt;

  for (const auto &[action, bindings] : profile->bindings) {
    for (const auto &binding : bindings) {
      if (binding.matches(key, mods)) {
        return action;
      }
    }
  }

  return std::nullopt;
}

std::optional<ShortcutAction>
ShortcutManager::getActionForMouse(int button, int mods) const {
  const ShortcutProfile *profile = getActiveProfile();
  if (!profile)
    return std::nullopt;

  for (const auto &[action, bindings] : profile->bindings) {
    for (const auto &binding : bindings) {
      if (binding.matchesMouse(button, mods)) {
        return action;
      }
    }
  }

  return std::nullopt;
}

std::string ShortcutManager::getActionName(ShortcutAction action) {
  switch (action) {
  // View/Display Controls
  case ShortcutAction::ToggleGUI:
    return "ToggleGUI";
  case ShortcutAction::ToggleFPS:
    return "ToggleFPS";
  case ShortcutAction::ToggleWireframe:
    return "ToggleWireframe";
  case ShortcutAction::ToggleRadar:
    return "ToggleRadar";
  case ShortcutAction::ToggleZeroPlane:
    return "ToggleZeroPlane";
  case ShortcutAction::TakeScreenshot:
    return "TakeScreenshot";

  // Camera Controls
  case ShortcutAction::CenterView:
    return "CenterView";
  case ShortcutAction::ToggleZoomToCursor:
    return "ToggleZoomToCursor";
  case ShortcutAction::ToggleOrbitAroundCursor:
    return "ToggleOrbitAroundCursor";
  case ShortcutAction::ResetCamera:
    return "ResetCamera";

  // Lighting
  case ShortcutAction::CycleLighting:
    return "CycleLighting";
  case ShortcutAction::ToggleShadows:
    return "ToggleShadows";
  case ShortcutAction::ToggleHDR:
    return "ToggleHDR";
  case ShortcutAction::ToggleBloom:
    return "ToggleBloom";
  case ShortcutAction::TogglePCSS:
    return "TogglePCSS";
  case ShortcutAction::ToggleSunLight:
    return "ToggleSunLight";

  // Materials & Rendering
  case ShortcutAction::TogglePBR:
    return "TogglePBR";
  case ShortcutAction::ToggleUnlit:
    return "ToggleUnlit";

  // VCT
  case ShortcutAction::ToggleVoxelViz:
    return "ToggleVoxelViz";
  case ShortcutAction::ToggleVCTIndirectDiffuse:
    return "ToggleVCTIndirectDiffuse";
  case ShortcutAction::ToggleVCTIndirectSpecular:
    return "ToggleVCTIndirectSpecular";
  case ShortcutAction::ToggleVCTDirectLight:
    return "ToggleVCTDirectLight";
  case ShortcutAction::ToggleVCTSoftShadows:
    return "ToggleVCTSoftShadows";

  // Raytracing
  case ShortcutAction::ToggleRaytracing:
    return "ToggleRaytracing";
  case ShortcutAction::ToggleIndirectLighting:
    return "ToggleIndirectLighting";
  case ShortcutAction::ToggleEmissiveLighting:
    return "ToggleEmissiveLighting";
  case ShortcutAction::ToggleBVH:
    return "ToggleBVH";
  case ShortcutAction::ClearIrradianceCache:
    return "ClearIrradianceCache";

  // 3D Cursor
  case ShortcutAction::ToggleSphereCursor:
    return "ToggleSphereCursor";
  case ShortcutAction::ToggleCircleCursor:
    return "ToggleCircleCursor";
  case ShortcutAction::TogglePlaneCursor:
    return "TogglePlaneCursor";

  // Window Management
  case ShortcutAction::OpenSettings:
    return "OpenSettings";
  case ShortcutAction::OpenCursorSettings:
    return "OpenCursorSettings";
  case ShortcutAction::OpenBrushTool:
    return "OpenBrushTool";
  case ShortcutAction::OpenMeasurementTool:
    return "OpenMeasurementTool";
  case ShortcutAction::OpenClipPlaneTool:
    return "OpenClipPlaneTool";

  // File Operations
  case ShortcutAction::ImportModel:
    return "ImportModel";
  case ShortcutAction::ImportPointCloud:
    return "ImportPointCloud";
  case ShortcutAction::SaveScene:
    return "SaveScene";
  case ShortcutAction::LoadScene:
    return "LoadScene";

  // Object Manipulation
  case ShortcutAction::DeleteObject:
    return "DeleteObject";

  // Edit History
  case ShortcutAction::Undo:
    return "Undo";
  case ShortcutAction::Redo:
    return "Redo";

  // Transform Gizmo
  case ShortcutAction::GizmoModeMove:
    return "GizmoModeMove";
  case ShortcutAction::GizmoModeRotate:
    return "GizmoModeRotate";
  case ShortcutAction::GizmoModeScale:
    return "GizmoModeScale";
  case ShortcutAction::GizmoToggleSpace:
    return "GizmoToggleSpace";

  default:
    return "Unknown";
  }
}

std::string ShortcutManager::getActionDescription(ShortcutAction action) {
  switch (action) {
  // View/Display Controls
  case ShortcutAction::ToggleGUI:
    return "Toggle GUI Visibility";
  case ShortcutAction::ToggleFPS:
    return "Toggle FPS Counter";
  case ShortcutAction::ToggleWireframe:
    return "Toggle Wireframe Mode";
  case ShortcutAction::ToggleRadar:
    return "Toggle Radar/Mini-Map";
  case ShortcutAction::ToggleZeroPlane:
    return "Toggle Zero Plane";
  case ShortcutAction::TakeScreenshot:
    return "Save Screenshot to Image File";

  // Camera Controls
  case ShortcutAction::CenterView:
    return "Center View on Cursor/Object";
  case ShortcutAction::ToggleZoomToCursor:
    return "Toggle Zoom to Cursor";
  case ShortcutAction::ToggleOrbitAroundCursor:
    return "Toggle Orbit Around Cursor";
  case ShortcutAction::ToggleSpaceMouseMode:
    return "Toggle SpaceMouse Mode (CAD/Drone)";
  case ShortcutAction::ResetCamera:
    return "Reset Camera to Scene Default";

  // Lighting
  case ShortcutAction::CycleLighting:
    return "Cycle Lighting Mode";
  case ShortcutAction::ToggleShadows:
    return "Toggle Shadows";
  case ShortcutAction::ToggleHDR:
    return "Toggle HDR";
  case ShortcutAction::ToggleBloom:
    return "Toggle Bloom Effect";
  case ShortcutAction::TogglePCSS:
    return "Toggle Soft Shadows (PCSS)";
  case ShortcutAction::ToggleSunLight:
    return "Toggle Sun Light";

  // Materials & Rendering
  case ShortcutAction::TogglePBR:
    return "Toggle PBR Materials";
  case ShortcutAction::ToggleUnlit:
    return "Toggle Unlit (Albedo) Shading";

  // VCT
  case ShortcutAction::ToggleVoxelViz:
    return "Toggle Voxel Visualization";
  case ShortcutAction::ToggleVCTIndirectDiffuse:
    return "Toggle VCT Indirect Diffuse";
  case ShortcutAction::ToggleVCTIndirectSpecular:
    return "Toggle VCT Indirect Specular";
  case ShortcutAction::ToggleVCTDirectLight:
    return "Toggle VCT Direct Lighting";
  case ShortcutAction::ToggleVCTSoftShadows:
    return "Toggle VCT Soft Shadows";

  // Raytracing
  case ShortcutAction::ToggleRaytracing:
    return "Toggle Raytracing";
  case ShortcutAction::ToggleIndirectLighting:
    return "Toggle Indirect Lighting";
  case ShortcutAction::ToggleEmissiveLighting:
    return "Toggle Emissive Lighting";
  case ShortcutAction::ToggleBVH:
    return "Toggle BVH Acceleration";
  case ShortcutAction::ClearIrradianceCache:
    return "Clear Irradiance Cache";

  // 3D Cursor
  case ShortcutAction::ToggleSphereCursor:
    return "Toggle 3D Sphere Cursor";
  case ShortcutAction::ToggleCircleCursor:
    return "Toggle 2D Circle Cursor";
  case ShortcutAction::TogglePlaneCursor:
    return "Toggle Surface Plane Cursor";

  // Window Management
  case ShortcutAction::OpenSettings:
    return "Open Settings Window";
  case ShortcutAction::OpenCursorSettings:
    return "Open Cursor Settings";
  case ShortcutAction::OpenBrushTool:
    return "Open Brush Tool Window";
  case ShortcutAction::OpenMeasurementTool:
    return "Open Measurement Tool Window";
  case ShortcutAction::OpenClipPlaneTool:
    return "Open Section / Clip Plane Tool Window";

  // File Operations
  case ShortcutAction::ImportModel:
    return "Import Model";
  case ShortcutAction::ImportPointCloud:
    return "Import Point Cloud";
  case ShortcutAction::SaveScene:
    return "Save Scene";
  case ShortcutAction::LoadScene:
    return "Load Scene";

  // Object Manipulation
  case ShortcutAction::DeleteObject:
    return "Delete Selected Object";

  // Edit History
  case ShortcutAction::Undo:
    return "Undo Last Action";
  case ShortcutAction::Redo:
    return "Redo Last Undone Action";

  // Transform Gizmo
  case ShortcutAction::GizmoModeMove:
    return "Gizmo: Move Mode";
  case ShortcutAction::GizmoModeRotate:
    return "Gizmo: Rotate Mode";
  case ShortcutAction::GizmoModeScale:
    return "Gizmo: Scale Mode";
  case ShortcutAction::GizmoToggleSpace:
    return "Gizmo: Toggle World/Local Space";

  default:
    return "Unknown Action";
  }
}

void ShortcutManager::resetActiveProfileToDefaults() {
  ShortcutProfile *profile = getActiveProfile();
  if (profile) {
    ShortcutProfile defaultProfile = createDefaultProfile();
    profile->bindings = defaultProfile.bindings;
  }
}

bool ShortcutManager::saveToFile(const std::string &filepath) const {
  try {
    nlohmann::json j;
    j["version"] = 1;
    j["activeProfile"] = activeProfileName;
    j["profiles"] = nlohmann::json::array();

    for (const auto &profile : profiles) {
      j["profiles"].push_back(profile.toJson());
    }

    std::ofstream file(filepath);
    if (!file.is_open())
      return false;

    file << j.dump(4);
    file.close();
    return true;
  } catch (...) {
    return false;
  }
}

bool ShortcutManager::loadFromFile(const std::string &filepath) {
  try {
    std::ifstream file(filepath);
    if (!file.is_open())
      return false;

    nlohmann::json j;
    file >> j;
    file.close();

    // Clear existing profiles
    profiles.clear();

    // Load profiles
    if (j.contains("profiles") && j["profiles"].is_array()) {
      for (const auto &profileJson : j["profiles"]) {
        ShortcutProfile profile = ShortcutProfile::fromJson(profileJson);
        profiles.push_back(profile);
      }
    }

    // If no profiles loaded, create default
    if (profiles.empty()) {
      profiles.push_back(createDefaultProfile());
    }

    // Backfill default bindings for actions added after this shortcuts file
    // was written (e.g. Undo/Redo) so new features stay reachable. Default
    // bindings that would conflict with the user's existing bindings are
    // skipped.
    ShortcutProfile defaults = createDefaultProfile();
    for (auto &profile : profiles) {
      for (const auto &[action, defaultBindings] : defaults.bindings) {
        bool hasValidBinding = false;
        for (const auto &binding : profile.getBindings(action)) {
          if (binding.isValid()) {
            hasValidBinding = true;
            break;
          }
        }
        if (hasValidBinding)
          continue;

        int slot = 0;
        for (const auto &binding : defaultBindings) {
          if (binding.isValid() && slot < 2 &&
              !profile.findConflict(binding, action).has_value()) {
            profile.setBinding(action, binding, slot);
            slot++;
          }
        }
      }
    }

    // Set active profile
    if (j.contains("activeProfile")) {
      std::string activeName = j["activeProfile"];
      int index = findProfileIndex(activeName);
      if (index != -1) {
        activeProfileIndex = index;
        activeProfileName = activeName;
      } else {
        activeProfileIndex = 0;
        activeProfileName = profiles[0].name;
      }
    } else {
      activeProfileIndex = 0;
      activeProfileName = profiles[0].name;
    }

    updateProfileNames();
    return true;
  } catch (...) {
    // On error, ensure we have at least a default profile
    if (profiles.empty()) {
      profiles.push_back(createDefaultProfile());
      activeProfileIndex = 0;
      activeProfileName = profiles[0].name;
      updateProfileNames();
    }
    return false;
  }
}

bool ShortcutManager::importProfileFromFile(const std::string &filepath) {
  try {
    std::ifstream file(filepath);
    if (!file.is_open())
      return false;

    nlohmann::json j;
    file >> j;
    file.close();

    // Try to load as a single profile or as a profiles array
    if (j.contains("profiles") && j["profiles"].is_array() &&
        !j["profiles"].empty()) {
      // Load first profile from array
      ShortcutProfile profile = ShortcutProfile::fromJson(j["profiles"][0]);

      // Ensure unique name
      std::string baseName = profile.name;
      int counter = 1;
      while (findProfileIndex(profile.name) != -1) {
        profile.name = baseName + " (" + std::to_string(counter++) + ")";
      }

      addProfile(profile);
      return true;
    } else if (j.contains("name") && j.contains("bindings")) {
      // Load as single profile
      ShortcutProfile profile = ShortcutProfile::fromJson(j);

      // Ensure unique name
      std::string baseName = profile.name;
      int counter = 1;
      while (findProfileIndex(profile.name) != -1) {
        profile.name = baseName + " (" + std::to_string(counter++) + ")";
      }

      addProfile(profile);
      return true;
    }

    return false;
  } catch (...) {
    return false;
  }
}

bool ShortcutManager::exportProfileToFile(const std::string &profileName,
                                          const std::string &filepath) const {
  try {
    int index = findProfileIndex(profileName);
    if (index == -1)
      return false;

    const ShortcutProfile &profile = profiles[index];
    nlohmann::json j = profile.toJson();

    std::ofstream file(filepath);
    if (!file.is_open())
      return false;

    file << j.dump(4);
    file.close();
    return true;
  } catch (...) {
    return false;
  }
}

void ShortcutManager::updateProfileNames() {
  profileNames.clear();
  for (const auto &profile : profiles) {
    profileNames.push_back(profile.name);
  }
}

int ShortcutManager::findProfileIndex(const std::string &name) const {
  for (int i = 0; i < profiles.size(); ++i) {
    if (profiles[i].name == name) {
      return i;
    }
  }
  return -1;
}

} // namespace StereoVista
