#include "Haki/Input/InputManager.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>

#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace
{

using Haki::Input::InputManager;

enum class TestAction
{
    Jump,
    Fire,
    MoveX
};

enum class TestResult
{
    Passed,
    Failed,
    Aborted
};

TestResult runTimedTest(GLFWwindow *window, std::string_view name, std::string_view instructions,
                        const std::function<bool()> &observe, std::chrono::seconds timeout)
{
    const std::string title = "Haki Input Test: " + std::string(name);
    glfwSetWindowTitle(window, title.c_str());
    glfwShowWindow(window);
    glfwFocusWindow(window);
    std::cout << "\n[TEST] " << name << '\n' << "       " << instructions << '\n' << std::flush;

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        glfwPollEvents();
        InputManager::update();

        const bool succeeded = observe();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.04f, 0.09f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(window);

        if (glfwWindowShouldClose(window) || InputManager::isKeyPressed(gainput::KeyEscape))
        {
            std::cout << "[ABORT] Interactive test run stopped by the user.\n";
            return TestResult::Aborted;
        }
        if (succeeded)
        {
            std::cout << "[PASS] " << name << '\n';
            return TestResult::Passed;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    std::cout << "[FAIL] " << name << " timed out.\n";
    return TestResult::Failed;
}

} // namespace

int main()
{
    if (glfwInit() != GLFW_TRUE)
    {
        std::cerr << "Could not initialize GLFW. The interactive tests require a graphical session.\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow *window = glfwCreateWindow(800, 450, "Haki Input Interactive Tests", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Could not create the interactive test window.\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        std::cerr << "Could not initialize OpenGL for the interactive test window.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glfwShowWindow(window);
    glfwFocusWindow(window);
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.04f, 0.09f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(window);
    glfwPollEvents();

    if (!InputManager::initialize(window, 4))
    {
        std::cerr << "Could not initialize the input manager.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    InputManager::bindAction(TestAction::Jump, gainput::KeySpace);
    InputManager::bindAction(TestAction::Jump, gainput::PadButtonA, 0);
    InputManager::bindAction(TestAction::Fire, gainput::MouseButtonLeft);
    InputManager::bindActionAxis(TestAction::MoveX, gainput::PadButtonLeftStickX, 0);
    InputManager::setActionDeadZone(TestAction::MoveX, 0.2f);

    std::cout << "Haki Input Interactive Tests\n"
              << "Complete each instruction within 6 seconds. Press Escape to stop.\n";

    int passed = 0;
    int failed = 0;
    int skipped = 0;
    bool aborted = false;

    auto record = [&](TestResult result)
    {
        if (result == TestResult::Passed)
        {
            ++passed;
        }
        else if (result == TestResult::Failed)
        {
            ++failed;
        }
        else
        {
            aborted = true;
        }
    };

    bool keyPressed = false;
    bool keyDown = false;
    bool actionPressed = false;
    bool actionDown = false;
    record(runTimedTest(
        window, "Keyboard and action states", "Press Space, hold it briefly, then release it.",
        [&]
        {
            keyPressed |= InputManager::isKeyPressed(gainput::KeySpace);
            keyDown |= InputManager::isKeyDown(gainput::KeySpace);
            actionPressed |= InputManager::isActionPressed(TestAction::Jump);
            actionDown |= InputManager::isActionDown(TestAction::Jump);
            return keyPressed && keyDown && actionPressed && actionDown &&
                   InputManager::isKeyReleased(gainput::KeySpace) &&
                   InputManager::isActionReleased(TestAction::Jump);
        },
        std::chrono::seconds(6)));

    if (!aborted)
    {
        bool mousePressed = false;
        bool mouseDown = false;
        bool actionFirePressed = false;
        record(runTimedTest(
            window, "Mouse button and action states", "Click, briefly hold, and release the left mouse button.",
            [&]
            {
                mousePressed |= InputManager::isMouseButtonPressed(gainput::MouseButtonLeft);
                mouseDown |= InputManager::isMouseButtonDown(gainput::MouseButtonLeft);
                actionFirePressed |= InputManager::isActionPressed(TestAction::Fire);
                return mousePressed && mouseDown && actionFirePressed &&
                       InputManager::isMouseButtonReleased(gainput::MouseButtonLeft) &&
                       InputManager::isActionReleased(TestAction::Fire);
            },
            std::chrono::seconds(6)));
    }

    if (!aborted)
    {
        record(runTimedTest(
            window, "Mouse movement", "Move the mouse noticeably inside the window.",
            [] { return glm::length(InputManager::getMouseDelta()) > 0.002f; }, std::chrono::seconds(6)));
    }

    if (!aborted)
    {
        record(runTimedTest(window, "Mouse wheel", "Scroll the mouse wheel up or down.",
                            [] { return std::abs(InputManager::getMouseWheelDelta()) > 0.0f; },
                            std::chrono::seconds(6)));
    }

    if (!aborted)
    {
        glfwPollEvents();
        InputManager::update();
        const char *joystickName = glfwGetJoystickName(GLFW_JOYSTICK_1);
        const char *gamepadName = glfwGetGamepadName(GLFW_JOYSTICK_1);
        const char *gamepadGuid = glfwGetJoystickGUID(GLFW_JOYSTICK_1);
        std::cout << "\nGamepad 0 joystick: " << (joystickName ? joystickName : "not present") << '\n'
                  << "Gamepad GUID: " << (gamepadGuid ? gamepadGuid : "unavailable") << '\n'
                  << "Gamepad mapping: " << (gamepadName ? gamepadName : "not recognized by GLFW") << '\n';
        if (!InputManager::isGamepadConnected(0))
        {
            std::cout << "[SKIP] Gamepad tests: slot 0 is absent or has no GLFW gamepad mapping.\n";
            skipped += 2;
        }
        else
        {
            bool padPressed = false;
            bool padDown = false;
            bool padActionPressed = false;
            bool padActionDown = false;
            record(runTimedTest(
                window, "Gamepad button and action",
                "Press, briefly hold, and release the A/Cross button on gamepad 0.",
                [&]
                {
                    padPressed |= InputManager::isGamepadButtonPressed(0, gainput::PadButtonA);
                    padDown |= InputManager::isGamepadButtonDown(0, gainput::PadButtonA);
                    padActionPressed |= InputManager::isActionPressed(TestAction::Jump);
                    padActionDown |= InputManager::isActionDown(TestAction::Jump);
                    return padPressed && padDown && padActionPressed && padActionDown &&
                           InputManager::isGamepadButtonReleased(0, gainput::PadButtonA) &&
                           InputManager::isActionReleased(TestAction::Jump);
                },
                std::chrono::seconds(6)));

            if (!aborted)
            {
                record(runTimedTest(
                    window, "Gamepad axis and action value", "Move the left stick horizontally past halfway.",
                    []
                    {
                        return std::abs(InputManager::getGamepadAxis(0, gainput::PadButtonLeftStickX)) > 0.5f &&
                               std::abs(InputManager::getActionValue(TestAction::MoveX)) > 0.5f;
                    },
                    std::chrono::seconds(6)));
            }
        }
    }

    std::cout << "\nSummary: " << passed << " passed, " << failed << " failed, " << skipped << " skipped.\n";

    InputManager::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return aborted || failed > 0 ? 1 : 0;
}
