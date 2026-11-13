#pragma once

#include <entt/entt.hpp>

#include <glm/vec2.hpp>

namespace Haki
{

struct PlayerMoveInput
{
    // x = right, y = forward (up the screen); length is clamped to 1.
    glm::vec2 Move{0.0f};
};

// Stand-in local player control until gameplay scripting exists. Turns stick/WASD input into
// CharacterControllerComponent::DesiredVelocity for every entity with a PlayerInputComponent, relative to the
// primary scene camera's heading, and turns the character to face where it moves.
class PlayerControllerSystem
{
public:
    // WASD plus gamepad 0's left stick. Returns zero input when the input manager is not initialized.
    static PlayerMoveInput ReadInput();

    static void Update(entt::registry &registry, const PlayerMoveInput &input);
};

} // namespace Haki
