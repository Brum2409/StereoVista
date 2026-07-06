#include "Gui/Panels.h"
#include "Gui/Services.h"

#include "Cursors/Base/CursorManager.h" // + Sphere/Plane/Fragment + GUI cursor enums

#include "imgui/imgui.h"

#include <glm/glm.hpp>

namespace Gui {
namespace {

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

// Unified scaling controls shared by every cursor type (BaseCursor API).
void drawScaling(Cursor::BaseCursor* cursor) {
    const char* modes[] = { "Normal", "Fixed", "Constrained dynamic", "Logarithmic" };
    int mode = static_cast<int>(cursor->getScalingMode());
    if (ImGui::Combo("Scaling mode", &mode, modes, 4))
        cursor->setScalingMode(static_cast<GUI::CursorScalingMode>(mode));
    float base = cursor->getBaseSize();
    if (ImGui::SliderFloat("Base size", &base, 0.005f, 1.0f, "%.3f"))
        cursor->setBaseSize(base);
    float minDiff = cursor->getMinDiff();
    if (ImGui::SliderFloat("Min diff", &minDiff, 0.0f, 5.0f, "%.2f"))
        cursor->setMinDiff(minDiff);
    float maxDiff = cursor->getMaxDiff();
    if (ImGui::SliderFloat("Max diff", &maxDiff, 0.0f, 50.0f, "%.2f"))
        cursor->setMaxDiff(maxDiff);
}

void drawSphere(Cursor::SphereCursor* sphere) {
    glm::vec4 color = sphere->getColor();
    if (ImGui::ColorEdit4("Color", &color.x))
        sphere->setColor(color);
    float transparency = sphere->getTransparency();
    if (ImGui::SliderFloat("Transparency", &transparency, 0.0f, 1.0f, "%.2f"))
        sphere->setTransparency(transparency);
    float edge = sphere->getEdgeSoftness();
    if (ImGui::SliderFloat("Edge softness", &edge, 0.0f, 1.0f, "%.2f"))
        sphere->setEdgeSoftness(edge);
    float centre = sphere->getCenterTransparency();
    if (ImGui::SliderFloat("Centre transparency", &centre, 0.0f, 1.0f, "%.2f"))
        sphere->setCenterTransparency(centre);
    float radius = sphere->getFixedRadius();
    if (ImGui::SliderFloat("Fixed radius", &radius, 0.005f, 1.0f, "%.3f"))
        sphere->setFixedRadius(radius);

    bool inner = sphere->getShowInnerSphere();
    if (ImGui::Checkbox("Inner sphere", &inner))
        sphere->setShowInnerSphere(inner);
    if (inner) {
        glm::vec4 innerColor = sphere->getInnerSphereColor();
        if (ImGui::ColorEdit4("Inner color", &innerColor.x))
            sphere->setInnerSphereColor(innerColor);
        float factor = sphere->getInnerSphereFactor();
        if (ImGui::SliderFloat("Inner factor", &factor, 0.1f, 1.0f, "%.2f"))
            sphere->setInnerSphereFactor(factor);
    }
    ImGui::Separator();
    drawScaling(sphere);
}

void drawPlane(Cursor::PlaneCursor* plane) {
    glm::vec4 color = plane->getColor();
    if (ImGui::ColorEdit4("Color", &color.x))
        plane->setColor(color);
    float diameter = plane->getDiameter();
    if (ImGui::SliderFloat("Diameter", &diameter, 0.005f, 1.0f, "%.3f"))
        plane->setDiameter(diameter);
    ImGui::Separator();
    drawScaling(plane);
}

void drawFragment(Cursor::FragmentCursor* frag) {
    glm::vec4 outer = frag->getOuterColor();
    if (ImGui::ColorEdit4("Outer color", &outer.x))
        frag->setOuterColor(outer);
    glm::vec4 inner = frag->getInnerColor();
    if (ImGui::ColorEdit4("Inner color", &inner.x))
        frag->setInnerColor(inner);
    float outerR = frag->getBaseOuterRadius();
    if (ImGui::SliderFloat("Outer radius", &outerR, 0.005f, 0.2f, "%.3f"))
        frag->setBaseOuterRadius(outerR);
    float outerB = frag->getBaseOuterBorderThickness();
    if (ImGui::SliderFloat("Outer border", &outerB, 0.0f, 0.05f, "%.3f"))
        frag->setBaseOuterBorderThickness(outerB);
    float innerR = frag->getBaseInnerRadius();
    if (ImGui::SliderFloat("Inner radius", &innerR, 0.0f, 0.05f, "%.3f"))
        frag->setBaseInnerRadius(innerR);
    float innerB = frag->getBaseInnerBorderThickness();
    if (ImGui::SliderFloat("Inner border", &innerB, 0.0f, 0.05f, "%.3f"))
        frag->setBaseInnerBorderThickness(innerB);
}

} // namespace

// 3D cursor settings: type, appearance per type, scaling, the background
// last-depth cache (also mitigates the cursor-flicker note in docs/TODO.md A2),
// and the orbit-centre marker. Presets + the render-to-texture preview are
// deferred with the preferences task (docs/TODO.md C4/C5).
void drawCursorPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Cursor, open)) {
        ImGui::End();
        return;
    }

    Settings::Cursor& cursor = services.settings().cursor;
    Cursor::CursorManager& cm = services.cursors();

    ImGui::Checkbox("Show 3D cursor", &cursor.show);
    const char* typeNames[] = { "Sphere", "Plane", "Fragment ring" };
    ImGui::Combo("Cursor type", &cursor.type, typeNames, 3);

    // ── Background depth cache (C2) ─────────────────────────────────────────
    ImGui::SeparatorText("Background depth cache");
    bool keep = cm.isKeepLastDepthOnBackground();
    if (ImGui::Checkbox("Keep last depth over background", &keep))
        cm.setKeepLastDepthOnBackground(keep);
    helpMarker("Holds the cursor at the last valid depth instead of snapping to "
               "the OS cursor when the mouse leaves geometry (helps sparse point "
               "clouds).");
    if (keep) {
        const char* cacheModes[] = { "Indefinite", "Timed", "Distance" };
        int cacheMode = cm.getBackgroundCacheMode();
        if (ImGui::Combo("Expiry", &cacheMode, cacheModes, 3))
            cm.setBackgroundCacheMode(cacheMode);
        if (cacheMode == GUI::CURSOR_CACHE_TIMED) {
            float t = cm.getBackgroundCacheTime();
            if (ImGui::SliderFloat("Hold time (s)", &t, 0.1f, 10.0f, "%.1f"))
                cm.setBackgroundCacheTime(t);
        }
        if (cacheMode == GUI::CURSOR_CACHE_DISTANCE) {
            float d = cm.getBackgroundCacheDistance();
            if (ImGui::SliderFloat("Travel (px)", &d, 10.0f, 1000.0f, "%.0f"))
                cm.setBackgroundCacheDistance(d);
        }
    }

    // ── Orbit-centre marker (C3) ────────────────────────────────────────────
    ImGui::SeparatorText("Orbit centre marker");
    bool alwaysShow = cm.isAlwaysShowOrbitCenter();
    if (ImGui::Checkbox("Always show", &alwaysShow))
        cm.setAlwaysShowOrbitCenter(alwaysShow);
    {
        glm::vec4 markerColor = cm.getOrbitCenterColor();
        if (ImGui::ColorEdit4("Marker color", &markerColor.x))
            cm.setOrbitCenterColor(markerColor);
    }
    {
        float markerRadius = cm.getOrbitCenterSphereRadius();
        if (ImGui::SliderFloat("Marker radius", &markerRadius, 0.02f, 1.0f, "%.3f"))
            cm.setOrbitCenterSphereRadius(markerRadius);
    }

    // ── Appearance (C1) ─────────────────────────────────────────────────────
    ImGui::SeparatorText("Appearance");
    if (cursor.type == 0) {
        drawSphere(cm.getSphereCursor());
    } else if (cursor.type == 1) {
        drawPlane(cm.getPlaneCursor());
    } else {
        drawFragment(cm.getFragmentCursor());
    }

    // ── Presets / preview (C4/C5) ───────────────────────────────────────────
    ImGui::SeparatorText("Presets");
    ImGui::TextDisabled("Cursor presets + the 3D preview thumbnail return with "
                        "the preferences/persistence task (docs/TODO.md C4/C5).");

    ImGui::End();
}

} // namespace Gui
