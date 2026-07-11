#pragma once

#include "RHI/VulkanCommon.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/PointCloudGpu.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace renderer {

class MeshBuffer;
class OverlayDrawList;

// ============================================================================
// The renderer's per-frame SCENE INPUT. The application (scene + camera +
// GUI state) builds one of these each frame and hands it to
// Renderer::renderFrame — the renderer holds no scene state of its own.
// Everything here is multiview-shaped: views[] always carries kMaxViews
// entries (mono duplicates view 0) so Phase 7 stereo only changes who fills
// view 1, not the structures.
// ============================================================================

struct ViewCamera {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f }; // from the renderer::projection factories ONLY
    glm::vec3 position{ 0.0f };
};

// Per-eye cameras of one docked viewport (mono duplicates view 0), for the
// viewports beyond the primary — see FrameSubmission's multi-viewport block.
struct ViewportCameras {
    ViewCamera views[kMaxViews];
};

struct DrawItem {
    const MeshBuffer* mesh = nullptr;
    glm::mat4 model{ 1.0f };
    glm::mat3 normalMatrix{ 1.0f };
    uint32_t materialIndex = 0;
    bool castsShadows = true;
    // Per-draw linear albedo multiplier (DrawPush.tint). 1,1,1 = neutral;
    // available for per-instance colour variation / highlighting.
    glm::vec3 tint{ 1.0f };
    // Draw with the forward pass's line-mode debug pipeline (M4 inspector /
    // the global wireframe toggle). Falls back to fill silently when the
    // device lacks fillModeNonSolid. Shadows still render solid.
    bool wireframe = false;
    // Rasterize without back-face culling (forward pass dynamic cull mode).
    // The GL app never enabled GL_CULL_FACE for the scene, so imported models
    // set this for parity (open meshes / inconsistent winding keep both
    // sides); I3S draws set it from the material's doubleSided flag. The
    // shadow pipelines always cull NONE regardless.
    bool twoSided = false;
    // World-space bounding sphere; drives the sun shadow frustum fit (and
    // later CPU culling). Radius 0 = unknown, treated as a point.
    glm::vec3 worldBoundsCenter{ 0.0f };
    float worldBoundsRadius = 0.0f;
};

// A rectangle of the view-0 scene depth buffer to copy back to the CPU this
// frame (pixel coordinates, top-left origin). Results surface one frame
// later in Renderer::depthSamples() — the same one-frame staleness class as
// the GL glReadPixels picking, but without a pipeline stall.
struct DepthQueryRect {
    glm::ivec2 origin{ 0 };
    glm::ivec2 size{ 0 };
};

// Published depth-picking readback (see Renderer::depthSamples()).
// Reconstruct world positions with THIS invViewProj — it belongs to the
// frame that rendered the depth, not to the current camera.
struct DepthReadback {
    bool valid = false;
    glm::mat4 invViewProj{ 1.0f }; // view 0, clip -> world
    glm::vec3 cameraPos{ 0.0f };
    VkExtent2D extent{};
    std::vector<DepthQueryRect> rects;
    std::vector<float> depths; // rect texels back-to-back, row-major

    // Reverse-Z depth at an absolute pixel, searching all rects.
    bool sample(const glm::ivec2& pixel, float& outDepth) const {
        if (!valid)
            return false;
        size_t offset = 0;
        for (const DepthQueryRect& rect : rects) {
            const glm::ivec2 local = pixel - rect.origin;
            if (local.x >= 0 && local.y >= 0 && local.x < rect.size.x &&
                local.y < rect.size.y) {
                outDepth = depths[offset + size_t(local.y) * rect.size.x + local.x];
                return true;
            }
            offset += size_t(rect.size.x) * rect.size.y;
        }
        return false;
    }
};

// Fragment (ring) cursor drawn by mesh.frag on scene surfaces (Phase 6 port
// of the GL uber-shader cursor). Colors are LINEAR (the app converts the
// authored sRGB values once).
struct FragmentCursorState {
    bool show = false;
    glm::vec3 position{ 0.0f };
    bool valid = false;
    glm::vec4 outerColor{ 1.0f };
    glm::vec4 innerColor{ 1.0f, 1.0f, 1.0f, 0.5f };
    float outerRadius = 0.04f;
    float outerThickness = 0.005f;
    float innerRadius = 0.004f;
    float innerThickness = 0.005f;
};

struct SunState {
    bool enabled = false;
    glm::vec3 direction{ -0.408f, -0.816f, -0.408f }; // travels this way
    glm::vec3 color{ 1.0f, 0.98f, 0.95f };
    float intensity = 0.16f;
    // Apparent angular diameter in degrees; drives PCSS penumbra growth.
    // The real sun is ~0.53; the default is stylized for readable softness.
    float angularSizeDeg = 1.5f;
};

struct PointLightState {
    glm::vec3 position{ 0.0f };
    glm::vec3 color{ 1.0f };
    float intensity = 1.0f;
    float attenLinear = 0.09f;
    float attenQuadratic = 0.032f;
    bool castsShadows = true;
    float radius = 0.05f; // emitter world radius, drives PCSS penumbra width
};

// One visible point cloud for the Schütz compute pipeline (Phase 5). The
// addresses stay valid for the frame: PointCloudGpu::destroy waits for the
// device before freeing, and unload happens before the submission is built.
struct PointCloudDrawItem {
    PointCloudGpuAddresses addresses;
    uint32_t numBatches = 0;
    int pointsPerThread = 0; // ceil(kComputeBatchSize / SV_PC_RASTER_WORKGROUP)
    glm::mat4 model{ 1.0f };
    // Per-cloud gate for the density LOD (ANDed with the frame-wide
    // lodPointsPerPixel setting). Streaming clouds submit false while their
    // batches are still file-order (see Application::buildSubmission).
    bool densityLod = true;
};

// Frame-wide point-cloud rendering options (GL: preferences.pointSplatSettings
// + pointCloudQuality).
struct PointCloudSettings {
    bool hqs = false;          // High-Quality Shading (3-pass averaging)
    float hqsThreshold = 0.01f; // relative depth window (1% = paper default)
    int splatMaxRadius = 0;    // 0 = single-pixel; >0 = adaptive splat clamp (px)
    // Density LOD budget: the compute rasterizer thins each batch to ~this
    // many points per on-screen pixel (0 = off, process every point). Applies
    // to every batch source — flat clouds and I3S pool pages alike — see
    // docs/POINTCLOUD_LOD.md Stage 1. Pair with adaptive splats: the splat
    // radius grows with the thinning to keep surfaces closed.
    float lodPointsPerPixel = 2.0f;
};

// Matches skybox.frag's SKY_MODE_* indices.
enum class SkyMode : int {
    Cubemap = 0,
    Equirect = 1,
    SolidColor = 2,
    Gradient = 3,
};

struct SkyState {
    SkyMode mode = SkyMode::Gradient;
    float intensity = 1.0f;
    glm::vec3 solidColor{ 0.2f, 0.3f, 0.4f };
    glm::vec3 gradientBottom{ 0.7f, 0.7f, 1.0f };
    glm::vec3 gradientTop{ 0.1f, 0.1f, 0.3f };
};

struct FrameSubmission {
    // Per-eye cameras of the PRIMARY viewport (viewport 0). The sun shadow
    // fallback fit and the I3S node selection reference views[0]; XR overwrites
    // both entries with the HMD eye poses.
    ViewCamera views[kMaxViews];

    // ---- Multi-viewport (dockable GUI viewports, Renderer::setViewportOutputs)
    // Viewports 1..viewportCount-1 take their per-eye cameras from
    // extraViewports[i-1]; all viewports share the draw list, lights, sky and
    // settings — only the cameras (and the target extents, configured on the
    // renderer) differ. viewportHidden[v] skips v's scene rendering for the
    // frame (its GUI tab is not visible) without touching any GPU state.
    uint32_t viewportCount = 1;
    ViewportCameras extraViewports[kMaxViewports > 1 ? kMaxViewports - 1 : 1];
    bool viewportHidden[kMaxViewports] = {};
    // The viewport the depthQueries below sample (the one under the mouse).
    uint32_t depthPickViewport = 0;

    std::vector<DrawItem> draws;
    SunState sun;
    std::vector<PointLightState> pointLights; // beyond kMaxPointLights ignored
    bool shadowsEnabled = true;
    bool softShadows = true; // PCSS contact hardening (else fixed-width PCF)
    // Point-light shadow reach in world units (cube-map far plane; the GL app
    // hardcoded far_plane = 50). Depth precision spreads over the range, so
    // keep it as tight as the scene allows.
    float pointShadowRange = 50.0f;
    float ambient = 0.03f;   // flat ambient albedo multiplier (GL parity)
    SkyState sky;
    std::vector<PointCloudDrawItem> pointClouds; // beyond SV_PC_MAX_CLOUDS share the last id
    PointCloudSettings pointCloudSettings;
    // World-space section/clip planes (n.xyz, d), kept side dot(n,p)+d >= 0.
    // Applied to point clouds (compute rasterizer) AND meshes (mesh.vert
    // gl_ClipDistance). Beyond SV_MAX_CLIP_PLANES ignored.
    std::vector<glm::vec4> clipPlanes;

    // ---- Phase 6: overlays, depth picking, fragment cursor ----
    // World-space overlay geometry (cursors/tools/gizmo/plugins), drawn on
    // the backbuffer after tonemap, depth-tested against the scene depth.
    // The list must stay alive until renderFrame returns.
    const OverlayDrawList* overlay = nullptr;
    // Scene-depth rectangles to read back for picking (view 0).
    std::vector<DepthQueryRect> depthQueries;
    FragmentCursorState fragmentCursor;
    // Optional extra recording at the top of the frame command buffer, before
    // the scene passes — CursorPreview3D renders its offscreen thumbnail
    // here so ImGui can sample it later in the same frame.
    std::function<void(VkCommandBuffer)> recordAux;
};

// The per-eye camera array of viewport `viewport` (0 = the primary views[]).
// Out-of-range indices clamp to the highest camera the submission filled, so
// renderer-side viewport counts may briefly exceed the submission's during a
// config transition without reading garbage.
inline const ViewCamera* viewportViews(const FrameSubmission& s, uint32_t viewport) {
    viewport = std::min(viewport, std::max(s.viewportCount, 1u) - 1u);
    return viewport == 0 ? s.views : s.extraViewports[viewport - 1].views;
}

} // namespace renderer
