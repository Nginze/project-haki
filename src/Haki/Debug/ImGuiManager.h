#pragma once

#include "Haki/Debug/EditorPanels.h"
#include "Haki/Debug/EditorState.h"
#include "Haki/Scene/SceneCamera.h"

#include <cstddef>
#include <string>

struct GLFWwindow;

namespace Haki
{

class Font;
class Renderer;
class Scene;
struct ShaderReloadResult;

enum class DebugCameraMode
{
    Scene,   // render through the scene's own (gameplay) camera
    FreeLook // render through the debug free-look camera
};

class ImGuiManager
{
public:
    ImGuiManager() = default;
    ~ImGuiManager();

    ImGuiManager(const ImGuiManager &) = delete;
    ImGuiManager &operator=(const ImGuiManager &) = delete;
    ImGuiManager(ImGuiManager &&) = delete;
    ImGuiManager &operator=(ImGuiManager &&) = delete;

    bool Initialize(GLFWwindow *window);
    void Shutdown();

    void BeginFrame();
    void Draw(Renderer &renderer, Scene *scene);
    void Render();

    bool WantsCaptureMouse() const;
    bool WantsCaptureKeyboard() const;
    // Framebuffer pixels per ImGui (window) unit; >1 on high-DPI displays.
    float GetFramebufferScale() const;
    bool IsInitialized() const { return initialized_; }

    // Chosen in the View > Camera menu. Scene by default.
    DebugCameraMode GetCameraMode() const { return cameraMode_; }
    DebugProjectionMode GetProjectionMode() const { return projectionMode_; }
    // True once after the projection mode was changed in the menu.
    bool ConsumeProjectionModeChanged();
    float GetDebugOrthographicSize() const { return debugOrthographicSize_; }
    void SetDebugOrthographicSize(float size);

    bool IsShaderHotReloadEnabled() const { return shaderHotReloadEnabled_; }
    bool ConsumeShaderReloadRequest();
    void SetShaderReloadResult(const ShaderReloadResult &result);
    bool IsScriptHotReloadEnabled() const { return scriptHotReloadEnabled_; }
    bool ConsumeScriptReloadRequest();
    // Font used by the canvas demo overlay; may be null.
    void SetDefaultFont(const Font *font) { defaultFont_ = font; }

    // Pane visibility, selection, playtest, pause and window requests from the editor UI.
    EditorState &GetEditorState() { return state_; }
    const EditorState &GetEditorState() const { return state_; }

private:
    void HandleShortcuts();
    // Returns the menu bar height.
    float DrawMainMenuBar(Renderer &renderer, Scene *scene);
    void DrawViewMenu(Renderer &renderer);
    void DrawLightingMenu(Renderer &renderer, Scene *scene);
    void DrawPhysicsMenu(Scene *scene);
    void DrawWindowMenu(Renderer &renderer);
    void DrawEngineMenu(Renderer &renderer, Scene *scene);
    void DrawScriptErrorsWindow(Scene *scene);
    void DrawRendererStatsWindow(Renderer &renderer);
    void QueueWorldOverlays(Renderer &renderer, Scene *scene);
    void QueueCanvasDemo(Renderer &renderer) const;
    void QueueBillboardDemo(Renderer &renderer, Scene *scene) const;

    EditorState state_;
    EditorPanels panels_;
    float appliedOpacity_ = -1.0f;
    const Scene *selectionScene_ = nullptr; // the selection is cleared when the active scene changes
    bool showScriptErrors_ = false;
    bool showRendererStats_ = false;

    bool initialized_ = false;
    bool shaderHotReloadEnabled_ = true;
    bool shaderReloadRequested_ = false;
    bool scriptHotReloadEnabled_ = true;
    bool scriptReloadRequested_ = false;
    std::size_t lastReloaded_ = 0;
    std::size_t lastFailed_ = 0;
    std::string lastReloadError_;
    const Font *defaultFont_ = nullptr;
    bool canvasDemoEnabled_ = false;
    bool billboardDemoEnabled_ = false;
    DebugCameraMode cameraMode_ = DebugCameraMode::Scene;
    DebugProjectionMode projectionMode_ = DebugProjectionMode::AsAuthored;
    bool projectionModeChanged_ = false;
    float debugOrthographicSize_ = 10.0f;
};

} // namespace Haki
