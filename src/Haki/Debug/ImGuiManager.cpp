#include "Haki/Debug/ImGuiManager.h"

#include "Haki/AssetManager/AssetManager.h"
#include "Haki/Debug/EditorTheme.h"
#include "Haki/Renderer/Font.h"
#include "Haki/Renderer/Renderer.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scene/Scene.h"

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
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace Haki
{
namespace
{

// Keeps a value finite and inside [minimum, maximum]; non-finite values become `fallback`.
void Sanitize(float &value, float fallback, float minimum, float maximum)
{
    value = std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}

void SanitizeColor(glm::vec3 &color, const glm::vec3 &fallback)
{
    for (int channel = 0; channel < 3; ++channel)
    {
        color[channel] = std::isfinite(color[channel]) ? std::max(color[channel], 0.0f) : fallback[channel];
    }
}

// Menu items drawn as widgets need a fixed width or they stretch the menu across the screen.
constexpr float MenuWidgetWidth = 170.0f;

void DrawRendererStats(const RendererStats &stats)
{
    if (ImGui::BeginTable("RendererStats", 2, ImGuiTableFlags_SizingStretchProp))
    {
        const auto row = [](const char *label, std::uint32_t value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::Text("%u", value);
        };
        row("Internal width", stats.RenderWidth);
        row("Internal height", stats.RenderHeight);
        row("Draw calls", stats.DrawCalls);
        row("Shadow draw calls", stats.ShadowDrawCalls);
        row("Shadow cascades", stats.ShadowCascadesRendered);
        row("Depth prepass draws", stats.DepthPrepassDrawCalls);
        row("SSAO draws", stats.SsaoDrawCalls);
        row("Bloom draws", stats.BloomDrawCalls);
        row("Post-process draws", stats.PostProcessDrawCalls);
        row("Transmission draws", stats.TransmissionDrawCalls);
        row("Submitted", stats.SubmittedItems);
        row("Skipped", stats.SkippedItems);
        row("Culled items", stats.CulledItems);
        row("Shadow culled draws", stats.ShadowCulledDraws);
        row("Debug lines", stats.DebugLines);
        row("Canvas quads", stats.CanvasQuads);
        row("Shader changes", stats.ShaderChanges);
        row("Material changes", stats.MaterialChanges);
        row("Mesh changes", stats.MeshChanges);
        row("Texture changes", stats.TextureChanges);
        ImGui::EndTable();
    }
}

void DrawGpuTimings(const RendererGpuTimings &timings)
{
    if (!timings.Valid)
    {
        ImGui::TextDisabled("Waiting for GPU results...");
        return;
    }
    if (ImGui::BeginTable("GpuTimings", 2, ImGuiTableFlags_SizingStretchProp))
    {
        const auto row = [](const char *label, float milliseconds) {
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
    ApplyEditorTheme(state_.PaneOpacity);
    appliedOpacity_ = state_.PaneOpacity;

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

void ImGuiManager::HandleShortcuts()
{
    // Polled from ImGui's key state so they work whichever window has focus. F11 always works; the
    // rest are ignored while typing into a text field, and while playtesting.
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
    {
        state_.Playtest = !state_.Playtest;
    }
    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return;
    }
    if (state_.Playtest)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
        {
            state_.PlaytestTelemetry = !state_.PlaytestTelemetry;
        }
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) state_.ShowHierarchy = !state_.ShowHierarchy;
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) state_.ShowInspector = !state_.ShowInspector;
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) state_.ShowTelemetry = !state_.ShowTelemetry;
    if (ImGui::IsKeyPressed(ImGuiKey_F6, false) || ImGui::IsKeyPressed(ImGuiKey_Pause, false))
    {
        state_.SimulationPaused = !state_.SimulationPaused;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
    {
        state_.SimulationPaused = true;
        state_.StepRequested = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) state_.ShowGrid = !state_.ShowGrid;
}

float ImGuiManager::DrawMainMenuBar(Renderer &renderer, Scene *scene)
{
    if (!ImGui::BeginMainMenuBar())
    {
        return 0.0f;
    }
    const float height = ImGui::GetWindowHeight();
    if (ImGui::BeginMenu("Project"))
    {
        if (ImGui::MenuItem("Save Project Settings")) state_.SaveProjectSettings = true;
        ImGui::MenuItem("Autosave Project Settings", nullptr, &state_.AutosaveProjectSettings);
        if (!state_.ProjectSaveStatus.empty()) ImGui::TextWrapped("%s",state_.ProjectSaveStatus.c_str());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        DrawViewMenu(renderer);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Lighting"))
    {
        DrawLightingMenu(renderer, scene);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Physics"))
    {
        DrawPhysicsMenu(scene);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window"))
    {
        DrawWindowMenu(renderer);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Engine"))
    {
        DrawEngineMenu(renderer, scene);
        ImGui::EndMenu();
    }

    // Status hints on the right of the menus.
    ImGui::Separator();
    if (state_.SimulationPaused)
    {
        ImGui::TextColored({1.0f, 0.75f, 0.3f, 1.0f}, "PAUSED  (F6 resume, F7 step)");
    }
    if (scene && scene->GetScripts().IsInitialized() && !scene->GetScripts().GetErrors().empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        char label[64];
        std::snprintf(label, sizeof(label), "%zu script error(s)", scene->GetScripts().GetErrors().size());
        if (ImGui::SmallButton(label))
        {
            showScriptErrors_ = true;
        }
        ImGui::PopStyleColor();
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
    return height;
}

void ImGuiManager::DrawViewMenu(Renderer &renderer)
{
    ImGui::MenuItem("Scene Hierarchy", "F1", &state_.ShowHierarchy);
    ImGui::MenuItem("Entity Inspector", "F2", &state_.ShowInspector);
    ImGui::MenuItem("Telemetry Overlay", "F3", &state_.ShowTelemetry);
    ImGui::SetNextItemWidth(MenuWidgetWidth);
    ImGui::SliderFloat("Pane Opacity", &state_.PaneOpacity, 0.2f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Separator();
    ImGui::MenuItem("World Grid", "Ctrl+G", &state_.ShowGrid);
    ImGui::MenuItem("Wireframe", nullptr, &renderer.GetSettings().Wireframe);
    ImGui::MenuItem("Selection Highlight", nullptr, &state_.ShowSelection);
    ImGui::Separator();
    if (ImGui::BeginMenu("Camera"))
    {
        if (ImGui::MenuItem("Tactical Gameplay Camera", nullptr, cameraMode_ == DebugCameraMode::Scene))
        {
            cameraMode_ = DebugCameraMode::Scene;
        }
#if HAKI_ENABLE_DEBUG_CAMERA
        constexpr bool freeLookAvailable = true;
#else
        constexpr bool freeLookAvailable = false;
#endif
        if (ImGui::MenuItem("Free-Look Debug Camera", nullptr, cameraMode_ == DebugCameraMode::FreeLook,
                            freeLookAvailable))
        {
            cameraMode_ = DebugCameraMode::FreeLook;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Projection"))
        {
            const auto item = [this](const char *label, DebugProjectionMode mode) {
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
}

void ImGuiManager::DrawLightingMenu(Renderer &renderer, Scene *scene)
{
    RendererSettings &settings = renderer.GetSettings();
    ImGui::PushItemWidth(MenuWidgetWidth);

    // The sun is the scene's first directional light.
    DirectionalLightComponent *sun = nullptr;
    if (scene)
    {
        auto lights = scene->GetRegistry().view<DirectionalLightComponent>();
        if (lights.begin() != lights.end())
        {
            sun = &lights.get<DirectionalLightComponent>(*lights.begin());
        }
    }
    ImGui::SeparatorText("Sun");
    if (sun)
    {
        Sanitize(sun->Intensity, 1.0f, 0.0f, 1000.0f);
        ImGui::DragFloat("Sun Intensity", &sun->Intensity, 0.05f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::ColorEdit3("Sun Color", &sun->Color.x, ImGuiColorEditFlags_Float);
    }
    else
    {
        ImGui::TextDisabled("No directional light in this scene");
    }

    ImGui::SeparatorText("Shadows");
    ImGui::Checkbox("Directional Shadows", &settings.ShadowsEnabled);
    ImGui::BeginDisabled(!settings.ShadowsEnabled);
    int shadowFilter = static_cast<int>(settings.ShadowFilter);
    if (ImGui::Combo("Filter", &shadowFilter, "PCF\0PCSS (soft)\0"))
    {
        settings.ShadowFilter = static_cast<ShadowFilterMode>(shadowFilter);
    }
    static constexpr int ShadowResolutions[] = {1024, 2048, 4096};
    int shadowResolution = 1;
    for (int index = 0; index < IM_ARRAYSIZE(ShadowResolutions); ++index)
    {
        if (settings.ShadowMapResolution == ShadowResolutions[index]) shadowResolution = index;
    }
    if (ImGui::Combo("Resolution", &shadowResolution, "1024\0002048\0004096\0"))
    {
        settings.ShadowMapResolution = ShadowResolutions[shadowResolution];
    }
    ImGui::DragFloat("Distance", &settings.ShadowDistance, 1.0f, 1.0f, 5000.0f, "%.0f m");
    ImGui::SliderFloat("Cascade Lambda", &settings.CascadeSplitLambda, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Show Cascade Zones", &settings.ShowCascadeZones);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Bloom");
    Sanitize(settings.BloomIntensity, 0.1f, 0.0f, 10.0f);
    Sanitize(settings.BloomThreshold, 1.0f, 0.0f, 100.0f);
    Sanitize(settings.BloomSoftKnee, 0.5f, 0.0f, 1.0f);
    Sanitize(settings.BloomDiffusion, 7.0f, 1.0f, 10.0f);
    ImGui::Checkbox("Bloom", &settings.BloomEnabled);
    ImGui::BeginDisabled(!settings.BloomEnabled);
    ImGui::DragFloat("Bloom Intensity", &settings.BloomIntensity, 0.01f, 0.0f, 10.0f, "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Bloom Threshold", &settings.BloomThreshold, 0.05f, 0.0f, 100.0f, "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Soft Knee", &settings.BloomSoftKnee, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Diffusion", &settings.BloomDiffusion, 1.0f, 10.0f, "%.1f");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Ambient & Exposure");
    SanitizeColor(settings.AmbientSkyColor, {0.60f, 0.70f, 0.90f});
    SanitizeColor(settings.AmbientGroundColor, {0.30f, 0.25f, 0.20f});
    Sanitize(settings.AmbientIntensity, 0.3f, 0.0f, 10.0f);
    Sanitize(settings.Exposure, 1.0f, 0.01f, 10.0f);
    ImGui::DragFloat("Exposure", &settings.Exposure, 0.05f, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    int tonemap = static_cast<int>(settings.Tonemap);
    if (ImGui::Combo("Tonemapper", &tonemap, "ACES (filmic)\0Neutral (authored colours)\0"))
    {
        settings.Tonemap = static_cast<TonemapMode>(tonemap);
    }
    ImGui::Checkbox("Indirect Diffuse (IBL)", &settings.IndirectDiffuseEnabled);
    ImGui::Checkbox("Indirect Specular (IBL)", &settings.IndirectSpecularEnabled);
    ImGui::BeginDisabled(settings.IndirectDiffuseEnabled && settings.IndirectSpecularEnabled);
    ImGui::ColorEdit3("Ambient Sky", &settings.AmbientSkyColor.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit3("Ambient Ground", &settings.AmbientGroundColor.x, ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Ambient Intensity", &settings.AmbientIntensity, 0.01f, 0.0f, 10.0f, "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::Checkbox("Skybox", &settings.SkyboxEnabled);
    if (scene && !AssetManager::GetTexture(scene->GetEnvironment().Texture))
    {
        // The sky and image-based lighting both come from the scene's environment map.
        ImGui::SameLine();
        ImGui::TextColored({1.0f, 0.7f, 0.3f, 1.0f}, "(scene has no environment map)");
        ImGui::SetItemTooltip("Add to the scene JSON:\n"
                              "\"environment\": { \"texture\": \"engine.texture.environment.qwantani-dusk\" }");
    }

    ImGui::Separator();
    if (ImGui::BeginMenu("SSAO"))
    {
        Sanitize(settings.SsaoRadius, 0.5f, 0.05f, 5.0f);
        Sanitize(settings.SsaoBias, 0.025f, 0.0f, 0.25f);
        Sanitize(settings.SsaoPower, 1.5f, 0.1f, 8.0f);
        ImGui::PushItemWidth(MenuWidgetWidth);
        ImGui::Checkbox("SSAO", &settings.SsaoEnabled);
        ImGui::BeginDisabled(!settings.SsaoEnabled);
        ImGui::Checkbox("Blur", &settings.SsaoBlurEnabled);
        ImGui::Checkbox("Half Resolution", &settings.SsaoHalfResolution);
        ImGui::SliderInt("Samples", &settings.SsaoSampleCount, 8, 32);
        ImGui::DragFloat("Radius", &settings.SsaoRadius, 0.01f, 0.05f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Bias", &settings.SsaoBias, 0.001f, 0.0f, 0.25f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Power", &settings.SsaoPower, 0.05f, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Anti-aliasing (FXAA)"))
    {
        Sanitize(settings.FxaaContrastThreshold, 0.0312f, 0.0f, 1.0f);
        Sanitize(settings.FxaaRelativeThreshold, 0.063f, 0.0f, 1.0f);
        Sanitize(settings.FxaaSubpixelBlending, 0.75f, 0.0f, 1.0f);
        ImGui::PushItemWidth(MenuWidgetWidth);
        ImGui::Checkbox("FXAA", &settings.FxaaEnabled);
        ImGui::BeginDisabled(!settings.FxaaEnabled);
        ImGui::SliderFloat("Contrast Threshold", &settings.FxaaContrastThreshold, 0.0312f, 0.0833f, "%.4f");
        ImGui::SliderFloat("Relative Threshold", &settings.FxaaRelativeThreshold, 0.063f, 0.333f, "%.3f");
        ImGui::SliderFloat("Subpixel Blending", &settings.FxaaSubpixelBlending, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::EndMenu();
    }
    ImGui::PopItemWidth();
}

void ImGuiManager::DrawPhysicsMenu(Scene *scene)
{
    ImGui::SeparatorText("Jolt Debug Draw");
    ImGui::MenuItem("Rigid Body Colliders", nullptr, &state_.ShowRigidBodies);
    ImGui::MenuItem("Trigger / Sensor Zones", nullptr, &state_.ShowSensors);
    ImGui::MenuItem("Character Sweeps", nullptr, &state_.ShowCharacters);
    ImGui::SetItemTooltip("CharacterVirtual capsules: white = requested velocity, cyan = actual velocity,\n"
                          "green = ground normal while grounded.");
    ImGui::SeparatorText("Simulation");
    ImGui::MenuItem("Pause", "F6", &state_.SimulationPaused);
    if (ImGui::MenuItem("Step One Tick", "F7"))
    {
        state_.SimulationPaused = true;
        state_.StepRequested = true;
    }
    if (scene && scene->GetPhysics().IsInitialized())
    {
        ImGui::Separator();
        ImGui::TextDisabled("Bodies: %zu  Characters: %zu", scene->GetPhysics().GetBodyCount(),
                            scene->GetPhysics().GetCharacterCount());
    }
}

void ImGuiManager::DrawWindowMenu(Renderer &renderer)
{
    ImGui::MenuItem("Fullscreen Window", nullptr, &state_.Fullscreen);
    ImGui::MenuItem("VSync", nullptr, &state_.Vsync);
    ImGui::MenuItem("Fullscreen Playtest", "F11", &state_.Playtest);
    ImGui::SetItemTooltip("Hides the editor and debug overlays and goes fullscreen. F11 again to return.");
    ImGui::Separator();
    if (ImGui::BeginMenu("Resolution"))
    {
        static constexpr int Presets[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
        for (const auto &preset : Presets)
        {
            char label[32];
            std::snprintf(label, sizeof(label), "%d x %d", preset[0], preset[1]);
            if (ImGui::MenuItem(label))
            {
                state_.RequestedWidth = preset[0];
                state_.RequestedHeight = preset[1];
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Fit to Monitor"))
        {
            state_.RequestMaximize = true;
        }
        ImGui::EndMenu();
    }

    RendererSettings &settings = renderer.GetSettings();
    Sanitize(settings.RenderScale, 1.0f, 0.25f, 2.0f);
    ImGui::SeparatorText("Render Scale");
    static constexpr float Scales[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    for (const float scale : Scales)
    {
        char label[32];
        std::snprintf(label, sizeof(label), "%.0f%%%s", scale * 100.0f, scale > 1.0f ? "  (supersampled)" : "");
        if (ImGui::MenuItem(label, nullptr, std::abs(settings.RenderScale - scale) < 0.001f))
        {
            settings.RenderScale = scale;
        }
    }
    ImGui::SetNextItemWidth(MenuWidgetWidth);
    ImGui::SliderFloat("Custom", &settings.RenderScale, 0.25f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    static constexpr int RenderHeights[] = {0, 480, 720, 1080, 1440};
    int heightIndex = 0;
    for (int index = 0; index < IM_ARRAYSIZE(RenderHeights); ++index)
    {
        if (settings.RenderHeight == RenderHeights[index]) heightIndex = index;
    }
    ImGui::SetNextItemWidth(MenuWidgetWidth);
    if (ImGui::Combo("Base Height", &heightIndex, "Window\000480p\000720p\0001080p\0001440p\0"))
    {
        settings.RenderHeight = RenderHeights[heightIndex];
    }
    ImGui::TextDisabled("Internal: %u x %u", renderer.GetStats().RenderWidth, renderer.GetStats().RenderHeight);
}

void ImGuiManager::DrawEngineMenu(Renderer &renderer, Scene *scene)
{
    RendererSettings &settings = renderer.GetSettings();
    ImGui::SeparatorText("Shaders");
    ImGui::MenuItem("Hot Reload Shaders", nullptr, &shaderHotReloadEnabled_);
    if (ImGui::MenuItem("Reload All Shaders"))
    {
        shaderReloadRequested_ = true;
    }
    if (lastReloaded_ != 0 || lastFailed_ != 0)
    {
        ImGui::TextDisabled("Last reload: %zu rebuilt, %zu failed", lastReloaded_, lastFailed_);
        if (!lastReloadError_.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f);
            ImGui::TextUnformatted(lastReloadError_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }

    ImGui::SeparatorText("Scripts");
    ImGui::MenuItem("Hot Reload Scripts", nullptr, &scriptHotReloadEnabled_);
    if (ImGui::MenuItem("Reload All Scripts"))
    {
        scriptReloadRequested_ = true;
    }
    ImGui::MenuItem("Script Errors...", nullptr, &showScriptErrors_);
    if (scene && scene->GetScripts().IsInitialized())
    {
        ImGui::TextDisabled("Scripts: %zu  Instances: %zu", scene->GetScripts().GetLoadedScriptCount(),
                            scene->GetScripts().GetInstanceCount());
    }

    ImGui::SeparatorText("Renderer");
    ImGui::MenuItem("Frustum Culling", nullptr, &settings.FrustumCullingEnabled);
    ImGui::MenuItem("Show Bounds", nullptr, &settings.ShowBounds);
    ImGui::MenuItem("Depth Prepass", nullptr, &settings.DepthPrepassEnabled);
    ImGui::MenuItem("GPU Timer Queries", nullptr, &settings.GpuTimingsEnabled);
    ImGui::MenuItem("Renderer Stats...", nullptr, &showRendererStats_);

    ImGui::SeparatorText("Canvas Demos");
    ImGui::BeginDisabled(defaultFont_ == nullptr);
    ImGui::MenuItem("Canvas HUD Demo", nullptr, &canvasDemoEnabled_);
    ImGui::MenuItem("Billboards Demo", nullptr, &billboardDemoEnabled_);
    ImGui::EndDisabled();
}

void ImGuiManager::DrawScriptErrorsWindow(Scene *scene)
{
    if (!showScriptErrors_)
    {
        return;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Size.x * 0.5f, viewport->Size.y * 0.5f}, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({560.0f, 260.0f}, ImGuiCond_Appearing);
    if (ImGui::Begin("Script Errors", &showScriptErrors_, ImGuiWindowFlags_NoSavedSettings))
    {
        if (!scene || !scene->GetScripts().IsInitialized())
        {
            ImGui::TextDisabled("No scripts in this scene");
        }
        else
        {
            ScriptSystem &scripts = scene->GetScripts();
            const std::vector<std::string> &errors = scripts.GetErrors();
            if (ImGui::Button("Clear"))
            {
                scripts.ClearErrors();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu error(s), newest first", errors.size());
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.5f, 1.0f));
            for (std::size_t index = errors.size(); index-- > 0;)
            {
                ImGui::TextWrapped("%s", errors[index].c_str());
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
}

void ImGuiManager::DrawRendererStatsWindow(Renderer &renderer)
{
    if (!showRendererStats_)
    {
        return;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Size.x * 0.5f, viewport->Size.y * 0.5f}, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({320.0f, 520.0f}, ImGuiCond_Appearing);
    if (ImGui::Begin("Renderer Stats", &showRendererStats_, ImGuiWindowFlags_NoSavedSettings))
    {
        DrawRendererStats(renderer.GetStats());
        ImGui::SeparatorText("GPU Timings");
        DrawGpuTimings(renderer.GetGpuTimings());
    }
    ImGui::End();
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
    if (state_.PaneOpacity != appliedOpacity_)
    {
        ApplyEditorTheme(state_.PaneOpacity);
        appliedOpacity_ = state_.PaneOpacity;
    }
    HandleShortcuts();
    // Entity handles are per registry: a selection from the previous scene could name an unrelated entity.
    if (scene != selectionScene_)
    {
        selectionScene_ = scene;
        state_.Selected = entt::null;
    }
    if (scene && state_.Selected != entt::null && !scene->GetRegistry().valid(state_.Selected))
    {
        state_.Selected = entt::null;
    }

    state_.MenuBarHeight = 0.0f;
    state_.HierarchyWidth = 0.0f;
    state_.InspectorWidth = 0.0f;
    if (state_.EditorVisible())
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        state_.MenuBarHeight = DrawMainMenuBar(renderer, scene);
        state_.HierarchyWidth =
            panels_.DrawHierarchy(scene, state_, state_.MenuBarHeight, viewport->Size.x, viewport->Size.y);
        state_.InspectorWidth =
            panels_.DrawInspector(scene, state_, state_.MenuBarHeight, viewport->Size.x, viewport->Size.y);
        DrawScriptErrorsWindow(scene);
        DrawRendererStatsWindow(renderer);
        QueueWorldOverlays(renderer, scene);
        if (canvasDemoEnabled_)
        {
            QueueCanvasDemo(renderer);
        }
        if (billboardDemoEnabled_)
        {
            QueueBillboardDemo(renderer, scene);
        }
    }
}

void ImGuiManager::QueueWorldOverlays(Renderer &renderer, Scene *scene)
{
    DebugDraw &draw = renderer.GetDebugDraw();
    if (state_.ShowGrid)
    {
        draw.Grid({0.0f, 0.0f, 0.0f}, 20.0f, 40, {0.8f, 0.85f, 0.9f, 0.35f});
        draw.Axes(glm::mat4(1.0f), 1.0f, false);
    }
    if (!scene)
    {
        return;
    }
    if (state_.PhysicsOverlayVisible())
    {
        scene->GetPhysics().DrawColliders(scene->GetRegistry(), draw,
                                          {state_.ShowRigidBodies, state_.ShowSensors, state_.ShowCharacters});
    }
    if (state_.ShowSelection && state_.Selected != entt::null)
    {
        EditorPanels::DrawSelection(*scene, state_.Selected, draw);
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
    const auto lightMarker = [&](entt::entity entity, const char *type, const glm::vec4 &color) {
        const glm::vec3 position(registry.get<TransformComponent>(entity).WorldMatrix[3]);
        const CanvasAnchor anchor{position, false, 0.0f};
        canvas.FillRect(anchor, {-5.0f, -5.0f, 10.0f, 10.0f}, color, 2);
        canvas.StrokeRect(anchor, {-7.0f, -7.0f, 14.0f, 14.0f}, {0.0f, 0.0f, 0.0f, 0.8f}, 2.0f, 2);
        const std::string label = GetEntityLabel(registry, entity) + " (" + type + ")";
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
        canvas.DrawLabel(anchor, font, GetEntityLabel(registry, entity), 15.0f, {1.0f, 1.0f, 1.0f, 0.9f}, 4.0f, 1);
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
    // Keep clear of the inspector pane on the right.
    const float right = size.x - state_.InspectorWidth;

    // Health bar anchored to the bottom-left; the layer argument puts the outline above the fill.
    const float health = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 0.8f);
    const CanvasRect bar{margin + state_.HierarchyWidth, size.y - margin - 28.0f, 320.0f, 28.0f};
    canvas.FillRect(bar, {0.08f, 0.08f, 0.1f, 0.8f});
    canvas.FillRect({bar.X, bar.Y, bar.Width * health, bar.Height}, {0.85f, 0.18f, 0.2f, 1.0f});
    canvas.StrokeRect(bar, {1.0f, 1.0f, 1.0f, 0.9f}, 2.0f, 1);
    canvas.DrawText(font, "HP", {bar.X, bar.Y - 30.0f}, 24.0f, {1.0f, 0.9f, 0.85f, 1.0f});

    // Centred prompt near the bottom.
    constexpr std::string_view prompt = "Press E to interact";
    const glm::vec2 promptSize = font.MeasureText(prompt, 32.0f);
    const float centre = (state_.HierarchyWidth + right) * 0.5f;
    canvas.DrawText(font, prompt, {centre - promptSize.x * 0.5f + 2.0f, size.y - 120.0f + 2.0f}, 32.0f,
                    {0.0f, 0.0f, 0.0f, 0.6f});
    canvas.DrawText(font, prompt, {centre - promptSize.x * 0.5f, size.y - 120.0f}, 32.0f, {1.0f, 0.95f, 0.7f, 1.0f},
                    1);

    // The font atlas itself drawn as a sprite, bottom-right.
    const float atlasSize = 192.0f;
    canvas.FillRect({right - atlasSize - margin, size.y - atlasSize - margin, atlasSize, atlasSize},
                    {0.1f, 0.1f, 0.15f, 0.85f});
    canvas.DrawSprite(&font.GetAtlas(), {right - atlasSize - margin, size.y - atlasSize - margin, atlasSize, atlasSize},
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

float ImGuiManager::GetFramebufferScale() const
{
    return initialized_ ? std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f) : 1.0f;
}

bool ImGuiManager::WantsCaptureKeyboard() const
{
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace Haki
