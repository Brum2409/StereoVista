#pragma once

// include windows before glfw/glad to prevent apientry redefinitions
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream> 
#include <array>

#include "engine/window.h"
#include "engine/input.h"
#include "engine/shader.h"
#include "engine/buffers.h"
#include "engine/data.h"

namespace Engine {
    // Texture unit assignments for different texture types
    constexpr int DIFFUSE_TEXTURE_UNIT = 0;
    constexpr int SPECULAR_TEXTURE_UNIT = 1;
    constexpr int NORMAL_TEXTURE_UNIT = 2;
    constexpr int AO_TEXTURE_UNIT = 3;
    constexpr int SHADOW_MAP_TEXTURE_UNIT = 4;
    constexpr int VOXEL_TEXTURE_UNIT = 5;
    constexpr int INSTANCE_TEXTURE_UNIT = 6;

    // Maximum number of supported texture units
    constexpr int MAX_TEXTURE_UNITS = 16;
}