#pragma once

#include <gainput/gainput.h>
#include <glm/vec2.hpp>

#include <cstddef>
#include <type_traits>

struct GLFWwindow;

namespace Haki::Input
{

class InputManager
{
public:
    using Key = gainput::Key;
    using MouseButton = gainput::MouseButton;
    using GamepadButton = gainput::PadButton;
    using ActionPolicy = gainput::InputMap::UserButtonPolicy;

    InputManager() = delete;

    static bool initialize(GLFWwindow *window, std::size_t gamepadCount = 4);
    static void shutdown();
    static void update();
    static void setDisplaySize(int width, int height);
    static bool isInitialized();

    static bool isKeyDown(Key key);
    static bool isKeyUp(Key key);
    static bool isKeyPressed(Key key);
    static bool isKeyReleased(Key key);

    static bool isMouseButtonDown(MouseButton button);
    static bool isMouseButtonUp(MouseButton button);
    static bool isMouseButtonPressed(MouseButton button);
    static bool isMouseButtonReleased(MouseButton button);
    static glm::vec2 getMousePosition();
    static glm::vec2 getMouseDelta();
    static glm::vec2 getMouseDeltaPixels();
    static glm::vec2 getMouseScrollDelta();
    static float getMouseWheelDelta();
    static bool setCursorCaptured(bool captured);
    static bool isCursorCaptured();

    static std::size_t getGamepadCount();
    static bool isGamepadConnected(std::size_t gamepadIndex);
    static bool isGamepadButtonDown(std::size_t gamepadIndex, GamepadButton button);
    static bool isGamepadButtonUp(std::size_t gamepadIndex, GamepadButton button);
    static bool isGamepadButtonPressed(std::size_t gamepadIndex, GamepadButton button);
    static bool isGamepadButtonReleased(std::size_t gamepadIndex, GamepadButton button);
    static float getGamepadAxis(std::size_t gamepadIndex, GamepadButton axis);
    static float getGamepadAxisPrevious(std::size_t gamepadIndex, GamepadButton axis);
    static float getGamepadAxisDelta(std::size_t gamepadIndex, GamepadButton axis);
    static void setGamepadDeadZone(std::size_t gamepadIndex, GamepadButton axis, float deadZone);
    static bool setGamepadVibration(std::size_t gamepadIndex, float leftMotor, float rightMotor);

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool bindAction(Action action, Key key)
    {
        return bindKeyAction(toActionId(action), key);
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool bindAction(Action action, MouseButton button)
    {
        return bindMouseAction(toActionId(action), button);
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool bindAction(Action action, GamepadButton button, std::size_t gamepadIndex = 0)
    {
        return bindGamepadAction(toActionId(action), button, gamepadIndex);
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool bindActionAxis(Action action, MouseButton axis, float min = 0.0f, float max = 1.0f)
    {
        return bindMouseAction(toActionId(action), axis, min, max);
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool bindActionAxis(Action action, GamepadButton axis, std::size_t gamepadIndex = 0,
                               float min = 0.0f, float max = 1.0f)
    {
        return bindGamepadAction(toActionId(action), axis, gamepadIndex, min, max);
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool isActionMapped(Action action)
    {
        return isActionMappedById(toActionId(action));
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool isActionDown(Action action)
    {
        return isActionDownById(toActionId(action));
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool isActionUp(Action action)
    {
        return !isActionDown(action);
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool isActionPressed(Action action)
    {
        return isActionPressedById(toActionId(action));
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool isActionReleased(Action action)
    {
        return isActionReleasedById(toActionId(action));
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static float getActionValue(Action action)
    {
        return getActionValueById(toActionId(action));
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static float getActionPreviousValue(Action action)
    {
        return getActionPreviousValueById(toActionId(action));
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static float getActionDelta(Action action)
    {
        return getActionDeltaById(toActionId(action));
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool setActionDeadZone(Action action, float deadZone)
    {
        return setActionDeadZoneById(toActionId(action), deadZone);
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static bool setActionPolicy(Action action, ActionPolicy policy)
    {
        return setActionPolicyById(toActionId(action), policy);
    }

    template <typename Action>
        requires std::is_enum_v<Action>
    static void unbindAction(Action action)
    {
        unbindActionById(toActionId(action));
    }

    static void clearActions();

private:
    template <typename Action>
    static constexpr gainput::UserButtonId toActionId(Action action)
    {
        using Underlying = std::underlying_type_t<Action>;
        return static_cast<gainput::UserButtonId>(static_cast<Underlying>(action));
    }

    static bool bindKeyAction(gainput::UserButtonId action, Key key, float min = 0.0f, float max = 1.0f);
    static bool bindMouseAction(gainput::UserButtonId action, MouseButton button, float min = 0.0f,
                                float max = 1.0f);
    static bool bindGamepadAction(gainput::UserButtonId action, GamepadButton button, std::size_t gamepadIndex,
                                  float min = 0.0f, float max = 1.0f);
    static bool isActionMappedById(gainput::UserButtonId action);
    static bool isActionDownById(gainput::UserButtonId action);
    static bool isActionPressedById(gainput::UserButtonId action);
    static bool isActionReleasedById(gainput::UserButtonId action);
    static float getActionValueById(gainput::UserButtonId action);
    static float getActionPreviousValueById(gainput::UserButtonId action);
    static float getActionDeltaById(gainput::UserButtonId action);
    static bool setActionDeadZoneById(gainput::UserButtonId action, float deadZone);
    static bool setActionPolicyById(gainput::UserButtonId action, ActionPolicy policy);
    static void unbindActionById(gainput::UserButtonId action);
};

} // namespace Haki::Input
