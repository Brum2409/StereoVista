#pragma once

// ============================================================================
// i3s::I3SLayer — parses 3dSceneLayer.json + the node hierarchy out of an
// SlpkArchive into the normalized SlpkTypes model.
// ----------------------------------------------------------------------------
// This is the ONE place that knows about I3S version differences:
//   * 1.7+ mesh profiles: nodePages/*.json.gz (flat paged array, root = 0)
//   * 1.6 mesh profiles:  per-node 3dNodeIndexDocument.json walked from
//                         store.rootNode (BFS-flattened, root = 0)
//   * PCSL 2.0 point clouds: store.index-described node pages with implicit
//                         firstChild/childCount ranges
// All three produce the same flat I3SNodeTree. Fails loudly: false + a
// human-readable reason naming the version/profile it could not handle.
//
// Pure CPU + archive reads — safe to run on a worker thread.
// ============================================================================

#include "Loaders/Slpk/SlpkTypes.h"

#include <string>

namespace i3s {

class SlpkArchive;

class I3SLayer {
public:
    // Reads + parses "3dSceneLayer.json(.gz)" from the archive root.
    static bool parseLayerInfo(const SlpkArchive& archive, LayerInfo& out,
                               std::string& error);

    // Loads and flattens the full node hierarchy (shape chosen by `info`).
    static bool loadNodeTree(const SlpkArchive& archive, const LayerInfo& info,
                             I3SNodeTree& out, std::string& error);

    // 1.6 only: materials live in per-node shared/sharedResource.json, not in
    // the layer document. Reads the shared resource of the given node and
    // fills info.materials (+ texture flags) — in practice one definition is
    // shared across the package, so the first geometry node's version is
    // adopted layer-wide (M1 simplification, recorded in the plan). No-op
    // when info.materials is already populated. Returns false only on a
    // malformed document; a missing one falls back to the default material.
    static bool parse16SharedResource(const SlpkArchive& archive,
                                      const I3SNodeTree& tree, LayerInfo& info,
                                      std::string& error);
};

} // namespace i3s
