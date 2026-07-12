#pragma once

// ============================================================================
//  Gui::Exporters — the export registry (mirror of the import pipeline)
//  (UI redesign Pass 7; docs/UI_REDESIGN.md §13, coherence contract C6).
// ----------------------------------------------------------------------------
//  Exporters register themselves and declare what they can export. Every export
//  surface renders from this registry:
//
//      * File ▸ Export…            (selection-aware)
//      * the Outliner context menu (batch, per selection)
//      * the Inspector's Export sections
//
//  A new exporter (a web package, a mesh format, a stereo screenshot mode) is
//  ONE registration — no UI file changes (§16.1 recipe).
//
//  Why the Gui layer and not Core (where ImportService's planner lives): every
//  executor already exists behind Gui::Services (exportPointCloud,
//  requestScreenshot, saveSceneAs). An exporter is therefore pure UI-side glue
//  — a title, an applicability test and a call — with no Vulkan or loader
//  dependency to hide. If an exporter ever needs the device, it grows a Services
//  method exactly like importFiles did.
// ============================================================================

#include "Scene/SceneItems.h"

#include <functional>
#include <string>
#include <vector>

namespace Gui {

class Services;

namespace Exporters {

struct Exporter {
    std::string id;             // "export.pointcloud"
    std::string title;          // "Point cloud…"
    std::string description;    // one-line "what you get"
    const char* icon = nullptr; // ICON_FA_* literal or null

    // Can this exporter act on the given selection? (An empty selection is
    // allowed — the screenshot and scene exporters don't need one.)
    std::function<bool(Services&, const std::vector<scene::SceneItemRef>&)> applies;
    // Do it. Opens its own dialog / toasts its own result.
    std::function<void(Services&, const std::vector<scene::SceneItemRef>&)> run;
};

// Append-only registry (C6). Built-ins (screenshot, point cloud, scene) register
// on first use.
void registerExporter(Exporter exporter);

// Every exporter that applies to `refs`, in registration order.
void collect(Services& services, const std::vector<scene::SceneItemRef>& refs,
             std::vector<const Exporter*>& out);

// The File ▸ Export… modal (selection-aware). `open` is owned by the caller.
void drawExportDialog(Services& services, bool* open);

} // namespace Exporters
} // namespace Gui
