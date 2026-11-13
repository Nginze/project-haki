#include "Haki/Systems/TransformHierarchySystem.h"
#include "Haki/Scene/Scene.h"

#include "Haki/AssetManager/AssetManager.h"
#include "Haki/GL/Mesh.h"
#include "Haki/Logging/Logger.h"
#include "Haki/Physics/PhysicsCore.h"
#include "Haki/Renderer/Renderer.h"
#include "Haki/Scene/Components.h"
#include "Haki/Systems/AnimationSystem.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace Haki
{

void Scene::OnUpdate(float /*deltaTime*/)
{
}

void Scene::SetScripting(std::filesystem::path scriptRoot, std::string sceneScript)
{
    scriptRoot_ = std::move(scriptRoot);
    sceneScript_ = std::move(sceneScript);
}

void Scene::UpdateRuntime(float deltaTime)
{
    if (!physics_.IsInitialized() && PhysicsCore::IsInitialized())
    {
        physics_.Initialize(registry_);
    }
    if (!scripts_.IsInitialized() && (!sceneScript_.empty() || !registry_.view<ScriptComponent>().empty()))
    {
        scripts_.Initialize(registry_, &physics_, scriptRoot_, sceneScript_);
    }

    if (simulationPaused_)
    {
        // Paused: only a requested step advances time.
        if (stepRequested_) stepRequested_ = false;
        else deltaTime = 0.0f;
    }

    using Clock = std::chrono::steady_clock;
    const auto elapsedMs = [](Clock::time_point start) {
        return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
    };

    // Scripts steer characters and push bodies before the step, then react to its contacts.
    // A zero-length update (e.g. right after loading) only creates physics bodies; scripts start on the first tick.
    const bool ticking = deltaTime > 0.0f;
    scripts_.SetInputEnabled(gameplayInputEnabled_, gameplayMouseEnabled_);
    auto start = Clock::now();
    if (ticking)
    {
        scripts_.Update(registry_, deltaTime);
    }
    cpuTimings_.ScriptsMs += elapsedMs(start);

    start = Clock::now();
    physics_.Update(registry_, deltaTime);
    cpuTimings_.PhysicsMs += elapsedMs(start);
    if (ticking)
    {
        start = Clock::now();
        scripts_.DispatchContacts(registry_);
        cpuTimings_.ScriptsMs += elapsedMs(start);
    }

    start = Clock::now();
    TransformHierarchySystem::Update(registry_);
    cpuTimings_.SceneMs += elapsedMs(start);

    start = Clock::now();
    AnimationSystem::Update(registry_, deltaTime);
    cpuTimings_.AnimationMs += elapsedMs(start);
    ++cpuTimings_.Ticks;
}

SceneCameraOverride Scene::MakeCameraView(const TransformComponent &transform, const CameraComponent &camera)
{
    SceneCameraOverride view;
    view.WorldMatrix = transform.WorldMatrix;
    view.Position = transform.Translation;
    view.Projection = camera.Projection;
    view.FieldOfViewDegrees = camera.FieldOfViewDegrees;
    view.OrthographicSize = camera.OrthographicSize;
    view.NearPlane = camera.NearPlane;
    view.FarPlane = camera.FarPlane;
    return view;
}

std::optional<SceneCameraOverride> Scene::GetPrimaryCameraView() const
{
    auto cameras = registry_.view<TransformComponent, CameraComponent>();
    for (const entt::entity entity : cameras)
    {
        if (cameras.get<CameraComponent>(entity).Primary)
        {
            return MakeCameraView(cameras.get<TransformComponent>(entity), cameras.get<CameraComponent>(entity));
        }
    }
    return std::nullopt;
}

void Scene::OnRender(const SceneRenderContext &context)
{
    if (!context.Render || context.Width <= 0 || context.Height <= 0)
    {
        return;
    }

    const TransformComponent *cameraTransform = nullptr;
    const CameraComponent *camera = nullptr;
    RenderStorageSummary summary;
    auto cameras = registry_.view<TransformComponent, CameraComponent>();
    for (const entt::entity entity : cameras)
    {
        ++summary.Cameras;
        const CameraComponent &candidate = cameras.get<CameraComponent>(entity);
        if (candidate.Primary)
        {
            ++summary.PrimaryCameras;
            if (!camera)
            {
                cameraTransform = &cameras.get<TransformComponent>(entity);
                camera = &candidate;
            }
        }
    }

    std::vector<DirectionalLight> directionalLights;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;

    auto directionals = registry_.view<TransformComponent, DirectionalLightComponent>();
    for (const entt::entity entity : directionals)
    {
        const auto &transform = directionals.get<TransformComponent>(entity);
        const auto &light = directionals.get<DirectionalLightComponent>(entity);
        directionalLights.push_back({glm::normalize(transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f)),
                                     light.Color, light.Intensity});
        ++summary.DirectionalLights;
    }
    auto points = registry_.view<TransformComponent, PointLightComponent>();
    for (const entt::entity entity : points)
    {
        const auto &transform = points.get<TransformComponent>(entity);
        const auto &light = points.get<PointLightComponent>(entity);
        pointLights.push_back({transform.Translation, light.Color, light.Intensity, light.Range});
        ++summary.PointLights;
    }
    auto spots = registry_.view<TransformComponent, SpotLightComponent>();
    for (const entt::entity entity : spots)
    {
        const auto &transform = spots.get<TransformComponent>(entity);
        const auto &light = spots.get<SpotLightComponent>(entity);
        spotLights.push_back({transform.Translation,
                              glm::normalize(transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f)),
                              light.Color,
                              light.Intensity,
                              light.Range,
                              std::cos(glm::radians(light.InnerAngleDegrees)),
                              std::cos(glm::radians(light.OuterAngleDegrees))});
        ++summary.SpotLights;
    }

    auto renderables = registry_.view<TransformComponent, MeshRendererComponent>();
    for (const entt::entity entity : renderables)
    {
        (void)entity;
        ++summary.Renderables;
    }
    auto skinnedRenderables = registry_.view<TransformComponent, SkinnedMeshRendererComponent>();
    for (const entt::entity entity : skinnedRenderables)
    {
        (void)entity;
        ++summary.SkinnedRenderables;
    }

    SceneCameraOverride view;
    if (context.CameraOverride)
    {
        view = *context.CameraOverride;
    }
    else if (cameraTransform && camera)
    {
        view = MakeCameraView(*cameraTransform, *camera);
    }
    else
    {
        LogRenderStorageIfChanged(summary);
        return;
    }

    RenderFrameData frame;
    frame.Camera.View = glm::inverse(view.WorldMatrix);
    frame.Camera.Projection = BuildProjection(view, static_cast<float>(context.Width) / context.Height);
    frame.Camera.WorldPosition = view.Position;
    frame.Camera.Viewport = {0.0f, 0.0f, static_cast<float>(context.Width), static_cast<float>(context.Height)};
    frame.Camera.NearPlane = view.NearPlane;
    frame.Camera.FarPlane = view.FarPlane;
    frame.Environment = environment_;
    frame.DirectionalLights = directionalLights;
    frame.PointLights = pointLights;
    frame.SpotLights = spotLights;

    if (!context.Render->BeginFrame(frame))
    {
        LogRenderStorageIfChanged(summary);
        return;
    }

    for (const entt::entity entity : renderables)
    {
        const auto &transform = renderables.get<TransformComponent>(entity);
        const auto &meshRenderer = renderables.get<MeshRendererComponent>(entity);
        const Mesh *mesh = AssetManager::GetMesh(meshRenderer.Mesh);
        if (!mesh)
        {
            continue;
        }
        for (std::uint32_t submesh = 0; submesh < mesh->GetSubmeshCount(); ++submesh)
        {
            if (meshRenderer.Materials.empty())
            {
                continue;
            }
            if (meshRenderer.Materials.size() != 1 && submesh >= meshRenderer.Materials.size())
            {
                continue;
            }
            const MaterialHandle material = meshRenderer.Materials.size() == 1
                                                ? meshRenderer.Materials.front()
                                                : meshRenderer.Materials[submesh];
            context.Render->Submit({meshRenderer.Mesh, material, transform.WorldMatrix, submesh});
            ++summary.SubmittedSubmeshes;
        }
    }

    for (const entt::entity entity : skinnedRenderables)
    {
        const auto &transform = skinnedRenderables.get<TransformComponent>(entity);
        const auto &meshRenderer = skinnedRenderables.get<SkinnedMeshRendererComponent>(entity);
        const Mesh *mesh = AssetManager::GetMesh(meshRenderer.Mesh);
        if (!mesh || !mesh->IsSkinned() || meshRenderer.FinalBoneMatrices.empty())
        {
            continue;
        }
        for (std::uint32_t submesh = 0; submesh < mesh->GetSubmeshCount(); ++submesh)
        {
            if (meshRenderer.Materials.empty() ||
                (meshRenderer.Materials.size() != 1 && submesh >= meshRenderer.Materials.size()))
            {
                continue;
            }
            const MaterialHandle material = meshRenderer.Materials.size() == 1
                                                ? meshRenderer.Materials.front()
                                                : meshRenderer.Materials[submesh];
            context.Render->Submit({meshRenderer.Mesh, material, transform.WorldMatrix, submesh,
                                    meshRenderer.FinalBoneMatrices});
            ++summary.SubmittedSubmeshes;
        }
    }

    context.Render->Render();
    context.Render->EndFrame();
    LogRenderStorageIfChanged(summary);
}

void Scene::LogRenderStorageIfChanged(const RenderStorageSummary &summary)
{
    if (lastRenderStorageSummary_ && *lastRenderStorageSummary_ == summary)
    {
        return;
    }

    lastRenderStorageSummary_ = summary;
    Logger::Debug() << "Scene ECS: cameras=" << summary.Cameras << ", primary=" << summary.PrimaryCameras
                    << ", directionalLights=" << summary.DirectionalLights
                     << ", pointLights=" << summary.PointLights << ", spotLights=" << summary.SpotLights
                     << ", renderables=" << summary.Renderables
                     << ", skinnedRenderables=" << summary.SkinnedRenderables
                     << ", submittedSubmeshes=" << summary.SubmittedSubmeshes;
}

} // namespace Haki
