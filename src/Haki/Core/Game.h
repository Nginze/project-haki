#pragma once

#include "Haki/Platform/Window.h"
#include "Haki/Renderer/Font.h"
#include "Haki/Renderer/Renderer.h"
#include "Haki/Scene/SceneManager.h"
#if HAKI_ENABLE_IMGUI
#include "Haki/Debug/ImGuiManager.h"
#endif
#if HAKI_ENABLE_DEBUG_CAMERA
#include "Haki/Systems/CameraSystem.h"
#endif

#include <string>
#include <filesystem>
#include <memory>

namespace Haki
{

class AssetCatalog;
class SceneSerializer;

class Game
{
public:
    // width/height set the initial window size only. The render resolution follows the window unless
    // RendererSettings::RenderHeight fixes it.
    Game(std::string title, unsigned int width, unsigned int height,
         std::filesystem::path sceneManifest = "Assets/Game/Project.json");
    ~Game();

    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;
    Game(Game &&) = delete;
    Game &operator=(Game &&) = delete;

    bool Run();

    SceneManager &GetSceneManager() { return sceneManager_; }
    // Renderer settings (e.g. GetRenderer().GetSettings().RenderHeight) can be set before Run().
    Renderer &GetRenderer() { return renderer_; }
    // Loaded from Assets/Engine/Fonts/Roboto-Medium.ttf during Run(); null if it could not be loaded.
    const Font *GetDefaultFont() const { return defaultFont_.IsValid() ? &defaultFont_ : nullptr; }
    const SceneManager &GetSceneManager() const { return sceneManager_; }

private:
    bool InitializeSystems();
    void ShutdownSystems();
    void PollShaderHotReload(double frameTime);
    void PollScriptHotReload(double frameTime);
    void ApplyProjectionOverride(SceneCameraOverride &view, DebugProjectionMode mode, bool modeChanged,
                                 bool freeLook);

    // The window is declared first so its OpenGL context is destroyed last.
    Window window_;
    Renderer renderer_;
    Font defaultFont_;
#if HAKI_ENABLE_IMGUI
    ImGuiManager imguiManager_;
#endif
    SceneManager sceneManager_;
#if HAKI_ENABLE_DEBUG_CAMERA
    CameraSystem debugCameraSystem_;
    bool freeLookActive_ = false;
#endif
    std::shared_ptr<AssetCatalog> assetCatalog_;
    std::shared_ptr<SceneSerializer> sceneSerializer_;
    std::filesystem::path sceneManifest_;
    double shaderReloadTimer_ = 0.0;
    double scriptReloadTimer_ = 0.0;
    bool initialized_ = false;
};

} // namespace Haki
