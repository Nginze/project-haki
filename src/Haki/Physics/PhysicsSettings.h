#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace Haki
{

// Per-world settings used when a PhysicsSystem creates its JPH::PhysicsSystem.
struct PhysicsSettings
{
    glm::vec3 Gravity{0.0f, -9.81f, 0.0f};

    // Capacity limits. Jolt preallocates for these, so keep them close to what a scene needs.
    std::uint32_t MaxBodies = 4096;
    std::uint32_t MaxBodyPairs = 4096;
    std::uint32_t MaxContactConstraints = 2048;
    std::uint32_t NumBodyMutexes = 0; // 0 = Jolt default

    // Collision sub-steps per update. Raise it if fast objects tunnel at the 60 Hz tick.
    int CollisionSteps = 1;
};

} // namespace Haki
