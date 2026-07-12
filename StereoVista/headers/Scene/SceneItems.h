#pragma once

// ============================================================================
//  scene::SceneItemRef + scene::Selection — identity & selection
//  (UI redesign Pass 1; docs/UI_REDESIGN.md §7.1, coherence contract C3).
// ----------------------------------------------------------------------------
//  Every persisted object carries a stable uint64_t ObjectId handed out by the
//  owning scene's monotonic counter (scene::Scene::allocateId, serialized in
//  the v3 .scene format). Anything that outlives a frame — the selection, undo
//  records, snapshots, palette results — refers to objects by
//  {Kind, ObjectId}, NEVER by container index: indices shift on every
//  add/delete, ids don't.
//
//  SceneItemRef still carries a cached container index (and a sub-index for a
//  model's mesh) purely as an access accelerator: consumers re-resolve it from
//  the id whenever it is stale (scene::Scene::resolve). Equality intentionally
//  ignores the cache.
//
//  Selection is the ONE application-wide selection (grown from the old
//  Application::Selection {int model; int mesh;} seed): an ordered multi-select
//  where the PRIMARY item is the most recently added one (drives the gizmo, the
//  Inspector header, the status bar). onChanged callbacks let dependents
//  (gizmo binding, panels) resynchronize without polling.
//
//  Pure std + stdint — no scene, GUI or GPU includes; safe from any layer.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace scene {

struct SceneItemRef {
    // Append-only (serialized in selection-adjacent data and used by plugins).
    enum class Kind {
        None = 0,
        Model,
        Mesh,        // id = owning Model's id, sub = mesh index
        PointCloud,
        SceneLayer,  // SLPK / I3S
        Sun,         // singleton: id unused
        PointLight,
        SpotLight,   // reserved (the Vulkan renderer has no spot lights yet)
        Measurement,
        ClipPlane,
        Group,
        Environment  // singleton: the sky, id unused
    };

    Kind kind = Kind::None;
    uint64_t id = 0;   // persistent ObjectId (authoritative)
    int index = -1;    // cached container index (accelerator only)
    int sub = -1;      // cached sub index (Mesh: mesh index inside the model)

    bool valid() const { return kind != Kind::None; }

    // Identity comparison: the index cache doesn't participate — except for
    // Mesh, whose sub IS part of the identity (meshes have no ids of their
    // own; they are addressed as model-id + mesh index).
    bool operator==(const SceneItemRef& o) const {
        if (kind != o.kind || id != o.id)
            return false;
        return kind != Kind::Mesh || sub == o.sub;
    }
    bool operator!=(const SceneItemRef& o) const { return !(*this == o); }
};

// Ordered application-wide multi-selection. Primary = last added.
class Selection {
public:
    const std::vector<SceneItemRef>& items() const { return items_; }
    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }

    // The primary item (most recently added), or an invalid ref when empty.
    SceneItemRef primary() const {
        return items_.empty() ? SceneItemRef{} : items_.back();
    }

    bool contains(const SceneItemRef& ref) const {
        for (const SceneItemRef& item : items_)
            if (item == ref)
                return true;
        return false;
    }

    void clear() {
        if (items_.empty())
            return;
        items_.clear();
        notify();
    }

    void selectOne(const SceneItemRef& ref) {
        if (items_.size() == 1 && items_[0] == ref)
            return;
        items_.clear();
        if (ref.valid())
            items_.push_back(ref);
        notify();
    }

    // Add (or re-add) an item; it becomes the primary.
    void add(const SceneItemRef& ref) {
        if (!ref.valid())
            return;
        removeNoNotify(ref);
        items_.push_back(ref);
        notify();
    }

    void remove(const SceneItemRef& ref) {
        if (removeNoNotify(ref))
            notify();
    }

    void toggle(const SceneItemRef& ref) {
        if (contains(ref))
            remove(ref);
        else
            add(ref);
    }

    void set(std::vector<SceneItemRef> refs) {
        items_ = std::move(refs);
        notify();
    }

    // Refresh the cached indices in place. `resolver` returns false when the
    // object no longer exists; such refs are dropped (with one notification
    // if anything was dropped).
    void revalidate(const std::function<bool(SceneItemRef&)>& resolver) {
        bool dropped = false;
        for (size_t i = 0; i < items_.size();) {
            if (resolver(items_[i])) {
                ++i;
            } else {
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
                dropped = true;
            }
        }
        if (dropped)
            notify();
    }

    // Fired after every selection change. One consumer per concern; keep the
    // callbacks cheap (they run inside UI interaction).
    void onChanged(std::function<void()> callback) {
        callbacks_.push_back(std::move(callback));
    }

private:
    bool removeNoNotify(const SceneItemRef& ref) {
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i] == ref) {
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    void notify() {
        for (const std::function<void()>& cb : callbacks_)
            if (cb)
                cb();
    }

    std::vector<SceneItemRef> items_;
    std::vector<std::function<void()>> callbacks_;
};

} // namespace scene
