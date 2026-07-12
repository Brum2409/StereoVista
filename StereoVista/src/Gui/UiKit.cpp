#include "Gui/UiKit.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui_sytle.h" // g_Fonts / g_GuiScale / g_StyleColors / theme queries

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Gui {
namespace UiKit {

namespace {

ImVec4 withAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

} // namespace

// ── Tokens ──────────────────────────────────────────────────────────────────

float Scale() {
    return g_GuiScale.currentScale;
}

float Space(int step) {
    static const float kSteps[] = { 2.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f };
    const int i = std::max(1, std::min(step, 7)) - 1;
    return kSteps[i] * Scale();
}

float RadiusInner() { return 4.0f * Scale(); }
float RadiusCard()  { return 8.0f * Scale(); }

static bool s_reduceMotion = false;

void SetReduceMotion(bool reduce) { s_reduceMotion = reduce; }
bool ReduceMotion() { return s_reduceMotion; }

float Anim01(ImGuiID id, float target, float durationSec) {
    target = std::max(0.0f, std::min(target, 1.0f));
    if (s_reduceMotion || durationSec <= 0.0f)
        return target;

    struct AnimState { float value; int lastFrame; };
    static std::unordered_map<ImGuiID, AnimState> s_anims;

    const int frame = ImGui::GetFrameCount();

    // Occasional sweep so one-off ids don't accumulate forever.
    if (s_anims.size() > 1024) {
        for (auto it = s_anims.begin(); it != s_anims.end();) {
            if (frame - it->second.lastFrame > 120) it = s_anims.erase(it);
            else ++it;
        }
    }

    auto [it, inserted] = s_anims.try_emplace(id, AnimState{ target, frame - 1 });
    AnimState& st = it->second;
    if (st.lastFrame == frame)
        return st.value;              // second query this frame: no double-advance
    if (st.lastFrame != frame - 1)
        st.value = target;            // stale entry (widget re-appeared): snap
    st.lastFrame = frame;

    // Exponential ease-out sized so the value covers ~95% of the way in
    // `durationSec` (the app redraws every frame, so this is all it takes).
    const float dt = ImGui::GetIO().DeltaTime;
    const float t = 1.0f - std::exp(-3.0f * dt / durationSec);
    st.value += (target - st.value) * t;
    if (std::fabs(target - st.value) < 0.001f)
        st.value = target;
    return st.value;
}

ImVec4 Color(Semantic semantic) {
    switch (semantic) {
    case Semantic::Primary: return g_StyleColors.primary;
    case Semantic::Accent:  return g_StyleColors.accent;
    case Semantic::Success: return g_StyleColors.success;
    case Semantic::Warning: return g_StyleColors.warning;
    case Semantic::Danger:  return g_StyleColors.danger;
    case Semantic::Info:    return g_StyleColors.info;
    }
    return g_StyleColors.accent;
}

// ── Object kinds ────────────────────────────────────────────────────────────

KindStyle StyleFor(ObjectKind kind) {
    // Fixed hues tuned for dark themes; light themes darken them below so the
    // same kind stays recognizable on every theme (C2).
    struct Entry { const char* icon; ImVec4 color; const char* noun; };
    static const Entry kEntries[] = {
        /* Model       */ { ICON_FA_CUBE,           ImVec4(1.00f, 0.64f, 0.30f, 1.0f), "Model" },
        /* Mesh        */ { ICON_FA_VECTOR_SQUARE,  ImVec4(0.95f, 0.76f, 0.45f, 1.0f), "Mesh" },
        /* PointCloud  */ { ICON_FA_CLOUD,          ImVec4(0.45f, 0.75f, 1.00f, 1.0f), "Point cloud" },
        /* SceneLayer  */ { ICON_FA_LAYER_GROUP,    ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "Scene layer" },
        /* Sun         */ { ICON_FA_SUN,            ImVec4(1.00f, 0.85f, 0.25f, 1.0f), "Sun" },
        /* PointLight  */ { ICON_FA_LIGHTBULB,      ImVec4(1.00f, 0.78f, 0.40f, 1.0f), "Point light" },
        /* SpotLight   */ { ICON_FA_BOLT,           ImVec4(1.00f, 0.70f, 0.50f, 1.0f), "Spot light" },
        /* Group       */ { ICON_FA_FOLDER,         ImVec4(0.62f, 0.70f, 0.85f, 1.0f), "Group" },
        /* Measurement */ { ICON_FA_RULER,          ImVec4(0.45f, 0.90f, 0.55f, 1.0f), "Measurement" },
        /* ClipPlane   */ { ICON_FA_CUT,            ImVec4(0.95f, 0.50f, 0.55f, 1.0f), "Clip plane" },
        /* Snapshot    */ { ICON_FA_CAMERA_RETRO,   ImVec4(0.75f, 0.60f, 1.00f, 1.0f), "Snapshot" },
        /* Environment */ { ICON_FA_MOUNTAIN,       ImVec4(0.50f, 0.80f, 0.90f, 1.0f), "Environment" },
        /* Tool        */ { ICON_FA_TOOLS,          ImVec4(0.70f, 0.75f, 0.80f, 1.0f), "Tool" },
        /* Setting     */ { ICON_FA_SLIDERS_H,      ImVec4(0.65f, 0.72f, 0.78f, 1.0f), "Setting" },
        /* Command     */ { ICON_FA_TERMINAL,       ImVec4(0.72f, 0.65f, 0.95f, 1.0f), "Command" },
        /* File        */ { ICON_FA_FILE,           ImVec4(0.68f, 0.72f, 0.76f, 1.0f), "File" },
        /* BrushCluster*/ { ICON_FA_PAINT_BRUSH,    ImVec4(0.95f, 0.55f, 0.85f, 1.0f), "Brush cluster" },
        /* LiveCapture */ { ICON_FA_VIDEO,          ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "Live capture" },
    };
    static_assert(sizeof(kEntries) / sizeof(kEntries[0]) ==
                      static_cast<size_t>(ObjectKind::Count),
                  "kEntries must cover every ObjectKind");

    const int i = static_cast<int>(kind);
    KindStyle style;
    if (i < 0 || i >= static_cast<int>(ObjectKind::Count)) {
        // Unknown kind (newer data / plugin): neutral, never crash (C2 note).
        style.icon = ICON_FA_QUESTION_CIRCLE;
        style.color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        style.noun = "Object";
        return style;
    }
    style.icon = kEntries[i].icon;
    style.color = kEntries[i].color;
    style.noun = kEntries[i].noun;
    if (!IsGuiThemeDark(g_currentTheme)) {
        // Darken the pastel hues for contrast on light surfaces.
        style.color.x *= 0.68f;
        style.color.y *= 0.68f;
        style.color.z *= 0.68f;
    }
    return style;
}

// ── Widgets: ports of the proven GL GUI.cpp widgets (:498-668) ──────────────

void InlineIcon(const char* icon, const ImVec4& color) {
    ImGui::AlignTextToFramePadding();
    if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine();
    }
}

ImFont* IconFont() { return g_Fonts.icons; }

void SectionHeader(const char* label) {
    const float scale = Scale();
    ImGui::Spacing();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fontSize = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 accent = ImGui::GetColorU32(g_StyleColors.accent);
    dl->AddRectFilled(ImVec2(p.x, p.y + 2.0f * scale),
                      ImVec2(p.x + 3.5f * scale, p.y + fontSize), accent,
                      2.0f * scale);
    ImGui::Indent(10.0f * scale);
    ImGui::TextUnformatted(label);
    ImGui::Unindent(10.0f * scale);
    ImGui::Spacing();
    ImGui::Separator();
}

void PanelTitle(const char* icon, const std::string& title) {
    InlineIcon(icon, g_StyleColors.accent);
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();
}

void MenuBarSeparator() {
    const float scale = Scale();
    const float barTop = ImGui::GetWindowPos().y;
    const float barHeight = ImGui::GetFrameHeight();
    const float thickness = 2.0f * scale;
    const float padX = 5.0f * scale;   // horizontal breathing room either side
    const float insetY = 6.0f * scale; // shrink in from the top and bottom edges

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float x = cursor.x + padX + thickness * 0.5f;
    const float y0 = barTop + insetY;
    const float y1 = barTop + barHeight - insetY;

    ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    col.w = 0.20f;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(x - thickness * 0.5f, y0), ImVec2(x + thickness * 0.5f, y1),
        ImGui::GetColorU32(col), thickness * 0.5f);

    // Reserve the horizontal slot so the following menu flows past the divider.
    ImGui::Dummy(ImVec2(padX * 2.0f + thickness, 0.0f));
}

bool NavItem(const char* icon, const char* label, bool selected) {
    const float scale = Scale();
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const float rowH = ImGui::GetFrameHeight() + 8.0f * scale;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          withAlpha(g_StyleColors.primary, 0.16f));
    // selected=false: we paint the accent pill ourselves below.
    const bool clicked = ImGui::Selectable("##navitem", false,
                                           ImGuiSelectableFlags_None,
                                           ImVec2(fullWidth, rowH));
    ImGui::PopStyleColor();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fontSize = ImGui::GetFontSize();
    ImU32 textCol =
        ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);

    // Eased selection fill (§15 micro-motion; snaps when reduce-motion is on).
    const float sel = Anim01(ImGui::GetID(label), selected ? 1.0f : 0.0f);
    if (sel > 0.01f) {
        dl->AddRectFilled(p0, ImVec2(p0.x + fullWidth, p0.y + rowH),
                          ImGui::GetColorU32(
                              withAlpha(g_StyleColors.primary, 0.16f * sel)),
                          8.0f * scale);
        dl->AddRectFilled(
            ImVec2(p0.x, p0.y + rowH * 0.18f),
            ImVec2(p0.x + 3.0f * scale, p0.y + rowH * 0.82f),
            ImGui::GetColorU32(withAlpha(g_StyleColors.primary, sel)),
            2.0f * scale);
    }
    if (selected)
        textCol = ImGui::GetColorU32(ImGuiCol_Text);

    const float pad = 14.0f * scale;
    const float iconSlot = 24.0f * scale;
    const ImVec2 iconPos(p0.x + pad, p0.y + (rowH - fontSize) * 0.5f);
    if (g_Fonts.icons)
        dl->AddText(g_Fonts.icons, fontSize, iconPos, textCol, icon);
    const ImVec2 labelPos(p0.x + pad + iconSlot, p0.y + (rowH - fontSize) * 0.5f);
    dl->AddText(labelPos, textCol, label);

    return clicked;
}

bool ToggleSwitch(const char* label, bool* v) {
    const float scale = Scale();
    ImGui::PushID(label);
    const float height = ImGui::GetFrameHeight();
    const float width = height * 1.85f;
    const float radius = height * 0.5f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##toggle", ImVec2(width, height));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        *v = !*v;
        changed = true;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsItemHovered();
    // Knob position and track fill ease together (§15).
    const float t = Anim01(ImGui::GetID("##anim"), *v ? 1.0f : 0.0f);
    const ImVec4 off = ImGui::GetStyleColorVec4(hovered ? ImGuiCol_FrameBgHovered
                                                        : ImGuiCol_FrameBg);
    const ImVec4 on = hovered ? g_StyleColors.primaryHover : g_StyleColors.primary;
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                      ImGui::GetColorU32(lerpColor(off, on, t)), radius);
    const float knob = radius - 2.5f * scale;
    const float cx = p.x + radius + (width - 2.0f * radius) * t;
    dl->AddCircleFilled(ImVec2(cx, p.y + radius), knob,
                        IM_COL32(255, 255, 255, 255));

    if (label && label[0] != '\0' && !(label[0] == '#' && label[1] == '#')) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
    }
    ImGui::PopID();
    return changed;
}

void HelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ── Widgets: the redesign set ───────────────────────────────────────────────

bool SearchInput(const char* id, char* buf, size_t bufSize, const char* hint) {
    ImGui::PushID(id);
    InlineIcon(ICON_FA_SEARCH, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool hadText = buf[0] != '\0';
    const float clearW =
        hadText ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(
        std::max(ImGui::GetContentRegionAvail().x - clearW, 60.0f * Scale()));
    bool changed = ImGui::InputTextWithHint("##search", hint, buf, bufSize);
    if (hadText) {
        ImGui::SameLine();
        if (IconButton("##clear", ICON_FA_TIMES, "Clear search")) {
            buf[0] = '\0';
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

bool Chip(const char* label, bool active) {
    const ImVec4 base = active ? g_StyleColors.accent
                               : withAlpha(g_StyleColors.primary, 0.35f);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_StyleColors.primaryHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_StyleColors.primaryActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                        ImGui::GetFrameHeight() * 0.5f);
    const std::string id = std::string(label) + "##chip";
    const bool clicked = ImGui::SmallButton(id.c_str());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return clicked;
}

void Badge(const char* text, const ImVec4& color) {
    const float scale = Scale();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float padX = 6.0f * scale;
    const float padY = 1.5f * scale;
    const ImVec2 size(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                      ImGui::GetColorU32(withAlpha(color, 0.22f)), size.y * 0.5f);
    dl->AddText(ImVec2(p.x + padX, p.y + padY), ImGui::GetColorU32(color), text);
    ImGui::Dummy(size);
}

bool BeginCard(const char* id, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RadiusCard());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(Space(4), Space(4)));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders |
                            ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height <= 0.0f)
        flags |= ImGuiChildFlags_AutoResizeY;
    const bool open =
        ImGui::BeginChild(id, ImVec2(0.0f, height), flags, ImGuiWindowFlags_None);
    ImGui::PopStyleVar(2);
    return open;
}

bool BeginGlobalCard(const char* id, float height) {
    // Tinted so a global (non-per-object) block is unmistakable inside a
    // per-object context (§8), with a "GLOBAL" tag in the top-right corner.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, withAlpha(g_StyleColors.info, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(g_StyleColors.info, 0.45f));
    const bool open = BeginCard(id, height);
    ImGui::PopStyleColor(2);
    if (open) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const char* tag = "GLOBAL";
        ImFont* font = g_Fonts.smallFont ? g_Fonts.smallFont : ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize() * 0.78f;
        const ImVec2 tagSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, tag);
        const ImVec2 wp = ImGui::GetWindowPos();
        const float ww = ImGui::GetWindowWidth();
        dl->AddText(font, fontSize,
                    ImVec2(wp.x + ww - tagSize.x - Space(4), wp.y + Space(2)),
                    ImGui::GetColorU32(withAlpha(g_StyleColors.info, 0.90f)), tag);
    }
    return open;
}

void EndCard() {
    ImGui::EndChild();
}

bool IconButton(const char* strId, const char* icon, const char* tooltip) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          withAlpha(g_StyleColors.primary, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          withAlpha(g_StyleColors.primary, 0.32f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RadiusInner());
    bool clicked = false;
    ImGui::PushID(strId);
    if (g_Fonts.icons)
        ImGui::PushFont(g_Fonts.icons);
    const float h = ImGui::GetFrameHeight();
    clicked = ImGui::Button(icon, ImVec2(h, h));
    if (g_Fonts.icons)
        ImGui::PopFont();
    ImGui::PopID();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

bool SegmentedControl(const char* id, const char* const items[], int count,
                      int* current) {
    if (count <= 0)
        return false;
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f * Scale(), 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RadiusInner());
    for (int i = 0; i < count; ++i) {
        if (i > 0)
            ImGui::SameLine();
        const bool selected = (*current == i);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? g_StyleColors.primary
                                       : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              selected ? g_StyleColors.primaryHover
                                       : withAlpha(g_StyleColors.primary, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_StyleColors.primaryActive);
        ImGui::PushID(i);
        if (ImGui::Button(items[i]) && !selected) {
            *current = i;
            changed = true;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

void EmptyState(const char* icon, const char* title, const char* hint) {
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(0.0f, Space(6)));

    if (g_Fonts.icons && icon) {
        // The icon drawn larger through the icon font, centered and dim.
        const float iconSize = ImGui::GetFontSize() * 2.2f;
        const ImVec2 sz =
            g_Fonts.icons->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max((avail - sz.x) * 0.5f, 0.0f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(
            g_Fonts.icons, iconSize, p,
            ImGui::GetColorU32(ImGuiCol_TextDisabled), icon);
        ImGui::Dummy(sz);
        ImGui::Dummy(ImVec2(0.0f, Space(2)));
    }
    if (title) {
        const ImVec2 sz = ImGui::CalcTextSize(title);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max((avail - sz.x) * 0.5f, 0.0f));
        ImGui::TextUnformatted(title);
    }
    if (hint) {
        const float wrapW = std::min(avail, 320.0f * Scale());
        const ImVec2 sz = ImGui::CalcTextSize(hint, nullptr, false, wrapW);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max((avail - sz.x) * 0.5f, 0.0f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
        ImGui::TextDisabled("%s", hint);
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0.0f, Space(6)));
}

void PropertyRow(const char* label, float labelFraction) {
    const float x0 = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered() &&
        ImGui::CalcTextSize(label).x > avail * labelFraction)
        ImGui::SetTooltip("%s", label); // truncated labels always tooltip (C9)
    ImGui::SameLine(x0 + avail * std::max(0.15f, std::min(labelFraction, 0.8f)));
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool ResetGlyph(const char* strId, bool modified) {
    if (!modified) {
        // Keep the slot so rows don't shift when a value becomes modified.
        const float h = ImGui::GetFrameHeight();
        ImGui::Dummy(ImVec2(h, h));
        return false;
    }
    return IconButton(strId, ICON_FA_UNDO, "Reset to default");
}

void Dot(bool on) {
    const float scale = Scale();
    const float d = 7.0f * scale;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    if (on)
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(p.x + d * 0.5f, p.y + h * 0.5f), d * 0.5f,
            ImGui::GetColorU32(g_StyleColors.accent));
    ImGui::Dummy(ImVec2(d, h));
}

int HintToast(const char* id, const char* text, const char* const actions[],
              int actionCount) {
    int result = -1;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          withAlpha(g_StyleColors.info, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(g_StyleColors.info, 0.50f));
    if (BeginCard(id)) {
        InlineIcon(ICON_FA_INFO_CIRCLE, g_StyleColors.info);
        const float closeW = ImGui::GetFrameHeight();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                               ImGui::GetContentRegionAvail().x - closeW -
                               Space(2));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::SameLine(ImGui::GetCursorPosX() +
                        ImGui::GetContentRegionAvail().x - closeW);
        if (IconButton("##dismiss", ICON_FA_TIMES, "Don't show this again"))
            result = kHintDismissed;
        for (int i = 0; i < actionCount; ++i) {
            if (i > 0)
                ImGui::SameLine();
            ImGui::PushID(i);
            if (ImGui::SmallButton(actions[i]))
                result = i;
            ImGui::PopID();
        }
    }
    EndCard();
    ImGui::PopStyleColor(2);
    return result;
}

// ── Fuzzy matching ──────────────────────────────────────────────────────────

bool FuzzyMatch(const char* needle, const char* haystack, int* outScore,
                int* outMatches, int maxMatches, int* outMatchCount) {
    if (outScore) *outScore = 0;
    if (outMatchCount) *outMatchCount = 0;
    if (!needle || !haystack)
        return false;
    if (needle[0] == '\0')
        return true; // an empty needle always matches (GL matcher behaviour)

    // Greedy first-fit subsequence walk (the GL snapshot matcher) extended
    // with a score: word-start and consecutive-run bonuses, gap penalties.
    int score = 0;
    int matchCount = 0;
    int prevMatch = -2;
    const char* h = haystack;
    int hIndex = 0;
    for (const char* n = needle; *n; ++n) {
        const char nc = static_cast<char>(
            std::tolower(static_cast<unsigned char>(*n)));
        bool found = false;
        for (; *h; ++h, ++hIndex) {
            const char hc = static_cast<char>(
                std::tolower(static_cast<unsigned char>(*h)));
            if (hc != nc)
                continue;
            score += 4;
            if (hIndex == prevMatch + 1)
                score += 8; // consecutive run
            const char before =
                (hIndex == 0) ? '\0' : haystack[hIndex - 1];
            if (hIndex == 0 || before == ' ' || before == '_' ||
                before == '-' || before == '.' || before == '/' ||
                before == ':')
                score += 8; // word start
            score -= std::min(hIndex - (prevMatch + 1), 8); // gap penalty
            prevMatch = hIndex;
            if (outMatches && matchCount < maxMatches)
                outMatches[matchCount] = hIndex;
            ++matchCount;
            ++h;
            ++hIndex;
            found = true;
            break;
        }
        if (!found)
            return false;
    }
    if (outScore) *outScore = score;
    if (outMatchCount) *outMatchCount = matchCount;
    return true;
}

void TextFuzzyHighlighted(const char* text, const int* matches,
                          int matchCount) {
    if (!text)
        return;
    if (!matches || matchCount <= 0) {
        ImGui::TextUnformatted(text);
        return;
    }
    // Draw alternating plain / highlighted byte runs on one line. Search
    // targets are names/ids (effectively ASCII); multi-byte characters are
    // never split because match indices come from the byte-wise matcher.
    const ImVec4 accent = g_StyleColors.accent;
    const int len = static_cast<int>(std::strlen(text));
    int pos = 0;
    int m = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    bool first = true;
    while (pos < len) {
        while (m < matchCount && matches[m] < pos)
            ++m;
        const bool highlighted = (m < matchCount && matches[m] == pos);
        int end = pos;
        if (highlighted) {
            while (end < len && m < matchCount && matches[m] == end) {
                ++end;
                ++m;
            }
        } else {
            end = (m < matchCount) ? matches[m] : len;
        }
        if (!first)
            ImGui::SameLine();
        first = false;
        if (highlighted)
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::TextUnformatted(text + pos, text + end);
        if (highlighted)
            ImGui::PopStyleColor();
        pos = end;
    }
    ImGui::PopStyleVar();
}

} // namespace UiKit
} // namespace Gui
