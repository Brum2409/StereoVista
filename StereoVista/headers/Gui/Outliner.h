#pragma once

// ============================================================================
//  Gui::Outliner — the Outliner's per-kind content provider registry
//  (UI redesign Pass 1; docs/UI_REDESIGN.md §7.3, coherence contract C6).
// ----------------------------------------------------------------------------
//  The Scene panel (the Outliner) renders three collapsible sections —
//  Environment · Scene objects · Annotations & tool output — whose ROWS come
//  from registered providers, not from hard-wired panel code. A future tool
//  that produces objects registers a provider (plus its ObjectKind style) and
//  its output appears in the tree with zero edits to ScenePanel.cpp (§16.1).
//
//  Providers emit flat Item lists; the panel owns presentation: grouping
//  (user groups from scene::Scene::groups), selection, filtering/search,
//  rename, context menus, drag & drop. Built-in providers for the shipped
//  kinds (models+meshes, point clouds, I3S layers, lights, sun/sky,
//  measurements, clip planes) register themselves on first use.
//
//  Gui layer: pure ImGui/UiKit over Gui::Services — no Vulkan, no renderer.
// ============================================================================

#include "Gui/UiKit.h"
#include "Scene/SceneItems.h"

#include <functional>
#include <string>
#include <vector>

namespace Gui {

class Services;

namespace Outliner {

enum class Section { Environment = 0, SceneObjects, Annotations, Count };

struct Item {
    scene::SceneItemRef ref;
    UiKit::ObjectKind kind = UiKit::ObjectKind::Model;
    std::string name;
    std::string badge;    // small trailing badge ("42%", "1.2M pts", "" = none)
    uint64_t groupId = 0; // SceneObjects: owning user group (0 = root)
    bool visible = true;
    bool locked = false;
    // Capability flags drive which row controls / context entries appear.
    bool canHide = true;
    bool canLock = true;
    bool canRename = true;
    bool canDelete = true;
    bool canDuplicate = true;
    bool canGroup = true;
    bool canFrame = true;
    int meshCount = 0; // Model rows: > 1 renders expandable mesh children
};

using Provider = std::function<void(Services&, std::vector<Item>&)>;

// Append a content provider to a section (append-only registry — C6).
void registerProvider(Section section, Provider provider);

// Fill `out` with the section's rows from every registered provider (the
// built-in providers self-register on the first call). Called by the panel
// once per section per frame.
void collect(Services& services, Section section, std::vector<Item>& out);

} // namespace Outliner
} // namespace Gui
