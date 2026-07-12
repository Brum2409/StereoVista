#pragma once

// ============================================================================
//  Gui::Palette — the Ctrl+K command palette & global search
//  (UI redesign Pass 4; docs/UI_REDESIGN.md §10, coherence contract C6).
// ----------------------------------------------------------------------------
//  A centered, keyboard-driven overlay that searches EVERYTHING: commands
//  (with their live shortcut + enabled state), scene objects, snapshots and
//  recent scenes — ranked by fuzzy score blended with command frecency.
//
//  Its CONTENT comes from a provider registry, not hard-wired code (C6): a
//  future pass registers a provider (the Pass-6 settings index is the next one)
//  and its rows appear with zero edits to Palette.cpp. Providers emit candidate
//  Items; the palette owns filtering, ranking, prefix scoping and presentation,
//  so search feels identical everywhere.
//
//  Every command row runs through core::CommandRegistry::run (C5) — the palette
//  never calls an action directly.
//
//  Gui layer: pure ImGui/UiKit over Gui::Services — no Vulkan, no renderer.
// ============================================================================

#include "Gui/UiKit.h"
#include "Scene/SceneItems.h"

#include <functional>
#include <string>
#include <vector>

namespace Gui {

class Services;

namespace Palette {

// Where a row came from — drives the icon/color (via UiKit::StyleFor) and the
// prefix filters ('>' commands, '#' objects, ':' settings). Append-only.
enum class Source {
    Command = 0,
    Object,
    Snapshot,
    Recent,
    Setting, // reserved: the Pass-6 settings index registers into this
    Count
};

struct Item {
    Source source = Source::Command;
    std::string title;                 // matched + displayed
    std::string subtitle;              // category / path / timestamp (dim)
    std::string keywords;              // extra match text (not displayed)
    std::string shortcut;              // live key label (commands)
    UiKit::ObjectKind kind = UiKit::ObjectKind::Command; // icon + color
    bool enabled = true;
    double boost = 0.0;                // frecency (added to the fuzzy score)

    // Enter. Required.
    std::function<void()> activate;
    // Shift+Enter (e.g. frame an object instead of selecting it). Optional.
    std::function<void()> alternate;
};

using Provider = std::function<void(Services&, std::vector<Item>&)>;

// Append a content provider (append-only registry — C6). Built-ins for
// commands / objects / snapshots / recents self-register on first use.
void registerProvider(Provider provider);

// Gather every provider's candidates (called once per open frame; the palette
// then fuzzy-filters and ranks them).
void collect(Services& services, std::vector<Item>& out);

// Draw the overlay. `open` is owned by the GuiSystem (the palette.open command
// sets it); the palette clears it on Esc / activation / click-away.
void draw(Services& services, bool* open);

} // namespace Palette
} // namespace Gui
