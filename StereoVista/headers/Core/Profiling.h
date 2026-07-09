#pragma once

// ============================================================================
// Profiling macros — Tracy (vendored, headers/libs/tracy, pinned in the SLPK
// plan §3) behind app-owned names so call sites never include Tracy directly.
// ----------------------------------------------------------------------------
// TRACY_ENABLE is defined project-wide (both x64 configs) together with
// TRACY_ON_DEMAND: the client stays dormant (near-zero overhead, no history
// buffering) until a Tracy profiler connects, so shipping with it on is fine.
// Remove TRACY_ENABLE from the .vcxproj defines to compile it out entirely —
// these macros become no-ops.
//
// Usage: SV_ZONE_N("name") or SV_ZONE() at scope tops (CPU zones),
// SV_FRAME_MARK() once per frame at the present boundary, SV_THREAD_NAME()
// once per worker thread, SV_PLOT("name", double) for HUD-style counters.
// ============================================================================

#if defined(TRACY_ENABLE)

// Include-dir note: resolved via the existing headers/libs include dir; the
// double "tracy" is the vendored layout (headers/libs/tracy/tracy/Tracy.hpp).
#include <tracy/tracy/Tracy.hpp>

#define SV_ZONE() ZoneScoped
#define SV_ZONE_N(name) ZoneScopedN(name)
#define SV_FRAME_MARK() FrameMark
#define SV_THREAD_NAME(name) tracy::SetThreadName(name)
#define SV_PLOT(name, value) TracyPlot(name, static_cast<double>(value))

#else

#define SV_ZONE()
#define SV_ZONE_N(name)
#define SV_FRAME_MARK()
#define SV_THREAD_NAME(name)
#define SV_PLOT(name, value)

#endif
