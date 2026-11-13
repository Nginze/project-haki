#include "Haki/Core/Game.h"

#include "Haki/AssetLoader/ShaderFileLoader.h"
#include "Haki/AssetLoader/TextureFileLoader.h"
#include "Haki/AssetLoader/GltfMeshLoader.h"
#include "Haki/AssetLoader/GltfSkeletonLoader.h"
#include "Haki/AssetLoader/GltfAnimationClipLoader.h"
#include "Haki/AssetLoader/MaterialFileLoader.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/AssetManager/AssetCatalog.h"
#include "Haki/Input/InputManager.h"
#include "Haki/Logging/Logger.h"
#include "Haki/Physics/PhysicsCore.h"
#include "Haki/Serialization/SceneSerializer.h"
#include "Haki/Systems/PlayerControllerSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Haki
{
namespace
{

constexpr double FixedDeltaTime = 1.0 / 60.0;
constexpr double MaximumFrameTime = 0.25;
constexpr double MaximumDebugCameraFrameTime = 1.0 / 15.0;
constexpr int MaximumUpdatesPerFrame = 8;
constexpr double ShaderReloadPollInterval = 0.5;
constexpr double ScriptReloadPollInterval = 0.5;

} // namespace

Game::Game(std::string title, unsigned int width, unsigned int height, std::filesystem::path sceneManifest)
    : window_(static_cast<int>(width), static_cast<int>(height), std::move(title)),
      assetCatalog_(std::make_shared<AssetCatalog>()),
      sceneSerializer_(std::make_shared<SceneSerializer>(assetCatalog_)), sceneManifest_(std::move(sceneManifest))
{
}

Game::~Game()
{
    ShutdownSystems();
}

bool Game::InitializeSystems()
{
    if (initialized_)
    {
        return true;
    }
    if (!window_.GetNativeWindow())
    {
        Logger::Error() << "Cannot initialize game systems without a valid window";
        return false;
    }
    if (!AssetManager::Initialize())
    {
        Logger::Error() << "Asset manager initialization failed";
        return false;
    }

    const auto gltfCache = std::make_shared<GltfDocumentCache>();
    if (!AssetManager::RegisterShaderLoader(std::make_unique<ShaderFileLoader>()) ||
        !AssetManager::RegisterTextureLoader(std::make_unique<TextureFileLoader>(assetCatalog_)) ||
        !AssetManager::RegisterMeshLoader(std::make_unique<GltfMeshLoader>(assetCatalog_, gltfCache)) ||
        !AssetManager::RegisterSkeletonLoader(std::make_unique<GltfSkeletonLoader>(assetCatalog_, gltfCache)) ||
        !AssetManager::RegisterAnimationClipLoader(
            std::make_unique<GltfAnimationClipLoader>(assetCatalog_, gltfCache)) ||
        !AssetManager::RegisterMaterialLoader(std::make_unique<MaterialFileLoader>(assetCatalog_)))
    {
        Logger::Error() << "Asset loader registration failed";
        AssetManager::Shutdown();
        return false;
    }

    if (!PhysicsCore::Initialize())
    {
        Logger::Error() << "Physics initialization failed";
        AssetManager::Shutdown();
        return false;
    }

    if (!Input::InputManager::initialize(window_.GetNativeWindow()))
    {
        Logger::Error() << "Input manager initialization failed";
        PhysicsCore::Shutdown();
        AssetManager::Shutdown();
        return false;
    }

    RendererConfig rendererConfig;
    rendererConfig.ClearColor = {0.05f, 0.05f, 0.07f, 1.0f};
    std::string diagnostic;
    if (!renderer_.Initialize(rendererConfig, &diagnostic))
    {
        Logger::Error() << "Renderer initialization failed: " << diagnostic;
        Input::InputManager::shutdown();
        PhysicsCore::Shutdown();
        AssetManager::Shutdown();
        return false;
    }

    std::string fontError;
    if (!defaultFont_.LoadFromFile("Assets/Engine/Fonts/Roboto-Medium.ttf", 48.0f, &fontError))
    {
        Logger::Warning() << "Default font unavailable, canvas text is disabled: " << fontError;
    }
    if (!telemetryFont_.LoadFromFile("Assets/Engine/Fonts/JetBrainsMono-Regular.ttf", 32.0f, &fontError))
    {
        Logger::Warning() << "Telemetry font unavailable, using the default font: " << fontError;
    }
    telemetry_.Initialize();

#if HAKI_ENABLE_IMGUI
    if (!imguiManager_.Initialize(window_.GetNativeWindow()))
    {
        Logger::Error() << "ImGui manager initialization failed";
        defaultFont_.Reset();
        telemetryFont_.Reset();
        renderer_.Shutdown();
        Input::InputManager::shutdown();
        PhysicsCore::Shutdown();
        AssetManager::Shutdown();
        return false;
    }
#endif

    if (!sceneManager_.Initialize(sceneManifest_, sceneSerializer_))
    {
        for (const SerializationError &error : sceneManager_.GetLastSerializationErrors())
        {
            Logger::Error() << error.Source.string() << error.JsonPointer << ": " << error.Message;
        }
        Logger::Error() << "Scene manager initialization failed";
        sceneManager_.Shutdown();
#if HAKI_ENABLE_IMGUI
        imguiManager_.Shutdown();
#endif
        defaultFont_.Reset();
        telemetryFont_.Reset();
        renderer_.Shutdown();
        Input::InputManager::shutdown();
        PhysicsCore::Shutdown();
        AssetManager::Shutdown();
        return false;
    }

#if HAKI_ENABLE_IMGUI
    imguiManager_.SetDefaultFont(GetDefaultFont());
#endif
    initialized_ = true;
    return true;
}

bool Game::Run()
{
    if (!InitializeSystems())
    {
        ShutdownSystems();
        return false;
    }

    using Clock = std::chrono::steady_clock;
    auto previousTime = Clock::now();
    double accumulator = 0.0;

    while (!window_.ShouldClose())
    {
        const auto currentTime = Clock::now();
        const double elapsed = std::chrono::duration<double>(currentTime - previousTime).count();
        previousTime = currentTime;
        const double frameTime = std::min(elapsed, MaximumFrameTime);
        accumulator += frameTime;

        window_.PollEvents();
        Input::InputManager::update();

        if (Scene *scene = sceneManager_.GetActiveScene())
        {
            scene->ResetCpuTimings();
        }
        float editorMs = 0.0f;
        const auto msSince = [](Clock::time_point start) {
            return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
        };

        bool freeLook = false;
        bool keyboardCaptured = false;
        bool mouseCaptured = false;
#if HAKI_ENABLE_IMGUI
        auto editorStart = Clock::now();
        imguiManager_.BeginFrame();
        imguiManager_.Draw(renderer_, sceneManager_.GetActiveScene());
        editorMs += msSince(editorStart);
        ApplyEditorRequests();

        keyboardCaptured = imguiManager_.WantsCaptureKeyboard();
        mouseCaptured = imguiManager_.WantsCaptureMouse();
#if HAKI_ENABLE_DEBUG_CAMERA
        // Playtest always plays through the gameplay camera.
        freeLook = imguiManager_.GetCameraMode() == DebugCameraMode::FreeLook && !imguiManager_.GetEditorState().Playtest;
#endif
#endif
#if HAKI_ENABLE_DEBUG_CAMERA
        if (freeLook != freeLookActive_)
        {
            if (freeLook)
            {
                Scene *scene = sceneManager_.GetActiveScene();
                debugCameraSystem_.Activate(scene ? &scene->GetRegistry() : nullptr);
            }
            else
            {
                debugCameraSystem_.Reset();
            }
            freeLookActive_ = freeLook;
        }
#endif

        // WASD drives either the free-look camera or the player, never both.
        const PlayerMoveInput playerInput =
            !freeLook && !keyboardCaptured ? PlayerControllerSystem::ReadInput() : PlayerMoveInput{};

        int updateCount = 0;
        while (accumulator >= FixedDeltaTime && updateCount < MaximumUpdatesPerFrame)
        {
            if (Scene *scene = sceneManager_.GetActiveScene())
            {
                scene->GetScripts().SetViewportSize(window_.GetWidth(), window_.GetHeight());
                PlayerControllerSystem::Update(scene->GetRegistry(), playerInput);
                scene->SetGameplayInputEnabled(!freeLook && !keyboardCaptured, !mouseCaptured);
            }
            sceneManager_.Update(static_cast<float>(FixedDeltaTime));
            accumulator -= FixedDeltaTime;
            ++updateCount;
        }

        if (updateCount == MaximumUpdatesPerFrame && accumulator >= FixedDeltaTime)
        {
            accumulator = std::fmod(accumulator, FixedDeltaTime);
        }

        SceneRenderContext renderContext{&renderer_, window_.GetWidth(), window_.GetHeight()};
        // The view is only overridden for free look or a View > Camera > Projection override; otherwise the
        // scene renders its own primary camera exactly as authored.
        std::optional<SceneCameraOverride> viewOverride;
#if HAKI_ENABLE_IMGUI
        const DebugProjectionMode projectionMode = imguiManager_.GetProjectionMode();
        const bool projectionModeChanged = imguiManager_.ConsumeProjectionModeChanged();
#else
        const DebugProjectionMode projectionMode = DebugProjectionMode::AsAuthored;
        const bool projectionModeChanged = false;
#endif
#if HAKI_ENABLE_DEBUG_CAMERA
        if (freeLook)
        {
#if HAKI_ENABLE_IMGUI
            const bool orthographicZoom = projectionMode == DebugProjectionMode::Orthographic;
            debugCameraSystem_.SetOrthographicZoom(
                orthographicZoom ||
                    (projectionMode == DebugProjectionMode::AsAuthored &&
                     debugCameraSystem_.GetRenderCamera().Projection == CameraProjection::Orthographic),
                orthographicZoom && !projectionModeChanged ? imguiManager_.GetDebugOrthographicSize() : -1.0f);
            if (imguiManager_.WantsCaptureMouse() || keyboardCaptured)
            {
                Input::InputManager::setCursorCaptured(false);
            }
            else
#endif
            {
                debugCameraSystem_.Update(static_cast<float>(std::min(frameTime, MaximumDebugCameraFrameTime)));
            }
            viewOverride = debugCameraSystem_.GetRenderCamera();
        }
#endif
        if (projectionMode != DebugProjectionMode::AsAuthored)
        {
            if (!viewOverride)
            {
                if (const Scene *scene = sceneManager_.GetActiveScene())
                {
                    viewOverride = scene->GetPrimaryCameraView();
                }
            }
            if (viewOverride)
            {
                ApplyProjectionOverride(*viewOverride, projectionMode, projectionModeChanged, freeLook);
            }
        }
        if (viewOverride)
        {
            renderContext.CameraOverride = &*viewOverride;
        }
        PollShaderHotReload(elapsed);
        PollScriptHotReload(elapsed);
        QueueTelemetry(static_cast<float>(elapsed));
        const auto renderStart = Clock::now();
        sceneManager_.Render(renderContext);
        lastRenderMs_ = msSince(renderStart);
#if HAKI_ENABLE_IMGUI
        editorStart = Clock::now();
        imguiManager_.Render();
        editorMs += msSince(editorStart);
#endif
        lastEditorMs_ = editorMs;
        window_.SwapBuffers();
    }

    ShutdownSystems();
    return true;
}

void Game::PollShaderHotReload(double frameTime)
{
#if HAKI_ENABLE_IMGUI
    const bool enabled = imguiManager_.IsShaderHotReloadEnabled();
    const bool forced = imguiManager_.ConsumeShaderReloadRequest();
#else
    const bool enabled = true;
    const bool forced = false;
#endif
    shaderReloadTimer_ += frameTime;
    if (!forced && (!enabled || shaderReloadTimer_ < ShaderReloadPollInterval))
    {
        return;
    }
    shaderReloadTimer_ = 0.0;

    const ShaderReloadResult result = AssetManager::ReloadChangedShaders(forced);
    if (result.Reloaded == 0 && result.Failed == 0)
    {
        return;
    }
    if (result.Reloaded > 0)
    {
        Logger::Info() << "Reloaded " << result.Reloaded << " shader(s)";
    }
    for (const std::string &error : result.Errors)
    {
        Logger::Error() << error;
    }
#if HAKI_ENABLE_IMGUI
    imguiManager_.SetShaderReloadResult(result);
#endif
}

void Game::ApplyProjectionOverride(SceneCameraOverride &view, DebugProjectionMode mode, bool modeChanged,
                                   bool freeLook)
{
    if (mode == DebugProjectionMode::Perspective)
    {
        view.Projection = CameraProjection::Perspective;
        return;
    }
#if HAKI_ENABLE_IMGUI
    if (modeChanged)
    {
        // Keep the framing when switching: the scene stays about the same size on screen.
        imguiManager_.SetDebugOrthographicSize(OrthographicSizeMatchingView(view));
    }
#if HAKI_ENABLE_DEBUG_CAMERA
    else if (freeLook)
    {
        imguiManager_.SetDebugOrthographicSize(debugCameraSystem_.GetOrthographicSize()); // wheel zoom
    }
#endif
    view.Projection = CameraProjection::Orthographic;
    view.OrthographicSize = imguiManager_.GetDebugOrthographicSize();
#if HAKI_ENABLE_DEBUG_CAMERA
    if (freeLook)
    {
        debugCameraSystem_.SetOrthographicZoom(true, view.OrthographicSize);
    }
#endif
#else
    (void)modeChanged;
    (void)freeLook;
    view.Projection = CameraProjection::Orthographic;
    view.OrthographicSize = OrthographicSizeMatchingView(view);
#endif
}

void Game::PollScriptHotReload(double frameTime)
{
#if HAKI_ENABLE_IMGUI
    const bool enabled = imguiManager_.IsScriptHotReloadEnabled();
    const bool forced = imguiManager_.ConsumeScriptReloadRequest();
#else
    const bool enabled = true;
    const bool forced = false;
#endif
    scriptReloadTimer_ += frameTime;
    if (!forced && (!enabled || scriptReloadTimer_ < ScriptReloadPollInterval))
    {
        return;
    }
    scriptReloadTimer_ = 0.0;
    if (Scene *scene = sceneManager_.GetActiveScene())
    {
        scene->GetScripts().ReloadChanged(forced);
    }
}

void Game::ApplyEditorRequests()
{
#if HAKI_ENABLE_IMGUI
    EditorState &state = imguiManager_.GetEditorState();
    // Playtest goes fullscreen; leaving it restores the windowed size.
    if (state.Playtest != window_.IsFullscreen())
    {
        window_.SetFullscreen(state.Playtest);
        if (state.Playtest && !window_.IsFullscreen())
        {
            // Fullscreen refused (no monitor): playtest still hides the editor, just inside the window.
            static bool warned = false;
            if (!warned) Logger::Warning() << "Playtest continues in a window";
            warned = true;
        }
    }
    if (state.RequestedWidth > 0 && state.RequestedHeight > 0)
    {
        window_.SetSize(state.RequestedWidth, state.RequestedHeight);
        state.Playtest = false;
    }
    if (state.RequestMaximize)
    {
        window_.Maximize();
        state.Playtest = false;
    }
    state.RequestedWidth = state.RequestedHeight = 0;
    state.RequestMaximize = false;

    if (Scene *scene = sceneManager_.GetActiveScene())
    {
        scene->SetSimulationPaused(state.SimulationPaused);
        if (state.StepRequested)
        {
            scene->RequestStep();
        }
    }
    state.StepRequested = false;
#endif
}

void Game::QueueTelemetry(float frameSeconds)
{
    const Scene *scene = sceneManager_.GetActiveScene();
    const SceneCpuTimings *sceneCpu = scene ? &scene->GetCpuTimings() : nullptr;
    bool paused = false;
    bool visible = true;
    glm::vec2 origin = ComputeTelemetryOrigin(false, 0.0f, false, 0.0f);
    float scale = 1.0f;
#if HAKI_ENABLE_IMGUI
    const EditorState &state = imguiManager_.GetEditorState();
    paused = state.SimulationPaused;
    visible = state.TelemetryVisible();
    // Panes are laid out in window units; the canvas works in framebuffer pixels.
    scale = imguiManager_.GetFramebufferScale();
    origin = ComputeTelemetryOrigin(state.MenuBarHeight > 0.0f, state.MenuBarHeight, state.HierarchyWidth > 0.0f,
                                    state.HierarchyWidth) * scale;
#endif
    telemetry_.Update(frameSeconds, sceneCpu, lastRenderMs_, lastEditorMs_, renderer_.GetGpuTimings(),
                      renderer_.GetSettings(), paused);
    if (!visible)
    {
        return;
    }
    const Font *font = telemetryFont_.IsValid() ? &telemetryFont_ : GetDefaultFont();
    if (font)
    {
        telemetry_.Draw(renderer_.GetCanvas(), *font, origin, 14.0f * scale);
    }
}

void Game::ShutdownSystems()
{
    if (!initialized_)
    {
        return;
    }

#if HAKI_ENABLE_DEBUG_CAMERA
    debugCameraSystem_.Reset();
#endif
    sceneManager_.Shutdown();
#if HAKI_ENABLE_IMGUI
    imguiManager_.SetDefaultFont(nullptr);
    imguiManager_.Shutdown();
#endif
    defaultFont_.Reset();
    telemetryFont_.Reset();
    renderer_.Shutdown();
    Input::InputManager::shutdown();
    PhysicsCore::Shutdown();
    AssetManager::Shutdown();
    initialized_ = false;
}

} // namespace Haki
