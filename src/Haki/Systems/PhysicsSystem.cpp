#include "Haki/Systems/PhysicsSystem.h"

#include "Haki/Logging/Logger.h"
#include "Haki/Physics/PhysicsConversions.h"
#include "Haki/Physics/PhysicsCore.h"
#include "Haki/Physics/PhysicsLayers.h"
#include "Haki/Renderer/DebugDraw.h"
#include "Haki/Scene/Components.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Haki
{
namespace
{

constexpr float MinimumDimension = 0.001f;
constexpr float DefaultBoxConvexRadius = 0.05f;
constexpr float KinematicPositionEpsilon = 1.0e-5f;
constexpr float KinematicRotationEpsilon = 1.0e-6f;

JPH::uint64 ToUserData(entt::entity entity)
{
    return static_cast<JPH::uint64>(entt::to_integral(entity));
}

entt::entity ToEntity(JPH::uint64 userData)
{
    return entt::entity{static_cast<std::underlying_type_t<entt::entity>>(userData)};
}

JPH::EMotionType ToJoltMotion(PhysicsMotionType motion)
{
    switch (motion)
    {
    case PhysicsMotionType::Kinematic: return JPH::EMotionType::Kinematic;
    case PhysicsMotionType::Dynamic: return JPH::EMotionType::Dynamic;
    case PhysicsMotionType::Static: break;
    }
    return JPH::EMotionType::Static;
}

float SafeDimension(float value)
{
    return std::isfinite(value) ? std::max(std::abs(value), MinimumDimension) : MinimumDimension;
}

// Collider dimensions after the transform's scale is baked in. Spheres and capsules can only scale
// uniformly (capsules: radius in XZ, height in Y), so they take the largest relevant axis.
struct ScaledCollider
{
    ColliderShape Shape = ColliderShape::Box;
    glm::vec3 HalfExtents{0.5f};
    float Radius = 0.5f;
    float HalfHeight = 0.5f;
    glm::vec3 Offset{0.0f};
};

ScaledCollider ScaleCollider(const ColliderComponent &collider, const glm::vec3 &transformScale)
{
    const glm::vec3 scale = glm::abs(transformScale);
    ScaledCollider result;
    result.Shape = collider.Shape;
    result.Offset = collider.Offset * transformScale;
    result.HalfExtents = {SafeDimension(collider.HalfExtents.x * scale.x),
                          SafeDimension(collider.HalfExtents.y * scale.y),
                          SafeDimension(collider.HalfExtents.z * scale.z)};
    switch (collider.Shape)
    {
    case ColliderShape::Sphere:
        result.Radius = SafeDimension(collider.Radius * std::max({scale.x, scale.y, scale.z}));
        break;
    case ColliderShape::Capsule:
        result.Radius = SafeDimension(collider.Radius * std::max(scale.x, scale.z));
        result.HalfHeight = SafeDimension(collider.HalfHeight * scale.y);
        break;
    case ColliderShape::Box: break;
    }
    return result;
}

JPH::RefConst<JPH::Shape> CreateShape(const ScaledCollider &collider)
{
    JPH::RefConst<JPH::Shape> shape;
    switch (collider.Shape)
    {
    case ColliderShape::Box:
    {
        const float smallest = std::min({collider.HalfExtents.x, collider.HalfExtents.y, collider.HalfExtents.z});
        shape = new JPH::BoxShape(ToJolt(collider.HalfExtents), std::min(DefaultBoxConvexRadius, smallest * 0.5f));
        break;
    }
    case ColliderShape::Sphere: shape = new JPH::SphereShape(collider.Radius); break;
    case ColliderShape::Capsule: shape = new JPH::CapsuleShape(collider.HalfHeight, collider.Radius); break;
    }

    if (glm::dot(collider.Offset, collider.Offset) > 0.0f)
    {
        shape = new JPH::RotatedTranslatedShape(ToJolt(collider.Offset), JPH::Quat::sIdentity(), shape);
    }
    return shape;
}

// Upright capsule with its bottom at the local origin, so the entity origin sits at the character's feet.
JPH::RefConst<JPH::Shape> CreateCharacterShape(const CharacterControllerComponent &character)
{
    const float radius = SafeDimension(character.Radius);
    const float halfHeight = SafeDimension(character.HalfHeight);
    return new JPH::RotatedTranslatedShape(JPH::Vec3(0.0f, halfHeight + radius, 0.0f), JPH::Quat::sIdentity(),
                                           new JPH::CapsuleShape(halfHeight, radius));
}

void DrawCapsule(DebugDraw &draw, const glm::vec3 &center, const glm::quat &rotation, float halfHeight, float radius,
                 const glm::vec4 &color)
{
    const glm::vec3 up = rotation * glm::vec3(0.0f, halfHeight, 0.0f);
    const glm::vec3 side = rotation * glm::vec3(radius, 0.0f, 0.0f);
    const glm::vec3 front = rotation * glm::vec3(0.0f, 0.0f, radius);
    draw.Sphere(center + up, radius, color);
    draw.Sphere(center - up, radius, color);
    for (const glm::vec3 &edge : {side, -side, front, -front})
    {
        draw.Line(center - up + edge, center + up + edge, color);
    }
}

glm::quat SafeRotation(const glm::quat &rotation)
{
    const float length = glm::length(rotation);
    return std::isfinite(length) && length > 0.00001f ? rotation / length : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

// Jolt calls this from its worker threads during PhysicsSystem::Update, so it only queues raw body IDs.
class ContactCollector final : public JPH::ContactListener
{
public:
    struct RawEvent
    {
        PhysicsContactType Type;
        JPH::BodyID First;
        JPH::BodyID Second;
        bool IsSensor;
    };

    void OnContactAdded(const JPH::Body &first, const JPH::Body &second, const JPH::ContactManifold &,
                        JPH::ContactSettings &) override
    {
        Push({PhysicsContactType::Began, first.GetID(), second.GetID(), first.IsSensor() || second.IsSensor()});
    }

    void OnContactRemoved(const JPH::SubShapeIDPair &pair) override
    {
        Push({PhysicsContactType::Ended, pair.GetBody1ID(), pair.GetBody2ID(), false});
    }

    std::vector<RawEvent> Take()
    {
        std::lock_guard lock(mutex_);
        std::vector<RawEvent> events = std::move(events_);
        events_.clear();
        return events;
    }

private:
    void Push(const RawEvent &event)
    {
        std::lock_guard lock(mutex_);
        events_.push_back(event);
    }

    std::mutex mutex_;
    std::vector<RawEvent> events_;
};

} // namespace

struct PhysicsSystem::State
{
    // The layer interfaces must outlive the JPH::PhysicsSystem that references them.
    BroadPhaseLayerInterfaceImpl BroadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl ObjectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl ObjectLayerPairFilter;
    ContactCollector Contacts;
    JPH::PhysicsSystem World;

    struct BodyRecord
    {
        entt::entity Entity = entt::null;
        bool IsSensor = false;
    };
    std::unordered_map<JPH::uint32, BodyRecord> Bodies;
    std::vector<PhysicsContactEvent> ContactEvents;

    struct CharacterRecord
    {
        JPH::Ref<JPH::CharacterVirtual> Character;
        glm::vec3 LastPosition{0.0f}; // last position written to the transform, to detect teleports
    };
    std::unordered_map<entt::entity, CharacterRecord> Characters;
    std::unordered_set<entt::entity> RejectedCharacters; // logged once: character mixed with a rigid body

    JPH::BodyInterface &Interface() { return World.GetBodyInterface(); }
    const JPH::BodyInterface &Interface() const { return World.GetBodyInterface(); }
};

PhysicsSystem::PhysicsSystem(PhysicsSettings settings) : settings_(settings)
{
}

PhysicsSystem::~PhysicsSystem()
{
    Shutdown();
}

bool PhysicsSystem::Initialize(entt::registry &registry)
{
    if (state_)
    {
        return registry_ == &registry;
    }
    if (!PhysicsCore::IsInitialized())
    {
        Logger::Error() << "PhysicsSystem requires PhysicsCore::Initialize() first";
        return false;
    }

    state_ = std::make_unique<State>();
    state_->World.Init(settings_.MaxBodies, settings_.NumBodyMutexes, settings_.MaxBodyPairs,
                       settings_.MaxContactConstraints, state_->BroadPhaseLayerInterface,
                       state_->ObjectVsBroadPhaseLayerFilter, state_->ObjectLayerPairFilter);
    state_->World.SetGravity(ToJolt(settings_.Gravity));
    state_->World.SetContactListener(&state_->Contacts);

    registry_ = &registry;
    registry.on_destroy<RigidBodyComponent>().connect<&PhysicsSystem::OnRigidBodyDestroyed>(*this);
    registry.on_destroy<ColliderComponent>().connect<&PhysicsSystem::OnColliderDestroyed>(*this);
    registry.on_destroy<CharacterControllerComponent>().connect<&PhysicsSystem::OnCharacterDestroyed>(*this);
    return true;
}

void PhysicsSystem::Shutdown()
{
    if (!state_)
    {
        return;
    }

    if (registry_)
    {
        registry_->on_destroy<RigidBodyComponent>().disconnect<&PhysicsSystem::OnRigidBodyDestroyed>(*this);
        registry_->on_destroy<ColliderComponent>().disconnect<&PhysicsSystem::OnColliderDestroyed>(*this);
        registry_->on_destroy<CharacterControllerComponent>().disconnect<&PhysicsSystem::OnCharacterDestroyed>(*this);
        auto bodies = registry_->view<RigidBodyComponent>();
        for (const entt::entity entity : bodies)
        {
            bodies.get<RigidBodyComponent>(entity).BodyId = InvalidPhysicsBodyId;
        }
        auto characters = registry_->view<CharacterControllerComponent>();
        for (const entt::entity entity : characters)
        {
            characters.get<CharacterControllerComponent>(entity).InnerBodyId = InvalidPhysicsBodyId;
        }
    }

    // Characters destroy their own inner bodies, so release them before the bodies below and the world.
    for (const auto &[entity, record] : state_->Characters)
    {
        (void)entity;
        state_->Bodies.erase(record.Character->GetInnerBodyID().GetIndexAndSequenceNumber());
    }
    state_->Characters.clear();

    JPH::BodyInterface &bodyInterface = state_->Interface();
    for (const auto &[id, record] : state_->Bodies)
    {
        (void)record;
        const JPH::BodyID bodyId(id);
        bodyInterface.RemoveBody(bodyId);
        bodyInterface.DestroyBody(bodyId);
    }
    state_.reset();
    registry_ = nullptr;
}

void PhysicsSystem::CreatePendingBodies(entt::registry &registry)
{
    JPH::BodyInterface &bodyInterface = state_->Interface();
    auto view = registry.view<TransformComponent, RigidBodyComponent, ColliderComponent>();
    for (const entt::entity entity : view)
    {
        auto &body = view.get<RigidBodyComponent>(entity);
        if (body.BodyId != InvalidPhysicsBodyId)
        {
            continue;
        }
        const auto &transform = view.get<TransformComponent>(entity);
        const auto &collider = view.get<ColliderComponent>(entity);

        JPH::BodyCreationSettings settings(CreateShape(ScaleCollider(collider, transform.Scale)),
                                           ToJolt(transform.Translation), ToJolt(SafeRotation(transform.Rotation)),
                                           ToJoltMotion(body.Motion), ToObjectLayer(body.Layer));
        settings.mUserData = ToUserData(entity);
        settings.mIsSensor = body.IsSensor;
        settings.mFriction = body.Friction;
        settings.mRestitution = body.Restitution;
        settings.mLinearDamping = body.LinearDamping;
        settings.mAngularDamping = body.AngularDamping;
        settings.mGravityFactor = body.GravityFactor;
        if (body.Motion == PhysicsMotionType::Dynamic && body.Mass > 0.0f && std::isfinite(body.Mass))
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = body.Mass;
        }
        // Lets scripts switch movable bodies between dynamic and kinematic later (SetMotionType).
        settings.mAllowDynamicOrKinematic = body.Motion != PhysicsMotionType::Static;
        if (body.Motion == PhysicsMotionType::Dynamic && body.LockRotation)
        {
            settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
                                    JPH::EAllowedDOFs::TranslationZ;
        }
        if (body.Motion != PhysicsMotionType::Static && body.IsSensor)
        {
            // Lets moving sensors detect static and kinematic bodies too.
            settings.mCollideKinematicVsNonDynamic = true;
        }

        const JPH::EActivation activation = body.Motion == PhysicsMotionType::Static ? JPH::EActivation::DontActivate
                                                                                    : JPH::EActivation::Activate;
        const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, activation);
        if (bodyId.IsInvalid())
        {
            Logger::Error() << "Physics body limit reached (" << settings_.MaxBodies << "); entity "
                            << entt::to_integral(entity) << " has no body";
            continue;
        }
        body.BodyId = bodyId.GetIndexAndSequenceNumber();
        state_->Bodies[body.BodyId] = {entity, body.IsSensor};
    }
}

void PhysicsSystem::Update(entt::registry &registry, float deltaTime)
{
    if (!state_ || &registry != registry_)
    {
        return;
    }

    CreatePendingBodies(registry);
    CreatePendingCharacters(registry);
    if (!(deltaTime > 0.0f) || !std::isfinite(deltaTime))
    {
        return;
    }

    JPH::BodyInterface &bodyInterface = state_->Interface();
    auto bodies = registry.view<TransformComponent, RigidBodyComponent>();
    for (const entt::entity entity : bodies)
    {
        const auto &body = bodies.get<RigidBodyComponent>(entity);
        if (body.Motion != PhysicsMotionType::Kinematic || body.BodyId == InvalidPhysicsBodyId)
        {
            continue;
        }
        const auto &transform = bodies.get<TransformComponent>(entity);
        const JPH::BodyID bodyId(body.BodyId);
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodyInterface.GetPositionAndRotation(bodyId, position, rotation);
        const JPH::Vec3 targetPosition = ToJolt(transform.Translation);
        const JPH::Quat targetRotation = ToJolt(SafeRotation(transform.Rotation));
        const bool moved = (targetPosition - position).LengthSq() > KinematicPositionEpsilon * KinematicPositionEpsilon ||
                           1.0f - std::abs(targetRotation.Dot(rotation)) > KinematicRotationEpsilon;
        if (moved || bodyInterface.IsActive(bodyId))
        {
            // Called while the body is still active too, so it stops (zero velocity) once the target holds.
            bodyInterface.MoveKinematic(bodyId, targetPosition, targetRotation, deltaTime);
        }
    }

    UpdateCharacters(registry, deltaTime);

    const JPH::EPhysicsUpdateError error = state_->World.Update(
        deltaTime, std::max(settings_.CollisionSteps, 1), &PhysicsCore::GetTempAllocator(), &PhysicsCore::GetJobSystem());
    if (error != JPH::EPhysicsUpdateError::None)
    {
        Logger::Warning() << "Physics update overflowed its limits (error flags " << static_cast<int>(error)
                          << "); raise PhysicsSettings capacities";
    }

    for (const entt::entity entity : bodies)
    {
        const auto &body = bodies.get<RigidBodyComponent>(entity);
        if (body.Motion != PhysicsMotionType::Dynamic || body.BodyId == InvalidPhysicsBodyId)
        {
            continue;
        }
        const JPH::BodyID bodyId(body.BodyId);
        if (!bodyInterface.IsActive(bodyId))
        {
            continue;
        }
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodyInterface.GetPositionAndRotation(bodyId, position, rotation);
        auto &transform = bodies.get<TransformComponent>(entity);
        transform.Translation = ToGlm(position);
        transform.Rotation = ToGlm(rotation);
    }

    state_->ContactEvents.clear();
    for (const ContactCollector::RawEvent &raw : state_->Contacts.Take())
    {
        const auto first = state_->Bodies.find(raw.First.GetIndexAndSequenceNumber());
        const auto second = state_->Bodies.find(raw.Second.GetIndexAndSequenceNumber());
        if (first == state_->Bodies.end() || second == state_->Bodies.end())
        {
            continue;
        }
        state_->ContactEvents.push_back({raw.Type, first->second.Entity, second->second.Entity,
                                         raw.IsSensor || first->second.IsSensor || second->second.IsSensor});
    }
}

const std::vector<PhysicsContactEvent> &PhysicsSystem::GetContactEvents() const
{
    static const std::vector<PhysicsContactEvent> empty;
    return state_ ? state_->ContactEvents : empty;
}

void PhysicsSystem::CreatePendingCharacters(entt::registry &registry)
{
    auto view = registry.view<TransformComponent, CharacterControllerComponent>();
    for (const entt::entity entity : view)
    {
        if (state_->Characters.contains(entity))
        {
            continue;
        }
        if (registry.any_of<RigidBodyComponent, ColliderComponent>(entity))
        {
            if (state_->RejectedCharacters.insert(entity).second)
            {
                Logger::Warning() << "Entity " << entt::to_integral(entity)
                                  << " has a CharacterController and a RigidBody/Collider; the character is ignored";
            }
            continue;
        }

        auto &controller = view.get<CharacterControllerComponent>(entity);
        const auto &transform = view.get<TransformComponent>(entity);
        const JPH::RefConst<JPH::Shape> shape = CreateCharacterShape(controller);

        JPH::CharacterVirtualSettings settings;
        settings.mShape = shape;
        settings.mInnerBodyShape = shape;
        settings.mInnerBodyLayer = ToObjectLayer(PhysicsLayer::Character);
        settings.mMaxSlopeAngle = glm::radians(std::clamp(controller.MaxSlopeDegrees, 0.0f, 89.0f));
        settings.mMass = std::max(controller.Mass, 0.0f);
        settings.mMaxStrength = std::max(controller.MaxStrength, 0.0f);
        // Only the bottom hemisphere counts as standing on something.
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -SafeDimension(controller.Radius));

        // The character stays upright; gameplay owns the transform's rotation (facing).
        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            &settings, ToJolt(transform.Translation), JPH::Quat::sIdentity(), ToUserData(entity), &state_->World);

        const JPH::BodyID innerBody = character->GetInnerBodyID();
        {
            // Jolt moves the inner body without activating it, and sensors only see active bodies.
            // Keeping it awake lets characters trigger sensors such as the danger zone.
            const JPH::BodyLockWrite lock(state_->World.GetBodyLockInterface(), innerBody);
            if (lock.Succeeded())
            {
                lock.GetBody().SetAllowSleeping(false);
            }
        }
        state_->Interface().ActivateBody(innerBody);

        controller.InnerBodyId = innerBody.GetIndexAndSequenceNumber();
        controller.Velocity = glm::vec3(0.0f);
        controller.Grounded = false;
        state_->Bodies[controller.InnerBodyId] = {entity, false};
        state_->Characters[entity] = {std::move(character), transform.Translation};
    }
}

void PhysicsSystem::UpdateCharacters(entt::registry &registry, float deltaTime)
{
    const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(state_->ObjectVsBroadPhaseLayerFilter,
                                                             ToObjectLayer(PhysicsLayer::Character));
    const JPH::DefaultObjectLayerFilter objectLayerFilter(state_->ObjectLayerPairFilter,
                                                          ToObjectLayer(PhysicsLayer::Character));
    const JPH::BodyFilter bodyFilter;
    const JPH::ShapeFilter shapeFilter;
    const JPH::Vec3 gravity = state_->World.GetGravity();

    for (auto &[entity, record] : state_->Characters)
    {
        auto *transform = registry.try_get<TransformComponent>(entity);
        auto *controller = registry.try_get<CharacterControllerComponent>(entity);
        if (!transform || !controller)
        {
            continue;
        }
        JPH::CharacterVirtual &character = *record.Character;

        // Gameplay moved the transform directly (spawn, respawn, knock-back teleport).
        if (transform->Translation != record.LastPosition)
        {
            character.SetPosition(ToJolt(transform->Translation));
            character.SetLinearVelocity(JPH::Vec3::sZero());
        }

        const glm::vec3 desired = std::isfinite(controller->DesiredVelocity.x) &&
                                          std::isfinite(controller->DesiredVelocity.z)
                                      ? glm::vec3(controller->DesiredVelocity.x, 0.0f, controller->DesiredVelocity.z)
                                      : glm::vec3(0.0f);
        const JPH::Vec3 characterGravity = gravity * controller->GravityFactor;

        character.UpdateGroundVelocity();
        const JPH::Vec3 groundVelocity = character.GetGroundVelocity();
        const JPH::Vec3 currentVelocity = character.GetLinearVelocity();
        const bool movingAwayFromGround = (currentVelocity.GetY() - groundVelocity.GetY()) >= 0.1f;

        JPH::Vec3 velocity;
        if (character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && !movingAwayFromGround)
        {
            velocity = groundVelocity + ToJolt(desired);
        }
        else
        {
            velocity = JPH::Vec3(desired.x, currentVelocity.GetY(), desired.z);
        }
        velocity += characterGravity * deltaTime;
        character.SetLinearVelocity(velocity);

        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(controller->StepHeight, 0.0f), 0.0f);
        character.ExtendedUpdate(deltaTime, characterGravity, updateSettings, broadPhaseFilter, objectLayerFilter,
                                 bodyFilter, shapeFilter, PhysicsCore::GetTempAllocator());

        transform->Translation = ToGlm(character.GetPosition());
        record.LastPosition = transform->Translation;
        controller->Velocity = ToGlm(character.GetLinearVelocity());
        controller->Grounded = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        controller->GroundNormal = controller->Grounded ? ToGlm(character.GetGroundNormal()) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void PhysicsSystem::OnCharacterDestroyed(entt::registry &registry, entt::entity entity)
{
    if (!state_)
    {
        return;
    }
    state_->RejectedCharacters.erase(entity);
    const auto found = state_->Characters.find(entity);
    if (found == state_->Characters.end())
    {
        return;
    }
    state_->Bodies.erase(found->second.Character->GetInnerBodyID().GetIndexAndSequenceNumber());
    state_->Characters.erase(found); // releasing the Ref destroys the character and its inner body
    if (auto *controller = registry.try_get<CharacterControllerComponent>(entity))
    {
        controller->InnerBodyId = InvalidPhysicsBodyId;
    }
}

void PhysicsSystem::DestroyBody(entt::registry &registry, entt::entity entity)
{
    if (!state_)
    {
        return;
    }
    auto *body = registry.try_get<RigidBodyComponent>(entity);
    if (!body || body->BodyId == InvalidPhysicsBodyId)
    {
        return;
    }
    const JPH::BodyID bodyId(body->BodyId);
    JPH::BodyInterface &bodyInterface = state_->Interface();
    bodyInterface.RemoveBody(bodyId);
    bodyInterface.DestroyBody(bodyId);
    state_->Bodies.erase(body->BodyId);
    body->BodyId = InvalidPhysicsBodyId;
}

void PhysicsSystem::OnRigidBodyDestroyed(entt::registry &registry, entt::entity entity)
{
    DestroyBody(registry, entity);
}

void PhysicsSystem::OnColliderDestroyed(entt::registry &registry, entt::entity entity)
{
    DestroyBody(registry, entity);
}

std::optional<RaycastHit> PhysicsSystem::Raycast(const glm::vec3 &origin, const glm::vec3 &direction,
                                                 float maxDistance, PhysicsLayerMask layers) const
{
    const float length = glm::length(direction);
    if (!state_ || !std::isfinite(length) || length <= 0.0f || !(maxDistance > 0.0f) || !std::isfinite(maxDistance))
    {
        return std::nullopt;
    }

    const glm::vec3 unitDirection = direction / length;
    const JPH::RRayCast ray(ToJolt(origin), ToJolt(unitDirection * maxDistance));
    JPH::RayCastResult result;
    const ObjectLayerMaskFilter layerFilter(layers);
    if (!state_->World.GetNarrowPhaseQuery().CastRay(ray, result, {}, layerFilter))
    {
        return std::nullopt;
    }

    RaycastHit hit;
    hit.Distance = result.mFraction * maxDistance;
    hit.Point = origin + unitDirection * hit.Distance;
    const JPH::BodyLockRead lock(state_->World.GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
    {
        const JPH::Body &body = lock.GetBody();
        hit.Entity = ToEntity(body.GetUserData());
        hit.Normal = ToGlm(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ToJolt(hit.Point)));
    }
    return hit;
}

void PhysicsSystem::OverlapSphere(const glm::vec3 &center, float radius, std::vector<entt::entity> &out,
                                  PhysicsLayerMask layers) const
{
    if (!state_ || !(radius > 0.0f) || !std::isfinite(radius))
    {
        return;
    }

    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const ObjectLayerMaskFilter layerFilter(layers);
    state_->World.GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sOne(),
                                                     JPH::RMat44::sTranslation(ToJolt(center)),
                                                     JPH::CollideShapeSettings(), JPH::RVec3::sZero(), collector, {},
                                                     layerFilter);

    const std::size_t firstNew = out.size();
    const JPH::BodyInterface &bodyInterface = state_->Interface();
    for (const JPH::CollideShapeResult &result : collector.mHits)
    {
        const entt::entity entity = ToEntity(bodyInterface.GetUserData(result.mBodyID2));
        if (std::find(out.begin() + static_cast<std::ptrdiff_t>(firstNew), out.end(), entity) == out.end())
        {
            out.push_back(entity);
        }
    }
}

bool PhysicsSystem::AddImpulse(const entt::registry &registry, entt::entity entity, const glm::vec3 &impulse)
{
    const auto *body = state_ ? registry.try_get<RigidBodyComponent>(entity) : nullptr;
    if (!body || body->BodyId == InvalidPhysicsBodyId || body->Motion != PhysicsMotionType::Dynamic)
    {
        return false;
    }
    state_->Interface().AddImpulse(JPH::BodyID(body->BodyId), ToJolt(impulse));
    return true;
}

bool PhysicsSystem::SetLinearVelocity(const entt::registry &registry, entt::entity entity, const glm::vec3 &velocity)
{
    const auto *body = state_ ? registry.try_get<RigidBodyComponent>(entity) : nullptr;
    if (!body || body->BodyId == InvalidPhysicsBodyId || body->Motion == PhysicsMotionType::Static)
    {
        return false;
    }
    state_->Interface().SetLinearVelocity(JPH::BodyID(body->BodyId), ToJolt(velocity));
    return true;
}

std::optional<glm::vec3> PhysicsSystem::GetLinearVelocity(const entt::registry &registry, entt::entity entity) const
{
    const auto *body = state_ ? registry.try_get<RigidBodyComponent>(entity) : nullptr;
    if (!body || body->BodyId == InvalidPhysicsBodyId)
    {
        return std::nullopt;
    }
    return ToGlm(state_->Interface().GetLinearVelocity(JPH::BodyID(body->BodyId)));
}

bool PhysicsSystem::Teleport(const entt::registry &registry, entt::entity entity)
{
    const auto *body = state_ ? registry.try_get<RigidBodyComponent>(entity) : nullptr;
    const auto *transform = registry.try_get<TransformComponent>(entity);
    if (!body || !transform || body->BodyId == InvalidPhysicsBodyId)
    {
        return false;
    }
    JPH::BodyInterface &bodyInterface = state_->Interface();
    const JPH::BodyID bodyId(body->BodyId);
    const bool movable = body->Motion != PhysicsMotionType::Static;
    bodyInterface.SetPositionAndRotation(bodyId, ToJolt(transform->Translation), ToJolt(SafeRotation(transform->Rotation)),
                                         movable ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (body->Motion == PhysicsMotionType::Dynamic)
    {
        bodyInterface.SetLinearAndAngularVelocity(bodyId, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    }
    return true;
}

bool PhysicsSystem::ApplyBodyMaterial(const entt::registry &registry, entt::entity entity)
{
    const auto *body = state_ ? registry.try_get<RigidBodyComponent>(entity) : nullptr;
    if (!body || body->BodyId == InvalidPhysicsBodyId)
    {
        return false;
    }
    JPH::BodyInterface &bodyInterface = state_->Interface();
    const JPH::BodyID bodyId(body->BodyId);
    const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    bodyInterface.SetFriction(bodyId, std::max(finiteOr(body->Friction, 0.5f), 0.0f));
    bodyInterface.SetRestitution(bodyId, std::clamp(finiteOr(body->Restitution, 0.0f), 0.0f, 1.0f));
    if (body->Motion == PhysicsMotionType::Dynamic)
    {
        bodyInterface.SetGravityFactor(bodyId, finiteOr(body->GravityFactor, 1.0f));
        bodyInterface.ActivateBody(bodyId);
    }
    return true;
}

bool PhysicsSystem::SetMotionType(entt::registry &registry, entt::entity entity, PhysicsMotionType motion)
{
    auto *body = state_ ? registry.try_get<RigidBodyComponent>(entity) : nullptr;
    if (!body || body->BodyId == InvalidPhysicsBodyId || body->Motion == PhysicsMotionType::Static ||
        motion == PhysicsMotionType::Static)
    {
        return false;
    }
    if (body->Motion == motion)
    {
        return true;
    }
    JPH::BodyInterface &bodyInterface = state_->Interface();
    const JPH::BodyID bodyId(body->BodyId);
    if (motion == PhysicsMotionType::Kinematic)
    {
        bodyInterface.SetLinearAndAngularVelocity(bodyId, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    }
    bodyInterface.SetMotionType(bodyId, ToJoltMotion(motion), JPH::EActivation::Activate);
    body->Motion = motion;
    return true;
}

bool PhysicsSystem::IsBodyAwake(const entt::registry &registry, entt::entity entity) const
{
    const auto *body = state_ ? registry.try_get<RigidBodyComponent>(entity) : nullptr;
    return body && body->BodyId != InvalidPhysicsBodyId && state_->Interface().IsActive(JPH::BodyID(body->BodyId));
}

void PhysicsSystem::DrawColliders(const entt::registry &registry, DebugDraw &draw,
                                  const ColliderDrawFilter &filter) const
{
    static const glm::vec4 StaticColor{0.6f, 0.6f, 0.6f, 1.0f};
    static const glm::vec4 DynamicColor{0.2f, 1.0f, 0.3f, 1.0f};
    static const glm::vec4 SleepingColor{0.3f, 0.5f, 1.0f, 1.0f};
    static const glm::vec4 KinematicColor{1.0f, 0.85f, 0.2f, 1.0f};
    static const glm::vec4 SensorColor{1.0f, 0.3f, 1.0f, 1.0f};
    static const glm::vec4 CharacterColor{0.2f, 0.9f, 1.0f, 1.0f};

    auto view = registry.view<TransformComponent, RigidBodyComponent, ColliderComponent>();
    for (const entt::entity entity : view)
    {
        const auto &transform = view.get<TransformComponent>(entity);
        const auto &body = view.get<RigidBodyComponent>(entity);
        const ScaledCollider collider = ScaleCollider(view.get<ColliderComponent>(entity), transform.Scale);

        if (body.IsSensor ? !filter.Sensors : !filter.Bodies)
        {
            continue;
        }
        glm::vec4 color = StaticColor;
        if (body.IsSensor)
        {
            color = SensorColor;
        }
        else if (body.Motion == PhysicsMotionType::Kinematic)
        {
            color = KinematicColor;
        }
        else if (body.Motion == PhysicsMotionType::Dynamic)
        {
            const bool awake = state_ && body.BodyId != InvalidPhysicsBodyId &&
                               state_->Interface().IsActive(JPH::BodyID(body.BodyId));
            color = awake ? DynamicColor : SleepingColor;
        }

        const glm::quat rotation = SafeRotation(transform.Rotation);
        const glm::mat4 pose = glm::translate(glm::mat4(1.0f), transform.Translation) * glm::mat4_cast(rotation);
        const glm::vec3 center = glm::vec3(pose * glm::vec4(collider.Offset, 1.0f));
        switch (collider.Shape)
        {
        case ColliderShape::Box: draw.Box(pose, collider.Offset, collider.HalfExtents, color); break;
        case ColliderShape::Sphere: draw.Sphere(center, collider.Radius, color); break;
        case ColliderShape::Capsule:
            DrawCapsule(draw, center, rotation, collider.HalfHeight, collider.Radius, color);
            break;
        }
    }

    if (!filter.Characters)
    {
        return;
    }
    static const glm::vec4 DesiredColor{1.0f, 1.0f, 1.0f, 1.0f};
    static const glm::vec4 GroundColor{0.3f, 1.0f, 0.3f, 1.0f};
    auto characters = registry.view<TransformComponent, CharacterControllerComponent>();
    for (const entt::entity entity : characters)
    {
        const auto &controller = characters.get<CharacterControllerComponent>(entity);
        const float radius = SafeDimension(controller.Radius);
        const float halfHeight = SafeDimension(controller.HalfHeight);
        const glm::vec3 feet = characters.get<TransformComponent>(entity).Translation;
        DrawCapsule(draw, feet + glm::vec3(0.0f, halfHeight + radius, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                    halfHeight, radius, CharacterColor);
        // Motion of the CharacterVirtual sweep: what gameplay asked for vs. what the character did.
        const glm::vec3 waist = feet + glm::vec3(0.0f, halfHeight + radius, 0.0f);
        if (glm::dot(controller.DesiredVelocity, controller.DesiredVelocity) > 0.0001f)
        {
            draw.Line(waist, waist + controller.DesiredVelocity * 0.25f, DesiredColor, false);
        }
        if (glm::dot(controller.Velocity, controller.Velocity) > 0.0001f)
        {
            draw.Line(feet, feet + controller.Velocity * 0.25f, CharacterColor, false);
        }
        if (controller.Grounded)
        {
            draw.Line(feet, feet + controller.GroundNormal * 0.6f, GroundColor, false);
        }
    }
}

std::size_t PhysicsSystem::GetBodyCount() const
{
    return state_ ? state_->World.GetNumBodies() : 0;
}

std::size_t PhysicsSystem::GetCharacterCount() const
{
    return state_ ? state_->Characters.size() : 0;
}

} // namespace Haki
