#pragma once

// ============================================================================
//  core::ImportService — ONE entry point for everything that enters the scene
//  (UI redesign Pass 5; docs/UI_REDESIGN.md §11).
// ----------------------------------------------------------------------------
//  "Don't ask, know" (§4): the user drops or picks a pile of files and the app
//  figures out what each one is. This header owns the PURE half — planning:
//
//      planImport(paths) -> ImportPlan     (no Vulkan, no scene, no GPU)
//
//  Execution lives behind the Application (it needs the device + material
//  system): Application::importFiles(paths) runs the plan through the existing
//  loaders, then groups / prettifies / selects / frames the result.
//
//  Detection is extension-driven with CONTENT SNIFFING for the two genuinely
//  ambiguous cases the old code got wrong:
//    * `.ply` is both a mesh and a point-cloud format. The GL/drop path
//      hard-wired it to point clouds, so dropping a PLY *mesh* silently
//      produced a cloud of its vertices. We read the header: a non-zero
//      `element face` count means mesh.
//    * `.txt` may be an XYZ cloud or unrelated text. We parse the first data
//      line: >= 3 floats means cloud, otherwise it is reported as unknown
//      instead of being fed to the cloud parser.
//
//  Unknown files never open a modal — they come back in the plan and the caller
//  raises ONE warning toast (contract C8).
// ============================================================================

#include <string>
#include <vector>

namespace core {

enum class ImportKind {
    Unknown = 0,
    Model,       // Assimp mesh file
    PointCloud,  // LAS/LAZ/PLY-cloud/XYZ/TXT/PCB/HDF5
    SceneLayer,  // .slpk (I3S)
    SceneFile,   // .scene (replace / merge / ask)
    Environment, // .hdr/.exr — offered as the sky
    Texture,     // image — applied to the selected material
};

struct ImportAction {
    ImportKind kind = ImportKind::Unknown;
    std::string path;
    std::string reason; // human "why" (unknown files; shown in the toast)
};

struct ImportPlan {
    std::vector<ImportAction> actions; // every input, in order, classified
    // Convenience buckets (same paths, grouped by kind) so the executor can
    // hand whole batches to the loaders that want them (LAS tiles must go to
    // beginLoadLASMultipleProgressive in ONE call to share a centre).
    std::vector<std::string> models;
    std::vector<std::string> pointClouds;
    std::vector<std::string> sceneLayers;
    std::vector<std::string> sceneFiles;
    std::vector<std::string> environments;
    std::vector<std::string> textures;
    std::vector<std::string> unknown;

    bool empty() const { return actions.empty(); }
    // Objects that land in the scene as new selectable items.
    size_t objectCount() const {
        return models.size() + pointClouds.size() + sceneLayers.size();
    }
};

// Classify every path (reads a few hundred bytes of the ambiguous ones).
ImportPlan planImport(const std::vector<std::string>& paths);

// Display name for an imported file: stem, '_'/'-' -> space, collapsed and
// trimmed ("survey_north_02.las" -> "survey north 02").
std::string prettyName(const std::string& path);

// Auto-group name for a multi-file import: the longest common prefix of the
// file stems (trimmed of trailing separators); falls back to the common parent
// folder name when the prefix is too short to be meaningful.
std::string groupNameFor(const std::vector<std::string>& paths);

// Lower-cased extension including the dot (".las"); "" when there is none.
std::string lowerExtension(const std::string& path);

} // namespace core
