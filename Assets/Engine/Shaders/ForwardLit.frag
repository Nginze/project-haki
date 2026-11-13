#version 330 core

struct DirectionalLight
{
    vec4 Direction;
    vec4 ColorIntensity;
};

struct PointLight
{
    vec4 PositionRange;
    vec4 ColorIntensity;
};

struct SpotLight
{
    vec4 PositionRange;
    vec4 DirectionInner;
    vec4 ColorIntensity;
    vec4 Outer;
};

layout(std140) uniform HakiCamera
{
    mat4 View;
    mat4 Projection;
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraWorldPosition;
    vec4 Viewport;
};

layout(std140) uniform HakiLights
{
    ivec4 LightCounts;
    DirectionalLight DirectionalLights[4];
    PointLight PointLights[64];
    SpotLight SpotLights[16];
};

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord;

uniform vec4 BaseColor;
uniform sampler2D BaseColorTexture;
uniform int UseBaseColorTexture;
uniform sampler2D HakiSsaoMap;
uniform int HakiSsaoEnabled;
uniform sampler2D HakiShadowMap0;
uniform sampler2D HakiShadowMap1;
uniform sampler2D HakiShadowMap2;
uniform sampler2D HakiShadowMap3;
uniform mat4 HakiLightViewProjection0;
uniform mat4 HakiLightViewProjection1;
uniform mat4 HakiLightViewProjection2;
uniform mat4 HakiLightViewProjection3;
uniform vec4 HakiCascadeSplits;
uniform vec4 HakiCascadeTexelWorldSize;
uniform float HakiCameraNear;
uniform float HakiCascadeBlendFraction;
uniform int HakiShadowEnabled;
uniform float HakiShadowBias;
uniform int HakiShadowFilterMode;
uniform int HakiShowCascadeZones;

out vec4 FragColor;

vec3 EvaluateDirectional(DirectionalLight light, vec3 normal)
{
    vec3 toLight = normalize(-light.Direction.xyz);
    float diffuse = max(dot(normal, toLight), 0.0);
    return light.ColorIntensity.rgb * light.ColorIntensity.a * diffuse;
}

float HakiSsaoVisibility()
{
    if (HakiSsaoEnabled == 0)
    {
        return 1.0;
    }
    // The SSAO target may be lower resolution than the frame; map through the HDR viewport size.
    vec2 screenUv = gl_FragCoord.xy / max(Viewport.zw, vec2(1.0));
    return texture(HakiSsaoMap, screenUv).r;
}

const int HAKI_SHADOW_FILTER_PCSS = 1;
const int HAKI_PCF_SAMPLES = 12;
const int HAKI_PCSS_BLOCKER_SAMPLES = 12;
const int HAKI_PCSS_FILTER_SAMPLES = 24;
const float HAKI_PCSS_LIGHT_RADIUS_WORLD = 0.5;
const float HAKI_PCSS_MAX_FILTER_RADIUS_UV = 0.04;

float HakiShadowRandom(vec2 coordinates)
{
    return fract(sin(dot(coordinates, vec2(12.9898, 78.233))) * 43758.5453);
}

vec2 HakiShadowDiskSample(int index, int sampleCount, float rotation)
{
    float radius = sqrt((float(index) + 0.5) / float(sampleCount));
    float angle = float(index) * 2.39996323 + rotation;
    return vec2(cos(angle), sin(angle)) * radius;
}

bool HakiShadowCoordinatesValid(vec2 coordinates)
{
    return coordinates.x >= 0.0 && coordinates.x <= 1.0 &&
           coordinates.y >= 0.0 && coordinates.y <= 1.0;
}

float HakiShadowDepth(int cascade, vec2 coordinates)
{
    if (cascade == 0)
    {
        return texture(HakiShadowMap0, coordinates).r;
    }
    if (cascade == 1)
    {
        return texture(HakiShadowMap1, coordinates).r;
    }
    if (cascade == 2)
    {
        return texture(HakiShadowMap2, coordinates).r;
    }
    return texture(HakiShadowMap3, coordinates).r;
}

vec4 HakiShadowClipPosition(int cascade, vec3 worldPosition)
{
    if (cascade == 0)
    {
        return HakiLightViewProjection0 * vec4(worldPosition, 1.0);
    }
    if (cascade == 1)
    {
        return HakiLightViewProjection1 * vec4(worldPosition, 1.0);
    }
    if (cascade == 2)
    {
        return HakiLightViewProjection2 * vec4(worldPosition, 1.0);
    }
    return HakiLightViewProjection3 * vec4(worldPosition, 1.0);
}

float HakiCascadeSplit(int cascade)
{
    return cascade == 0 ? HakiCascadeSplits.x
         : cascade == 1 ? HakiCascadeSplits.y
         : cascade == 2 ? HakiCascadeSplits.z
                        : HakiCascadeSplits.w;
}

float HakiCascadeTexelSize(int cascade)
{
    return cascade == 0 ? HakiCascadeTexelWorldSize.x
         : cascade == 1 ? HakiCascadeTexelWorldSize.y
         : cascade == 2 ? HakiCascadeTexelWorldSize.z
                        : HakiCascadeTexelWorldSize.w;
}

int HakiSelectCascade(float viewDepth)
{
    if (viewDepth <= HakiCascadeSplits.x) return 0;
    if (viewDepth <= HakiCascadeSplits.y) return 1;
    if (viewDepth <= HakiCascadeSplits.z) return 2;
    if (viewDepth <= HakiCascadeSplits.w) return 3;
    return -1;
}

float HakiShadowCompare(int cascade, vec2 coordinates, float receiverDepth)
{
    if (!HakiShadowCoordinatesValid(coordinates))
    {
        return 1.0;
    }
    return receiverDepth <= HakiShadowDepth(cascade, coordinates) ? 1.0 : 0.0;
}

float HakiPcfVisibility(int cascade, vec3 projected, float bias, vec2 texelSize)
{
    // 12 taps on a per-pixel rotated Vogel disk (radius 1.5 texels); the rotation trades banding for
    // fine noise that FXAA smooths, at half the cost of the old 5x5 grid.
    float receiverDepth = projected.z - bias;
    float rotation = HakiShadowRandom(gl_FragCoord.xy) * 6.28318530718;
    float visibility = 0.0;
    for (int index = 0; index < HAKI_PCF_SAMPLES; ++index)
    {
        vec2 offset = HakiShadowDiskSample(index, HAKI_PCF_SAMPLES, rotation) * texelSize * 1.5;
        visibility += HakiShadowCompare(cascade, projected.xy + offset, receiverDepth);
    }
    return visibility / float(HAKI_PCF_SAMPLES);
}

float HakiPcssVisibility(int cascade, vec3 projected, float bias, vec2 texelSize)
{
    float receiverDepth = projected.z - bias;
    float rotation = HakiShadowRandom(projected.xy) * 6.28318530718;
    float lightRadiusUv = HAKI_PCSS_LIGHT_RADIUS_WORLD /
                          max(HakiCascadeTexelSize(cascade) * float(textureSize(HakiShadowMap0, 0).x), 0.0001);
    float searchRadius = lightRadiusUv * clamp(projected.z, 0.0, 1.0);
    float blockerDepthSum = 0.0;
    int blockerCount = 0;

    for (int index = 0; index < HAKI_PCSS_BLOCKER_SAMPLES; ++index)
    {
        vec2 coordinates = projected.xy +
                           HakiShadowDiskSample(index, HAKI_PCSS_BLOCKER_SAMPLES, rotation) * searchRadius;
        if (!HakiShadowCoordinatesValid(coordinates))
        {
            continue;
        }
        float storedDepth = HakiShadowDepth(cascade, coordinates);
        if (storedDepth < receiverDepth)
        {
            blockerDepthSum += storedDepth;
            ++blockerCount;
        }
    }

    if (blockerCount == 0)
    {
        return 1.0;
    }

    float averageBlockerDepth = blockerDepthSum / float(blockerCount);
    float penumbraRadius = (receiverDepth - averageBlockerDepth) /
                           max(averageBlockerDepth, 0.0001) * lightRadiusUv;
    float minimumRadius = max(texelSize.x, texelSize.y) * 1.5;
    float filterRadius = clamp(penumbraRadius, minimumRadius, HAKI_PCSS_MAX_FILTER_RADIUS_UV);
    float visibility = 0.0;
    for (int index = 0; index < HAKI_PCSS_FILTER_SAMPLES; ++index)
    {
        vec2 coordinates = projected.xy +
                           HakiShadowDiskSample(index, HAKI_PCSS_FILTER_SAMPLES, rotation) * filterRadius;
        visibility += HakiShadowCompare(cascade, coordinates, receiverDepth);
    }
    return visibility / float(HAKI_PCSS_FILTER_SAMPLES);
}

float HakiCascadeShadowVisibility(int cascade, vec3 worldPosition, vec3 normal, vec3 toLight)
{
    vec4 lightClip = HakiShadowClipPosition(cascade, worldPosition);
    if (lightClip.w <= 0.0)
    {
        return 1.0;
    }
    vec3 projected = lightClip.xyz / lightClip.w;
    projected = projected * 0.5 + 0.5;
    if (projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0)
    {
        return 1.0;
    }

    float normalAlignment = max(dot(normal, toLight), 0.0);
    float bias = max(HakiShadowBias * (1.0 - normalAlignment), HakiShadowBias * 0.25);
    vec2 texelSize = 1.0 / vec2(textureSize(HakiShadowMap0, 0));
    if (HakiShadowFilterMode == HAKI_SHADOW_FILTER_PCSS)
    {
        return HakiPcssVisibility(cascade, projected, bias, texelSize);
    }
    return HakiPcfVisibility(cascade, projected, bias, texelSize);
}

float HakiDirectionalShadowVisibility(vec3 worldPosition, vec3 normal, vec3 toLight)
{
    if (HakiShadowEnabled == 0)
    {
        return 1.0;
    }

    float viewDepth = -(View * vec4(worldPosition, 1.0)).z;
    int cascade = HakiSelectCascade(viewDepth);
    if (cascade < 0)
    {
        return 1.0;
    }

    float visibility = HakiCascadeShadowVisibility(cascade, worldPosition, normal, toLight);
    if (cascade < 3)
    {
        float cascadeNear = cascade == 0 ? HakiCameraNear : HakiCascadeSplit(cascade - 1);
        float cascadeFar = HakiCascadeSplit(cascade);
        float blendStart = mix(cascadeFar, cascadeNear, HakiCascadeBlendFraction);
        if (viewDepth > blendStart)
        {
            float nextVisibility = HakiCascadeShadowVisibility(cascade + 1, worldPosition, normal, toLight);
            visibility = mix(visibility, nextVisibility,
                             smoothstep(blendStart, cascadeFar, viewDepth));
        }
    }
    return visibility;
}

vec3 HakiCascadeDebugTint(vec3 color, vec3 worldPosition)
{
    if (HakiShowCascadeZones == 0 || HakiShadowEnabled == 0)
    {
        return color;
    }
    int cascade = HakiSelectCascade(-(View * vec4(worldPosition, 1.0)).z);
    if (cascade < 0)
    {
        return color;
    }
    vec3 zoneColor = cascade == 0 ? vec3(1.0, 0.15, 0.15)
                   : cascade == 1 ? vec3(0.15, 1.0, 0.15)
                   : cascade == 2 ? vec3(0.15, 0.35, 1.0)
                                  : vec3(1.0, 0.9, 0.15);
    return mix(color, color * 0.5 + zoneColor * 0.5, 0.45);
}

vec3 EvaluatePoint(PointLight light, vec3 normal)
{
    vec3 offset = light.PositionRange.xyz - vWorldPosition;
    float distanceToLight = length(offset);
    float range = max(light.PositionRange.w, 0.0001);
    float attenuation = pow(max(1.0 - distanceToLight / range, 0.0), 2.0);
    float diffuse = max(dot(normal, normalize(offset)), 0.0);
    return light.ColorIntensity.rgb * light.ColorIntensity.a * diffuse * attenuation;
}

vec3 EvaluateSpot(SpotLight light, vec3 normal)
{
    vec3 offset = light.PositionRange.xyz - vWorldPosition;
    float distanceToLight = length(offset);
    vec3 toLight = normalize(offset);
    float range = max(light.PositionRange.w, 0.0001);
    float rangeAttenuation = pow(max(1.0 - distanceToLight / range, 0.0), 2.0);
    float cone = dot(-toLight, normalize(light.DirectionInner.xyz));
    float coneAttenuation = smoothstep(light.Outer.x, light.DirectionInner.w, cone);
    float diffuse = max(dot(normal, toLight), 0.0);
    return light.ColorIntensity.rgb * light.ColorIntensity.a * diffuse * rangeAttenuation * coneAttenuation;
}

void main()
{
    vec4 surface = BaseColor;
    if (UseBaseColorTexture != 0)
    {
        surface *= texture(BaseColorTexture, vTexCoord);
    }

    vec3 normal = normalize(vWorldNormal);
    vec3 lighting = vec3(0.20) * HakiSsaoVisibility();
    for (int index = 0; index < LightCounts.x; ++index)
    {
        vec3 toLight = normalize(-DirectionalLights[index].Direction.xyz);
        float visibility = index == 0 && dot(normal, toLight) > 0.0
                               ? HakiDirectionalShadowVisibility(vWorldPosition, normal, toLight)
                               : 1.0;
        lighting += EvaluateDirectional(DirectionalLights[index], normal) * visibility;
    }
    for (int index = 0; index < LightCounts.y; ++index)
    {
        lighting += EvaluatePoint(PointLights[index], normal);
    }
    for (int index = 0; index < LightCounts.z; ++index)
    {
        lighting += EvaluateSpot(SpotLights[index], normal);
    }
    vec3 finalColor = HakiCascadeDebugTint(surface.rgb * lighting, vWorldPosition);
    FragColor = vec4(finalColor, surface.a);
}
