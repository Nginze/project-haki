#pragma once

#include "Haki/Physics/PhysicsTypes.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace Haki
{

// Jolt sorts bodies into a few broad-phase trees. Static geometry gets its own tree because it rarely
// changes; everything that moves or triggers shares the other.
namespace BroadPhaseLayers
{
inline constexpr JPH::BroadPhaseLayer NonMoving{0};
inline constexpr JPH::BroadPhaseLayer Moving{1};
inline constexpr JPH::uint Count = 2;
} // namespace BroadPhaseLayers

inline JPH::ObjectLayer ToObjectLayer(PhysicsLayer layer)
{
    return static_cast<JPH::ObjectLayer>(layer);
}

inline PhysicsLayer ToPhysicsLayer(JPH::ObjectLayer layer)
{
    return static_cast<PhysicsLayer>(layer);
}

// The collision matrix: which object layers can touch each other.
bool ShouldLayersCollide(PhysicsLayer first, PhysicsLayer second);

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override;
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override;
};

// Restricts scene queries to the layers in a PhysicsLayerMask.
class ObjectLayerMaskFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit ObjectLayerMaskFilter(PhysicsLayerMask mask) : mask_(mask) {}
    bool ShouldCollide(JPH::ObjectLayer layer) const override;

private:
    PhysicsLayerMask mask_;
};

} // namespace Haki
