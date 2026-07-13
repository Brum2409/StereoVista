#pragma once

// ============================================================================
//  Gui::UiKit  —  the one design system every StereoVista panel, tool and
//  plugin builds its UI from (UI redesign Pass 0; docs/UI_REDESIGN.md §6.1).
// ----------------------------------------------------------------------------
//  Three layers, all theme-aware (they read the live theme's semantic colors
//  and the current GUI scale, so they work across all 7 themes and 0.5–2.0x):
//
//    * TOKENS   — the spacing scale, radii and motion durations. Never
//                 hard-code a pixel value in feature code; use these.
//    * KINDS    — ObjectKind + StyleFor(kind): the ONE place an object type's
//                 icon, color and noun are defined (coherence contract C2).
//                 Consumed by the Outliner, Inspector, palette, toasts, …
//    * WIDGETS  — the shared widget set. The first five are ports of the
//                 proven widgets from the excluded GL GUI.cpp (:498-668), the
//                 fuzzy matcher is a scored/highlighting extension of the GL
//                 snapshot matcher (:7267), the rest are new for the redesign.
//
//  UiKit is pure ImGui over the project style state (imgui_sytle.h globals):
//  no Vulkan, no renderer, no Application access — safe to call from any
//  panel or plugin inside the ImGui frame.
// ============================================================================

#include "imgui/imgui.h"

#include <cstddef>
#include <string>

namespace Gui {
namespace UiKit {

// ── Tokens ──────────────────────────────────────────────────────────────────

// The live GUI scale (window-derived x user factor, 0.5–2.0). Multiply every
// hand-drawn pixel dimension by this — the widgets below already do.
float Scale();

// Spacing scale in *scaled* pixels: Space(1)=2, (2)=4, (3)=6, (4)=8, (5)=12,
// (6)=16, (7)=24 at scale 1.0. Out-of-range steps clamp.
float Space(int step);

// Corner radii: inner widgets 4 px, cards/containers 8 px (scaled).
float RadiusInner();
float RadiusCard();

// ── Motion (§15) ────────────────────────────────────────────────────────────
//
// Motion standards: micro interactions 120–150 ms, panel-level 200–250 ms.
// Every animation in the GUI runs through the primitives below, so a single
// "reduce motion" preference snaps all of them to their target instantly and
// the speed/bounce preferences retune all of them at once.
//
// The state behind every primitive is keyed by an ImGuiID (use ImGui::GetID, or
// ImGui::GetItemID after a widget) and garbage-collected when it stops being
// queried, so it is safe to animate throw-away, per-frame ids.
inline constexpr float kMotionMicroSec = 0.14f;
inline constexpr float kMotionPanelSec = 0.22f;

// Synced from Settings once per frame (GuiSystem::draw). `speed` scales every
// duration (>1 = snappier), `bounce` scales the overshoot of the springy curves
// (0 = no overshoot at all, 1 = the shipped feel).
void  SetMotion(bool reduce, float speed, float bounce);
void  SetReduceMotion(bool reduce);
bool  ReduceMotion();
float MotionBounce();

// Seconds since startup, for ambient/idle motion (a bobbing empty-state icon, a
// breathing badge). Stops advancing under reduce-motion, so anything driven by
// it holds still.
float MotionTime();

// Exponential ease toward `target`, clamped to [0,1]. The workhorse for
// hover/selection fades — no overshoot, never surprises. With reduce-motion on
// it returns the target.
float Anim01(ImGuiID id, float target, float durationSec = kMotionMicroSec);

// A real spring toward `target`: carries velocity across frames, so it
// overshoots and settles instead of easing flatly into place. This is what
// makes a knob, a pill or a pop feel physical. `freq` is roughly the
// oscillations/sec and `damp` the damping ratio (1 = critical, no overshoot;
// the live bounce preference scales the under-damping). The returned value
// deliberately LEAVES [0,1] while it overshoots — that is the bounce — so clamp
// it yourself where a value out of range would break (an alpha, a fraction).
float Spring(ImGuiID id, float target, float freq = 4.0f, float damp = 0.62f,
             float* outVelocity = nullptr);

// Springs 0 -> 1 the FIRST time `id` is seen — the entrance animation for a
// panel, a card, an overlay. (Spring() deliberately starts settled at its target,
// so it can't be used for this.) Anything that stops being drawn is forgotten, so
// closing and reopening replays the entrance; pass reset = true to replay it
// explicitly for something that never went away.
float Appear(ImGuiID id, bool reset = false, float freq = 5.0f, float damp = 0.62f);

// One-shot impulse. Kick() sets it to `strength`; Kicked() reads it and decays
// it toward 0. The click-feedback / arrival-pop / "look at me" primitive.
void  Kick(ImGuiID id, float strength = 1.0f);
float Kicked(ImGuiID id, float decaySec = 0.40f);

// Kicks `id` whenever `value` differs from what it was last frame, and returns
// the decaying flash. This is the "a value changed under me" animation — an
// undo, a gizmo drag writing back, a preset load — and it works for a value the
// user did NOT touch, which is exactly when a flash earns its keep.
float ChangeFlash(ImGuiID id, float value, float decaySec = 0.55f);

// Easing curves over t in [0,1]. EaseOutBack overshoots past 1 and settles back
// (scaled by the bounce preference); EaseOutElastic wobbles in.
float EaseOutCubic(float t);
float EaseOutBack(float t);
float EaseOutElastic(float t);

// ── Generic item FX: animates ANY ImGui widget ──────────────────────────────
//
// Call IMMEDIATELY after submitting a widget (ImGui::Button, a Selectable, a
// TreeNode, a MenuItem, …). It reads the item ImGui just drew — its rect, id and
// hover/active state — and composites the animation on top of it, so a plain
// ImGui widget gains an eased hover wash, a click ripple that spreads from the
// press point, and an accent focus ring without being rewritten. No-op under
// reduce-motion.
enum ItemFxFlags_ {
    ItemFx_None  = 0,
    ItemFx_Hover = 1 << 0, // eased wash over the item rect
    ItemFx_Press = 1 << 1, // ripple from the press point, clipped to the item
    ItemFx_Focus = 1 << 2, // accent ring while the item is active
    ItemFx_Default = ItemFx_Hover | ItemFx_Press,
    ItemFx_All = ItemFx_Hover | ItemFx_Press | ItemFx_Focus,
};
// `rounding` < 0 takes the style's frame rounding.
void ItemFx(int flags = ItemFx_Default, float rounding = -1.0f);

// The same effects for a caller that captured the item's id, rect and state
// itself. Needed whenever the FX cannot be drawn immediately after the widget —
// a custom-drawn row that submits a drag-drop source, a context menu or its own
// content in between would otherwise have ItemFx read the wrong "last item".
void ItemFxAt(ImGuiID id, const ImVec2& rectMin, const ImVec2& rectMax, int flags,
              float rounding, bool hovered, bool pressed, bool active);

// Wash the LAST item with the accent, decaying over ~half a second. Call it
// EVERY frame (it also draws the decay); pass trigger = true on the frame the
// change happened. This is the "a value changed under me" animation — an undo, a
// gizmo drag writing back, a preset load — and it is the caller's job to know
// that the change did not come from dragging this very widget, which would make
// the flash noise instead of signal.
void ItemFlash(bool trigger);

// The same thing for a call site that can hand over a plain number: detects the
// change itself, and stays quiet while the user is actively driving the widget.
void ItemChangeFlash(float value);

// ── Global widget-color motion (no call sites at all) ───────────────────────
//
// ImGui only ever has ONE hovered item and ONE active item, so the hovered/active
// entries of the style palette can be animated against them: every button,
// slider, checkbox, combo, tab, tree row, menu item, scrollbar and resize grip in
// the app then eases its hover and press tint in, with no widget rewritten and no
// call site touched. Call BeginFrameMotion() once at the top of the GUI frame
// (before any widget is submitted).
//
// The pristine palette is snapshotted the first time and re-snapshotted whenever
// the theme changes, so a theme switch never bakes an interpolated colour in.
void BeginFrameMotion();
void InvalidateStyleBaseline(); // force a re-snapshot (after a manual restyle)

// Semantic theme colors (the live theme's palette; never hard-code these).
enum class Semantic { Primary, Accent, Success, Warning, Danger, Info };
ImVec4 Color(Semantic semantic);

// ── Object kinds (contract C2: one style per kind, everywhere) ──────────────

// Append-only — persisted data and plugins may hold these values. Consumers
// must tolerate kinds they don't know (StyleFor returns a neutral style).
enum class ObjectKind {
    Model = 0,
    Mesh,
    PointCloud,
    SceneLayer, // SLPK / I3S
    Sun,
    PointLight,
    SpotLight,
    Group,
    Measurement,
    ClipPlane,
    Snapshot,
    Environment,
    Tool,
    Setting,
    Command,
    File,
    // Reserved for roadmap features (§16) — styled now so nothing scrambles later.
    BrushCluster,
    LiveCapture,
    Count
};

struct KindStyle {
    const char* icon;  // ICON_FA_* UTF-8 literal (static storage)
    ImVec4      color; // theme-adjusted (readable on dark AND light themes)
    const char* noun;  // human singular ("Point cloud", "Scene layer", …)
};

// The single source of icon + color + noun for a kind. Colors are fixed hues
// tone-mapped against the active theme's darkness each call, so the same kind
// reads consistently in every theme.
KindStyle StyleFor(ObjectKind kind);

// ── Widgets (GL ports first, then the redesign set) ─────────────────────────

// FontAwesome glyph via the dedicated icon font (reliable regardless of the
// merged-font path), cursor kept on the same line for a following label.
void InlineIcon(const char* icon, const ImVec4& color);

// The dedicated FontAwesome icon font (nullptr if it failed to load). For
// callers that need to draw an icon at an explicit size/position via the
// draw list directly — e.g. a tree row's enlarged, vertically-centered kind
// icon — where InlineIcon's fixed text-size layout doesn't fit. Prefer
// InlineIcon for normal, body-text-size inline icons.
ImFont* IconFont();

// Draw an icon-font glyph rotated about its own centre — the animated chevron
// of a section, the spinning ↺ of a reset. `angleRad` 0 = upright, positive =
// clockwise. Draws nothing when the icon font failed to load.
void DrawIconRotated(ImDrawList* dl, const char* icon, ImVec2 centre, float size,
                     float angleRad, ImU32 color);

// An indeterminate progress spinner (background work: streaming points, opening
// a scene layer). Lays out at the cursor and advances by its own size, so it sits
// inline in a status strip or a row.
void Spinner(float radius, const ImVec4& color);

// Section header: accent bar + title + thin separator.
void SectionHeader(const char* label);

// Panel title row: accent icon + title + separator.
void PanelTitle(const char* icon, const std::string& title);

// Soft rounded divider for the main menu bar (gentler than a hard Separator).
void MenuBarSeparator();

// Sidebar navigation entry (icon + label + accent pill when selected).
// Returns true when clicked.
bool NavItem(const char* icon, const char* label, bool selected);

// Modern on/off toggle switch with an eased knob. Returns true on change.
bool ToggleSwitch(const char* label, bool* v);

// "(?)" hover marker with wrapped tooltip text.
void HelpMarker(const char* desc);

// Search field: magnifier icon, hint text, and an [x] clear button when
// non-empty. Returns true when the text changed this frame.
bool SearchInput(const char* id, char* buf, size_t bufSize,
                 const char* hint = "Search...");

// Small rounded pill chip (filters/tags). `active` fills it with the accent.
// Returns true when clicked.
bool Chip(const char* label, bool active);

// Small rounded count/status badge (non-interactive), e.g. "3", "v1.7".
void Badge(const char* text, const ImVec4& color);

// Rounded card container (child region with padding). Always pair
// Begin/EndCard — EndCard must be called regardless of the return value.
bool BeginCard(const char* id, float height = 0.0f);
void EndCard();

// Tinted card for GLOBAL (non-per-object) settings shown in per-object
// contexts, with a "GLOBAL" tag in the corner (§8). Pair with EndCard().
bool BeginGlobalCard(const char* id, float height = 0.0f);

// Frameless icon button (hover fill only). Returns true when clicked.
bool IconButton(const char* strId, const char* icon,
                const char* tooltip = nullptr);

// Segmented control (exclusive buttons in a rounded group). Returns true when
// the selection changed; `current` is the in/out selected index.
bool SegmentedControl(const char* id, const char* const items[], int count,
                      int* current);

// Centered empty-state block: big dim icon, title, wrapped hint (§4 "empty
// states explain").
void EmptyState(const char* icon, const char* title, const char* hint);

// Property row prologue: draws the label in a fixed-fraction left column and
// puts the cursor in the value column with the item width set. Follow with
// exactly one widget.
//
// `reserveRight` reserves room on the value widget's right edge:
//    0 (default) — fill to the edge.
//   >0           — reserve that many (scaled) pixels, e.g. ResetGutter() so a
//                  trailing ResetGlyph lands flush instead of being clipped.
void PropertyRow(const char* label, float labelFraction = 0.38f,
                 float reserveRight = 0.0f);

// The width of a reset gutter (one frame-height button + item spacing) — pass to
// PropertyRow's reserveRight on rows that end in a ResetGlyph.
float ResetGutter();

// ── Grouped collapsible section (contract C6: header + its body read as one) ──
//
// A collapsible section whose OPEN body is visually bound to its header: the
// header is the themed rounded bar; when open, the body is indented under a
// left accent guide so the controls it holds clearly belong to it (a bare
// CollapsingHeader leaves its content floating, disconnected from the header).
//
// `open` (optional) seeds the initial state and is written back each frame so a
// Settings flag can persist it. Pair EVERY BeginSection with EndSection, but
// call EndSection ONLY when BeginSection returned true:
//     if (UiKit::BeginSection("Transform", &flag)) { ...; UiKit::EndSection(); }
bool BeginSection(const char* label, bool* open = nullptr);
void EndSection();

// Reset affordance for a value row: a dim ↺ glyph that lights up while
// `modified` and returns true when clicked (§12 resets). Draws nothing (but
// keeps layout stable) when not modified.
bool ResetGlyph(const char* strId, bool modified);

// Small accent "modified" dot (settings rows differing from defaults).
void Dot(bool on);

// Dismissible hint toast body with optional action buttons: returns the
// clicked action index, kHintDismissed for the [x], or -1. Drawn inline in
// the current window (the HintEngine of Pass 8 positions it).
inline constexpr int kHintDismissed = -2;
int HintToast(const char* id, const char* text, const char* const actions[],
              int actionCount);

// ── Fuzzy matching (palette + every local search field) ─────────────────────

// Case-insensitive subsequence match of `needle` in `haystack` (an empty
// needle matches everything, score 0). Returns false when it doesn't match;
// otherwise fills `outScore` (higher = better: word starts and runs score up,
// gaps score down) and up to `maxMatches` matched haystack indices for
// highlighting (both optional).
bool FuzzyMatch(const char* needle, const char* haystack, int* outScore,
                int* outMatches = nullptr, int maxMatches = 0,
                int* outMatchCount = nullptr);

// Render `text` with the matched characters (indices from FuzzyMatch)
// accent-highlighted.
void TextFuzzyHighlighted(const char* text, const int* matches,
                          int matchCount);

} // namespace UiKit
} // namespace Gui
