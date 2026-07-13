#pragma once

// ============================================================================
//  Gui::Inspector — the Inspector's per-kind editor registry + multi-edit core
//  (UI redesign Pass 2; docs/UI_REDESIGN.md §8, coherence contract C6).
// ----------------------------------------------------------------------------
//  The Inspector renders the controls for the current selection. Its per-kind
//  SECTIONS come from registered editors, not hard-wired panel code — the same
//  registry pattern as Gui::Outliner (Pass 1). A future tool that produces a
//  new object kind registers an editor (plus its ObjectKind style) and its
//  Inspector content appears with zero edits to InspectorPanel.cpp (§16.1).
//
//  The panel shell (InspectorPanel.cpp) owns the frame: header card, tool card,
//  dispatch to the kind editor, global cards and the empty state. An editor is
//  handed an EditContext — a homogeneous group of refs of ONE kind — and draws
//  that kind's sections.
//
//  MULTI-EDIT + UNDO (the ported GL PanelEditTracker idea on core::UndoManager):
//  EditRow<T> binds one property across the whole selection. It applies edits
//  live to every selected object for immediate feedback, and records exactly
//  ONE undoable step per gesture (drag begin -> release), gesture-grained (C4).
//  Undo/redo closures re-resolve objects by ObjectId every time (never by cached
//  index — indices shift on add/delete, C3); the resolver must capture only
//  stable state (a Services*/Scene*), never a frame-local.
//
//  Gui layer: pure ImGui/UiKit over Gui::Services — no Vulkan, no renderer.
// ============================================================================

#include "Core/UndoManager.h"
#include "Gui/Services.h"
#include "Gui/UiKit.h"
#include "Scene/SceneItems.h"

#include "imgui/imgui.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Gui {
namespace Inspector {

using Kind = scene::SceneItemRef::Kind;

// A resolvable edit target. `id` is the owning object's ObjectId; `sub` is a
// secondary selector (a model's mesh index for material edits, else -1). The
// pair is stable across add/delete — resolvers re-derive the live pointer.
struct EditHandle {
    uint64_t id = 0;
    int sub = -1;
};

// The selection slice handed to a per-kind editor: refs are all of `kind`, in
// selection order (the selection's primary, if of this kind, is last).
struct EditContext {
    Services& services;
    Kind kind = Kind::None;
    std::vector<scene::SceneItemRef> refs;

    bool multi() const { return refs.size() > 1; }
    scene::SceneItemRef primary() const {
        return refs.empty() ? scene::SceneItemRef{} : refs.back();
    }
    // Object-property handles ({id, meshIndex}) for every ref in the slice.
    std::vector<EditHandle> objectHandles() const {
        std::vector<EditHandle> out;
        out.reserve(refs.size());
        for (const scene::SceneItemRef& r : refs)
            out.push_back(EditHandle{ r.id, r.sub });
        return out;
    }
};

using Editor = std::function<void(EditContext&)>;

// Register the per-kind section renderer (append-only registry — C6). One
// editor per kind; the last registration for a kind wins (lets a plugin
// override a built-in in a later pass). editorFor() triggers the built-in
// registration on first use.
void registerEditor(Kind kind, Editor editor);
const Editor* editorFor(Kind kind);

// Built-in editor registration entry points (defined across Inspector.cpp and
// inspectors/*.cpp; called once by registerBuiltins()). Append-only.
namespace builtins {
void registerModelEditor();       // Model + Mesh
void registerPointCloudEditor();
void registerPointLightEditor();
void registerSunEditor();
void registerEnvironmentEditor();
void registerLayerEditor();       // I3S SceneLayer (inspectors/LayerInspector.cpp)
} // namespace builtins

// ── Multi-edit property row (live apply to all + one undo per gesture) ───────

namespace detail {

// Per-row before-snapshot store, keyed by the value widget's ImGui id, kept
// across the frames of one drag gesture. One map instance per property type T.
template <class T>
std::unordered_map<ImGuiID, std::vector<std::pair<EditHandle, T>>>& pendingStore() {
    static std::unordered_map<ImGuiID, std::vector<std::pair<EditHandle, T>>> store;
    return store;
}

template <class T>
bool sameSnapshot(const std::vector<std::pair<EditHandle, T>>& a,
                  const std::vector<std::pair<EditHandle, T>>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].first.id != b[i].first.id || a[i].first.sub != b[i].first.sub ||
            !(a[i].second == b[i].second))
            return false;
    return true;
}

// The value each row showed last frame, so EditRow can tell a change it made
// itself from one that arrived from somewhere else (an undo, the gizmo, a preset)
// and flash only the latter. One map per property type T, like pendingStore.
template <class T>
std::unordered_map<ImGuiID, T>& lastShownStore() {
    static std::unordered_map<ImGuiID, T> store;
    return store;
}

} // namespace detail

// Draw a labeled property row bound to a shared property across `handles`.
//   resolve : (const EditHandle&) -> T*  (nullptr when the handle can't carry
//             the property right now — kind mismatch, deleted, locked). MUST
//             capture only stable state (Services*/Scene*), never a frame-local.
//   widget  : (T& value) -> bool  (draws the value-column control on a scratch
//             value; returns true when it edited this frame). Give the control
//             the id "##v" so the row's PushID scopes it.
//   resync  : optional; run after each live apply AND inside undo/redo (e.g.
//             recompute world bounds for a transform edit).
// Applies the edit live to every handle and records ONE undo entry per gesture.
// Returns true while the row's widget is active (dragging).
template <class T, class Resolve, class Widget>
bool EditRow(EditContext& ctx, const char* label, const char* strId,
             const std::vector<EditHandle>& handles, Resolve resolve, Widget widget,
             std::function<void()> resync = std::function<void()>()) {
    // Representative value = the last resolvable handle (the primary, kept last)
    // so the row shows the primary's value; flag a mixed selection for the hint.
    T value{};
    bool haveValue = false;
    bool mixed = false;
    for (const EditHandle& h : handles) {
        if (T* p = resolve(h)) {
            if (haveValue && !(*p == value))
                mixed = true;
            value = *p;
            haveValue = true;
        }
    }
    if (!haveValue)
        return false;

    // Pre-apply snapshot of every handle's live value (before this frame's edit
    // touches the objects) — correct even for single-click widgets (checkbox).
    std::vector<std::pair<EditHandle, T>> before;
    before.reserve(handles.size());
    for (const EditHandle& h : handles)
        if (T* p = resolve(h))
            before.push_back({ h, *p });

    UiKit::PropertyRow(label);
    ImGui::PushID(strId);
    T edited = value;
    const bool changed = widget(edited);
    const ImGuiID widgetId = ImGui::GetItemID();

    // Flash the row when its value moved WITHOUT this widget being the one that
    // moved it — an undo, a redo, a gizmo drag in the viewport, a section reset.
    // That is precisely the change the user can otherwise miss, and because every
    // Inspector row funnels through here, every Inspector row gets it for free.
    // A change the row made itself (`changed`, or a drag in flight) must stay
    // quiet, or the flash would just be noise trailing the pointer.
    auto& lastShown = detail::lastShownStore<T>();
    const auto shownIt = lastShown.find(widgetId);
    const bool external = shownIt != lastShown.end() && !(shownIt->second == value) &&
                          !changed && !ImGui::IsItemActive();
    lastShown[widgetId] = changed ? edited : value;
    UiKit::ItemFlash(external);
    ImGui::PopID();

    if (changed) {
        for (const EditHandle& h : handles)
            if (T* p = resolve(h))
                *p = edited;
        if (resync)
            resync();
    }

    // Gesture-grained single-undo. IsItemActivated/DeactivatedAfterEdit are
    // unreliable for multi-component widgets (DragFloatN: the group id != the
    // active sub-component id), so gate on the panel-wide IsAnyItemActive() the
    // way the GL PanelEditTracker did: capture the pre-drag snapshot the first
    // frame the row changes, commit one undo entry once nothing is active.
    auto& pending = detail::pendingStore<T>();
    const bool hadPending = pending.find(widgetId) != pending.end();
    if (changed && !hadPending)
        pending[widgetId] = before; // `before` = pre-widget values this frame
    const auto it = pending.find(widgetId);
    const bool anyActive = ImGui::IsAnyItemActive();
    if (mixed && !anyActive) {
        ImGui::SameLine();
        ImGui::TextDisabled("(mixed)");
    }
    if (it != pending.end() && !anyActive) {
        std::vector<std::pair<EditHandle, T>> beforeSnap = it->second;
        pending.erase(it);

        std::vector<std::pair<EditHandle, T>> after;
        after.reserve(handles.size());
        for (const EditHandle& h : handles)
            if (T* p = resolve(h))
                after.push_back({ h, *p });

        if (!detail::sameSnapshot(beforeSnap, after))
            ctx.services.undo().record(
                label,
                [resolve, beforeSnap, resync]() {
                    for (const std::pair<EditHandle, T>& c : beforeSnap)
                        if (T* p = resolve(c.first))
                            *p = c.second;
                    if (resync)
                        resync();
                },
                [resolve, after, resync]() {
                    for (const std::pair<EditHandle, T>& c : after)
                        if (T* p = resolve(c.first))
                            *p = c.second;
                    if (resync)
                        resync();
                });
    }
    return anyActive;
}

// Reset a whole section's fields to a caller-supplied default in ONE undoable
// step. The editor supplies a small POD snapshot type `Snap` and:
//   capture : (const EditHandle&) -> Snap   (read the live fields)
//   apply   : (const EditHandle&, const Snap&)  (write fields back to live)
//   defaults: the Snap to reset to.
// Draws the shared UiKit ResetGlyph (visible only while `modified`). On click it
// snapshots every handle's current fields, writes the defaults, and records one
// LambdaUndoCommand (undo restores each snapshot, redo re-applies defaults).
// Resolvers must capture only stable state (Services*/Scene*).
template <class Snap, class Capture, class Apply>
void SectionReset(EditContext& ctx, const char* strId, const char* undoLabel,
                  bool modified, const std::vector<EditHandle>& handles,
                  Capture capture, Apply apply, const Snap& defaults,
                  std::function<void()> resync = std::function<void()>()) {
    if (!UiKit::ResetGlyph(strId, modified))
        return;
    std::vector<std::pair<EditHandle, Snap>> before;
    before.reserve(handles.size());
    for (const EditHandle& h : handles) {
        before.push_back({ h, capture(h) });
        apply(h, defaults);
    }
    if (resync)
        resync();
    ctx.services.undo().record(
        undoLabel,
        [capture, apply, before, resync]() {
            (void)capture;
            for (const std::pair<EditHandle, Snap>& c : before)
                apply(c.first, c.second);
            if (resync)
                resync();
        },
        [apply, handles, defaults, resync]() {
            for (const EditHandle& h : handles)
                apply(h, defaults);
            if (resync)
                resync();
        });
}

} // namespace Inspector
} // namespace Gui
