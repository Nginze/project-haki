#include "Haki/Platform/Window.h"
#include <iostream>

Window::Window(int width, int height, const std::string &title)
    : m_width(width), m_height(height), m_title(title)
{
    Init();
}

Window::~Window()
{
    Shutdown();
}

// Move Constructor
Window::Window(Window &&other) noexcept
    : m_window(other.m_window), m_width(other.m_width), m_height(other.m_height),
      m_title(std::move(other.m_title)), m_resizeCallback(std::move(other.m_resizeCallback))
{
    other.m_window = nullptr;
}

// Move Assignment
Window &Window::operator=(Window &&other) noexcept
{
    if (this != &other)
    {
        Shutdown();
        m_window = other.m_window;
        m_width = other.m_width;
        m_height = other.m_height;
        m_title = std::move(other.m_title);
        m_resizeCallback = std::move(other.m_resizeCallback);
        other.m_window = nullptr;
    }
    return *this;
}

void Window::Init()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        std::cerr << "Failed to create GLFW Window!" << std::endl;
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);

    // GLAD must be loaded *after* setting the current context
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD!" << std::endl;
        return;
    }

    // Resolve the true backbuffer size (may differ from the requested window
    // size on high-DPI / scaled displays) and keep it in m_width/m_height so
    // the viewport and UI canvas always agree with framebuffer pixel space.
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    if (fbWidth > 0 && fbHeight > 0)
    {
        m_width = fbWidth;
        m_height = fbHeight;
    }

    glViewport(0, 0, m_width, m_height);

    // This connects our custom C++ Window object instance to the underlying C-style GLFWwindow pointer
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, FramebufferSizeCallback);
    glfwSetCharCallback(
        m_window,
        [](GLFWwindow *, unsigned int codepoint)
        {
            // Engine::Get()
            //     .GetInputManager()
            //     .OnTextInput(codepoint);
        });
}

void Window::Shutdown()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
}

bool Window::ShouldClose() const
{
    return glfwWindowShouldClose(m_window);
}

void Window::SwapBuffers()
{
    glfwSwapBuffers(m_window);
}

void Window::PollEvents()
{
    glfwPollEvents();
}

// Static bridge callback
void Window::FramebufferSizeCallback(GLFWwindow *window, int width, int height)
{
    Logger::Debug() << "Resize: " << width << " - " << height;
    // Retrieve the C++ Window class instance using the User Pointer we bound during Init
    Window *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));

    if (instance)
    {
        instance->m_width = width;
        instance->m_height = height;
        glViewport(0, 0, width, height);

        // If the user of this window registered a custom resize behavior, trigger it now
        if (instance->m_resizeCallback)
        {
            Logger::Debug() << "Calling resize callback";
            instance->m_resizeCallback(width, height);
        }
        else
        {

            Logger::Debug() << "No resize callback";
        }
    }
}