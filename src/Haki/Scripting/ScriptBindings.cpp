// The Lua API exposed to gameplay scripts. Each section below is one Lua module/type.

#include "Haki/Scripting/ScriptRuntime.h"

#include "Haki/AssetManager/AnimationClip.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/Input/InputManager.h"
#include "Haki/Logging/Logger.h"
#include "Haki/Renderer/Canvas.h"
#include "Haki/Renderer/Font.h"
#include "Haki/Scene/Components.h"
#include "Haki/Systems/AnimationSystem.h"
#include "Haki/Systems/PhysicsSystem.h"
#include "Haki/Systems/PlayerControllerSystem.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Haki
{
namespace
{

using Input::InputManager;

constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 LocalForward{0.0f, 0.0f, -1.0f};
constexpr glm::vec3 LocalRight{1.0f, 0.0f, 0.0f};

std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string FormatVec3(const glm::vec3 &v)
{
    std::ostringstream out;
    out << "vec3(" << v.x << ", " << v.y << ", " << v.z << ")";
    return out.str();
}

float YawOf(const glm::quat &rotation)
{
    const glm::vec3 forward = rotation * LocalForward;
    return std::atan2(-forward.x, -forward.z);
}

glm::quat LookRotation(const glm::vec3 &direction)
{
    const float length = glm::length(direction);
    if (!(length > 1e-6f))
    {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const glm::vec3 forward = direction / length;
    // Straight up/down has no defined yaw; pick a stable up axis instead of producing NaNs.
    const glm::vec3 up = std::abs(glm::dot(forward, WorldUp)) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : WorldUp;
    return glm::quatLookAt(forward, up);
}

// ---------------------------------------------------------------------------------------------------------
// Input name tables
// ---------------------------------------------------------------------------------------------------------

const std::unordered_map<std::string, gainput::Key> &KeyNames()
{
    static const std::unordered_map<std::string, gainput::Key> keys = [] {
        std::unordered_map<std::string, gainput::Key> map;
        for (int i = 0; i < 26; ++i)
        {
            map[std::string(1, static_cast<char>('a' + i))] = static_cast<gainput::Key>(gainput::KeyA + i);
        }
        for (int i = 0; i < 10; ++i)
        {
            map[std::string(1, static_cast<char>('0' + i))] = static_cast<gainput::Key>(gainput::Key0 + i);
        }
        for (int i = 0; i < 12; ++i)
        {
            map["f" + std::to_string(i + 1)] = static_cast<gainput::Key>(gainput::KeyF1 + i);
        }
        map["space"] = gainput::KeySpace;
        map["escape"] = gainput::KeyEscape;
        map["enter"] = gainput::KeyReturn;
        map["return"] = gainput::KeyReturn;
        map["tab"] = gainput::KeyTab;
        map["backspace"] = gainput::KeyBackSpace;
        map["shift"] = gainput::KeyShiftL;
        map["rshift"] = gainput::KeyShiftR;
        map["ctrl"] = gainput::KeyCtrlL;
        map["rctrl"] = gainput::KeyCtrlR;
        map["alt"] = gainput::KeyAltL;
        map["ralt"] = gainput::KeyAltR;
        map["left"] = gainput::KeyLeft;
        map["right"] = gainput::KeyRight;
        map["up"] = gainput::KeyUp;
        map["down"] = gainput::KeyDown;
        return map;
    }();
    return keys;
}

const std::unordered_map<std::string, gainput::MouseButton> &MouseNames()
{
    static const std::unordered_map<std::string, gainput::MouseButton> buttons = {
        {"left", gainput::MouseButtonLeft},
        {"right", gainput::MouseButtonRight},
        {"middle", gainput::MouseButtonMiddle},
    };
    return buttons;
}

const std::unordered_map<std::string, gainput::PadButton> &PadButtonNames()
{
    static const std::unordered_map<std::string, gainput::PadButton> buttons = {
        {"a", gainput::PadButtonA},         {"b", gainput::PadButtonB},
        {"x", gainput::PadButtonX},         {"y", gainput::PadButtonY},
        {"l1", gainput::PadButtonL1},       {"r1", gainput::PadButtonR1},
        {"l2", gainput::PadButtonL2},       {"r2", gainput::PadButtonR2},
        {"l3", gainput::PadButtonL3},       {"r3", gainput::PadButtonR3},
        {"start", gainput::PadButtonStart}, {"select", gainput::PadButtonSelect},
        {"left", gainput::PadButtonLeft},   {"right", gainput::PadButtonRight},
        {"up", gainput::PadButtonUp},       {"down", gainput::PadButtonDown},
    };
    return buttons;
}

const std::unordered_map<std::string, gainput::PadButton> &PadAxisNames()
{
    static const std::unordered_map<std::string, gainput::PadButton> axes = {
        {"left_x", gainput::PadButtonLeftStickX},  {"left_y", gainput::PadButtonLeftStickY},
        {"right_x", gainput::PadButtonRightStickX}, {"right_y", gainput::PadButtonRightStickY},
        {"left_trigger", gainput::PadButtonAxis4},  {"right_trigger", gainput::PadButtonAxis5},
    };
    return axes;
}

template <typename Map>
typename Map::mapped_type Lookup(const Map &map, const std::string &name, const char *kind)
{
    const auto found = map.find(Lowercase(name));
    if (found == map.end())
    {
        throw std::runtime_error(std::string("unknown ") + kind + " '" + name + "'");
    }
    return found->second;
}

std::size_t PadIndex(int pad)
{
    return pad < 0 ? 0 : static_cast<std::size_t>(pad);
}

// ---------------------------------------------------------------------------------------------------------
// Entity helpers
// ---------------------------------------------------------------------------------------------------------

entt::registry &Registry(ScriptRuntime &runtime)
{
    if (!runtime.Registry)
    {
        throw std::runtime_error("no active scene");
    }
    return *runtime.Registry;
}

bool IsValid(ScriptRuntime &runtime, const LuaEntity &entity)
{
    return runtime.Registry && entity.Value != entt::null && runtime.Registry->valid(entity.Value);
}

void RequireValid(ScriptRuntime &runtime, const LuaEntity &entity)
{
    if (!IsValid(runtime, entity))
    {
        throw std::runtime_error("entity is no longer valid");
    }
}

TransformComponent &Transform(ScriptRuntime &runtime, const LuaEntity &entity)
{
    RequireValid(runtime, entity);
    TransformComponent *transform = runtime.Registry->try_get<TransformComponent>(entity.Value);
    if (!transform)
    {
        throw std::runtime_error("entity has no transform");
    }
    return *transform;
}

CharacterControllerComponent *Character(ScriptRuntime &runtime, const LuaEntity &entity)
{
    RequireValid(runtime, entity);
    return runtime.Registry->try_get<CharacterControllerComponent>(entity.Value);
}

CharacterControllerComponent &RequireCharacter(ScriptRuntime &runtime, const LuaEntity &entity)
{
    CharacterControllerComponent *character = Character(runtime, entity);
    if (!character)
    {
        throw std::runtime_error("entity has no characterController");
    }
    return *character;
}

// The animator that animates this entity: on itself or on the nearest descendant (the skinned mesh is
// usually a child of the scripted character root). entt::null if there is none.
entt::entity FindAnimatorEntity(entt::registry &registry, entt::entity root)
{
    if (registry.all_of<AnimatorComponent>(root))
    {
        return root;
    }
    const auto *rootId = registry.try_get<EntityIdComponent>(root);
    if (!rootId)
    {
        return entt::null;
    }
    std::unordered_set<std::string> frontier{rootId->Id};
    std::unordered_set<std::string> seen = frontier;
    while (!frontier.empty())
    {
        std::unordered_set<std::string> next;
        for (const auto [child, parent] : registry.view<ParentTransformComponent>().each())
        {
            if (!frontier.contains(parent.ParentId))
            {
                continue;
            }
            if (registry.all_of<AnimatorComponent>(child))
            {
                return child;
            }
            if (const auto *id = registry.try_get<EntityIdComponent>(child); id && seen.insert(id->Id).second)
            {
                next.insert(id->Id);
            }
        }
        frontier = std::move(next);
    }
    return entt::null;
}

std::string UnknownClipMessage(const AnimatorComponent &animator, const std::string &name)
{
    std::vector<std::string> names;
    for (const auto &[clip, handle] : animator.Clips) names.push_back(clip);
    std::sort(names.begin(), names.end());
    std::string message = "no animation named '" + name + "' (available: ";
    for (std::size_t index = 0; index < names.size() && index < 8; ++index)
    {
        message += (index ? ", " : "") + names[index];
    }
    return message + (names.size() > 8 ? ", ...)" : ")");
}

glm::vec3 Finite(const glm::vec3 &value, const char *what)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
    {
        throw std::runtime_error(std::string(what) + " must be finite");
    }
    return value;
}

// ---------------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------------

void RegisterVec3(sol::state &lua)
{
    auto vec3 = lua.new_usertype<glm::vec3>(
        "vec3", sol::call_constructor,
        sol::factories([] { return glm::vec3(0.0f); }, [](float s) { return glm::vec3(s); },
                       [](float x, float y, float z) { return glm::vec3(x, y, z); }),
        sol::meta_function::addition, [](const glm::vec3 &a, const glm::vec3 &b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3 &a, const glm::vec3 &b) { return a - b; },
        sol::meta_function::multiplication,
        sol::overload([](const glm::vec3 &a, float s) { return a * s; },
                      [](float s, const glm::vec3 &a) { return a * s; },
                      [](const glm::vec3 &a, const glm::vec3 &b) { return a * b; }),
        sol::meta_function::division,
        sol::overload([](const glm::vec3 &a, float s) { return a / s; },
                      [](const glm::vec3 &a, const glm::vec3 &b) { return a / b; }),
        sol::meta_function::unary_minus, [](const glm::vec3 &a) { return -a; },
        sol::meta_function::equal_to, [](const glm::vec3 &a, const glm::vec3 &b) { return a == b; },
        sol::meta_function::to_string, &FormatVec3);

    vec3["x"] = sol::property([](const glm::vec3 &v) { return v.x; }, [](glm::vec3 &v, float x) { v.x = x; });
    vec3["y"] = sol::property([](const glm::vec3 &v) { return v.y; }, [](glm::vec3 &v, float y) { v.y = y; });
    vec3["z"] = sol::property([](const glm::vec3 &v) { return v.z; }, [](glm::vec3 &v, float z) { v.z = z; });
    vec3["length"] = [](const glm::vec3 &v) { return glm::length(v); };
    vec3["length_squared"] = [](const glm::vec3 &v) { return glm::dot(v, v); };
    vec3["normalized"] = [](const glm::vec3 &v) {
        const float length = glm::length(v);
        return length > 1e-6f ? v / length : glm::vec3(0.0f);
    };
    vec3["dot"] = [](const glm::vec3 &a, const glm::vec3 &b) { return glm::dot(a, b); };
    vec3["cross"] = [](const glm::vec3 &a, const glm::vec3 &b) { return glm::cross(a, b); };
    vec3["distance"] = [](const glm::vec3 &a, const glm::vec3 &b) { return glm::distance(a, b); };
    vec3["flat"] = [](const glm::vec3 &v) { return glm::vec3(v.x, 0.0f, v.z); };
    vec3["copy"] = [](const glm::vec3 &v) { return v; };
    vec3["lerp"] = [](const glm::vec3 &a, const glm::vec3 &b, float t) { return a + (b - a) * t; };
    vec3["zero"] = [] { return glm::vec3(0.0f); };
    vec3["up"] = [] { return WorldUp; };
}

void RegisterQuat(sol::state &lua)
{
    auto quat = lua.new_usertype<glm::quat>(
        "quat", sol::no_constructor,
        sol::meta_function::multiplication,
        sol::overload([](const glm::quat &q, const glm::vec3 &v) { return q * v; },
                      [](const glm::quat &a, const glm::quat &b) { return a * b; }),
        sol::meta_function::to_string, [](const glm::quat &q) {
            std::ostringstream out;
            out << "quat(" << q.w << ", " << q.x << ", " << q.y << ", " << q.z << ")";
            return out.str();
        });
    quat["identity"] = [] { return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); };
    quat["from_yaw"] = [](float radians) { return glm::angleAxis(radians, WorldUp); };
    quat["from_euler"] = [](float x, float y, float z) { return glm::quat(glm::vec3(x, y, z)); };
    quat["from_axis_angle"] = [](const glm::vec3 &axis, float radians) {
        const float length = glm::length(axis);
        return length > 1e-6f ? glm::angleAxis(radians, axis / length) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    };
    quat["look_rotation"] = &LookRotation;
    quat["slerp"] = [](const glm::quat &a, const glm::quat &b, float t) { return glm::slerp(a, b, t); };
    quat["yaw"] = &YawOf;
    quat["inverse"] = [](const glm::quat &q) { return glm::inverse(q); };
    quat["forward"] = [](const glm::quat &q) { return q * LocalForward; };
    quat["right"] = [](const glm::quat &q) { return q * LocalRight; };
}

void RegisterEntity(sol::state &lua, ScriptRuntime &rt)
{
    auto entity = lua.new_usertype<LuaEntity>(
        "Entity", sol::no_constructor, sol::meta_function::equal_to,
        [](const LuaEntity &a, const LuaEntity &b) { return a == b; }, sol::meta_function::to_string,
        [&rt](const LuaEntity &e) {
            std::string id;
            if (IsValid(rt, e))
            {
                if (const auto *component = rt.Registry->try_get<EntityIdComponent>(e.Value))
                {
                    id = component->Id;
                }
            }
            return "Entity(" + std::to_string(entt::to_integral(e.Value)) + (id.empty() ? "" : " '" + id + "'") +
                   ")";
        });

    entity["valid"] = [&rt](const LuaEntity &e) { return IsValid(rt, e); };
    // Small integer, stable while the entity lives; handy as a table key.
    entity["index"] = [](const LuaEntity &e) { return entt::to_integral(e.Value); };
    entity["id"] = [&rt](const LuaEntity &e) {
        RequireValid(rt, e);
        const auto *component = rt.Registry->try_get<EntityIdComponent>(e.Value);
        return component ? component->Id : std::string();
    };
    entity["name"] = [&rt](const LuaEntity &e) {
        RequireValid(rt, e);
        const auto *component = rt.Registry->try_get<NameComponent>(e.Value);
        return component ? component->Name : std::string();
    };

    // Transform. Setting the position of a character teleports it.
    entity["position"] = [&rt](const LuaEntity &e) { return Transform(rt, e).Translation; };
    entity["set_position"] = [&rt](const LuaEntity &e, const glm::vec3 &p) {
        Transform(rt, e).Translation = Finite(p, "position");
    };
    entity["rotation"] = [&rt](const LuaEntity &e) { return Transform(rt, e).Rotation; };
    entity["set_rotation"] = [&rt](const LuaEntity &e, const glm::quat &q) {
        const float length = glm::length(q);
        if (!(length > 1e-6f) || !std::isfinite(length))
        {
            throw std::runtime_error("rotation must be a finite, non-zero quaternion");
        }
        Transform(rt, e).Rotation = q / length;
    };
    entity["yaw"] = [&rt](const LuaEntity &e) { return YawOf(Transform(rt, e).Rotation); };
    entity["set_yaw"] = [&rt](const LuaEntity &e, float radians) {
        if (!std::isfinite(radians))
        {
            throw std::runtime_error("yaw must be finite");
        }
        Transform(rt, e).Rotation = glm::angleAxis(radians, WorldUp);
    };
    entity["scale"] = [&rt](const LuaEntity &e) { return Transform(rt, e).Scale; };
    entity["set_scale"] = [&rt](const LuaEntity &e, const glm::vec3 &s) {
        Transform(rt, e).Scale = Finite(s, "scale");
    };
    entity["forward"] = [&rt](const LuaEntity &e) { return Transform(rt, e).Rotation * LocalForward; };
    entity["right"] = [&rt](const LuaEntity &e) { return Transform(rt, e).Rotation * LocalRight; };
    entity["look_at"] = [&rt](const LuaEntity &e, const glm::vec3 &target) {
        TransformComponent &transform = Transform(rt, e);
        transform.Rotation = LookRotation(Finite(target, "target") - transform.Translation);
    };

    // Scripts.
    entity["script"] = [&rt](const LuaEntity &e) -> sol::object {
        RequireValid(rt, e);
        return rt.FindInstance(e.Value);
    };
    entity["destroy"] = [&rt](const LuaEntity &e) {
        if (IsValid(rt, e))
        {
            rt.PendingDestroy.push_back(e.Value);
        }
    };

    // Characters (CharacterControllerComponent).
    entity["is_character"] = [&rt](const LuaEntity &e) { return Character(rt, e) != nullptr; };
    entity["set_move_velocity"] = [&rt](const LuaEntity &e, const glm::vec3 &v) {
        RequireCharacter(rt, e).DesiredVelocity = Finite(v, "move velocity");
    };
    entity["move_velocity"] = [&rt](const LuaEntity &e) { return RequireCharacter(rt, e).DesiredVelocity; };
    entity["is_grounded"] = [&rt](const LuaEntity &e) {
        const CharacterControllerComponent *character = Character(rt, e);
        return character && character->Grounded;
    };
    entity["ground_normal"] = [&rt](const LuaEntity &e) { return RequireCharacter(rt, e).GroundNormal; };

    // Velocity of a character or a rigid body; zero for anything else.
    entity["velocity"] = [&rt](const LuaEntity &e) {
        if (const CharacterControllerComponent *character = Character(rt, e))
        {
            return character->Velocity;
        }
        if (rt.Physics)
        {
            if (const auto velocity = rt.Physics->GetLinearVelocity(*rt.Registry, e.Value))
            {
                return *velocity;
            }
        }
        return glm::vec3(0.0f);
    };

    // Cameras (CameraComponent).
    const auto camera = [&rt](const LuaEntity &e) -> CameraComponent & {
        RequireValid(rt, e);
        CameraComponent *component = rt.Registry->try_get<CameraComponent>(e.Value);
        if (!component)
        {
            throw std::runtime_error("entity has no camera");
        }
        return *component;
    };
    entity["is_camera"] = [&rt](const LuaEntity &e) {
        RequireValid(rt, e);
        return rt.Registry->all_of<CameraComponent>(e.Value);
    };
    entity["is_orthographic"] = [camera](const LuaEntity &e) {
        return camera(e).Projection == CameraProjection::Orthographic;
    };
    // size: half the view height in metres.
    entity["set_orthographic"] = [camera](const LuaEntity &e, sol::optional<float> size) {
        CameraComponent &component = camera(e);
        if (size)
        {
            if (!(*size > 0.0f) || !std::isfinite(*size))
            {
                throw std::runtime_error("orthographic size must be positive");
            }
            component.OrthographicSize = *size;
        }
        component.Projection = CameraProjection::Orthographic;
    };
    entity["set_perspective"] = [camera](const LuaEntity &e, sol::optional<float> fovDegrees) {
        CameraComponent &component = camera(e);
        if (fovDegrees)
        {
            if (!(*fovDegrees > 0.0f && *fovDegrees < 180.0f))
            {
                throw std::runtime_error("field of view must be between 0 and 180 degrees");
            }
            component.FieldOfViewDegrees = *fovDegrees;
        }
        component.Projection = CameraProjection::Perspective;
    };
    entity["ortho_size"] = [camera](const LuaEntity &e) { return camera(e).OrthographicSize; };
    entity["fov"] = [camera](const LuaEntity &e) { return camera(e).FieldOfViewDegrees; };

    // Animation. Methods act on the entity's animator, or on the animator of its nearest descendant.
    const auto animatorEntity = [&rt](const LuaEntity &e) {
        RequireValid(rt, e);
        return FindAnimatorEntity(*rt.Registry, e.Value);
    };
    const auto animator = [&rt, animatorEntity](const LuaEntity &e) -> AnimatorComponent & {
        const entt::entity target = animatorEntity(e);
        if (target == entt::null)
        {
            throw std::runtime_error("entity has no animator (on itself or a child)");
        }
        return rt.Registry->get<AnimatorComponent>(target);
    };
    const auto clipDuration = [](const AnimatorComponent &component, const std::string &name) {
        const auto found = component.Clips.find(name);
        if (found == component.Clips.end())
        {
            throw std::runtime_error(UnknownClipMessage(component, name));
        }
        const AnimationClip *clip = AssetManager::GetAnimationClip(found->second);
        return clip && std::isfinite(clip->Duration) ? clip->Duration : 0.0f;
    };
    entity["has_animator"] = [animatorEntity](const LuaEntity &e) { return animatorEntity(e) != entt::null; };
    entity["has_animation"] = [&rt, animatorEntity](const LuaEntity &e, const std::string &name) {
        const entt::entity target = animatorEntity(e);
        return target != entt::null && rt.Registry->get<AnimatorComponent>(target).Clips.contains(name);
    };
    // options: { fade = seconds (default: the animator's defaultFade), loop = false, speed = 1 }.
    // A one-shot pauses locomotion and returns to it when it ends.
    entity["play_animation"] = [animator](const LuaEntity &e, const std::string &name,
                                          sol::optional<sol::table> options) {
        AnimatorComponent &component = animator(e);
        float fade = -1.0f;
        bool loop = false;
        float speed = 1.0f;
        if (options)
        {
            if (const sol::optional<float> value = (*options)["fade"]; value)
            {
                if (!(*value >= 0.0f) || !std::isfinite(*value)) throw std::runtime_error("fade must be >= 0");
                fade = *value;
            }
            loop = (*options)["loop"].get_or(false);
            speed = (*options)["speed"].get_or(1.0f);
            if (!std::isfinite(speed)) throw std::runtime_error("speed must be finite");
        }
        if (!AnimationSystem::Play(component, name, fade, loop, speed))
        {
            throw std::runtime_error(UnknownClipMessage(component, name));
        }
    };
    // Fades back to locomotion, or freezes the current clip if the animator has no locomotion.
    entity["stop_animation"] = [&rt, animatorEntity](const LuaEntity &e, sol::optional<float> fade) {
        const entt::entity target = animatorEntity(e);
        if (target == entt::null)
        {
            throw std::runtime_error("entity has no animator (on itself or a child)");
        }
        AnimatorComponent &component = rt.Registry->get<AnimatorComponent>(target);
        if (rt.Registry->all_of<LocomotionAnimationComponent>(target))
        {
            AnimationSystem::ReturnToLocomotion(component, fade && std::isfinite(*fade) ? std::max(*fade, 0.0f) : -1.0f);
        }
        else
        {
            component.IsPlaying = false;
        }
    };
    // The clip playing, or "locomotion" while the speed blend drives the animator.
    entity["animation"] = [animator](const LuaEntity &e) {
        const AnimatorComponent &component = animator(e);
        return component.Source == AnimatorSource::Locomotion ? std::string("locomotion") : component.CurrentClipName;
    };
    entity["animation_time"] = [animator](const LuaEntity &e) { return animator(e).CurrentTime; };
    entity["animation_length"] = [animator, clipDuration](const LuaEntity &e, sol::optional<std::string> name) {
        const AnimatorComponent &component = animator(e);
        return clipDuration(component, name ? *name : component.CurrentClipName);
    };
    // True from when a one-shot reaches its end until the animator moves on (one tick with locomotion).
    entity["animation_finished"] = [animator](const LuaEntity &e) {
        const AnimatorComponent &component = animator(e);
        return component.Source == AnimatorSource::Clip && (component.OneShotFinished || !component.IsPlaying);
    };
    entity["locomotion_speed"] = [animator](const LuaEntity &e) { return animator(e).LocomotionSpeed; };

    // Rigid bodies (dynamic or kinematic). Return false when the entity has no live body.
    entity["add_impulse"] = [&rt](const LuaEntity &e, const glm::vec3 &impulse) {
        RequireValid(rt, e);
        return rt.Physics && rt.Physics->AddImpulse(*rt.Registry, e.Value, Finite(impulse, "impulse"));
    };
    entity["set_velocity"] = [&rt](const LuaEntity &e, const glm::vec3 &velocity) {
        RequireValid(rt, e);
        return rt.Physics && rt.Physics->SetLinearVelocity(*rt.Registry, e.Value, Finite(velocity, "velocity"));
    };
    entity["set_kinematic"] = [&rt](const LuaEntity &e, bool kinematic) {
        RequireValid(rt, e);
        return rt.Physics && rt.Physics->SetMotionType(*rt.Registry, e.Value,
                                                       kinematic ? PhysicsMotionType::Kinematic
                                                                 : PhysicsMotionType::Dynamic);
    };
    entity["is_kinematic"] = [&rt](const LuaEntity &e) {
        RequireValid(rt, e);
        const auto *body = rt.Registry->try_get<RigidBodyComponent>(e.Value);
        return body && body->Motion == PhysicsMotionType::Kinematic;
    };
    entity["has_body"] = [&rt](const LuaEntity &e) {
        RequireValid(rt, e);
        const auto *body = rt.Registry->try_get<RigidBodyComponent>(e.Value);
        return body && body->BodyId != InvalidPhysicsBodyId;
    };
}

void RegisterScene(sol::state &lua, ScriptRuntime &rt)
{
    sol::table scene = lua.create_named_table("scene");
    scene["viewport_aspect"] = [&rt] { return rt.ViewportAspect; };
    scene["find"] = [&rt](const std::string &id) -> sol::optional<LuaEntity> {
        auto view = Registry(rt).view<EntityIdComponent>();
        for (const entt::entity entity : view)
        {
            if (view.get<EntityIdComponent>(entity).Id == id)
            {
                return LuaEntity{entity};
            }
        }
        return sol::nullopt;
    };
    scene["find_by_name"] = [&rt](const std::string &name) -> sol::optional<LuaEntity> {
        auto view = Registry(rt).view<NameComponent>();
        for (const entt::entity entity : view)
        {
            if (view.get<NameComponent>(entity).Name == name)
            {
                return LuaEntity{entity};
            }
        }
        return sol::nullopt;
    };
    scene["primary_camera"] = [&rt]() -> sol::optional<LuaEntity> {
        auto view = Registry(rt).view<TransformComponent, CameraComponent>();
        for (const entt::entity entity : view)
        {
            if (view.get<CameraComponent>(entity).Primary)
            {
                return LuaEntity{entity};
            }
        }
        return sol::nullopt;
    };
    scene["entities_with_script"] = [&rt](const std::string &path, sol::this_state state) {
        sol::state_view lua(state);
        sol::table result = lua.create_table();
        auto view = Registry(rt).view<ScriptComponent>();
        int index = 1;
        for (const entt::entity entity : view)
        {
            if (view.get<ScriptComponent>(entity).Path == path)
            {
                result[index++] = LuaEntity{entity};
            }
        }
        return result;
    };
    scene["characters"] = [&rt](sol::this_state state) {
        sol::state_view lua(state);
        sol::table result = lua.create_table();
        int index = 1;
        for (const entt::entity entity : Registry(rt).view<CharacterControllerComponent>())
        {
            result[index++] = LuaEntity{entity};
        }
        return result;
    };
    scene["destroy"] = [&rt](const LuaEntity &e) {
        if (IsValid(rt, e))
        {
            rt.PendingDestroy.push_back(e.Value);
        }
    };
}

void RegisterPhysics(sol::state &lua, ScriptRuntime &rt)
{
    sol::table physics = lua.create_named_table("physics");
    physics["raycast"] = [&rt](const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                               sol::optional<bool> includeTriggers, sol::this_state state) -> sol::object {
        sol::state_view lua(state);
        if (!rt.Physics || !rt.Physics->IsInitialized())
        {
            return sol::make_object(lua, sol::lua_nil);
        }
        const PhysicsLayerMask layers = includeTriggers.value_or(false) ? AllPhysicsLayers : DefaultQueryLayers;
        const auto hit = rt.Physics->Raycast(origin, direction, maxDistance, layers);
        if (!hit)
        {
            return sol::make_object(lua, sol::lua_nil);
        }
        sol::table result = lua.create_table();
        result["entity"] = LuaEntity{hit->Entity};
        result["point"] = hit->Point;
        result["normal"] = hit->Normal;
        result["distance"] = hit->Distance;
        return result;
    };
    physics["overlap_sphere"] = [&rt](const glm::vec3 &center, float radius, sol::optional<bool> includeTriggers,
                                      sol::this_state state) {
        sol::state_view lua(state);
        sol::table result = lua.create_table();
        if (!rt.Physics || !rt.Physics->IsInitialized())
        {
            return result;
        }
        std::vector<entt::entity> hits;
        const PhysicsLayerMask layers = includeTriggers.value_or(false) ? AllPhysicsLayers : DefaultQueryLayers;
        rt.Physics->OverlapSphere(center, radius, hits, layers);
        int index = 1;
        for (const entt::entity entity : hits)
        {
            result[index++] = LuaEntity{entity};
        }
        return result;
    };
}

void RegisterInput(sol::state &lua, ScriptRuntime &rt)
{
    // Every query reports "nothing pressed" while gameplay input is disabled or there is no window.
    auto active = [&rt] { return rt.InputEnabled && InputManager::isInitialized(); };
    auto mouseActive = [&rt] { return rt.InputEnabled && rt.MouseEnabled && InputManager::isInitialized(); };

    sol::table input = lua.create_named_table("input");
    input["key_down"] = [active](const std::string &name) {
        const gainput::Key key = Lookup(KeyNames(), name, "key");
        return active() && InputManager::isKeyDown(key);
    };
    input["key_pressed"] = [active](const std::string &name) {
        const gainput::Key key = Lookup(KeyNames(), name, "key");
        return active() && InputManager::isKeyPressed(key);
    };
    input["key_released"] = [active](const std::string &name) {
        const gainput::Key key = Lookup(KeyNames(), name, "key");
        return active() && InputManager::isKeyReleased(key);
    };
    input["mouse_down"] = [mouseActive](const std::string &name) {
        const gainput::MouseButton button = Lookup(MouseNames(), name, "mouse button");
        return mouseActive() && InputManager::isMouseButtonDown(button);
    };
    input["mouse_pressed"] = [mouseActive](const std::string &name) {
        const gainput::MouseButton button = Lookup(MouseNames(), name, "mouse button");
        return mouseActive() && InputManager::isMouseButtonPressed(button);
    };
    input["gamepad_connected"] = [active](int pad) {
        return active() && InputManager::isGamepadConnected(PadIndex(pad));
    };
    input["gamepad_down"] = [active](int pad, const std::string &name) {
        const gainput::PadButton button = Lookup(PadButtonNames(), name, "gamepad button");
        return active() && InputManager::isGamepadButtonDown(PadIndex(pad), button);
    };
    input["gamepad_pressed"] = [active](int pad, const std::string &name) {
        const gainput::PadButton button = Lookup(PadButtonNames(), name, "gamepad button");
        return active() && InputManager::isGamepadButtonPressed(PadIndex(pad), button);
    };
    input["gamepad_axis"] = [active](int pad, const std::string &name) {
        const gainput::PadButton axis = Lookup(PadAxisNames(), name, "gamepad axis");
        return active() ? InputManager::getGamepadAxis(PadIndex(pad), axis) : 0.0f;
    };
    // WASD + gamepad 0 left stick: x = right, y = forward, length <= 1.
    input["move"] = [active](sol::this_state state) {
        sol::state_view lua(state);
        glm::vec2 move = active() ? PlayerControllerSystem::ReadInput().Move : glm::vec2(0.0f);
        if (const float length = glm::length(move); length > 1.0f)
        {
            move /= length;
        }
        sol::table result = lua.create_table();
        result["x"] = move.x;
        result["y"] = move.y;
        return result;
    };
}

std::string JoinArguments(sol::variadic_args args, sol::this_state state)
{
    sol::state_view lua(state);
    sol::protected_function toString = lua["tostring"];
    std::string text;
    bool first = true;
    for (const sol::stack_proxy argument : args)
    {
        if (!first)
        {
            text += '\t';
        }
        first = false;
        const sol::protected_function_result converted = toString(argument.get<sol::object>());
        text += converted.valid() && converted.get_type() == sol::type::string ? converted.get<std::string>()
                                                                               : std::string("?");
    }
    return text;
}

void RegisterLog(sol::state &lua)
{
    sol::table log = lua.create_named_table("log");
    log["info"] = [](sol::variadic_args args, sol::this_state state) {
        Logger::Info() << "[lua] " << JoinArguments(args, state);
    };
    log["warn"] = [](sol::variadic_args args, sol::this_state state) {
        Logger::Warning() << "[lua] " << JoinArguments(args, state);
    };
    log["error"] = [](sol::variadic_args args, sol::this_state state) {
        Logger::Error() << "[lua] " << JoinArguments(args, state);
    };
    lua["print"] = [](sol::variadic_args args, sol::this_state state) {
        Logger::Info() << "[lua] " << JoinArguments(args, state);
    };
}

// ---------------------------------------------------------------- draw (on_draw only)

struct DrawOptions
{
    glm::vec4 Color{1.0f};
    float Size = 18.0f;
    float OffsetY = 0.0f;
    float Outline = 0.0f;
    bool DepthTest = true;
    float MaxDistance = 0.0f;
    int Layer = 10;
};

float OptionNumber(const sol::table &table, const char *key, float fallback)
{
    const sol::object value = table[key];
    if (value.get_type() == sol::type::lua_nil)
    {
        return fallback;
    }
    if (value.get_type() != sol::type::number || !std::isfinite(value.as<float>()))
    {
        throw std::runtime_error(std::string("draw option '") + key + "' must be a finite number");
    }
    return value.as<float>();
}

// Colours are {r, g, b[, a]} or {r=, g=, b=, a=} with 0..1 components.
glm::vec4 ParseColor(const sol::object &value)
{
    if (value.get_type() != sol::type::table)
    {
        throw std::runtime_error("draw color must be a table {r, g, b, a}");
    }
    const sol::table table = value;
    const bool named = table["r"].get_type() != sol::type::lua_nil;
    const auto component = [&](int index, const char *name, float fallback) {
        const sol::object c = named ? sol::object(table[name]) : sol::object(table[index]);
        if (c.get_type() == sol::type::lua_nil)
        {
            return fallback;
        }
        if (c.get_type() != sol::type::number || !std::isfinite(c.as<float>()))
        {
            throw std::runtime_error("draw color components must be finite numbers");
        }
        return std::clamp(c.as<float>(), 0.0f, 1.0f);
    };
    return {component(1, "r", 1.0f), component(2, "g", 1.0f), component(3, "b", 1.0f), component(4, "a", 1.0f)};
}

DrawOptions ParseDrawOptions(const sol::optional<sol::table> &options)
{
    DrawOptions result;
    if (!options)
    {
        return result;
    }
    const sol::table &table = *options;
    if (const sol::object color = table["color"]; color.get_type() != sol::type::lua_nil)
    {
        result.Color = ParseColor(color);
    }
    result.Size = std::max(OptionNumber(table, "size", result.Size), 1.0f);
    result.OffsetY = OptionNumber(table, "offset_y", result.OffsetY);
    result.Outline = std::max(OptionNumber(table, "outline", result.Outline), 0.0f);
    result.MaxDistance = std::max(OptionNumber(table, "max_distance", result.MaxDistance), 0.0f);
    result.Layer = static_cast<int>(OptionNumber(table, "layer", static_cast<float>(result.Layer)));
    if (const sol::object depth = table["depth_test"]; depth.get_type() == sol::type::boolean)
    {
        result.DepthTest = depth.as<bool>();
    }
    return result;
}

Canvas &RequireCanvas(const ScriptRuntime &rt)
{
    if (!rt.DrawCanvas)
    {
        throw std::runtime_error("draw.* only works inside on_draw");
    }
    return *rt.DrawCanvas;
}

void RegisterDraw(sol::state &lua, ScriptRuntime &rt)
{
    sol::table draw = lua.create_named_table("draw");

    // World-anchored (billboards): always face the camera and keep a constant pixel size.
    draw["label"] = [&rt](const glm::vec3 &position, const std::string &text, sol::optional<sol::table> options) {
        Canvas &canvas = RequireCanvas(rt);
        const DrawOptions o = ParseDrawOptions(options);
        if (!rt.DrawFont)
        {
            return;
        }
        const CanvasAnchor anchor{Finite(position, "label position"), o.DepthTest, o.MaxDistance};
        canvas.DrawLabel(anchor, *rt.DrawFont, text, o.Size, o.Color, o.OffsetY, o.Layer);
    };
    draw["rect"] = [&rt](const glm::vec3 &position, float x, float y, float w, float h,
                         sol::optional<sol::table> options) {
        Canvas &canvas = RequireCanvas(rt);
        const DrawOptions o = ParseDrawOptions(options);
        const CanvasAnchor anchor{Finite(position, "rect position"), o.DepthTest, o.MaxDistance};
        const CanvasRect rect{x, y, w, h};
        if (o.Outline > 0.0f)
        {
            canvas.StrokeRect(anchor, rect, o.Color, o.Outline, o.Layer);
        }
        else
        {
            canvas.FillRect(anchor, rect, o.Color, o.Layer);
        }
    };

    // Screen space (HUD), window pixels with the origin top-left.
    draw["screen_text"] = [&rt](float x, float y, const std::string &text, sol::optional<sol::table> options) {
        Canvas &canvas = RequireCanvas(rt);
        const DrawOptions o = ParseDrawOptions(options);
        if (rt.DrawFont)
        {
            canvas.DrawText(*rt.DrawFont, text, {x, y}, o.Size, o.Color, o.Layer);
        }
    };
    draw["screen_rect"] = [&rt](float x, float y, float w, float h, sol::optional<sol::table> options) {
        Canvas &canvas = RequireCanvas(rt);
        const DrawOptions o = ParseDrawOptions(options);
        const CanvasRect rect{x, y, w, h};
        if (o.Outline > 0.0f)
        {
            canvas.StrokeRect(rect, o.Color, o.Outline, o.Layer);
        }
        else
        {
            canvas.FillRect(rect, o.Color, o.Layer);
        }
    };
    draw["screen_size"] = [&rt]() {
        const glm::vec2 size = RequireCanvas(rt).GetSize();
        return std::make_tuple(size.x, size.y);
    };
    // Pixel size of a text block (0, 0 without a font), e.g. for a backing panel behind a label.
    draw["measure"] = [&rt](const std::string &text, sol::optional<float> size) {
        RequireCanvas(rt);
        const float pixelSize = std::max(size.value_or(18.0f), 1.0f);
        const glm::vec2 measured = rt.DrawFont ? rt.DrawFont->MeasureText(text, pixelSize) : glm::vec2(0.0f);
        return std::make_tuple(measured.x, measured.y);
    };
}

} // namespace

void RegisterScriptBindings(sol::state &lua, ScriptRuntime &runtime)
{
    RegisterVec3(lua);
    RegisterQuat(lua);
    RegisterEntity(lua, runtime);
    RegisterScene(lua, runtime);
    RegisterPhysics(lua, runtime);
    RegisterInput(lua, runtime);
    RegisterDraw(lua, runtime);
    RegisterLog(lua);
    lua.create_named_table("time");
    UpdateScriptTime(lua, runtime);
}

void UpdateScriptTime(sol::state &lua, const ScriptRuntime &runtime)
{
    sol::table time = lua["time"];
    time["dt"] = runtime.DeltaTime;
    time["elapsed"] = runtime.Elapsed;
    time["frame"] = runtime.Frame;
}

} // namespace Haki
