#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Tools/MeasurementTool.h"

#include "Renderer/OverlayDrawList.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>

namespace Tools {

using renderer::OverlayDepth;
using renderer::OverlayDrawList;

// ────────────────────────────────────────────────────────────────────────────
// Interaction / data (unchanged from the GL tool, now owning its measurements)
// ────────────────────────────────────────────────────────────────────────────
void MeasurementTool::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        finishActive(); // commit (or discard) whatever was in progress
        m_previewValid = false;
    }
}

void MeasurementTool::setMode(Engine::Measurement::Type mode) {
    if (m_mode == mode) return;
    finishActive();
    m_mode = mode;
}

void MeasurementTool::addPoint(const glm::vec3& worldPos) {
    if (!m_hasActive) {
        m_active = Engine::Measurement();
        m_active.type = m_mode;
        m_active.color = nextColor;
        m_nameCounter++;
        const char* prefix =
            (m_mode == Engine::Measurement::Type::Angle) ? "Angle" :
            (m_mode == Engine::Measurement::Type::Point) ? "Point" :
            (m_mode == Engine::Measurement::Type::Area)  ? "Area"  : "Distance";
        m_active.name = std::string(prefix) + " " + std::to_string(m_nameCounter);
        m_hasActive = true;
    }

    m_active.points.push_back(worldPos);

    if (m_active.type == Engine::Measurement::Type::Point &&
        m_active.points.size() >= 1) {
        commitActive();
    } else if (m_active.type == Engine::Measurement::Type::Angle &&
               m_active.points.size() >= 3) {
        commitActive();
    }
}

void MeasurementTool::finishActive() {
    if (!m_hasActive) return;
    const size_t required =
        (m_active.type == Engine::Measurement::Type::Angle) ? 3 :
        (m_active.type == Engine::Measurement::Type::Area)  ? 3 :
        (m_active.type == Engine::Measurement::Type::Point) ? 1 : 2;
    if (m_active.points.size() >= required) commitActive();
    else cancelActive();
}

void MeasurementTool::cancelActive() {
    m_hasActive = false;
    m_active = Engine::Measurement();
}

void MeasurementTool::undoLastPoint() {
    if (!m_hasActive || m_active.points.empty()) return;
    m_active.points.pop_back();
    if (m_active.points.empty()) cancelActive();
}

void MeasurementTool::commitActive() {
    if (!m_active.points.empty())
        m_measurements.push_back(std::move(m_active));
    m_hasActive = false;
    m_active = Engine::Measurement();
}

void MeasurementTool::deleteMeasurement(int index) {
    if (index >= 0 && index < static_cast<int>(m_measurements.size()))
        m_measurements.erase(m_measurements.begin() + index);
}

void MeasurementTool::clearAll() {
    cancelActive();
    m_measurements.clear();
}

std::string MeasurementTool::formatLength(float worldLength) const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f %s", worldLength * unitScale,
                  unitSuffix.c_str());
    return buf;
}

std::string MeasurementTool::formatArea(float worldArea) const {
    char buf[64];
    // "\xC2\xB2" is the UTF-8 superscript two so the suffix reads e.g. "m²".
    std::snprintf(buf, sizeof(buf), "%.3f %s\xC2\xB2",
                  worldArea * unitScale * unitScale, unitSuffix.c_str());
    return buf;
}

bool MeasurementTool::exportToCSV(const std::string& path) const {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "[MeasurementTool] failed to open '" << path
                  << "' for writing\n";
        return false;
    }

    auto csvQuote = [](const std::string& s) {
        std::string out = "\"";
        for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
        out += "\"";
        return out;
    };

    file << "Name,Type,Summary,Unit,PointIndex,X,Y,Z\n";
    for (const auto& m : m_measurements) {
        const char* typeName =
            (m.type == Engine::Measurement::Type::Angle) ? "Angle" :
            (m.type == Engine::Measurement::Type::Point) ? "Point" :
            (m.type == Engine::Measurement::Type::Area)  ? "Area"  : "Distance";

        char summary[48] = "";
        const char* unit = "";
        std::string areaUnit;
        if (m.type == Engine::Measurement::Type::Distance) {
            std::snprintf(summary, sizeof(summary), "%.6f", m.totalLength() * unitScale);
            unit = unitSuffix.c_str();
        } else if (m.type == Engine::Measurement::Type::Angle) {
            std::snprintf(summary, sizeof(summary), "%.4f", m.angleDegrees());
            unit = "deg";
        } else if (m.type == Engine::Measurement::Type::Area) {
            std::snprintf(summary, sizeof(summary), "%.6f",
                          m.area() * unitScale * unitScale);
            areaUnit = unitSuffix + "^2";
            unit = areaUnit.c_str();
        }

        if (m.points.empty()) {
            file << csvQuote(m.name) << ',' << typeName << ',' << summary << ','
                 << csvQuote(unit) << ",,,,\n";
            continue;
        }
        for (size_t i = 0; i < m.points.size(); i++) {
            const glm::vec3& p = m.points[i];
            char coords[96];
            std::snprintf(coords, sizeof(coords), "%.6f,%.6f,%.6f", p.x, p.y, p.z);
            file << csvQuote(m.name) << ',' << typeName << ',' << summary << ','
                 << csvQuote(unit) << ',' << i << ',' << coords << '\n';
        }
    }
    return file.good();
}

// ────────────────────────────────────────────────────────────────────────────
// Overlay geometry
// ────────────────────────────────────────────────────────────────────────────
void MeasurementTool::appendTo(OverlayDrawList& list, const glm::vec3* previewPoint) {
    m_previewValid = false;
    if (m_enabled && previewPoint) {
        m_previewPoint = *previewPoint;
        m_previewValid = true;
    }

    const float markerSize = std::max(8.0f, lineWidth * 3.0f);
    const glm::vec3 activeColor = glm::mix(nextColor, glm::vec3(1.0f), 0.25f);
    const glm::vec3 previewColor = glm::mix(activeColor, glm::vec3(1.0f), 0.5f);

    // One geometry build per depth mode: Occluded (opaque, in front of geometry)
    // then Hidden (x-ray ghost, only where geometry covers it) — the overlay's
    // two depth modes replace the GL LEQUAL/GREATER two-pass.
    auto build = [&](OverlayDepth depth, float aLine, float aFill, float aMark) {
        auto seg = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
            list.line(a, b, glm::vec4(c, aLine), lineWidth, depth);
        };
        auto mark = [&](const glm::vec3& p, const glm::vec3& c) {
            list.marker(p, markerSize, glm::vec4(c, aMark), depth);
        };
        auto fan = [&](const std::vector<glm::vec3>& pts, const glm::vec3& c) {
            if (pts.size() < 3) return;
            glm::vec3 centre(0.0f);
            for (const glm::vec3& p : pts) centre += p;
            centre /= static_cast<float>(pts.size());
            std::vector<glm::vec3> tris;
            tris.reserve(pts.size() * 3);
            for (size_t i = 0; i < pts.size(); ++i) {
                tris.push_back(centre);
                tris.push_back(pts[i]);
                tris.push_back(pts[(i + 1) % pts.size()]);
            }
            list.triangles(tris.data(), tris.size(), glm::vec4(c, aFill), depth);
        };

        // Committed measurements (fills first so outlines/markers sit on top).
        for (const Engine::Measurement& m : m_measurements) {
            if (!m.visible) continue;
            const bool areaClosed =
                m.type == Engine::Measurement::Type::Area && m.points.size() >= 3;
            if (areaClosed) fan(m.points, m.color);
            for (size_t i = 1; i < m.points.size(); ++i)
                seg(m.points[i - 1], m.points[i], m.color);
            if (areaClosed) seg(m.points.back(), m.points.front(), m.color);
            for (const glm::vec3& p : m.points) mark(p, m.color);
        }

        // In-progress measurement.
        if (m_hasActive) {
            if (m_active.type == Engine::Measurement::Type::Area &&
                m_active.points.size() >= 3 && !m_previewValid)
                fan(m_active.points, activeColor);
            for (size_t i = 1; i < m_active.points.size(); ++i)
                seg(m_active.points[i - 1], m_active.points[i], activeColor);
            for (const glm::vec3& p : m_active.points) mark(p, activeColor);
        }

        // Live rubber-band to the 3D cursor.
        if (m_previewValid && m_hasActive && !m_active.points.empty()) {
            seg(m_active.points.back(), m_previewPoint, previewColor);
            if (m_active.type == Engine::Measurement::Type::Area &&
                m_active.points.size() >= 2) {
                seg(m_previewPoint, m_active.points.front(), previewColor);
                std::vector<glm::vec3> ghost = m_active.points;
                ghost.push_back(m_previewPoint);
                fan(ghost, activeColor);
            }
        }
        if (m_previewValid) {
            mark(m_previewPoint, glm::vec3(1.0f));
        } else if (m_hasActive &&
                   m_active.type == Engine::Measurement::Type::Area &&
                   m_active.points.size() >= 3) {
            seg(m_active.points.back(), m_active.points.front(), activeColor);
        }
    };

    build(OverlayDepth::Occluded, 1.0f, 0.18f, 1.0f);
    if (xRay) build(OverlayDepth::Hidden, 0.22f, 0.07f, 0.22f);
}

// ────────────────────────────────────────────────────────────────────────────
// Screen-space value labels (ImGui foreground draw list)
// ────────────────────────────────────────────────────────────────────────────
void MeasurementTool::drawLabels(const glm::mat4& viewProj,
                                 const glm::vec2& viewportPos,
                                 const glm::vec2& viewportSize) const {
    if (!showLabels) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    auto project = [&](const glm::vec3& world, ImVec2& out) -> bool {
        const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
        if (clip.w <= 1e-5f) return false; // behind the camera
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        out.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        out.y = viewportPos.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y;
        return true;
    };
    auto label = [&](const glm::vec3& world, const std::string& text, ImU32 col) {
        ImVec2 s;
        if (!project(world, s)) return;
        const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        dl->AddRectFilled(ImVec2(s.x - 3.0f, s.y - 2.0f),
                          ImVec2(s.x + ts.x + 3.0f, s.y + ts.y + 2.0f),
                          IM_COL32(0, 0, 0, 150), 3.0f);
        dl->AddText(ImVec2(s.x, s.y), col, text.c_str());
    };
    auto measure = [&](const Engine::Measurement& m) {
        if (m.points.empty()) return;
        const ImU32 col = IM_COL32(int(m.color.r * 255), int(m.color.g * 255),
                                   int(m.color.b * 255), 255);
        switch (m.type) {
        case Engine::Measurement::Type::Distance: {
            if (showSegmentLabels) {
                for (size_t i = 1; i < m.points.size(); ++i) {
                    const glm::vec3 mid = (m.points[i - 1] + m.points[i]) * 0.5f;
                    label(mid, formatLength(glm::length(m.points[i] - m.points[i - 1])),
                          col);
                }
            }
            if (m.points.size() >= 2)
                label(m.points.back(), "= " + formatLength(m.totalLength()), col);
            break;
        }
        case Engine::Measurement::Type::Angle: {
            if (m.points.size() >= 3) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", m.angleDegrees());
                label(m.points[1], buf, col);
            }
            break;
        }
        case Engine::Measurement::Type::Area: {
            if (m.points.size() >= 3)
                label(m.centroid(),
                      formatArea(m.area()) + "  P=" + formatLength(m.perimeter()), col);
            break;
        }
        case Engine::Measurement::Type::Point: {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "(%.2f, %.2f, %.2f)", m.points[0].x,
                          m.points[0].y, m.points[0].z);
            label(m.points[0], buf, col);
            break;
        }
        }
    };

    for (const Engine::Measurement& m : m_measurements)
        if (m.visible) measure(m);
    if (m_hasActive) measure(m_active);
}

} // namespace Tools
