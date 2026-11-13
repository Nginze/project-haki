#include "Haki/Debug/ImGuiManager.h"

#include "Haki/AssetManager/AssetManager.h"
#include "Haki/Renderer/Font.h"
#include "Haki/Renderer/Renderer.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scene/Scene.h"
#include "Haki/Systems/AnimationSystem.h"

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <limits>
#include <string>
#include <utility>

namespace Haki
{
namespace
{

std::string GetEntityName(entt::registry &registry, entt::entity entity)
{
    if (const auto *name = registry.try_get<NameComponent>(entity); name && !name->Name.empty())
    {
        return name->Name;
    }
    if (const auto *id = registry.try_get<EntityIdComponent>(entity); id && !id->Id.empty())
    {
        return id->Id;
    }
    return "Entity " + std::to_string(entt::to_integral(entity));
}

void UpdateWorldMatrix(TransformComponent &transform)
{
    transform.WorldMatrix = glm::translate(glm::mat4(1.0f), transform.Translation) *
                            glm::mat4_cast(transform.Rotation) *
                            glm::scale(glm::mat4(1.0f), transform.Scale);
}

bool DrawPosition(TransformComponent &transform)
{
    return ImGui::DragFloat3("Position", &transform.Translation.x, 0.05f);
}

bool DrawRotation(TransformComponent &transform)
{
    glm::vec3 rotationDegrees = glm::degrees(glm::eulerAngles(glm::normalize(transform.Rotation)));
    if (!ImGui::DragFloat3("Rotation", &rotationDegrees.x, 0.25f))
    {
        return false;
    }

    transform.Rotation = glm::normalize(glm::quat(glm::radians(rotationDegrees)));
    return true;
}

template <typename LightComponent>
void DrawCommonLightControls(LightComponent &light)
{
    if (!std::isfinite(light.Color.x) || !std::isfinite(light.Color.y) || !std::isfinite(light.Color.z))
    {
        light.Color = glm::vec3(1.0f);
    }
    if (!std::isfinite(light.Intensity) || light.Intensity < 0.0f)
    {
        light.Intensity = 0.0f;
    }
    ImGui::ColorEdit3("Color", &light.Color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Intensity", &light.Intensity, 0.05f, 0.0f, std::numeric_limits<float>::max(), "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
}

void DrawLightControls(DirectionalLightComponent &light)
{
    DrawCommonLightControls(light);
}

void DrawLightControls(PointLightComponent &light)
{
    DrawCommonLightControls(light);
    if (!std::isfinite(light.Range) || light.Range <= 0.0f)
    {
        light.Range = 10.0f;
    }
    ImGui::DragFloat("Range", &light.Range, 0.1f, 0.01f, std::numeric_limits<float>::max(), "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
}

void DrawLightControls(SpotLightComponent &light)
{
    constexpr float AngleStep = 0.1f;
    constexpr float MaximumAngle = 90.0f - AngleStep;

    DrawCommonLightControls(light);
    if (!std::isfinite(light.Range) || light.Range <= 0.0f)
    {
        light.Range = 10.0f;
    }
    if (!std::isfinite(light.InnerAngleDegrees) || !std::isfinite(light.OuterAngleDegrees))
    {
        light.InnerAngleDegrees = 20.0f;
        light.OuterAngleDegrees = 30.0f;
    }
    ImGui::DragFloat("Range", &light.Range, 0.1f, 0.01f, std::numeric_limits<float>::max(), "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);

    light.OuterAngleDegrees = std::clamp(light.OuterAngleDegrees, AngleStep, MaximumAngle);
    light.InnerAngleDegrees = std::clamp(light.InnerAngleDegrees, 0.0f, light.OuterAngleDegrees - AngleStep);
    ImGui::DragFloat("Inner Angle", &light.InnerAngleDegrees, AngleStep, 0.0f,
                     light.OuterAngleDegrees - AngleStep, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Outer Angle", &light.OuterAngleDegrees, AngleStep, light.InnerAngleDegrees + AngleStep,
                     MaximumAngle, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
}

template <typename LightComponent>
void DrawLightNode(entt::registry &registry, entt::entity entity, const char *type, bool showPosition,
                    bool showRotation)
{
    const std::string label = GetEntityName(registry, entity) + " (" + type + ")";
    ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
    ImGui::PushID(type);
    if (ImGui::TreeNode(label.c_str()))
    {
        TransformComponent &transform = registry.get<TransformComponent>(entity);
        bool changed = false;
        if (showPosition)
        {
            changed |= DrawPosition(transform);
        }
        if (showRotation)
        {
            changed |= DrawRotation(transform);
        }
        if (changed)
        {
            UpdateWorldMatrix(transform);
        }
        DrawLightControls(registry.get<LightComponent>(entity));
        ImGui::TreePop();
    }
    ImGui::PopID();
    ImGui::PopID();
}

void DrawRendererStats(const RendererStats &stats)
{
    const ImGuiIO &io = ImGui::GetIO();
    const float frameTimeMilliseconds = io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f;

    ImGui::Text("%.1f FPS  |  %.2f ms", io.Framerate, frameTimeMilliseconds);
    if (ImGui::BeginTable("RendererStats", 2, ImGuiTableFlags_SizingStretchProp))
    {
        const auto row = [](const char *label, std::uint32_t value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::Text("%u", value);
        };

        row("Draw calls", stats.DrawCalls);
        row("Shadow draw calls", stats.ShadowDrawCalls);
        row("Shadow cascades", stats.ShadowCascadesRendered);
        row("SSAO prepass draws", stats.SsaoPrepassDrawCalls);
        row("Depth prepass draws", stats.DepthPrepassDrawCalls);
        row("SSAO draws", stats.SsaoDrawCalls);
        row("Bloom draws", stats.BloomDrawCalls);
        row("Post-process draws", stats.PostProcessDrawCalls);
        row("FXAA draws", stats.FxaaDrawCalls);
        row("Skybox draws", stats.SkyboxDrawCalls);
        row("Transmission draws", stats.TransmissionDrawCalls);
        row("Transmission copies", stats.TransmissionSceneCopies);
        row("Submitted", stats.SubmittedItems);
        row("Skipped", stats.SkippedItems);
        row("Culled items", stats.CulledItems);
        row("Shadow culled draws", stats.ShadowCulledDraws);
        row("Debug lines", stats.DebugLines);
        row("Canvas quads", stats.CanvasQuads);
        row("Canvas draws", stats.CanvasDrawCalls);
        row("Shader changes", stats.ShaderChanges);
        row("Material changes", stats.MaterialChanges);
        row("Mesh changes", stats.MeshChanges);
        row("Texture changes", stats.TextureChanges);
        ImGui::EndTable();
    }
}

void DrawGpuTimings(RendererSettings &settings, const RendererGpuTimings &timings)
{
    ImGui::SeparatorText("GPU Timings");
    ImGui::Checkbox("Measure GPU Time", &settings.GpuTimingsEnabled);
    if (!settings.GpuTimingsEnabled)
    {
        return;
    }
    if (!timings.Valid)
    {
        ImGui::TextDisabled("Waiting for GPU results...");
        return;
    }
    if (ImGui::BeginTable("GpuTimings", 2, ImGuiTableFlags_SizingStretchProp))
    {
        const auto row = [](const char *label, float milliseconds)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f ms", milliseconds);
        };
        row("Shadows", timings.ShadowMs);
        row("Prepass + SSAO", timings.SsaoMs);
        row("Forward", timings.ForwardMs);
        row("Bloom", timings.BloomMs);
        row("Tonemap + FXAA", timings.PostProcessMs);
        row("Debug draw", timings.DebugDrawMs);
        row("UI canvas", timings.CanvasMs);
        row("Total", timings.TotalMs);
        ImGui::EndTable();
    }
}

void DrawLights(Scene *scene)
{
    ImGui::SeparatorText("Lights");
    if (!scene)
    {
        ImGui::TextDisabled("No active scene");
        return;
    }

    entt::registry &registry = scene->GetRegistry();
    std::size_t lightCount = 0;

    for (const entt::entity entity : registry.view<TransformComponent, DirectionalLightComponent>())
    {
        DrawLightNode<DirectionalLightComponent>(registry, entity, "Directional", false, true);
        ++lightCount;
    }
    for (const entt::entity entity : registry.view<TransformComponent, PointLightComponent>())
    {
        DrawLightNode<PointLightComponent>(registry, entity, "Point", true, false);
        ++lightCount;
    }
    for (const entt::entity entity : registry.view<TransformComponent, SpotLightComponent>())
    {
        DrawLightNode<SpotLightComponent>(registry, entity, "Spot", true, true);
        ++lightCount;
    }

    if (lightCount == 0)
    {
        ImGui::TextDisabled("No lights in the active scene");
    }
}

} // namespace

ImGuiManager::~ImGuiManager()
{
    Shutdown();
}

bool ImGuiManager::Initialize(GLFWwindow *window)
{
    if (initialized_)
    {
        return true;
    }
    if (!window)
    {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
    {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330"))
    {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    initialized_ = true;
    return true;
}

void ImGuiManager::Shutdown()
{
    if (!initialized_)
    {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void ImGuiManager::BeginFrame()
{
    if (!initialized_)
    {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }
    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::BeginMenu("Camera"))
        {
            if (ImGui::MenuItem("Scene Camera", nullptr, cameraMode_ == DebugCameraMode::Scene))
            {
                cameraMode_ = DebugCameraMode::Scene;
            }
#if HAKI_ENABLE_DEBUG_CAMERA
            constexpr bool freeLookAvailable = true;
#else
            constexpr bool freeLookAvailable = false;
#endif
            if (ImGui::MenuItem("Free Look", nullptr, cameraMode_ == DebugCameraMode::FreeLook, freeLookAvailable))
            {
                cameraMode_ = DebugCameraMode::FreeLook;
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Projection"))
            {
                const auto item = [this](const char *label, DebugProjectionMode mode)
                {
                    if (ImGui::MenuItem(label, nullptr, projectionMode_ == mode) && projectionMode_ != mode)
                    {
                        projectionMode_ = mode;
                        projectionModeChanged_ = true;
                    }
                };
                item("As Authored", DebugProjectionMode::AsAuthored);
                item("Perspective", DebugProjectionMode::Perspective);
                item("Orthographic", DebugProjectionMode::Orthographic);
                ImGui::EndMenu();
            }
            ImGui::BeginDisabled(projectionMode_ != DebugProjectionMode::Orthographic);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::DragFloat("Ortho Size", &debugOrthographicSize_, 0.1f, 0.5f, 500.0f, "%.1f m",
                                 ImGuiSliderFlags_AlwaysClamp))
            {
                SetDebugOrthographicSize(debugOrthographicSize_);
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (cameraMode_ == DebugCameraMode::FreeLook)
    {
        ImGui::TextDisabled(projectionMode_ == DebugProjectionMode::Orthographic
                                ? "Free look: hold LMB to look, WASD/QE to fly, wheel to zoom, shift+wheel for speed"
                                : "Free look: hold LMB to look, WASD/QE to fly, wheel for speed");
    }
    if (projectionMode_ == DebugProjectionMode::Orthographic)
    {
        ImGui::TextDisabled("Ortho %.1f m", debugOrthographicSize_);
    }
    else if (projectionMode_ == DebugProjectionMode::Perspective)
    {
        ImGui::TextDisabled("Perspective (override)");
    }
    ImGui::EndMainMenuBar();
}

bool ImGuiManager::ConsumeProjectionModeChanged()
{
    return std::exchange(projectionModeChanged_, false);
}

void ImGuiManager::SetDebugOrthographicSize(float size)
{
    debugOrthographicSize_ = std::isfinite(size) ? std::clamp(size, 0.5f, 500.0f) : 10.0f;
}

void ImGuiManager::Draw(Renderer &renderer, Scene *scene)
{
    if (!initialized_)
    {
        return;
    }

    DrawMainMenuBar();

    if (ImGui::Begin("Haki Debug"))
    {
        DrawRendererStats(renderer.GetStats());
        RendererSettings &settings = renderer.GetSettings();
        DrawGpuTimings(settings, renderer.GetGpuTimings());
        ImGui::SeparatorText("Physics");
        ImGui::Checkbox("Show Colliders", &showColliders_);
        if (scene && scene->GetPhysics().IsInitialized())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Bodies: %zu", scene->GetPhysics().GetBodyCount());
        }
        DrawScripting(scene);
        DrawAnimation(scene);
        ImGui::SeparatorText("Renderer");
        ImGui::Checkbox("Frustum Culling", &settings.FrustumCullingEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Show Bounds", &settings.ShowBounds);
        ImGui::SameLine();
        ImGui::BeginDisabled(defaultFont_ == nullptr);
        ImGui::Checkbox("Canvas Demo", &canvasDemoEnabled_);
        ImGui::SameLine();
        ImGui::Checkbox("Billboards", &billboardDemoEnabled_);
        ImGui::EndDisabled();
        ImGui::Checkbox("Depth Prepass", &settings.DepthPrepassEnabled);
        settings.RenderScale = std::clamp(std::isfinite(settings.RenderScale) ? settings.RenderScale : 1.0f, 0.5f, 1.0f);
        static constexpr int RenderHeights[] = {0, 480, 720, 1080, 1440};
        static constexpr const char *RenderHeightLabels[] = {"Native (window)", "480p", "720p", "1080p", "1440p"};
        int renderHeightIndex = 0;
        for (int index = 0; index < IM_ARRAYSIZE(RenderHeights); ++index)
        {
            if (settings.RenderHeight == RenderHeights[index])
            {
                renderHeightIndex = index;
            }
        }
        if (ImGui::Combo("Resolution", &renderHeightIndex, RenderHeightLabels, IM_ARRAYSIZE(RenderHeightLabels)))
        {
            settings.RenderHeight = RenderHeights[renderHeightIndex];
        }
        ImGui::SliderFloat("Render Scale", &settings.RenderScale, 0.5f, 1.0f, "%.2f");
        ImGui::TextDisabled("Internal: %u x %u", renderer.GetStats().RenderWidth, renderer.GetStats().RenderHeight);
        ImGui::Checkbox("Skybox", &settings.SkyboxEnabled);
        ImGui::Checkbox("Indirect Diffuse", &settings.IndirectDiffuseEnabled);
        ImGui::Checkbox("Indirect Specular", &settings.IndirectSpecularEnabled);
        const auto sanitizeColor = [](glm::vec3 &color, const glm::vec3 &fallback)
        {
            for (int channel = 0; channel < 3; ++channel)
            {
                color[channel] = std::isfinite(color[channel]) ? std::max(color[channel], 0.0f) : fallback[channel];
            }
        };
        sanitizeColor(settings.AmbientSkyColor, {0.60f, 0.70f, 0.90f});
        sanitizeColor(settings.AmbientGroundColor, {0.30f, 0.25f, 0.20f});
        settings.AmbientIntensity = std::clamp(
            std::isfinite(settings.AmbientIntensity) ? settings.AmbientIntensity : 0.3f, 0.0f, 10.0f);
        ImGui::BeginDisabled(settings.IndirectDiffuseEnabled && settings.IndirectSpecularEnabled);
        ImGui::ColorEdit3("Ambient Sky", &settings.AmbientSkyColor.x, ImGuiColorEditFlags_Float);
        ImGui::ColorEdit3("Ambient Ground", &settings.AmbientGroundColor.x, ImGuiColorEditFlags_Float);
        ImGui::DragFloat("Ambient Intensity", &settings.AmbientIntensity, 0.01f, 0.0f, 10.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::Checkbox("SSAO", &settings.SsaoEnabled);
        settings.SsaoRadius = std::clamp(std::isfinite(settings.SsaoRadius) ? settings.SsaoRadius : 0.5f,
                                         0.05f, 5.0f);
        settings.SsaoBias = std::clamp(std::isfinite(settings.SsaoBias) ? settings.SsaoBias : 0.025f,
                                       0.0f, 0.25f);
        settings.SsaoPower = std::clamp(std::isfinite(settings.SsaoPower) ? settings.SsaoPower : 1.5f,
                                        0.1f, 8.0f);
        ImGui::BeginDisabled(!settings.SsaoEnabled);
        ImGui::Checkbox("SSAO Blur", &settings.SsaoBlurEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("SSAO Half Res", &settings.SsaoHalfResolution);
        ImGui::SliderInt("SSAO Samples", &settings.SsaoSampleCount, 8, 32);
        ImGui::DragFloat("SSAO Radius", &settings.SsaoRadius, 0.01f, 0.05f, 5.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("SSAO Bias", &settings.SsaoBias, 0.001f, 0.0f, 0.25f, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("SSAO Power", &settings.SsaoPower, 0.05f, 0.1f, 8.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::Checkbox("Shadows", &settings.ShadowsEnabled);

        static constexpr const char *ShadowFilters[] = {"PCF", "PCSS"};
        int shadowFilter = static_cast<int>(settings.ShadowFilter);
        ImGui::BeginDisabled(!settings.ShadowsEnabled);
        if (ImGui::Combo("Shadow Filter", &shadowFilter, ShadowFilters, IM_ARRAYSIZE(ShadowFilters)))
        {
            settings.ShadowFilter = static_cast<ShadowFilterMode>(shadowFilter);
        }
        static constexpr int ShadowResolutions[] = {1024, 2048, 4096};
        static constexpr const char *ShadowResolutionLabels[] = {"1024", "2048", "4096"};
        int shadowResolutionIndex = 1;
        for (int index = 0; index < IM_ARRAYSIZE(ShadowResolutions); ++index)
        {
            if (settings.ShadowMapResolution == ShadowResolutions[index])
            {
                shadowResolutionIndex = index;
            }
        }
        if (ImGui::Combo("Shadow Resolution", &shadowResolutionIndex, ShadowResolutionLabels,
                         IM_ARRAYSIZE(ShadowResolutionLabels)))
        {
            settings.ShadowMapResolution = ShadowResolutions[shadowResolutionIndex];
        }
        ImGui::DragFloat("Shadow Distance", &settings.ShadowDistance, 1.0f, 1.0f, 5000.0f, "%.0f");
        ImGui::SliderFloat("PSSM Lambda", &settings.CascadeSplitLambda, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Show Cascade Zones", &settings.ShowCascadeZones);
        ImGui::EndDisabled();
        settings.BloomIntensity = std::clamp(
            std::isfinite(settings.BloomIntensity) ? settings.BloomIntensity : 0.1f, 0.0f, 10.0f);
        settings.BloomThreshold = std::clamp(
            std::isfinite(settings.BloomThreshold) ? settings.BloomThreshold : 1.0f, 0.0f, 100.0f);
        settings.BloomSoftKnee = std::clamp(
            std::isfinite(settings.BloomSoftKnee) ? settings.BloomSoftKnee : 0.5f, 0.0f, 1.0f);
        settings.BloomDiffusion = std::clamp(
            std::isfinite(settings.BloomDiffusion) ? settings.BloomDiffusion : 7.0f, 1.0f, 10.0f);
        ImGui::Checkbox("Bloom", &settings.BloomEnabled);
        ImGui::BeginDisabled(!settings.BloomEnabled);
        ImGui::DragFloat("Bloom Intensity", &settings.BloomIntensity, 0.01f, 0.0f, 10.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Bloom Threshold", &settings.BloomThreshold, 0.05f, 0.0f, 100.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Bloom Soft Knee", &settings.BloomSoftKnee, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Bloom Diffusion", &settings.BloomDiffusion, 1.0f, 10.0f, "%.1f");
        ImGui::EndDisabled();
        ImGui::DragFloat("Exposure", &settings.Exposure, 0.05f, 0.01f, 10.0f, "%.2f");
        settings.FxaaContrastThreshold = std::clamp(
            std::isfinite(settings.FxaaContrastThreshold) ? settings.FxaaContrastThreshold : 0.0312f, 0.0f, 1.0f);
        settings.FxaaRelativeThreshold = std::clamp(
            std::isfinite(settings.FxaaRelativeThreshold) ? settings.FxaaRelativeThreshold : 0.063f, 0.0f, 1.0f);
        settings.FxaaSubpixelBlending = std::clamp(
            std::isfinite(settings.FxaaSubpixelBlending) ? settings.FxaaSubpixelBlending : 0.75f, 0.0f, 1.0f);
        ImGui::Checkbox("FXAA", &settings.FxaaEnabled);
        ImGui::BeginDisabled(!settings.FxaaEnabled);
        ImGui::SliderFloat("FXAA Contrast Threshold", &settings.FxaaContrastThreshold, 0.0312f, 0.0833f, "%.4f");
        ImGui::SliderFloat("FXAA Relative Threshold", &settings.FxaaRelativeThreshold, 0.063f, 0.333f, "%.3f");
        ImGui::SliderFloat("FXAA Subpixel Blending", &settings.FxaaSubpixelBlending, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        DrawShaderReload();
        DrawLights(scene);
    }
    ImGui::End();

    if (canvasDemoEnabled_)
    {
        QueueCanvasDemo(renderer);
    }
    if (showColliders_ && scene)
    {
        scene->GetPhysics().DrawColliders(scene->GetRegistry(), renderer.GetDebugDraw());
    }
    if (billboardDemoEnabled_)
    {
        QueueBillboardDemo(renderer, scene);
    }
}

void ImGuiManager::QueueBillboardDemo(Renderer &renderer, Scene *scene) const
{
    if (!defaultFont_ || !scene)
    {
        return;
    }
    Canvas &canvas = renderer.GetCanvas();
    const Font &font = *defaultFont_;
    entt::registry &registry = scene->GetRegistry();
    const float time = static_cast<float>(ImGui::GetTime());

    // Light markers: always visible (no depth test) so lights can be found through walls.
    const auto lightMarker = [&](entt::entity entity, const char *type, const glm::vec4 &color)
    {
        const glm::vec3 position(registry.get<TransformComponent>(entity).WorldMatrix[3]);
        const CanvasAnchor anchor{position, false, 0.0f};
        canvas.FillRect(anchor, {-5.0f, -5.0f, 10.0f, 10.0f}, color, 2);
        canvas.StrokeRect(anchor, {-7.0f, -7.0f, 14.0f, 14.0f}, {0.0f, 0.0f, 0.0f, 0.8f}, 2.0f, 2);
        const std::string label = GetEntityName(registry, entity) + " (" + type + ")";
        canvas.DrawLabel(anchor, font, label, 16.0f, {1.0f, 1.0f, 1.0f, 0.95f}, 12.0f, 2);
    };
    for (const entt::entity entity : registry.view<TransformComponent, PointLightComponent>())
    {
        lightMarker(entity, "Point", {1.0f, 0.85f, 0.3f, 1.0f});
    }
    for (const entt::entity entity : registry.view<TransformComponent, SpotLightComponent>())
    {
        lightMarker(entity, "Spot", {0.4f, 0.8f, 1.0f, 1.0f});
    }

    // Name tags and health bars over meshes: depth tested and limited to 40 m like in-game nameplates.
    int tagged = 0;
    for (const entt::entity entity : registry.view<TransformComponent, MeshRendererComponent>())
    {
        if (++tagged > 64)
        {
            break;
        }
        const glm::vec3 position(registry.get<TransformComponent>(entity).WorldMatrix[3]);
        const CanvasAnchor anchor{position + glm::vec3(0.0f, 1.0f, 0.0f), true, 40.0f};
        const float phase = static_cast<float>(entt::to_integral(entity)) * 0.37f;
        const float health = 0.55f + 0.45f * std::sin(time * 0.9f + phase);
        canvas.FillRect(anchor, {-40.0f, 0.0f, 80.0f, 8.0f}, {0.05f, 0.05f, 0.06f, 0.75f});
        canvas.FillRect(anchor, {-40.0f, 0.0f, 80.0f * health, 8.0f}, {0.3f, 0.9f, 0.35f, 1.0f});
        canvas.StrokeRect(anchor, {-41.0f, -1.0f, 82.0f, 10.0f}, {0.0f, 0.0f, 0.0f, 0.9f}, 1.0f, 1);
        canvas.DrawLabel(anchor, font, GetEntityName(registry, entity), 15.0f, {1.0f, 1.0f, 1.0f, 0.9f}, 4.0f, 1);
    }
}

void ImGuiManager::QueueCanvasDemo(Renderer &renderer) const
{
    if (!defaultFont_)
    {
        return;
    }
    Canvas &canvas = renderer.GetCanvas();
    const Font &font = *defaultFont_;
    const glm::vec2 size = canvas.GetSize();
    const float margin = 24.0f;

    // FPS label, top-right so it does not sit under the debug window.
    char fps[64];
    std::snprintf(fps, sizeof(fps), "%.0f FPS", ImGui::GetIO().Framerate);
    const glm::vec2 fpsSize = font.MeasureText(fps, 28.0f);
    canvas.FillRect({size.x - fpsSize.x - margin - 12.0f, margin - 6.0f, fpsSize.x + 24.0f, fpsSize.y + 12.0f},
                    {0.0f, 0.0f, 0.0f, 0.55f});
    canvas.DrawText(font, fps, {size.x - fpsSize.x - margin, margin}, 28.0f, {1.0f, 1.0f, 1.0f, 1.0f}, 1);

    // Health bar anchored to the bottom-left; the layer argument puts the outline above the fill.
    const float health = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 0.8f);
    const CanvasRect bar{margin, size.y - margin - 28.0f, 320.0f, 28.0f};
    canvas.FillRect(bar, {0.08f, 0.08f, 0.1f, 0.8f});
    canvas.FillRect({bar.X, bar.Y, bar.Width * health, bar.Height}, {0.85f, 0.18f, 0.2f, 1.0f});
    canvas.StrokeRect(bar, {1.0f, 1.0f, 1.0f, 0.9f}, 2.0f, 1);
    canvas.DrawText(font, "HP", {bar.X, bar.Y - 30.0f}, 24.0f, {1.0f, 0.9f, 0.85f, 1.0f});

    // Centred prompt near the bottom.
    constexpr std::string_view prompt = "Press E to interact";
    const glm::vec2 promptSize = font.MeasureText(prompt, 32.0f);
    canvas.DrawText(font, prompt, {(size.x - promptSize.x) * 0.5f + 2.0f, size.y - 120.0f + 2.0f}, 32.0f,
                    {0.0f, 0.0f, 0.0f, 0.6f});
    canvas.DrawText(font, prompt, {(size.x - promptSize.x) * 0.5f, size.y - 120.0f}, 32.0f,
                    {1.0f, 0.95f, 0.7f, 1.0f}, 1);

    // The font atlas itself drawn as a sprite, bottom-right.
    const float atlasSize = 192.0f;
    canvas.FillRect({size.x - atlasSize - margin, size.y - atlasSize - margin, atlasSize, atlasSize},
                    {0.1f, 0.1f, 0.15f, 0.85f});
    canvas.DrawSprite(&font.GetAtlas(), {size.x - atlasSize - margin, size.y - atlasSize - margin, atlasSize,
                                         atlasSize},
                      {0.6f, 0.9f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, 1);
}

bool ImGuiManager::ConsumeShaderReloadRequest()
{
    return std::exchange(shaderReloadRequested_, false);
}

void ImGuiManager::SetShaderReloadResult(const ShaderReloadResult &result)
{
    lastReloaded_ = result.Reloaded;
    lastFailed_ = result.Failed;
    lastReloadError_ = result.Errors.empty() ? std::string{} : result.Errors.front();
}

bool ImGuiManager::ConsumeScriptReloadRequest()
{
    return std::exchange(scriptReloadRequested_, false);
}

void ImGuiManager::DrawScripting(Scene *scene)
{
    ImGui::SeparatorText("Scripting");
    ImGui::Checkbox("Auto Reload Scripts", &scriptHotReloadEnabled_);
    ImGui::SameLine();
    if (ImGui::Button("Reload Scripts"))
    {
        scriptReloadRequested_ = true;
    }
    if (!scene || !scene->GetScripts().IsInitialized())
    {
        ImGui::TextDisabled("No scripts in this scene");
        return;
    }
    ScriptSystem &scripts = scene->GetScripts();
    ImGui::TextDisabled("Scripts: %zu  Instances: %zu", scripts.GetLoadedScriptCount(), scripts.GetInstanceCount());
    const std::vector<std::string> &errors = scripts.GetErrors();
    if (errors.empty())
    {
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear Errors"))
    {
        scripts.ClearErrors();
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
    // Newest first; older errors are usually consequences of the first.
    const std::size_t shown = std::min<std::size_t>(errors.size(), 5);
    for (std::size_t i = 0; i < shown; ++i)
    {
        ImGui::TextWrapped("%s", errors[errors.size() - 1 - i].c_str());
    }
    ImGui::PopStyleColor();
}

void ImGuiManager::DrawAnimation(Scene *scene)
{
    if (!scene)
    {
        return;
    }
    auto animators = scene->GetRegistry().view<AnimatorComponent>();
    if (animators.begin() == animators.end())
    {
        return;
    }
    ImGui::SeparatorText("Animation");
    for (const entt::entity entity : animators)
    {
        const AnimatorComponent &animator = animators.get<AnimatorComponent>(entity);
        const auto *id = scene->GetRegistry().try_get<NameComponent>(entity);
        const std::string label = id && !id->Name.empty() ? id->Name : "entity " + std::to_string(entt::to_integral(entity));
        if (animator.Source == AnimatorSource::Locomotion)
        {
            ImGui::Text("%s: locomotion  %.2f m/s  blend %.2f", label.c_str(), animator.LocomotionSpeed,
                        animator.LocomotionPosition);
            if (const auto *locomotion = scene->GetRegistry().try_get<LocomotionAnimationComponent>(entity))
            {
                const auto samples = AnimationSystem::GetLocomotionSamples(*locomotion);
                std::string line;
                for (std::size_t index = 0; index < samples.size(); ++index)
                {
                    char sample[96];
                    std::snprintf(sample, sizeof(sample), "%s%s @ %.2f", index ? "  |  " : "",
                                  samples[index].Clip.c_str(), samples[index].Speed);
                    line += sample;
                }
                ImGui::TextDisabled("  %s", line.c_str());
            }
        }
        else
        {
            ImGui::Text("%s: %s  %.2fs%s", label.c_str(), animator.CurrentClipName.c_str(), animator.CurrentTime,
                        animator.OneShotFinished ? "  (finished)" : animator.Loop ? "  (loop)" : "");
        }
        if (animator.FadeDuration > 0.0f)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("fading %.0f%%", 100.0f * animator.FadeElapsed / animator.FadeDuration);
        }
    }
}

void ImGuiManager::DrawShaderReload()
{
    ImGui::SeparatorText("Shaders");
    ImGui::Checkbox("Hot Reload", &shaderHotReloadEnabled_);
    ImGui::SameLine();
    if (ImGui::Button("Reload All"))
    {
        shaderReloadRequested_ = true;
    }
    if (lastReloaded_ == 0 && lastFailed_ == 0)
    {
        return;
    }
    ImGui::Text("Last reload: %zu rebuilt, %zu failed", lastReloaded_, lastFailed_);
    if (!lastReloadError_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", lastReloadError_.c_str());
        ImGui::PopStyleColor();
    }
}

void ImGuiManager::Render()
{
    if (!initialized_)
    {
        return;
    }

    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiManager::WantsCaptureMouse() const
{
    return initialized_ && ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiManager::WantsCaptureKeyboard() const
{
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace Haki
