#include "Haki/Scene/Components.h"
#include "Haki/Systems/PlayerControllerSystem.h"

#include <entt/entt.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <iostream>

namespace
{

bool Near(const glm::vec3 &left, const glm::vec3 &right, float epsilon = 0.0001f)
{
    return glm::length(left - right) <= epsilon;
}

void Check(bool condition, const char *name, int &failures)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += condition ? 0 : 1;
}

entt::entity CreatePlayer(entt::registry &registry, float speed = 5.0f)
{
    const entt::entity entity = registry.create();
    registry.emplace<Haki::TransformComponent>(entity);
    registry.emplace<Haki::CharacterControllerComponent>(entity);
    registry.emplace<Haki::PlayerInputComponent>(entity, speed);
    return entity;
}

void CreateCamera(entt::registry &registry, const glm::quat &rotation)
{
    const entt::entity entity = registry.create();
    registry.emplace<Haki::TransformComponent>(entity).Rotation = rotation;
    registry.emplace<Haki::CameraComponent>(entity).Primary = true;
}

glm::vec3 Desired(entt::registry &registry, entt::entity entity)
{
    return registry.get<Haki::CharacterControllerComponent>(entity).DesiredVelocity;
}

} // namespace

int main()
{
    using namespace Haki;
    int failures = 0;

    {
        entt::registry registry;
        const entt::entity player = CreatePlayer(registry);
        PlayerControllerSystem::Update(registry, {{0.0f, 1.0f}});
        Check(Near(Desired(registry, player), {0.0f, 0.0f, -5.0f}), "without a camera, forward is -Z", failures);
        PlayerControllerSystem::Update(registry, {{1.0f, 1.0f}});
        Check(std::abs(glm::length(Desired(registry, player)) - 5.0f) < 0.0001f,
              "diagonal input is normalized to move speed", failures);
    }
    {
        // Top-down camera looking down -Z at 60 degrees: forward still maps to -Z on the ground.
        entt::registry registry;
        CreateCamera(registry, glm::angleAxis(glm::radians(-60.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
        const entt::entity player = CreatePlayer(registry);
        PlayerControllerSystem::Update(registry, {{0.0f, 1.0f}});
        Check(Near(Desired(registry, player), {0.0f, 0.0f, -5.0f}), "pitched camera forward is flattened to ground",
              failures);
    }
    {
        // Camera turned 90 degrees left (looking down -X): forward input moves -X, right input moves -Z.
        entt::registry registry;
        CreateCamera(registry, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        const entt::entity player = CreatePlayer(registry, 2.0f);
        PlayerControllerSystem::Update(registry, {{0.0f, 1.0f}});
        Check(Near(Desired(registry, player), {-2.0f, 0.0f, 0.0f}), "forward follows the camera heading", failures);
        PlayerControllerSystem::Update(registry, {{1.0f, 0.0f}});
        Check(Near(Desired(registry, player), {0.0f, 0.0f, -2.0f}), "right is relative to the camera", failures);
        const glm::vec3 facing = registry.get<TransformComponent>(player).Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        Check(Near(facing, {0.0f, 0.0f, -1.0f}), "character turns to face its movement", failures);

        PlayerControllerSystem::Update(registry, {});
        Check(Near(Desired(registry, player), glm::vec3(0.0f)), "no input stops the character", failures);
        const glm::vec3 kept = registry.get<TransformComponent>(player).Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        Check(Near(kept, {0.0f, 0.0f, -1.0f}), "facing is kept when idle", failures);
    }
    {
        entt::registry registry;
        const entt::entity npc = registry.create();
        registry.emplace<TransformComponent>(npc);
        registry.emplace<CharacterControllerComponent>(npc);
        PlayerControllerSystem::Update(registry, {{0.0f, 1.0f}});
        Check(Near(Desired(registry, npc), glm::vec3(0.0f)), "characters without PlayerInput are untouched",
              failures);
    }
    Check(glm::length(PlayerControllerSystem::ReadInput().Move) == 0.0f,
          "reading input without an input manager is safe", failures);
    return failures == 0 ? 0 : 1;
}
