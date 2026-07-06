#include "Gui/Panels.h"
#include "Gui/Services.h"

#include "imgui/imgui.h"

namespace Gui {

// Performance / diagnostics: fps, the WSI frame-timing breakdown, swapchain +
// device info — folded out of the old interim debug panel.
void drawDiagnosticsPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Diagnostics, open)) {
        ImGui::End();
        return;
    }

    const FrameDiagnostics d = services.diagnostics();

    // Rolling frame-time history for a small sparkline.
    static float history[120] = {};
    static int cursor = 0;
    history[cursor] = d.frameMs;
    cursor = (cursor + 1) % IM_ARRAYSIZE(history);

    ImGui::Text("%.1f FPS  (%.2f ms)", d.fps, d.frameMs);
    ImGui::PlotLines("##frametime", history, IM_ARRAYSIZE(history), cursor, nullptr,
                     0.0f, 33.3f, ImVec2(-1.0f, 48.0f));

    ImGui::SeparatorText("Frame pacing (ms)");
    ImGui::Text("slot wait  %.2f", d.slotWaitMs);
    ImGui::Text("acquire    %.2f", d.acquireMs);
    ImGui::Text("present    %.2f", d.presentMs);
    ImGui::TextDisabled("Large slot wait = GPU/driver bound. Sum far below the "
                        "frame time = an external throttle (power/battery caps).");

    ImGui::SeparatorText("Swapchain");
    ImGui::Text("%u x %u, %u images", d.swapchainWidth, d.swapchainHeight,
                d.swapchainImages);
    ImGui::Text("Multiview views: %u", d.viewCount);
    ImGui::Text("Stereo present capable: %s", d.stereoPresentCapable ? "yes" : "no");
    ImGui::Text("Swapchain recreations: %u", d.swapchainRecreations);
    ImGui::Text("Upload ring: %.1f / %.0f MB", services.uploadRingUsedMB(),
                services.uploadRingCapacityMB());

    ImGui::SeparatorText("Device");
    ImGui::Text("GPU: %s", d.gpuName);
    ImGui::Text("Vulkan %u.%u.%u", d.apiMajor, d.apiMinor, d.apiPatch);
    ImGui::Text("Validation layers: %s", d.validation ? "on" : "off");

    ImGui::End();
}

} // namespace Gui
