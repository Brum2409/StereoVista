// File: input.cpp
#include "engine/input.h"
#include "engine/window.h"

namespace Engine {
    namespace Input {
        bool keyPressedData[GLFW_KEY_LAST] = {};
        bool mouseButtonPressedData[GLFW_MOUSE_BUTTON_LAST] = {};
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        float mouseScrollX = 0.0f;
        float mouseScrollY = 0.0f;

        // Utility
        bool isKeyDown(int key) {
            if (key >= 0 && key < GLFW_KEY_LAST) {
                return glfwGetKey(Window::nativeWindow, key) == GLFW_PRESS;
            }
            return false;
        }

        bool isMouseButtonDown(int mouseButton) {
            if (mouseButton >= 0 && mouseButton < GLFW_MOUSE_BUTTON_LAST) {
                return glfwGetMouseButton(Window::nativeWindow, mouseButton) == GLFW_PRESS;
            }
            return false;
        }

        // Callbacks
        void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
            if (key >= 0 && key < GLFW_KEY_LAST) {
                keyPressedData[key] = action != GLFW_RELEASE;
            }
        }

        void mousePosCallback(GLFWwindow* window, double xpos, double ypos) {
            mouseX = (float)xpos;
            mouseY = (float)ypos;
        }

        void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
            if (button >= 0 && button < GLFW_MOUSE_BUTTON_LAST) {
                mouseButtonPressedData[button] = action == GLFW_PRESS;
            }
        }

        void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
            mouseScrollX = (float)xoffset;
            mouseScrollY = (float)yoffset;
        }

        void handleKeyInput(Camera& camera, float deltaTime) {
            float cameraSpeed = 2.5f * deltaTime;

            if (isKeyDown(GLFW_KEY_ESCAPE)) {
                Window::close();
            }
            if (isKeyDown(GLFW_KEY_W)) {
                camera.ProcessKeyboard(FORWARD, deltaTime);
            }
            if (isKeyDown(GLFW_KEY_S)) {
                camera.ProcessKeyboard(BACKWARD, deltaTime);
            }
            if (isKeyDown(GLFW_KEY_A)) {
                camera.ProcessKeyboard(LEFT, deltaTime);
            }
            if (isKeyDown(GLFW_KEY_D)) {
                camera.ProcessKeyboard(RIGHT, deltaTime);
            }
            if (isKeyDown(GLFW_KEY_SPACE)) {
                camera.ProcessKeyboard(UP, deltaTime);
            }
            if (isKeyDown(GLFW_KEY_LEFT_SHIFT)) {

                camera.ProcessKeyboard(DOWN, deltaTime);
            }
        }
    }
}