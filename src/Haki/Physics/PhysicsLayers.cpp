#include "Haki/Physics/PhysicsLayers.h"

namespace Haki
{

bool ShouldLayersCollide(PhysicsLayer first, PhysicsLayer second)
{
    if (first == PhysicsLayer::Static || second == PhysicsLayer::Static)
    {
        // Static geometry never needs to test against other static geometry or against triggers.
        const PhysicsLayer other = first == PhysicsLayer::Static ? second : first;
        return other == PhysicsLayer::Dynamic || other == PhysicsLayer::Character;
    }
    if (first == PhysicsLayer::Trigger || second == PhysicsLayer::Trigger)
    {
        const PhysicsLayer other = first == PhysicsLayer::Trigger ? second : first;
        return other == PhysicsLayer::Dynamic || other == PhysicsLayer::Character;
    }
    if (first == PhysicsLayer::Character && second == PhysicsLayer::Character)
    {
        // Brawlers pass through each other; attack queries still hit them through a layer mask.
        return false;
    }
    return first < PhysicsLayer::Count && second < PhysicsLayer::Count;
}

JPH::BroadPhaseLayer BroadPhaseLayerInterfaceImpl::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
{
    return ToPhysicsLayer(layer) == PhysicsLayer::Static ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char *BroadPhaseLayerInterfaceImpl::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const
{
    return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
}
#endif

bool ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(JPH::ObjectLayer layer,
                                                     JPH::BroadPhaseLayer broadPhaseLayer) const
{
    const PhysicsLayer physicsLayer = ToPhysicsLayer(layer);
    if (broadPhaseLayer == BroadPhaseLayers::NonMoving)
    {
        return ShouldLayersCollide(physicsLayer, PhysicsLayer::Static);
    }
    // The moving tree holds dynamic, character and trigger bodies; static bodies never query it.
    return physicsLayer != PhysicsLayer::Static;
}

bool ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const
{
    return ShouldLayersCollide(ToPhysicsLayer(first), ToPhysicsLayer(second));
}

bool ObjectLayerMaskFilter::ShouldCollide(JPH::ObjectLayer layer) const
{
    return LayerMaskContains(mask_, ToPhysicsLayer(layer));
}

} // namespace Haki
