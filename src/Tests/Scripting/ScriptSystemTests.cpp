#include "Haki/Serialization/SceneSerializer.h"
#include "Haki/AssetManager/AssetCatalog.h"
#include "Haki/Physics/PhysicsCore.h"
#include "Haki/Renderer/Canvas.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scene/Scene.h"
#include "Haki/Scripting/ScriptSystem.h"

#include <entt/entt.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

namespace fs = std::filesystem;
constexpr float Step = 1.0f / 60.0f;

void Check(bool condition, const char *name, int &failures)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += condition ? 0 : 1;
}

// Scratch scripts folder, removed again when the test ends.
struct ScriptFolder
{
    ScriptFolder()
    {
        Root = fs::temp_directory_path() /
               ("haki-script-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(Root / "lib");
    }
    ~ScriptFolder()
    {
        std::error_code error;
        fs::remove_all(Root, error);
    }

    // Writes a script and moves its timestamp forward so hot reload always sees the change.
    void Write(const std::string &path, const std::string &code)
    {
        const fs::path file = Root / path;
        const bool existed = fs::exists(file);
        const auto previous = existed ? fs::last_write_time(file) : fs::file_time_type{};
        std::ofstream(file) << code;
        if (existed)
        {
            fs::last_write_time(file, previous + std::chrono::seconds(2));
        }
    }

    fs::path Root;
};

entt::entity CreateScripted(Haki::Scene &scene, const std::string &id, const std::string &script,
                            std::unordered_map<std::string, Haki::ScriptValue> properties = {})
{
    entt::registry &registry = scene.GetRegistry();
    const entt::entity entity = registry.create();
    registry.emplace<Haki::EntityIdComponent>(entity, id);
    registry.emplace<Haki::NameComponent>(entity, id);
    registry.emplace<Haki::TransformComponent>(entity);
    registry.emplace<Haki::ScriptComponent>(entity, script, std::move(properties));
    return entity;
}

void Tick(Haki::Scene &scene, int count = 1)
{
    for (int i = 0; i < count; ++i)
    {
        scene.UpdateRuntime(Step);
    }
}

bool Near(const glm::vec3 &a, const glm::vec3 &b, float epsilon = 0.0001f)
{
    return glm::length(a - b) <= epsilon;
}

void RunLifecycleTests(int &failures)
{
    ScriptFolder folder;
    folder.Write("counter.lua", R"(
trace = trace or {}
local Counter = { properties = { speed = 1, label = "default", offset = vec3(1, 2, 3) } }
function Counter:on_start()
    self.started = (self.started or 0) + 1
    trace[#trace + 1] = "start:" .. self.label
end
function Counter:on_update(dt)
    self.updates = (self.updates or 0) + 1
    self.last_dt = dt
    trace[#trace + 1] = "update:" .. self.label
end
return Counter
)");
    folder.Write("rules.lua", R"(
local Rules = {}
function Rules:on_start() RULES = self; trace = trace or {}; trace[#trace + 1] = "rules" end
function Rules:on_update()
    if time.frame == 2 then self.victim:destroy() end
end
function Rules:on_contact_begin() end
return Rules
)");
    folder.Write("victim.lua", R"(
local Victim = {}
function Victim:on_start() RULES.victim = self.entity end
function Victim:on_destroy() destroyed_id = self.entity:id() end
return Victim
)");

    Haki::Scene scene;
    scene.SetScripting(folder.Root, "rules.lua");
    const entt::entity hero = CreateScripted(scene, "hero", "counter.lua", {{"speed", 7.0}, {"label", std::string("hero")}});
    const entt::entity victim = CreateScripted(scene, "victim", "victim.lua");
    Haki::ScriptSystem &scripts = scene.GetScripts();

    scene.UpdateRuntime(0.0f);
    Check(scripts.IsInitialized() && scripts.GetInstanceCount() == 0,
          "a zero-length update initializes scripting without starting scripts", failures);

    Tick(scene);
    Check(scripts.GetInstanceCount() == 3 && scripts.GetLoadedScriptCount() == 3,
          "scene script and one instance per scripted entity are created", failures);
    Check(scripts.Execute("assert(trace[1] == 'rules' and trace[2] == 'start:hero' and trace[3] == 'update:hero')"),
          "scene script starts first, and on_start runs before the first on_update", failures);
    Check(scripts.Execute(R"(
local s = scene.find('hero'):script()
assert(s.speed == 7 and s.label == 'hero', 'overrides')
assert(s.offset == vec3(1, 2, 3), 'defaults')
s.offset.x = 5
assert(s.properties.offset.x == 1, 'vec3 defaults are copied per instance')
assert(math.abs(s.last_dt - 1 / 60) < 1e-6 and math.abs(time.dt - 1 / 60) < 1e-6)
)"),
          "properties override class defaults and are copied per instance", failures);

    Tick(scene, 2);
    Check(scripts.Execute("local s = scene.find('hero'):script(); assert(s.started == 1 and s.updates == 3)"),
          "on_start runs once and on_update every tick", failures);
    Check(!scene.GetRegistry().valid(victim) && scripts.GetInstanceCount() == 2,
          "entity:destroy() from Lua destroys the entity after the callbacks", failures);
    Check(scripts.Execute("assert(destroyed_id == 'victim' and not RULES.victim:valid())"),
          "on_destroy runs and old handles become invalid", failures);
    Check(!scripts.Execute("RULES.victim:position()"), "using a destroyed entity raises a Lua error", failures);
    Check(scene.GetRegistry().valid(hero), "other entities are untouched", failures);
}

void RunEntityApiTests(int &failures)
{
    ScriptFolder folder;
    folder.Write("mover.lua", R"(
local Mover = {}
function Mover:on_update()
    self.entity:set_position(vec3(1, 2, 3))
    self.entity:set_yaw(math.pi / 2)
    self.entity:set_move_velocity(vec3(2, 0, 0))
end
return Mover
)");
    Haki::Scene scene;
    scene.SetScripting(folder.Root, {});
    const entt::entity mover = CreateScripted(scene, "mover", "mover.lua");
    scene.GetRegistry().emplace<Haki::CharacterControllerComponent>(mover);
    Tick(scene);

    const auto &transform = scene.GetRegistry().get<Haki::TransformComponent>(mover);
    Check(Near(transform.Translation, {1.0f, 2.0f, 3.0f}), "set_position moves the entity", failures);
    Check(Near(transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f), {-1.0f, 0.0f, 0.0f}),
          "set_yaw turns forward (-Z) around +Y", failures);
    Check(Near(scene.GetRegistry().get<Haki::CharacterControllerComponent>(mover).DesiredVelocity, {2.0f, 0.0f, 0.0f}),
          "set_move_velocity drives the character controller", failures);

    Haki::ScriptSystem &scripts = scene.GetScripts();
    Check(scripts.Execute(R"(
local e = scene.find('mover')
assert(e == scene.find_by_name('mover'))
assert(e:is_character() and not e:has_body())
assert((e:forward() - vec3(-1, 0, 0)):length() < 1e-4)
assert(math.abs(e:yaw() - math.pi / 2) < 1e-4)
local q = quat.look_rotation(vec3(1, 0, 0))
assert(((q * vec3(0, 0, -1)) - vec3(1, 0, 0)):length() < 1e-4)
assert((vec3(1, 2, 3) + vec3(1, 1, 1)) * 2 == vec3(4, 6, 8))
assert(vec3(3, 0, 4):length() == 5 and vec3(0, 0, 2):normalized() == vec3(0, 0, 1))
assert(scene.find('nobody') == nil and scene.primary_camera() == nil)
assert(#scene.entities_with_script('mover.lua') == 1)
)"),
          "entity, scene, vec3 and quat API", failures);
    {
        const entt::entity cameraEntity = CreateScripted(scene, "cam", "mover.lua");
        scene.GetRegistry().remove<Haki::ScriptComponent>(cameraEntity);
        scene.GetRegistry().emplace<Haki::CameraComponent>(cameraEntity);
        Check(scripts.Execute(R"(
local cam = scene.find('cam')
assert(cam:is_camera() and not cam:is_orthographic())
cam:set_orthographic(8)
assert(cam:is_orthographic() and cam:ortho_size() == 8)
cam:set_perspective(45)
assert(not cam:is_orthographic() and cam:fov() == 45 and cam:ortho_size() == 8)
cam:set_orthographic()
assert(cam:is_orthographic() and cam:ortho_size() == 8)
)"),
              "camera projection can be switched from Lua", failures);
        const auto &component = scene.GetRegistry().get<Haki::CameraComponent>(cameraEntity);
        Check(component.Projection == Haki::CameraProjection::Orthographic && component.OrthographicSize == 8.0f,
              "Lua camera changes reach the CameraComponent", failures);
        Check(!scripts.Execute("scene.find('cam'):set_orthographic(0)") &&
                  !scripts.Execute("scene.find('mover'):set_orthographic(5)"),
              "invalid sizes and non-camera entities are rejected", failures);
    }
    {
        // The animator lives on a child mesh entity, as in imported characters.
        entt::registry &registry = scene.GetRegistry();
        const entt::entity mesh = registry.create();
        registry.emplace<Haki::EntityIdComponent>(mesh, "mover-mesh");
        registry.emplace<Haki::TransformComponent>(mesh);
        registry.emplace<Haki::ParentTransformComponent>(mesh).ParentId = "mover";
        auto &animator = registry.emplace<Haki::AnimatorComponent>(mesh);
        animator.Clips.emplace("Idle", Haki::AnimationClipHandle{});
        animator.Clips.emplace("Punch", Haki::AnimationClipHandle{});
        animator.CurrentClipName = "Idle";
        registry.emplace<Haki::LocomotionAnimationComponent>(mesh);
        Check(scripts.Execute(R"(
local e = scene.find('mover')
assert(e:has_animator() and e:has_animation('Punch') and not e:has_animation('Kick'))
e:play_animation('Punch', { fade = 0.1, speed = 1.5 })
assert(e:animation() == 'Punch' and not e:animation_finished())
)"),
              "play_animation on a character root drives the animator on its child", failures);
        Check(animator.Source == Haki::AnimatorSource::Clip && !animator.Loop && animator.ClipSpeed == 1.5f,
              "play options reach the animator (one-shot by default)", failures);
        animator.OneShotFinished = true;
        Check(scripts.Execute("assert(scene.find('mover'):animation_finished())"),
              "animation_finished reports a completed one-shot", failures);
        Check(scripts.Execute("local e = scene.find('mover'); e:stop_animation(0); assert(e:animation() == 'locomotion')"),
              "stop_animation returns to locomotion", failures);
        Check(!scripts.Execute("scene.find('mover'):play_animation('Kick')") &&
                  !scripts.Execute("scene.find('cam'):play_animation('Punch')") &&
                  !scripts.Execute("scene.find('mover'):play_animation('Punch', { fade = -1 })"),
              "unknown clips, entities without an animator and negative fades are rejected", failures);
    }
    Check(!scripts.Execute("scene.find('mover'):set_position(vec3(0/0, 0, 0))"),
          "non-finite positions are rejected", failures);
    Check(!scripts.Execute("input.key_down('not-a-key')"), "unknown key names raise an error", failures);
    Check(scripts.Execute("assert(not input.key_down('w') and input.move().x == 0)"),
          "input reads as idle without a window", failures);
}

void RunErrorAndReloadTests(int &failures)
{
    ScriptFolder folder;
    folder.Write("bad.lua", R"(
local Bad = {}
function Bad:on_update() error("boom") end
return Bad
)");
    folder.Write("good.lua", R"(
local Good = {}
function Good:on_update() self.n = (self.n or 0) + 1; value = 1 end
return Good
)");
    Haki::Scene scene;
    scene.SetScripting(folder.Root, {});
    CreateScripted(scene, "bad", "bad.lua");
    CreateScripted(scene, "good", "good.lua");
    Haki::ScriptSystem &scripts = scene.GetScripts();
    Tick(scene, 3);

    const auto &errors = scripts.GetErrors();
    Check(errors.size() == 1 && errors.front().find("boom") != std::string::npos &&
              errors.front().find("bad.lua") != std::string::npos,
          "a runtime error is reported once with its script and message", failures);
    Check(scripts.Execute("assert(scene.find('good'):script().n == 3)"),
          "a failing script does not stop other scripts", failures);

    // Hot reload keeps instance state and swaps the functions.
    folder.Write("good.lua", R"(
local Good = {}
function Good:on_update() self.n = (self.n or 0) + 1; value = 2 end
return Good
)");
    Haki::ScriptReloadResult reload = scripts.ReloadChanged();
    Tick(scene);
    Check(reload.Reloaded == 1 && reload.Failed == 0, "only the edited script reloads", failures);
    Check(scripts.Execute("assert(value == 2 and scene.find('good'):script().n == 4)"),
          "reloaded functions run while instance state is kept", failures);

    folder.Write("good.lua", "local Good = {\nfunction Good:on_update() end\nreturn Good\n");
    reload = scripts.ReloadChanged();
    Tick(scene);
    Check(reload.Failed == 1 && scripts.Execute("assert(scene.find('good'):script().n == 5)"),
          "a syntax error on reload keeps the previous version running", failures);

    // Fixing a broken script re-enables its instances.
    folder.Write("bad.lua", R"(
local Bad = {}
function Bad:on_update() fixed = true end
return Bad
)");
    scripts.ClearErrors();
    scripts.ReloadChanged();
    Tick(scene);
    Check(scripts.Execute("assert(fixed == true)") && scripts.GetErrors().empty(),
          "reloading a fixed script re-enables its disabled instances", failures);
    reload = scripts.ReloadChanged(true); // good.lua still has its syntax error on disk
    Check(reload.Reloaded == 1 && reload.Failed == 1, "a forced reload re-runs every script", failures);
}

void RunSandboxTests(int &failures)
{
    ScriptFolder folder;
    folder.Write("lib/util.lua", "return { answer = 42 }\n");
    folder.Write("user.lua", R"(
local util = require("lib.util")
local User = {}
function User:on_start() answer = util.answer end
return User
)");
    folder.Write("../outside.lua", "return {}\n");
    Haki::Scene scene;
    scene.SetScripting(folder.Root, {});
    CreateScripted(scene, "user", "user.lua");
    Tick(scene);
    Haki::ScriptSystem &scripts = scene.GetScripts();

    Check(scripts.Execute("assert(io == nil and os == nil and debug == nil and package == nil)"),
          "io, os, debug and package are not available", failures);
    Check(scripts.Execute("assert(dofile == nil and loadfile == nil and load == nil)"),
          "file and bytecode loading functions are removed", failures);
    Check(scripts.Execute("assert(answer == 42 and require('lib.util') == require('lib.util'))"),
          "require loads modules from the scripts folder once", failures);
    Check(!scripts.Execute("require('..outside')") && !scripts.Execute("require('lib/../../outside')"),
          "require cannot leave the scripts folder", failures);
    std::error_code error;
    fs::remove(folder.Root / ".." / "outside.lua", error);

    // Editing a required module reloads the scripts that use it.
    folder.Write("lib/util.lua", "return { answer = 7 }\n");
    const Haki::ScriptReloadResult reload = scripts.ReloadChanged();
    Check(reload.Reloaded == 1 && scripts.Execute("assert(require('lib.util').answer == 7)"),
          "an edited module is reloaded together with its users", failures);
}

void RunPhysicsTests(int &failures)
{
    ScriptFolder folder;
    folder.Write("walker.lua", R"(
local Walker = { properties = { speed = 3 } }
function Walker:on_start() self.spawn = self.entity:position() end
function Walker:on_update() self.entity:set_move_velocity(vec3(self.speed, 0, 0)) end
function Walker:on_trigger_enter(other) walker_saw = other:id() end
function Walker:respawn() self.respawns = (self.respawns or 0) + 1; self.entity:set_position(self.spawn) end
return Walker
)");
    folder.Write("zone.lua", R"(
local Zone = {}
function Zone:on_trigger_enter(other)
    zone_hits = (zone_hits or 0) + 1
    local behaviour = other:script()
    if behaviour and behaviour.respawn then behaviour:respawn() end
end
return Zone
)");
    folder.Write("rules.lua", R"(
local Rules = {}
function Rules:on_trigger_enter(a, b) rules_saw_trigger = true end
return Rules
)");

    Haki::PhysicsCore::Initialize();
    {
        Haki::Scene scene;
        scene.SetScripting(folder.Root, "rules.lua");
        entt::registry &registry = scene.GetRegistry();

        const entt::entity floor = registry.create();
        registry.emplace<Haki::EntityIdComponent>(floor, "floor");
        registry.emplace<Haki::TransformComponent>(floor);
        registry.emplace<Haki::RigidBodyComponent>(floor);
        registry.emplace<Haki::ColliderComponent>(floor).HalfExtents = {30.0f, 0.5f, 30.0f};

        const entt::entity zone = CreateScripted(scene, "zone", "zone.lua");
        registry.get<Haki::TransformComponent>(zone).Translation = {2.0f, 1.5f, 0.0f};
        Haki::RigidBodyComponent sensor;
        sensor.Layer = Haki::PhysicsLayer::Trigger;
        sensor.IsSensor = true;
        registry.emplace<Haki::RigidBodyComponent>(zone, sensor);
        registry.emplace<Haki::ColliderComponent>(zone).HalfExtents = {0.5f, 1.0f, 2.0f};

        const entt::entity walker = CreateScripted(scene, "walker", "walker.lua");
        registry.get<Haki::TransformComponent>(walker).Translation = {-3.0f, 0.5f, 0.0f};
        registry.emplace<Haki::CharacterControllerComponent>(walker);

        scene.UpdateRuntime(0.0f);
        Tick(scene, 150);
        Haki::ScriptSystem &scripts = scene.GetScripts();
        Check(scripts.Execute("assert(zone_hits and zone_hits >= 1 and walker_saw == 'zone')"),
              "walking into a sensor calls on_trigger_enter on both scripts", failures);
        Check(scripts.Execute("assert(rules_saw_trigger)"), "the scene script sees every trigger event", failures);
        Check(scripts.Execute("assert(scene.find('walker'):script().respawns >= 1)") &&
                  registry.get<Haki::TransformComponent>(walker).Translation.x < 2.0f,
              "scripts call each other: the zone teleports the walker back to spawn", failures);
        Check(scripts.Execute(R"(
local hit = physics.raycast(vec3(0, 10, 10), vec3(0, -1, 0), 20)
assert(hit and hit.entity == scene.find('floor') and math.abs(hit.point.y - 0.5) < 1e-3)
assert(physics.raycast(vec3(0, 10, 10), vec3(0, 1, 0), 20) == nil)
local found = physics.overlap_sphere(scene.find('walker'):position() + vec3(0, 0.9, 0), 0.5)
local sawWalker = false
for _, e in ipairs(found) do if e == scene.find('walker') then sawWalker = true end end
assert(sawWalker)
assert(#physics.overlap_sphere(vec3(2, 1.5, 0), 0.2) == 0)
assert(#physics.overlap_sphere(vec3(2, 1.5, 0), 0.2, true) == 1)
)"),
              "raycast and overlap_sphere from Lua hit characters and skip triggers by default", failures);

        // Carrying: a kinematic body follows set_position; switched back it falls and can be thrown.
        const entt::entity box = registry.create();
        registry.emplace<Haki::EntityIdComponent>(box, "box");
        registry.emplace<Haki::TransformComponent>(box).Translation = {6.0f, 3.0f, 6.0f};
        Haki::RigidBodyComponent dynamicBody;
        dynamicBody.Motion = Haki::PhysicsMotionType::Dynamic;
        dynamicBody.Layer = Haki::PhysicsLayer::Dynamic;
        dynamicBody.Mass = 10.0f;
        registry.emplace<Haki::RigidBodyComponent>(box, dynamicBody);
        registry.emplace<Haki::ColliderComponent>(box).HalfExtents = {0.25f, 0.25f, 0.25f};
        Tick(scene, 2);
        Check(scripts.Execute("local b = scene.find('box'); assert(b:set_kinematic(true) and b:is_kinematic())") &&
                  !scripts.Execute("assert(scene.find('floor'):set_kinematic(true))"),
              "set_kinematic switches dynamic bodies and refuses static ones", failures);
        for (int i = 0; i < 30; ++i)
        {
            scripts.Execute("scene.find('box'):set_position(vec3(6, 4, 6 + " + std::to_string(i * 0.05f) + "))");
            Tick(scene);
        }
        const glm::vec3 held = registry.get<Haki::TransformComponent>(box).Translation;
        Check(std::abs(held.y - 4.0f) < 0.01f && std::abs(held.z - 7.45f) < 0.06f,
              "a kinematic body follows set_position without falling", failures);
        Check(scripts.Execute("local b = scene.find('box'); assert(b:set_kinematic(false) and not b:is_kinematic());"
                              "assert(b:set_velocity(vec3(4, 0, 0)))"),
              "set_kinematic(false) makes the body dynamic again", failures);
        Tick(scene, 20);
        const glm::vec3 thrown = registry.get<Haki::TransformComponent>(box).Translation;
        Check(thrown.y < 3.9f && thrown.x > 6.5f, "the released body falls and keeps its thrown velocity", failures);
    }
    Haki::PhysicsCore::Shutdown();
}

void RunArenaCameraTests(int &failures)
{
    ScriptFolder folder;
    const auto source = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
                        "Assets/Game/Scenes/arena-01/Scripts/sandbox_rules.lua";
    std::ifstream input(source);
    const std::string code((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    Check(!code.empty(), "camera test reads the actual gameplay script", failures);
    folder.Write("camera.lua", code);
    entt::registry registry;
    const auto player = registry.create();
    registry.emplace<Haki::NameComponent>(player, "Player");
    auto &playerPose = registry.emplace<Haki::TransformComponent>(player);
    playerPose.Translation = {-9.5f, 0, 6.5f};
    registry.emplace<Haki::CharacterControllerComponent>(player);
    const auto camera = registry.create();
    auto &cameraSettings = registry.emplace<Haki::CameraComponent>(camera);
    cameraSettings.Primary = true;
    cameraSettings.FieldOfViewDegrees = 32;
    auto &cameraPose = registry.emplace<Haki::TransformComponent>(camera);
    cameraPose.Translation = {0, 19.5f, 14.5f};
    cameraPose.Rotation = glm::quatLookAt(glm::normalize(-cameraPose.Translation), glm::vec3(0, 1, 0));
    const auto rotation = cameraPose.Rotation;
    const auto authoredCamera = cameraPose.Translation;
    Haki::ScriptSystem scripts;
    scripts.SetViewportSize(1600, 900);
    Check(scripts.Initialize(registry, nullptr, folder.Root, "camera.lua"), "camera script initializes", failures);
    for (int i = 0; i < 90; ++i) scripts.Update(registry, Step);
    Check(glm::distance(cameraPose.Translation, authoredCamera) < 0.001f,
          "stationary off-center spawn never starts panning", failures);
    Check(cameraSettings.Projection == Haki::CameraProjection::Perspective && cameraSettings.FieldOfViewDegrees == 32 &&
              std::abs(glm::dot(cameraPose.Rotation, rotation)) > 0.999f,
          "authored projection, field of view and angle remain intact", failures);
    playerPose.Translation = {0, 0, 0};
    scripts.Update(registry, Step);
    playerPose.Translation.x = 0.5f;
    scripts.Update(registry, Step);
    Check(glm::distance(cameraPose.Translation, authoredCamera) < 0.001f,
          "movement within the central safe region leaves the camera fixed", failures);
    playerPose.Translation.x = 9;
    scripts.Update(registry, Step);
    Check(cameraPose.Translation.x > 0 && cameraPose.Translation.x < 9,
          "crossing the screen threshold starts a gradual pan", failures);
    const auto paused = cameraPose.Translation;
    for (int i = 0; i < 90; ++i) scripts.Update(registry, Step);
    Check(glm::distance(cameraPose.Translation, paused) < 0.001f,
          "stopping outside the safe region freezes the pan without drift", failures);
    for (int i = 0; i < 1200; ++i)
    {
        playerPose.Translation.x += 0.001f;
        scripts.Update(registry, Step);
    }
    const auto relative = playerPose.Translation - cameraPose.Translation;
    const auto forward = rotation * glm::vec3(0, 0, -1);
    const auto right = rotation * glm::vec3(1, 0, 0);
    const float halfHeight = glm::dot(relative, forward) * std::tan(glm::radians(16.0f));
    Check(std::abs(glm::dot(relative, right)) / (halfHeight * 16.0f / 9.0f) < 0.25f,
          "continued movement pans the player back near the screen center", failures);
    Check(std::abs(cameraPose.Translation.y - authoredCamera.y) < 0.001f,
          "pan preserves authored camera height", failures);
    const auto beforeJump = cameraPose.Translation;
    playerPose.Translation.y += 3;
    scripts.Update(registry, Step);
    Check(glm::distance(cameraPose.Translation, beforeJump) < 0.001f,
          "vertical motion does not activate camera following", failures);

    // The same world position is inside a wide viewport but near the edge of a portrait one.
    scripts.Shutdown();
    cameraPose.Translation = authoredCamera;
    playerPose.Translation = {0, 0, 0};
    scripts.SetViewportSize(1600, 900);
    scripts.Initialize(registry, nullptr, folder.Root, "camera.lua");
    scripts.Update(registry, Step);
    playerPose.Translation.x = 5;
    scripts.Update(registry, Step);
    Check(glm::distance(cameraPose.Translation, authoredCamera) < 0.001f,
          "wide viewport provides a larger horizontal safe region", failures);
    scripts.SetViewportSize(900, 1600);
    playerPose.Translation.x += 0.01f;
    scripts.Update(registry, Step);
    Check(cameraPose.Translation.x > 0, "resize changes the edge threshold using actual viewport aspect", failures);
    scripts.SetViewportSize(0, 0);
    Check(scripts.Execute("assert(math.abs(scene.viewport_aspect() - 900/1600) < 0.001)"),
          "minimized viewport keeps the last valid aspect", failures);
    registry.destroy(player);
    scripts.Update(registry, Step);
    Check(scripts.GetErrors().empty(), "missing player does not produce script errors", failures);
}

void RunAssetLayoutTests(int &failures)
{
    const auto root = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    Haki::AssetCatalog catalog;
    std::vector<Haki::SerializationError> errors;
    Check(catalog.Load(root/"Assets/Game/Scenes/arena-01/Catalog.json", errors),
          "scene catalog composes engine and shared resources", failures);
    Check(catalog.Find("engine.shader.gltf-pbr") && catalog.GetCount()>10,
          "engine defaults and arena resources coexist in scene scope", failures);
    ScriptFolder folder;
    folder.Write("a.json", R"({"type":"haki.asset-catalog","version":1,"imports":["b.json"],"assets":[]})");
    folder.Write("b.json", R"({"type":"haki.asset-catalog","version":1,"imports":["a.json"],"assets":[]})");
    errors.clear();
    Check(!catalog.Load(folder.Root/"a.json", errors) && !errors.empty(), "catalog import cycles rejected", failures);
    const auto local = folder.Root/"Game/Scenes/demo/Scripts";
    const auto shared = folder.Root/"Game/Shared/Scripts/lib";
    fs::create_directories(local); fs::create_directories(shared);
    std::ofstream(shared/"helper.lua") << "return {value=42}";
    std::ofstream(local/"rules.lua") << "return { on_start=function(self) local_started=true end }";
    std::ofstream(local/"player.lua") << "return { on_start=function(self) player_started=true end }";
    const auto sceneFile = local.parent_path()/"demo.scene.json";
    std::ofstream(local.parent_path()/"Catalog.json") << R"({"type":"haki.asset-catalog","version":1,"assets":[]})";
    auto writeScene = [&](const char *sceneScript, const char *entityScript) {
        std::ofstream(sceneFile) << R"({"type":"haki.scene","version":1,"id":"demo","assetCatalog":"Catalog.json","script":")"
          << sceneScript << R"(","entities":[{"id":"camera","components":{"camera":{"primary":true}}},)"
          << R"({"id":"player","components":{"script":{"path":")" << entityScript << R"("}}}]})";
    };
    auto scopedCatalog = std::make_shared<Haki::AssetCatalog>();
    Haki::SceneSerializer serializer(scopedCatalog);
    // A stale legacy root must never affect a catalog-owning scene.
    serializer.SetScriptRoot(folder.Root/"missing-legacy-scripts");
    writeScene("rules.lua", "player.lua");
    auto loaded = serializer.Deserialize(sceneFile);
    Check(bool(loaded), "scene-local script validation uses the scene Scripts folder", failures);
    if (loaded) {
        loaded.Value->UpdateRuntime(Step);
        Check(loaded.Value->GetScripts().Execute("assert(local_started and player_started)"),
              "validated local scene and entity scripts execute from the same root", failures);
    }
    std::ofstream(shared/"behaviour.lua") << "return { on_start=function(self) shared_started=true end }";
    writeScene("shared/lib/behaviour.lua", "shared/lib/behaviour.lua");
    auto sharedScene = serializer.Deserialize(sceneFile);
    Check(bool(sharedScene), "shared scene and entity scripts pass file-existence validation", failures);
    if (sharedScene) {
        sharedScene.Value->UpdateRuntime(Step);
        Check(sharedScene.Value->GetScripts().Execute("assert(shared_started)"),
              "shared scripts execute through the same resolver used at load", failures);
    }
    writeScene("rules.lua", "missing.lua");
    auto missing = serializer.Deserialize(sceneFile);
    Check(!missing && !missing.Errors.empty() &&
              missing.Errors.back().Message.find((local/"missing.lua").generic_string()) != std::string::npos,
          "missing script diagnostic reports the scene-local path", failures);
    entt::registry registry;
    Haki::ScriptSystem scripts;
    scripts.Initialize(registry, nullptr, local);
    Check(scripts.Execute("assert(require('shared.lib.helper').value == 42)"),
          "scene scripts resolve explicitly shared modules", failures);
}

} // namespace

// Editor pause / single-step: scripts and animation time stand still until a step is requested.
void RunPauseTests(int &failures)
{
    ScriptFolder folder;
    folder.Write("counter.lua", R"(
local Counter = {}
function Counter:on_start() self.updates = 0 end
function Counter:on_update() self.updates = self.updates + 1 end
return Counter
)");
    Haki::Scene scene;
    scene.SetScripting(folder.Root, {});
    CreateScripted(scene, "counter", "counter.lua");
    Tick(scene, 2);
    Haki::ScriptSystem &scripts = scene.GetScripts();
    Check(scripts.Execute("assert(scene.find('counter'):script().updates == 2)"), "the counter runs before pausing",
          failures);

    scene.SetSimulationPaused(true);
    scene.ResetCpuTimings();
    Tick(scene, 5);
    Check(scripts.Execute("assert(scene.find('counter'):script().updates == 2)"), "a paused scene does not update scripts",
          failures);
    Check(scene.GetCpuTimings().Ticks == 5, "paused ticks still run (transforms resolve) and are timed", failures);

    scene.RequestStep();
    Tick(scene, 3);
    Check(scripts.Execute("assert(scene.find('counter'):script().updates == 3)"),
          "a requested step advances exactly one tick", failures);

    scene.SetSimulationPaused(false);
    Tick(scene, 2);
    Check(scripts.Execute("assert(scene.find('counter'):script().updates == 5)"), "resuming continues normally",
          failures);
}

void RunDrawTests(int &failures)
{
    ScriptFolder folder;
    folder.Write("painter.lua", R"(
local Painter = {}
function Painter:on_start() self.draws = 0; self.last_dt = -1 end
function Painter:on_draw(dt)
    self.draws = self.draws + 1
    self.last_dt = dt
    local p = self.entity:position()
    draw.rect(p, -40, -30, 80, 8, { color = { 0.2, 0.9, 0.3, 1 } })
    draw.rect(p, -41, -31, 82, 10, { color = { r = 0, g = 0, b = 0 }, outline = 1 })
    draw.label(p, "hello", { size = 20, offset_y = 12 }) -- no font in tests: skipped quietly
    draw.screen_rect(10, 10, 50, 20, { color = { 0.2, 0.9, 0.3, 1 } })
    local w, h = draw.measure("hello")
    self.measured = w == 0 and h == 0
end
return Painter
)");
    folder.Write("eager.lua", R"(
local Eager = {}
function Eager:on_update() draw.screen_rect(0, 0, 1, 1) end
return Eager
)");
    Haki::Scene scene;
    scene.SetScripting(folder.Root, {});
    CreateScripted(scene, "painter", "painter.lua");
    Haki::ScriptSystem &scripts = scene.GetScripts();

    Tick(scene, 3);
    Check(scripts.Execute("assert(scene.find('painter'):script().draws == 0)"), "fixed ticks never call on_draw",
          failures);

    Haki::Canvas canvas;
    scripts.Draw(scene.GetRegistry(), canvas, nullptr, 0.25f);
    Check(scripts.Execute("local s = scene.find('painter'):script(); assert(s.draws == 1 and s.last_dt == 0.25)"),
          "on_draw runs once per Draw with the frame dt", failures);
    // fill + 4 outline edges + screen rect
    Check(canvas.GetQuadCount() == 6, "draw.rect/screen_rect queue canvas quads", failures);
    {
        std::vector<Haki::CanvasVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<Haki::CanvasBatch> batches;
        canvas.BuildBatches(nullptr, vertices, indices, batches);
        bool green = false;
        for (const Haki::CanvasVertex &v : vertices)
        {
            green = green || (std::abs(v.Color.g - 0.9f) < 1e-4f && std::abs(v.Color.r - 0.2f) < 1e-4f);
        }
        Check(green, "draw option colours reach the canvas", failures);
    }
    Check(scripts.Execute("assert(scene.find('painter'):script().measured)"), "draw.measure is 0x0 without a font",
          failures);

    scripts.ClearErrors();
    CreateScripted(scene, "eager", "eager.lua");
    Tick(scene);
    bool rejected = false;
    for (const std::string &error : scripts.GetErrors())
    {
        rejected = rejected || error.find("only works inside on_draw") != std::string::npos;
    }
    Check(rejected, "draw.* outside on_draw raises a script error", failures);
    Check(!scripts.Execute("draw.screen_rect(0, 0, 1, 1)"), "draw.* from the console is rejected too", failures);
}

int main()
{
    int failures = 0;
    std::cout << "=== pause and step ===\n";
    RunPauseTests(failures);
    std::cout << "=== draw ===\n";
    RunDrawTests(failures);
    std::cout << "=== lifecycle ===\n";
    RunLifecycleTests(failures);
    RunAssetLayoutTests(failures);
    std::cout << "=== entity api ===\n";
    RunEntityApiTests(failures);
    std::cout << "=== errors and hot reload ===\n";
    RunErrorAndReloadTests(failures);
    std::cout << "=== sandbox ===\n";
    RunSandboxTests(failures);
    std::cout << "=== arena camera ===\n";
    RunArenaCameraTests(failures);
    std::cout << "=== physics ===\n";
    RunPhysicsTests(failures);
    return failures == 0 ? 0 : 1;
}
