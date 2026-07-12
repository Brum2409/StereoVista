#include "Gui/Panels.h"
#include "Gui/Services.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace Gui {

// Point clouds: load, per-cloud visibility / VRAM / streaming progress / unload,
// plus the frame-wide render options (HQS depth window, adaptive splats,
// downsample, Morton resort). Clip planes live in their own panel.
void drawPointCloudPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::PointClouds, open)) {
        ImGui::End();
        return;
    }

    Settings::PointCloud& pc = services.settings().pointCloud;

    if (ImGui::Button("Load point cloud..."))
        services.openPointCloudDialog();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Downsample", &pc.downsample);
    pc.downsample = std::max(1, pc.downsample);
    ImGui::Checkbox("Morton resort (LAS/LAZ streaming)", &pc.mortonResort);

    ImGui::SeparatorText("Rendering");
    ImGui::Checkbox("High-Quality Shading", &pc.hqs);
    if (pc.hqs) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Depth window", &pc.hqsThreshold, 0.001f, 0.1f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);
    }
    ImGui::Checkbox("Adaptive splats", &pc.splatEnabled);
    if (pc.splatEnabled) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderInt("Max radius (px)", &pc.splatMaxRadius, 1, 8);
    }
    // Density LOD: the rasterizer thins each batch to ~N points per on-screen
    // pixel (continuous, per-batch — no popping). Works for flat clouds and
    // I3S pool pages alike; docs/POINTCLOUD_LOD.md Stage 1.
    ImGui::Checkbox("Density LOD", &pc.lodEnabled);
    if (pc.lodEnabled) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Points / pixel", &pc.lodPointsPerPixel, 0.25f, 8.0f,
                           "%.2f", ImGuiSliderFlags_Logarithmic);
        if (!pc.splatEnabled) {
            ImGui::SameLine();
            ImGui::TextDisabled("(enable splats to close thinned holes)");
        }
    }

    // Async compute (renderer-wide; point clouds are today's only compute
    // consumer, so the toggle lives with them). Greyed out when the GPU has
    // no second queue — the renderer falls back to inline compute anyway.
    {
        const FrameDiagnostics d = services.diagnostics();
        ImGui::BeginDisabled(!d.asyncComputeSupported);
        ImGui::Checkbox("Async compute", &services.settings().render.asyncCompute);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (!d.asyncComputeSupported)
            ImGui::TextDisabled("(no compute queue on this GPU)");
        else if (d.asyncComputeActive)
            ImGui::TextDisabled("(overlapping this frame)");
        else
            ImGui::TextDisabled(d.asyncComputeEnabled ? "(idle - no visible clouds)"
                                                      : "(inline fallback)");
    }

    ImGui::Text("Upload ring: %.1f / %.0f MB in flight", services.uploadRingUsedMB(),
                services.uploadRingCapacityMB());

    // The per-cloud LISTING dissolved into the Outliner in UI redesign Pass 1
    // (C10: selection/visibility/rename/delete live on the Scene panel rows).
    // What remains here is the read-only load telemetry the Outliner's badges
    // compress away (VRAM, batches, exact stream rates) — until the Pass-2
    // Inspector takes it over. No new features land in this panel.
    ImGui::SeparatorText("Load status");
    if (services.pointCloudCount() == 0) {
        ImGui::TextDisabled("(no clouds loaded - manage them in the Scene panel)");
    } else {
        ImGui::TextDisabled("Clouds are managed in the Scene panel.");
        for (size_t i = 0; i < services.pointCloudCount(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextUnformatted(services.pointCloudName(i).c_str());

            const double vram = services.pointCloudVramMB(i);
            if (vram > 0.0)
                ImGui::Text("  VRAM %.1f MB, %u batches", vram,
                            services.pointCloudBatches(i));

            const PointCloudProgress progress = services.pointCloudProgress(i);
            if (progress.active) {
                char overlay[96];
                std::snprintf(overlay, sizeof(overlay), "%u / %u pts (%.1f M/s)",
                              progress.pointsLoaded, progress.pointsTotal,
                              progress.pointsPerSecond / 1e6);
                ImGui::ProgressBar(progress.fraction, ImVec2(-80.0f, 0.0f), overlay);
                if (progress.resorting) {
                    ImGui::SameLine();
                    ImGui::TextUnformatted("resorting...");
                }
            } else {
                ImGui::Text("  %u points loaded", services.pointCloudPoints(i));
            }
            ImGui::PopID();
        }
    }

    ImGui::End();
}

} // namespace Gui
