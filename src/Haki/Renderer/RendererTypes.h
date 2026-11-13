#pragma once

#include "Haki/AssetManager/AssetTypes.h"

#include <cstdint>
#include <span>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Haki
{

inline constexpr std::size_t MaxDirectionalLights = 4;
inline constexpr std::size_t MaxPointLights = 64;
inline constexpr std::size_t MaxSpotLights = 16;
inline constexpr std::size_t ShadowCascadeCount = 4;
inline constexpr std::size_t MaxSkinJoints = 128;

struct CameraData
{
    glm::mat4 View{1.0f};
    glm::mat4 Projection{1.0f};
    glm::vec3 WorldPosition{0.0f};
    glm::vec4 Viewport{0.0f};
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
};

struct DirectionalLight
{
    glm::vec3 Direction{0.0f, -1.0f, 0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 1.0f;
};

struct PointLight
{
    glm::vec3 Position{0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 1.0f;
    float Range = 10.0f;
};

struct SpotLight
{
    glm::vec3 Position{0.0f};
    glm::vec3 Direction{0.0f, -1.0f, 0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 1.0f;
    float Range = 10.0f;
    float InnerCosine = 0.9f;
    float OuterCosine = 0.8f;
};

struct EnvironmentData
{
    TextureHandle Texture;
    float Intensity = 1.0f;
    float RotationDegrees = 0.0f;
};

struct RenderFrameData
{
    CameraData Camera;
    EnvironmentData Environment;
    std::span<const DirectionalLight> DirectionalLights;
    std::span<const PointLight> PointLights;
    std::span<const SpotLight> SpotLights;
};

struct RenderItem
{
    MeshHandle Mesh;
    MaterialHandle Material;
    glm::mat4 Transform{1.0f};
    std::uint32_t Submesh = 0;
    // The matrices are borrowed until Render or EndFrame clears the submission queue.
    std::span<const glm::mat4> JointPalette;
};

enum class RenderPath
{
    Forward
};

struct RendererConfig
{
    glm::vec4 ClearColor{0.05f, 0.05f, 0.07f, 1.0f};
    bool ClearColorBuffer = true;
    bool ClearDepthBuffer = true;
};

enum class ShadowFilterMode
{
    PCF = 0,
    PCSS = 1
};

struct RendererSettings
{
    bool FrustumCullingEnabled = true;
    bool ShowBounds = false;
    bool GpuTimingsEnabled = true;
    // Draws scene geometry as lines (View > Wireframe). Disables the depth prepass and SSAO while on.
    bool Wireframe = false;
    // Internal resolution as a fraction of the viewport (0.25-2.0). Below 1 the image is upscaled when
    // presented; above 1 it is supersampled and filtered down.
    float RenderScale = 1.0f;
    // Fixed internal render height (e.g. 720). 0 follows the window. Width keeps the window's aspect
    // ratio, and the image is upscaled to the window when presented. RenderScale still applies on top.
    int RenderHeight = 0;
    // Renders opaque depth first so the expensive lighting shader runs once per visible pixel.
    bool DepthPrepassEnabled = true;
    bool SkyboxEnabled = true;
    bool IndirectDiffuseEnabled = true;
    bool IndirectSpecularEnabled = true;
    glm::vec3 AmbientSkyColor{0.60f, 0.70f, 0.90f};
    glm::vec3 AmbientGroundColor{0.30f, 0.25f, 0.20f};
    float AmbientIntensity = 0.3f;
    bool SsaoEnabled = true;
    bool SsaoBlurEnabled = true;
    bool SsaoHalfResolution = true;
    int SsaoSampleCount = 16;
    float SsaoRadius = 0.5f;
    float SsaoBias = 0.025f;
    float SsaoPower = 1.5f;
    bool ShadowsEnabled = true;
    int ShadowMapResolution = 2048;
    ShadowFilterMode ShadowFilter = ShadowFilterMode::PCF;
    float ShadowDistance = 200.0f;
    float CascadeSplitLambda = 0.75f;
    bool ShowCascadeZones = false;
    bool BloomEnabled = true;
    float BloomIntensity = 0.1f;
    float BloomThreshold = 1.0f;
    float BloomSoftKnee = 0.5f;
    float BloomDiffusion = 7.0f;
    float Exposure = 1.0f;
    bool FxaaEnabled = true;
    float FxaaContrastThreshold = 0.0312f;
    float FxaaRelativeThreshold = 0.063f;
    float FxaaSubpixelBlending = 0.75f;
};

struct RendererStats
{
    std::uint32_t SubmittedItems = 0;
    std::uint32_t RenderWidth = 0;
    std::uint32_t RenderHeight = 0;
    std::uint32_t DrawCalls = 0;
    std::uint32_t ShadowDrawCalls = 0;
    std::uint32_t ShadowCascadesRendered = 0;
    std::uint32_t SsaoPrepassDrawCalls = 0;
    std::uint32_t DepthPrepassDrawCalls = 0;
    std::uint32_t SsaoDrawCalls = 0;
    std::uint32_t BloomDrawCalls = 0;
    std::uint32_t PostProcessDrawCalls = 0;
    std::uint32_t FxaaDrawCalls = 0;
    std::uint32_t SkyboxDrawCalls = 0;
    std::uint32_t TransmissionDrawCalls = 0;
    std::uint32_t TransmissionSceneCopies = 0;
    std::uint32_t SkippedItems = 0;
    std::uint32_t CulledItems = 0;
    std::uint32_t ShadowCulledDraws = 0;
    std::uint32_t DebugLines = 0;
    std::uint32_t DebugDrawCalls = 0;
    std::uint32_t CanvasQuads = 0;
    std::uint32_t CanvasDrawCalls = 0;
    std::uint32_t CanvasCulledQuads = 0;
    std::uint32_t ShaderChanges = 0;
    std::uint32_t MaterialChanges = 0;
    std::uint32_t MeshChanges = 0;
    std::uint32_t TextureChanges = 0;
};

// GPU time per pass in milliseconds, smoothed over recent frames. Valid becomes true once the
// first timer results come back from the GPU (a few frames after rendering starts).
struct RendererGpuTimings
{
    float ShadowMs = 0.0f;
    float SsaoMs = 0.0f;
    float ForwardMs = 0.0f;
    float BloomMs = 0.0f;
    float PostProcessMs = 0.0f;
    float DebugDrawMs = 0.0f;
    float CanvasMs = 0.0f;
    float TotalMs = 0.0f;
    bool Valid = false;
};

} // namespace Haki
