#pragma once

// Jolt-free physics vocabulary shared by components, serialization and gameplay code.

#include <cstdint>
#include <limits>

namespace Haki
{

enum class PhysicsMotionType : std::uint8_t
{
    Static,    // never moves (floor, walls, cover)
    Kinematic, // moved by gameplay code through its TransformComponent; pushes dynamic bodies
    Dynamic    // moved by the simulation; its TransformComponent is written back every update
};

// Collision layers. Which layers collide with each other is defined in PhysicsLayers.cpp.
enum class PhysicsLayer : std::uint8_t
{
    Static,
    Dynamic,
    Character,
    Trigger,
    Count
};

enum class ColliderShape : std::uint8_t
{
    Box,
    Sphere,
    Capsule // Y-axis aligned; HalfHeight is the half-length of the cylinder part (excludes the caps)
};

// Bit set of PhysicsLayer values, used to filter scene queries.
using PhysicsLayerMask = std::uint32_t;

constexpr PhysicsLayerMask ToLayerMask(PhysicsLayer layer)
{
    return PhysicsLayerMask{1} << static_cast<std::uint32_t>(layer);
}

constexpr PhysicsLayerMask AllPhysicsLayers = (PhysicsLayerMask{1} << static_cast<std::uint32_t>(PhysicsLayer::Count)) - 1;

constexpr bool LayerMaskContains(PhysicsLayerMask mask, PhysicsLayer layer)
{
    return (mask & ToLayerMask(layer)) != 0;
}

// Matches JPH::BodyID::cInvalidBodyID without including Jolt.
constexpr std::uint32_t InvalidPhysicsBodyId = 0xffffffffu;

} // namespace Haki
