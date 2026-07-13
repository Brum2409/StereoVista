#include "Gui/Outliner.h"
#include "Gui/Panels.h"
#include "Gui/Services.h"
#include "Gui/UiKit.h"
#include "Scene/Scene.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// The Outliner (window title Gui::Windows::Scene) — UI redesign Pass 1 (§7.3).
// One tree for everything loaded or created: three collapsible sections with
// count badges, rows styled by UiKit::StyleFor (C2), hover-revealed eye/lock,
// Ctrl/Shift multi-select on the app-wide scene::Selection (C3), drag into/out
// of user groups, inline rename (F2 / double-click), a full context menu,
// type-filter chips and fuzzy search (names AND type nouns). All mutations go
// through the Services item operations — one undoable step each (C4).
// ============================================================================

namespace Gui {

namespace {

using scene::SceneItemRef;
using Kind = SceneItemRef::Kind;
using Outliner::Item;

// Per-level tree indent (pushed as ImGuiStyleVar_IndentSpacing around the
// whole tree — kept as one named constant so the child-guide band drawn by
// drawSceneObjects/drawModelRow lines up with where ImGui actually indents).
float indentStep() { return 26.0f * UiKit::Scale(); }

// Vertical gap between rows — every row element is absolutely positioned
// (see drawRow), which bypasses ImGui's automatic inter-item spacing, so
// this is the ONLY thing putting daylight between one row and the next. Rows
// themselves stay compact; this is where the "more margin" lives.
float rowGap() { return 6.0f * UiKit::Scale(); }

// Filter chips (bitmask). "Notes" covers the annotations section.
enum FilterBit : uint32_t {
    kFilterModels = 1u << 0,
    kFilterClouds = 1u << 1,
    kFilterLayers = 1u << 2,
    kFilterLights = 1u << 3,
    kFilterNotes = 1u << 4,
};

struct PanelState {
    char search[128] = "";
    uint32_t filterMask = 0; // 0 = show everything
    // Inline rename.
    SceneItemRef renameRef;
    char renameBuf[256] = "";
    bool renameFocusPending = false;
    // Shift-range anchor + the row order of the PREVIOUS frame (a range can
    // extend below the clicked row, whose index isn't known mid-draw).
    SceneItemRef lastClicked;
    std::vector<SceneItemRef> rows;     // built this frame, in draw order
    std::vector<SceneItemRef> prevRows; // last frame's rows
    // Refs being dragged (ImGui payloads are POD; the refs live here).
    std::vector<SceneItemRef> dragRefs;
    // A plain (no-modifier) press on a row that's already part of a larger
    // multi-selection is deferred: collapsing to just that row immediately
    // on mouse-down would break "drag the whole selection onto a group" (by
    // the time BeginDragDropSource ran, the selection would already be down
    // to one item). Resolved to a real selectOne() on release IF no drag
    // happened; if a drag did happen, cleared without ever firing.
    SceneItemRef pendingClickRef;
    bool pendingClickDragged = false;
    // Mutations deferred to after the tree walk (they shift the indices the
    // collected items cache; ops re-resolve by id, this keeps one frame's
    // view internally consistent).
    std::vector<std::function<void()>> deferred;
};

PanelState& state() {
    static PanelState s;
    return s;
}

uint32_t filterBitFor(UiKit::ObjectKind kind) {
    switch (kind) {
    case UiKit::ObjectKind::Model:
    case UiKit::ObjectKind::Mesh:        return kFilterModels;
    case UiKit::ObjectKind::PointCloud:  return kFilterClouds;
    case UiKit::ObjectKind::SceneLayer:  return kFilterLayers;
    case UiKit::ObjectKind::Sun:
    case UiKit::ObjectKind::PointLight:
    case UiKit::ObjectKind::SpotLight:   return kFilterLights;
    case UiKit::ObjectKind::Measurement:
    case UiKit::ObjectKind::ClipPlane:   return kFilterNotes;
    default:                             return 0; // groups/environment: always
    }
}

// Search matches names AND type nouns (§7.3): "point" finds every point cloud.
bool passesFilter(const PanelState& s, const Item& item) {
    if (s.filterMask != 0) {
        const uint32_t bit = filterBitFor(item.kind);
        if (bit != 0 && (s.filterMask & bit) == 0)
            return false;
    }
    if (s.search[0] == '\0')
        return true;
    if (UiKit::FuzzyMatch(s.search, item.name.c_str(), nullptr))
        return true;
    return UiKit::FuzzyMatch(s.search, UiKit::StyleFor(item.kind).noun, nullptr);
}

// The refs an operation applies to: the whole selection when the row is part
// of it, else just the row (batch ops = one undo step, C4).
std::vector<SceneItemRef> opTargets(Services& services, const SceneItemRef& ref) {
    scene::Selection& selection = services.selection();
    if (selection.contains(ref))
        return selection.items();
    return { ref };
}

void beginRename(const SceneItemRef& ref, const std::string& name) {
    PanelState& s = state();
    s.renameRef = ref;
    std::snprintf(s.renameBuf, sizeof(s.renameBuf), "%s", name.c_str());
    s.renameFocusPending = true;
}

// Row click → selection update (plain / Ctrl toggle / Shift range).
void handleRowClick(Services& services, const SceneItemRef& ref) {
    PanelState& s = state();
    scene::Selection& selection = services.selection();
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && s.lastClicked.valid()) {
        // Range over the previous frame's visible row order.
        const std::vector<SceneItemRef>& rows = s.prevRows;
        int a = -1, b = -1;
        for (int i = 0; i < int(rows.size()); ++i) {
            if (rows[size_t(i)] == s.lastClicked)
                a = i;
            if (rows[size_t(i)] == ref)
                b = i;
        }
        if (a >= 0 && b >= 0) {
            if (a > b)
                std::swap(a, b);
            std::vector<SceneItemRef> range;
            if (io.KeyCtrl)
                range = selection.items(); // Ctrl+Shift extends
            for (int i = a; i <= b; ++i) {
                bool seen = false;
                for (const SceneItemRef& have : range)
                    seen = seen || have == rows[size_t(i)];
                if (!seen)
                    range.push_back(rows[size_t(i)]);
            }
            selection.set(std::move(range));
            return; // anchor stays for follow-up ranges
        }
    }
    if (io.KeyCtrl)
        selection.toggle(ref);
    else
        selection.selectOne(ref);
    s.lastClicked = ref;
}

// "Select similar ▸" — by type / source / material, scanned panel-side.
void selectSimilar(Services& services, const Item& item, int mode) {
    scene::Scene& scene = services.scene();
    std::vector<SceneItemRef> refs;
    auto push = [&](Kind kind, uint64_t id, int index) {
        SceneItemRef ref;
        ref.kind = kind;
        ref.id = id;
        ref.index = index;
        refs.push_back(ref);
    };

    if (mode == 0) { // type
        switch (item.ref.kind) {
        case Kind::Model:
            for (size_t i = 0; i < scene.models.size(); ++i)
                push(Kind::Model, scene.models[i].id, int(i));
            break;
        case Kind::PointCloud:
            for (size_t i = 0; i < scene.pointClouds.size(); ++i)
                push(Kind::PointCloud, scene.pointClouds[i].id, int(i));
            break;
        case Kind::SceneLayer:
            for (size_t i = 0; i < scene.i3sLayers.size(); ++i)
                if (scene.i3sLayers[i])
                    push(Kind::SceneLayer, scene.i3sLayers[i]->id, int(i));
            break;
        case Kind::PointLight:
            for (size_t i = 0; i < scene.pointLights.size(); ++i)
                push(Kind::PointLight, scene.pointLights[i].id, int(i));
            break;
        case Kind::Measurement:
            for (size_t i = 0; i < scene.measurements.size(); ++i)
                push(Kind::Measurement, scene.measurements[i].id, int(i));
            break;
        case Kind::ClipPlane:
            for (size_t i = 0; i < scene.clipPlanes.size(); ++i)
                push(Kind::ClipPlane, scene.clipPlanes[i].id, int(i));
            break;
        default:
            break;
        }
    } else if (mode == 1) { // source file
        SceneItemRef ref = item.ref;
        if (!scene.resolve(ref))
            return;
        if (ref.kind == Kind::Model) {
            const std::string& source = scene.models[ref.index].sourcePath;
            const std::string& primitive = scene.models[ref.index].primitiveType;
            for (size_t i = 0; i < scene.models.size(); ++i)
                if ((!source.empty() && scene.models[i].sourcePath == source) ||
                    (!primitive.empty() &&
                     scene.models[i].primitiveType == primitive))
                    push(Kind::Model, scene.models[i].id, int(i));
        } else if (ref.kind == Kind::PointCloud) {
            const std::string& source = scene.pointClouds[ref.index].filePath;
            for (size_t i = 0; i < scene.pointClouds.size(); ++i)
                if (!source.empty() && scene.pointClouds[i].filePath == source)
                    push(Kind::PointCloud, scene.pointClouds[i].id, int(i));
        } else if (ref.kind == Kind::SceneLayer) {
            const std::string& source = scene.i3sLayers[ref.index]->sourcePath;
            for (size_t i = 0; i < scene.i3sLayers.size(); ++i)
                if (scene.i3sLayers[i] &&
                    scene.i3sLayers[i]->sourcePath == source)
                    push(Kind::SceneLayer, scene.i3sLayers[i]->id, int(i));
        }
    } else { // material (models sharing any bindless material index)
        SceneItemRef ref = item.ref;
        if (!scene.resolve(ref) || ref.kind != Kind::Model)
            return;
        std::vector<uint32_t> materials;
        for (const scene::ModelMesh& mesh : scene.models[ref.index].meshes)
            materials.push_back(mesh.materialIndex);
        for (size_t i = 0; i < scene.models.size(); ++i) {
            bool shares = false;
            for (const scene::ModelMesh& mesh : scene.models[i].meshes)
                for (uint32_t m : materials)
                    shares = shares || mesh.materialIndex == m;
            if (shares)
                push(Kind::Model, scene.models[i].id, int(i));
        }
    }
    if (!refs.empty())
        services.selection().set(std::move(refs));
}

// Context menu shared by every row. Runs inside BeginPopupContextItem.
void rowContextMenu(Services& services, const Item& item) {
    PanelState& s = state();
    const std::vector<SceneItemRef> targets = opTargets(services, item.ref);

    if (item.canFrame && ImGui::MenuItem(ICON_FA_CROSSHAIRS "  Frame", "F"))
        s.deferred.push_back([&services, targets] { services.frameItems(targets); });
    if (ImGui::MenuItem(ICON_FA_EYE "  Isolate"))
        s.deferred.push_back(
            [&services, targets] { services.isolateItems(targets); });

    if (ImGui::BeginMenu("Select similar")) {
        if (ImGui::MenuItem("Same type"))
            selectSimilar(services, item, 0);
        const bool hasSource = item.ref.kind == Kind::Model ||
                               item.ref.kind == Kind::PointCloud ||
                               item.ref.kind == Kind::SceneLayer;
        if (ImGui::MenuItem("Same source", nullptr, false, hasSource))
            selectSimilar(services, item, 1);
        if (ImGui::MenuItem("Same material", nullptr, false,
                            item.ref.kind == Kind::Model))
            selectSimilar(services, item, 2);
        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (item.canHide &&
        ImGui::MenuItem(item.visible ? ICON_FA_EYE_SLASH "  Hide"
                                     : ICON_FA_EYE "  Show")) {
        const bool show = !item.visible;
        s.deferred.push_back([&services, targets, show] {
            services.setItemsVisible(targets, show);
        });
    }
    if (item.canLock &&
        ImGui::MenuItem(item.locked ? ICON_FA_UNLOCK "  Unlock"
                                    : ICON_FA_LOCK "  Lock")) {
        const bool lock = !item.locked;
        s.deferred.push_back([&services, targets, lock] {
            services.setItemsLocked(targets, lock);
        });
    }

    ImGui::Separator();
    if (item.canDuplicate &&
        ImGui::MenuItem(ICON_FA_CLONE "  Duplicate", "Ctrl+D"))
        s.deferred.push_back(
            [&services, targets] { services.duplicateItems(targets); });
    if (item.canGroup && ImGui::MenuItem(ICON_FA_FOLDER "  Group", "Ctrl+G"))
        s.deferred.push_back([&services, targets] {
            const uint64_t groupId = services.groupItems(targets);
            if (groupId != 0) {
                // Land in rename so the new folder gets a real name (§4).
                SceneItemRef groupRef;
                groupRef.kind = Kind::Group;
                groupRef.id = groupId;
                if (const scene::Group* g = services.scene().findGroup(groupId))
                    beginRename(groupRef, g->name);
            }
        });
    if ((item.ref.kind == Kind::Group || item.groupId != 0) &&
        ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Ungroup", "Ctrl+Shift+G"))
        s.deferred.push_back(
            [&services, targets] { services.ungroupItems(targets); });
    if (item.canRename && ImGui::MenuItem(ICON_FA_PEN "  Rename", "F2"))
        beginRename(item.ref, item.name);

    if (item.canDelete) {
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_TRASH_ALT "  Delete"))
            s.deferred.push_back(
                [&services, targets] { services.deleteItems(targets); });
    }
}

// One tree row: [chevron] [kind icon] [name] ... [badge] [eye] [lock]. Every
// element's Y is centered against an explicit row-height budget (rowH) —
// taller and roomier than a bare ImGui tree row, closer to a comfortable
// file-explorer list (the row height + centering was the #1 polish gap:
// default ImGui tree rows read as cramped, and elements anchored to rowMin.y
// alone drift to the top of a taller row instead of centering in it).
// Returns true when the node is OPEN (caller draws children and calls
// ImGui::Indent/Unindent itself — see drawModelRow / drawSceneObjects's
// group recursion — rather than relying on TreeNodeEx's own TreePush).
bool drawRow(Services& services, const Item& item, bool asTreeNode,
             bool forceLeaf) {
    PanelState& s = state();
    scene::Selection& selection = services.selection();
    const UiKit::KindStyle style = UiKit::StyleFor(item.kind);
    const float scale = UiKit::Scale();
    const float fontSize = ImGui::GetFontSize();

    // Row-height budget: fontSize + 2*padY, PLUS a real gap between rows
    // (rowGap()) — every element in the row is absolutely positioned, which
    // bypasses ImGui's automatic inter-item spacing entirely, so without an
    // explicit gap rows would sit welded edge-to-edge regardless of how
    // generous padY is. The row itself stays close to a normal list-item
    // size (~26px @ scale 1); the breathing room comes from the gap BETWEEN
    // rows (rowGap(), below) rather than inflating each row's own box.
    const float padX = 8.0f * scale;
    const float padY = 6.5f * scale;
    const float rowH = fontSize + 2.0f * padY;

    char idBuf[48];
    std::snprintf(idBuf, sizeof(idBuf), "k%d_%llu_%d", int(item.ref.kind),
                  static_cast<unsigned long long>(item.ref.id), item.ref.sub);
    ImGui::PushID(idBuf);

    // NoTreePushOnOpen unconditionally: children indent/connector-guide is
    // owned explicitly by the caller (drawModelRow / drawSceneObjects's
    // group recursion, via ImGui::Indent/Unindent) rather than trusted to
    // ImGui's own internal TreePush-on-open behavior for this exact flag
    // combination — every row already carries its own unique PushID above,
    // so TreePush's ID-stack side effect isn't needed here, only the indent.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_AllowOverlap |
                               ImGuiTreeNodeFlags_FramePadding |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen;
    const bool leaf = forceLeaf || !asTreeNode;
    if (leaf)
        flags |= ImGuiTreeNodeFlags_Leaf;
    const bool isSelected = selection.contains(item.ref);
    if (isSelected)
        flags |= ImGuiTreeNodeFlags_Selected;

    // Suppress ImGui's own (square, theme-blue) selection/hover tint — a
    // rounded fill + accent left bar is painted below instead, matching the
    // NavItem convention already used elsewhere in UiKit for a "polished"
    // selected-row look, rather than two different selection languages.
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
    const bool opened = ImGui::TreeNodeEx("##row", flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    // Latched HERE, immediately after the node: the drag-drop source and the
    // context menu below submit their own items, so by the time the row is
    // painted "the last item" is no longer this row.
    const ImGuiID rowId = ImGui::GetItemID();
    const bool rowPressed = ImGui::IsItemActivated();
    const bool rowActive = ImGui::IsItemActive();
    const bool rowHovered =
        ImGui::IsMouseHoveringRect(rowMin, rowMax) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    s.rows.push_back(item.ref);

    // Click → selection; right-click selects too (before the context menu).
    // A plain click on a row that's already part of a bigger multi-selection
    // is DEFERRED rather than resolved immediately: IsItemClicked fires on
    // mouse-DOWN, before ImGui knows whether this press will turn into a
    // drag — collapsing the selection right away would mean
    // BeginDragDropSource below only ever sees the single clicked row,
    // breaking "drag every selected object onto a group at once".
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl && !io.KeyShift && selection.size() > 1 &&
            selection.contains(item.ref)) {
            s.pendingClickRef = item.ref;
            s.pendingClickDragged = false;
        } else {
            handleRowClick(services, item.ref);
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) &&
        !selection.contains(item.ref)) {
        selection.selectOne(item.ref);
        s.lastClicked = item.ref;
    }
    if (item.canRename && ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        beginRename(item.ref, item.name);

    // Drag source: dragging a selected row drags the whole selection.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        if (s.pendingClickRef == item.ref)
            s.pendingClickDragged = true; // this press became a drag
        s.dragRefs = opTargets(services, item.ref);
        int dummy = 0;
        ImGui::SetDragDropPayload("SV_OUTLINER_ITEMS", &dummy, sizeof(dummy));
        ImGui::Text("%d item(s)", int(s.dragRefs.size()));
        ImGui::EndDragDropSource();
    }
    // Resolve a deferred click once the press releases: a real click (no
    // drag ever started) collapses the selection to just this row, same as
    // the immediate path above.
    if (s.pendingClickRef == item.ref && ImGui::IsItemDeactivated()) {
        if (!s.pendingClickDragged)
            handleRowClick(services, item.ref);
        s.pendingClickRef = SceneItemRef{};
    }
    // Group rows accept drops (move into this group).
    if (item.ref.kind == Kind::Group && ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("SV_OUTLINER_ITEMS")) {
            const std::vector<SceneItemRef> refs = s.dragRefs;
            const uint64_t groupId = item.ref.id;
            s.deferred.push_back([&services, refs, groupId] {
                services.moveItemsToGroup(refs, groupId);
            });
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("rowctx")) {
        rowContextMenu(services, item);
        ImGui::EndPopup();
    }

    // ── Selection / hover fill (painted before the row's own content, on
    // top of whatever TreeNodeEx already drew — its Header colors are
    // transparent above, so this is the only fill that shows). Selection is
    // a solid filled pill (not a faint tint + thin bar) — reads as a clear,
    // deliberate "this is selected" state rather than a subtle hint. ──────
    //
    // Both states are ANIMATED off the row's ImGui id: selection WIPES in from
    // the row's left edge on a spring (so a selection visibly travels to the row
    // you picked rather than teleporting), hover eases, and a click leaves a
    // ripple. The row also arrives with a pop the first time it is seen — which,
    // because the motion store forgets ids that stop being queried, is exactly
    // when an object is added to the scene or the panel is reopened.
    const float selT = UiKit::Spring(rowId, isSelected ? 1.0f : 0.0f, 5.4f, 0.62f);
    const float hovT =
        UiKit::Anim01(rowId ^ 0xB5297A4Du, rowHovered ? 1.0f : 0.0f, 0.11f);
    const float rowW = rowMax.x - rowMin.x;
    if (selT > 0.01f) {
        ImVec4 fill = UiKit::Color(UiKit::Semantic::Accent);
        fill.w = 0.55f;
        const float wipe = rowW * std::min(std::max(selT, 0.0f), 1.0f);
        dl->AddRectFilled(rowMin, ImVec2(rowMin.x + wipe, rowMax.y),
                          ImGui::GetColorU32(fill), UiKit::RadiusInner());
    }
    if (hovT > 0.01f && selT < 0.99f) {
        ImVec4 fill = UiKit::Color(UiKit::Semantic::Accent);
        fill.w = 0.08f * hovT * (1.0f - selT);
        dl->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(fill),
                          UiKit::RadiusInner());
    }
    UiKit::ItemFxAt(rowId, rowMin, rowMax, UiKit::ItemFx_Press,
                    UiKit::RadiusInner(), rowHovered, rowPressed, rowActive);

    // ── Icon: same nominal size as body text (matches InlineIcon/NavItem —
    // the convention every other icon in the GUI already uses), in a fixed
    // slot column so names line up regardless of glyph width. Folders swap
    // open/closed glyphs like a real tree. Positioned via the draw list
    // directly (not InlineIcon) so it can be vertically centered against
    // rowH rather than the smaller default frame height — centered against
    // the REQUESTED size, not CalcTextSizeA's measured box: FontAwesome
    // glyphs aren't ink-centered within their own measured line height, so
    // centering the measured box (rather than the nominal size, which is
    // what NavItem's already-correct convention uses) drifted visibly. ────
    const char* iconGlyph = style.icon;
    if (item.ref.kind == Kind::Group)
        iconGlyph = opened ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    // Left margin before the icon slot: one consistent starting point for
    // every row regardless of whether ImGui drew an expand arrow before it
    // (leaf rows like Sun/lights have none; Group/multi-mesh Model rows do)
    // — without it, content sat flush against rowMin.x with no gutter.
    // Hovering nudges the row's contents toward the reader (and selection holds
    // them there) — the same "the row leans into you" language NavItem uses.
    const float slide = (2.0f * hovT + 2.0f * std::min(selT, 1.0f)) * scale;
    const float leftPad = UiKit::Space(3) + slide;
    const float iconSlotX = rowMin.x + leftPad;
    const float iconSize = fontSize;
    const float iconSlotW = iconSize + UiKit::Space(3);
    const float textGap = UiKit::Space(2);
    const float contentX = iconSlotX + iconSlotW + textGap;
    if (ImFont* iconFont = UiKit::IconFont()) {
        const ImVec2 gs = iconFont->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, iconGlyph);
        dl->AddText(iconFont, iconSize,
                    ImVec2(iconSlotX + (iconSlotW - gs.x) * 0.5f,
                          rowMin.y + (rowH - iconSize) * 0.5f),
                    ImGui::GetColorU32(style.color), iconGlyph);
    }

    const bool dimmed = !item.visible || item.locked;
    if (s.renameRef.valid() && s.renameRef == item.ref) {
        // Inline rename: an InputText in place of the label, vertically
        // centered against rowH.
        ImGui::SetCursorScreenPos(
            ImVec2(contentX, rowMin.y + (rowH - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::SetNextItemWidth(std::max(
            rowMax.x - contentX - 60.0f * scale, 80.0f * scale));
        if (s.renameFocusPending) {
            ImGui::SetKeyboardFocusHere();
            s.renameFocusPending = false;
        }
        const bool commit =
            ImGui::InputText("##rename", s.renameBuf, sizeof(s.renameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_AutoSelectAll);
        const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (cancel) {
            s.renameRef = SceneItemRef{};
        } else if (commit || ImGui::IsItemDeactivated()) {
            const SceneItemRef ref = s.renameRef;
            const std::string name = s.renameBuf;
            s.deferred.push_back(
                [&services, ref, name] { services.renameItem(ref, name); });
            s.renameRef = SceneItemRef{};
        }
    } else {
        // Plain body font — a bold-font pass for Group/selected rows was
        // tried here and reverted: the bold and regular faces load from
        // separate font files with no guarantee they share ascent/descent
        // metrics, and it visibly drifted off-center on this machine.
        // Selection/hierarchy already reads from the accent fill + left bar
        // above and the folder icon swap, without risking a font-metrics bug.
        ImGui::SetCursorScreenPos(
            ImVec2(contentX, rowMin.y + (rowH - fontSize) * 0.5f));
        if (dimmed)
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        // Fuzzy-highlight the search match inside the name.
        int matches[64];
        int matchCount = 0;
        if (s.search[0] != '\0' &&
            UiKit::FuzzyMatch(s.search, item.name.c_str(), nullptr, matches, 64,
                              &matchCount) &&
            matchCount > 0)
            UiKit::TextFuzzyHighlighted(item.name.c_str(), matches, matchCount);
        else
            ImGui::TextUnformatted(item.name.c_str());
        if (dimmed)
            ImGui::PopStyleColor();
    }

    // ── Right-aligned: badge + hover-revealed eye/lock, all vertically
    // centered against rowH (a temporarily bigger FramePadding grows the
    // icon buttons to match, instead of the small default GetFrameHeight()
    // square floating at the top of a now-much-taller row), with real gaps
    // between them instead of touching edges. ─────────────────────────────
    const float btnPad = padY * 0.5f;
    const float btnH = fontSize + btnPad * 2.0f;
    const float btnGap = UiKit::Space(2);
    // Inset the whole right-side cluster (count badge + eye/lock) from the panel
    // border so it reads as a tidy right-aligned column with breathing room,
    // rather than jammed against the edge. This X is measured from the row's
    // right edge (window WorkRect, constant for every row), so the badge stays
    // in the same column whether an item is top-level or nested in a group.
    float right = rowMax.x - UiKit::Space(6);

    // Lock (right-most): visible on hover or while locked.
    if (item.canLock) {
        right -= btnH;
        if (rowHovered || item.locked) {
            ImGui::SetCursorScreenPos(ImVec2(right, rowMin.y + (rowH - btnH) * 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(btnPad, btnPad));
            if (UiKit::IconButton("##lock",
                                  item.locked ? ICON_FA_LOCK : ICON_FA_UNLOCK,
                                  item.locked ? "Unlock" : "Lock")) {
                const std::vector<SceneItemRef> one = { item.ref };
                const bool lock = !item.locked;
                state().deferred.push_back([&services, one, lock] {
                    services.setItemsLocked(one, lock);
                });
            }
            ImGui::PopStyleVar();
        }
        right -= btnGap;
    }
    // Eye: visible on hover or while hidden.
    if (item.canHide) {
        right -= btnH;
        if (rowHovered || !item.visible) {
            ImGui::SetCursorScreenPos(ImVec2(right, rowMin.y + (rowH - btnH) * 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(btnPad, btnPad));
            if (UiKit::IconButton("##eye",
                                  item.visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
                                  item.visible ? "Hide" : "Show")) {
                const std::vector<SceneItemRef> one = { item.ref };
                const bool show = !item.visible;
                state().deferred.push_back([&services, one, show] {
                    services.setItemsVisible(one, show);
                });
            }
            ImGui::PopStyleVar();
        }
        right -= btnGap;
    }

    // Badge (streaming %, point count, node count, measured value) — drawn
    // via the draw list so it costs no layout.
    if (!item.badge.empty()) {
        const ImVec2 size = ImGui::CalcTextSize(item.badge.c_str());
        const float x = right - size.x;
        if (x > contentX + 30.0f * scale)
            dl->AddText(ImVec2(x, rowMin.y + (rowH - size.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        item.badge.c_str());
    }

    // Land the cursor at a known-good Y for whatever comes next (a sibling
    // row, or the caller's Indent/children) — every element above was
    // absolutely positioned via SetCursorScreenPos, so ImGui's own
    // line-height bookkeeping can't be
    // trusted to have tracked the full rowH; pin it explicitly (plus the
    // inter-row gap) instead of risking the next row overlapping this one.
    ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y + rowGap()));

    ImGui::PopID();
    return opened && !leaf;
}

void drawItemRow(Services& services, const Item& item);

// A background band + left line from a parent row down through its children
// — the "this is a real tree, not a flat indented list" cue plain indent
// alone doesn't give (Explorer/VS Code convention). The Y span isn't known
// until every child has been laid out, so the actual drawing is deferred:
// every expanded parent pushes its (x, y0, y1) here, and the caller (the
// "Scene objects" section) draws them all in one pass, on a draw-list
// channel BEHIND the rows themselves (see ChannelsSplit below) — nesting
// group-in-group means an arbitrary number of these can be pending at once,
// and ImDrawList's channel splitter is not safe to nest per-row.
struct ChildBand { float x, y0, y1; };
std::vector<ChildBand>& pendingBands() {
    static std::vector<ChildBand> bands;
    return bands;
}

void drawChildBand(ImDrawList* dl, const ChildBand& b) {
    if (b.y1 <= b.y0)
        return;
    // A clean, thin, low-contrast line — no background fill. A filled band
    // read as too heavy; plain text color at low alpha (not the accent hue)
    // keeps it a quiet structural cue rather than a colored decoration.
    ImVec4 line = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    line.w = 0.16f;
    dl->AddLine(ImVec2(b.x, b.y0), ImVec2(b.x, b.y1), ImGui::GetColorU32(line),
               1.0f * UiKit::Scale());
}

// ── Rolling the children of an expandable row open and shut ─────────────────
//
// Same construction as UiKit::BeginSection: an outer child whose height IS the
// animation, wrapping an auto-sized inner child that always lays the children out
// at their natural full height — so the height we animate toward is ImGui's OWN
// number for those children and the roll can land on it exactly.
//
// The children keep being laid out while the node is rolling SHUT (the caller has
// already been told the node is closed), clipped away as the outer child shrinks.

// The children's measured height, keyed by the row. Small and bounded (one entry
// per expandable model/group), so it needs no sweeping.
std::unordered_map<ImGuiID, float>& rollHeights() {
    static std::unordered_map<ImGuiID, float> heights;
    return heights;
}

// A key derived from the item's REF, not from ImGui's id stack: the roll's state
// must survive the row moving to a different depth (dragged into a group), which
// changes its ImGui id but not its identity.
ImGuiID rollKey(const SceneItemRef& ref) {
    ImGuiID hash = 2166136261u;
    auto mix = [&hash](uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= ImGuiID((value >> (i * 8)) & 0xFF);
            hash *= 16777619u;
        }
    };
    mix(uint64_t(int(ref.kind)));
    mix(ref.id);
    mix(uint64_t(uint32_t(ref.sub)));
    return hash;
}

// Returns true while the children should be laid out (open, or still rolling
// shut). Pair with endChildRoll() ONLY when it returned true.
bool beginChildRoll(ImGuiID key, bool open) {
    // A critically-damped SPRING, not an exponential ease-out. Ease-out has its
    // highest speed at t=0, so a tall group would dump most of its height in the
    // first two frames and then crawl — which reads as "it just snapped open".
    // A spring starts at zero velocity (a gentle ease-IN) and decelerates into
    // place, and its rate does not scale with the group's height, so a group of
    // forty rows opens with the same unhurried feel as a group of three. Clamped
    // to [0,1] so any overshoot from a high bounce preference caps at the full
    // height (an invisible settle) instead of showing empty space past the rows.
    const float raw =
        UiKit::Spring(key ^ 0x5EC10011u, open ? 1.0f : 0.0f, 4.0f, 1.0f);
    const float t = std::clamp(raw, 0.0f, 1.0f);
    if (!open && t <= 0.002f)
        return false;

    const float measured = rollHeights()[key];
    const bool settled = UiKit::ReduceMotion() || (open && t >= 0.999f);
    const float clipH = std::max(1.0f, measured * t);

    // Zero the spacing AROUND the clip child (it is an item in the tree, and that
    // gap would otherwise pop in and out at full size at either end of the roll),
    // and restore the real spacing inside for the child rows.
    const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(key)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing.x, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##rollClip", ImVec2(0.0f, settled ? 0.0f : clipH),
                      settled ? ImGuiChildFlags_AutoResizeY : ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginChild("##rollBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);
    return true;
}

// Closes the roll and leaves the CLIP child as the last item, so the caller can
// read its (animated) rect for the hierarchy guide line.
void endChildRoll(ImGuiID key) {
    ImGui::PopStyleVar(); // ItemSpacing (the restored one)
    ImGui::EndChild();    // ##rollBody — auto-sized: ImGui's own height
    const float measured = ImGui::GetItemRectSize().y;
    if (measured > 1.0f)
        rollHeights()[key] = measured;
    ImGui::EndChild();     // ##rollClip
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2); // WindowPadding, the zeroed ItemSpacing
    ImGui::PopID();
}

// Model row + its expandable mesh children.
void drawModelRow(Services& services, const Item& item) {
    scene::Scene& scene = services.scene();
    const bool asTree = item.meshCount > 1;
    const bool opened = drawRow(services, item, asTree, !asTree);

    // Children: the model's meshes (sub-index refs — contract C3).
    auto drawMeshes = [&] {
        SceneItemRef modelRef = item.ref;
        if (!scene.resolve(modelRef))
            return;
        const scene::Model& model = scene.models[modelRef.index];
        for (int mi = 0; mi < int(model.meshes.size()); ++mi) {
            Item mesh;
            mesh.ref = modelRef;
            mesh.ref.kind = Kind::Mesh;
            mesh.ref.sub = mi;
            mesh.kind = UiKit::ObjectKind::Mesh;
            mesh.name = model.meshes[size_t(mi)].name.empty()
                            ? "mesh " + std::to_string(mi)
                            : model.meshes[size_t(mi)].name;
            mesh.visible = item.visible;
            mesh.locked = item.locked;
            mesh.canHide = false; // per-mesh visibility is a Pass-2 topic
            mesh.canLock = false;
            mesh.canDuplicate = false;
            mesh.canDelete = false;
            mesh.canGroup = false;
            drawRow(services, mesh, false, true);
        }
    };

    // A single-mesh model is a LEAF: it has no disclosure arrow, so there is
    // nothing to roll — it never changes state, and wrapping it in a clip child
    // would only cost two windows per model.
    if (!asTree) {
        if (!opened)
            return;
        ImGui::Indent(indentStep());
        const float guideX = ImGui::GetCursorScreenPos().x - indentStep() * 0.5f;
        const float startY = ImGui::GetCursorScreenPos().y;
        drawMeshes();
        pendingBands().push_back(
            { guideX, startY, ImGui::GetCursorScreenPos().y - rowGap() });
        ImGui::Unindent(indentStep());
        return;
    }

    const ImGuiID key = rollKey(item.ref);
    ImGui::Indent(indentStep());
    const float guideX = ImGui::GetCursorScreenPos().x - indentStep() * 0.5f;
    if (beginChildRoll(key, opened)) {
        drawMeshes();
        endChildRoll(key);
        // The guide line spans the CLIP child, so it rolls with the children
        // instead of hanging below them mid-collapse.
        pendingBands().push_back({ guideX, ImGui::GetItemRectMin().y,
                                   ImGui::GetItemRectMax().y - rowGap() });
    }
    ImGui::Unindent(indentStep());
}

void drawItemRow(Services& services, const Item& item) {
    if (item.ref.kind == Kind::Model)
        drawModelRow(services, item);
    else
        drawRow(services, item, false, true);
}

// Section header: collapsible, count badge, optional drop-to-root target.
bool sectionHeader(Services& services, const char* label, size_t count,
                   bool dropToRoot) {
    // A heavier header than the default — matches the taller rows below it
    // instead of reading as a thin strip above a roomy list.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(UiKit::Space(4), UiKit::Space(4)));
    const bool open = ImGui::CollapsingHeader(
        label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::PopStyleVar();
    if (dropToRoot && ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("SV_OUTLINER_ITEMS")) {
            const std::vector<SceneItemRef> refs = state().dragRefs;
            state().deferred.push_back([&services, refs] {
                services.moveItemsToGroup(refs, 0); // scene root = ungrouped
            });
        }
        ImGui::EndDragDropTarget();
    }
    // Count badge, right-aligned on the header row.
    char badge[16];
    std::snprintf(badge, sizeof(badge), "%d", int(count));
    const ImVec2 headerMin = ImGui::GetItemRectMin();
    const ImVec2 headerMax = ImGui::GetItemRectMax();
    const ImVec2 size = ImGui::CalcTextSize(badge);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(headerMax.x - size.x - UiKit::Space(4),
               headerMin.y + (headerMax.y - headerMin.y - size.y) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), badge);
    return open;
}

// Scene-objects section: user groups (nested folders) + ungrouped items. An
// active search/filter flattens the hierarchy to the matching rows.
void drawSceneObjects(Services& services, const std::vector<Item>& items) {
    PanelState& s = state();
    scene::Scene& scene = services.scene();
    const bool flat = s.search[0] != '\0' || s.filterMask != 0;

    if (flat) {
        for (const Item& item : items)
            drawItemRow(services, item);
        return;
    }

    // Group rows are synthesized from scene.groups (they are structure, not
    // provider content). Orphaned parents fall back to the root level.
    std::function<void(uint64_t)> drawLevel = [&](uint64_t parentId) {
        for (size_t g = 0; g < scene.groups.size(); ++g) {
            const scene::Group& group = scene.groups[g];
            const bool orphan =
                group.parentId != 0 && !scene.findGroup(group.parentId);
            const bool atThisLevel =
                (group.parentId == parentId) || (parentId == 0 && orphan);
            if (!atThisLevel)
                continue;
            Item item;
            item.ref.kind = Kind::Group;
            item.ref.id = group.id;
            item.ref.index = int(g);
            item.kind = UiKit::ObjectKind::Group;
            item.name = group.name;
            item.visible = group.visible;
            item.locked = group.locked;
            item.canDuplicate = false;
            size_t members = 0;
            for (const Item& candidate : items)
                if (candidate.groupId == group.id)
                    ++members;
            for (const scene::Group& sub : scene.groups)
                if (sub.parentId == group.id)
                    ++members;
            item.badge = std::to_string(members);
            const bool opened = drawRow(services, item, true, false);
            const ImGuiID key = rollKey(item.ref);
            ImGui::Indent(indentStep());
            const float guideX =
                ImGui::GetCursorScreenPos().x - indentStep() * 0.5f;
            if (beginChildRoll(key, opened)) {
                drawLevel(group.id);
                for (const Item& candidate : items)
                    if (candidate.groupId == group.id)
                        drawItemRow(services, candidate);
                endChildRoll(key);
                pendingBands().push_back({ guideX, ImGui::GetItemRectMin().y,
                                           ImGui::GetItemRectMax().y - rowGap() });
            }
            ImGui::Unindent(indentStep());
        }
    };
    drawLevel(0);
    for (const Item& item : items)
        if (item.groupId == 0 || !scene.findGroup(item.groupId))
            drawItemRow(services, item);
}

} // namespace

// The Outliner (docs/UI_REDESIGN.md §7). Selection is the app-wide
// scene::Selection; every mutation routes through the Services item ops.
void drawScenePanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Scene, open)) {
        ImGui::End();
        return;
    }

    PanelState& s = state();
    s.prevRows = std::move(s.rows);
    s.rows.clear();
    s.deferred.clear();

    // ── Toolbar: a compact filter popup beside the search field. A full row
    // of pill buttons read as toolbar clutter sitting on top of the tree; a
    // single funnel icon (accent-tinted + a dot while any filter is active)
    // carries the same capability without competing for attention. ─────────
    {
        const bool anyFilter = s.filterMask != 0;
        if (anyFilter)
            ImGui::PushStyleColor(ImGuiCol_Text, UiKit::Color(UiKit::Semantic::Accent));
        if (UiKit::IconButton("##filter", ICON_FA_FILTER,
                              anyFilter ? "Filters active - click to edit"
                                       : "Filter by type"))
            ImGui::OpenPopup("##outliner_filter_popup");
        if (anyFilter)
            ImGui::PopStyleColor();
        if (anyFilter) {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(bmax.x - 2.0f * UiKit::Scale(), bmin.y + 2.0f * UiKit::Scale()),
                3.0f * UiKit::Scale(),
                ImGui::GetColorU32(UiKit::Color(UiKit::Semantic::Accent)));
        }
        if (ImGui::BeginPopup("##outliner_filter_popup")) {
            auto filterRow = [&](const char* label, uint32_t bit) {
                bool active = (s.filterMask & bit) != 0;
                if (ImGui::Checkbox(label, &active))
                    s.filterMask ^= bit;
            };
            ImGui::TextDisabled("Show only");
            ImGui::Separator();
            filterRow("Models", kFilterModels);
            filterRow("Point clouds", kFilterClouds);
            filterRow("Scene layers", kFilterLayers);
            filterRow("Lights", kFilterLights);
            filterRow("Annotations", kFilterNotes);
            if (s.filterMask != 0) {
                ImGui::Separator();
                if (ImGui::Selectable("Clear filters"))
                    s.filterMask = 0;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        UiKit::SearchInput("##outliner_search", s.search, sizeof(s.search),
                           "Search objects and types...");
    }
    ImGui::Dummy(ImVec2(0.0f, UiKit::Space(2)));

    // ── Collect + filter the three sections ─────────────────────────────────
    std::vector<Item> sections[size_t(Outliner::Section::Count)];
    size_t totalObjects = 0, totalShown = 0;
    for (size_t sec = 0; sec < size_t(Outliner::Section::Count); ++sec) {
        std::vector<Item> raw;
        Outliner::collect(services, Outliner::Section(sec), raw);
        totalObjects += raw.size();
        for (Item& item : raw) {
            if (passesFilter(s, item)) {
                sections[sec].push_back(std::move(item));
                ++totalShown;
            }
        }
    }

    // F2 on the primary selection starts a rename (needs the item's name —
    // find it among the collected rows).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_F2, false) && !s.renameRef.valid()) {
        const SceneItemRef primary = services.selection().primary();
        for (const std::vector<Item>& section : sections)
            for (const Item& item : section)
                if (item.ref == primary && item.canRename)
                    beginRename(item.ref, item.name);
    }

    // ── The tree ────────────────────────────────────────────────────────────
    // Explicit padding on all four sides (a plain BeginChild with no flags
    // otherwise renders flush against its content — rows would sit right up
    // against the panel's left/right/bottom edges, cramped regardless of the
    // per-row leftPad above).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(UiKit::Space(3), UiKit::Space(3)));
    ImGui::BeginChild("##outliner_tree", ImVec2(0, 0),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    // Roomier per-level indent than ImGui's default — nested groups/meshes
    // read more clearly as a hierarchy (Explorer-style) at the taller row
    // height above. Shared with the child-guide-line X math (indentStep()).
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, indentStep());

    const scene::Scene& scene = services.scene();
    const bool sceneEmpty = scene.models.empty() && scene.pointClouds.empty() &&
                            scene.i3sLayers.empty() && scene.pointLights.empty() &&
                            scene.measurements.empty() && scene.clipPlanes.empty();

    if (sceneEmpty) {
        UiKit::EmptyState(ICON_FA_CUBE, "Empty scene",
                          "Import models, point clouds or scene layers from the "
                          "File menu - or drop files anywhere in the window.");
    } else if (totalShown == 0) {
        // The empty search state explains itself (§7 acceptance).
        std::string hint = "Nothing matches \"" + std::string(s.search) + "\"";
        if (s.filterMask != 0)
            hint += " with the active type filters";
        hint += ". Clear the search or the chips to see all " +
                std::to_string(totalObjects) + " objects.";
        UiKit::EmptyState(ICON_FA_SEARCH, "No matches", hint.c_str());
    } else {
        // Section content stays flush with its header — only rows actually
        // nested inside a group/model (via the explicit Indent/Unindent in
        // drawModelRow / drawLevel, indentStep() below) get the extra
        // indent, not every top-level item in the section.
        if (sectionHeader(services, "Environment", sections[0].size(), false)) {
            for (const Item& item : sections[0])
                drawRow(services, item, false, true);
        }
        if (sectionHeader(services, "Scene objects", sections[1].size(), true)) {
            // ONE channel split for the whole section (never per-row/per-
            // group): ImDrawList's channel splitter isn't safe to nest, and
            // groups can nest arbitrarily deep, so every expanded parent's
            // connector-line span is deferred into pendingBands() and drawn
            // here in a single pass, behind (channel 0) everything the rows
            // themselves drew (channel 1).
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            drawSceneObjects(services, sections[1]);
            dl->ChannelsSetCurrent(0);
            for (const ChildBand& band : pendingBands())
                drawChildBand(dl, band);
            pendingBands().clear();
            dl->ChannelsMerge();
        }
        if (sectionHeader(services, "Annotations & tool output",
                          sections[2].size(), false)) {
            if (sections[2].empty())
                ImGui::TextDisabled(
                    "  (measurements and section planes land here)");
            for (const Item& item : sections[2])
                drawRow(services, item, false, true);
        }
    }

    ImGui::PopStyleVar(); // IndentSpacing
    ImGui::EndChild();
    ImGui::End();

    // Mutations run AFTER the tree walk (see PanelState::deferred).
    for (const std::function<void()>& op : s.deferred)
        op();
    s.deferred.clear();
}

} // namespace Gui
