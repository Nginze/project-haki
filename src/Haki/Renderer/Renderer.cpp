#include "Haki/Renderer/Renderer.h"

#include "Haki/AssetManager/AssetManager.h"
#include "Haki/GL/Buffer.h"
#include "Haki/GL/Framebuffer.h"
#include "Haki/GL/Mesh.h"
#include "Haki/GL/Shader.h"
#include "Haki/GL/Texture.h"
#include "Haki/Renderer/Canvas.h"
#include "Haki/Renderer/DebugDraw.h"
#include "Haki/Renderer/Frustum.h"
#include "Haki/Renderer/Material.h"
#include "Haki/Renderer/ShadowCascades.h"

#include <glad/glad.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Haki
{
namespace
{

constexpr std::uint32_t DefaultShadowMapResolution = 2048;
constexpr float ShadowDepthBias = 0.0015f;
constexpr std::uint32_t EnvironmentResolution = 512;
constexpr std::uint32_t EnvironmentMipLevels = 10;
constexpr std::uint32_t IrradianceResolution = 32;
constexpr std::uint32_t PrefilterResolution = 128;
constexpr std::uint32_t PrefilterMipLevels = 5;
constexpr std::uint32_t BrdfResolution = 512;
constexpr std::size_t MaxBloomPyramidLevels = 16;
constexpr std::string_view ShadowVertexSource = R"glsl(#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 5) in uvec4 aJoints;
layout(location = 6) in vec4 aWeights;

layout(std140) uniform HakiObject
{
    mat4 Model;
    mat4 NormalMatrix;
};

layout(std140) uniform HakiSkin
{
    mat4 JointMatrices[128];
};

uniform mat4 HakiLightViewProjection;
uniform int HakiSkinningEnabled;

void main()
{
    mat4 skinMatrix = mat4(1.0);
    if (HakiSkinningEnabled != 0)
    {
        vec4 weights = aWeights / max(dot(aWeights, vec4(1.0)), 0.000001);
        skinMatrix = JointMatrices[aJoints.x] * weights.x +
                     JointMatrices[aJoints.y] * weights.y +
                     JointMatrices[aJoints.z] * weights.z +
                     JointMatrices[aJoints.w] * weights.w;
    }
    gl_Position = HakiLightViewProjection * Model * skinMatrix * vec4(aPosition, 1.0);
}
)glsl";

constexpr std::string_view ShadowFragmentSource = R"glsl(#version 330 core

void main()
{
}
)glsl";

constexpr std::string_view PostProcessVertexSource = R"glsl(#version 330 core

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)glsl";

constexpr std::string_view PostProcessFragmentSource = R"glsl(#version 330 core

in vec2 vTexCoord;

uniform sampler2D HakiHdrColor;
uniform sampler2D HakiBloomColor;
uniform float HakiExposure;
uniform float HakiBloomIntensity;
uniform int HakiTonemap;

out vec4 FragColor;

vec3 AcesFitted(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

// Khronos PBR Neutral: identity below the compression start, smooth highlight roll-off.
vec3 PbrNeutral(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return clamp(color, 0.0, 1.0);
    }
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return clamp(mix(color, vec3(newPeak), g), 0.0, 1.0);
}

void main()
{
    vec3 sceneColor = max(texture(HakiHdrColor, vTexCoord).rgb, vec3(0.0));
    vec3 bloomColor = max(texture(HakiBloomColor, vTexCoord).rgb, vec3(0.0));
    vec3 hdrColor = (sceneColor + bloomColor * max(HakiBloomIntensity, 0.0)) * max(HakiExposure, 0.0);
    vec3 mapped = HakiTonemap == 1 ? PbrNeutral(hdrColor) : AcesFitted(hdrColor);
    vec3 gammaCorrected = pow(mapped, vec3(1.0 / 2.2));
    FragColor = vec4(gammaCorrected, dot(gammaCorrected, vec3(0.299, 0.587, 0.114)));
}
)glsl";

constexpr std::string_view DebugVertexSource = R"glsl(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
uniform mat4 HakiViewProjection;
out vec4 vColor;
void main()
{
    vColor = aColor;
    gl_Position = HakiViewProjection * vec4(aPosition, 1.0);
}
)glsl";

constexpr std::string_view DebugFragmentSource = R"glsl(#version 330 core
in vec4 vColor;
uniform sampler2D HakiSceneDepth;
uniform vec4 HakiOutputViewport;
uniform int HakiDepthTest;
out vec4 FragColor;
void main()
{
    if (HakiDepthTest != 0)
    {
        // The scene depth was rendered with the same camera, so window-space depth is directly comparable.
        vec2 uv = (gl_FragCoord.xy - HakiOutputViewport.xy) / max(HakiOutputViewport.zw, vec2(1.0));
        float sceneDepth = texture(HakiSceneDepth, uv).r;
        if (gl_FragCoord.z > sceneDepth + 0.00005)
            discard;
    }
    FragColor = vColor;
}
)glsl";

constexpr std::string_view CanvasVertexSource = R"glsl(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec3 aAnchor;
uniform vec2 HakiScreenSize;
out vec2 vTexCoord;
out vec4 vColor;
flat out vec3 vAnchor;
void main()
{
    vec2 ndc = aPosition / max(HakiScreenSize, vec2(1.0)) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vAnchor = aAnchor;
}
)glsl";

constexpr std::string_view CanvasFragmentSource = R"glsl(#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
flat in vec3 vAnchor;
uniform sampler2D HakiCanvasTexture;
uniform sampler2D HakiSceneDepth;
uniform int HakiSceneDepthValid;
out vec4 FragColor;
void main()
{
    // World billboards are hidden as a whole when scene geometry covers their anchor point.
    if (vAnchor.z >= 0.0 && HakiSceneDepthValid != 0 &&
        vAnchor.z > texture(HakiSceneDepth, vAnchor.xy).r + 0.00005)
        discard;
    FragColor = vColor * texture(HakiCanvasTexture, vTexCoord);
}
)glsl";

constexpr std::string_view FxaaFragmentSource = R"glsl(#version 330 core

in vec2 vTexCoord;

uniform sampler2D HakiLdrColor;
uniform vec2 HakiTexelSize;
uniform float HakiContrastThreshold;
uniform float HakiRelativeThreshold;
uniform float HakiSubpixelBlending;

out vec4 FragColor;

const int EdgeStepCount = 10;
const float EdgeSteps[EdgeStepCount] = float[](1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 12.0);
const float EdgeGuess = 12.0;

float LumaAt(vec2 uv)
{
    return texture(HakiLdrColor, uv).a;
}

float LumaOffset(vec2 uv, vec2 offset)
{
    return texture(HakiLdrColor, uv + offset * HakiTexelSize).a;
}

void main()
{
    vec2 uv = vTexCoord;
    vec4 center = texture(HakiLdrColor, uv);
    float lumaM = center.a;
    float lumaN = LumaOffset(uv, vec2( 0.0,  1.0));
    float lumaS = LumaOffset(uv, vec2( 0.0, -1.0));
    float lumaE = LumaOffset(uv, vec2( 1.0,  0.0));
    float lumaW = LumaOffset(uv, vec2(-1.0,  0.0));

    float maxLuma = max(max(max(lumaN, lumaS), max(lumaE, lumaW)), lumaM);
    float minLuma = min(min(min(lumaN, lumaS), min(lumaE, lumaW)), lumaM);
    float range = maxLuma - minLuma;
    if (range < max(HakiContrastThreshold, HakiRelativeThreshold * maxLuma))
    {
        FragColor = vec4(center.rgb, 1.0);
        return;
    }

    float lumaNE = LumaOffset(uv, vec2( 1.0,  1.0));
    float lumaNW = LumaOffset(uv, vec2(-1.0,  1.0));
    float lumaSE = LumaOffset(uv, vec2( 1.0, -1.0));
    float lumaSW = LumaOffset(uv, vec2(-1.0, -1.0));

    // Subpixel aliasing: compare center against a 3x3 low-pass.
    float lowPass = 2.0 * (lumaN + lumaS + lumaE + lumaW) + (lumaNE + lumaNW + lumaSE + lumaSW);
    lowPass /= 12.0;
    float subpixel = clamp(abs(lowPass - lumaM) / range, 0.0, 1.0);
    subpixel = smoothstep(0.0, 1.0, subpixel);
    subpixel = subpixel * subpixel * HakiSubpixelBlending;

    // Edge orientation.
    float horizontal = abs(lumaN + lumaS - 2.0 * lumaM) * 2.0 +
                       abs(lumaNE + lumaSE - 2.0 * lumaE) +
                       abs(lumaNW + lumaSW - 2.0 * lumaW);
    float vertical = abs(lumaE + lumaW - 2.0 * lumaM) * 2.0 +
                     abs(lumaNE + lumaNW - 2.0 * lumaN) +
                     abs(lumaSE + lumaSW - 2.0 * lumaS);
    bool isHorizontal = horizontal >= vertical;

    float lumaPositive = isHorizontal ? lumaN : lumaE;
    float lumaNegative = isHorizontal ? lumaS : lumaW;
    float gradientPositive = abs(lumaPositive - lumaM);
    float gradientNegative = abs(lumaNegative - lumaM);

    float stepLength = isHorizontal ? HakiTexelSize.y : HakiTexelSize.x;
    float oppositeLuma;
    float gradient;
    if (gradientPositive < gradientNegative)
    {
        stepLength = -stepLength;
        oppositeLuma = lumaNegative;
        gradient = gradientNegative;
    }
    else
    {
        oppositeLuma = lumaPositive;
        gradient = gradientPositive;
    }

    // Walk along the edge in both directions, centred between the two edge pixels.
    vec2 edgeUv = uv;
    vec2 edgeStep;
    if (isHorizontal)
    {
        edgeUv.y += stepLength * 0.5;
        edgeStep = vec2(HakiTexelSize.x, 0.0);
    }
    else
    {
        edgeUv.x += stepLength * 0.5;
        edgeStep = vec2(0.0, HakiTexelSize.y);
    }

    float edgeLuma = (lumaM + oppositeLuma) * 0.5;
    float gradientThreshold = gradient * 0.25;

    vec2 uvPositive = edgeUv + edgeStep * EdgeSteps[0];
    vec2 uvNegative = edgeUv - edgeStep * EdgeSteps[0];
    float deltaPositive = LumaAt(uvPositive) - edgeLuma;
    float deltaNegative = LumaAt(uvNegative) - edgeLuma;
    bool donePositive = abs(deltaPositive) >= gradientThreshold;
    bool doneNegative = abs(deltaNegative) >= gradientThreshold;

    for (int i = 1; i < EdgeStepCount && !(donePositive && doneNegative); ++i)
    {
        if (!donePositive)
        {
            uvPositive += edgeStep * EdgeSteps[i];
            deltaPositive = LumaAt(uvPositive) - edgeLuma;
            donePositive = abs(deltaPositive) >= gradientThreshold;
        }
        if (!doneNegative)
        {
            uvNegative -= edgeStep * EdgeSteps[i];
            deltaNegative = LumaAt(uvNegative) - edgeLuma;
            doneNegative = abs(deltaNegative) >= gradientThreshold;
        }
    }
    if (!donePositive)
        uvPositive += edgeStep * EdgeGuess;
    if (!doneNegative)
        uvNegative -= edgeStep * EdgeGuess;

    float distancePositive = isHorizontal ? uvPositive.x - uv.x : uvPositive.y - uv.y;
    float distanceNegative = isHorizontal ? uv.x - uvNegative.x : uv.y - uvNegative.y;
    bool positiveIsNearer = distancePositive <= distanceNegative;
    float nearestDistance = min(distancePositive, distanceNegative);
    float edgeLength = distancePositive + distanceNegative;

    // Only blend if the luma at the nearer end moves the opposite way to the centre.
    float nearestDelta = positiveIsNearer ? deltaPositive : deltaNegative;
    bool centerBelowEdge = lumaM - edgeLuma < 0.0;
    float edgeBlend = ((nearestDelta < 0.0) != centerBelowEdge)
                          ? 0.5 - nearestDistance / max(edgeLength, 1e-6)
                          : 0.0;

    float blend = max(edgeBlend, subpixel);
    vec2 finalUv = uv;
    if (isHorizontal)
        finalUv.y += blend * stepLength;
    else
        finalUv.x += blend * stepLength;

    FragColor = vec4(texture(HakiLdrColor, finalUv).rgb, 1.0);
}
)glsl";

constexpr std::string_view BloomDownsampleFragmentSource = R"glsl(#version 330 core
in vec2 vTexCoord;
uniform sampler2D HakiBloomSource;
uniform vec2 HakiSourceTexelSize;
uniform int HakiApplyThreshold;
uniform float HakiThreshold;
uniform float HakiSoftKnee;
out vec4 FragColor;

vec3 SampleSource(vec2 offset)
{
    return clamp(texture(HakiBloomSource, vTexCoord + HakiSourceTexelSize * offset).rgb,
                 vec3(0.0), vec3(65504.0));
}

vec3 ApplyThreshold(vec3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float threshold = max(HakiThreshold, 0.0);
    float knee = max(threshold * clamp(HakiSoftKnee, 0.0, 1.0), 0.00001);
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(brightness - threshold, soft) / max(brightness, 0.00001);
    return color * contribution;
}

void main()
{
    vec3 a = SampleSource(vec2(-1.0, -1.0));
    vec3 b = SampleSource(vec2( 0.0, -1.0));
    vec3 c = SampleSource(vec2( 1.0, -1.0));
    vec3 d = SampleSource(vec2(-0.5, -0.5));
    vec3 e = SampleSource(vec2( 0.5, -0.5));
    vec3 f = SampleSource(vec2(-1.0,  0.0));
    vec3 g = SampleSource(vec2( 0.0,  0.0));
    vec3 h = SampleSource(vec2( 1.0,  0.0));
    vec3 i = SampleSource(vec2(-0.5,  0.5));
    vec3 j = SampleSource(vec2( 0.5,  0.5));
    vec3 k = SampleSource(vec2(-1.0,  1.0));
    vec3 l = SampleSource(vec2( 0.0,  1.0));
    vec3 m = SampleSource(vec2( 1.0,  1.0));

    vec3 color = (d + e + i + j) * 0.125;
    color += (a + b + g + f) * 0.03125;
    color += (b + c + h + g) * 0.03125;
    color += (f + g + l + k) * 0.03125;
    color += (g + h + m + l) * 0.03125;
    if (HakiApplyThreshold != 0)
        color = ApplyThreshold(color);
    FragColor = vec4(clamp(color, vec3(0.0), vec3(65504.0)), 1.0);
}
)glsl";

constexpr std::string_view BloomUpsampleFragmentSource = R"glsl(#version 330 core
in vec2 vTexCoord;
uniform sampler2D HakiBloomLow;
uniform sampler2D HakiBloomHigh;
uniform vec2 HakiLowTexelSize;
uniform float HakiSampleScale;
out vec4 FragColor;

void main()
{
    vec2 d = HakiLowTexelSize * max(HakiSampleScale, 0.0);
    vec3 low = texture(HakiBloomLow, vTexCoord + vec2(-d.x, -d.y)).rgb;
    low += texture(HakiBloomLow, vTexCoord + vec2( 0.0, -d.y)).rgb * 2.0;
    low += texture(HakiBloomLow, vTexCoord + vec2( d.x, -d.y)).rgb;
    low += texture(HakiBloomLow, vTexCoord + vec2(-d.x,  0.0)).rgb * 2.0;
    low += texture(HakiBloomLow, vTexCoord).rgb * 4.0;
    low += texture(HakiBloomLow, vTexCoord + vec2( d.x,  0.0)).rgb * 2.0;
    low += texture(HakiBloomLow, vTexCoord + vec2(-d.x,  d.y)).rgb;
    low += texture(HakiBloomLow, vTexCoord + vec2( 0.0,  d.y)).rgb * 2.0;
    low += texture(HakiBloomLow, vTexCoord + vec2( d.x,  d.y)).rgb;
    vec3 color = max(texture(HakiBloomHigh, vTexCoord).rgb, vec3(0.0)) + max(low, vec3(0.0)) / 16.0;
    FragColor = vec4(clamp(color, vec3(0.0), vec3(65504.0)), 1.0);
}
)glsl";

constexpr std::string_view SsaoPrepassVertexSource = R"glsl(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 5) in uvec4 aJoints;
layout(location = 6) in vec4 aWeights;
layout(location = 7) in vec2 aTexCoord1;

layout(std140) uniform HakiCamera
{
    mat4 View;
    mat4 Projection;
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraWorldPosition;
    vec4 Viewport;
};

layout(std140) uniform HakiObject
{
    mat4 Model;
    mat4 NormalMatrix;
};

layout(std140) uniform HakiSkin
{
    mat4 JointMatrices[128];
};

uniform int HakiSkinningEnabled;
uniform int BaseColorTextureTexCoord;
uniform mat3 BaseColorTextureUvTransform;
out vec3 vViewNormal;
out vec2 vTexCoord;

// Doubles as the depth prepass: position maths must match the forward vertex shaders exactly.
invariant gl_Position;

void main()
{
    mat4 skinMatrix = mat4(1.0);
    if (HakiSkinningEnabled != 0)
    {
        vec4 weights = aWeights / max(dot(aWeights, vec4(1.0)), 0.000001);
        skinMatrix = JointMatrices[aJoints.x] * weights.x +
                     JointMatrices[aJoints.y] * weights.y +
                     JointMatrices[aJoints.z] * weights.z +
                     JointMatrices[aJoints.w] * weights.w;
    }
    vec4 worldPosition = Model * skinMatrix * vec4(aPosition, 1.0);
    vec3 skinnedNormal = transpose(inverse(mat3(skinMatrix))) * aNormal;
    vec3 worldNormal = normalize(mat3(NormalMatrix) * skinnedNormal);
    vViewNormal = normalize(mat3(View) * worldNormal);
    // Alpha-tested materials must cut out with the same UVs the forward shader uses.
    vTexCoord = (BaseColorTextureUvTransform *
                 vec3(BaseColorTextureTexCoord == 1 ? aTexCoord1 : aTexCoord, 1.0)).xy;
    gl_Position = ViewProjection * worldPosition;
}
)glsl";

constexpr std::string_view SsaoPrepassFragmentSource = R"glsl(#version 330 core
in vec3 vViewNormal;
in vec2 vTexCoord;

uniform int AlphaMode;
uniform float AlphaCutoff;
uniform vec4 BaseColorFactor;
uniform int UseBaseColorTexture;
uniform sampler2D BaseColorTexture;

out vec3 FragNormal;

void main()
{
    float alpha = BaseColorFactor.a;
    if (UseBaseColorTexture != 0)
        alpha *= texture(BaseColorTexture, vTexCoord).a;
    if (AlphaMode == 1 && alpha < AlphaCutoff)
        discard;

    FragNormal = normalize(vViewNormal);
}
)glsl";

constexpr std::string_view SsaoFragmentSource = R"glsl(#version 330 core
in vec2 vTexCoord;
uniform sampler2D HakiDepthMap;
uniform sampler2D HakiNormalMap;
uniform sampler2D HakiNoiseMap;
uniform mat4 HakiProjection;
uniform mat4 HakiInverseProjection;
uniform vec2 HakiViewportSize;
uniform float HakiRadius;
uniform float HakiBias;
uniform float HakiPower;
uniform int HakiSampleCount;
out float FragOcclusion;

vec3 HemisphereSample(int index, int count)
{
    float fraction = (float(index) + 0.5) / float(count);
    float angle = float(index) * 2.39996323;
    float radius = sqrt(fraction);
    float z = sqrt(1.0 - fraction);
    float scale = mix(0.1, 1.0, fraction * fraction);
    return vec3(cos(angle) * radius, sin(angle) * radius, z) * scale;
}

vec3 ReconstructViewPosition(vec2 uv, float depth)
{
    vec4 position = HakiInverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return position.xyz / position.w;
}

void main()
{
    float depth = texture(HakiDepthMap, vTexCoord).r;
    if (depth >= 0.999999)
    {
        FragOcclusion = 1.0;
        return;
    }

    vec3 position = ReconstructViewPosition(vTexCoord, depth);
    vec3 normal = normalize(texture(HakiNormalMap, vTexCoord).xyz);
    vec2 randomVector = texture(HakiNoiseMap, vTexCoord * HakiViewportSize / 4.0).xy;
    vec3 tangent = vec3(randomVector, 0.0) - normal * dot(vec3(randomVector, 0.0), normal);
    if (dot(tangent, tangent) < 0.0001)
        tangent = cross(normal, abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0));
    tangent = normalize(tangent);
    mat3 basis = mat3(tangent, cross(normal, tangent), normal);

    int sampleCount = clamp(HakiSampleCount, 4, 32);
    float occlusion = 0.0;
    for (int index = 0; index < 32; ++index)
    {
        if (index >= sampleCount)
            break;
        vec3 samplePosition = position + basis * HemisphereSample(index, sampleCount) * HakiRadius;
        vec4 projected = HakiProjection * vec4(samplePosition, 1.0);
        vec2 uv = projected.xy / projected.w * 0.5 + 0.5;
        if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0)
            continue;
        float sampleDepth = texture(HakiDepthMap, uv).r;
        if (sampleDepth >= 0.999999)
            continue;
        float sampleViewZ = ReconstructViewPosition(uv, sampleDepth).z;
        float rangeWeight = smoothstep(0.0, 1.0, HakiRadius / max(abs(position.z - sampleViewZ), 0.0001));
        occlusion += (sampleViewZ >= samplePosition.z + HakiBias ? 1.0 : 0.0) * rangeWeight;
    }
    FragOcclusion = pow(clamp(1.0 - occlusion / float(sampleCount), 0.0, 1.0), HakiPower);
}
)glsl";

// One axis of a separable, depth-aware blur: 5 taps along HakiDirection (in SSAO texels).
constexpr std::string_view SsaoBlurFragmentSource = R"glsl(#version 330 core
in vec2 vTexCoord;
uniform sampler2D HakiSsaoInput;
uniform sampler2D HakiDepthMap;
uniform mat4 HakiInverseProjection;
uniform vec2 HakiDirection;
out float FragOcclusion;

float ViewDepth(vec2 uv)
{
    float depth = texture(HakiDepthMap, uv).r;
    vec4 position = HakiInverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return position.z / position.w;
}

void main()
{
    float centerDepth = texture(HakiDepthMap, vTexCoord).r;
    if (centerDepth >= 0.999999)
    {
        FragOcclusion = 1.0;
        return;
    }
    vec2 texel = HakiDirection / vec2(textureSize(HakiSsaoInput, 0));
    float centerViewDepth = ViewDepth(vTexCoord);
    // Scale the depth tolerance with distance so far geometry is not over-sharpened.
    float depthScale = 1.0 / max(abs(centerViewDepth) * 0.05, 0.05);
    const float weights[3] = float[](0.4026, 0.2442, 0.0545);
    float total = texture(HakiSsaoInput, vTexCoord).r * weights[0];
    float weightTotal = weights[0];
    for (int tap = 1; tap <= 2; ++tap)
    {
        for (int side = -1; side <= 1; side += 2)
        {
            vec2 uv = vTexCoord + texel * float(tap * side);
            float weight = weights[tap] * exp(-abs(ViewDepth(uv) - centerViewDepth) * depthScale);
            total += texture(HakiSsaoInput, uv).r * weight;
            weightTotal += weight;
        }
    }
    FragOcclusion = total / max(weightTotal, 0.0001);
}
)glsl";

constexpr std::string_view CaptureVertexSource = R"glsl(#version 330 core
layout(location = 0) in vec3 aPosition;
out vec3 vDirection;
uniform mat4 HakiCaptureProjection;
uniform mat4 HakiCaptureView;
void main()
{
    vDirection = aPosition;
    gl_Position = HakiCaptureProjection * HakiCaptureView * vec4(aPosition, 1.0);
}
)glsl";

constexpr std::string_view EquirectangularFragmentSource = R"glsl(#version 330 core
in vec3 vDirection;
out vec4 FragColor;
uniform sampler2D HakiEquirectangularMap;
const vec2 HakiInverseAtan = vec2(0.15915494309, 0.31830988618);
void main()
{
    vec3 direction = normalize(vDirection);
    vec2 uv = vec2(atan(direction.z, direction.x), asin(clamp(direction.y, -1.0, 1.0)));
    uv = uv * HakiInverseAtan + 0.5;
    FragColor = vec4(texture(HakiEquirectangularMap, uv).rgb, 1.0);
}
)glsl";

constexpr std::string_view IrradianceFragmentSource = R"glsl(#version 330 core
in vec3 vDirection;
out vec4 FragColor;
uniform samplerCube HakiEnvironmentMap;
const float PI = 3.14159265359;
void main()
{
    vec3 normal = normalize(vDirection);
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, normal));
    up = cross(normal, right);
    vec3 irradiance = vec3(0.0);
    float samples = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += 0.025)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += 0.025)
        {
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleDirection = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;
            irradiance += texture(HakiEnvironmentMap, sampleDirection).rgb * cos(theta) * sin(theta);
            samples += 1.0;
        }
    }
    FragColor = vec4(PI * irradiance / max(samples, 1.0), 1.0);
}
)glsl";

constexpr std::string_view PrefilterFragmentSource = R"glsl(#version 330 core
in vec3 vDirection;
out vec4 FragColor;
uniform samplerCube HakiEnvironmentMap;
uniform float HakiRoughness;
const float PI = 3.14159265359;
float RadicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint index, uint count) { return vec2(float(index) / float(count), RadicalInverse(index)); }
vec3 ImportanceSampleGGX(vec2 xi, vec3 normal, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosineTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sineTheta = sqrt(max(1.0 - cosineTheta * cosineTheta, 0.0));
    vec3 halfway = vec3(cos(phi) * sineTheta, sin(phi) * sineTheta, cosineTheta);
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    return normalize(tangent * halfway.x + cross(normal, tangent) * halfway.y + normal * halfway.z);
}
float DistributionGGX(vec3 normal, vec3 halfway, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.000001);
}
void main()
{
    vec3 normal = normalize(vDirection);
    vec3 result = vec3(0.0);
    float weight = 0.0;
    const uint sampleCount = 1024u;
    for (uint index = 0u; index < sampleCount; ++index)
    {
        vec3 halfway = ImportanceSampleGGX(Hammersley(index, sampleCount), normal, HakiRoughness);
        vec3 light = normalize(2.0 * dot(normal, halfway) * halfway - normal);
        float nDotL = max(dot(normal, light), 0.0);
        if (nDotL > 0.0)
        {
            float nDotH = max(dot(normal, halfway), 0.0);
            float hDotV = max(dot(halfway, normal), 0.0);
            float pdf = DistributionGGX(normal, halfway, HakiRoughness) * nDotH /
                        max(4.0 * hDotV, 0.0001) + 0.0001;
            float texelSolidAngle = 4.0 * PI / (6.0 * 512.0 * 512.0);
            float sampleSolidAngle = 1.0 / (float(sampleCount) * pdf);
            float mipLevel = HakiRoughness <= 0.0
                                 ? 0.0
                                 : 0.5 * log2(sampleSolidAngle / texelSolidAngle);
            result += textureLod(HakiEnvironmentMap, light, mipLevel).rgb * nDotL;
            weight += nDotL;
        }
    }
    FragColor = vec4(result / max(weight, 0.0001), 1.0);
}
)glsl";

constexpr std::string_view BrdfVertexSource = R"glsl(#version 330 core
layout(location = 0) in vec2 aPosition;
out vec2 vTexCoord;
void main()
{
    vTexCoord = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)glsl";

constexpr std::string_view BrdfFragmentSource = R"glsl(#version 330 core
in vec2 vTexCoord;
out vec2 FragColor;
const float PI = 3.14159265359;
float RadicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint index, uint count) { return vec2(float(index) / float(count), RadicalInverse(index)); }
vec3 ImportanceSampleGGX(vec2 xi, vec3 normal, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosineTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sineTheta = sqrt(max(1.0 - cosineTheta * cosineTheta, 0.0));
    vec3 halfway = vec3(cos(phi) * sineTheta, sin(phi) * sineTheta, cosineTheta);
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    return normalize(tangent * halfway.x + cross(normal, tangent) * halfway.y + normal * halfway.z);
}
float GeometrySchlickGGX(float nDotV, float roughness)
{
    float k = roughness * roughness * 0.5;
    return nDotV / (nDotV * (1.0 - k) + k);
}
void main()
{
    float nDotV = max(vTexCoord.x, 0.0001);
    float roughness = vTexCoord.y;
    vec3 view = vec3(sqrt(max(1.0 - nDotV * nDotV, 0.0)), 0.0, nDotV);
    vec3 normal = vec3(0.0, 0.0, 1.0);
    float a = 0.0;
    float b = 0.0;
    const uint sampleCount = 1024u;
    for (uint index = 0u; index < sampleCount; ++index)
    {
        vec3 halfway = ImportanceSampleGGX(Hammersley(index, sampleCount), normal, roughness);
        vec3 light = normalize(2.0 * dot(view, halfway) * halfway - view);
        float nDotL = max(light.z, 0.0);
        float nDotH = max(halfway.z, 0.0);
        float vDotH = max(dot(view, halfway), 0.0);
        if (nDotL > 0.0)
        {
            float geometry = GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
            float visibility = geometry * vDotH / max(nDotH * nDotV, 0.0001);
            float fresnel = pow(1.0 - vDotH, 5.0);
            a += (1.0 - fresnel) * visibility;
            b += fresnel * visibility;
        }
    }
    FragColor = vec2(a, b) / float(sampleCount);
}
)glsl";

constexpr std::string_view SkyboxVertexSource = R"glsl(#version 330 core
layout(location = 0) in vec3 aPosition;
out vec3 vDirection;
uniform mat4 HakiSkyProjection;
uniform mat4 HakiSkyView;
void main()
{
    vDirection = aPosition;
    vec4 position = HakiSkyProjection * HakiSkyView * vec4(aPosition, 1.0);
    gl_Position = position.xyww;
}
)glsl";

constexpr std::string_view SkyboxFragmentSource = R"glsl(#version 330 core
in vec3 vDirection;
out vec4 FragColor;
uniform samplerCube HakiEnvironmentMap;
uniform float HakiEnvironmentIntensity;
uniform float HakiEnvironmentRotation;
void main()
{
    float cosine = cos(HakiEnvironmentRotation);
    float sine = sin(HakiEnvironmentRotation);
    vec3 direction = normalize(vDirection);
    direction = vec3(cosine * direction.x - sine * direction.z, direction.y,
                     sine * direction.x + cosine * direction.z);
    FragColor = vec4(texture(HakiEnvironmentMap, direction).rgb * HakiEnvironmentIntensity, 1.0);
}
)glsl";

// GPU timestamp markers written at pass boundaries; each pass's time is the gap to the previous marker.
enum TimerMarker : std::size_t
{
    TimerFrameStart,
    TimerShadowEnd,
    TimerSsaoEnd,
    TimerForwardEnd,
    TimerBloomEnd,
    TimerPostEnd,
    TimerDebugEnd,
    TimerCanvasEnd,
    TimerMarkerCount
};

struct TimerFrame
{
    std::array<GLuint, TimerMarkerCount> Queries{};
    std::array<bool, TimerMarkerCount> Written{};
    bool Pending = false;
};

// Results are read a few frames late so querying them never stalls the CPU on the GPU.
constexpr std::size_t TimerFrameCount = 4;

struct PostProcessVertex
{
    float Position[2];
    float TexCoord[2];
};

struct alignas(16) GPUCameraData
{
    glm::mat4 View;
    glm::mat4 Projection;
    glm::mat4 ViewProjection;
    glm::mat4 InverseViewProjection;
    glm::vec4 WorldPosition;
    glm::vec4 Viewport;
};

struct alignas(16) GPUDirectionalLight
{
    glm::vec4 Direction;
    glm::vec4 ColorIntensity;
};

struct alignas(16) GPUPointLight
{
    glm::vec4 PositionRange;
    glm::vec4 ColorIntensity;
};

struct alignas(16) GPUSpotLight
{
    glm::vec4 PositionRange;
    glm::vec4 DirectionInner;
    glm::vec4 ColorIntensity;
    glm::vec4 Outer;
};

struct alignas(16) GPULightData
{
    glm::ivec4 Counts{0};
    std::array<GPUDirectionalLight, MaxDirectionalLights> Directional{};
    std::array<GPUPointLight, MaxPointLights> Point{};
    std::array<GPUSpotLight, MaxSpotLights> Spot{};
};

struct alignas(16) GPUObjectData
{
    glm::mat4 Model;
    glm::mat4 Normal;
};

using GPUSkinData = std::array<glm::mat4, MaxSkinJoints>;

static_assert(sizeof(GPUCameraData) % 16 == 0);
static_assert(sizeof(GPULightData) % 16 == 0);
static_assert(sizeof(GPUObjectData) % 16 == 0);
static_assert(sizeof(glm::mat4) == 64);

template <typename T>
std::span<const std::byte> BytesOf(const T &value)
{
    return {reinterpret_cast<const std::byte *>(&value), sizeof(T)};
}

struct PreparedItem
{
    RenderItem Item;
    Mesh *MeshResource = nullptr;
    Material *MaterialResource = nullptr;
    Shader *ShaderResource = nullptr;
    float Depth = 0.0f;
    WorldBounds Bounds;
    bool Cullable = true;
    std::uint32_t ObjectSlot = 0;
};

bool HandleLess(ShaderHandle left, ShaderHandle right)
{
    return left.Index != right.Index ? left.Index < right.Index : left.Generation < right.Generation;
}

bool HandleLess(MaterialHandle left, MaterialHandle right)
{
    return left.Index != right.Index ? left.Index < right.Index : left.Generation < right.Generation;
}

bool HandleLess(MeshHandle left, MeshHandle right)
{
    return left.Index != right.Index ? left.Index < right.Index : left.Generation < right.Generation;
}

} // namespace

class Renderer::Impl
{
public:
    bool Initialize(const RendererConfig &newConfig, std::string *diagnostic)
    {
        if (initialized_)
        {
            return true;
        }
        if (!GLAD_GL_VERSION_3_3)
        {
            SetDiagnostic(diagnostic, "OpenGL 3.3 has not been loaded");
            return false;
        }

        GLint maximumBlockSize = 0;
        GLint uniformBufferBindings = 0;
        GLint textureUnits = 0;
        glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maximumBlockSize);
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &uniformBufferBindings);
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &textureUnits);
        if (maximumBlockSize < static_cast<GLint>(std::max(sizeof(GPULightData), sizeof(GPUSkinData))))
        {
            SetDiagnostic(diagnostic, "OpenGL uniform blocks are too small for renderer light and skin data");
            return false;
        }
        if (uniformBufferBindings <= 3)
        {
            SetDiagnostic(diagnostic, "OpenGL provides fewer than 4 uniform buffer bindings");
            return false;
        }
        if (textureUnits < 16)
        {
            SetDiagnostic(diagnostic, "OpenGL provides fewer than 16 fragment texture units");
            return false;
        }

        if (!cameraBuffer_.Initialize(BufferType::Uniform, sizeof(GPUCameraData), nullptr, BufferUsage::Dynamic) ||
            !lightBuffer_.Initialize(BufferType::Uniform, sizeof(GPULightData), nullptr, BufferUsage::Dynamic) ||
            !objectBuffer_.Initialize(BufferType::Uniform, sizeof(GPUObjectData), nullptr, BufferUsage::Stream) ||
            !skinBuffer_.Initialize(BufferType::Uniform, sizeof(GPUSkinData), nullptr, BufferUsage::Stream))
        {
            Shutdown();
            SetDiagnostic(diagnostic, "Failed to create renderer uniform buffers");
            return false;
        }
        cameraBuffer_.BindBase(0);
        lightBuffer_.BindBase(1);
        objectBuffer_.BindBase(2);
        skinBuffer_.BindBase(3);
        GPUSkinData identityPalette;
        identityPalette.fill(glm::mat4(1.0f));
        skinBuffer_.Update(0, BytesOf(identityPalette));

        const std::array<std::byte, 4> white{
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        TextureDescriptor fallbackDescriptor;
        fallbackDescriptor.Width = 1;
        fallbackDescriptor.Height = 1;
        fallbackDescriptor.GenerateMipmaps = false;
        fallbackDescriptor.MinFilter = TextureFilter::Nearest;
        fallbackDescriptor.MagFilter = TextureFilter::Nearest;
        if (!fallbackTexture_.Initialize(fallbackDescriptor, white))
        {
            Shutdown();
            SetDiagnostic(diagnostic, "Failed to create fallback texture");
            return false;
        }

        GLint previousFramebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        bool shadowFramebuffersInitialized = true;
        for (Framebuffer &framebuffer : shadowFramebuffers_)
        {
            shadowFramebuffersInitialized = framebuffer.Initialize(
                                                {DefaultShadowMapResolution, DefaultShadowMapResolution,
                                                 FramebufferMode::DepthOnly}) &&
                                            shadowFramebuffersInitialized;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        std::string shadowDiagnostic;
        if (!shadowFramebuffersInitialized ||
            !shadowShader_.Initialize({ShadowVertexSource, ShadowFragmentSource}, shadowDiagnostic))
        {
            Shutdown();
            SetDiagnostic(diagnostic, shadowFramebuffersInitialized
                                          ? "Failed to create shadow shader: " + shadowDiagnostic
                                          : "Failed to create cascade shadow framebuffers");
            return false;
        }

        const std::array<PostProcessVertex, 3> postProcessVertices{{
            {{-1.0f, -1.0f}, {0.0f, 0.0f}},
            {{3.0f, -1.0f}, {2.0f, 0.0f}},
            {{-1.0f, 3.0f}, {0.0f, 2.0f}},
        }};
        MeshDescriptor postProcessDescriptor;
        postProcessDescriptor.Layout.Stride = sizeof(PostProcessVertex);
        postProcessDescriptor.Layout.Attributes = {
            {0, 2, VertexComponentType::Float, false, false, offsetof(PostProcessVertex, Position)},
            {1, 2, VertexComponentType::Float, false, false, offsetof(PostProcessVertex, TexCoord)},
        };
        postProcessDescriptor.VertexData = std::as_bytes(std::span(postProcessVertices));
        std::string postProcessDiagnostic;
        if (!postProcessMesh_.Initialize(postProcessDescriptor) ||
            !postProcessShader_.Initialize({PostProcessVertexSource, PostProcessFragmentSource}, postProcessDiagnostic))
        {
            Shutdown();
            SetDiagnostic(diagnostic, postProcessDiagnostic.empty()
                                          ? "Failed to create post-process triangle"
                                          : "Failed to create post-process shader: " + postProcessDiagnostic);
            return false;
        }

        std::string bloomDiagnostic;
        if (!bloomDownsampleShader_.Initialize({PostProcessVertexSource, BloomDownsampleFragmentSource},
                                                bloomDiagnostic) ||
            !bloomUpsampleShader_.Initialize({PostProcessVertexSource, BloomUpsampleFragmentSource},
                                              bloomDiagnostic))
        {
            Shutdown();
            SetDiagnostic(diagnostic, "Failed to create bloom shader: " + bloomDiagnostic);
            return false;
        }

        std::string fxaaDiagnostic;
        if (!fxaaShader_.Initialize({PostProcessVertexSource, FxaaFragmentSource}, fxaaDiagnostic))
        {
            Shutdown();
            SetDiagnostic(diagnostic, "Failed to create FXAA shader: " + fxaaDiagnostic);
            return false;
        }

        GLint uniformAlignment = 256;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniformAlignment);
        const std::size_t alignment = static_cast<std::size_t>(std::max(uniformAlignment, 1));
        objectSlotStride_ = (sizeof(GPUObjectData) + alignment - 1) / alignment * alignment;
        glGenBuffers(1, &objectRingBuffer_);
        objectRingCapacity_ = 0;
        shadowResolution_ = DefaultShadowMapResolution;

        std::string canvasDiagnostic;
        if (!canvasShader_.Initialize({CanvasVertexSource, CanvasFragmentSource}, canvasDiagnostic))
        {
            Shutdown();
            SetDiagnostic(diagnostic, "Failed to create canvas shader: " + canvasDiagnostic);
            return false;
        }
        glGenVertexArrays(1, &canvasVertexArray_);
        glGenBuffers(1, &canvasVertexBuffer_);
        glGenBuffers(1, &canvasIndexBuffer_);
        glBindVertexArray(canvasVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, canvasVertexBuffer_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, canvasIndexBuffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex),
                              reinterpret_cast<const void *>(offsetof(CanvasVertex, Position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex),
                              reinterpret_cast<const void *>(offsetof(CanvasVertex, TexCoord)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex),
                              reinterpret_cast<const void *>(offsetof(CanvasVertex, Color)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex),
                              reinterpret_cast<const void *>(offsetof(CanvasVertex, Anchor)));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        std::string debugDiagnostic;
        if (!debugShader_.Initialize({DebugVertexSource, DebugFragmentSource}, debugDiagnostic))
        {
            Shutdown();
            SetDiagnostic(diagnostic, "Failed to create debug draw shader: " + debugDiagnostic);
            return false;
        }
        glGenVertexArrays(1, &debugVertexArray_);
        glGenBuffers(1, &debugVertexBuffer_);
        glBindVertexArray(debugVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, debugVertexBuffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex),
                              reinterpret_cast<const void *>(offsetof(DebugVertex, Position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(DebugVertex),
                              reinterpret_cast<const void *>(offsetof(DebugVertex, Color)));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        debugVertexCapacity_ = 0;

        for (TimerFrame &frame : timerFrames_)
        {
            glGenQueries(static_cast<GLsizei>(frame.Queries.size()), frame.Queries.data());
            frame.Written.fill(false);
            frame.Pending = false;
        }
        timerQueriesCreated_ = true;
        timerFrameIndex_ = 0;
        timerFrameOpen_ = false;
        gpuTimings_ = {};

        std::string ssaoDiagnostic;
        const std::array<float, 32> noisePixels{
             0.7071f,  0.7071f, -0.9239f,  0.3827f,  0.1951f, -0.9808f,  0.8315f, -0.5556f,
            -0.3827f, -0.9239f,  0.9808f,  0.1951f, -0.5556f,  0.8315f,  0.0000f, -1.0000f,
             0.9239f, -0.3827f, -0.1951f,  0.9808f, -0.8315f, -0.5556f,  0.3827f,  0.9239f,
            -0.7071f,  0.7071f,  0.5556f, -0.8315f, -0.9808f, -0.1951f,  1.0000f,  0.0000f};
        TextureDescriptor noiseDescriptor;
        noiseDescriptor.Width = 4;
        noiseDescriptor.Height = 4;
        noiseDescriptor.Format = TextureFormat::RG16F;
        noiseDescriptor.MinFilter = TextureFilter::Nearest;
        noiseDescriptor.MagFilter = TextureFilter::Nearest;
        noiseDescriptor.WrapU = TextureWrap::Repeat;
        noiseDescriptor.WrapV = TextureWrap::Repeat;
        noiseDescriptor.GenerateMipmaps = false;
        if (!ssaoPrepassShader_.Initialize({SsaoPrepassVertexSource, SsaoPrepassFragmentSource}, ssaoDiagnostic) ||
            !ssaoShader_.Initialize({PostProcessVertexSource, SsaoFragmentSource}, ssaoDiagnostic) ||
            !ssaoBlurShader_.Initialize({PostProcessVertexSource, SsaoBlurFragmentSource}, ssaoDiagnostic) ||
            !ssaoNoiseTexture_.Initialize(noiseDescriptor, std::as_bytes(std::span(noisePixels))))
        {
            Shutdown();
            SetDiagnostic(diagnostic, ssaoDiagnostic.empty() ? "Failed to create SSAO noise texture"
                                                              : "Failed to create SSAO shader: " + ssaoDiagnostic);
            return false;
        }

        if (!InitializeIblResources(diagnostic))
        {
            Shutdown();
            return false;
        }

        config_ = newConfig;
        maxTextureUnits_ = static_cast<std::uint32_t>(textureUnits);
        initialized_ = true;
        return true;
    }

    void Shutdown()
    {
        submissions_.clear();
        environmentCube_.Reset();
        irradianceCube_.Reset();
        prefilterCube_.Reset();
        brdfLut_.Reset();
        skyboxShader_.Reset();
        brdfShader_.Reset();
        prefilterShader_.Reset();
        irradianceShader_.Reset();
        equirectangularShader_.Reset();
        cubeMesh_.Reset();
        if (captureDepthBuffer_ != 0)
        {
            glDeleteRenderbuffers(1, &captureDepthBuffer_);
            captureDepthBuffer_ = 0;
        }
        if (captureFramebuffer_ != 0)
        {
            glDeleteFramebuffers(1, &captureFramebuffer_);
            captureFramebuffer_ = 0;
        }
        postProcessShader_.Reset();
        fxaaShader_.Reset();
        debugShader_.Reset();
        debugDraw_.Clear();
        canvasShader_.Reset();
        canvas_.Clear();
        for (GLuint *buffer : {&canvasVertexBuffer_, &canvasIndexBuffer_})
        {
            if (*buffer != 0)
            {
                glDeleteBuffers(1, buffer);
                *buffer = 0;
            }
        }
        if (canvasVertexArray_ != 0)
        {
            glDeleteVertexArrays(1, &canvasVertexArray_);
            canvasVertexArray_ = 0;
        }
        if (objectRingBuffer_ != 0)
        {
            glDeleteBuffers(1, &objectRingBuffer_);
            objectRingBuffer_ = 0;
        }
        objectRingCapacity_ = 0;
        objectStaging_.clear();
        objectSlotCount_ = 0;
        depthPrepassValid_ = false;
        forceDepthWriteOff_ = false;
        if (debugVertexBuffer_ != 0)
        {
            glDeleteBuffers(1, &debugVertexBuffer_);
            debugVertexBuffer_ = 0;
        }
        if (debugVertexArray_ != 0)
        {
            glDeleteVertexArrays(1, &debugVertexArray_);
            debugVertexArray_ = 0;
        }
        debugVertexCapacity_ = 0;
        if (timerQueriesCreated_)
        {
            for (TimerFrame &frame : timerFrames_)
            {
                glDeleteQueries(static_cast<GLsizei>(frame.Queries.size()), frame.Queries.data());
                frame = {};
            }
            timerQueriesCreated_ = false;
        }
        timerFrameOpen_ = false;
        ldrFramebuffer_.Reset();
        bloomUpsampleShader_.Reset();
        bloomDownsampleShader_.Reset();
        for (Framebuffer &framebuffer : bloomUpFramebuffers_)
            framebuffer.Reset();
        for (Framebuffer &framebuffer : bloomDownFramebuffers_)
            framebuffer.Reset();
        postProcessMesh_.Reset();
        ssaoBlurFramebuffer_.Reset();
        ssaoFramebuffer_.Reset();
        ssaoPrepassFramebuffer_.Reset();
        ssaoNoiseTexture_.Reset();
        ssaoBlurShader_.Reset();
        ssaoShader_.Reset();
        ssaoPrepassShader_.Reset();
        transmissionSceneColor_.Reset();
        hdrFramebuffer_.Reset();
        shadowShader_.Reset();
        for (Framebuffer &framebuffer : shadowFramebuffers_)
        {
            framebuffer.Reset();
        }
        fallbackTexture_.Reset();
        skinBuffer_.Reset();
        objectBuffer_.Reset();
        lightBuffer_.Reset();
        cameraBuffer_.Reset();
        initialized_ = false;
        frameActive_ = false;
        currentEnvironmentHandle_ = {};
        environmentValid_ = false;
        ssaoValid_ = false;
        transmissionSceneValid_ = false;
        bloomValid_ = false;
        ResetStateCache();
    }

    bool BeginFrame(const RenderFrameData &frame)
    {
        if (!initialized_)
        {
            return false;
        }

        const int viewportWidth = static_cast<int>(frame.Camera.Viewport.z);
        const int viewportHeight = static_cast<int>(frame.Camera.Viewport.w);
        if (viewportWidth <= 0 || viewportHeight <= 0)
        {
            return false;
        }
        const float renderScale =
            std::clamp(std::isfinite(settings_.RenderScale) ? settings_.RenderScale : 1.0f, 0.25f, 2.0f);
        float baseWidth = static_cast<float>(viewportWidth);
        float baseHeight = static_cast<float>(viewportHeight);
        if (settings_.RenderHeight > 0)
        {
            // Fixed internal resolution: keep the window's aspect ratio so the upscale does not stretch.
            baseHeight = static_cast<float>(std::min(settings_.RenderHeight, 8192));
            baseWidth = baseHeight * static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
        }
        const int hdrWidth = std::max(1, static_cast<int>(std::lround(baseWidth * renderScale)));
        const int hdrHeight = std::max(1, static_cast<int>(std::lround(baseHeight * renderScale)));

        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &outputDrawFramebuffer_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &outputReadFramebuffer_);
        glGetIntegerv(GL_VIEWPORT, outputViewport_.data());
        canvas_.SetSize(glm::vec2(static_cast<float>(outputViewport_[2]), static_cast<float>(outputViewport_[3])));
        const bool hdrReady = hdrFramebuffer_.IsValid()
                                  ? hdrFramebuffer_.Resize(static_cast<std::uint32_t>(hdrWidth),
                                                           static_cast<std::uint32_t>(hdrHeight))
                                  : hdrFramebuffer_.Initialize({static_cast<std::uint32_t>(hdrWidth),
                                                                static_cast<std::uint32_t>(hdrHeight),
                                                                 FramebufferMode::ColorDepthTexture,
                                                                 TextureFormat::RGBA16F});
        bool ssaoReady = true;
        const auto resizeOrInitialize = [](Framebuffer &framebuffer, int width, int height, FramebufferMode mode,
                                           TextureFormat format)
        {
            return framebuffer.IsValid()
                       ? framebuffer.Resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height))
                       : framebuffer.Initialize({static_cast<std::uint32_t>(width),
                                                 static_cast<std::uint32_t>(height), mode, format});
        };
        if (settings_.SsaoEnabled || settings_.DepthPrepassEnabled)
        {
            // Full-resolution normals + depth: feeds SSAO and doubles as the forward depth prepass.
            ssaoReady = resizeOrInitialize(ssaoPrepassFramebuffer_, hdrWidth, hdrHeight,
                                           FramebufferMode::ColorDepthTexture, TextureFormat::RGB16F);
        }
        if (settings_.SsaoEnabled && ssaoReady)
        {
            const int ssaoWidth = settings_.SsaoHalfResolution ? std::max(hdrWidth / 2, 1) : hdrWidth;
            const int ssaoHeight = settings_.SsaoHalfResolution ? std::max(hdrHeight / 2, 1) : hdrHeight;
            ssaoReady = resizeOrInitialize(ssaoFramebuffer_, ssaoWidth, ssaoHeight, FramebufferMode::ColorOnly,
                                           TextureFormat::R8) &&
                        resizeOrInitialize(ssaoBlurFramebuffer_, ssaoWidth, ssaoHeight, FramebufferMode::ColorOnly,
                                           TextureFormat::R8);
        }
        if (!hdrReady || !ssaoReady)
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(outputDrawFramebuffer_));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(outputReadFramebuffer_));
            glViewport(outputViewport_[0], outputViewport_[1], outputViewport_[2], outputViewport_[3]);
            frameActive_ = false;
            ssaoValid_ = false;
            return false;
        }
        hdrFramebuffer_.Bind();
        glViewport(0, 0, hdrWidth, hdrHeight);

        submissions_.clear();
        stats_ = {};
        stats_.RenderWidth = static_cast<std::uint32_t>(hdrWidth);
        stats_.RenderHeight = static_cast<std::uint32_t>(hdrHeight);
        currentView_ = frame.Camera.View;
        currentProjection_ = frame.Camera.Projection;
        currentInverseProjection_ = glm::inverse(frame.Camera.Projection);
        currentInverseViewProjection_ = glm::inverse(frame.Camera.Projection * frame.Camera.View);
        currentCameraNear_ = frame.Camera.NearPlane;
        currentCameraPosition_ = frame.Camera.WorldPosition;
        currentCameraFar_ = frame.Camera.FarPlane;
        shadowValid_ = false;
        ssaoValid_ = false;
        transmissionSceneValid_ = false;
        hasShadowLight_ = !frame.DirectionalLights.empty() &&
                          glm::length(frame.DirectionalLights.front().Direction) > 0.0001f;
        if (hasShadowLight_)
        {
            shadowLightDirection_ = glm::normalize(frame.DirectionalLights.front().Direction);
        }
        cameraBuffer_.BindBase(0);
        lightBuffer_.BindBase(1);
        objectBuffer_.BindBase(2);
        skinBuffer_.BindBase(3);

        GPUCameraData camera{
            frame.Camera.View,
            frame.Camera.Projection,
            frame.Camera.Projection * frame.Camera.View,
            currentInverseViewProjection_,
            glm::vec4(frame.Camera.WorldPosition, 1.0f),
            // Shaders map gl_FragCoord through this, so it must describe the internal HDR target.
            glm::vec4(0.0f, 0.0f, static_cast<float>(hdrWidth), static_cast<float>(hdrHeight)),
        };
        cameraBuffer_.Update(0, BytesOf(camera));

        GPULightData lights;
        const std::size_t directionalCount = std::min(frame.DirectionalLights.size(), MaxDirectionalLights);
        const std::size_t pointCount = std::min(frame.PointLights.size(), MaxPointLights);
        const std::size_t spotCount = std::min(frame.SpotLights.size(), MaxSpotLights);
        lights.Counts = {static_cast<int>(directionalCount), static_cast<int>(pointCount),
                         static_cast<int>(spotCount), 0};
        for (std::size_t index = 0; index < directionalCount; ++index)
        {
            const DirectionalLight &light = frame.DirectionalLights[index];
            lights.Directional[index] = {glm::vec4(light.Direction, 0.0f), glm::vec4(light.Color, light.Intensity)};
        }
        for (std::size_t index = 0; index < pointCount; ++index)
        {
            const PointLight &light = frame.PointLights[index];
            lights.Point[index] = {glm::vec4(light.Position, light.Range), glm::vec4(light.Color, light.Intensity)};
        }
        for (std::size_t index = 0; index < spotCount; ++index)
        {
            const SpotLight &light = frame.SpotLights[index];
            lights.Spot[index] = {glm::vec4(light.Position, light.Range),
                                  glm::vec4(light.Direction, light.InnerCosine),
                                  glm::vec4(light.Color, light.Intensity),
                                  glm::vec4(light.OuterCosine, 0.0f, 0.0f, 0.0f)};
        }
        lightBuffer_.Update(0, BytesOf(lights));

        currentEnvironmentIntensity_ = std::max(frame.Environment.Intensity, 0.0f);
        currentEnvironmentRotation_ = glm::radians(frame.Environment.RotationDegrees);
        UpdateEnvironment(frame.Environment);

        GLbitfield clearMask = 0;
        if (config_.ClearColorBuffer)
        {
            glClearColor(config_.ClearColor.r, config_.ClearColor.g, config_.ClearColor.b, config_.ClearColor.a);
            clearMask |= GL_COLOR_BUFFER_BIT;
        }
        if (config_.ClearDepthBuffer)
        {
            glClearDepth(1.0);
            clearMask |= GL_DEPTH_BUFFER_BIT;
        }
        if (clearMask != 0)
        {
            glDepthMask(GL_TRUE);
            glClear(clearMask);
        }

        frameActive_ = true;
        ResetStateCache();
        CollectGpuTimings();
        OpenTimerFrame();
        return true;
    }

    void Submit(const RenderItem &item)
    {
        if (frameActive_)
        {
            submissions_.push_back(item);
            ++stats_.SubmittedItems;
        }
    }

    void ExecuteForwardPass()
    {
        if (!frameActive_)
        {
            return;
        }

        std::vector<PreparedItem> opaque;
        std::vector<PreparedItem> visibleOpaque;
        std::vector<PreparedItem> transmission;
        std::vector<PreparedItem> transparent;
        opaque.reserve(submissions_.size());
        visibleOpaque.reserve(submissions_.size());
        transmission.reserve(submissions_.size());
        transparent.reserve(submissions_.size());
        objectStaging_.clear();
        objectSlotCount_ = 0;
        const bool cullingEnabled = settings_.FrustumCullingEnabled;
        const Frustum cameraFrustum = Frustum::FromViewProjection(currentProjection_ * currentView_);

        for (const RenderItem &item : submissions_)
        {
            Mesh *mesh = AssetManager::GetMesh(item.Mesh);
            Material *material = AssetManager::GetMaterial(item.Material);
            Shader *shader = material ? AssetManager::GetShader(material->GetShader()) : nullptr;
            if (!mesh || !mesh->IsValid() || !material || !shader || !shader->IsValid() ||
                item.Submesh >= mesh->GetSubmeshCount() || item.JointPalette.size() > MaxSkinJoints)
            {
                ++stats_.SkippedItems;
                continue;
            }

            const glm::vec4 worldCenter = item.Transform * glm::vec4(mesh->GetBounds().Center, 1.0f);
            const float depth = -(currentView_ * worldCenter).z;
            // Skinned meshes only carry bind-pose bounds, so an animated pose may exceed them.
            PreparedItem prepared{item, mesh, material, shader, depth,
                                  TransformBounds(mesh->GetBounds(), item.Transform), item.JointPalette.empty()};
            prepared.ObjectSlot = AddObjectSlot(item.Transform);
            const bool visible = !cullingEnabled || !prepared.Cullable ||
                                 cameraFrustum.Intersects(prepared.Bounds);
            if (settings_.ShowBounds)
            {
                const glm::vec4 color = !prepared.Cullable ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f)
                                        : visible         ? glm::vec4(0.3f, 1.0f, 0.4f, 1.0f)
                                                          : glm::vec4(1.0f, 0.3f, 0.3f, 1.0f);
                debugDraw_.Box(prepared.Bounds.Center, prepared.Bounds.Extents, color, false);
            }
            if (material->GetState().Blend == BlendMode::Opaque &&
                material->GetState().Pass != MaterialPass::Transmission)
            {
                // Off-screen opaque items still cast shadows, so keep them for the shadow pass.
                opaque.push_back(prepared);
            }
            if (!visible)
            {
                ++stats_.CulledItems;
                continue;
            }
            if (material->GetState().Pass == MaterialPass::Transmission)
            {
                transmission.push_back(prepared);
            }
            else if (material->GetState().Blend == BlendMode::Opaque)
            {
                visibleOpaque.push_back(prepared);
            }
            else
            {
                transparent.push_back(prepared);
            }
        }

        UploadObjectSlots();

        const auto opaqueOrder = [](const PreparedItem &left, const PreparedItem &right)
        {
            const ShaderHandle leftShader = left.MaterialResource->GetShader();
            const ShaderHandle rightShader = right.MaterialResource->GetShader();
            if (leftShader != rightShader)
            {
                return HandleLess(leftShader, rightShader);
            }
            if (left.Item.Material != right.Item.Material)
            {
                return HandleLess(left.Item.Material, right.Item.Material);
            }
            if (left.Item.Mesh != right.Item.Mesh)
            {
                return HandleLess(left.Item.Mesh, right.Item.Mesh);
            }
            return left.Depth < right.Depth;
        };
        std::stable_sort(opaque.begin(), opaque.end(), opaqueOrder);
        std::stable_sort(visibleOpaque.begin(), visibleOpaque.end(), opaqueOrder);

        std::stable_sort(transparent.begin(), transparent.end(), [](const PreparedItem &left, const PreparedItem &right)
        {
            if (left.Depth != right.Depth)
            {
                return left.Depth > right.Depth;
            }
            const ShaderHandle leftShader = left.MaterialResource->GetShader();
            const ShaderHandle rightShader = right.MaterialResource->GetShader();
            if (leftShader != rightShader)
            {
                return HandleLess(leftShader, rightShader);
            }
            return HandleLess(left.Item.Material, right.Item.Material);
        });
        std::stable_sort(transmission.begin(), transmission.end(), [](const PreparedItem &left, const PreparedItem &right)
        {
            if (left.Depth != right.Depth)
            {
                return left.Depth > right.Depth;
            }
            return HandleLess(left.Item.Material, right.Item.Material);
        });

        shadowValid_ = false;
        if (settings_.ShadowsEnabled)
        {
            ExecuteShadowPass(opaque);
        }
        WriteTimerMarker(TimerShadowEnd);
        depthPrepassValid_ = false;
        // Wireframe lines never match prepass triangle depth, so the prepass (and SSAO) is skipped.
        const bool wireframe = settings_.Wireframe;
        const bool wantSsao = settings_.SsaoEnabled && !wireframe && !visibleOpaque.empty();
        const bool wantPrepass = settings_.DepthPrepassEnabled && !wireframe && !visibleOpaque.empty();
        if (wantSsao || wantPrepass)
        {
            if (ExecuteDepthPrepass(visibleOpaque, wantSsao))
            {
                if (wantSsao)
                {
                    ExecuteSsaoOcclusion();
                }
                if (wantPrepass)
                {
                    depthPrepassValid_ = CopyPrepassDepth();
                }
            }
            hdrFramebuffer_.Bind();
            glViewport(0, 0, static_cast<GLsizei>(hdrFramebuffer_.GetWidth()),
                       static_cast<GLsizei>(hdrFramebuffer_.GetHeight()));
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            ResetStateCache();
        }
        WriteTimerMarker(TimerSsaoEnd);

        forceWireframe_ = wireframe;
        if (depthPrepassValid_)
        {
            // Depth is already final: shade only the front-most surface and leave the depth buffer alone.
            glDepthFunc(GL_LEQUAL);
            forceDepthWriteOff_ = true;
        }
        for (const PreparedItem &item : visibleOpaque)
        {
            Draw(item, true);
        }
        if (depthPrepassValid_)
        {
            forceDepthWriteOff_ = false;
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            stateValid_ = false;
        }
        DrawSkybox();
        if (!transmission.empty())
        {
            CaptureTransmissionScene();
            for (const PreparedItem &item : transmission)
            {
                Draw(item, false, true);
            }
        }
        for (const PreparedItem &item : transparent)
        {
            Draw(item, false);
        }
        forceWireframe_ = false;
        glDepthMask(GL_TRUE);
        ResetStateCache();
        WriteTimerMarker(TimerForwardEnd);
    }

    void EndFrame()
    {
        if (!frameActive_)
        {
            return;
        }

        ExecuteBloomPass();
        WriteTimerMarker(TimerBloomEnd);
        PresentHdrFrame();
        WriteTimerMarker(TimerPostEnd);
        ExecuteDebugPass();
        WriteTimerMarker(TimerDebugEnd);
        ExecuteCanvasPass();
        WriteTimerMarker(TimerCanvasEnd);
        CloseTimerFrame();
        frameActive_ = false;
        submissions_.clear();
    }

    bool IsInitialized() const { return initialized_; }
    RendererSettings &GetSettings() { return settings_; }
    const RendererSettings &GetSettings() const { return settings_; }
    const RendererStats &GetStats() const { return stats_; }
    const RendererGpuTimings &GetGpuTimings() const { return gpuTimings_; }
    DebugDraw &GetDebugDraw() { return debugDraw_; }
    Canvas &GetCanvas() { return canvas_; }

private:
    static void SetDiagnostic(std::string *diagnostic, std::string message)
    {
        if (diagnostic)
        {
            *diagnostic = std::move(message);
        }
    }

    bool InitializeIblResources(std::string *diagnostic)
    {
        static constexpr std::array<float, 108> CubeVertices{
            -1,-1,-1,  1, 1,-1,  1,-1,-1,  1,1,-1, -1,-1,-1, -1,1,-1,
            -1,-1, 1,  1,-1, 1,  1, 1, 1,  1,1, 1, -1,1, 1, -1,-1,1,
            -1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1,-1, -1,-1,1, -1,1,1,
             1, 1, 1,  1,-1,-1,  1, 1,-1,  1,-1,-1, 1,1,1, 1,-1,1,
            -1,-1,-1,  1,-1,-1,  1,-1, 1,  1,-1,1, -1,-1,1, -1,-1,-1,
            -1, 1,-1, -1, 1, 1,  1, 1, 1,  1, 1,1,  1,1,-1, -1,1,-1};
        MeshDescriptor cubeDescriptor;
        cubeDescriptor.Layout.Stride = sizeof(float) * 3;
        cubeDescriptor.Layout.Attributes = {{0, 3, VertexComponentType::Float, false, false, 0}};
        cubeDescriptor.VertexData = std::as_bytes(std::span(CubeVertices));

        std::string shaderDiagnostic;
        if (!cubeMesh_.Initialize(cubeDescriptor) ||
            !equirectangularShader_.Initialize({CaptureVertexSource, EquirectangularFragmentSource}, shaderDiagnostic) ||
            !irradianceShader_.Initialize({CaptureVertexSource, IrradianceFragmentSource}, shaderDiagnostic) ||
            !prefilterShader_.Initialize({CaptureVertexSource, PrefilterFragmentSource}, shaderDiagnostic) ||
            !brdfShader_.Initialize({BrdfVertexSource, BrdfFragmentSource}, shaderDiagnostic) ||
            !skyboxShader_.Initialize({SkyboxVertexSource, SkyboxFragmentSource}, shaderDiagnostic))
        {
            SetDiagnostic(diagnostic, shaderDiagnostic.empty() ? "Failed to create IBL cube mesh"
                                                               : "Failed to create IBL shader: " + shaderDiagnostic);
            return false;
        }

        glGenFramebuffers(1, &captureFramebuffer_);
        glGenRenderbuffers(1, &captureDepthBuffer_);
        if (captureFramebuffer_ == 0 || captureDepthBuffer_ == 0 || !GenerateBrdfLut())
        {
            SetDiagnostic(diagnostic, "Failed to create IBL capture resources");
            return false;
        }
        return true;
    }

    bool GenerateBrdfLut()
    {
        TextureDescriptor descriptor;
        descriptor.Width = BrdfResolution;
        descriptor.Height = BrdfResolution;
        descriptor.Format = TextureFormat::RG16F;
        descriptor.MinFilter = TextureFilter::Linear;
        descriptor.MagFilter = TextureFilter::Linear;
        descriptor.WrapU = TextureWrap::ClampToEdge;
        descriptor.WrapV = TextureWrap::ClampToEdge;
        descriptor.GenerateMipmaps = false;
        if (!brdfLut_.Initialize(descriptor))
        {
            return false;
        }

        GLint drawFramebuffer = 0;
        GLint readFramebuffer = 0;
        std::array<GLint, 4> viewport{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport.data());
        glBindFramebuffer(GL_FRAMEBUFFER, captureFramebuffer_);
        glBindRenderbuffer(GL_RENDERBUFFER, captureDepthBuffer_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, BrdfResolution, BrdfResolution);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureDepthBuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               brdfLut_.GetNativeId(), 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (complete)
        {
            glViewport(0, 0, BrdfResolution, BrdfResolution);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            brdfShader_.Bind();
            postProcessMesh_.Bind();
            postProcessMesh_.Draw();
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        ResetStateCache();
        return complete;
    }

    bool BakeEnvironment(Texture &source)
    {
        TextureDescriptor environmentDescriptor;
        environmentDescriptor.Width = EnvironmentResolution;
        environmentDescriptor.Height = EnvironmentResolution;
        environmentDescriptor.Format = TextureFormat::RGB16F;
        environmentDescriptor.Type = TextureType::Cubemap;
        environmentDescriptor.MinFilter = TextureFilter::LinearMipmapLinear;
        environmentDescriptor.MagFilter = TextureFilter::Linear;
        environmentDescriptor.WrapU = TextureWrap::ClampToEdge;
        environmentDescriptor.WrapV = TextureWrap::ClampToEdge;
        environmentDescriptor.WrapW = TextureWrap::ClampToEdge;
        environmentDescriptor.MipLevels = EnvironmentMipLevels;
        environmentDescriptor.GenerateMipmaps = false;

        TextureDescriptor irradianceDescriptor = environmentDescriptor;
        irradianceDescriptor.Width = IrradianceResolution;
        irradianceDescriptor.Height = IrradianceResolution;
        irradianceDescriptor.MinFilter = TextureFilter::Linear;
        irradianceDescriptor.MipLevels = 1;

        TextureDescriptor prefilterDescriptor = environmentDescriptor;
        prefilterDescriptor.Width = PrefilterResolution;
        prefilterDescriptor.Height = PrefilterResolution;
        prefilterDescriptor.MipLevels = PrefilterMipLevels;

        environmentCube_.Reset();
        irradianceCube_.Reset();
        prefilterCube_.Reset();
        if (!environmentCube_.Initialize(environmentDescriptor) ||
            !irradianceCube_.Initialize(irradianceDescriptor) ||
            !prefilterCube_.Initialize(prefilterDescriptor))
        {
            return false;
        }

        static const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        static const std::array<glm::mat4, 6> views{
            glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
            glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
            glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
            glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
            glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
            glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0))};

        GLint drawFramebuffer = 0;
        GLint readFramebuffer = 0;
        std::array<GLint, 4> viewport{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport.data());
        glBindFramebuffer(GL_FRAMEBUFFER, captureFramebuffer_);
        glBindRenderbuffer(GL_RENDERBUFFER, captureDepthBuffer_);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        cubeMesh_.Bind();

        bool complete = true;
        const auto renderFaces = [&](Shader &shader, Texture &target, std::uint32_t size, std::uint32_t mip)
        {
            glViewport(0, 0, static_cast<GLsizei>(size), static_cast<GLsizei>(size));
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
            shader.Set("HakiCaptureProjection", projection);
            for (std::size_t face = 0; face < views.size(); ++face)
            {
                shader.Set("HakiCaptureView", views[face]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face),
                                       target.GetNativeId(), static_cast<GLint>(mip));
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                {
                    complete = false;
                    return;
                }
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                cubeMesh_.Draw();
            }
        };

        equirectangularShader_.Bind();
        if (const ShaderUniform *map = equirectangularShader_.FindUniform("HakiEquirectangularMap"))
        {
            source.Bind(static_cast<std::uint32_t>(map->TextureUnit));
        }
        renderFaces(equirectangularShader_, environmentCube_, EnvironmentResolution, 0);
        if (complete)
        {
            environmentCube_.GenerateMipmaps();
            irradianceShader_.Bind();
            if (const ShaderUniform *map = irradianceShader_.FindUniform("HakiEnvironmentMap"))
            {
                environmentCube_.Bind(static_cast<std::uint32_t>(map->TextureUnit));
            }
            renderFaces(irradianceShader_, irradianceCube_, IrradianceResolution, 0);
        }
        if (complete)
        {
            prefilterShader_.Bind();
            if (const ShaderUniform *map = prefilterShader_.FindUniform("HakiEnvironmentMap"))
            {
                environmentCube_.Bind(static_cast<std::uint32_t>(map->TextureUnit));
            }
            for (std::uint32_t mip = 0; mip < PrefilterMipLevels && complete; ++mip)
            {
                const std::uint32_t size = PrefilterResolution >> mip;
                prefilterShader_.Set("HakiRoughness", static_cast<float>(mip) /
                                                         static_cast<float>(PrefilterMipLevels - 1));
                renderFaces(prefilterShader_, prefilterCube_, size, mip);
            }
        }

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        ResetStateCache();
        return complete;
    }

    void UpdateEnvironment(const EnvironmentData &environment)
    {
        if (environment.Texture == currentEnvironmentHandle_)
        {
            return;
        }
        environmentValid_ = false;
        if (!environment.Texture.IsValid())
        {
            currentEnvironmentHandle_ = {};
            return;
        }
        Texture *source = AssetManager::GetTexture(environment.Texture);
        environmentValid_ = source && source->IsValid() && source->GetType() == TextureType::Texture2D &&
                            BakeEnvironment(*source);
        if (environmentValid_)
        {
            currentEnvironmentHandle_ = environment.Texture;
        }
        else
        {
            environmentCube_.Reset();
            irradianceCube_.Reset();
            prefilterCube_.Reset();
        }
    }

    void ExecuteBloomPass()
    {
        bloomValid_ = false;
        if (!settings_.BloomEnabled || !std::isfinite(settings_.BloomIntensity) ||
            settings_.BloomIntensity <= 0.0f)
        {
            return;
        }

        const std::uint32_t sourceWidth = hdrFramebuffer_.GetWidth();
        const std::uint32_t sourceHeight = hdrFramebuffer_.GetHeight();
        std::uint32_t width = std::max(sourceWidth / 2, 1u);
        std::uint32_t height = std::max(sourceHeight / 2, 1u);
        const float diffusion = std::clamp(std::isfinite(settings_.BloomDiffusion)
                                               ? settings_.BloomDiffusion
                                               : 7.0f,
                                           1.0f, 10.0f);
        const float levelEstimate = std::log2(static_cast<float>(std::max(width, height))) + diffusion - 10.0f;
        const float levelFloor = std::floor(levelEstimate);
        bloomLevelCount_ = static_cast<std::size_t>(std::clamp(
            static_cast<int>(levelFloor), 1, static_cast<int>(MaxBloomPyramidLevels)));
        bloomSampleScale_ = 0.5f + levelEstimate - levelFloor;

        for (std::size_t level = 0; level < bloomLevelCount_; ++level)
        {
            const auto prepare = [width, height](Framebuffer &framebuffer)
            {
                return framebuffer.IsValid()
                           ? framebuffer.Resize(width, height)
                           : framebuffer.Initialize({width, height, FramebufferMode::ColorOnly,
                                                     TextureFormat::RGBA16F});
            };
            if (!prepare(bloomDownFramebuffers_[level]) || !prepare(bloomUpFramebuffers_[level]))
            {
                hdrFramebuffer_.Bind();
                glViewport(0, 0, static_cast<GLsizei>(sourceWidth), static_cast<GLsizei>(sourceHeight));
                ResetStateCache();
                return;
            }
            width = std::max(width / 2, 1u);
            height = std::max(height / 2, 1u);
        }

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        postProcessMesh_.Bind();

        bloomDownsampleShader_.Bind();
        bloomDownsampleShader_.Set("HakiThreshold", std::max(
            std::isfinite(settings_.BloomThreshold) ? settings_.BloomThreshold : 1.0f, 0.0f));
        bloomDownsampleShader_.Set("HakiSoftKnee", std::clamp(
            std::isfinite(settings_.BloomSoftKnee) ? settings_.BloomSoftKnee : 0.5f, 0.0f, 1.0f));
        const Texture *source = &hdrFramebuffer_.GetColorAttachment();
        std::uint32_t previousWidth = sourceWidth;
        std::uint32_t previousHeight = sourceHeight;
        for (std::size_t level = 0; level < bloomLevelCount_; ++level)
        {
            Framebuffer &target = bloomDownFramebuffers_[level];
            target.Bind();
            glViewport(0, 0, static_cast<GLsizei>(target.GetWidth()), static_cast<GLsizei>(target.GetHeight()));
            bloomDownsampleShader_.Set("HakiSourceTexelSize",
                                       glm::vec2(1.0f / static_cast<float>(previousWidth),
                                                 1.0f / static_cast<float>(previousHeight)));
            bloomDownsampleShader_.Set("HakiApplyThreshold", level == 0 ? 1 : 0);
            if (const ShaderUniform *uniform = bloomDownsampleShader_.FindUniform("HakiBloomSource");
                uniform && uniform->TextureUnit >= 0)
                source->Bind(static_cast<std::uint32_t>(uniform->TextureUnit));
            if (postProcessMesh_.Draw())
                ++stats_.BloomDrawCalls;
            source = &target.GetColorAttachment();
            previousWidth = target.GetWidth();
            previousHeight = target.GetHeight();
        }

        if (bloomLevelCount_ > 1)
        {
            bloomUpsampleShader_.Bind();
            bloomUpsampleShader_.Set("HakiSampleScale", bloomSampleScale_);
            const Texture *low = &bloomDownFramebuffers_[bloomLevelCount_ - 1].GetColorAttachment();
            for (std::size_t level = bloomLevelCount_ - 1; level-- > 0;)
            {
                Framebuffer &target = bloomUpFramebuffers_[level];
                target.Bind();
                glViewport(0, 0, static_cast<GLsizei>(target.GetWidth()), static_cast<GLsizei>(target.GetHeight()));
                bloomUpsampleShader_.Set("HakiLowTexelSize",
                                         glm::vec2(1.0f / static_cast<float>(low->GetWidth()),
                                                   1.0f / static_cast<float>(low->GetHeight())));
                if (const ShaderUniform *uniform = bloomUpsampleShader_.FindUniform("HakiBloomLow");
                    uniform && uniform->TextureUnit >= 0)
                    low->Bind(static_cast<std::uint32_t>(uniform->TextureUnit));
                if (const ShaderUniform *uniform = bloomUpsampleShader_.FindUniform("HakiBloomHigh");
                    uniform && uniform->TextureUnit >= 0)
                    bloomDownFramebuffers_[level].GetColorAttachment().Bind(
                        static_cast<std::uint32_t>(uniform->TextureUnit));
                if (postProcessMesh_.Draw())
                    ++stats_.BloomDrawCalls;
                low = &target.GetColorAttachment();
            }
        }

        bloomValid_ = true;
        glDepthMask(GL_TRUE);
        ResetStateCache();
    }

    void PresentHdrFrame()
    {
        const std::uint32_t sourceWidth = hdrFramebuffer_.GetWidth();
        const std::uint32_t sourceHeight = hdrFramebuffer_.GetHeight();
        bool useFxaa = settings_.FxaaEnabled;
        if (useFxaa)
        {
            useFxaa = ldrFramebuffer_.IsValid()
                          ? ldrFramebuffer_.Resize(sourceWidth, sourceHeight)
                          : ldrFramebuffer_.Initialize({sourceWidth, sourceHeight, FramebufferMode::ColorOnly,
                                                        TextureFormat::RGBA8});
        }

        if (useFxaa)
        {
            ldrFramebuffer_.Bind();
            glViewport(0, 0, static_cast<GLsizei>(sourceWidth), static_cast<GLsizei>(sourceHeight));
        }
        else
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(outputDrawFramebuffer_));
            glViewport(outputViewport_[0], outputViewport_[1], outputViewport_[2], outputViewport_[3]);
        }
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        postProcessShader_.Bind();
        postProcessShader_.Set("HakiExposure", std::max(settings_.Exposure, 0.0f));
        postProcessShader_.Set("HakiTonemap", static_cast<int>(settings_.Tonemap));
        const float bloomIntensity = bloomValid_ && std::isfinite(settings_.BloomIntensity)
                                         ? std::max(settings_.BloomIntensity, 0.0f)
                                         : 0.0f;
        postProcessShader_.Set("HakiBloomIntensity", bloomIntensity);
        if (const ShaderUniform *hdrColor = postProcessShader_.FindUniform("HakiHdrColor");
            hdrColor && hdrColor->TextureUnit >= 0)
        {
            hdrFramebuffer_.GetColorAttachment().Bind(static_cast<std::uint32_t>(hdrColor->TextureUnit));
        }
        if (const ShaderUniform *bloomColor = postProcessShader_.FindUniform("HakiBloomColor");
            bloomColor && bloomColor->TextureUnit >= 0)
        {
            const Texture &texture = bloomValid_
                                         ? (bloomLevelCount_ > 1
                                                ? bloomUpFramebuffers_[0].GetColorAttachment()
                                                : bloomDownFramebuffers_[0].GetColorAttachment())
                                         : fallbackTexture_;
            texture.Bind(static_cast<std::uint32_t>(bloomColor->TextureUnit));
        }
        if (postProcessMesh_.Draw())
        {
            ++stats_.PostProcessDrawCalls;
        }

        if (useFxaa)
        {
            ExecuteFxaaPass();
        }

        glDepthMask(GL_TRUE);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(outputReadFramebuffer_));
        ResetStateCache();
    }

    void ExecuteDebugPass()
    {
        if (debugDraw_.IsEmpty() || !debugShader_.IsValid() || debugVertexArray_ == 0)
        {
            debugDraw_.Clear();
            return;
        }

        const std::vector<DebugVertex> &tested = debugDraw_.GetDepthTestedVertices();
        const std::vector<DebugVertex> &overlay = debugDraw_.GetOverlayVertices();
        const std::size_t vertexCount = tested.size() + overlay.size();

        glBindVertexArray(debugVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, debugVertexBuffer_);
        if (vertexCount > debugVertexCapacity_)
        {
            debugVertexCapacity_ = std::max(vertexCount, debugVertexCapacity_ * 2);
        }
        // Orphan the previous storage so the upload never waits on the GPU still reading last frame's lines.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(debugVertexCapacity_ * sizeof(DebugVertex)), nullptr,
                     GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(tested.size() * sizeof(DebugVertex)),
                        tested.data());
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(tested.size() * sizeof(DebugVertex)),
                        static_cast<GLsizeiptr>(overlay.size() * sizeof(DebugVertex)), overlay.data());

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(outputDrawFramebuffer_));
        glViewport(outputViewport_[0], outputViewport_[1], outputViewport_[2], outputViewport_[3]);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        debugShader_.Bind();
        debugShader_.Set("HakiViewProjection", currentProjection_ * currentView_);
        debugShader_.Set("HakiOutputViewport",
                         glm::vec4(static_cast<float>(outputViewport_[0]), static_cast<float>(outputViewport_[1]),
                                   static_cast<float>(outputViewport_[2]), static_cast<float>(outputViewport_[3])));
        if (const ShaderUniform *depth = debugShader_.FindUniform("HakiSceneDepth");
            depth && depth->TextureUnit >= 0)
        {
            hdrFramebuffer_.GetDepthAttachment().Bind(static_cast<std::uint32_t>(depth->TextureUnit));
        }

        if (!tested.empty())
        {
            debugShader_.Set("HakiDepthTest", 1);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(tested.size()));
            ++stats_.DebugDrawCalls;
        }
        if (!overlay.empty())
        {
            debugShader_.Set("HakiDepthTest", 0);
            glDrawArrays(GL_LINES, static_cast<GLint>(tested.size()), static_cast<GLsizei>(overlay.size()));
            ++stats_.DebugDrawCalls;
        }
        stats_.DebugLines += static_cast<std::uint32_t>(vertexCount / 2);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        debugDraw_.Clear();
        ResetStateCache();
    }

    void ExecuteCanvasPass()
    {
        if (canvas_.IsEmpty() || !canvasShader_.IsValid() || canvasVertexArray_ == 0)
        {
            canvas_.Clear();
            return;
        }
        CanvasProjection projection;
        projection.ViewProjection = currentProjection_ * currentView_;
        projection.CameraPosition = currentCameraPosition_;
        projection.ScreenSize = glm::vec2(static_cast<float>(outputViewport_[2]), static_cast<float>(outputViewport_[3]));
        std::size_t culledWorldQuads = 0;
        canvas_.BuildBatches(&fallbackTexture_, canvasVertices_, canvasIndices_, canvasBatches_, &projection,
                             &culledWorldQuads);
        stats_.CanvasCulledQuads += static_cast<std::uint32_t>(culledWorldQuads);
        if (canvasBatches_.empty())
        {
            canvas_.Clear();
            return;
        }

        glBindVertexArray(canvasVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, canvasVertexBuffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(canvasVertices_.size() * sizeof(CanvasVertex)),
                     canvasVertices_.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, canvasIndexBuffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(canvasIndices_.size() * sizeof(std::uint32_t)),
                     canvasIndices_.data(), GL_STREAM_DRAW);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(outputDrawFramebuffer_));
        glViewport(outputViewport_[0], outputViewport_[1], outputViewport_[2], outputViewport_[3]);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        canvasShader_.Bind();
        canvasShader_.Set("HakiScreenSize", glm::vec2(static_cast<float>(outputViewport_[2]),
                                                      static_cast<float>(outputViewport_[3])));
        const ShaderUniform *sceneDepth = canvasShader_.FindUniform("HakiSceneDepth");
        const bool sceneDepthValid = sceneDepth && sceneDepth->TextureUnit >= 0 &&
                                     hdrFramebuffer_.GetDepthAttachment().IsValid();
        if (sceneDepthValid)
        {
            hdrFramebuffer_.GetDepthAttachment().Bind(static_cast<std::uint32_t>(sceneDepth->TextureUnit));
        }
        canvasShader_.Set("HakiSceneDepthValid", sceneDepthValid ? 1 : 0);
        const ShaderUniform *sampler = canvasShader_.FindUniform("HakiCanvasTexture");
        const std::uint32_t unit =
            sampler && sampler->TextureUnit >= 0 ? static_cast<std::uint32_t>(sampler->TextureUnit) : 0u;
        for (const CanvasBatch &batch : canvasBatches_)
        {
            const Texture *texture = batch.BatchTexture && batch.BatchTexture->IsValid() ? batch.BatchTexture
                                                                                         : &fallbackTexture_;
            texture->Bind(unit);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.IndexCount), GL_UNSIGNED_INT,
                           reinterpret_cast<const void *>(static_cast<std::uintptr_t>(batch.FirstIndex) *
                                                          sizeof(std::uint32_t)));
            ++stats_.CanvasDrawCalls;
        }
        stats_.CanvasQuads += static_cast<std::uint32_t>(canvasVertices_.size() / 4);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        canvas_.Clear();
        ResetStateCache();
    }

    void OpenTimerFrame()
    {
        timerFrameOpen_ = false;
        if (!timerQueriesCreated_ || !settings_.GpuTimingsEnabled)
        {
            return;
        }
        TimerFrame &frame = timerFrames_[timerFrameIndex_];
        frame.Pending = false; // Results never arrived in time; drop them rather than stall.
        frame.Written.fill(false);
        timerFrameOpen_ = true;
        WriteTimerMarker(TimerFrameStart);
    }

    void WriteTimerMarker(TimerMarker marker)
    {
        if (!timerFrameOpen_)
        {
            return;
        }
        TimerFrame &frame = timerFrames_[timerFrameIndex_];
        glQueryCounter(frame.Queries[marker], GL_TIMESTAMP);
        frame.Written[marker] = true;
    }

    void CloseTimerFrame()
    {
        if (!timerFrameOpen_)
        {
            return;
        }
        timerFrames_[timerFrameIndex_].Pending = true;
        timerFrameOpen_ = false;
        timerFrameIndex_ = (timerFrameIndex_ + 1) % TimerFrameCount;
    }

    void CollectGpuTimings()
    {
        if (!timerQueriesCreated_)
        {
            return;
        }
        for (TimerFrame &frame : timerFrames_)
        {
            if (!frame.Pending)
            {
                continue;
            }
            std::size_t lastMarker = TimerFrameStart;
            for (std::size_t marker = 0; marker < TimerMarkerCount; ++marker)
            {
                if (frame.Written[marker])
                {
                    lastMarker = marker;
                }
            }
            GLuint available = 0;
            glGetQueryObjectuiv(frame.Queries[lastMarker], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == 0)
            {
                continue;
            }

            std::array<float, TimerMarkerCount> durations{};
            GLuint64 previous = 0;
            GLuint64 start = 0;
            bool havePrevious = false;
            for (std::size_t marker = 0; marker < TimerMarkerCount; ++marker)
            {
                if (!frame.Written[marker])
                {
                    continue;
                }
                GLuint64 timestamp = 0;
                glGetQueryObjectui64v(frame.Queries[marker], GL_QUERY_RESULT, &timestamp);
                if (havePrevious && timestamp >= previous)
                {
                    durations[marker] = static_cast<float>(static_cast<double>(timestamp - previous) * 1.0e-6);
                }
                if (!havePrevious)
                {
                    start = timestamp;
                }
                previous = timestamp;
                havePrevious = true;
            }
            frame.Pending = false;

            const float total = previous >= start
                                    ? static_cast<float>(static_cast<double>(previous - start) * 1.0e-6)
                                    : 0.0f;
            const bool first = !gpuTimings_.Valid;
            const auto smooth = [first](float &value, float sample)
            {
                value = first ? sample : value + (sample - value) * 0.1f;
            };
            smooth(gpuTimings_.ShadowMs, durations[TimerShadowEnd]);
            smooth(gpuTimings_.SsaoMs, durations[TimerSsaoEnd]);
            smooth(gpuTimings_.ForwardMs, durations[TimerForwardEnd]);
            smooth(gpuTimings_.BloomMs, durations[TimerBloomEnd]);
            smooth(gpuTimings_.PostProcessMs, durations[TimerPostEnd]);
            smooth(gpuTimings_.DebugDrawMs, durations[TimerDebugEnd]);
            smooth(gpuTimings_.CanvasMs, durations[TimerCanvasEnd]);
            smooth(gpuTimings_.TotalMs, total);
            gpuTimings_.Valid = true;
        }
    }

    void ExecuteFxaaPass()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(outputDrawFramebuffer_));
        glViewport(outputViewport_[0], outputViewport_[1], outputViewport_[2], outputViewport_[3]);

        const Texture &ldrColor = ldrFramebuffer_.GetColorAttachment();
        const auto sanitize = [](float value, float fallback, float low, float high)
        {
            return std::clamp(std::isfinite(value) ? value : fallback, low, high);
        };

        fxaaShader_.Bind();
        fxaaShader_.Set("HakiTexelSize", glm::vec2(1.0f / static_cast<float>(std::max(ldrColor.GetWidth(), 1u)),
                                                   1.0f / static_cast<float>(std::max(ldrColor.GetHeight(), 1u))));
        fxaaShader_.Set("HakiContrastThreshold", sanitize(settings_.FxaaContrastThreshold, 0.0312f, 0.0f, 1.0f));
        fxaaShader_.Set("HakiRelativeThreshold", sanitize(settings_.FxaaRelativeThreshold, 0.063f, 0.0f, 1.0f));
        fxaaShader_.Set("HakiSubpixelBlending", sanitize(settings_.FxaaSubpixelBlending, 0.75f, 0.0f, 1.0f));
        if (const ShaderUniform *uniform = fxaaShader_.FindUniform("HakiLdrColor");
            uniform && uniform->TextureUnit >= 0)
        {
            ldrColor.Bind(static_cast<std::uint32_t>(uniform->TextureUnit));
        }
        if (postProcessMesh_.Draw())
        {
            ++stats_.FxaaDrawCalls;
        }
    }

    bool ComputeShadowCascades(const std::vector<PreparedItem> &opaque)
    {
        const float cameraNear = std::max(currentCameraNear_, 0.001f);
        const float cameraFar = std::max(currentCameraFar_, cameraNear + 0.001f);
        const float shadowFar = std::clamp(settings_.ShadowDistance, cameraNear + 0.001f, cameraFar);
        cascadeSplits_ = CalculatePssmSplits(cameraNear, shadowFar, settings_.CascadeSplitLambda);
        float previousSplit = cameraNear;

        std::array<glm::vec3, 4> frustumNear;
        std::array<glm::vec3, 4> frustumFar;
        std::size_t cornerIndex = 0;
        for (int y = -1; y <= 1; y += 2)
        {
            for (int x = -1; x <= 1; x += 2)
            {
                glm::vec4 nearCorner = currentInverseViewProjection_ * glm::vec4(x, y, -1.0f, 1.0f);
                glm::vec4 farCorner = currentInverseViewProjection_ * glm::vec4(x, y, 1.0f, 1.0f);
                if (std::abs(nearCorner.w) <= 0.000001f || std::abs(farCorner.w) <= 0.000001f)
                {
                    return false;
                }
                frustumNear[cornerIndex] = glm::vec3(nearCorner) / nearCorner.w;
                frustumFar[cornerIndex] = glm::vec3(farCorner) / farCorner.w;
                ++cornerIndex;
            }
        }

        const glm::vec3 referenceUp =
            std::abs(glm::dot(shadowLightDirection_, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 lightRight = glm::normalize(glm::cross(shadowLightDirection_, referenceUp));
        const glm::vec3 lightUp = glm::normalize(glm::cross(lightRight, shadowLightDirection_));
        const float cameraRange = cameraFar - cameraNear;

        for (std::size_t cascade = 0; cascade < ShadowCascadeCount; ++cascade)
        {
            const float split = cascadeSplits_[cascade];
            const float nearRatio = (previousSplit - cameraNear) / cameraRange;
            const float farRatio = (split - cameraNear) / cameraRange;
            std::array<glm::vec3, 8> corners;
            glm::vec3 center(0.0f);
            for (std::size_t index = 0; index < 4; ++index)
            {
                const glm::vec3 ray = frustumFar[index] - frustumNear[index];
                corners[index] = frustumNear[index] + ray * nearRatio;
                corners[index + 4] = frustumNear[index] + ray * farRatio;
                center += corners[index] + corners[index + 4];
            }
            center /= static_cast<float>(corners.size());

            float radius = 0.0f;
            for (const glm::vec3 &corner : corners)
            {
                radius = std::max(radius, glm::length(corner - center));
            }
            radius = std::ceil(radius * 16.0f) / 16.0f;
            if (!std::isfinite(radius) || radius <= 0.0001f)
            {
                return false;
            }

            const float worldUnitsPerTexel = radius * 2.0f / static_cast<float>(shadowResolution_);
            const float centerRight = glm::dot(center, lightRight);
            const float centerUp = glm::dot(center, lightUp);
            const float snappedRight = std::round(centerRight / worldUnitsPerTexel) * worldUnitsPerTexel;
            const float snappedUp = std::round(centerUp / worldUnitsPerTexel) * worldUnitsPerTexel;
            center += lightRight * (snappedRight - centerRight) + lightUp * (snappedUp - centerUp);

            float casterDistance = radius;
            for (const PreparedItem &prepared : opaque)
            {
                const MeshBounds &bounds = prepared.MeshResource->GetBounds();
                for (int x = -1; x <= 1; x += 2)
                {
                    for (int y = -1; y <= 1; y += 2)
                    {
                        for (int z = -1; z <= 1; z += 2)
                        {
                            const glm::vec3 local = bounds.Center + bounds.Extents * glm::vec3(x, y, z);
                            const glm::vec3 world = glm::vec3(prepared.Item.Transform * glm::vec4(local, 1.0f));
                            if (std::isfinite(world.x) && std::isfinite(world.y) && std::isfinite(world.z))
                            {
                                casterDistance = std::max(casterDistance, glm::length(world - center));
                            }
                        }
                    }
                }
            }

            const float depthExtent = casterDistance + std::max(casterDistance * 0.05f, 1.0f);
            const glm::vec3 eye = center - shadowLightDirection_ * depthExtent;
            const glm::mat4 lightView = glm::lookAt(eye, center, referenceUp);
            const glm::mat4 lightProjection =
                glm::ortho(-radius, radius, -radius, radius, 0.1f, depthExtent * 2.0f);
            shadowViewProjections_[cascade] = lightProjection * lightView;
            cascadeWorldUnitsPerTexel_[cascade] = worldUnitsPerTexel;
            previousSplit = split;
        }
        return true;
    }

    void ExecuteShadowPass(const std::vector<PreparedItem> &opaque)
    {
        shadowValid_ = false;
        const std::uint32_t requestedResolution = static_cast<std::uint32_t>(
            std::clamp(settings_.ShadowMapResolution, 256, 8192));
        if (requestedResolution != shadowResolution_)
        {
            bool resized = true;
            for (Framebuffer &framebuffer : shadowFramebuffers_)
            {
                resized = framebuffer.Resize(requestedResolution, requestedResolution) && resized;
            }
            hdrFramebuffer_.Bind();
            glViewport(0, 0, static_cast<GLsizei>(hdrFramebuffer_.GetWidth()),
                       static_cast<GLsizei>(hdrFramebuffer_.GetHeight()));
            if (!resized)
            {
                return;
            }
            shadowResolution_ = requestedResolution;
        }
        if (!hasShadowLight_ || opaque.empty() || !ComputeShadowCascades(opaque))
        {
            return;
        }

        GLint previousDrawFramebuffer = 0;
        GLint previousReadFramebuffer = 0;
        std::array<GLint, 4> previousViewport{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport.data());

        glViewport(0, 0, static_cast<GLsizei>(shadowResolution_), static_cast<GLsizei>(shadowResolution_));
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);
        shadowShader_.Bind();
        for (std::size_t cascade = 0; cascade < ShadowCascadeCount; ++cascade)
        {
            shadowFramebuffers_[cascade].Bind();
            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT);
            shadowShader_.Set("HakiLightViewProjection", shadowViewProjections_[cascade]);
            const Frustum cascadeFrustum = Frustum::FromViewProjection(shadowViewProjections_[cascade]);
            MeshHandle currentShadowMesh;
            for (const PreparedItem &item : opaque)
            {
                if (settings_.FrustumCullingEnabled && item.Cullable && !cascadeFrustum.Intersects(item.Bounds))
                {
                    ++stats_.ShadowCulledDraws;
                    continue;
                }
                ApplySkinning(shadowShader_, item.Item);
                BindObjectSlot(item.ObjectSlot);
                if (currentShadowMesh != item.Item.Mesh)
                {
                    item.MeshResource->Bind();
                    currentShadowMesh = item.Item.Mesh;
                }
                if (item.MeshResource->Draw(item.Item.Submesh))
                {
                    ++stats_.ShadowDrawCalls;
                }
            }
            ++stats_.ShadowCascadesRendered;
        }

        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        shadowValid_ = true;
        ResetStateCache();
    }

    // Renders visible opaque depth (and view-space normals when SSAO needs them) at full resolution.
    bool ExecuteDepthPrepass(const std::vector<PreparedItem> &opaque, bool writeNormals)
    {
        const GLsizei width = static_cast<GLsizei>(ssaoPrepassFramebuffer_.GetWidth());
        const GLsizei height = static_cast<GLsizei>(ssaoPrepassFramebuffer_.GetHeight());
        if (!ssaoPrepassFramebuffer_.IsValid() || width <= 0 || height <= 0)
        {
            return false;
        }

        ssaoPrepassFramebuffer_.Bind();
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!writeNormals)
        {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        }
        ssaoPrepassShader_.Bind();

        for (const PreparedItem &item : opaque)
        {
            const MaterialState &state = item.MaterialResource->GetState();
            // Items that do not write solid depth themselves must not occlude anything in the prepass.
            if (!state.DepthTest || !state.DepthWrite || state.Wireframe)
            {
                continue;
            }
            if (state.Cull == CullMode::None)
            {
                glDisable(GL_CULL_FACE);
            }
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(state.Cull == CullMode::Back ? GL_BACK : GL_FRONT);
            }

            int alphaMode = 0;
            float alphaCutoff = 0.5f;
            glm::vec4 baseColorFactor(1.0f);
            int useBaseColorTexture = 0;
            Texture *baseColorTexture = nullptr;
            const auto &parameters = item.MaterialResource->GetParameters();
            const auto getParameter = [&parameters](std::string_view name) -> const MaterialParameter *
            {
                const auto found = parameters.find(std::string(name));
                return found == parameters.end() ? nullptr : &found->second;
            };
            if (const MaterialParameter *parameter = getParameter("AlphaMode"))
                if (const int *value = std::get_if<int>(parameter)) alphaMode = *value;
            if (const MaterialParameter *parameter = getParameter("AlphaCutoff"))
                if (const float *value = std::get_if<float>(parameter)) alphaCutoff = *value;
            if (const MaterialParameter *parameter = getParameter("BaseColorFactor"))
                if (const glm::vec4 *value = std::get_if<glm::vec4>(parameter)) baseColorFactor = *value;
            if (const MaterialParameter *parameter = getParameter("UseBaseColorTexture"))
                if (const int *value = std::get_if<int>(parameter)) useBaseColorTexture = *value;
            int baseColorTexCoord = 0;
            glm::mat3 baseColorUvTransform(1.0f);
            if (const MaterialParameter *parameter = getParameter("BaseColorTextureTexCoord"))
                if (const int *value = std::get_if<int>(parameter)) baseColorTexCoord = *value;
            if (const MaterialParameter *parameter = getParameter("BaseColorTextureUvTransform"))
                if (const glm::mat3 *value = std::get_if<glm::mat3>(parameter)) baseColorUvTransform = *value;
            ssaoPrepassShader_.Set("BaseColorTextureTexCoord", baseColorTexCoord);
            ssaoPrepassShader_.Set("BaseColorTextureUvTransform", baseColorUvTransform);
            if (const MaterialParameter *parameter = getParameter("BaseColorTexture"))
            {
                if (const TextureHandle *value = std::get_if<TextureHandle>(parameter))
                    baseColorTexture = AssetManager::GetTexture(*value);
            }
            const ShaderUniform *baseColorMap = ssaoPrepassShader_.FindUniform("BaseColorTexture");
            if (useBaseColorTexture != 0 && baseColorTexture && baseColorTexture->IsValid() && baseColorMap &&
                baseColorMap->TextureUnit >= 0 &&
                static_cast<std::uint32_t>(baseColorMap->TextureUnit) < maxTextureUnits_)
            {
                baseColorTexture->Bind(static_cast<std::uint32_t>(baseColorMap->TextureUnit));
            }
            else
            {
                useBaseColorTexture = 0;
            }
            ssaoPrepassShader_.Set("AlphaMode", alphaMode);
            ssaoPrepassShader_.Set("AlphaCutoff", std::max(alphaCutoff, 0.0f));
            ssaoPrepassShader_.Set("BaseColorFactor", baseColorFactor);
            ssaoPrepassShader_.Set("UseBaseColorTexture", useBaseColorTexture);
            ApplySkinning(ssaoPrepassShader_, item.Item);

            BindObjectSlot(item.ObjectSlot);
            item.MeshResource->Bind();
            if (item.MeshResource->Draw(item.Item.Submesh))
            {
                ++(writeNormals ? stats_.SsaoPrepassDrawCalls : stats_.DepthPrepassDrawCalls);
            }
        }

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        return true;
    }

    // Copies prepass depth into the HDR target so the forward pass can early-z against it.
    bool CopyPrepassDepth()
    {
        if (ssaoPrepassFramebuffer_.GetWidth() != hdrFramebuffer_.GetWidth() ||
            ssaoPrepassFramebuffer_.GetHeight() != hdrFramebuffer_.GetHeight())
        {
            return false;
        }
        const GLint width = static_cast<GLint>(hdrFramebuffer_.GetWidth());
        const GLint height = static_cast<GLint>(hdrFramebuffer_.GetHeight());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, ssaoPrepassFramebuffer_.GetId());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdrFramebuffer_.GetId());
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        return true;
    }

    // Computes SSAO from the prepass at the (possibly half) SSAO resolution, then blurs it separably.
    // The final occlusion always ends up in ssaoFramebuffer_.
    void ExecuteSsaoOcclusion()
    {
        ssaoValid_ = false;
        const GLsizei width = static_cast<GLsizei>(ssaoFramebuffer_.GetWidth());
        const GLsizei height = static_cast<GLsizei>(ssaoFramebuffer_.GetHeight());
        if (width <= 0 || height <= 0)
        {
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        postProcessMesh_.Bind();

        const auto bindSampler = [this](Shader &shader, std::string_view name, const Texture &texture)
        {
            const ShaderUniform *uniform = shader.FindUniform(name);
            if (!uniform || uniform->Type != ShaderValueType::Sampler2D || uniform->TextureUnit < 0 ||
                static_cast<std::uint32_t>(uniform->TextureUnit) >= maxTextureUnits_)
            {
                return false;
            }
            texture.Bind(static_cast<std::uint32_t>(uniform->TextureUnit));
            return true;
        };

        ssaoFramebuffer_.Bind();
        glViewport(0, 0, width, height);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ssaoShader_.Bind();
        ssaoShader_.Set("HakiProjection", currentProjection_);
        ssaoShader_.Set("HakiInverseProjection", currentInverseProjection_);
        ssaoShader_.Set("HakiViewportSize", glm::vec2(width, height));
        const auto sanitize = [](float value, float fallback, float minimum, float maximum)
        {
            return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
        };
        ssaoShader_.Set("HakiRadius", sanitize(settings_.SsaoRadius, 0.5f, 0.001f, 100.0f));
        ssaoShader_.Set("HakiBias", sanitize(settings_.SsaoBias, 0.025f, 0.0f, 10.0f));
        ssaoShader_.Set("HakiPower", sanitize(settings_.SsaoPower, 1.5f, 0.01f, 16.0f));
        ssaoShader_.Set("HakiSampleCount", std::clamp(settings_.SsaoSampleCount, 4, 32));
        const bool rawBindings =
            bindSampler(ssaoShader_, "HakiDepthMap", ssaoPrepassFramebuffer_.GetDepthAttachment()) &&
            bindSampler(ssaoShader_, "HakiNormalMap", ssaoPrepassFramebuffer_.GetColorAttachment()) &&
            bindSampler(ssaoShader_, "HakiNoiseMap", ssaoNoiseTexture_);
        bool rawValid = false;
        if (rawBindings && postProcessMesh_.Draw())
        {
            ++stats_.SsaoDrawCalls;
            rawValid = true;
        }

        bool blurValid = false;
        if (rawValid && settings_.SsaoBlurEnabled)
        {
            ssaoBlurShader_.Bind();
            ssaoBlurShader_.Set("HakiInverseProjection", currentInverseProjection_);
            const auto blurPass = [&](Framebuffer &target, const Framebuffer &source, glm::vec2 direction)
            {
                target.Bind();
                ssaoBlurShader_.Set("HakiDirection", direction);
                if (!bindSampler(ssaoBlurShader_, "HakiSsaoInput", source.GetColorAttachment()) ||
                    !bindSampler(ssaoBlurShader_, "HakiDepthMap", ssaoPrepassFramebuffer_.GetDepthAttachment()) ||
                    !postProcessMesh_.Draw())
                {
                    return false;
                }
                ++stats_.SsaoDrawCalls;
                return true;
            };
            blurValid = blurPass(ssaoBlurFramebuffer_, ssaoFramebuffer_, glm::vec2(1.0f, 0.0f)) &&
                        blurPass(ssaoFramebuffer_, ssaoBlurFramebuffer_, glm::vec2(0.0f, 1.0f));
        }
        ssaoValid_ = settings_.SsaoBlurEnabled ? blurValid : rawValid;
        glDepthMask(GL_TRUE);
    }

    // Per-frame object data: every prepared item gets an aligned slot in one uniform buffer upload.
    std::uint32_t AddObjectSlot(const glm::mat4 &transform)
    {
        const GPUObjectData object{transform, glm::transpose(glm::inverse(transform))};
        const std::size_t offset = objectStaging_.size();
        objectStaging_.resize(offset + objectSlotStride_);
        std::memcpy(objectStaging_.data() + offset, &object, sizeof(object));
        return objectSlotCount_++;
    }

    void UploadObjectSlots()
    {
        if (objectRingBuffer_ == 0 || objectStaging_.empty())
        {
            return;
        }
        glBindBuffer(GL_UNIFORM_BUFFER, objectRingBuffer_);
        if (objectStaging_.size() > objectRingCapacity_)
        {
            objectRingCapacity_ = std::max(objectStaging_.size(), objectRingCapacity_ * 2);
        }
        // Orphan last frame's storage so this upload never waits for draws still reading it.
        glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(objectRingCapacity_), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(objectStaging_.size()), objectStaging_.data());
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void BindObjectSlot(std::uint32_t slot)
    {
        glBindBufferRange(GL_UNIFORM_BUFFER, 2, objectRingBuffer_,
                          static_cast<GLintptr>(static_cast<std::size_t>(slot) * objectSlotStride_),
                          static_cast<GLsizeiptr>(sizeof(GPUObjectData)));
    }

    void ResetStateCache()
    {
        globalsValid_ = false;
        currentShader_ = {};
        currentMaterial_ = {};
        currentMesh_ = {};
        currentTextures_.fill(nullptr);
        textureUnitsBound_.fill(false);
        stateValid_ = false;
    }

    bool CaptureTransmissionScene()
    {
        const std::uint32_t width = hdrFramebuffer_.GetWidth();
        const std::uint32_t height = hdrFramebuffer_.GetHeight();
        if (width == 0 || height == 0)
        {
            transmissionSceneValid_ = false;
            return false;
        }

        std::uint32_t mipLevels = 1;
        for (std::uint32_t extent = std::max(width, height); extent > 1; extent >>= 1)
        {
            ++mipLevels;
        }
        if (!transmissionSceneColor_.IsValid() || transmissionSceneColor_.GetWidth() != width ||
            transmissionSceneColor_.GetHeight() != height || transmissionSceneColor_.GetMipLevels() != mipLevels)
        {
            TextureDescriptor descriptor;
            descriptor.Width = width;
            descriptor.Height = height;
            descriptor.Format = TextureFormat::RGBA16F;
            descriptor.MinFilter = TextureFilter::LinearMipmapLinear;
            descriptor.MagFilter = TextureFilter::Linear;
            descriptor.WrapU = TextureWrap::ClampToEdge;
            descriptor.WrapV = TextureWrap::ClampToEdge;
            descriptor.MipLevels = mipLevels;
            descriptor.GenerateMipmaps = false;
            if (!transmissionSceneColor_.Initialize(descriptor))
            {
                transmissionSceneValid_ = false;
                return false;
            }
        }

        hdrFramebuffer_.Bind();
        transmissionSceneColor_.Bind(0);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, static_cast<GLsizei>(width),
                            static_cast<GLsizei>(height));
        transmissionSceneColor_.GenerateMipmaps();
        transmissionSceneValid_ = true;
        ++stats_.TransmissionSceneCopies;
        ResetStateCache();
        return true;
    }

    void ApplyState(const MaterialState &state)
    {
        MaterialState effectiveState = state;
        if (forceWireframe_)
        {
            effectiveState.Wireframe = true;
        }
        if (effectiveState.Blend == BlendMode::AlphaBlend || forceDepthWriteOff_)
        {
            effectiveState.DepthWrite = false;
        }
        if (!stateValid_ || effectiveState.DepthTest != currentState_.DepthTest)
        {
            effectiveState.DepthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        }
        if (!stateValid_ || effectiveState.DepthWrite != currentState_.DepthWrite)
        {
            glDepthMask(effectiveState.DepthWrite ? GL_TRUE : GL_FALSE);
        }
        if (!stateValid_ || effectiveState.Blend != currentState_.Blend)
        {
            if (effectiveState.Blend == BlendMode::AlphaBlend)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            else
            {
                glDisable(GL_BLEND);
            }
        }
        if (!stateValid_ || effectiveState.Cull != currentState_.Cull)
        {
            if (effectiveState.Cull == CullMode::None)
            {
                glDisable(GL_CULL_FACE);
            }
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(effectiveState.Cull == CullMode::Back ? GL_BACK : GL_FRONT);
            }
        }
        if (!stateValid_ || effectiveState.Wireframe != currentState_.Wireframe)
        {
            glPolygonMode(GL_FRONT_AND_BACK, effectiveState.Wireframe ? GL_LINE : GL_FILL);
        }
        currentState_ = effectiveState;
        stateValid_ = true;
    }

    void ApplySkinning(Shader &shader, const RenderItem &item)
    {
        const bool enabled = !item.JointPalette.empty();
        shader.Set("HakiSkinningEnabled", enabled ? 1 : 0);
        if (enabled)
        {
            skinBuffer_.Update(0, std::as_bytes(item.JointPalette));
        }
    }

    void ApplyMaterial(const PreparedItem &item)
    {
        Shader &shader = *item.ShaderResource;
        const auto &parameters = item.MaterialResource->GetParameters();

        // Older generated PBR materials predate these uniforms, so reset extension state before applying overrides.
        shader.Set("PbrWorkflow", 0);
        shader.Set("SpecularFactor", glm::vec3(1.0f));
        shader.Set("GlossinessFactor", 1.0f);
        shader.Set("TransmissionFactor", 0.0f);
        shader.Set("UseTransmissionTexture", 0);
        shader.Set("ThicknessFactor", 0.0f);
        shader.Set("UseThicknessTexture", 0);
        shader.Set("AttenuationColor", glm::vec3(1.0f));
        shader.Set("AttenuationDistance", 0.0f);
        shader.Set("Ior", 1.5f);
        shader.Set("DielectricSpecular", glm::vec3(1.0f));
        static constexpr std::array<std::string_view, 7> TexturedInputs{
            "BaseColorTexture", "MetallicRoughnessTexture", "NormalTexture", "EmissiveTexture",
            "OcclusionTexture", "TransmissionTexture", "ThicknessTexture"};
        for (const std::string_view input : TexturedInputs)
        {
            std::string name(input);
            shader.Set(name + "TexCoord", 0);
            shader.Set(name + "UvTransform", glm::mat3(1.0f));
        }

        for (const ShaderUniform &uniform : shader.GetUniforms())
        {
            if (uniform.Type != ShaderValueType::Sampler2D)
            {
                continue;
            }
            if (uniform.Name.starts_with("HakiShadowMap"))
            {
                continue;
            }
            if (uniform.Name == "HakiIrradianceMap" || uniform.Name == "HakiPrefilterMap" ||
                uniform.Name == "HakiBrdfLut" || uniform.Name == "HakiSsaoMap" ||
                uniform.Name == "HakiTransmissionSceneColor")
            {
                continue;
            }
            TextureHandle textureHandle;
            Texture *texture = nullptr;
            if (const auto parameter = parameters.find(uniform.Name); parameter != parameters.end())
            {
                if (const auto *handle = std::get_if<TextureHandle>(&parameter->second))
                {
                    textureHandle = *handle;
                    texture = AssetManager::GetTexture(*handle);
                }
            }
            if (uniform.TextureUnit < 0 || static_cast<std::uint32_t>(uniform.TextureUnit) >= maxTextureUnits_)
            {
                continue;
            }
            const std::size_t unit = static_cast<std::size_t>(uniform.TextureUnit);
            if (!texture || !texture->IsValid())
            {
                textureHandle = {};
                texture = &fallbackTexture_;
            }
            if (unit >= currentTextures_.size() || !textureUnitsBound_[unit] || currentTextures_[unit] != texture)
            {
                texture->Bind(static_cast<std::uint32_t>(unit));
                if (unit < currentTextures_.size())
                {
                    currentTextures_[unit] = texture;
                    textureUnitsBound_[unit] = true;
                }
                ++stats_.TextureChanges;
            }
        }

        for (const auto &[name, parameter] : parameters)
        {
            std::visit(
                [&shader, &name](const auto &value)
                {
                    using Value = std::decay_t<decltype(value)>;
                    if constexpr (!std::is_same_v<Value, TextureHandle>)
                    {
                        shader.Set(name, value);
                    }
                },
                parameter);
        }
    }

    void ApplyShadowGlobals(Shader &shader)
    {
        shader.Set("HakiShadowEnabled", shadowValid_ ? 1 : 0);
        if (!shadowValid_)
        {
            return;
        }

        static constexpr std::array<std::string_view, ShadowCascadeCount> MatrixNames{
            "HakiLightViewProjection0", "HakiLightViewProjection1", "HakiLightViewProjection2",
            "HakiLightViewProjection3"};
        static constexpr std::array<std::string_view, ShadowCascadeCount> MapNames{
            "HakiShadowMap0", "HakiShadowMap1", "HakiShadowMap2", "HakiShadowMap3"};

        for (std::size_t cascade = 0; cascade < ShadowCascadeCount; ++cascade)
        {
            shader.Set(MatrixNames[cascade], shadowViewProjections_[cascade]);
        }
        shader.Set("HakiCascadeSplits",
                   glm::vec4(cascadeSplits_[0], cascadeSplits_[1], cascadeSplits_[2], cascadeSplits_[3]));
        shader.Set("HakiCascadeTexelWorldSize",
                   glm::vec4(cascadeWorldUnitsPerTexel_[0], cascadeWorldUnitsPerTexel_[1],
                             cascadeWorldUnitsPerTexel_[2], cascadeWorldUnitsPerTexel_[3]));
        shader.Set("HakiCameraNear", currentCameraNear_);
        shader.Set("HakiCascadeBlendFraction", 0.10f);
        shader.Set("HakiShowCascadeZones", settings_.ShowCascadeZones ? 1 : 0);
        shader.Set("HakiShadowBias", ShadowDepthBias);
        shader.Set("HakiShadowFilterMode", static_cast<int>(settings_.ShadowFilter));

        for (std::size_t cascade = 0; cascade < ShadowCascadeCount; ++cascade)
        {
            const ShaderUniform *shadowMap = shader.FindUniform(MapNames[cascade]);
            if (!shadowMap || shadowMap->Type != ShaderValueType::Sampler2D || shadowMap->TextureUnit < 0 ||
                static_cast<std::uint32_t>(shadowMap->TextureUnit) >= maxTextureUnits_)
            {
                continue;
            }

            const std::size_t unit = static_cast<std::size_t>(shadowMap->TextureUnit);
            const Texture *shadowTexture = &shadowFramebuffers_[cascade].GetDepthAttachment();
            if (unit >= currentTextures_.size() || !textureUnitsBound_[unit] ||
                currentTextures_[unit] != shadowTexture)
            {
                shadowTexture->Bind(static_cast<std::uint32_t>(unit));
                if (unit < currentTextures_.size())
                {
                    currentTextures_[unit] = shadowTexture;
                    textureUnitsBound_[unit] = true;
                }
                ++stats_.TextureChanges;
            }
        }
    }

    void ApplyIblGlobals(Shader &shader)
    {
        const bool diffuseEnabled = environmentValid_ && settings_.IndirectDiffuseEnabled;
        const bool specularEnabled = environmentValid_ && settings_.IndirectSpecularEnabled;
        shader.Set("HakiIndirectDiffuseEnabled", diffuseEnabled ? 1 : 0);
        shader.Set("HakiIndirectSpecularEnabled", specularEnabled ? 1 : 0);
        shader.Set("HakiEnvironmentIntensity", currentEnvironmentIntensity_);
        shader.Set("HakiEnvironmentRotation", currentEnvironmentRotation_);
        shader.Set("HakiPrefilterMaxLod", static_cast<float>(PrefilterMipLevels - 1));
        const auto sanitizeColor = [](const glm::vec3 &color)
        {
            return glm::vec3(std::isfinite(color.r) ? std::max(color.r, 0.0f) : 0.0f,
                             std::isfinite(color.g) ? std::max(color.g, 0.0f) : 0.0f,
                             std::isfinite(color.b) ? std::max(color.b, 0.0f) : 0.0f);
        };
        shader.Set("HakiAmbientSkyColor", sanitizeColor(settings_.AmbientSkyColor));
        shader.Set("HakiAmbientGroundColor", sanitizeColor(settings_.AmbientGroundColor));
        shader.Set("HakiAmbientIntensity",
                   std::isfinite(settings_.AmbientIntensity) ? std::max(settings_.AmbientIntensity, 0.0f) : 0.0f);
        if (!environmentValid_)
        {
            return;
        }

        const auto bindGlobal = [this, &shader](std::string_view name, const Texture &texture,
                                                ShaderValueType expectedType)
        {
            const ShaderUniform *uniform = shader.FindUniform(name);
            if (!uniform || uniform->Type != expectedType || uniform->TextureUnit < 0 ||
                static_cast<std::uint32_t>(uniform->TextureUnit) >= maxTextureUnits_)
            {
                return;
            }
            const std::size_t unit = static_cast<std::size_t>(uniform->TextureUnit);
            if (unit >= currentTextures_.size() || !textureUnitsBound_[unit] ||
                currentTextures_[unit] != &texture)
            {
                texture.Bind(static_cast<std::uint32_t>(unit));
                if (unit < currentTextures_.size())
                {
                    currentTextures_[unit] = &texture;
                    textureUnitsBound_[unit] = true;
                }
                ++stats_.TextureChanges;
            }
        };
        bindGlobal("HakiIrradianceMap", irradianceCube_, ShaderValueType::SamplerCube);
        bindGlobal("HakiPrefilterMap", prefilterCube_, ShaderValueType::SamplerCube);
        bindGlobal("HakiBrdfLut", brdfLut_, ShaderValueType::Sampler2D);
    }

    void ApplySsaoGlobals(Shader &shader, bool receiveSsao)
    {
        bool enabled = receiveSsao && ssaoValid_;
        const ShaderUniform *map = shader.FindUniform("HakiSsaoMap");
        if (!map || map->Type != ShaderValueType::Sampler2D || map->TextureUnit < 0 ||
            static_cast<std::uint32_t>(map->TextureUnit) >= maxTextureUnits_)
        {
            enabled = false;
        }
        shader.Set("HakiSsaoEnabled", enabled ? 1 : 0);
        if (!enabled)
        {
            return;
        }

        const Texture *texture = &ssaoFramebuffer_.GetColorAttachment();
        const std::size_t unit = static_cast<std::size_t>(map->TextureUnit);
        if (unit >= currentTextures_.size() || !textureUnitsBound_[unit] || currentTextures_[unit] != texture)
        {
            texture->Bind(static_cast<std::uint32_t>(unit));
            if (unit < currentTextures_.size())
            {
                currentTextures_[unit] = texture;
                textureUnitsBound_[unit] = true;
            }
            ++stats_.TextureChanges;
        }
    }

    void ApplyTransmissionGlobals(Shader &shader)
    {
        const ShaderUniform *map = shader.FindUniform("HakiTransmissionSceneColor");
        bool valid = transmissionSceneValid_ && map && map->Type == ShaderValueType::Sampler2D &&
                     map->TextureUnit >= 0 && static_cast<std::uint32_t>(map->TextureUnit) < maxTextureUnits_;
        shader.Set("HakiTransmissionSceneValid", valid ? 1 : 0);
        shader.Set("HakiTransmissionMaxLod",
                   valid ? static_cast<float>(transmissionSceneColor_.GetMipLevels() - 1) : 0.0f);
        if (!valid)
        {
            return;
        }

        const std::size_t unit = static_cast<std::size_t>(map->TextureUnit);
        if (unit >= currentTextures_.size() || !textureUnitsBound_[unit] ||
            currentTextures_[unit] != &transmissionSceneColor_)
        {
            transmissionSceneColor_.Bind(static_cast<std::uint32_t>(unit));
            if (unit < currentTextures_.size())
            {
                currentTextures_[unit] = &transmissionSceneColor_;
                textureUnitsBound_[unit] = true;
            }
            ++stats_.TextureChanges;
        }
    }

    void DrawSkybox()
    {
        if (!environmentValid_ || !settings_.SkyboxEnabled)
        {
            return;
        }
        const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean blend = glIsEnabled(GL_BLEND);
        const GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
        GLboolean depthWrite = GL_TRUE;
        GLint depthFunction = GL_LESS;
        GLint cullMode = GL_BACK;
        std::array<GLint, 2> polygonMode{};
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
        glGetIntegerv(GL_POLYGON_MODE, polygonMode.data());

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        skyboxShader_.Bind();
        // An orthographic projection would squash the sky cube into one flat colour; give the sky its own
        // perspective with the same aspect ratio instead.
        glm::mat4 skyProjection = currentProjection_;
        if (currentProjection_[3][3] == 1.0f && currentProjection_[0][0] != 0.0f)
        {
            const float aspect = currentProjection_[1][1] / currentProjection_[0][0];
            skyProjection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 10.0f);
        }
        skyboxShader_.Set("HakiSkyProjection", skyProjection);
        skyboxShader_.Set("HakiSkyView", glm::mat4(glm::mat3(currentView_)));
        skyboxShader_.Set("HakiEnvironmentIntensity", currentEnvironmentIntensity_);
        skyboxShader_.Set("HakiEnvironmentRotation", currentEnvironmentRotation_);
        if (const ShaderUniform *map = skyboxShader_.FindUniform("HakiEnvironmentMap");
            map && map->TextureUnit >= 0 && static_cast<std::uint32_t>(map->TextureUnit) < maxTextureUnits_)
        {
            environmentCube_.Bind(static_cast<std::uint32_t>(map->TextureUnit));
        }
        cubeMesh_.Bind();
        if (cubeMesh_.Draw())
        {
            ++stats_.SkyboxDrawCalls;
        }
        depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        glDepthFunc(static_cast<GLenum>(depthFunction));
        glDepthMask(depthWrite);
        glCullFace(static_cast<GLenum>(cullMode));
        glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygonMode[0]));
        ResetStateCache();
    }

    void Draw(const PreparedItem &item, bool receiveSsao, bool transmission = false)
    {
        const ShaderHandle shaderHandle = item.MaterialResource->GetShader();
        if (currentShader_ != shaderHandle)
        {
            item.ShaderResource->Bind();
            ApplyShadowGlobals(*item.ShaderResource);
            ApplyIblGlobals(*item.ShaderResource);
            currentShader_ = shaderHandle;
            currentMaterial_ = {};
            globalsValid_ = false;
            ++stats_.ShaderChanges;
        }
        // SSAO and transmission inputs only change between passes, not between draws.
        if (!globalsValid_ || globalsReceiveSsao_ != receiveSsao ||
            globalsTransmissionValid_ != transmissionSceneValid_)
        {
            ApplySsaoGlobals(*item.ShaderResource, receiveSsao);
            ApplyTransmissionGlobals(*item.ShaderResource);
            globalsValid_ = true;
            globalsReceiveSsao_ = receiveSsao;
            globalsTransmissionValid_ = transmissionSceneValid_;
        }
        if (currentMaterial_ != item.Item.Material)
        {
            ApplyState(item.MaterialResource->GetState());
            ApplyMaterial(item);
            currentMaterial_ = item.Item.Material;
            ++stats_.MaterialChanges;
        }

        BindObjectSlot(item.ObjectSlot);
        ApplySkinning(*item.ShaderResource, item.Item);

        if (currentMesh_ != item.Item.Mesh)
        {
            item.MeshResource->Bind();
            currentMesh_ = item.Item.Mesh;
            ++stats_.MeshChanges;
        }
        if (item.MeshResource->Draw(item.Item.Submesh))
        {
            ++stats_.DrawCalls;
            if (transmission)
            {
                ++stats_.TransmissionDrawCalls;
            }
        }
    }

    RendererConfig config_;
    RendererSettings settings_;
    RendererStats stats_;
    RendererGpuTimings gpuTimings_;
    DebugDraw debugDraw_;
    Canvas canvas_;
    Shader canvasShader_;
    GLuint canvasVertexArray_ = 0;
    GLuint canvasVertexBuffer_ = 0;
    GLuint canvasIndexBuffer_ = 0;
    std::vector<CanvasVertex> canvasVertices_;
    std::vector<std::uint32_t> canvasIndices_;
    std::vector<CanvasBatch> canvasBatches_;
    Shader debugShader_;
    GLuint debugVertexArray_ = 0;
    GLuint debugVertexBuffer_ = 0;
    std::size_t debugVertexCapacity_ = 0;

    std::array<TimerFrame, TimerFrameCount> timerFrames_{};
    std::size_t timerFrameIndex_ = 0;
    bool timerFrameOpen_ = false;
    bool timerQueriesCreated_ = false;
    Buffer cameraBuffer_;
    Buffer lightBuffer_;
    Buffer objectBuffer_;
    Buffer skinBuffer_;
    Texture fallbackTexture_;
    Texture environmentCube_;
    Texture irradianceCube_;
    Texture prefilterCube_;
    Texture brdfLut_;
    Texture ssaoNoiseTexture_;
    Texture transmissionSceneColor_;
    Framebuffer hdrFramebuffer_;
    Framebuffer ssaoPrepassFramebuffer_;
    Framebuffer ssaoFramebuffer_;
    Framebuffer ssaoBlurFramebuffer_;
    std::array<Framebuffer, MaxBloomPyramidLevels> bloomDownFramebuffers_;
    std::array<Framebuffer, MaxBloomPyramidLevels> bloomUpFramebuffers_;
    Framebuffer ldrFramebuffer_;
    Shader postProcessShader_;
    Shader fxaaShader_;
    Shader bloomDownsampleShader_;
    Shader bloomUpsampleShader_;
    Shader ssaoPrepassShader_;
    Shader ssaoShader_;
    Shader ssaoBlurShader_;
    Mesh postProcessMesh_;
    Mesh cubeMesh_;
    Shader equirectangularShader_;
    Shader irradianceShader_;
    Shader prefilterShader_;
    Shader brdfShader_;
    Shader skyboxShader_;
    std::array<Framebuffer, ShadowCascadeCount> shadowFramebuffers_;
    Shader shadowShader_;
    std::vector<RenderItem> submissions_;
    glm::mat4 currentView_{1.0f};
    glm::vec3 currentCameraPosition_{0.0f};
    glm::mat4 currentProjection_{1.0f};
    glm::mat4 currentInverseProjection_{1.0f};
    glm::mat4 currentInverseViewProjection_{1.0f};
    std::array<glm::mat4, ShadowCascadeCount> shadowViewProjections_{};
    std::array<float, ShadowCascadeCount> cascadeSplits_{};
    std::array<float, ShadowCascadeCount> cascadeWorldUnitsPerTexel_{};
    glm::vec3 shadowLightDirection_{0.0f, -1.0f, 0.0f};
    ShaderHandle currentShader_;
    MaterialHandle currentMaterial_;
    MeshHandle currentMesh_;
    std::array<const Texture *, 32> currentTextures_{};
    std::array<bool, 32> textureUnitsBound_{};
    std::array<GLint, 4> outputViewport_{};
    MaterialState currentState_;
    GLint outputDrawFramebuffer_ = 0;
    GLint outputReadFramebuffer_ = 0;
    GLuint captureFramebuffer_ = 0;
    GLuint captureDepthBuffer_ = 0;
    std::uint32_t maxTextureUnits_ = 0;
    TextureHandle currentEnvironmentHandle_;
    float currentEnvironmentIntensity_ = 1.0f;
    float currentEnvironmentRotation_ = 0.0f;
    float currentCameraNear_ = 0.1f;
    float currentCameraFar_ = 1000.0f;
    float bloomSampleScale_ = 1.0f;
    std::size_t bloomLevelCount_ = 0;
    bool initialized_ = false;
    bool frameActive_ = false;
    bool stateValid_ = false;
    bool globalsValid_ = false;
    bool globalsReceiveSsao_ = false;
    bool globalsTransmissionValid_ = false;
    std::uint32_t shadowResolution_ = DefaultShadowMapResolution;
    bool depthPrepassValid_ = false;
    bool forceDepthWriteOff_ = false;
    bool forceWireframe_ = false; // View > Wireframe: forward-pass geometry only
    // Per-frame object transforms, uploaded once and bound per draw with glBindBufferRange.
    GLuint objectRingBuffer_ = 0;
    std::size_t objectRingCapacity_ = 0;
    std::size_t objectSlotStride_ = sizeof(GPUObjectData);
    std::vector<std::byte> objectStaging_;
    std::uint32_t objectSlotCount_ = 0;
    bool hasShadowLight_ = false;
    bool shadowValid_ = false;
    bool ssaoValid_ = false;
    bool transmissionSceneValid_ = false;
    bool bloomValid_ = false;
    bool environmentValid_ = false;
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer &&) noexcept = default;
Renderer &Renderer::operator=(Renderer &&) noexcept = default;

bool Renderer::Initialize(const RendererConfig &config, std::string *diagnostic)
{
    return impl_->Initialize(config, diagnostic);
}

void Renderer::Shutdown()
{
    impl_->Shutdown();
}

bool Renderer::IsInitialized() const
{
    return impl_->IsInitialized();
}

bool Renderer::BeginFrame(const RenderFrameData &frame)
{
    return impl_->BeginFrame(frame);
}

void Renderer::Submit(const RenderItem &item)
{
    impl_->Submit(item);
}

void Renderer::Render(RenderPath path)
{
    if (path == RenderPath::Forward)
    {
        impl_->ExecuteForwardPass();
    }
}

void Renderer::EndFrame()
{
    impl_->EndFrame();
}

RendererSettings &Renderer::GetSettings()
{
    return impl_->GetSettings();
}

const RendererSettings &Renderer::GetSettings() const
{
    return impl_->GetSettings();
}

const RendererStats &Renderer::GetStats() const
{
    return impl_->GetStats();
}

const RendererGpuTimings &Renderer::GetGpuTimings() const
{
    return impl_->GetGpuTimings();
}

DebugDraw &Renderer::GetDebugDraw()
{
    return impl_->GetDebugDraw();
}

Canvas &Renderer::GetCanvas()
{
    return impl_->GetCanvas();
}

} // namespace Haki
