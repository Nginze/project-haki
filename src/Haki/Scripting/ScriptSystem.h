#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Haki
{

class PhysicsSystem;

struct ScriptReloadResult
{
    std::size_t Reloaded = 0;
    std::size_t Failed = 0;
};

// Runs Lua gameplay scripts for one scene (one sandboxed Lua 5.4 state per scene).
//  - Each entity with a ScriptComponent gets an instance of the class table its script returns:
//    on_start, on_update(dt), on_contact_begin/end(other), on_trigger_enter/exit(other), on_destroy.
//  - An optional scene script receives the same callbacks with self.entity = nil.
//  - A script error logs once and disables that instance until its script is reloaded.
//  - ReloadChanged re-runs edited files and swaps their functions in place, keeping instance state.
class ScriptSystem
{
public:
    ScriptSystem();
    ~ScriptSystem();

    ScriptSystem(const ScriptSystem &) = delete;
    ScriptSystem &operator=(const ScriptSystem &) = delete;
    ScriptSystem(ScriptSystem &&) = delete;
    ScriptSystem &operator=(ScriptSystem &&) = delete;

    // The registry (and physics, when given) must outlive this system, or Shutdown must be called first.
    // Script paths are relative to scriptRoot. physics may be null (queries then find nothing).
    bool Initialize(entt::registry &registry, PhysicsSystem *physics, std::filesystem::path scriptRoot,
                    std::string sceneScript = {});
    void Shutdown();
    bool IsInitialized() const { return state_ != nullptr; }

    // Creates instances for new ScriptComponents, calls on_start for new ones, then on_update for all.
    void Update(entt::registry &registry, float deltaTime);
    // Delivers the physics contact events of the most recent step to the scripts of both entities.
    void DispatchContacts(entt::registry &registry);

    // Re-runs scripts whose files changed on disk (all scripts when force is true).
    ScriptReloadResult ReloadChanged(bool force = false);

    // When false, the Lua input API reports nothing pressed (free-look camera or ImGui has the keyboard).
    void SetInputEnabled(bool enabled);
    // Screen-space camera queries use the current viewport, including before Lua initializes.
    void SetViewportSize(int width, int height);

    // Runs a chunk of Lua in the scene's state. Returns false and records the error on failure.
    bool Execute(const std::string &code, const std::string &chunkName = "console");

    std::size_t GetLoadedScriptCount() const;
    std::size_t GetInstanceCount() const;
    // Most recent script errors, oldest first (capped).
    const std::vector<std::string> &GetErrors() const;
    void ClearErrors();

private:
    struct State;

    void OnScriptDestroyed(entt::registry &registry, entt::entity entity);

    std::unique_ptr<State> state_;
    entt::registry *registry_ = nullptr;
    float viewportAspect_ = 16.0f / 9.0f;
};

} // namespace Haki
