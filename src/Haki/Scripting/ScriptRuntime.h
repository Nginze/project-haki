#pragma once

// Internal to the scripting module: shared between ScriptSystem.cpp and ScriptBindings.cpp.

#include <sol/sol.hpp>

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Haki
{

class PhysicsSystem;

// Lua-side entity handle. Every method checks that the entity still exists.
struct LuaEntity
{
    entt::entity Value = entt::null;

    bool operator==(const LuaEntity &other) const { return Value == other.Value; }
};

struct ScriptRuntime
{
    entt::registry *Registry = nullptr;
    PhysicsSystem *Physics = nullptr;
    bool InputEnabled = true;
    bool MouseEnabled = true; // false while the editor UI owns the mouse
    float ViewportAspect = 16.0f / 9.0f;

    float DeltaTime = 0.0f;
    double Elapsed = 0.0;
    std::uint64_t Frame = 0;

    // Destroys requested from Lua; applied after the current batch of callbacks.
    std::vector<entt::entity> PendingDestroy;

    // Returns the script instance table of an entity, or nil.
    std::function<sol::object(entt::entity)> FindInstance;
};

// Registers vec3, quat, Entity, scene, physics, input, time and log in the given state.
void RegisterScriptBindings(sol::state &lua, ScriptRuntime &runtime);
// Refreshes the `time` table before a tick.
void UpdateScriptTime(sol::state &lua, const ScriptRuntime &runtime);

} // namespace Haki
