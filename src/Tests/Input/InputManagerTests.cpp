#include "Haki/Input/InputManager.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace
{

using Haki::Input::InputManager;

enum class TestAction
{
    Jump,
    Fire,
    MoveX
};

class TestRunner
{
public:
    explicit TestRunner(std::string_view name)
    {
        std::cout << "\n=== " << name << " ===\n";
    }

    void check(bool condition, const char *name)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
        if (!condition)
        {
            ++failures_;
        }
    }

    int result() const
    {
        std::cout << (failures_ == 0 ? "Case passed." : "Case failed.") << '\n';
        return failures_ == 0 ? 0 : 1;
    }

private:
    int failures_ = 0;
};

class HiddenWindow
{
public:
    HiddenWindow()
    {
#if defined(__linux__)
        // Hidden no-API windows trigger a GLFW 3.4 fractional-scaling teardown bug on some Wayland compositors.
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
        if (glfwInit() != GLFW_TRUE)
        {
            return;
        }

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ = glfwCreateWindow(320, 240, "InputManager automated tests", nullptr, nullptr);
        if (window_)
        {
            glfwPollEvents();
        }
    }

    ~HiddenWindow()
    {
        InputManager::shutdown();
        if (window_)
        {
            glfwPollEvents();
            glfwDestroyWindow(window_);
        }
        glfwTerminate();
    }

    GLFWwindow *get() const { return window_; }

private:
    GLFWwindow *window_ = nullptr;
};

int runLifecycleTests()
{
    TestRunner tests("InputManager.Lifecycle");

    InputManager::shutdown();
    tests.check(!InputManager::isInitialized(), "shutdown is safe before initialization");
    tests.check(!InputManager::initialize(nullptr), "initialization rejects a null window");
    tests.check(!InputManager::isInitialized(), "failed initialization leaves manager shut down");

    HiddenWindow window;
    if (!window.get())
    {
        std::cout << "[SKIP] GLFW could not create a hidden test window.\n";
        return 77;
    }

    tests.check(InputManager::initialize(window.get(), 2), "manager initializes with a GLFW window");
    tests.check(InputManager::isInitialized(), "manager reports initialized state");
    tests.check(InputManager::getGamepadCount() == 2, "requested gamepad slots are created");
    tests.check(InputManager::initialize(window.get(), 2), "reinitializing the same window is idempotent");

    tests.check(!InputManager::isCursorCaptured(), "cursor starts uncaptured");
    tests.check(InputManager::setCursorCaptured(true), "cursor capture can be enabled");
    tests.check(InputManager::isCursorCaptured(), "cursor capture state is reported");
    tests.check(glfwGetInputMode(window.get(), GLFW_CURSOR) == GLFW_CURSOR_DISABLED,
                "cursor capture uses GLFW disabled mode");
    tests.check(InputManager::setCursorCaptured(false), "cursor capture can be disabled");
    tests.check(!InputManager::isCursorCaptured(), "cursor release state is reported");

    InputManager::setDisplaySize(640, 480);
    InputManager::update();
    InputManager::shutdown();
    tests.check(!InputManager::isInitialized(), "shutdown clears initialized state");
    tests.check(InputManager::getGamepadCount() == 0, "shutdown clears gamepad slots");
    InputManager::shutdown();
    tests.check(!InputManager::isInitialized(), "repeated shutdown is safe");
    return tests.result();
}

int runSafeQueryTests()
{
    TestRunner tests("InputManager.SafeRawQueries");
    HiddenWindow window;
    if (!window.get())
    {
        std::cout << "[SKIP] GLFW could not create a hidden test window.\n";
        return 77;
    }
    if (!InputManager::initialize(window.get(), 2))
    {
        std::cout << "[FAIL] InputManager initialization failed.\n";
        return 1;
    }

    InputManager::update();
    tests.check(InputManager::isKeyUp(gainput::KeySpace), "unpressed key reports up");
    tests.check(InputManager::isMouseButtonUp(gainput::MouseButtonLeft), "unpressed mouse button reports up");
    tests.check(!InputManager::isGamepadConnected(99), "invalid gamepad index is safely disconnected");
    tests.check(!InputManager::isGamepadButtonDown(99, gainput::PadButtonA),
                "invalid gamepad button query returns false");
    tests.check(InputManager::getGamepadAxis(99, gainput::PadButtonLeftStickX) == 0.0f,
                "invalid gamepad axis query returns zero");
    const glm::vec2 mousePosition = InputManager::getMousePosition();
    tests.check(std::isfinite(mousePosition.x) && std::isfinite(mousePosition.y),
                "mouse position query returns finite values");
    const glm::vec2 mouseDeltaPixels = InputManager::getMouseDeltaPixels();
    tests.check(std::isfinite(mouseDeltaPixels.x) && std::isfinite(mouseDeltaPixels.y),
                "pixel mouse delta query returns finite values");
    return tests.result();
}

int runActionMappingTests()
{
    TestRunner tests("InputManager.ActionMappings");
    HiddenWindow window;
    if (!window.get())
    {
        std::cout << "[SKIP] GLFW could not create a hidden test window.\n";
        return 77;
    }
    if (!InputManager::initialize(window.get(), 2))
    {
        std::cout << "[FAIL] InputManager initialization failed.\n";
        return 1;
    }

    tests.check(!InputManager::isActionMapped(TestAction::Jump), "action starts unmapped");
    tests.check(!InputManager::isActionDown(TestAction::Jump), "unmapped action is not down");
    tests.check(InputManager::isActionUp(TestAction::Jump), "unmapped action safely reports up");
    tests.check(InputManager::getActionValue(TestAction::Jump) == 0.0f, "unmapped action value is zero");
    tests.check(InputManager::bindAction(TestAction::Jump, gainput::KeySpace),
                "keyboard action binding succeeds");
    tests.check(InputManager::bindAction(TestAction::Jump, gainput::PadButtonA, 0),
                "gamepad action binding succeeds");
    tests.check(InputManager::bindAction(TestAction::Fire, gainput::MouseButtonLeft),
                "mouse action binding succeeds");
    tests.check(InputManager::bindActionAxis(TestAction::MoveX, gainput::PadButtonLeftStickX, 0),
                "gamepad axis action binding succeeds");
    tests.check(InputManager::isActionMapped(TestAction::Jump), "bound action reports mapped");
    tests.check(InputManager::setActionDeadZone(TestAction::MoveX, 0.2f), "action dead zone can be set");
    tests.check(InputManager::setActionPolicy(TestAction::Jump, gainput::InputMap::UBP_MAX),
                "action mapping policy can be set");

    InputManager::unbindAction(TestAction::Fire);
    tests.check(!InputManager::isActionMapped(TestAction::Fire), "single action can be unbound");
    InputManager::clearActions();
    tests.check(!InputManager::isActionMapped(TestAction::Jump), "clearActions removes button mappings");
    tests.check(!InputManager::isActionMapped(TestAction::MoveX), "clearActions removes axis mappings");
    return tests.result();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: InputManagerTests <lifecycle|safe-queries|action-mappings>\n";
        return 2;
    }

    const std::string_view testCase = argv[1];
    if (testCase == "lifecycle")
    {
        return runLifecycleTests();
    }
    if (testCase == "safe-queries")
    {
        return runSafeQueryTests();
    }
    if (testCase == "action-mappings")
    {
        return runActionMappingTests();
    }

    std::cerr << "Unknown test case: " << testCase << '\n';
    return 2;
}
