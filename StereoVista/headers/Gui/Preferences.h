#pragma once

// ============================================================================
//  Gui::Preferences  —  Gui::Settings ⇄ preferences.json
//  (UI redesign Pass 0; docs/UI_REDESIGN.md §6.4).
// ----------------------------------------------------------------------------
//  Persists the whole Gui::Settings struct (and the command-frecency counters
//  that will rank the Pass-4 palette) to the cwd-relative preferences.json,
//  the same location the GL app used.
//
//    * load(): missing file or missing keys leave the passed-in values —
//      i.e. the struct's initializers — untouched (guardrail 5: behaviour-
//      preserving defaults). A GL-era preferences.json (recognized by its
//      missing "version" field) has the overlapping subset migrated
//      (camera feel, stereo, VR comfort, sun/sky, point-cloud quality, theme
//      + GUI scale); the GL-only sections (VCT/HDR/radar/SpaceMouse/…) are
//      dropped — those features' preferences return with their ports.
//    * save(): writes the v3 document. The Application calls it on exit and
//      from a debounced change check (compare snapshot() strings).
//
//  This file is the ONE place new Settings fields get serialization lines —
//  do not hand-roll json elsewhere (and never resurrect the GL
//  ApplicationPreferences blob; see Settings.h).
// ============================================================================

#include <string>

namespace core { class CommandRegistry; }

namespace Gui {

struct Settings;

namespace Preferences {

inline constexpr int kVersion = 3; // v1/v2 were GL-era formats

// Read `path` into `settings` (+ frecency into `commands` when non-null).
// Returns true when a file was read (native or migrated), false when absent
// or unparseable — values keep their defaults either way.
bool load(const std::string& path, Settings& settings,
          core::CommandRegistry* commands = nullptr);

// Serialize and write. Returns false on I/O failure.
bool save(const std::string& path, const Settings& settings,
          const core::CommandRegistry* commands = nullptr);

// The serialized document as a string — the debounced autosave compares
// snapshots to detect "something changed" without touching the disk.
std::string snapshot(const Settings& settings,
                     const core::CommandRegistry* commands = nullptr);

} // namespace Preferences
} // namespace Gui
