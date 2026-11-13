#pragma once

#include "Haki/Renderer/RendererTypes.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scene/SceneCamera.h"
#include "Haki/Scripting/ScriptSystem.h"
#include "Haki/Systems/PhysicsSystem.h"

#include <entt/entt.hpp>

#include <any>
#include <filesystem>
#include <string>
#include <cstddef>
#include <optional>

namespace Haki
{

class Renderer;

struct SceneRenderContext
{
    Renderer *Render = nullptr;
    int Width = 0;
    int Height = 0;
    // When set, rendered instead of the scene's primary camera (debug free-look).
    const SceneCameraOverride *CameraOverride = nullptr;
};

class SceneEnterContext
{
public:
    explicit SceneEnterContext(const std::any &payload) : payload_(payload) {}

    bool HasPayload() const { return payload_.has_value(); }

    template <typename T>
    const T *TryGet() const
    {
        return std::any_cast<T>(&payload_);
    }

    template <typename T>
    const T &Get() const
    {
        return std::any_cast<const T &>(payload_);
    }

private:
    const std::any &payload_;
};

class Scene
{
public:
    virtual ~Scene() = default;

    Scene() = default;

    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    Scene(Scene &&) = delete;
    Scene &operator=(Scene &&) = delete;

    virtual void OnLoad() {}
    virtual void OnEnter(const SceneEnterContext & /*context*/) {}
    virtual void OnUpdate(float deltaTime);
    virtual void OnRender(const SceneRenderContext &context);
    virtual void OnExit() {}
    virtual void OnUnload() {}

    entt::registry &GetRegistry() { return registry_; }
    const entt::registry &GetRegistry() const { return registry_; }
    void SetEnvironment(const EnvironmentData &environment) { environment_ = environment; }
    const EnvironmentData &GetEnvironment() const { return environment_; }
    // Initialized lazily on the first runtime update once PhysicsCore is initialized.
    PhysicsSystem &GetPhysics() { return physics_; }
    const PhysicsSystem &GetPhysics() const { return physics_; }

    // Lua scripting. Initialized lazily once the scene has a scene script or any ScriptComponent.
    ScriptSystem &GetScripts() { return scripts_; }
    const ScriptSystem &GetScripts() const { return scripts_; }
    // Where script paths are resolved from, and the optional scene-level script. Set before the first update.
    void SetScripting(std::filesystem::path scriptRoot, std::string sceneScript);
    const std::string &GetSceneScript() const { return sceneScript_; }
    // False while the debug free-look camera or ImGui owns the keyboard: Lua input then reads as idle.
    void SetGameplayInputEnabled(bool enabled) { gameplayInputEnabled_ = enabled; }

    // The primary camera's view and projection, or nothing if the scene has no primary camera.
    std::optional<SceneCameraOverride> GetPrimaryCameraView() const;
    static SceneCameraOverride MakeCameraView(const TransformComponent &transform, const CameraComponent &camera);

    // Engine-owned systems run independently of overrides of OnUpdate.
    void UpdateRuntime(float deltaTime);

private:
    struct RenderStorageSummary
    {
        std::size_t Cameras = 0;
        std::size_t PrimaryCameras = 0;
        std::size_t DirectionalLights = 0;
        std::size_t PointLights = 0;
        std::size_t SpotLights = 0;
        std::size_t Renderables = 0;
        std::size_t SkinnedRenderables = 0;
        std::size_t SubmittedSubmeshes = 0;

        bool operator==(const RenderStorageSummary &) const = default;
    };

    void LogRenderStorageIfChanged(const RenderStorageSummary &summary);

    entt::registry registry_;
    // Declared after the registry so it is destroyed (and detaches its registry hooks) first.
    PhysicsSystem physics_;
    // Declared after physics so scripts (which query physics) shut down first.
    ScriptSystem scripts_;
    std::filesystem::path scriptRoot_ = "Assets/Scripts";
    std::string sceneScript_;
    bool gameplayInputEnabled_ = true;
    EnvironmentData environment_;
    std::optional<RenderStorageSummary> lastRenderStorageSummary_;
};

} // namespace Haki
