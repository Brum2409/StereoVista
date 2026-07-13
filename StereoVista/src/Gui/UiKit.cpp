#include "Gui/UiKit.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui_internal.h" // HoveredIdPreviousFrame / ActiveId (motion core)
#include "imgui/imgui_sytle.h" // g_Fonts / g_GuiScale / g_StyleColors / theme queries

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

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

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// ── The one motion store ────────────────────────────────────────────────────
//
// Every primitive (Anim01, Spring, Kick, ChangeFlash, the ripples) keeps its
// state here, keyed by an ImGuiID. Effects that share a widget id salt it (id ^
// kSaltHover, …) so they get their own slot. Entries are frame-stamped and the
// map is swept once it grows, which is what makes it safe to animate ids that
// only exist for a frame.
struct MotionState {
    float  value = 0.0f;
    float  velocity = 0.0f;
    float  reference = 0.0f; // ChangeFlash: last seen value; ripple: elapsed t
    ImVec2 point{ 0.0f, 0.0f }; // ripple origin
    int    lastFrame = -1;
    bool   seeded = false;
};

// Salts keep independent effects on the same widget id from colliding.
constexpr ImGuiID kSaltHover  = 0x9E3779B9u;
constexpr ImGuiID kSaltPress  = 0x85EBCA6Bu;
constexpr ImGuiID kSaltFocus  = 0xC2B2AE35u;
constexpr ImGuiID kSaltChange = 0x27D4EB2Fu;
constexpr ImGuiID kSaltPop    = 0x165667B1u;
constexpr ImGuiID kSaltGlow   = 0xD3A2646Cu;

std::unordered_map<ImGuiID, MotionState>& motionStore() {
    static std::unordered_map<ImGuiID, MotionState> store;
    return store;
}

MotionState& motionState(ImGuiID id, bool* outFresh = nullptr) {
    std::unordered_map<ImGuiID, MotionState>& store = motionStore();
    const int frame = ImGui::GetFrameCount();

    // Sweep ids that stopped being queried (one-off rows, closed panels).
    if (store.size() > 2048) {
        for (auto it = store.begin(); it != store.end();) {
            if (frame - it->second.lastFrame > 120)
                it = store.erase(it);
            else
                ++it;
        }
    }

    auto [it, inserted] = store.try_emplace(id);
    MotionState& st = it->second;
    // "Fresh" = first ever query, or the widget vanished for a while and came
    // back (a reopened panel must not animate in from wherever it left off).
    const bool fresh = inserted || !st.seeded || st.lastFrame < frame - 1;
    if (outFresh)
        *outFresh = fresh;
    return st;
}

// The live motion preferences (synced once per frame from Settings).
bool  s_reduceMotion = false;
float s_motionSpeed = 1.0f;
float s_motionBounce = 1.0f;
float s_motionTime = 0.0f;
int   s_motionTimeFrame = -1;

// Frame delta, clamped so a hitch (a scene load, a shader compile) can't launch
// a spring across the screen or blow it up.
float motionDt() {
    return std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f) * s_motionSpeed;
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

// One unified corner radius for everything rounded (matches the ImGui style's
// kRounding). Both names kept for call-site clarity; they return the same value.
float RadiusInner() { return 6.0f * Scale(); }
float RadiusCard()  { return 6.0f * Scale(); }

void SetMotion(bool reduce, float speed, float bounce) {
    s_reduceMotion = reduce;
    s_motionSpeed = std::max(0.1f, std::min(speed, 4.0f));
    s_motionBounce = std::max(0.0f, std::min(bounce, 2.0f));
}
void SetReduceMotion(bool reduce) { s_reduceMotion = reduce; }
bool ReduceMotion() { return s_reduceMotion; }
float MotionBounce() { return s_motionBounce; }

float MotionTime() {
    // Advanced once per frame however many callers ask for it, and frozen under
    // reduce-motion so every ambient animation holds still rather than jumping
    // when the preference is turned back on.
    const int frame = ImGui::GetFrameCount();
    if (frame != s_motionTimeFrame) {
        s_motionTimeFrame = frame;
        if (!s_reduceMotion)
            s_motionTime += motionDt();
    }
    return s_motionTime;
}

// ── Curves ──────────────────────────────────────────────────────────────────

float EaseOutCubic(float t) {
    t = clamp01(t);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float EaseOutBack(float t) {
    t = clamp01(t);
    // The classic back-ease, with its overshoot scaled by the live preference so
    // "bounciness 0" degrades cleanly to a plain ease rather than a special case.
    const float overshoot = 1.70158f * s_motionBounce;
    const float inv = t - 1.0f;
    return 1.0f + (overshoot + 1.0f) * inv * inv * inv + overshoot * inv * inv;
}

float EaseOutElastic(float t) {
    t = clamp01(t);
    if (t <= 0.0f || t >= 1.0f)
        return t;
    const float period = 0.36f;
    const float amplitude = s_motionBounce;
    return 1.0f + amplitude * std::pow(2.0f, -10.0f * t) *
                      std::sin((t * 10.0f - 0.75f) * (2.0f * 3.14159265f) / (period * 3.0f));
}

// ── Primitives ──────────────────────────────────────────────────────────────

float Anim01(ImGuiID id, float target, float durationSec) {
    target = clamp01(target);
    if (s_reduceMotion || durationSec <= 0.0f)
        return target;

    const int frame = ImGui::GetFrameCount();
    bool fresh = false;
    MotionState& st = motionState(id, &fresh);
    if (st.lastFrame == frame)
        return st.value; // queried twice this frame: never double-advance
    if (fresh) {
        st.value = target; // first sight (or re-appeared): start settled
        st.seeded = true;
    }
    st.lastFrame = frame;

    // Exponential ease-out sized so the value covers ~95% of the way in
    // `durationSec` (the app redraws every frame, so this is all it takes).
    const float t = 1.0f - std::exp(-3.0f * motionDt() / durationSec);
    st.value += (target - st.value) * t;
    if (std::fabs(target - st.value) < 0.001f)
        st.value = target;
    return st.value;
}

float Spring(ImGuiID id, float target, float freq, float damp, float* outVelocity) {
    if (s_reduceMotion) {
        if (outVelocity)
            *outVelocity = 0.0f;
        return target;
    }

    const int frame = ImGui::GetFrameCount();
    bool fresh = false;
    MotionState& st = motionState(id, &fresh);
    if (fresh) {
        st.value = target; // arrive settled, don't spring in from nowhere
        st.velocity = 0.0f;
        st.seeded = true;
    }
    if (st.lastFrame == frame) {
        if (outVelocity)
            *outVelocity = st.velocity;
        return st.value; // queried twice this frame
    }
    st.lastFrame = frame;

    // Damped harmonic oscillator, integrated semi-implicitly. The bounce
    // preference under-damps it: damp 1.0 settles without overshoot, lower
    // values overshoot and swing back — which is the whole point.
    const float ratio = std::max(0.05f, damp / std::max(s_motionBounce, 0.05f));
    const float omega = 2.0f * 3.14159265f * freq;

    // Sub-step so a long frame can't make the integrator explode (an under-damped
    // spring is only stable while omega*dt stays small).
    float dt = motionDt();
    const float maxStep = 1.0f / (omega * 4.0f + 60.0f);
    int steps = std::min(8, std::max(1, int(dt / maxStep) + 1));
    const float h = dt / float(steps);
    for (int i = 0; i < steps; ++i) {
        const float accel = -2.0f * ratio * omega * st.velocity -
                            omega * omega * (st.value - target);
        st.velocity += accel * h;
        st.value += st.velocity * h;
    }
    if (std::fabs(target - st.value) < 0.0008f && std::fabs(st.velocity) < 0.01f) {
        st.value = target;
        st.velocity = 0.0f;
    }
    if (outVelocity)
        *outVelocity = st.velocity;
    return st.value;
}

float Appear(ImGuiID id, bool reset, float freq, float damp) {
    if (s_reduceMotion)
        return 1.0f;
    bool fresh = false;
    MotionState& st = motionState(id, &fresh);
    if (fresh || reset) {
        st.value = 0.0f;
        st.velocity = 0.0f;
        st.seeded = true;
        st.lastFrame = ImGui::GetFrameCount() - 1; // let Spring advance it today
    }
    return Spring(id, 1.0f, freq, damp);
}

void Kick(ImGuiID id, float strength) {
    if (s_reduceMotion)
        return;
    MotionState& st = motionState(id);
    st.seeded = true;
    st.value = std::max(st.value, strength); // a re-kick mid-decay tops it up
    st.lastFrame = ImGui::GetFrameCount();
}

float Kicked(ImGuiID id, float decaySec) {
    if (s_reduceMotion)
        return 0.0f;
    const int frame = ImGui::GetFrameCount();
    bool fresh = false;
    MotionState& st = motionState(id, &fresh);
    if (fresh) {
        st.value = 0.0f;
        st.seeded = true;
        st.lastFrame = frame;
        return 0.0f;
    }
    if (st.lastFrame != frame) {
        st.lastFrame = frame;
        if (decaySec > 0.0f)
            st.value *= std::exp(-motionDt() / (decaySec * 0.35f));
        if (st.value < 0.002f)
            st.value = 0.0f;
    }
    return st.value;
}

float ChangeFlash(ImGuiID id, float value, float decaySec) {
    if (s_reduceMotion)
        return 0.0f;
    const ImGuiID key = id ^ kSaltChange;
    bool fresh = false;
    MotionState& st = motionState(key, &fresh);
    if (fresh) {
        // First sight: adopt the value silently. Flashing every row the instant
        // a panel opens would be noise, not signal.
        st.reference = value;
        st.seeded = true;
        st.value = 0.0f;
        st.lastFrame = ImGui::GetFrameCount();
        return 0.0f;
    }
    if (st.reference != value) {
        st.reference = value;
        Kick(key, 1.0f);
    }
    return Kicked(key, decaySec);
}

// ── Generic item FX ─────────────────────────────────────────────────────────

void ItemFx(int flags, float rounding) {
    ItemFxAt(ImGui::GetItemID(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
             flags, rounding, ImGui::IsItemHovered(), ImGui::IsItemActivated(),
             ImGui::IsItemActive());
}

void ItemFxAt(ImGuiID id, const ImVec2& mn, const ImVec2& mx, int flags,
              float rounding, bool hovered, bool pressed, bool active) {
    if (s_reduceMotion || flags == ItemFx_None)
        return;
    if (id == 0)
        return;
    if (mx.x - mn.x < 1.0f || mx.y - mn.y < 1.0f)
        return;
    if (rounding < 0.0f)
        rounding = ImGui::GetStyle().FrameRounding;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Hover wash — a whisper of the text colour, eased both ways. Composited
    // OVER whatever the widget drew, so it works on a button, a row or an image.
    if (flags & ItemFx_Hover) {
        const float t = Anim01(id ^ kSaltHover, hovered ? 1.0f : 0.0f, 0.12f);
        if (t > 0.01f) {
            const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            dl->AddRectFilled(mn, mx,
                              ImGui::GetColorU32(withAlpha(text, 0.055f * t)),
                              rounding);
        }
    }

    // Press ripple — spreads from where the mouse actually went down, clipped to
    // the item, and outlives the click so a quick tap still reads.
    if (flags & ItemFx_Press) {
        const ImGuiID key = id ^ kSaltPress;
        MotionState& st = motionState(key);
        st.seeded = true;
        if (pressed) {
            ImVec2 origin = ImGui::GetIO().MousePos;
            // Keyboard/gamepad activation has no meaningful mouse position:
            // ripple from the centre instead of from off-screen.
            if (origin.x < mn.x || origin.x > mx.x || origin.y < mn.y ||
                origin.y > mx.y)
                origin = ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
            st.point = origin;
            st.reference = 0.0f;
        }
        if (st.lastFrame != ImGui::GetFrameCount()) {
            st.lastFrame = ImGui::GetFrameCount();
            if (st.reference < 1.0f)
                st.reference = std::min(1.0f, st.reference + motionDt() / 0.45f);
        }
        const float t = st.reference;
        if (t > 0.0f && t < 1.0f) {
            const float dx = std::max(st.point.x - mn.x, mx.x - st.point.x);
            const float dy = std::max(st.point.y - mn.y, mx.y - st.point.y);
            const float maxRadius = std::sqrt(dx * dx + dy * dy);
            const float radius = maxRadius * EaseOutCubic(t);
            const float alpha = 0.28f * (1.0f - t) * (1.0f - t);
            dl->PushClipRect(mn, mx, true);
            dl->AddCircleFilled(st.point, radius,
                                ImGui::GetColorU32(
                                    withAlpha(g_StyleColors.accent, alpha)),
                                32);
            dl->PopClipRect();
        }
    }

    // Focus ring — springs outward from the item's edge while it's being driven.
    if (flags & ItemFx_Focus) {
        const float t = Spring(id ^ kSaltFocus, active ? 1.0f : 0.0f, 5.0f, 0.6f);
        if (t > 0.01f) {
            const float grow = 2.0f * Scale() * t;
            dl->AddRect(ImVec2(mn.x - grow, mn.y - grow),
                        ImVec2(mx.x + grow, mx.y + grow),
                        ImGui::GetColorU32(
                            withAlpha(g_StyleColors.accent, 0.55f * clamp01(t))),
                        rounding + grow, 0, 1.5f * Scale());
        }
    }
}

namespace {

// Wash the last item with a decaying accent fill + edge. Shared by both flash
// entry points.
void drawItemFlash(float flash) {
    if (flash <= 0.01f)
        return;
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    if (mx.x - mn.x < 1.0f || mx.y - mn.y < 1.0f)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(mn, mx,
                      ImGui::GetColorU32(
                          withAlpha(g_StyleColors.accent, 0.18f * flash)),
                      rounding);
    dl->AddRect(mn, mx,
                ImGui::GetColorU32(withAlpha(g_StyleColors.accent, 0.75f * flash)),
                rounding, 0, 1.5f * Scale());
}

} // namespace

void ItemFlash(bool trigger) {
    if (s_reduceMotion)
        return;
    const ImGuiID id = ImGui::GetItemID();
    if (id == 0)
        return;
    const ImGuiID key = id ^ kSaltChange;
    if (trigger)
        Kick(key);
    drawItemFlash(Kicked(key, 0.55f));
}

void ItemChangeFlash(float value) {
    if (s_reduceMotion)
        return;
    const ImGuiID id = ImGui::GetItemID();
    if (id == 0)
        return;

    // Driving the widget yourself is not a "change under you": adopt the value
    // silently while it is active, so finishing a drag never ends in a flash.
    if (ImGui::IsItemActive()) {
        MotionState& st = motionState(id ^ kSaltChange);
        st.reference = value;
        st.seeded = true;
        st.value = 0.0f;
        st.lastFrame = ImGui::GetFrameCount();
        return;
    }
    drawItemFlash(ChangeFlash(id, value));
}

// ── Global widget-color motion ──────────────────────────────────────────────

namespace {

// One hovered item and one active item exist at a time, so the *Hovered / *Active
// entries of the palette can be animated against them — and every stock ImGui
// widget in the app inherits the motion for free. `base` is the pristine palette;
// we always interpolate FROM it, never from last frame's interpolated value, so
// the colours can't drift.
struct StyleMotion {
    bool    valid = false;
    int     theme = -1;
    ImVec4  base[ImGuiCol_COUNT] = {};
    ImGuiID hoveredId = 0;
    ImGuiID activeId = 0;
    float   hoverT = 0.0f;
    float   activeT = 0.0f;
};
StyleMotion s_styleMotion;

} // namespace

void InvalidateStyleBaseline() { s_styleMotion.valid = false; }

void BeginFrameMotion() {
    ImGuiStyle& style = ImGui::GetStyle();
    StyleMotion& sm = s_styleMotion;

    // (Re-)snapshot the pristine palette on the first frame and after any theme
    // change. ApplyGuiTheme has just written the untouched colours at that point,
    // so this can never capture one of our own interpolated values.
    if (!sm.valid || sm.theme != g_currentTheme) {
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
            sm.base[i] = style.Colors[i];
        sm.valid = true;
        sm.theme = g_currentTheme;
        sm.hoveredId = 0;
        sm.activeId = 0;
        sm.hoverT = 0.0f;
        sm.activeT = 0.0f;
    }

    // The pairs we animate: each hovered/active entry eases out of its own
    // resting colour. Tab is deliberately hover-only — ImGuiCol_TabSelected is a
    // selection state, not a pointer state, and animating it would make the
    // focused tab flicker on every hover.
    struct Pair { ImGuiCol target; ImGuiCol from; bool press; };
    static const Pair kPairs[] = {
        { ImGuiCol_ButtonHovered,       ImGuiCol_Button,        false },
        { ImGuiCol_ButtonActive,        ImGuiCol_ButtonHovered, true  },
        { ImGuiCol_FrameBgHovered,      ImGuiCol_FrameBg,       false },
        { ImGuiCol_FrameBgActive,       ImGuiCol_FrameBgHovered,true  },
        { ImGuiCol_HeaderHovered,       ImGuiCol_Header,        false },
        { ImGuiCol_HeaderActive,        ImGuiCol_HeaderHovered, true  },
        { ImGuiCol_TabHovered,          ImGuiCol_Tab,           false },
        { ImGuiCol_ScrollbarGrabHovered,ImGuiCol_ScrollbarGrab, false },
        { ImGuiCol_ScrollbarGrabActive, ImGuiCol_ScrollbarGrabHovered, true },
        { ImGuiCol_SeparatorHovered,    ImGuiCol_Separator,     false },
        { ImGuiCol_SeparatorActive,     ImGuiCol_SeparatorHovered, true },
        { ImGuiCol_ResizeGripHovered,   ImGuiCol_ResizeGrip,    false },
        { ImGuiCol_ResizeGripActive,    ImGuiCol_ResizeGripHovered, true },
        { ImGuiCol_SliderGrabActive,    ImGuiCol_SliderGrab,    true  },
    };

    if (s_reduceMotion) {
        for (const Pair& pair : kPairs)
            style.Colors[pair.target] = sm.base[pair.target];
        return;
    }

    // NewFrame() has already moved this frame's hover into HoveredIdPreviousFrame
    // (and cleared HoveredId), so that is the id the widgets about to be submitted
    // will draw as hovered. ActiveId survives NewFrame and needs no such dance.
    const ImGuiContext* g = ImGui::GetCurrentContext();
    const ImGuiID hovered = g ? g->HoveredIdPreviousFrame : 0;
    const ImGuiID active = g ? g->ActiveId : 0;

    // Moving the pointer to a NEW item restarts its fade — so each widget eases
    // in on its own terms instead of inheriting the last one's progress.
    if (hovered != sm.hoveredId) {
        sm.hoveredId = hovered;
        sm.hoverT = 0.0f;
    }
    if (active != sm.activeId) {
        sm.activeId = active;
        sm.activeT = 0.0f;
    }
    const float dt = motionDt();
    sm.hoverT += (1.0f - sm.hoverT) * (1.0f - std::exp(-dt / 0.038f));
    sm.activeT += (1.0f - sm.activeT) * (1.0f - std::exp(-dt / 0.022f));

    // The press curve overshoots slightly past the pressed colour and settles —
    // the colour equivalent of a button being struck rather than switched.
    const float pressT = EaseOutBack(sm.activeT);
    for (const Pair& pair : kPairs) {
        const float t = pair.press ? pressT : sm.hoverT;
        ImVec4 c = lerpColor(sm.base[pair.from], sm.base[pair.target], t);
        // EaseOutBack leaves [0,1]; that is the overshoot, but a colour still has
        // to be a colour.
        c.x = clamp01(c.x);
        c.y = clamp01(c.y);
        c.z = clamp01(c.z);
        c.w = clamp01(c.w);
        style.Colors[pair.target] = c;
    }
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

// ── Shared drawing helpers ──────────────────────────────────────────────────

void DrawIconRotated(ImDrawList* dl, const char* icon, ImVec2 centre, float size,
                     float angleRad, ImU32 color) {
    ImFont* font = g_Fonts.icons;
    if (!dl || !font || !icon || !*icon)
        return;
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
    const ImVec2 pos(centre.x - sz.x * 0.5f, centre.y - sz.y * 0.5f);
    const int first = dl->VtxBuffer.Size;
    dl->AddText(font, size, pos, color, icon);
    if (dl->VtxBuffer.Size == first)
        return; // clipped out entirely

    // ImFont::RenderText TRUNCATES the draw position to whole pixels to keep
    // glyphs crisp. That is right for text that sits still and WRONG for a glyph
    // that moves: a smooth sub-pixel drift quantises to 1px steps, which is why a
    // slowly bobbing icon stutters instead of floating. Push the truncated
    // fraction back into the quad ImGui just emitted — and rotate it about
    // `centre` while we are in there (the UVs ride along, so the glyph turns
    // rather than the texture sliding under it).
    const float fracX = pos.x - IM_TRUNC(pos.x);
    const float fracY = pos.y - IM_TRUNC(pos.y);
    const bool rotate = std::fabs(angleRad) > 0.001f;
    const float s = std::sin(angleRad);
    const float c = std::cos(angleRad);
    for (int i = first; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        v.pos.x += fracX;
        v.pos.y += fracY;
        if (rotate) {
            const float x = v.pos.x - centre.x;
            const float y = v.pos.y - centre.y;
            v.pos.x = centre.x + x * c - y * s;
            v.pos.y = centre.y + x * s + y * c;
        }
    }
}

void Spinner(float radius, const ImVec4& color) {
    const float rowH = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 centre(p.x + radius, p.y + rowH * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float t = MotionTime();
    // An arc that both spins and breathes its length, so it reads as "working"
    // rather than as a rotating dash.
    const float head = t * 3.4f;
    const float span = 3.14159265f * (0.55f + 0.35f * std::sin(t * 2.3f));
    const int segments = 24;
    dl->PathClear();
    for (int i = 0; i <= segments; ++i) {
        const float a = head + span * (float(i) / float(segments));
        dl->PathLineTo(ImVec2(centre.x + std::cos(a) * radius,
                              centre.y + std::sin(a) * radius));
    }
    dl->PathStroke(ImGui::GetColorU32(color), 0,
                   std::max(1.5f * Scale(), radius * 0.30f));
    ImGui::Dummy(ImVec2(radius * 2.0f, rowH));
}

namespace {

// Shorthand for the widgets below, which all want the plain "animate in on first
// sight" form of Appear.
float springIn(ImGuiID id, float freq = 5.0f, float damp = 0.62f) {
    return Appear(id, false, freq, damp);
}

// The press/release feel shared by every clickable UiKit widget: squashes to
// `held` while the mouse is down and springs back through 1.0 when released, so
// a click ends with a small pop instead of a dead stop.
float pressScale(ImGuiID id, bool held, float squash = 0.94f) {
    return Spring(id ^ kSaltPop, held ? squash : 1.0f, 6.0f, 0.42f);
}

} // namespace

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

    // The accent bar wipes down from the top when the header first appears, so a
    // panel that has just opened assembles itself instead of blinking into place.
    const float grow = clamp01(springIn(ImGui::GetID(label) ^ kSaltPop, 6.0f, 0.7f));
    const float top = p.y + 2.0f * scale;
    const float bottom = top + (fontSize - 2.0f * scale) * grow;
    dl->AddRectFilled(ImVec2(p.x, top), ImVec2(p.x + 3.5f * scale, bottom), accent,
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
    // Suppress the Selectable's own square highlight; we paint a rounded hover
    // and selection fill ourselves so nav rows match the unified rounding.
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    const bool clicked = ImGui::Selectable("##navitem", false,
                                           ImGuiSelectableFlags_None,
                                           ImVec2(fullWidth, rowH));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(2);
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fontSize = ImGui::GetFontSize();
    const float rounding = RadiusCard();
    const ImGuiID id = ImGui::GetID(label);

    // Selection SPRINGS in (it overshoots and settles) while hover just eases —
    // picking a nav item is a decision and should land with some weight; sliding
    // the pointer across the list should not.
    const float sel = clamp01(Spring(id, selected ? 1.0f : 0.0f, 5.0f, 0.6f));
    const float hov = Anim01(id ^ kSaltHover, hovered ? 1.0f : 0.0f, 0.11f);
    // A click leaves a brief glow behind, so the row acknowledges the press even
    // when it was already the selected one.
    if (clicked)
        Kick(id ^ kSaltGlow);
    const float glow = Kicked(id ^ kSaltGlow, 0.5f);

    if (hov > 0.01f && sel < 0.99f)
        dl->AddRectFilled(p0, ImVec2(p0.x + fullWidth, p0.y + rowH),
                          ImGui::GetColorU32(
                              withAlpha(g_StyleColors.primary, 0.10f * hov)),
                          rounding);
    if (sel > 0.01f || glow > 0.01f) {
        dl->AddRectFilled(
            p0, ImVec2(p0.x + fullWidth, p0.y + rowH),
            ImGui::GetColorU32(withAlpha(g_StyleColors.primary,
                                         0.16f * sel + 0.10f * glow)),
            rounding);
        // The accent bar grows out from the row's centre rather than appearing at
        // full height — the cheapest way to make a selection feel like it moved
        // here from the last one.
        const float mid = p0.y + rowH * 0.5f;
        const float halfH = rowH * 0.32f * sel;
        dl->AddRectFilled(ImVec2(p0.x, mid - halfH),
                          ImVec2(p0.x + 3.0f * scale, mid + halfH),
                          ImGui::GetColorU32(
                              withAlpha(g_StyleColors.primary, clamp01(sel))),
                          2.0f * scale);
    }

    const ImU32 textCol = ImGui::GetColorU32(lerpColor(
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        ImGui::GetStyleColorVec4(ImGuiCol_Text), std::max(sel, hov * 0.75f)));

    // Hovering nudges the row's content toward the reader; selecting nudges it a
    // little further and holds it there.
    const float slide = (2.0f * hov + 2.0f * sel) * scale;
    const float pad = 14.0f * scale;
    const float iconSlot = 24.0f * scale;
    const float textY = p0.y + (rowH - fontSize) * 0.5f;
    if (g_Fonts.icons) {
        // The icon pops on selection (and on a re-click), scaled about its centre.
        const float iconScale = 1.0f + 0.14f * glow + 0.06f * sel;
        DrawIconRotated(dl, icon,
                        ImVec2(p0.x + pad + slide + fontSize * 0.5f,
                               p0.y + rowH * 0.5f),
                        fontSize * iconScale, 0.0f, textCol);
    }
    dl->AddText(ImVec2(p0.x + pad + iconSlot + slide, textY), textCol, label);

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
    const ImGuiID id = ImGui::GetItemID();
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        *v = !*v;
        changed = true;
        Kick(id ^ kSaltGlow); // the flick that throws the knob
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    // The knob is thrown on a SPRING, not eased: it overshoots the end of the
    // track and settles back, which is what makes a switch feel like a switch.
    // The velocity that overshoot carries is reused below as squash-and-stretch.
    float velocity = 0.0f;
    const float t = Spring(id, *v ? 1.0f : 0.0f, 4.6f, 0.55f, &velocity);
    const float tc = clamp01(t);
    const float pop = Kicked(id ^ kSaltGlow, 0.45f);
    const float hov = Anim01(id ^ kSaltHover, hovered ? 1.0f : 0.0f, 0.11f);

    // Track: colour follows the CLAMPED position (an overshooting knob must not
    // drive the fill past its own colour).
    const ImVec4 offBase = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 offHot = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
    const ImVec4 off = lerpColor(offBase, offHot, hov);
    const ImVec4 on = lerpColor(g_StyleColors.primary, g_StyleColors.primaryHover, hov);
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                      ImGui::GetColorU32(lerpColor(off, on, tc)), radius);

    const float knob = (radius - 2.5f * scale) * (held ? 0.92f : 1.0f);
    const float cx = p.x + radius + (width - 2.0f * radius) * t;
    const float cy = p.y + radius;

    // A ring blooms out of the knob on every flip — the switch's "click".
    if (pop > 0.01f)
        dl->AddCircle(ImVec2(cx, cy), knob * (1.0f + 1.1f * (1.0f - pop)),
                      ImGui::GetColorU32(withAlpha(
                          *v ? g_StyleColors.primary : g_StyleColors.secondary,
                          0.55f * pop)),
                      24, 2.0f * scale * pop);

    // Squash and stretch: the knob stretches along its direction of travel in
    // proportion to how fast it is moving, and rounds back out as it settles.
    const float stretch = std::min(std::fabs(velocity) * 0.055f, 0.35f);
    dl->AddEllipseFilled(ImVec2(cx, cy), ImVec2(knob * (1.0f + stretch),
                                                knob / (1.0f + stretch * 0.75f)),
                         IM_COL32(255, 255, 255, 255), 0.0f, 24);

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
    const bool hadText = buf[0] != '\0';

    // The magnifier takes the accent while the field is being typed in, so the
    // search reads as live rather than as decoration. It is drawn BEFORE the
    // field exists, so it animates off the focus state we recorded last frame —
    // a one-frame lag that nobody can see.
    const ImGuiID focusKey = ImGui::GetID("##searchfocus");
    const bool wasFocused = motionState(focusKey).reference > 0.5f;
    const float focus = Anim01(focusKey, wasFocused ? 1.0f : 0.0f, 0.12f);
    InlineIcon(ICON_FA_SEARCH,
               lerpColor(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                         g_StyleColors.accent, focus));

    const float clearW =
        hadText ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(
        std::max(ImGui::GetContentRegionAvail().x - clearW, 60.0f * Scale()));
    bool changed = ImGui::InputTextWithHint("##search", hint, buf, bufSize);
    const bool focused = ImGui::IsItemActive();
    motionState(focusKey).reference = focused ? 1.0f : 0.0f;

    // An accent underline that wipes out from the centre of the field as it takes
    // focus, and retracts when it loses it.
    const float active = Anim01(ImGui::GetID("##searchbar"), focused ? 1.0f : 0.0f,
                                0.16f);
    if (active > 0.01f) {
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        const float mid = (mn.x + mx.x) * 0.5f;
        const float half = (mx.x - mn.x) * 0.5f * EaseOutCubic(active);
        const float thick = 2.0f * Scale();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(mid - half, mx.y - thick), ImVec2(mid + half, mx.y),
            ImGui::GetColorU32(withAlpha(g_StyleColors.accent, active)),
            thick * 0.5f);
    }

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

// Drawn by hand rather than as a styled ImGui::Button so the whole pill can
// squash under the press and spring back — geometry a Button can't give us. The
// layout (frame height, generous horizontal padding, unified radius) is
// unchanged, so callers that measured a chip still measure the same box.
bool Chip(const char* label, bool active) {
    const float scale = Scale();
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float padX = Space(5);
    const ImVec2 size(textSize.x + padX * 2.0f, ImGui::GetFrameHeight());

    ImGui::PushID(label);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##chip", size);
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();

    const float on = clamp01(Spring(id, active ? 1.0f : 0.0f, 5.5f, 0.6f));
    const float hov = Anim01(id ^ kSaltHover, hovered ? 1.0f : 0.0f, 0.11f);
    const float squash = pressScale(id, held, 0.93f);

    // Scale about the centre so the pill compresses INTO itself under the
    // pointer and bounces back through its resting size on release.
    const ImVec2 centre(p0.x + size.x * 0.5f, p0.y + size.y * 0.5f);
    const ImVec2 half(size.x * 0.5f * squash, size.y * 0.5f * squash);
    const ImVec2 a(centre.x - half.x, centre.y - half.y);
    const ImVec2 b(centre.x + half.x, centre.y + half.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 idle = withAlpha(g_StyleColors.primary, 0.35f + 0.20f * hov);
    const ImVec4 live = lerpColor(g_StyleColors.accent, g_StyleColors.primaryHover, hov);
    const ImVec4 fill = lerpColor(idle, live, on);
    const float rounding = RadiusInner();

    // An active chip carries a soft halo, so "this mode is ON" reads across the
    // viewport without having to find the chip first.
    if (on > 0.01f) {
        const float bloom = 2.5f * scale * on;
        dl->AddRect(ImVec2(a.x - bloom, a.y - bloom), ImVec2(b.x + bloom, b.y + bloom),
                    ImGui::GetColorU32(withAlpha(g_StyleColors.accent, 0.28f * on)),
                    rounding + bloom, 0, bloom);
    }
    dl->AddRectFilled(a, b, ImGui::GetColorU32(fill), rounding);

    const ImVec4 textOff = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImU32 textCol =
        ImGui::GetColorU32(lerpColor(textOff, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), on));
    dl->AddText(ImVec2(centre.x - textSize.x * 0.5f, centre.y - textSize.y * 0.5f),
                textCol, label);
    return clicked;
}

void Badge(const char* text, const ImVec4& color) {
    const float scale = Scale();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float padX = 7.0f * scale;
    const float padY = 2.5f * scale;
    const ImVec2 pill(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);
    // Reserve a full frame-height slot and center the pill vertically inside it,
    // so a badge sits on the same baseline as the text, inputs and buttons it
    // shares a row with (instead of hugging the top of the line).
    const float rowH = ImGui::GetFrameHeight();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p(p0.x, p0.y + std::max((rowH - pill.y) * 0.5f, 0.0f));

    // A badge pops in when it first appears — and because its id is derived from
    // its own text, a badge whose text CHANGES ("3" -> "4") is a brand-new id and
    // pops again by construction. A count ticking up is exactly the kind of
    // change that should catch the eye, and it costs nothing to get here.
    const ImGuiID id = ImGui::GetID(text);
    const float s = clamp01(springIn(id ^ kSaltPop, 7.0f, 0.5f));

    const ImVec2 centre(p.x + pill.x * 0.5f, p.y + pill.y * 0.5f);
    const ImVec2 half(pill.x * 0.5f * s, pill.y * 0.5f * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(centre.x - half.x, centre.y - half.y),
                      ImVec2(centre.x + half.x, centre.y + half.y),
                      ImGui::GetColorU32(withAlpha(color, 0.22f * s)), RadiusInner());
    dl->AddText(ImVec2(centre.x - textSize.x * 0.5f, centre.y - textSize.y * 0.5f),
                ImGui::GetColorU32(withAlpha(color, s)), text);
    ImGui::Dummy(ImVec2(pill.x, rowH));
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

void EndCard() {
    ImGui::EndChild();

    // The child is submitted as an item in the PARENT once EndChild returns, so
    // its rect and hover state are available here — which is what lets a card
    // catch a soft accent edge as the pointer crosses it, without the card having
    // to know anything about its own contents.
    const ImGuiID id = ImGui::GetItemID();
    if (id == 0 || ReduceMotion())
        return;
    const float hov = Anim01(id ^ kSaltGlow, ImGui::IsItemHovered() ? 1.0f : 0.0f,
                             kMotionPanelSec);
    if (hov <= 0.01f)
        return;
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(
        mn, mx,
        ImGui::GetColorU32(withAlpha(g_StyleColors.accent, 0.30f * hov)),
        RadiusCard(), 0, 1.0f * Scale());
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

namespace {

// The shared body of every icon button. `spin` turns the glyph (the reset ↺),
// `appear` scales the whole thing in (a glyph that has just become relevant).
// Returns true on click; draws nothing when appear has collapsed to zero.
bool iconButtonCore(const char* strId, const char* icon, const char* tooltip,
                    float spin, float appear) {
    const float h = ImGui::GetFrameHeight();
    ImGui::PushID(strId);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##ib", ImVec2(h, h));
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::PopID(); // last-item state (rect/id/hover) survives the pop

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 centre(p0.x + h * 0.5f, p0.y + h * 0.5f);
    const float hov = Anim01(id ^ kSaltHover, hovered ? 1.0f : 0.0f, 0.11f);
    const float squash = pressScale(id, held, 0.86f);

    // The hover fill GROWS out of the icon rather than fading in at full size.
    if (hov > 0.01f) {
        const float half = h * 0.5f * (0.74f + 0.26f * hov);
        dl->AddRectFilled(
            ImVec2(centre.x - half, centre.y - half),
            ImVec2(centre.x + half, centre.y + half),
            ImGui::GetColorU32(withAlpha(g_StyleColors.primary,
                                         (held ? 0.30f : 0.19f) * hov)),
            RadiusInner());
    }
    // The ripple goes on top of the fill but under the glyph — the item is still
    // the InvisibleButton above, so ItemFx reads the right rect.
    ItemFx(ItemFx_Press, RadiusInner());

    const ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImU32 col = ImGui::GetColorU32(
        lerpColor(withAlpha(base, base.w * 0.82f), g_StyleColors.accent, hov));
    DrawIconRotated(dl, icon, centre,
                    ImGui::GetFontSize() * squash * clamp01(appear), spin, col);

    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

} // namespace

bool IconButton(const char* strId, const char* icon, const char* tooltip) {
    return iconButtonCore(strId, icon, tooltip, 0.0f, 1.0f);
}

// The selection is ONE pill that slides between the segments on a spring, rather
// than N buttons that swap colour. It stretches in its direction of travel and
// settles — the difference between a control that switches and one that moves.
bool SegmentedControl(const char* id, const char* const items[], int count,
                      int* current) {
    if (count <= 0)
        return false;
    constexpr int kMaxSegments = 16;
    if (count > kMaxSegments)
        count = kMaxSegments;

    const float h = ImGui::GetFrameHeight();
    const float gap = Space(2);
    const ImVec2 fp = ImGui::GetStyle().FramePadding;
    const float rounding = RadiusInner();

    // Segment widths match what N ImGui::Buttons would have measured, so callers
    // that pre-compute this control's width (the viewport gizmo bar) still agree.
    float widths[kMaxSegments];
    float totalW = 0.0f;
    for (int i = 0; i < count; ++i) {
        widths[i] = ImGui::CalcTextSize(items[i]).x + fp.x * 2.0f;
        totalW += widths[i];
    }
    totalW += gap * float(count - 1);

    ImGui::PushID(id);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Track behind the segments, so the pill has something to travel along.
    dl->AddRectFilled(p0, ImVec2(p0.x + totalW, p0.y + h),
                      ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);

    // Where each segment starts, for both the hit-boxes and the pill.
    float offsets[kMaxSegments];
    float x = 0.0f;
    for (int i = 0; i < count; ++i) {
        offsets[i] = x;
        x += widths[i] + gap;
    }

    const int selected = (*current >= 0 && *current < count) ? *current : 0;
    float velocity = 0.0f;
    const ImGuiID animId = ImGui::GetID("##seg");
    // Animate the fractional INDEX (not a screen x), so the pill never chases the
    // control when the window is scrolled, moved or resized.
    float fi = Spring(animId, float(selected), 5.2f, 0.58f, &velocity);
    fi = std::max(0.0f, std::min(fi, float(count - 1)));

    const int i0 = int(fi);
    const int i1 = std::min(i0 + 1, count - 1);
    const float frac = fi - float(i0);
    const float pillX = p0.x + offsets[i0] + (offsets[i1] - offsets[i0]) * frac;
    const float pillW = widths[i0] + (widths[i1] - widths[i0]) * frac;

    // Squash and stretch off the spring's own velocity: the pill leans into the
    // move and rounds back out as it arrives.
    const float stretch = std::min(std::fabs(velocity) * 0.045f, 0.16f);
    const float growX = pillW * stretch * 0.5f;
    const float shrinkY = h * stretch * 0.25f;
    dl->AddRectFilled(ImVec2(pillX - growX, p0.y + shrinkY),
                      ImVec2(pillX + pillW + growX, p0.y + h - shrinkY),
                      ImGui::GetColorU32(g_StyleColors.primary), rounding);

    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0.0f));
    for (int i = 0; i < count; ++i) {
        if (i > 0)
            ImGui::SameLine();
        ImGui::PushID(i);
        const ImVec2 segPos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##seg", ImVec2(widths[i], h)) && i != selected) {
            *current = i;
            changed = true;
        }
        const ImGuiID segId = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        // Unselected segments still light up under the pointer; the pill's own
        // coverage of this segment decides how the label is coloured, so the text
        // brightens as the pill arrives rather than the instant the click lands.
        const float hov = Anim01(segId ^ kSaltHover, hovered ? 1.0f : 0.0f, 0.11f);
        const float cover = clamp01(1.0f - std::fabs(fi - float(i)));
        if (hov > 0.01f && cover < 0.99f)
            dl->AddRectFilled(segPos, ImVec2(segPos.x + widths[i], segPos.y + h),
                              ImGui::GetColorU32(withAlpha(g_StyleColors.primary,
                                                           0.16f * hov * (1.0f - cover))),
                              rounding);

        const ImVec2 ts = ImGui::CalcTextSize(items[i]);
        const ImVec4 idle = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const ImU32 textCol = ImGui::GetColorU32(
            lerpColor(idle, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), cover));
        dl->AddText(ImVec2(segPos.x + (widths[i] - ts.x) * 0.5f,
                           segPos.y + (h - ts.y) * 0.5f),
                    textCol, items[i]);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

void EmptyState(const char* icon, const char* title, const char* hint) {
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(0.0f, Space(6)));

    // An empty state is the one place in the GUI with nothing happening, so it is
    // the one place that earns ambient motion: the icon drifts, and the whole
    // block rises into place the first time it is shown.
    const ImGuiID id = ImGui::GetID(title ? title : "##empty");
    const float in = clamp01(springIn(id ^ kSaltPop, 3.4f, 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * in);

    if (g_Fonts.icons && icon) {
        // The icon drawn larger through the icon font, centered and dim. It goes
        // through DrawIconRotated (not AddText) purely so the drift below lands on
        // sub-pixel positions — AddText would snap it to whole pixels and the
        // float would read as a stutter.
        const float iconSize = ImGui::GetFontSize() * 2.2f;
        const ImVec2 sz =
            g_Fonts.icons->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max((avail - sz.x) * 0.5f, 0.0f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bob = std::sin(MotionTime() * 1.5f) * 3.0f * Scale();
        const float rise = (1.0f - in) * 12.0f * Scale();
        ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        col.w *= in;
        DrawIconRotated(ImGui::GetWindowDrawList(), icon,
                        ImVec2(p.x + sz.x * 0.5f,
                               p.y + sz.y * 0.5f + bob + rise),
                        iconSize, 0.0f, ImGui::GetColorU32(col));
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
    ImGui::PopStyleVar(); // Alpha
    ImGui::Dummy(ImVec2(0.0f, Space(6)));
}

float ResetGutter() {
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
}

void PropertyRow(const char* label, float labelFraction, float reserveRight) {
    const float x0 = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered() &&
        ImGui::CalcTextSize(label).x > avail * labelFraction)
        ImGui::SetTooltip("%s", label); // truncated labels always tooltip (C9)
    ImGui::SameLine(x0 + avail * std::max(0.15f, std::min(labelFraction, 0.8f)));
    // Reserve a trailing gutter (so a following ResetGlyph lands flush) only
    // when the caller asks; otherwise fill to the edge.
    ImGui::SetNextItemWidth(reserveRight > 0.0f ? -reserveRight : -FLT_MIN);
}

namespace {

// The disclosure arrow + title of a section, drawn over the panel via the
// window draw list. `openT` (0 closed -> 1 open) turns the chevron and slides
// the title, so the header animates in lock-step with the body below it.
void drawSectionHeader(const char* label, const ImVec2& p0, float availX,
                       float openT, float hoverT) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight();
    if (hoverT > 0.01f)
        dl->AddRectFilled(
            p0, ImVec2(p0.x + availX, p0.y + h),
            ImGui::GetColorU32(withAlpha(g_StyleColors.primary, 0.10f * hoverT)),
            RadiusInner());
    const float fs = ImGui::GetFontSize();
    const ImU32 txt = ImGui::GetColorU32(ImGuiCol_Text);
    float labelX = p0.x + Space(4);
    if (g_Fonts.icons) {
        const float as = fs * 0.72f;
        // ONE chevron that rotates a quarter turn, instead of two glyphs swapping:
        // the arrow now travels between its states the same way the body does.
        DrawIconRotated(dl, ICON_FA_CHEVRON_RIGHT,
                        ImVec2(p0.x + Space(3) + as * 0.5f, p0.y + h * 0.5f), as,
                        openT * 1.5707963f,
                        ImGui::GetColorU32(g_StyleColors.accent));
        labelX = p0.x + Space(3) + as + Space(3);
    }
    // The title drifts a hair right as the section opens — a tiny cue that the
    // header and the content under it are one object.
    dl->AddText(ImVec2(labelX + openT * Space(1), p0.y + (h - fs) * 0.5f), txt,
                label);
}

// Per-section animation, keyed by the section's id: how open it is, and how tall
// its body measured last frame (which is what lets it roll shut from a known
// height rather than snapping).
struct SectionAnim {
    float openT = 0.0f;
    float bodyH = 0.0f;
    bool  bodyChild = false; // did BeginSection open the inner (clipping) child?
};
std::unordered_map<ImGuiID, SectionAnim> s_sections;
std::vector<ImGuiID> s_sectionStack;

// Push/pop the shared section-panel styling (subtle theme-tinted fill + hairline
// border, unified rounding, generous padding). Kept in one place so the open and
// collapsed exit paths pop exactly what BeginSection pushed.
void pushSectionStyle() {
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, withAlpha(text, 0.045f));
    ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(text, 0.14f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RadiusCard());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f * Scale());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Space(5), Space(3)));
}
void popSectionStyle() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace

bool BeginSection(const char* label, bool* open) {
    // The section is a real bordered child window so its WindowPadding gives the
    // body left/right padding and constrains content width (long text wraps,
    // controls never overflow the border). The header lives INSIDE the child at
    // the top so header + body read as one connected panel.
    pushSectionStyle();
    ImGui::BeginChild(label, ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    const float availX = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##secHdr", ImVec2(availX, h));
    const ImGuiID headerId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    bool isOpen = open ? *open : true;
    if (clicked)
        isOpen = !isOpen;
    if (open)
        *open = isOpen;

    SectionAnim& anim = s_sections[headerId];
    const float hoverT = Anim01(headerId ^ kSaltHover, hovered ? 1.0f : 0.0f, 0.11f);
    anim.openT = Anim01(headerId ^ kSaltPop, isOpen ? 1.0f : 0.0f, kMotionPanelSec);
    drawSectionHeader(label, p0, availX, anim.openT, hoverT);

    // Keep laying the body out while the section is CLOSING (openT still above
    // zero) — the caller only submits its content when we return true, so this is
    // the only way a collapse can be animated rather than snapped. The body is
    // clipped to a shrinking child, so those items are invisible and unhoverable
    // on the way out.
    const bool renderBody = isOpen || anim.openT > 0.002f;
    if (!renderBody) {
        anim.bodyChild = false;
        ImGui::EndChild();
        popSectionStyle();
        ImGui::Spacing();
        return false;
    }

    // ── The roll ────────────────────────────────────────────────────────────
    //
    // TWO nested children, and the pairing is the whole trick:
    //
    //   ##secClip — fixed height = measured * openT. This is the animation; it
    //               clips whatever is inside it.
    //   ##secBody — AUTO-sized. It always lays out at its natural full height,
    //               and its height is therefore ImGui's OWN number, which is what
    //               EndSection records as the measurement.
    //
    // Measuring the inner child's auto height (rather than reading the cursor)
    // is what keeps the end of the roll continuous: when openT reaches 1 the clip
    // height IS the auto height, exactly, so switching the clip child back to
    // auto-sizing at that point moves nothing. Reading the cursor instead gave a
    // number that disagreed with ImGui's by a few pixels, and the section visibly
    // jumped as it landed.
    //
    // The divider and its spacing live INSIDE the clip, so they roll away with the
    // content instead of disappearing in one frame when the section finally shuts.
    //
    // openT comes straight from Anim01, which is ALREADY an ease-out. Putting a
    // second easing curve on top of it (as this first did) is what made the motion
    // crawl at the end and then snap.
    const bool settled = ReduceMotion() || anim.openT >= 0.999f;
    const float clipH = std::max(1.0f, anim.bodyH * anim.openT);

    // The clip child is an ITEM in the section, so ImGui puts a full ItemSpacing.y
    // between the header and it. That gap is not part of the roll: it sits at full
    // size the whole way down and then vanishes in a single frame when the body
    // stops being drawn — which is exactly the small jump at the end of a close.
    // Zero the spacing around the clip child (the body supplies its own top gap,
    // and that one DOES roll), then restore it inside for the content.
    const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing.x, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##secClip", ImVec2(0.0f, settled ? 0.0f : clipH),
                      settled ? ImGuiChildFlags_AutoResizeY : ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginChild("##secBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, itemSpacing);
    anim.bodyChild = true;
    s_sectionStack.push_back(headerId);

    // A divider under the title, then the body. Wrap long text at the padded right
    // edge instead of letting it spill over the border.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    return true;
}

void EndSection() {
    ImGui::PopTextWrapPos();

    if (!s_sectionStack.empty()) {
        const ImGuiID headerId = s_sectionStack.back();
        s_sectionStack.pop_back();
        SectionAnim& anim = s_sections[headerId];
        if (anim.bodyChild) {
            ImGui::PopStyleVar(); // ItemSpacing (restored for the body content)
            ImGui::EndChild();    // ##secBody — auto-sized
            // ImGui's own height for the body, so the clip child above can land on
            // it exactly. Never overwrite a real measurement with the 1px we spend
            // on a section's very first frame.
            const float measured = ImGui::GetItemRectSize().y;
            if (measured > 1.0f)
                anim.bodyH = measured;
            ImGui::EndChild();      // ##secClip
            ImGui::PopStyleColor(); // ChildBg
            ImGui::PopStyleVar(2);  // WindowPadding, ItemSpacing (the zeroed one)
            anim.bodyChild = false;
        }
    }

    ImGui::EndChild(); // the section
    popSectionStyle();
    ImGui::Spacing();
}

bool ResetGlyph(const char* strId, bool modified) {
    const float h = ImGui::GetFrameHeight();
    const ImGuiID key = ImGui::GetID(strId);

    // The glyph SCALES in when the row first differs from its default and scales
    // back out when it stops — so "this one is changed" arrives as a movement in
    // the corner of the eye rather than as a character that was always there.
    const float appear = clamp01(Spring(key ^ kSaltPop, modified ? 1.0f : 0.0f,
                                        6.5f, 0.5f));
    if (!modified && appear < 0.02f) {
        ImGui::Dummy(ImVec2(h, h)); // keep the slot: rows must not shift
        return false;
    }

    // A click winds the ↺ through a full turn as it decays — the glyph performs
    // the reset it stands for.
    const float spin = Kicked(key ^ kSaltGlow, 0.55f);
    if (!modified) {
        // Scaling out: draw it, but it is no longer a target.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        col.w *= appear * 0.8f;
        DrawIconRotated(ImGui::GetWindowDrawList(), ICON_FA_UNDO,
                        ImVec2(p.x + h * 0.5f, p.y + h * 0.5f),
                        ImGui::GetFontSize() * appear, -spin * 6.2831853f,
                        ImGui::GetColorU32(col));
        ImGui::Dummy(ImVec2(h, h));
        return false;
    }

    const bool clicked = iconButtonCore(strId, ICON_FA_UNDO, "Reset to default",
                                        -spin * 6.2831853f, appear);
    if (clicked)
        Kick(key ^ kSaltGlow);
    return clicked;
}

void Dot(bool on) {
    const float scale = Scale();
    const float d = 7.0f * scale;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();

    // Springs out of nothing when a setting leaves its default, and shrinks away
    // when it returns — the smallest state change in the GUI still gets to move.
    // Callers push the row's id before drawing the dot, so "##dot" is unique here.
    const float t = Spring(ImGui::GetID("##dot"), on ? 1.0f : 0.0f, 7.0f, 0.45f);
    const float r = d * 0.5f * clamp01(t);
    if (r > 0.2f)
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(p.x + d * 0.5f, p.y + h * 0.5f), r,
            ImGui::GetColorU32(withAlpha(g_StyleColors.accent, clamp01(t))));
    ImGui::Dummy(ImVec2(d, h));
}

int HintToast(const char* id, const char* text, const char* const actions[],
              int actionCount) {
    int result = -1;

    // A hint arrives: it rises into place and fades up, so it reads as something
    // that just happened rather than as chrome that was always there.
    const float in = clamp01(springIn(ImGui::GetID(id) ^ kSaltPop, 4.0f, 0.7f));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.0f - in) * Space(6));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * in);

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
    ImGui::PopStyleVar(); // Alpha
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
