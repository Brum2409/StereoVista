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

// Motion standards (§15): micro interactions 120–150 ms, panel-level
// 200–250 ms. Anim01 below uses these; a "reduce motion" preference snaps
// every animation to its target instantly.
inline constexpr float kMotionMicroSec = 0.14f;
inline constexpr float kMotionPanelSec = 0.22f;
void SetReduceMotion(bool reduce); // synced from Settings once per frame
bool ReduceMotion();

// Per-ID eased animation value in [0,1]: eases toward `target` at the micro
// (default) or a caller-chosen rate, returns the current value. The ONLY
// animation mechanism (§15) — state is keyed by `id` (use ImGui::GetID) and
// garbage-collected when unused. With reduce-motion on it returns the target.
float Anim01(ImGuiID id, float target, float durationSec = kMotionMicroSec);

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
void PropertyRow(const char* label, float labelFraction = 0.42f);

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
