#include "Gui/Outliner.h"

#include "Gui/Services.h"
#include "Scene/Scene.h"

#include <cstdio>

// The Outliner's provider registry + the built-in providers for every shipped
// object kind (see Gui/Outliner.h). Presentation lives in panels/ScenePanel.cpp.

namespace Gui {
namespace Outliner {

namespace {

struct Registry {
    std::vector<Provider> sections[size_t(Section::Count)];
    bool builtinsRegistered = false;
};

Registry& registry() {
    static Registry r;
    return r;
}

std::string compactCount(unsigned long long n) {
    char buf[32];
    if (n >= 1000000000ull)
        std::snprintf(buf, sizeof(buf), "%.1fB", double(n) / 1e9);
    else if (n >= 1000000ull)
        std::snprintf(buf, sizeof(buf), "%.1fM", double(n) / 1e6);
    else if (n >= 10000ull)
        std::snprintf(buf, sizeof(buf), "%.0fk", double(n) / 1e3);
    else
        std::snprintf(buf, sizeof(buf), "%llu", n);
    return buf;
}

using Kind = scene::SceneItemRef::Kind;

scene::SceneItemRef makeRef(Kind kind, uint64_t id, int index = -1,
                            int sub = -1) {
    scene::SceneItemRef ref;
    ref.kind = kind;
    ref.id = id;
    ref.index = index;
    ref.sub = sub;
    return ref;
}

// ── Built-in providers ───────────────────────────────────────────────────────

void provideEnvironment(Services& services, std::vector<Item>& out) {
    // Sun: a selectable object whose eye is the (resettable, non-undoable)
    // sun-enabled setting.
    {
        Item sun;
        sun.ref = makeRef(Kind::Sun, 0);
        sun.kind = UiKit::ObjectKind::Sun;
        sun.name = "Sun";
        sun.visible = services.settings().lighting.sun.enabled;
        sun.canLock = sun.canRename = sun.canDelete = sun.canDuplicate = false;
        sun.canGroup = sun.canFrame = false;
        out.push_back(std::move(sun));
    }
    // Sky / environment: selectable; its "visibility" is a mode, not a bool.
    {
        Item sky;
        sky.ref = makeRef(Kind::Environment, 0);
        sky.kind = UiKit::ObjectKind::Environment;
        sky.name = "Sky";
        sky.canHide = sky.canLock = sky.canRename = sky.canDelete = false;
        sky.canDuplicate = sky.canGroup = sky.canFrame = false;
        out.push_back(std::move(sky));
    }
}

void provideSceneObjects(Services& services, std::vector<Item>& out) {
    scene::Scene& scene = services.scene();

    for (size_t i = 0; i < scene.models.size(); ++i) {
        const scene::Model& model = scene.models[i];
        Item item;
        item.ref = makeRef(Kind::Model, model.id, int(i));
        item.kind = UiKit::ObjectKind::Model;
        item.name = model.name.empty() ? "Model " + std::to_string(i) : model.name;
        item.groupId = model.groupId;
        item.visible = model.visible;
        item.locked = model.locked;
        item.meshCount = int(model.meshes.size());
        out.push_back(std::move(item));
    }

    for (size_t i = 0; i < scene.pointClouds.size(); ++i) {
        const Engine::PointCloud& pc = scene.pointClouds[i];
        Item item;
        item.ref = makeRef(Kind::PointCloud, pc.id, int(i));
        item.kind = UiKit::ObjectKind::PointCloud;
        item.name = pc.name.empty() ? "Point cloud " + std::to_string(i) : pc.name;
        item.groupId = pc.groupId;
        item.visible = pc.visible;
        item.locked = pc.locked;
        const PointCloudProgress progress = services.pointCloudProgress(i);
        if (progress.active) {
            char badge[16];
            std::snprintf(badge, sizeof(badge), "%d%%",
                          int(progress.fraction * 100.0f));
            item.badge = badge;
        } else if (pc.totalPointCount > 0) {
            item.badge = compactCount(pc.totalPointCount) + " pts";
        }
        out.push_back(std::move(item));
    }

    for (size_t i = 0; i < scene.i3sLayers.size(); ++i) {
        if (!scene.i3sLayers[i])
            continue;
        const scene::I3SSceneLayer& layer = *scene.i3sLayers[i];
        Item item;
        item.ref = makeRef(Kind::SceneLayer, layer.id, int(i));
        item.kind = UiKit::ObjectKind::SceneLayer;
        item.name = layer.name.empty() ? "Scene layer" : layer.name;
        item.groupId = layer.groupId;
        item.visible = layer.visible;
        item.locked = layer.locked;
        item.canDuplicate = false; // re-open the package instead
        item.badge = compactCount(layer.tree.nodes.size()) + " nodes";
        out.push_back(std::move(item));
    }

    for (size_t i = 0; i < scene.pointLights.size(); ++i) {
        const scene::PointLight& light = scene.pointLights[i];
        Item item;
        item.ref = makeRef(Kind::PointLight, light.id, int(i));
        item.kind = UiKit::ObjectKind::PointLight;
        item.name = light.name.empty() ? "Point light " + std::to_string(i + 1)
                                       : light.name;
        item.groupId = light.groupId;
        item.visible = light.visible;
        item.locked = light.locked;
        out.push_back(std::move(item));
    }
}

void provideAnnotations(Services& services, std::vector<Item>& out) {
    scene::Scene& scene = services.scene();

    for (size_t i = 0; i < scene.measurements.size(); ++i) {
        const Engine::Measurement& m = scene.measurements[i];
        Item item;
        item.ref = makeRef(Kind::Measurement, m.id, int(i));
        item.kind = UiKit::ObjectKind::Measurement;
        item.name = m.name.empty() ? "Measurement " + std::to_string(i + 1)
                                   : m.name;
        item.visible = m.visible;
        item.locked = m.locked;
        item.canGroup = false; // annotations live outside user groups
        char badge[32] = "";
        switch (m.type) {
        case Engine::Measurement::Type::Distance:
            std::snprintf(badge, sizeof(badge), "%.2f m", double(m.totalLength()));
            break;
        case Engine::Measurement::Type::Angle:
            std::snprintf(badge, sizeof(badge), "%.1f\xC2\xB0",
                          double(m.angleDegrees()));
            break;
        case Engine::Measurement::Type::Area:
            std::snprintf(badge, sizeof(badge), "%.2f m\xC2\xB2", double(m.area()));
            break;
        default:
            break;
        }
        item.badge = badge;
        out.push_back(std::move(item));
    }

    for (size_t i = 0; i < scene.clipPlanes.size(); ++i) {
        const Engine::ClipPlane& plane = scene.clipPlanes[i];
        Item item;
        item.ref = makeRef(Kind::ClipPlane, plane.id, int(i));
        item.kind = UiKit::ObjectKind::ClipPlane;
        item.name = plane.name.empty() ? "Plane " + std::to_string(i + 1)
                                       : plane.name;
        item.visible = plane.enabled; // the plane's eye IS its enabled flag
        item.locked = plane.locked;
        item.canGroup = false;
        out.push_back(std::move(item));
    }
}

void registerBuiltins() {
    registerProvider(Section::Environment, provideEnvironment);
    registerProvider(Section::SceneObjects, provideSceneObjects);
    registerProvider(Section::Annotations, provideAnnotations);
}

} // namespace

void registerProvider(Section section, Provider provider) {
    const size_t index = size_t(section);
    if (index >= size_t(Section::Count) || !provider)
        return;
    registry().sections[index].push_back(std::move(provider));
}

void collect(Services& services, Section section, std::vector<Item>& out) {
    Registry& r = registry();
    if (!r.builtinsRegistered) {
        r.builtinsRegistered = true; // set FIRST: registerBuiltins re-enters
        registerBuiltins();
    }
    const size_t index = size_t(section);
    if (index >= size_t(Section::Count))
        return;
    for (const Provider& provider : r.sections[index])
        provider(services, out);
}

} // namespace Outliner
} // namespace Gui
