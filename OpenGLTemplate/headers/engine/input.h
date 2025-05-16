#pragma once
#include "Core.h"
#include "Core/Camera.h"

namespace Engine {
    namespace Input {
        extern bool keyPressedData[GLFW_KEY_LAST];
        extern bool mouseButtonPressedData[GLFW_MOUSE_BUTTON_LAST];
        extern float mouseX;
        extern float mouseY;
        extern float mouseScrollX;
        extern float mouseScrollY;

        // Utility functions
        bool isKeyDown(int key);
        bool isMouseButtonDown(int mouseButton);

        // Input handling
        void handleKeyInput(Camera& camera, float deltaTime);

        // Callback functions
        void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        void mousePosCallback(GLFWwindow* window, double xpos, double ypos);
        void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    }
}