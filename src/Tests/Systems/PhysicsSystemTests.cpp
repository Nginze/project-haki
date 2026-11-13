#include "Haki/Physics/PhysicsCore.h"
#include "Haki/Scene/Components.h"
#include "Haki/Systems/PhysicsSystem.h"

#include <entt/entt.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{

constexpr float Step = 1.0f / 60.0f;

bool Near(float left, float right, float tolerance = 0.0001f)
{
    return std::abs(left - right) < tolerance;
}

void Check(bool condition, const char *name, int &failures)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += condition ? 0 : 1;
}

entt::entity CreateBody(entt::registry &registry, glm::vec3 position, Haki::PhysicsMotionType motion,
                        Haki::PhysicsLayer layer, Haki::ColliderComponent collider, bool sensor = false)
{
    const entt::entity entity = registry.create();
    registry.emplace<Haki::TransformComponent>(entity).Translation = position;
    Haki::RigidBodyComponent body;
    body.Motion = motion;
    body.Layer = layer;
    body.IsSensor = sensor;
    registry.emplace<Haki::RigidBodyComponent>(entity, body);
    registry.emplace<Haki::ColliderComponent>(entity, collider);
    return entity;
}

Haki::ColliderComponent Box(glm::vec3 halfExtents)
{
    Haki::ColliderComponent collider;
    collider.Shape = Haki::ColliderShape::Box;
    collider.HalfExtents = halfExtents;
    return collider;
}

Haki::ColliderComponent Sphere(float radius)
{
    Haki::ColliderComponent collider;
    collider.Shape = Haki::ColliderShape::Sphere;
    collider.Radius = radius;
    return collider;
}

void Simulate(Haki::PhysicsSystem &physics, entt::registry &registry, int steps,
              std::vector<Haki::PhysicsContactEvent> *events = nullptr)
{
    for (int index = 0; index < steps; ++index)
    {
        physics.Update(registry, Step);
        if (events)
        {
            const auto &stepEvents = physics.GetContactEvents();
            events->insert(events->end(), stepEvents.begin(), stepEvents.end());
        }
    }
}

// Physics world with a 60 x 60 m static floor whose top surface is at y = 0.5.
struct World
{
    World()
    {
        physics.Initialize(registry);
        floor = CreateBody(registry, {0.0f, 0.0f, 0.0f}, Haki::PhysicsMotionType::Static, Haki::PhysicsLayer::Static,
                           Box({30.0f, 0.5f, 30.0f}));
    }

    entt::registry registry; // declared first so the physics system is destroyed before it
    Haki::PhysicsSystem physics;
    entt::entity floor = entt::null;
};

entt::entity CreateCharacter(entt::registry &registry, glm::vec3 feet)
{
    const entt::entity entity = registry.create();
    registry.emplace<Haki::TransformComponent>(entity).Translation = feet;
    registry.emplace<Haki::CharacterControllerComponent>(entity);
    return entity;
}

entt::entity CreateRamp(entt::registry &registry, float degrees)
{
    // A 0.2 m thick, 8 m long slab rising towards +X whose lower end touches the floor at x = 2.
    const float angle = glm::radians(degrees);
    const glm::vec3 halfExtents(4.0f, 0.1f, 2.0f);
    const glm::vec3 center(2.0f + halfExtents.x * std::cos(angle), 0.5f + halfExtents.x * std::sin(angle), 0.0f);
    const entt::entity ramp = CreateBody(registry, center, Haki::PhysicsMotionType::Static, Haki::PhysicsLayer::Static,
                                         Box(halfExtents));
    registry.get<Haki::TransformComponent>(ramp).Rotation = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f));
    return ramp;
}

void Walk(World &world, entt::entity character, glm::vec3 velocity, float seconds,
          std::vector<Haki::PhysicsContactEvent> *events = nullptr)
{
    world.registry.get<Haki::CharacterControllerComponent>(character).DesiredVelocity = velocity;
    Simulate(world.physics, world.registry, static_cast<int>(seconds * 60.0f + 0.5f), events);
}

const glm::vec3 &Feet(World &world, entt::entity entity)
{
    return world.registry.get<Haki::TransformComponent>(entity).Translation;
}

void RunCharacterTests(int &failures)
{
    using namespace Haki;
    {
        World world;
        const entt::entity character = CreateCharacter(world.registry, {0.0f, 2.0f, 0.0f});
        world.physics.Update(world.registry, 0.0f);
        const auto &controller = world.registry.get<CharacterControllerComponent>(character);
        Check(world.physics.GetCharacterCount() == 1 && controller.InnerBodyId != InvalidPhysicsBodyId &&
                  world.physics.GetBodyCount() == 2,
              "character is created with an inner hit body", failures);

        Walk(world, character, {}, 1.0f);
        Check(controller.Grounded && Near(Feet(world, character).y, 0.5f, 0.05f),
              "character falls and stands on the floor with its feet at the origin", failures);
        if (!Near(Feet(world, character).y, 0.5f, 0.05f)) std::cout << "  feet y = " << Feet(world, character).y << '\n';

        const auto hit = world.physics.Raycast(Feet(world, character) + glm::vec3(0.0f, 10.0f, 0.0f),
                                               {0.0f, -1.0f, 0.0f}, 20.0f);
        Check(hit && hit->Entity == character && Near(hit->Point.y, 0.5f + 1.8f, 0.05f),
              "raycast hits the character's 1.8 m capsule", failures);
        std::vector<entt::entity> overlaps;
        world.physics.OverlapSphere(Feet(world, character) + glm::vec3(0.6f, 1.0f, 0.0f), 0.5f, overlaps);
        Check(std::find(overlaps.begin(), overlaps.end(), character) != overlaps.end(),
              "attack overlap sphere hits the character", failures);

        world.registry.get<TransformComponent>(character).Translation = {10.0f, 0.5f, 10.0f};
        Walk(world, character, {}, 0.1f);
        Check(Near(Feet(world, character).x, 10.0f, 0.01f) && Near(Feet(world, character).z, 10.0f, 0.01f),
              "setting the transform teleports the character", failures);

        world.registry.remove<CharacterControllerComponent>(character);
        Check(world.physics.GetCharacterCount() == 0 && world.physics.GetBodyCount() == 1,
              "removing the controller destroys the character and its inner body", failures);
    }
    {
        World world;
        CreateBody(world.registry, {5.0f, 1.5f, 0.0f}, PhysicsMotionType::Static, PhysicsLayer::Static,
                   Box({0.5f, 1.0f, 5.0f}));
        const entt::entity character = CreateCharacter(world.registry, {0.0f, 0.5f, 0.0f});
        Walk(world, character, {4.0f, 0.0f, 0.0f}, 3.0f);
        const float stopX = Feet(world, character).x;
        Check(stopX > 3.9f && stopX < 4.2f, "a wall stops the character at its radius", failures);
        if (!(stopX > 3.9f && stopX < 4.2f)) std::cout << "  x = " << stopX << '\n';
        Walk(world, character, {4.0f, 0.0f, 4.0f}, 0.5f);
        Check(Feet(world, character).x < 4.2f && Feet(world, character).z > 1.5f,
              "moving diagonally into a wall slides along it", failures);
    }
    {
        World world;
        CreateRamp(world.registry, 20.0f);
        const entt::entity character = CreateCharacter(world.registry, {0.0f, 0.5f, 0.0f});
        Walk(world, character, {3.0f, 0.0f, 0.0f}, 2.5f);
        Check(Feet(world, character).y > 1.5f, "character walks up a 20 degree ramp", failures);
        if (!(Feet(world, character).y > 1.5f)) std::cout << "  y = " << Feet(world, character).y << '\n';
    }
    {
        World world;
        CreateRamp(world.registry, 60.0f);
        const entt::entity character = CreateCharacter(world.registry, {0.0f, 0.5f, 0.0f});
        Walk(world, character, {3.0f, 0.0f, 0.0f}, 2.5f);
        Check(Feet(world, character).y < 1.0f && Feet(world, character).x < 3.0f,
              "character cannot climb a 60 degree slope", failures);
        if (!(Feet(world, character).y < 1.0f)) std::cout << "  pos = " << Feet(world, character).x << ", " << Feet(world, character).y << '\n';
    }
    {
        World world;
        const entt::entity crate = CreateBody(world.registry, {3.0f, 0.9f, 0.0f}, PhysicsMotionType::Dynamic,
                                              PhysicsLayer::Dynamic, Box({0.4f, 0.4f, 0.4f}));
        world.registry.get<RigidBodyComponent>(crate).Mass = 10.0f;
        const entt::entity character = CreateCharacter(world.registry, {0.0f, 0.5f, 0.0f});
        Walk(world, character, {3.0f, 0.0f, 0.0f}, 2.0f);
        Check(Feet(world, crate).x > 3.5f, "character pushes a dynamic crate", failures);
        if (!(Feet(world, crate).x > 3.5f)) std::cout << "  crate x = " << Feet(world, crate).x << '\n';
    }
    {
        World world;
        const entt::entity zone = CreateBody(world.registry, {0.0f, 1.5f, 0.0f}, PhysicsMotionType::Static,
                                             PhysicsLayer::Trigger, Box({1.0f, 1.0f, 1.0f}), true);
        const entt::entity character = CreateCharacter(world.registry, {-5.0f, 0.5f, 0.0f});
        std::vector<PhysicsContactEvent> events;
        Walk(world, character, {4.0f, 0.0f, 0.0f}, 2.0f, &events);
        const bool entered = std::any_of(events.begin(), events.end(), [&](const PhysicsContactEvent &event)
        {
            return event.Type == PhysicsContactType::Began && event.IsSensor &&
                   ((event.First == zone && event.Second == character) ||
                    (event.First == character && event.Second == zone));
        });
        Check(entered, "character walking into a sensor raises a Began event", failures);
        Check(Feet(world, character).x > 2.0f, "sensors do not block characters", failures);
    }
    {
        World world;
        const entt::entity walker = CreateCharacter(world.registry, {0.0f, 0.5f, 0.0f});
        CreateCharacter(world.registry, {3.0f, 0.5f, 0.0f});
        Walk(world, walker, {3.0f, 0.0f, 0.0f}, 2.0f);
        Check(Feet(world, walker).x > 5.0f, "characters pass through each other", failures);
    }
    {
        World world;
        const entt::entity invalid = CreateCharacter(world.registry, {0.0f, 0.5f, 0.0f});
        world.registry.emplace<RigidBodyComponent>(invalid);
        world.registry.emplace<ColliderComponent>(invalid);
        world.physics.Update(world.registry, 0.0f);
        Check(world.physics.GetCharacterCount() == 0, "a character mixed with a rigid body is rejected", failures);
    }
}

// Editor support: moving bodies by hand and tweaking their material while the game runs.
void RunEditorTests(int &failures)
{
    using namespace Haki;
    World world;
    const entt::entity ball = CreateBody(world.registry, {0.0f, 5.0f, 0.0f}, PhysicsMotionType::Dynamic,
                                         PhysicsLayer::Dynamic, Sphere(0.5f));
    world.physics.Update(world.registry, 0.0f);
    Simulate(world.physics, world.registry, 20); // falling
    Check(world.physics.GetLinearVelocity(world.registry, ball)->y < -1.0f, "the ball is falling before the teleport",
          failures);

    world.registry.get<TransformComponent>(ball).Translation = {8.0f, 3.0f, -2.0f};
    Check(world.physics.Teleport(world.registry, ball), "teleport accepts a dynamic body", failures);
    const glm::vec3 velocity = *world.physics.GetLinearVelocity(world.registry, ball);
    Check(glm::length(velocity) < 0.0001f, "teleport stops a dynamic body", failures);
    world.physics.Update(world.registry, Step);
    const glm::vec3 &position = world.registry.get<TransformComponent>(ball).Translation;
    Check(Near(position.x, 8.0f, 0.01f) && Near(position.z, -2.0f, 0.01f) && position.y > 2.9f,
          "the body continues from the teleported position", failures);

    Check(world.physics.IsBodyAwake(world.registry, ball), "a teleported dynamic body is awake", failures);

    world.registry.get<RigidBodyComponent>(ball).GravityFactor = 0.0f;
    Check(world.physics.ApplyBodyMaterial(world.registry, ball), "material changes apply to a live body", failures);
    world.physics.Teleport(world.registry, ball);
    const float heightBefore = world.registry.get<TransformComponent>(ball).Translation.y;
    Simulate(world.physics, world.registry, 30);
    Check(Near(world.registry.get<TransformComponent>(ball).Translation.y, heightBefore, 0.01f),
          "a zero gravity factor applied at runtime makes the body float", failures);

    const entt::entity loose = world.registry.create();
    world.registry.emplace<TransformComponent>(loose);
    Check(!world.physics.Teleport(world.registry, loose) && !world.physics.ApplyBodyMaterial(world.registry, loose),
          "entities without a body are ignored", failures);
}

} // namespace

int main()
{
    using namespace Haki;
    int failures = 0;

    {
        entt::registry registry;
        PhysicsSystem physics;
        Check(!physics.Initialize(registry), "system refuses to initialize before PhysicsCore", failures);
    }

    Check(PhysicsCore::Initialize(), "PhysicsCore initializes headless", failures);
    {
        entt::registry registry;
        PhysicsSystem physics;
        Check(physics.Initialize(registry), "system initializes", failures);

        // The floor's top surface is at y = 0.5.
        const entt::entity floor =
            CreateBody(registry, {0.0f, 0.0f, 0.0f}, PhysicsMotionType::Static, PhysicsLayer::Static,
                       Box({20.0f, 0.5f, 20.0f}));
        const entt::entity ball = CreateBody(registry, {0.0f, 5.0f, 0.0f}, PhysicsMotionType::Dynamic,
                                             PhysicsLayer::Dynamic, Sphere(0.5f));
        const entt::entity trigger = CreateBody(registry, {5.0f, 2.5f, 0.0f}, PhysicsMotionType::Static,
                                                PhysicsLayer::Trigger, Box({1.0f, 1.0f, 1.0f}), true);
        const entt::entity faller = CreateBody(registry, {5.0f, 6.0f, 0.0f}, PhysicsMotionType::Dynamic,
                                               PhysicsLayer::Dynamic, Sphere(0.5f));
        const entt::entity mover = CreateBody(registry, {-5.0f, 2.0f, 0.0f}, PhysicsMotionType::Kinematic,
                                              PhysicsLayer::Dynamic, Box({0.5f, 0.5f, 0.5f}));

        physics.Update(registry, 0.0f);
        Check(physics.GetBodyCount() == 5, "zero-delta update creates bodies without stepping", failures);
        Check(registry.get<RigidBodyComponent>(ball).BodyId != InvalidPhysicsBodyId, "body id is stored", failures);
        Check(Near(registry.get<TransformComponent>(ball).Translation.y, 5.0f), "zero-delta update does not simulate",
              failures);

        std::vector<PhysicsContactEvent> events;
        Simulate(physics, registry, 240, &events);

        const float ballY = registry.get<TransformComponent>(ball).Translation.y;
        Check(Near(ballY, 1.0f, 0.05f), "dynamic sphere comes to rest on the static floor", failures);
        if (!Near(ballY, 1.0f, 0.05f)) std::cout << "  ball y = " << ballY << '\n';

        const float fallerY = registry.get<TransformComponent>(faller).Translation.y;
        Check(Near(fallerY, 1.0f, 0.05f), "sensor does not stop bodies passing through it", failures);
        const bool sensorBegan = std::any_of(events.begin(), events.end(), [&](const PhysicsContactEvent &event)
        {
            return event.Type == PhysicsContactType::Began && event.IsSensor &&
                   ((event.First == trigger && event.Second == faller) ||
                    (event.First == faller && event.Second == trigger));
        });
        Check(sensorBegan, "sensor reports a Began contact event", failures);

        const auto floorHit = physics.Raycast({3.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
        Check(floorHit && floorHit->Entity == floor && Near(floorHit->Distance, 9.5f, 0.01f) &&
                  Near(floorHit->Normal.y, 1.0f, 0.01f),
              "raycast reports entity, distance and normal", failures);

        const auto maskedHit = physics.Raycast({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f,
                                               ToLayerMask(PhysicsLayer::Static));
        Check(maskedHit && maskedHit->Entity == floor, "layer mask skips the dynamic ball", failures);

        const auto sensorMiss = physics.Raycast({5.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
        Check(sensorMiss && sensorMiss->Entity == faller, "default query mask ignores trigger volumes", failures);

        std::vector<entt::entity> overlaps;
        physics.OverlapSphere(registry.get<TransformComponent>(ball).Translation, 0.6f, overlaps);
        Check(std::find(overlaps.begin(), overlaps.end(), ball) != overlaps.end() &&
                  std::find(overlaps.begin(), overlaps.end(), faller) == overlaps.end(),
              "overlap sphere finds nearby colliders only", failures);

        const auto beforeMove = physics.Raycast({-4.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
        registry.get<TransformComponent>(mover).Translation.x = -4.0f;
        physics.Update(registry, Step);
        const auto afterMove = physics.Raycast({-4.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
        Check(beforeMove && beforeMove->Entity == floor && afterMove && afterMove->Entity == mover,
              "kinematic body follows its transform", failures);

        Check(physics.AddImpulse(registry, ball, {0.0f, 50.0f, 0.0f}) &&
                  !physics.AddImpulse(registry, floor, {0.0f, 1.0f, 0.0f}),
              "impulses apply to dynamic bodies only", failures);

        registry.destroy(ball);
        Check(physics.GetBodyCount() == 4, "destroying an entity destroys its body", failures);
        registry.remove<ColliderComponent>(faller);
        Check(physics.GetBodyCount() == 3 &&
                  registry.get<RigidBodyComponent>(faller).BodyId == InvalidPhysicsBodyId,
              "removing the collider destroys the body", failures);

        physics.Shutdown();
        Check(physics.GetBodyCount() == 0 &&
                  registry.get<RigidBodyComponent>(floor).BodyId == InvalidPhysicsBodyId,
              "shutdown releases every body", failures);
        registry.destroy(floor); // hooks are disconnected; must not touch the destroyed world
    }
    RunCharacterTests(failures);
    RunEditorTests(failures);
    PhysicsCore::Shutdown();
    Check(!PhysicsCore::IsInitialized(), "PhysicsCore shuts down", failures);
    return failures == 0 ? 0 : 1;
}
