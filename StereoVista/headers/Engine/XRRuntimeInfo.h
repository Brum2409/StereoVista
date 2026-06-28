#pragma once

// Plain-data description of the system's OpenXR configuration.
//
// This header intentionally pulls in NOTHING from OpenXR or <Windows.h> — it is
// only <string>/<vector> — so it can be shared between the engine (XRSession.cpp,
// which probes the system) and the GUI layer (GUI.cpp, which renders the picker)
// without dragging the loader headers or include-order constraints into the GUI.
//
// It is also platform-neutral: on non-Windows builds the structs still exist
// (just never populated), which keeps the GUI/menu code compiling unchanged.

#include <string>
#include <vector>

namespace Engine {

// A single OpenXR runtime detected on this machine.
struct XRRuntimeInfo {
    std::string name;             // Friendly name, e.g. "SteamVR"
    std::string manifestPath;     // Full path to the runtime's *.json manifest
    bool        isActive = false; // Is the runtime currently in effect (our
                                  // per-process override, else the system default)
    bool        isSystemDefault = false; // Is the OS/registry default runtime
    bool        serviceBased = false; // Needs a background service to be running
                                      // (e.g. Monado) — a common reason XR fails
    bool        present = false;  // Manifest file exists on disk
};

// Snapshot of the system OpenXR setup. Used both to explain why a session failed
// and to offer the user a list of runtimes to switch to.
//
// Two notions of "the runtime" are tracked separately so the picker stays stable:
//   * active*        — what the loader will actually use right now: our
//                      per-process XR_RUNTIME_JSON override if set, else the
//                      system default. Drives the failure explanation.
//   * systemDefault* — the OS-wide default from the registry, regardless of any
//                      override. Stays fixed as the user switches runtimes.
struct XRDiagnostics {
    bool        loaderPresent = false; // openxr_loader.dll is loadable

    std::string activeRuntimePath;     // effective active manifest path ("" = none)
    std::string activeRuntimeName;     // effective active friendly name
    std::string activeRuntimeSource;   // "XR_RUNTIME_JSON", "HKCU registry",
                                       // "HKLM registry", or ""
    bool        activeServiceBased = false; // active runtime needs a service

    std::string systemDefaultPath;     // registry default manifest path ("" = none)
    std::string systemDefaultName;     // registry default friendly name

    // Stable list of runtimes installed on the PC: the standard install
    // locations plus the system default. Does NOT change as the user switches
    // between them, so the list never "loses" entries.
    std::vector<XRRuntimeInfo> runtimes;
};

} // namespace Engine
