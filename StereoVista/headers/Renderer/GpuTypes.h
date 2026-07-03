#pragma once

// C++ binding of the shared C++/GLSL scene structs. The single source of
// truth is assets/shaders_vk/scene_types.h (see the header comment there);
// this file only aliases the GLSL type names to glm and pins the layout with
// static_asserts. All GPU buffers bound with layout(scalar) memcpy these
// structs verbatim.

#include <glm/glm.hpp>

#include <cstdint>

namespace renderer {
namespace gpu {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uint = std::uint32_t;

#include "../../assets/shaders_vk/scene_types.h"

static_assert(sizeof(ViewData) == 144, "ViewData layout drifted from GLSL scalar");
static_assert(sizeof(FrameData) == 1968, "FrameData layout drifted from GLSL scalar");
static_assert(sizeof(PointLightData) == 48, "PointLightData layout drifted from GLSL scalar");
static_assert(sizeof(MaterialData) == 64, "MaterialData layout drifted from GLSL scalar");
static_assert(sizeof(DrawPush) == 116, "DrawPush must stay within 128 push-constant bytes");
static_assert(sizeof(DepthSunPush) == 64, "DepthSunPush layout drifted");
static_assert(sizeof(DepthPointPush) == 68, "DepthPointPush layout drifted");

} // namespace gpu

inline constexpr uint32_t kMaxViews = SV_MAX_VIEWS;
inline constexpr uint32_t kMaxPointLights = SV_MAX_POINT_LIGHTS;
inline constexpr uint32_t kMaxShadowedPointLights = SV_MAX_SHADOWED_POINT_LIGHTS;
inline constexpr uint32_t kPointShadowResolution = SV_POINT_SHADOW_RESOLUTION;
inline constexpr uint32_t kSunShadowResolution = SV_SUN_SHADOW_RESOLUTION;
inline constexpr uint32_t kInvalidTexture = SV_INVALID_TEXTURE;

} // namespace renderer
