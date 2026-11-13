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

#if HAKI_ENABLE_IMGUI
    if (!imguiManager_.Initialize(window_.GetNativeWindow()))
    {
        Logger::Error() << "ImGui manager initialization failed";
        defaultFont_.Reset();
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

#if HAKI_ENABLE_IMGUI
        imguiManager_.BeginFrame();
        imguiManager_.Draw(renderer_, sceneManager_.GetActiveScene());
#endif

        bool freeLook = false;
        bool keyboardCaptured = false;
#if HAKI_ENABLE_IMGUI
        keyboardCaptured = imguiManager_.WantsCaptureKeyboard();
#if HAKI_ENABLE_DEBUG_CAMERA
        freeLook = imguiManager_.GetCameraMode() == DebugCameraMode::FreeLook;
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
                scene->SetGameplayInputEnabled(!freeLook && !keyboardCaptured);
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
        sceneManager_.Render(renderContext);
#if HAKI_ENABLE_IMGUI
        imguiManager_.Render();
#endif
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
    renderer_.Shutdown();
    Input::InputManager::shutdown();
    PhysicsCore::Shutdown();
    AssetManager::Shutdown();
    initialized_ = false;
}

} // namespace Haki
