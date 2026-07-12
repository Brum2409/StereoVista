#include "Gui/Inspector.h"

#include "Gui/Panels.h" // Gui::Windows (deep-link focus)
#include "Renderer/GpuTypes.h"
#include "Scene/Scene.h"

#include "imgui/IconsFontAwesome5.h"

#include <glm/glm.hpp>

#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// The Inspector's per-kind editor registry + the built-in editors for the
// shipped scene-object kinds (Model/Mesh, PointCloud, PointLight, Sun,
// Environment). The I3S SceneLayer editor lives in inspectors/LayerInspector.cpp
// (same builtins hook). Presentation shell: panels/InspectorPanel.cpp.
//
// Ported from the GL manipulation panels (GUI.cpp:8086-8850) onto the live
// Vulkan material/mesh/scene systems — content only, never the excluded file.
// Edits are gesture-grained single-undo steps via EditRow (Inspector.h);
// settings-bound editors (Sun/Sky) are non-undoable + resettable (contract C4).

namespace Gui {
namespace Inspector {

using Kind = scene::SceneItemRef::Kind;

namespace {

constexpr uint32_t kInvalidTexture = 0xFFFFFFFFu; // renderer::kInvalidTexture

// CollapsingHeader bound to a persisted open flag: seed once from the flag,
// read the live state back every frame so Gui::Preferences persists it (§8).
bool section(bool& persistedOpen, const char* label) {
    ImGui::SetNextItemOpen(persistedOpen, ImGuiCond_Once);
    const bool open = ImGui::CollapsingHeader(label);
    persistedOpen = open;
    return open;
}

std::string compactCount(unsigned long long n) {
    char buf[32];
    if (n >= 1000000000ull)
        std::snprintf(buf, sizeof(buf), "%.2fB", double(n) / 1e9);
    else if (n >= 1000000ull)
        std::snprintf(buf, sizeof(buf), "%.2fM", double(n) / 1e6);
    else if (n >= 1000ull)
        std::snprintf(buf, sizeof(buf), "%.1fk", double(n) / 1e3);
    else
        std::snprintf(buf, sizeof(buf), "%llu", n);
    return buf;
}

// Copy-on-click info row: value + a small copy button (QoL §15).
void infoRow(const char* label, const std::string& value) {
    UiKit::PropertyRow(label);
    ImGui::TextUnformatted(value.c_str());
    ImGui::SameLine();
    ImGui::PushID(label);
    if (UiKit::IconButton("copy", ICON_FA_COPY, "Copy"))
        ImGui::SetClipboardText(value.c_str());
    ImGui::PopID();
}

// ── Material target expansion (per (modelId, meshIndex)) ─────────────────────
// A material edit spreads across every mesh of every selected model; a Mesh ref
// targets just its own mesh. Materials are shared by index, so editing one that
// several meshes share affects all of them — the GL behaviour, kept.
std::vector<EditHandle> materialHandles(EditContext& ctx) {
    std::vector<EditHandle> out;
    scene::Scene& scene = ctx.services.scene();
    for (const scene::SceneItemRef& r : ctx.refs) {
        const int mi = scene.modelIndexOf(r.id);
        if (mi < 0)
            continue;
        const scene::Model& m = scene.models[size_t(mi)];
        if (r.kind == Kind::Mesh) {
            if (r.sub >= 0 && r.sub < int(m.meshes.size()))
                out.push_back(EditHandle{ r.id, r.sub });
        } else {
            for (int j = 0; j < int(m.meshes.size()); ++j)
                out.push_back(EditHandle{ r.id, j });
        }
    }
    return out;
}

// Detect a texture slot from a filename suffix (§8 texture drag-apply):
// 0 albedo (default) · 1 normal · 2 metallic · 3 roughness · 4 ao.
int slotFromFilename(const std::string& path) {
    std::string n = path;
    for (char& c : n)
        c = char(std::tolower((unsigned char)c));
    auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    if (has("_normal") || has("_nrm") || has("normalmap"))
        return 1;
    if (has("_metal") || has("metallic") || has("_mtl"))
        return 2;
    if (has("_rough") || has("roughness") || has("_rgh"))
        return 3;
    if (has("_ao") || has("occlusion") || has("ambientocclusion"))
        return 4;
    return 0;
}

const char* slotName(int slot) {
    switch (slot) {
    case 1: return "Normal";
    case 2: return "Metallic";
    case 3: return "Roughness";
    case 4: return "Ambient occlusion";
    default: return "Albedo";
    }
}

// ── Shared sections ──────────────────────────────────────────────────────────

// Transform section for kinds that carry a position/rotation(Deg)/scale triple.
// posPtr/rotPtr/sclPtr resolve the three live glm::vec3 fields from a handle.
template <class PosF, class RotF, class SclF>
void drawTransformSection(EditContext& ctx, bool& open, PosF posPtr, RotF rotPtr,
                          SclF sclPtr) {
    if (!section(open, "Transform"))
        return;
    Services* svc = &ctx.services;
    std::function<void()> resync = [svc]() { svc->scene().computeWorldBounds(); };
    const std::vector<EditHandle> handles = ctx.objectHandles();

    EditRow<glm::vec3>(ctx, "Position", "pos", handles, posPtr,
                       [](glm::vec3& v) { return ImGui::DragFloat3("##v", &v.x, 0.1f); },
                       resync);
    EditRow<glm::vec3>(ctx, "Rotation", "rot", handles, rotPtr,
                       [](glm::vec3& v) {
                           return ImGui::DragFloat3("##v", &v.x, 1.0f, -360.0f, 360.0f,
                                                    "%.0f\xC2\xB0");
                       },
                       resync);
    EditRow<glm::vec3>(ctx, "Scale", "scl", handles, sclPtr,
                       [](glm::vec3& v) {
                           return ImGui::DragFloat3("##v", &v.x, 0.01f, 0.0001f, 10000.0f);
                       },
                       resync);

    // Undoable reset-to-identity for the whole triple.
    struct Xf { glm::vec3 p, r, s; };
    bool modified = false;
    for (const EditHandle& h : handles) {
        glm::vec3* p = posPtr(h);
        glm::vec3* r = rotPtr(h);
        glm::vec3* s = sclPtr(h);
        if ((p && *p != glm::vec3(0.0f)) || (r && *r != glm::vec3(0.0f)) ||
            (s && *s != glm::vec3(1.0f))) {
            modified = true;
            break;
        }
    }
    ImGui::SameLine();
    SectionReset<Xf>(
        ctx, "rstXf", "Reset transform", modified, handles,
        [posPtr, rotPtr, sclPtr](const EditHandle& h) {
            Xf x{};
            if (glm::vec3* p = posPtr(h)) x.p = *p;
            if (glm::vec3* r = rotPtr(h)) x.r = *r;
            if (glm::vec3* s = sclPtr(h)) x.s = *s;
            return x;
        },
        [posPtr, rotPtr, sclPtr](const EditHandle& h, const Xf& x) {
            if (glm::vec3* p = posPtr(h)) *p = x.p;
            if (glm::vec3* r = rotPtr(h)) *r = x.r;
            if (glm::vec3* s = sclPtr(h)) *s = x.s;
        },
        Xf{ glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f) }, resync);
}

// Gizmo quick controls (mirrors the 1/2/3 hotkeys; GL DrawTransformGizmoControls).
void drawGizmoControls(Services& services) {
    bool on = services.gizmoEnabled();
    if (ImGui::Checkbox("Show gizmo", &on))
        services.setGizmoEnabled(on);
    if (!on)
        return;
    int mode = services.gizmoMode();
    const char* modes[] = { "Move", "Rotate", "Scale" };
    if (UiKit::SegmentedControl("gizmoMode", modes, 3, &mode))
        services.setGizmoMode(mode);
    bool local = services.gizmoLocalSpace();
    if (ImGui::Checkbox("Local", &local))
        services.setGizmoLocalSpace(local);
    ImGui::SameLine();
    bool snap = services.gizmoSnap();
    if (ImGui::Checkbox("Snap (Shift)", &snap))
        services.setGizmoSnap(snap);
}

// Material section over the selection's material targets.
void drawMaterialSection(EditContext& ctx, bool& open) {
    const std::vector<EditHandle> targets = materialHandles(ctx);
    if (targets.empty())
        return;
    if (!section(open, "Material"))
        return;
    Services* svc = &ctx.services;
    auto baseColor = [svc](const EditHandle& h) -> glm::vec4* {
        const int mi = svc->scene().modelIndexOf(h.id);
        if (mi < 0) return nullptr;
        renderer::gpu::MaterialData* m = svc->materialForMesh(mi, h.sub);
        return m ? &m->baseColor : nullptr;
    };
    auto metallic = [svc](const EditHandle& h) -> float* {
        const int mi = svc->scene().modelIndexOf(h.id);
        if (mi < 0) return nullptr;
        renderer::gpu::MaterialData* m = svc->materialForMesh(mi, h.sub);
        return m ? &m->metallic : nullptr;
    };
    auto roughness = [svc](const EditHandle& h) -> float* {
        const int mi = svc->scene().modelIndexOf(h.id);
        if (mi < 0) return nullptr;
        renderer::gpu::MaterialData* m = svc->materialForMesh(mi, h.sub);
        return m ? &m->roughness : nullptr;
    };
    auto emissive = [svc](const EditHandle& h) -> float* {
        const int mi = svc->scene().modelIndexOf(h.id);
        if (mi < 0) return nullptr;
        renderer::gpu::MaterialData* m = svc->materialForMesh(mi, h.sub);
        return m ? &m->emissive : nullptr;
    };
    auto normalScale = [svc](const EditHandle& h) -> float* {
        const int mi = svc->scene().modelIndexOf(h.id);
        if (mi < 0) return nullptr;
        renderer::gpu::MaterialData* m = svc->materialForMesh(mi, h.sub);
        return m ? &m->normalScale : nullptr;
    };

    EditRow<glm::vec4>(ctx, "Base color", "basecol", targets, baseColor,
                       [](glm::vec4& v) { return ImGui::ColorEdit3("##v", &v.x); });
    EditRow<float>(ctx, "Metallic", "metal", targets, metallic,
                   [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 1.0f); });
    EditRow<float>(ctx, "Roughness", "rough", targets, roughness,
                   [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 1.0f); });
    EditRow<float>(ctx, "Emissive", "emis", targets, emissive,
                   [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 8.0f); });
    EditRow<float>(ctx, "Normal scale", "nrm", targets, normalScale,
                   [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 2.0f); });

    if (targets.size() > 1)
        ImGui::TextDisabled("Materials are shared by index; editing one affects "
                            "every mesh that uses it.");
}

// Textures section: shows assigned slots and applies textures (load-by-dialog
// with filename-suffix slot detection, §8). Assignment routes through the app
// (Vulkan upload); clearing a slot is a plain index write.
void drawTexturesSection(EditContext& ctx, bool& open) {
    if (!section(open, "Textures"))
        return;
    Services& services = ctx.services;
    scene::Scene& scene = services.scene();

    const scene::SceneItemRef prim = ctx.primary();
    const int model = scene.modelIndexOf(prim.id);
    if (model < 0) {
        ImGui::TextDisabled("(no material)");
        return;
    }
    const int mesh = (prim.kind == Kind::Mesh) ? prim.sub : -1; // -1 = all meshes
    renderer::gpu::MaterialData* mat =
        services.materialForMesh(model, mesh >= 0 ? mesh : 0);
    if (!mat) {
        ImGui::TextDisabled("(no material)");
        return;
    }

    const uint32_t slotTex[5] = { mat->albedoTexture, mat->normalTexture,
                                  mat->metallicTexture, mat->roughnessTexture,
                                  mat->aoTexture };
    for (int slot = 0; slot < 5; ++slot) {
        ImGui::PushID(slot);
        UiKit::PropertyRow(slotName(slot));
        const bool assigned = slotTex[slot] != kInvalidTexture;
        if (assigned) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "#%u", slotTex[slot]);
            UiKit::Badge(buf, UiKit::Color(UiKit::Semantic::Success));
        } else {
            ImGui::TextDisabled("none");
        }
        ImGui::SameLine();
        if (UiKit::IconButton("load", ICON_FA_FOLDER_OPEN, "Load a texture for this slot")) {
            const std::string path = services.pickTextureFile();
            if (!path.empty())
                services.applyMaterialTexture(model, mesh, slot, path);
        }
        if (assigned) {
            ImGui::SameLine();
            if (UiKit::IconButton("clear", ICON_FA_TIMES, "Clear this slot")) {
                // Plain index write across every selected material target.
                for (const EditHandle& h : materialHandles(ctx)) {
                    const int mi = scene.modelIndexOf(h.id);
                    if (mi < 0) continue;
                    if (renderer::gpu::MaterialData* m = services.materialForMesh(mi, h.sub)) {
                        switch (slot) {
                        case 0: m->albedoTexture = kInvalidTexture; break;
                        case 1: m->normalTexture = kInvalidTexture; break;
                        case 2: m->metallicTexture = kInvalidTexture; break;
                        case 3: m->roughnessTexture = kInvalidTexture; break;
                        case 4: m->aoTexture = kInvalidTexture; break;
                        default: break;
                        }
                    }
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Load texture (auto slot)\xE2\x80\xA6", ImVec2(-FLT_MIN, 0.0f))) {
        const std::string path = services.pickTextureFile();
        if (!path.empty()) {
            const int slot = slotFromFilename(path);
            services.applyMaterialTexture(model, mesh, slot, path);
            services.toast(std::string("Applied to ") + slotName(slot) + " (by filename)");
        }
    }
    ImGui::TextDisabled("Names ending _normal / _rough / _metal / _ao pick their slot.");
}

// ── Built-in editors ─────────────────────────────────────────────────────────

void modelEditor(EditContext& ctx) {
    Services* svc = &ctx.services;
    Settings::Ui::Inspector& s = ctx.services.settings().ui.inspector;

    auto pos = [svc](const EditHandle& h) -> glm::vec3* {
        const int i = svc->scene().modelIndexOf(h.id);
        return i >= 0 ? &svc->scene().models[size_t(i)].position : nullptr;
    };
    auto rot = [svc](const EditHandle& h) -> glm::vec3* {
        const int i = svc->scene().modelIndexOf(h.id);
        return i >= 0 ? &svc->scene().models[size_t(i)].rotationDeg : nullptr;
    };
    auto scl = [svc](const EditHandle& h) -> glm::vec3* {
        const int i = svc->scene().modelIndexOf(h.id);
        return i >= 0 ? &svc->scene().models[size_t(i)].scale : nullptr;
    };

    // Mesh selection: the model has no transform to drill into per-mesh, so a
    // Mesh ref shows Material + Textures only; a Model ref gets the full stack.
    if (ctx.kind == Kind::Model) {
        drawTransformSection(ctx, s.transform, pos, rot, scl);
        drawGizmoControls(ctx.services);
    }
    drawMaterialSection(ctx, s.material);
    drawTexturesSection(ctx, s.textures);

    // Info (single-selection only; a per-object read-out).
    if (!ctx.multi() && section(s.info, "Info")) {
        const scene::SceneItemRef prim = ctx.primary();
        const int i = svc->scene().modelIndexOf(prim.id);
        if (i >= 0) {
            const scene::Model& m = svc->scene().models[size_t(i)];
            infoRow("Meshes", std::to_string(m.meshes.size()));
            if (!m.primitiveType.empty())
                infoRow("Primitive", m.primitiveType);
            if (!m.sourcePath.empty())
                infoRow("Source", m.sourcePath);
        }
    }
}

void pointCloudEditor(EditContext& ctx) {
    Services* svc = &ctx.services;
    Settings::Ui::Inspector& s = ctx.services.settings().ui.inspector;
    scene::Scene& scene = ctx.services.scene();

    auto pos = [svc](const EditHandle& h) -> glm::vec3* {
        const int i = svc->scene().pointCloudIndexOf(h.id);
        return i >= 0 ? &svc->scene().pointClouds[size_t(i)].position : nullptr;
    };
    auto rot = [svc](const EditHandle& h) -> glm::vec3* {
        const int i = svc->scene().pointCloudIndexOf(h.id);
        return i >= 0 ? &svc->scene().pointClouds[size_t(i)].rotation : nullptr;
    };
    auto scl = [svc](const EditHandle& h) -> glm::vec3* {
        const int i = svc->scene().pointCloudIndexOf(h.id);
        return i >= 0 ? &svc->scene().pointClouds[size_t(i)].scale : nullptr;
    };
    drawTransformSection(ctx, s.transform, pos, rot, scl);
    drawGizmoControls(ctx.services);

    // Display: base point size (per-object, undoable).
    if (section(s.display, "Display")) {
        auto pointSize = [svc](const EditHandle& h) -> float* {
            const int i = svc->scene().pointCloudIndexOf(h.id);
            return i >= 0 ? &svc->scene().pointClouds[size_t(i)].basePointSize : nullptr;
        };
        EditRow<float>(ctx, "Point size", "psz", ctx.objectHandles(), pointSize,
                       [](float& v) { return ImGui::SliderFloat("##v", &v, 1.0f, 10.0f, "%.1f"); });
    }

    // Info (single-selection).
    const scene::SceneItemRef prim = ctx.primary();
    const int pi = scene.pointCloudIndexOf(prim.id);
    if (!ctx.multi() && pi >= 0 && section(s.info, "Info")) {
        const Engine::PointCloud& pc = scene.pointClouds[size_t(pi)];
        infoRow("Points", compactCount(pc.totalPointCount));
        infoRow("Batches", std::to_string(pc.numBatches));
        infoRow("VRAM", [&] {
            char b[32];
            std::snprintf(b, sizeof(b), "%.1f MB", svc->pointCloudVramMB(size_t(pi)));
            return std::string(b);
        }());
        if (pc.hasBounds()) {
            const glm::vec3 ext = pc.boundsMax - pc.boundsMin;
            char b[64];
            std::snprintf(b, sizeof(b), "%.2f x %.2f x %.2f m", ext.x, ext.y, ext.z);
            infoRow("Extent", b);
        }
        if (!pc.filePath.empty())
            infoRow("Source", pc.filePath);
    }

    // Export (single-selection; routes through the app-layer exporter).
    if (!ctx.multi() && pi >= 0 && section(s.exportSection, "Export")) {
        static int format = 0; // 0 XYZ · 1 PCB · 2 HDF5 · 3 PLY
        static bool applyTransform = true;
        static bool plyBinary = true;
        const char* fmts[] = { "XYZ", "Binary (.pcb)", "HDF5", "PLY" };
        for (int f = 0; f < 4; ++f) {
            if (f) ImGui::SameLine();
            ImGui::RadioButton(fmts[f], &format, f);
        }
        if (format == 3)
            ImGui::Checkbox("Binary PLY", &plyBinary);
        ImGui::Checkbox("Apply transform", &applyTransform);
        UiKit::HelpMarker("On: bake position/rotation/scale into the exported "
                          "coordinates. Off: raw local-space points.");
        if (ImGui::Button("Export point cloud\xE2\x80\xA6", ImVec2(-FLT_MIN, 0.0f)))
            svc->exportPointCloud(size_t(pi), format, applyTransform, plyBinary);
    }
}

void pointLightEditor(EditContext& ctx) {
    Services* svc = &ctx.services;
    Settings::Ui::Inspector& s = ctx.services.settings().ui.inspector;
    const std::vector<EditHandle> handles = ctx.objectHandles();

    auto pos = [svc](const EditHandle& h) -> glm::vec3* {
        const int i = svc->scene().lightIndexOf(h.id);
        return i >= 0 ? &svc->scene().pointLights[size_t(i)].position : nullptr;
    };
    if (section(s.transform, "Transform")) {
        Services* s2 = svc;
        std::function<void()> resync = [s2]() { s2->scene().computeWorldBounds(); };
        EditRow<glm::vec3>(ctx, "Position", "pos", handles, pos,
                           [](glm::vec3& v) { return ImGui::DragFloat3("##v", &v.x, 0.1f); },
                           resync);
        drawGizmoControls(ctx.services);
    }

    if (section(s.lightProps, "Light")) {
        auto color = [svc](const EditHandle& h) -> glm::vec3* {
            const int i = svc->scene().lightIndexOf(h.id);
            return i >= 0 ? &svc->scene().pointLights[size_t(i)].color : nullptr;
        };
        auto intensity = [svc](const EditHandle& h) -> float* {
            const int i = svc->scene().lightIndexOf(h.id);
            return i >= 0 ? &svc->scene().pointLights[size_t(i)].intensity : nullptr;
        };
        auto radius = [svc](const EditHandle& h) -> float* {
            const int i = svc->scene().lightIndexOf(h.id);
            return i >= 0 ? &svc->scene().pointLights[size_t(i)].radius : nullptr;
        };
        auto linear = [svc](const EditHandle& h) -> float* {
            const int i = svc->scene().lightIndexOf(h.id);
            return i >= 0 ? &svc->scene().pointLights[size_t(i)].attenLinear : nullptr;
        };
        auto quad = [svc](const EditHandle& h) -> float* {
            const int i = svc->scene().lightIndexOf(h.id);
            return i >= 0 ? &svc->scene().pointLights[size_t(i)].attenQuadratic : nullptr;
        };
        auto shadows = [svc](const EditHandle& h) -> bool* {
            const int i = svc->scene().lightIndexOf(h.id);
            return i >= 0 ? &svc->scene().pointLights[size_t(i)].castsShadows : nullptr;
        };
        EditRow<glm::vec3>(ctx, "Color", "col", handles, color,
                           [](glm::vec3& v) { return ImGui::ColorEdit3("##v", &v.x); });
        EditRow<float>(ctx, "Intensity", "int", handles, intensity,
                       [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 10.0f, "%.2f"); });
        EditRow<float>(ctx, "Emitter radius", "rad", handles, radius,
                       [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 1.0f, "%.3f"); });
        EditRow<float>(ctx, "Atten. linear", "lin", handles, linear,
                       [](float& v) { return ImGui::DragFloat("##v", &v, 0.001f, 0.0f, 4.0f, "%.3f"); });
        EditRow<float>(ctx, "Atten. quadratic", "qua", handles, quad,
                       [](float& v) { return ImGui::DragFloat("##v", &v, 0.001f, 0.0f, 4.0f, "%.3f"); });
        EditRow<bool>(ctx, "Cast shadows", "shd", handles, shadows,
                      [](bool& v) { return ImGui::Checkbox("##v", &v); });
    }
}

// ── Settings-bound editors (non-undoable, resettable — contract C4) ──────────

void sunEditor(EditContext& ctx) {
    Settings& st = ctx.services.settings();
    renderer::SunState& sun = st.lighting.sun;
    static const renderer::SunState kDef{};

    if (section(st.ui.inspector.sun, "Sun")) {
        ImGui::Checkbox("Enabled", &sun.enabled);
        UiKit::PropertyRow("Color");
        ImGui::ColorEdit3("##suncol", &sun.color.x);
        UiKit::PropertyRow("Intensity");
        ImGui::SliderFloat("##sunint", &sun.intensity, 0.0f, 10.0f, "%.2f");
        UiKit::PropertyRow("Direction");
        glm::vec3 dir = sun.direction;
        if (ImGui::DragFloat3("##sundir", &dir.x, 0.01f, -1.0f, 1.0f, "%.2f")) {
            if (glm::length(dir) > 1e-4f)
                sun.direction = glm::normalize(dir);
        }
        UiKit::PropertyRow("Angular size");
        ImGui::SliderFloat("##sunang", &sun.angularSizeDeg, 0.1f, 10.0f, "%.2f\xC2\xB0");

        const bool modified = sun.enabled != kDef.enabled || sun.color != kDef.color ||
                              sun.intensity != kDef.intensity ||
                              sun.direction != kDef.direction ||
                              sun.angularSizeDeg != kDef.angularSizeDeg;
        ImGui::SameLine();
        if (UiKit::ResetGlyph("rstSun", modified))
            sun = kDef;
    }
}

void environmentEditor(EditContext& ctx) {
    Settings& st = ctx.services.settings();
    renderer::SkyState& sky = st.sky;
    static const renderer::SkyState kDef{};

    if (section(st.ui.inspector.sky, "Sky")) {
        const char* modes[] = { "Cubemap", "Equirectangular", "Solid color", "Gradient" };
        int mode = int(sky.mode);
        UiKit::PropertyRow("Mode");
        if (ImGui::BeginCombo("##skymode", modes[mode])) {
            for (int m = 0; m < 4; ++m) {
                bool avail = true;
                if (m == 0) avail = ctx.services.skyHasCubemap();
                if (m == 1) avail = ctx.services.skyHasEquirect();
                ImGui::BeginDisabled(!avail);
                if (ImGui::Selectable(modes[m], mode == m))
                    sky.mode = renderer::SkyMode(m);
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        UiKit::PropertyRow("Intensity");
        ImGui::SliderFloat("##skyint", &sky.intensity, 0.0f, 4.0f, "%.2f");
        if (sky.mode == renderer::SkyMode::SolidColor) {
            UiKit::PropertyRow("Color");
            ImGui::ColorEdit3("##skysolid", &sky.solidColor.x);
        } else if (sky.mode == renderer::SkyMode::Gradient) {
            UiKit::PropertyRow("Top");
            ImGui::ColorEdit3("##skytop", &sky.gradientTop.x);
            UiKit::PropertyRow("Bottom");
            ImGui::ColorEdit3("##skybot", &sky.gradientBottom.x);
        }
        const bool modified = sky.mode != kDef.mode || sky.intensity != kDef.intensity ||
                              sky.solidColor != kDef.solidColor ||
                              sky.gradientTop != kDef.gradientTop ||
                              sky.gradientBottom != kDef.gradientBottom;
        ImGui::SameLine();
        if (UiKit::ResetGlyph("rstSky", modified))
            sky = kDef;
    }
}

// ── Registry ─────────────────────────────────────────────────────────────────

struct Registry {
    Editor editors[size_t(Kind::Environment) + 1];
    bool builtinsRegistered = false;
};

Registry& registry() {
    static Registry r;
    return r;
}

void registerBuiltins() {
    builtins::registerModelEditor();
    builtins::registerPointCloudEditor();
    builtins::registerPointLightEditor();
    builtins::registerSunEditor();
    builtins::registerEnvironmentEditor();
    builtins::registerLayerEditor();
}

} // namespace

void registerEditor(Kind kind, Editor editor) {
    const size_t i = size_t(kind);
    if (i <= size_t(Kind::Environment) && editor)
        registry().editors[i] = std::move(editor);
}

const Editor* editorFor(Kind kind) {
    Registry& r = registry();
    if (!r.builtinsRegistered) {
        r.builtinsRegistered = true; // set FIRST: registerBuiltins re-enters
        registerBuiltins();
    }
    const size_t i = size_t(kind);
    if (i > size_t(Kind::Environment))
        return nullptr;
    return r.editors[i] ? &r.editors[i] : nullptr;
}

namespace builtins {

void registerModelEditor() {
    registerEditor(Kind::Model, modelEditor);
    registerEditor(Kind::Mesh, modelEditor);
}
void registerPointCloudEditor() { registerEditor(Kind::PointCloud, pointCloudEditor); }
void registerPointLightEditor() { registerEditor(Kind::PointLight, pointLightEditor); }
void registerSunEditor() { registerEditor(Kind::Sun, sunEditor); }
void registerEnvironmentEditor() { registerEditor(Kind::Environment, environmentEditor); }

} // namespace builtins

} // namespace Inspector
} // namespace Gui
