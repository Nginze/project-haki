#include "Haki/Input/InputManager.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Haki::Input
{
namespace
{

class GlfwKeyboardDevice final : public gainput::InputDevice
{
public:
    GlfwKeyboardDevice(gainput::InputManager &manager, gainput::DeviceId device, unsigned index,
                       DeviceVariant /*variant*/)
        : InputDevice(manager, device,
                      index == AutoIndex ? manager.GetDeviceCountByType(DT_KEYBOARD) : index),
          nextState_(manager.GetAllocator(), gainput::KeyCount_)
    {
        state_ = manager.GetAllocator().New<gainput::InputState>(manager.GetAllocator(), gainput::KeyCount_);
        previousState_ = manager.GetAllocator().New<gainput::InputState>(manager.GetAllocator(), gainput::KeyCount_);
    }

    ~GlfwKeyboardDevice() override
    {
        manager_.GetAllocator().Delete(state_);
        manager_.GetAllocator().Delete(previousState_);
    }

    DeviceType GetType() const override { return DT_KEYBOARD; }
    const char *GetTypeName() const override { return "glfw_keyboard"; }
    bool IsValidButtonId(gainput::DeviceButtonId button) const override { return button < gainput::KeyCount_; }
    gainput::ButtonType GetButtonType(gainput::DeviceButtonId /*button*/) const override
    {
        return gainput::BT_BOOL;
    }

    void setKey(gainput::Key key, bool down)
    {
        if (IsValidButtonId(key))
        {
            nextState_.Set(key, down);
        }
    }

    void releaseAll()
    {
        for (unsigned key = 0; key < gainput::KeyCount_; ++key)
        {
            nextState_.Set(key, false);
        }
    }

protected:
    void InternalUpdate(gainput::InputDeltaState * /*delta*/) override { *state_ = nextState_; }
    DeviceState InternalGetState() const override { return DS_OK; }

private:
    gainput::InputState nextState_;
};

class GlfwMouseDevice final : public gainput::InputDevice
{
public:
    GlfwMouseDevice(gainput::InputManager &manager, gainput::DeviceId device, unsigned index,
                    DeviceVariant /*variant*/)
        : InputDevice(manager, device, index == AutoIndex ? manager.GetDeviceCountByType(DT_MOUSE) : index),
          nextState_(manager.GetAllocator(), gainput::MouseButtonCount_), scrollDelta_(0.0f),
          nextScrollDelta_(0.0f)
    {
        state_ = manager.GetAllocator().New<gainput::InputState>(manager.GetAllocator(),
                                                                 gainput::MouseButtonCount_);
        previousState_ = manager.GetAllocator().New<gainput::InputState>(manager.GetAllocator(),
                                                                         gainput::MouseButtonCount_);
    }

    ~GlfwMouseDevice() override
    {
        manager_.GetAllocator().Delete(state_);
        manager_.GetAllocator().Delete(previousState_);
    }

    DeviceType GetType() const override { return DT_MOUSE; }
    const char *GetTypeName() const override { return "glfw_mouse"; }
    bool IsValidButtonId(gainput::DeviceButtonId button) const override
    {
        return button < gainput::MouseButtonCount_;
    }
    gainput::ButtonType GetButtonType(gainput::DeviceButtonId button) const override
    {
        return button >= gainput::MouseAxisX ? gainput::BT_FLOAT : gainput::BT_BOOL;
    }

    void setButton(gainput::MouseButton button, bool down)
    {
        if (IsValidButtonId(button) && GetButtonType(button) == gainput::BT_BOOL)
        {
            nextState_.Set(button, down);
        }
    }

    void setPosition(float x, float y, bool initialize = false)
    {
        nextState_.Set(gainput::MouseAxisX, x);
        nextState_.Set(gainput::MouseAxisY, y);
        if (initialize)
        {
            state_->Set(gainput::MouseAxisX, x);
            state_->Set(gainput::MouseAxisY, y);
            previousState_->Set(gainput::MouseAxisX, x);
            previousState_->Set(gainput::MouseAxisY, y);
        }
    }

    void addScroll(float x, float y)
    {
        nextScrollDelta_ += glm::vec2(x, y);
        if (y > 0.0f)
        {
            nextState_.Set(gainput::MouseButtonWheelUp, true);
        }
        else if (y < 0.0f)
        {
            nextState_.Set(gainput::MouseButtonWheelDown, true);
        }
    }

    glm::vec2 getScrollDelta() const { return scrollDelta_; }

    void releaseAll()
    {
        for (unsigned button = gainput::MouseButton0; button < gainput::MouseButtonCount; ++button)
        {
            nextState_.Set(button, false);
        }
    }

protected:
    void InternalUpdate(gainput::InputDeltaState * /*delta*/) override
    {
        *state_ = nextState_;
        scrollDelta_ = nextScrollDelta_;
        nextScrollDelta_ = glm::vec2(0.0f);
        nextState_.Set(gainput::MouseButtonWheelUp, false);
        nextState_.Set(gainput::MouseButtonWheelDown, false);
    }

    DeviceState InternalGetState() const override { return DS_OK; }

private:
    gainput::InputState nextState_;
    glm::vec2 scrollDelta_;
    glm::vec2 nextScrollDelta_;
};

#if defined(GAINPUT_PLATFORM_LINUX)
class GlfwGamepadDevice final : public gainput::InputDevice
{
public:
    GlfwGamepadDevice(gainput::InputManager &manager, gainput::DeviceId device, unsigned index,
                      DeviceVariant /*variant*/)
        : InputDevice(manager, device, index == AutoIndex ? manager.GetDeviceCountByType(DT_PAD) : index)
    {
        state_ = manager.GetAllocator().New<gainput::InputState>(manager.GetAllocator(), gainput::PadButtonMax_);
        previousState_ =
            manager.GetAllocator().New<gainput::InputState>(manager.GetAllocator(), gainput::PadButtonMax_);

        SetDeadZone(gainput::PadButtonLeftStickX, 0.15f);
        SetDeadZone(gainput::PadButtonLeftStickY, 0.15f);
        SetDeadZone(gainput::PadButtonRightStickX, 0.15f);
        SetDeadZone(gainput::PadButtonRightStickY, 0.15f);
    }

    ~GlfwGamepadDevice() override
    {
        manager_.GetAllocator().Delete(state_);
        manager_.GetAllocator().Delete(previousState_);
    }

    DeviceType GetType() const override { return DT_PAD; }
    const char *GetTypeName() const override { return "glfw_gamepad"; }
    bool IsValidButtonId(gainput::DeviceButtonId button) const override
    {
        return button < gainput::PadButtonMax_;
    }
    gainput::ButtonType GetButtonType(gainput::DeviceButtonId button) const override
    {
        return button < gainput::PadButtonAxisCount_ ? gainput::BT_FLOAT : gainput::BT_BOOL;
    }

protected:
    void InternalUpdate(gainput::InputDeltaState * /*delta*/) override
    {
        GLFWgamepadstate gamepadState{};
        if (glfwGetGamepadState(joystickId(), &gamepadState) != GLFW_TRUE)
        {
            clearState();
            return;
        }

        setAxis(gainput::PadButtonLeftStickX, gamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_X]);
        // GLFW reports stick Y as down-positive; Haki uses up-positive (stick up == W == +1).
        setAxis(gainput::PadButtonLeftStickY, -gamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
        setAxis(gainput::PadButtonRightStickX, gamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]);
        setAxis(gainput::PadButtonRightStickY, -gamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        setAxis(gainput::PadButtonAxis4,
                (gamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f);
        setAxis(gainput::PadButtonAxis5,
                (gamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f);

        setButton(gainput::PadButtonA, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_A]);
        setButton(gainput::PadButtonB, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_B]);
        setButton(gainput::PadButtonX, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_X]);
        setButton(gainput::PadButtonY, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_Y]);
        setButton(gainput::PadButtonL1, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER]);
        setButton(gainput::PadButtonR1, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER]);
        setButton(gainput::PadButtonSelect, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_BACK]);
        setButton(gainput::PadButtonStart, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_START]);
        setButton(gainput::PadButtonHome, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_GUIDE]);
        setButton(gainput::PadButtonL3, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB]);
        setButton(gainput::PadButtonR3, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB]);
        setButton(gainput::PadButtonUp, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP]);
        setButton(gainput::PadButtonRight, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT]);
        setButton(gainput::PadButtonDown, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN]);
        setButton(gainput::PadButtonLeft, gamepadState.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT]);
    }

    DeviceState InternalGetState() const override
    {
        return glfwJoystickPresent(joystickId()) == GLFW_TRUE && glfwJoystickIsGamepad(joystickId()) == GLFW_TRUE
                   ? DS_OK
                   : DS_UNAVAILABLE;
    }

private:
    int joystickId() const { return GLFW_JOYSTICK_1 + static_cast<int>(index_); }

    void setButton(gainput::PadButton button, unsigned char value)
    {
        state_->Set(button, value == GLFW_PRESS);
    }

    void setAxis(gainput::PadButton axis, float value)
    {
        value = std::clamp(value, -1.0f, 1.0f);
        const float deadZone = GetDeadZone(axis);
        const float magnitude = std::abs(value);
        if (deadZone > 0.0f)
        {
            value = magnitude < deadZone ? 0.0f
                                         : std::copysign((magnitude - deadZone) / (1.0f - deadZone), value);
        }
        state_->Set(axis, value);
    }

    void clearState()
    {
        for (unsigned button = 0; button < gainput::PadButtonAxisCount_; ++button)
        {
            state_->Set(button, 0.0f);
        }
        for (unsigned button = gainput::PadButtonAxisCount_; button < gainput::PadButtonMax_; ++button)
        {
            state_->Set(button, false);
        }
    }
};

using ManagedGamepad = GlfwGamepadDevice;
#else
using ManagedGamepad = gainput::InputDevicePad;
#endif

struct InputState
{
    gainput::InputManager manager;
    std::unique_ptr<gainput::InputMap> actionMap;
    GlfwKeyboardDevice *keyboard = nullptr;
    GlfwMouseDevice *mouse = nullptr;
    gainput::DeviceId keyboardId = gainput::InvalidDeviceId;
    gainput::DeviceId mouseId = gainput::InvalidDeviceId;
    std::vector<ManagedGamepad *> gamepads;
    std::vector<gainput::DeviceId> gamepadIds;
    GLFWwindow *window = nullptr;
    GLFWkeyfun previousKeyCallback = nullptr;
    GLFWmousebuttonfun previousMouseButtonCallback = nullptr;
    GLFWcursorposfun previousCursorPositionCallback = nullptr;
    GLFWscrollfun previousScrollCallback = nullptr;
    GLFWwindowfocusfun previousFocusCallback = nullptr;
    glm::dvec2 lastCursorPosition{0.0};
    glm::vec2 pendingMouseDeltaPixels{0.0f};
    glm::vec2 mouseDeltaPixels{0.0f};
    bool hasCursorPosition = false;
    bool cursorCaptured = false;
};

std::unique_ptr<InputState> g_state;

void resetCursorMotion()
{
    if (!g_state || !g_state->window)
    {
        return;
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(g_state->window, &x, &y);
    g_state->lastCursorPosition = {x, y};
    g_state->hasCursorPosition = true;
    g_state->pendingMouseDeltaPixels = glm::vec2(0.0f);
    g_state->mouseDeltaPixels = glm::vec2(0.0f);
}

void setCursorCapturedInternal(bool captured)
{
    if (!g_state || !g_state->window || g_state->cursorCaptured == captured)
    {
        return;
    }

    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(g_state->window, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    }
    glfwSetInputMode(g_state->window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    g_state->cursorCaptured = captured;
    resetCursorMotion();
}

#if defined(GAINPUT_PLATFORM_LINUX)
void installMissingXboxMappings(std::size_t gamepadCount)
{
    for (std::size_t index = 0; index < gamepadCount && index <= GLFW_JOYSTICK_LAST; ++index)
    {
        const int joystick = GLFW_JOYSTICK_1 + static_cast<int>(index);
        if (glfwJoystickPresent(joystick) != GLFW_TRUE || glfwJoystickIsGamepad(joystick) == GLFW_TRUE)
        {
            continue;
        }

        const char *name = glfwGetJoystickName(joystick);
        const char *guid = glfwGetJoystickGUID(joystick);
        if (!name || !guid || (!std::strstr(name, "Xbox") && !std::strstr(name, "X-Box")))
        {
            continue;
        }

        const std::string mapping =
            std::string(guid) +
            ",Xbox Controller,a:b0,b:b1,x:b2,y:b3,back:b6,guide:b8,start:b7," 
            "leftstick:b9,rightstick:b10,leftshoulder:b4,rightshoulder:b5," 
            "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8," 
            "leftx:a0,lefty:a1,rightx:a3,righty:a4,lefttrigger:a2,righttrigger:a5,platform:Linux,";
        glfwUpdateGamepadMappings(mapping.c_str());
    }
}
#endif

gainput::Key translateKey(int key)
{
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
    {
        return static_cast<gainput::Key>(gainput::KeyA + key - GLFW_KEY_A);
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
    {
        return static_cast<gainput::Key>(gainput::Key0 + key - GLFW_KEY_0);
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F19)
    {
        return static_cast<gainput::Key>(gainput::KeyF1 + key - GLFW_KEY_F1);
    }

    switch (key)
    {
    case GLFW_KEY_ESCAPE: return gainput::KeyEscape;
    case GLFW_KEY_PRINT_SCREEN: return gainput::KeyPrint;
    case GLFW_KEY_SCROLL_LOCK: return gainput::KeyScrollLock;
    case GLFW_KEY_PAUSE: return gainput::KeyBreak;
    case GLFW_KEY_SPACE: return gainput::KeySpace;
    case GLFW_KEY_APOSTROPHE: return gainput::KeyApostrophe;
    case GLFW_KEY_COMMA: return gainput::KeyComma;
    case GLFW_KEY_MINUS: return gainput::KeyMinus;
    case GLFW_KEY_PERIOD: return gainput::KeyPeriod;
    case GLFW_KEY_SLASH: return gainput::KeySlash;
    case GLFW_KEY_SEMICOLON: return gainput::KeySemicolon;
    case GLFW_KEY_EQUAL: return gainput::KeyEqual;
    case GLFW_KEY_LEFT_BRACKET: return gainput::KeyBracketLeft;
    case GLFW_KEY_BACKSLASH: return gainput::KeyBackslash;
    case GLFW_KEY_RIGHT_BRACKET: return gainput::KeyBracketRight;
    case GLFW_KEY_GRAVE_ACCENT: return gainput::KeyGrave;
    case GLFW_KEY_LEFT: return gainput::KeyLeft;
    case GLFW_KEY_RIGHT: return gainput::KeyRight;
    case GLFW_KEY_UP: return gainput::KeyUp;
    case GLFW_KEY_DOWN: return gainput::KeyDown;
    case GLFW_KEY_INSERT: return gainput::KeyInsert;
    case GLFW_KEY_HOME: return gainput::KeyHome;
    case GLFW_KEY_DELETE: return gainput::KeyDelete;
    case GLFW_KEY_END: return gainput::KeyEnd;
    case GLFW_KEY_PAGE_UP: return gainput::KeyPageUp;
    case GLFW_KEY_PAGE_DOWN: return gainput::KeyPageDown;
    case GLFW_KEY_NUM_LOCK: return gainput::KeyNumLock;
    case GLFW_KEY_KP_EQUAL: return gainput::KeyKpEqual;
    case GLFW_KEY_KP_DIVIDE: return gainput::KeyKpDivide;
    case GLFW_KEY_KP_MULTIPLY: return gainput::KeyKpMultiply;
    case GLFW_KEY_KP_SUBTRACT: return gainput::KeyKpSubtract;
    case GLFW_KEY_KP_ADD: return gainput::KeyKpAdd;
    case GLFW_KEY_KP_ENTER: return gainput::KeyKpEnter;
    case GLFW_KEY_KP_0: return gainput::KeyKpInsert;
    case GLFW_KEY_KP_1: return gainput::KeyKpEnd;
    case GLFW_KEY_KP_2: return gainput::KeyKpDown;
    case GLFW_KEY_KP_3: return gainput::KeyKpPageDown;
    case GLFW_KEY_KP_4: return gainput::KeyKpLeft;
    case GLFW_KEY_KP_5: return gainput::KeyKpBegin;
    case GLFW_KEY_KP_6: return gainput::KeyKpRight;
    case GLFW_KEY_KP_7: return gainput::KeyKpHome;
    case GLFW_KEY_KP_8: return gainput::KeyKpUp;
    case GLFW_KEY_KP_9: return gainput::KeyKpPageUp;
    case GLFW_KEY_KP_DECIMAL: return gainput::KeyKpDelete;
    case GLFW_KEY_BACKSPACE: return gainput::KeyBackSpace;
    case GLFW_KEY_TAB: return gainput::KeyTab;
    case GLFW_KEY_ENTER: return gainput::KeyReturn;
    case GLFW_KEY_CAPS_LOCK: return gainput::KeyCapsLock;
    case GLFW_KEY_LEFT_SHIFT: return gainput::KeyShiftL;
    case GLFW_KEY_LEFT_CONTROL: return gainput::KeyCtrlL;
    case GLFW_KEY_LEFT_SUPER: return gainput::KeySuperL;
    case GLFW_KEY_LEFT_ALT: return gainput::KeyAltL;
    case GLFW_KEY_RIGHT_ALT: return gainput::KeyAltR;
    case GLFW_KEY_RIGHT_SUPER: return gainput::KeySuperR;
    case GLFW_KEY_MENU: return gainput::KeyMenu;
    case GLFW_KEY_RIGHT_CONTROL: return gainput::KeyCtrlR;
    case GLFW_KEY_RIGHT_SHIFT: return gainput::KeyShiftR;
    default: return static_cast<gainput::Key>(gainput::InvalidDeviceButtonId);
    }
}

gainput::MouseButton translateMouseButton(int button)
{
    switch (button)
    {
    case GLFW_MOUSE_BUTTON_LEFT: return gainput::MouseButtonLeft;
    case GLFW_MOUSE_BUTTON_MIDDLE: return gainput::MouseButtonMiddle;
    case GLFW_MOUSE_BUTTON_RIGHT: return gainput::MouseButtonRight;
    default:
        if (button >= GLFW_MOUSE_BUTTON_4 && button <= GLFW_MOUSE_BUTTON_8)
        {
            return static_cast<gainput::MouseButton>(gainput::MouseButton5 + button - GLFW_MOUSE_BUTTON_4);
        }
        return static_cast<gainput::MouseButton>(gainput::InvalidDeviceButtonId);
    }
}

bool isValidKey(gainput::Key key)
{
    return static_cast<gainput::DeviceButtonId>(key) < gainput::KeyCount_;
}

bool isValidMouseButton(gainput::MouseButton button)
{
    return static_cast<gainput::DeviceButtonId>(button) < gainput::MouseButtonCount_;
}

gainput::InputDevice *getGamepad(std::size_t index)
{
    if (!g_state || index >= g_state->gamepads.size())
    {
        return nullptr;
    }
    return g_state->gamepads[index];
}

void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    const gainput::Key translated = translateKey(key);
    if (g_state && isValidKey(translated) && action != GLFW_REPEAT)
    {
        g_state->keyboard->setKey(translated, action == GLFW_PRESS);
    }
    if (g_state && g_state->previousKeyCallback)
    {
        g_state->previousKeyCallback(window, key, scancode, action, mods);
    }
}

void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    const gainput::MouseButton translated = translateMouseButton(button);
    if (g_state && isValidMouseButton(translated))
    {
        g_state->mouse->setButton(translated, action == GLFW_PRESS);
    }
    if (g_state && g_state->previousMouseButtonCallback)
    {
        g_state->previousMouseButtonCallback(window, button, action, mods);
    }
}

void cursorPositionCallback(GLFWwindow *window, double x, double y)
{
    if (g_state)
    {
        if (g_state->hasCursorPosition)
        {
            g_state->pendingMouseDeltaPixels +=
                glm::vec2(static_cast<float>(x - g_state->lastCursorPosition.x),
                          static_cast<float>(y - g_state->lastCursorPosition.y));
        }
        g_state->lastCursorPosition = {x, y};
        g_state->hasCursorPosition = true;

        int width = 0;
        int height = 0;
        glfwGetWindowSize(window, &width, &height);
        const float normalizedX = width > 0 ? static_cast<float>(x) / static_cast<float>(width) : 0.0f;
        const float normalizedY = height > 0 ? static_cast<float>(y) / static_cast<float>(height) : 0.0f;
        g_state->mouse->setPosition(normalizedX, normalizedY);
    }
    if (g_state && g_state->previousCursorPositionCallback)
    {
        g_state->previousCursorPositionCallback(window, x, y);
    }
}

void scrollCallback(GLFWwindow *window, double x, double y)
{
    if (g_state)
    {
        g_state->mouse->addScroll(static_cast<float>(x), static_cast<float>(y));
    }
    if (g_state && g_state->previousScrollCallback)
    {
        g_state->previousScrollCallback(window, x, y);
    }
}

void focusCallback(GLFWwindow *window, int focused)
{
    if (g_state && focused == GLFW_FALSE)
    {
        g_state->keyboard->releaseAll();
        g_state->mouse->releaseAll();
        setCursorCapturedInternal(false);
        resetCursorMotion();
    }
    if (g_state && g_state->previousFocusCallback)
    {
        g_state->previousFocusCallback(window, focused);
    }
}

bool buttonDown(const gainput::InputDevice *device, gainput::DeviceButtonId button)
{
    return device && device->IsValidButtonId(button) && device->GetButtonType(button) == gainput::BT_BOOL &&
           device->GetBool(button);
}

bool buttonPressed(const gainput::InputDevice *device, gainput::DeviceButtonId button)
{
    return buttonDown(device, button) && !device->GetBoolPrevious(button);
}

bool buttonReleased(const gainput::InputDevice *device, gainput::DeviceButtonId button)
{
    return device && device->IsValidButtonId(button) && device->GetButtonType(button) == gainput::BT_BOOL &&
           !device->GetBool(button) && device->GetBoolPrevious(button);
}

} // namespace

bool InputManager::initialize(GLFWwindow *window, std::size_t gamepadCount)
{
    if (!window)
    {
        return false;
    }
    if (g_state)
    {
        return g_state->window == window;
    }

    auto state = std::make_unique<InputState>();
    state->window = window;
#if defined(GAINPUT_PLATFORM_LINUX)
    installMissingXboxMappings(gamepadCount);
#endif
    state->keyboard = state->manager.CreateAndGetDevice<GlfwKeyboardDevice>();
    state->mouse = state->manager.CreateAndGetDevice<GlfwMouseDevice>();
    state->keyboardId = state->keyboard->GetDeviceId();
    state->mouseId = state->mouse->GetDeviceId();

    gamepadCount = std::min<std::size_t>(gamepadCount, gainput::MaxPadCount);
    state->gamepads.reserve(gamepadCount);
    state->gamepadIds.reserve(gamepadCount);
    for (std::size_t i = 0; i < gamepadCount; ++i)
    {
        auto *gamepad = state->manager.CreateAndGetDevice<ManagedGamepad>();
        state->gamepads.push_back(gamepad);
        state->gamepadIds.push_back(gamepad->GetDeviceId());
    }
    state->actionMap = std::make_unique<gainput::InputMap>(state->manager, "HakiActions");

    int width = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);
    state->manager.SetDisplaySize(std::max(width, 1), std::max(height, 1));

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    state->mouse->setPosition(width > 0 ? static_cast<float>(cursorX) / static_cast<float>(width) : 0.0f,
                              height > 0 ? static_cast<float>(cursorY) / static_cast<float>(height) : 0.0f, true);

    g_state = std::move(state);
    g_state->previousKeyCallback = glfwSetKeyCallback(window, keyCallback);
    g_state->previousMouseButtonCallback = glfwSetMouseButtonCallback(window, mouseButtonCallback);
    g_state->previousCursorPositionCallback = glfwSetCursorPosCallback(window, cursorPositionCallback);
    g_state->previousScrollCallback = glfwSetScrollCallback(window, scrollCallback);
    g_state->previousFocusCallback = glfwSetWindowFocusCallback(window, focusCallback);
    resetCursorMotion();
    return true;
}

void InputManager::shutdown()
{
    if (!g_state)
    {
        return;
    }

    if (g_state->window)
    {
        setCursorCapturedInternal(false);
        glfwSetKeyCallback(g_state->window, g_state->previousKeyCallback);
        glfwSetMouseButtonCallback(g_state->window, g_state->previousMouseButtonCallback);
        glfwSetCursorPosCallback(g_state->window, g_state->previousCursorPositionCallback);
        glfwSetScrollCallback(g_state->window, g_state->previousScrollCallback);
        glfwSetWindowFocusCallback(g_state->window, g_state->previousFocusCallback);
    }
    g_state.reset();
}

void InputManager::update()
{
    if (!g_state)
    {
        return;
    }

    g_state->mouseDeltaPixels = g_state->pendingMouseDeltaPixels;
    g_state->pendingMouseDeltaPixels = glm::vec2(0.0f);

    int width = 0;
    int height = 0;
    glfwGetWindowSize(g_state->window, &width, &height);
    g_state->manager.SetDisplaySize(std::max(width, 1), std::max(height, 1));
#if defined(GAINPUT_PLATFORM_LINUX)
    installMissingXboxMappings(g_state->gamepads.size());
#endif
    g_state->manager.Update();
}

void InputManager::setDisplaySize(int width, int height)
{
    if (g_state)
    {
        g_state->manager.SetDisplaySize(std::max(width, 1), std::max(height, 1));
    }
}

bool InputManager::isInitialized()
{
    return static_cast<bool>(g_state);
}

bool InputManager::isKeyDown(Key key)
{
    return g_state && buttonDown(g_state->keyboard, key);
}

bool InputManager::isKeyUp(Key key)
{
    return !isKeyDown(key);
}

bool InputManager::isKeyPressed(Key key)
{
    return g_state && buttonPressed(g_state->keyboard, key);
}

bool InputManager::isKeyReleased(Key key)
{
    return g_state && buttonReleased(g_state->keyboard, key);
}

bool InputManager::isMouseButtonDown(MouseButton button)
{
    return g_state && buttonDown(g_state->mouse, button);
}

bool InputManager::isMouseButtonUp(MouseButton button)
{
    return !isMouseButtonDown(button);
}

bool InputManager::isMouseButtonPressed(MouseButton button)
{
    return g_state && buttonPressed(g_state->mouse, button);
}

bool InputManager::isMouseButtonReleased(MouseButton button)
{
    return g_state && buttonReleased(g_state->mouse, button);
}

glm::vec2 InputManager::getMousePosition()
{
    if (!g_state)
    {
        return glm::vec2(0.0f);
    }
    return {g_state->mouse->GetFloat(gainput::MouseAxisX), g_state->mouse->GetFloat(gainput::MouseAxisY)};
}

glm::vec2 InputManager::getMouseDelta()
{
    if (!g_state)
    {
        return glm::vec2(0.0f);
    }
    return {g_state->mouse->GetFloat(gainput::MouseAxisX) -
                g_state->mouse->GetFloatPrevious(gainput::MouseAxisX),
            g_state->mouse->GetFloat(gainput::MouseAxisY) -
                g_state->mouse->GetFloatPrevious(gainput::MouseAxisY)};
}

glm::vec2 InputManager::getMouseDeltaPixels()
{
    return g_state ? g_state->mouseDeltaPixels : glm::vec2(0.0f);
}

glm::vec2 InputManager::getMouseScrollDelta()
{
    return g_state ? g_state->mouse->getScrollDelta() : glm::vec2(0.0f);
}

float InputManager::getMouseWheelDelta()
{
    return getMouseScrollDelta().y;
}

bool InputManager::setCursorCaptured(bool captured)
{
    if (!g_state || !g_state->window)
    {
        return false;
    }
    setCursorCapturedInternal(captured);
    return true;
}

bool InputManager::isCursorCaptured()
{
    return g_state && g_state->cursorCaptured;
}

std::size_t InputManager::getGamepadCount()
{
    return g_state ? g_state->gamepads.size() : 0;
}

bool InputManager::isGamepadConnected(std::size_t gamepadIndex)
{
    const auto *gamepad = getGamepad(gamepadIndex);
    return gamepad && gamepad->IsAvailable();
}

bool InputManager::isGamepadButtonDown(std::size_t gamepadIndex, GamepadButton button)
{
    return buttonDown(getGamepad(gamepadIndex), button);
}

bool InputManager::isGamepadButtonUp(std::size_t gamepadIndex, GamepadButton button)
{
    return !isGamepadButtonDown(gamepadIndex, button);
}

bool InputManager::isGamepadButtonPressed(std::size_t gamepadIndex, GamepadButton button)
{
    return buttonPressed(getGamepad(gamepadIndex), button);
}

bool InputManager::isGamepadButtonReleased(std::size_t gamepadIndex, GamepadButton button)
{
    return buttonReleased(getGamepad(gamepadIndex), button);
}

float InputManager::getGamepadAxis(std::size_t gamepadIndex, GamepadButton axis)
{
    const auto *gamepad = getGamepad(gamepadIndex);
    return gamepad && gamepad->IsValidButtonId(axis) && gamepad->GetButtonType(axis) == gainput::BT_FLOAT
               ? gamepad->GetFloat(axis)
               : 0.0f;
}

float InputManager::getGamepadAxisPrevious(std::size_t gamepadIndex, GamepadButton axis)
{
    const auto *gamepad = getGamepad(gamepadIndex);
    return gamepad && gamepad->IsValidButtonId(axis) && gamepad->GetButtonType(axis) == gainput::BT_FLOAT
               ? gamepad->GetFloatPrevious(axis)
               : 0.0f;
}

float InputManager::getGamepadAxisDelta(std::size_t gamepadIndex, GamepadButton axis)
{
    return getGamepadAxis(gamepadIndex, axis) - getGamepadAxisPrevious(gamepadIndex, axis);
}

void InputManager::setGamepadDeadZone(std::size_t gamepadIndex, GamepadButton axis, float deadZone)
{
    auto *gamepad = getGamepad(gamepadIndex);
    if (gamepad && gamepad->IsValidButtonId(axis) && gamepad->GetButtonType(axis) == gainput::BT_FLOAT)
    {
        gamepad->SetDeadZone(axis, std::clamp(deadZone, 0.0f, 0.99f));
    }
}

bool InputManager::setGamepadVibration(std::size_t gamepadIndex, float leftMotor, float rightMotor)
{
#if defined(GAINPUT_PLATFORM_LINUX)
    (void)gamepadIndex;
    (void)leftMotor;
    (void)rightMotor;
    return false;
#else
    auto *gamepad = getGamepad(gamepadIndex);
    auto *nativeGamepad = static_cast<gainput::InputDevicePad *>(gamepad);
    return nativeGamepad && nativeGamepad->Vibrate(std::clamp(leftMotor, 0.0f, 1.0f),
                                                    std::clamp(rightMotor, 0.0f, 1.0f));
#endif
}

bool InputManager::bindKeyAction(gainput::UserButtonId action, Key key, float min, float max)
{
    return g_state && isValidKey(key) && g_state->actionMap->MapFloat(action, g_state->keyboardId, key, min, max);
}

bool InputManager::bindMouseAction(gainput::UserButtonId action, MouseButton button, float min, float max)
{
    return g_state && isValidMouseButton(button) &&
           g_state->actionMap->MapFloat(action, g_state->mouseId, button, min, max);
}

bool InputManager::bindGamepadAction(gainput::UserButtonId action, GamepadButton button,
                                     std::size_t gamepadIndex, float min, float max)
{
    const auto *gamepad = getGamepad(gamepadIndex);
    return g_state && gamepad && gamepad->IsValidButtonId(button) &&
           g_state->actionMap->MapFloat(action, g_state->gamepadIds[gamepadIndex], button, min, max);
}

bool InputManager::isActionMappedById(gainput::UserButtonId action)
{
    return g_state && g_state->actionMap->IsMapped(action);
}

bool InputManager::isActionDownById(gainput::UserButtonId action)
{
    return getActionValueById(action) != 0.0f;
}

bool InputManager::isActionPressedById(gainput::UserButtonId action)
{
    return getActionValueById(action) != 0.0f && getActionPreviousValueById(action) == 0.0f;
}

bool InputManager::isActionReleasedById(gainput::UserButtonId action)
{
    return getActionValueById(action) == 0.0f && getActionPreviousValueById(action) != 0.0f;
}

float InputManager::getActionValueById(gainput::UserButtonId action)
{
    return isActionMappedById(action) ? g_state->actionMap->GetFloat(action) : 0.0f;
}

float InputManager::getActionPreviousValueById(gainput::UserButtonId action)
{
    return isActionMappedById(action) ? g_state->actionMap->GetFloatPrevious(action) : 0.0f;
}

float InputManager::getActionDeltaById(gainput::UserButtonId action)
{
    return isActionMappedById(action) ? g_state->actionMap->GetFloatDelta(action) : 0.0f;
}

bool InputManager::setActionDeadZoneById(gainput::UserButtonId action, float deadZone)
{
    return isActionMappedById(action) &&
           g_state->actionMap->SetDeadZone(action, std::clamp(deadZone, 0.0f, 0.99f));
}

bool InputManager::setActionPolicyById(gainput::UserButtonId action, ActionPolicy policy)
{
    return isActionMappedById(action) && g_state->actionMap->SetUserButtonPolicy(action, policy);
}

void InputManager::unbindActionById(gainput::UserButtonId action)
{
    if (g_state)
    {
        g_state->actionMap->Unmap(action);
    }
}

void InputManager::clearActions()
{
    if (g_state)
    {
        g_state->actionMap->Clear();
    }
}

} // namespace Haki::Input
