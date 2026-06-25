#pragma once

// ============================================================================
//  PluginRegistry  —  compile-time self-registration for plugins
// ----------------------------------------------------------------------------
//  Each plugin .cpp ends with:
//
//      REGISTER_PLUGIN(MyPlugin);
//
//  This places a factory for MyPlugin into a global list at static-init time.
//  PluginManager::loadRegisteredPlugins() walks that list and instantiates one
//  of each. No central list of plugins to maintain — adding a tool is a purely
//  local change to its own files (plus listing them in the .vcxproj).
//
//  Note (MSVC): because the application is built as a single executable (not a
//  static library), object files are always linked in and their static
//  initializers run. If the plugins are ever moved into a separate static lib,
//  the linker may discard the unreferenced objects — use /WHOLEARCHIVE (or an
//  explicit reference) for that lib so the registrars survive.
// ============================================================================

#include "Plugin.h"
#include <functional>
#include <memory>
#include <vector>

namespace Plugins {

// A function that produces a fresh plugin instance.
using PluginFactory = std::function<std::unique_ptr<Plugin>()>;

// The global registry. A function-local static (Meyers singleton) so it is
// safe to use from other static initializers regardless of init order.
std::vector<PluginFactory>& pluginFactoryRegistry();

// Helper whose constructor appends a factory to the registry. One static
// instance is created per REGISTER_PLUGIN use.
struct PluginRegistrar {
    explicit PluginRegistrar(PluginFactory factory);
};

} // namespace Plugins

// Token-paste helpers: build a unique registrar name from __LINE__. The extra
// indirection is required so __LINE__ is expanded before pasting. Pasting the
// line number (not the type) lets REGISTER_PLUGIN accept a namespace-qualified
// type, e.g. REGISTER_PLUGIN(Plugins::CrosshairPlugin), without producing an
// invalid identifier.
#define PLUGIN_REGISTRY_CONCAT_(a, b) a##b
#define PLUGIN_REGISTRY_CONCAT(a, b) PLUGIN_REGISTRY_CONCAT_(a, b)

// Register a Plugin subclass so the PluginManager instantiates it at startup.
// Use in the plugin's .cpp, at file scope, e.g.
//   REGISTER_PLUGIN(Plugins::CrosshairPlugin);
#define REGISTER_PLUGIN(Type)                                                  \
    namespace {                                                                \
    const ::Plugins::PluginRegistrar                                           \
        PLUGIN_REGISTRY_CONCAT(s_pluginRegistrar_, __LINE__)(                  \
            []() -> std::unique_ptr<::Plugins::Plugin> {                       \
                return std::make_unique<Type>();                              \
            });                                                                \
    }
