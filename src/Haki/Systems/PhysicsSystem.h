#pragma once

#include "Haki/Physics/PhysicsSettings.h"
#include "Haki/Physics/PhysicsTypes.h"

#include <entt/entt.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Haki
{

class DebugDraw;

struct RaycastHit
{
    entt::entity Entity = entt::null;
    glm::vec3 Point{0.0f};
    glm::vec3 Normal{0.0f};
    float Distance = 0.0f;
};

enum class PhysicsContactType : std::uint8_t
{
    Began,
    Ended
};

struct PhysicsContactEvent
{
    PhysicsContactType Type = PhysicsContactType::Began;
    entt::entity First = entt::null;
    entt::entity Second = entt::null;
    bool IsSensor = false; // at least one of the two bodies is a sensor
};

// Scene queries skip trigger volumes unless asked for them explicitly.
constexpr PhysicsLayerMask DefaultQueryLayers = AllPhysicsLayers & ~ToLayerMask(PhysicsLayer::Trigger);

// Owns one Jolt physics world and keeps it in sync with the ECS:
//  - entities with Transform + RigidBody + Collider get a Jolt body on the next Update;
//  - kinematic bodies follow their TransformComponent;
//  - dynamic bodies write their simulated pose back to their TransformComponent;
//  - removing either component (or destroying the entity) destroys the body;
//  - entities with Transform + CharacterController get a Jolt CharacterVirtual (feet at the entity origin)
//    plus a kinematic inner capsule body that raycasts, overlaps and sensors can see.
// Changing collider or rigid body values after creation needs the component to be removed and re-added.
// Requires PhysicsCore to be initialized. Has no window or GL dependency.
class PhysicsSystem
{
public:
    explicit PhysicsSystem(PhysicsSettings settings = {});
    ~PhysicsSystem();

    PhysicsSystem(const PhysicsSystem &) = delete;
    PhysicsSystem &operator=(const PhysicsSystem &) = delete;
    PhysicsSystem(PhysicsSystem &&) = delete;
    PhysicsSystem &operator=(PhysicsSystem &&) = delete;

    // The registry must outlive this system, or Shutdown must be called before it is destroyed.
    bool Initialize(entt::registry &registry);
    void Shutdown();
    bool IsInitialized() const { return state_ != nullptr; }

    // Creates pending bodies, then steps the simulation when deltaTime > 0.
    void Update(entt::registry &registry, float deltaTime);

    // Contact begin/end events from the most recent step.
    const std::vector<PhysicsContactEvent> &GetContactEvents() const;

    std::optional<RaycastHit> Raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                      PhysicsLayerMask layers = DefaultQueryLayers) const;
    // Appends each entity whose collider overlaps the sphere (no duplicates). Use for melee/attack hits.
    void OverlapSphere(const glm::vec3 &center, float radius, std::vector<entt::entity> &out,
                       PhysicsLayerMask layers = DefaultQueryLayers) const;

    bool AddImpulse(const entt::registry &registry, entt::entity entity, const glm::vec3 &impulse);
    bool SetLinearVelocity(const entt::registry &registry, entt::entity entity, const glm::vec3 &velocity);
    std::optional<glm::vec3> GetLinearVelocity(const entt::registry &registry, entt::entity entity) const;

    // Wireframes for every collider: grey static, green awake dynamic, blue sleeping, yellow kinematic,
    // magenta sensor, cyan character.
    void DrawColliders(const entt::registry &registry, DebugDraw &draw) const;

    std::size_t GetBodyCount() const;
    std::size_t GetCharacterCount() const;
    const PhysicsSettings &GetSettings() const { return settings_; }

private:
    struct State;

    void CreatePendingBodies(entt::registry &registry);
    void CreatePendingCharacters(entt::registry &registry);
    void UpdateCharacters(entt::registry &registry, float deltaTime);
    void OnCharacterDestroyed(entt::registry &registry, entt::entity entity);
    void DestroyBody(entt::registry &registry, entt::entity entity);
    void OnRigidBodyDestroyed(entt::registry &registry, entt::entity entity);
    void OnColliderDestroyed(entt::registry &registry, entt::entity entity);

    PhysicsSettings settings_;
    entt::registry *registry_ = nullptr;
    std::unique_ptr<State> state_;
};

} // namespace Haki
