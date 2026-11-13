#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <functional>

#include "Haki/Logging/Logger.h"

class Window
{
public:
    // Definition for a resize callback function
    using ResizeCallback = std::function<void(int width, int height)>;

    Window(int width, int height, const std::string &title);
    ~Window();

    // Prevent copying to avoid multiple objects trying to manage the same GLFW lifecycle
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    // Allow moving the window object if needed
    Window(Window &&other) noexcept;
    Window &operator=(Window &&other) noexcept;

    bool ShouldClose() const;
    void SwapBuffers();
    void PollEvents();

    // Getters
    GLFWwindow *GetNativeWindow() const { return m_window; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    // Set user-defined callbacks
    void SetResizeCallback(ResizeCallback callback)
    {
        Logger::Debug() << "Set Resize callback set";
        m_resizeCallback = callback;
    }

private:
    GLFWwindow *m_window = nullptr;
    int m_width;
    int m_height;
    std::string m_title;
    ResizeCallback m_resizeCallback = nullptr;

    void Init();
    void Shutdown();

    // Internal static callback required by GLFW to bridge C-style pointers to C++ classes
    static void FramebufferSizeCallback(GLFWwindow *window, int width, int height);
};
