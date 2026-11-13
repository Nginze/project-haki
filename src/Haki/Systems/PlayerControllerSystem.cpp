#include "Haki/Systems/PlayerControllerSystem.h"

#include "Haki/Input/InputManager.h"
#include "Haki/Scene/Components.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Haki
{
namespace
{

constexpr float StickDeadZone = 0.2f;

// Camera forward and right flattened onto the ground plane. Without a primary camera, forward is -Z.
void CameraGroundAxes(const entt::registry &registry, glm::vec3 &forward, glm::vec3 &right)
{
    forward = {0.0f, 0.0f, -1.0f};
    const auto cameras = registry.view<TransformComponent, CameraComponent>();
    for (const entt::entity entity : cameras)
    {
        if (!cameras.get<CameraComponent>(entity).Primary)
        {
            continue;
        }
        const glm::vec3 cameraForward = cameras.get<TransformComponent>(entity).Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 flat(cameraForward.x, 0.0f, cameraForward.z);
        if (const float length = glm::length(flat); length > 0.001f)
        {
            forward = flat / length;
        }
        break;
    }
    right = glm::vec3(-forward.z, 0.0f, forward.x);
}

} // namespace

PlayerMoveInput PlayerControllerSystem::ReadInput()
{
    using Input::InputManager;
    PlayerMoveInput input;
    if (!InputManager::isInitialized())
    {
        return input;
    }

    input.Move.x = static_cast<float>(InputManager::isKeyDown(gainput::KeyD)) -
                   static_cast<float>(InputManager::isKeyDown(gainput::KeyA));
    input.Move.y = static_cast<float>(InputManager::isKeyDown(gainput::KeyW)) -
                   static_cast<float>(InputManager::isKeyDown(gainput::KeyS));

    if (InputManager::isGamepadConnected(0))
    {
        const glm::vec2 stick(InputManager::getGamepadAxis(0, gainput::PadButtonLeftStickX),
                              InputManager::getGamepadAxis(0, gainput::PadButtonLeftStickY));
        if (glm::length(stick) > StickDeadZone)
        {
            input.Move += stick;
        }
    }
    return input;
}

void PlayerControllerSystem::Update(entt::registry &registry, const PlayerMoveInput &input)
{
    glm::vec2 move = std::isfinite(input.Move.x) && std::isfinite(input.Move.y) ? input.Move : glm::vec2(0.0f);
    if (const float length = glm::length(move); length > 1.0f)
    {
        move /= length;
    }

    glm::vec3 forward;
    glm::vec3 right;
    CameraGroundAxes(registry, forward, right);
    const glm::vec3 direction = forward * move.y + right * move.x;
    const float amount = glm::length(direction);

    auto players = registry.view<TransformComponent, CharacterControllerComponent, PlayerInputComponent>();
    for (const entt::entity entity : players)
    {
        const float speed = std::max(players.get<PlayerInputComponent>(entity).MoveSpeed, 0.0f);
        players.get<CharacterControllerComponent>(entity).DesiredVelocity = direction * speed;
        if (amount > 0.001f)
        {
            // Yaw that turns the model's forward (-Z) towards the movement direction.
            const glm::vec3 facing = direction / amount;
            const float yaw = std::atan2(-facing.x, -facing.z);
            players.get<TransformComponent>(entity).Rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }
}

} // namespace Haki
