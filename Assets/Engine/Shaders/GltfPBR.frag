#version 330 core

const float PI = 3.14159265359;

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
in vec3 vWorldTangent;
in vec3 vWorldBitangent;
in vec2 vTexCoord;
in vec2 vTexCoord1;

// Per-texture UV set (0 or 1) and KHR_texture_transform matrix; the renderer resets them to 0 / identity.
uniform int BaseColorTextureTexCoord;
uniform mat3 BaseColorTextureUvTransform;
uniform int MetallicRoughnessTextureTexCoord;
uniform mat3 MetallicRoughnessTextureUvTransform;
uniform int NormalTextureTexCoord;
uniform mat3 NormalTextureUvTransform;
uniform int EmissiveTextureTexCoord;
uniform mat3 EmissiveTextureUvTransform;
uniform int OcclusionTextureTexCoord;
uniform mat3 OcclusionTextureUvTransform;
uniform int TransmissionTextureTexCoord;
uniform mat3 TransmissionTextureUvTransform;
uniform int ThicknessTextureTexCoord;
uniform mat3 ThicknessTextureUvTransform;
uniform vec3 DielectricSpecular;
in vec4 vColor;
flat in float vModelScale;

uniform vec4 BaseColorFactor;
uniform sampler2D BaseColorTexture;
uniform int UseBaseColorTexture;
uniform int PbrWorkflow;
uniform vec3 SpecularFactor;
uniform float GlossinessFactor;
uniform float MetallicFactor;
uniform float RoughnessFactor;
uniform sampler2D MetallicRoughnessTexture;
uniform int UseMetallicRoughnessTexture;
uniform sampler2D NormalTexture;
uniform int UseNormalTexture;
uniform float NormalScale;
uniform vec3 EmissiveFactor;
uniform sampler2D EmissiveTexture;
uniform int UseEmissiveTexture;
uniform sampler2D OcclusionTexture;
uniform int UseOcclusionTexture;
uniform float OcclusionStrength;
uniform int AlphaMode;
uniform float AlphaCutoff;
uniform float TransmissionFactor;
uniform sampler2D TransmissionTexture;
uniform int UseTransmissionTexture;
uniform float ThicknessFactor;
uniform sampler2D ThicknessTexture;
uniform int UseThicknessTexture;
uniform vec3 AttenuationColor;
uniform float AttenuationDistance;
uniform float Ior;
uniform sampler2D HakiTransmissionSceneColor;
uniform int HakiTransmissionSceneValid;
uniform float HakiTransmissionMaxLod;
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
uniform samplerCube HakiIrradianceMap;
uniform samplerCube HakiPrefilterMap;
uniform sampler2D HakiBrdfLut;
uniform int HakiIndirectDiffuseEnabled;
uniform int HakiIndirectSpecularEnabled;
uniform float HakiEnvironmentIntensity;
uniform float HakiEnvironmentRotation;
uniform float HakiPrefilterMaxLod;
uniform vec3 HakiAmbientSkyColor;
uniform vec3 HakiAmbientGroundColor;
uniform float HakiAmbientIntensity;

out vec4 FragColor;

float DistributionGGX(vec3 normal, vec3 halfway, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.0001);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(vec3 normal, vec3 viewDirection, vec3 lightDirection, float roughness)
{
    return GeometrySchlickGGX(max(dot(normal, viewDirection), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
}

vec3 FresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosine, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
           pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 HakiRotateEnvironment(vec3 direction)
{
    float cosine = cos(HakiEnvironmentRotation);
    float sine = sin(HakiEnvironmentRotation);
    return vec3(cosine * direction.x - sine * direction.z, direction.y,
                sine * direction.x + cosine * direction.z);
}

vec2 HakiUv(int set, mat3 transform)
{
    return (transform * vec3(set == 1 ? vTexCoord1 : vTexCoord, 1.0)).xy;
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

float HakiSpecularOcclusion(float nDotV, float ambientOcclusion, float roughness)
{
    return clamp(pow(nDotV + ambientOcclusion, exp2(-16.0 * roughness - 1.0)) -
                 1.0 + ambientOcclusion, 0.0, 1.0);
}

// Hemisphere ambient used in place of IBL when an indirect term is disabled or no environment is loaded.
vec3 HakiHemisphereAmbient(vec3 direction)
{
    return mix(HakiAmbientGroundColor, HakiAmbientSkyColor, clamp(direction.y * 0.5 + 0.5, 0.0, 1.0)) *
           HakiAmbientIntensity;
}

// Karis' analytic approximation of the split-sum environment BRDF (no LUT required).
vec3 EnvBrdfApprox(vec3 f0, float roughness, float nDotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
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

vec3 EvaluateLight(vec3 radiance, vec3 lightDirection, vec3 normal, vec3 viewDirection,
                    vec3 diffuseColor, vec3 f0, float metallic, float roughness, float transmission)
{
    vec3 halfway = normalize(viewDirection + lightDirection);
    vec3 fresnel = FresnelSchlick(max(dot(halfway, viewDirection), 0.0), f0);
    float distribution = DistributionGGX(normal, halfway, roughness);
    float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    vec3 specular = distribution * geometry * fresnel /
                    max(4.0 * max(dot(normal, viewDirection), 0.0) * max(dot(normal, lightDirection), 0.0), 0.0001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic) * (1.0 - transmission);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    return (diffuseWeight * diffuseColor / PI + specular) * radiance * nDotL;
}

void main()
{
    vec4 surface = BaseColorFactor * vColor;
    if (UseBaseColorTexture != 0)
    {
        surface *= texture(BaseColorTexture, HakiUv(BaseColorTextureTexCoord, BaseColorTextureUvTransform));
    }
    if (AlphaMode == 1 && surface.a < AlphaCutoff)
    {
        discard;
    }

    float metallic = MetallicFactor;
    float roughness = RoughnessFactor;
    vec3 specularFactor = SpecularFactor;
    float glossiness = GlossinessFactor;
    if (UseMetallicRoughnessTexture != 0)
    {
        vec4 packedSample = texture(MetallicRoughnessTexture, HakiUv(MetallicRoughnessTextureTexCoord, MetallicRoughnessTextureUvTransform));
        if (PbrWorkflow == 1)
        {
            specularFactor *= packedSample.rgb;
            glossiness *= packedSample.a;
        }
        else
        {
            roughness *= packedSample.g;
            metallic *= packedSample.b;
        }
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    float transmission = TransmissionFactor;
    if (UseTransmissionTexture != 0)
    {
        transmission *= texture(TransmissionTexture, HakiUv(TransmissionTextureTexCoord, TransmissionTextureUvTransform)).r;
    }
    transmission = clamp(transmission, 0.0, 1.0);
    float thickness = ThicknessFactor;
    if (UseThicknessTexture != 0)
    {
        thickness *= texture(ThicknessTexture, HakiUv(ThicknessTextureTexCoord, ThicknessTextureUvTransform)).g;
    }
    thickness = max(thickness, 0.0) * max(vModelScale, 0.0);

    float materialIor = Ior == 0.0 ? 1000.0 : max(Ior, 1.0);
    float dielectricF0 = pow((materialIor - 1.0) / (materialIor + 1.0), 2.0);
    vec3 diffuseColor = surface.rgb;
    vec3 f0;
    if (PbrWorkflow == 1)
    {
        specularFactor = clamp(specularFactor, vec3(0.0), vec3(1.0));
        glossiness = clamp(glossiness, 0.0, 1.0);
        diffuseColor *= 1.0 - max(max(specularFactor.r, specularFactor.g), specularFactor.b);
        f0 = specularFactor;
        roughness = clamp(1.0 - glossiness, 0.04, 1.0);
        metallic = 0.0;
    }
    else
    {
        // KHR_materials_specular approximation: tint/scale the dielectric reflectance.
        f0 = mix(min(vec3(dielectricF0) * DielectricSpecular, vec3(1.0)), surface.rgb, metallic);
    }

    vec3 normal = normalize(vWorldNormal);
    if (UseNormalTexture != 0)
    {
        vec3 tangentNormal = texture(NormalTexture, HakiUv(NormalTextureTexCoord, NormalTextureUvTransform)).xyz * 2.0 - 1.0;
        tangentNormal.xy *= NormalScale;
        normal = normalize(mat3(normalize(vWorldTangent), normalize(vWorldBitangent), normal) * tangentNormal);
    }

    vec3 viewDirection = normalize(CameraWorldPosition.xyz - vWorldPosition);
    vec3 lighting = vec3(0.0);
    for (int index = 0; index < LightCounts.x; ++index)
    {
        vec3 direction = normalize(-DirectionalLights[index].Direction.xyz);
        vec3 radiance = DirectionalLights[index].ColorIntensity.rgb * DirectionalLights[index].ColorIntensity.a;
        // Back-facing surfaces receive no direct light, so their shadow lookup would be wasted.
        float visibility = index == 0 && dot(normal, direction) > 0.0
                               ? HakiDirectionalShadowVisibility(vWorldPosition, normal, direction)
                               : 1.0;
        lighting += EvaluateLight(radiance, direction, normal, viewDirection, diffuseColor, f0, metallic, roughness,
                                  transmission) *
                     visibility;
    }
    for (int index = 0; index < LightCounts.y; ++index)
    {
        vec3 offset = PointLights[index].PositionRange.xyz - vWorldPosition;
        float distanceToLight = length(offset);
        float range = max(PointLights[index].PositionRange.w, 0.0001);
        float attenuation = pow(max(1.0 - distanceToLight / range, 0.0), 2.0);
        vec3 radiance = PointLights[index].ColorIntensity.rgb * PointLights[index].ColorIntensity.a * attenuation;
        lighting += EvaluateLight(radiance, normalize(offset), normal, viewDirection, diffuseColor, f0, metallic,
                                  roughness, transmission);
    }
    for (int index = 0; index < LightCounts.z; ++index)
    {
        vec3 offset = SpotLights[index].PositionRange.xyz - vWorldPosition;
        float distanceToLight = length(offset);
        vec3 direction = normalize(offset);
        float range = max(SpotLights[index].PositionRange.w, 0.0001);
        float rangeAttenuation = pow(max(1.0 - distanceToLight / range, 0.0), 2.0);
        float cone = dot(-direction, normalize(SpotLights[index].DirectionInner.xyz));
        float coneAttenuation = smoothstep(SpotLights[index].Outer.x, SpotLights[index].DirectionInner.w, cone);
        vec3 radiance = SpotLights[index].ColorIntensity.rgb * SpotLights[index].ColorIntensity.a *
                        rangeAttenuation * coneAttenuation;
        lighting += EvaluateLight(radiance, direction, normal, viewDirection, diffuseColor, f0, metallic, roughness,
                                  transmission);
    }

    float occlusion = 1.0;
    if (UseOcclusionTexture != 0)
    {
        occlusion = mix(1.0, texture(OcclusionTexture, HakiUv(OcclusionTextureTexCoord, OcclusionTextureUvTransform)).r, OcclusionStrength);
    }
    vec3 emissive = EmissiveFactor;
    if (UseEmissiveTexture != 0)
    {
        emissive *= texture(EmissiveTexture, HakiUv(EmissiveTextureTexCoord, EmissiveTextureUvTransform)).rgb;
    }
    float nDotV = max(dot(normal, viewDirection), 0.0);
    vec3 fresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    float ambientOcclusion = occlusion * HakiSsaoVisibility();
    vec3 iblIndirect = vec3(0.0);
    vec3 fallbackIndirect = vec3(0.0);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic) * (1.0 - transmission) *
                         diffuseColor * ambientOcclusion;
    if (HakiIndirectDiffuseEnabled != 0)
    {
        vec3 irradiance = texture(HakiIrradianceMap, HakiRotateEnvironment(normal)).rgb;
        iblIndirect += diffuseWeight * irradiance;
    }
    else
    {
        fallbackIndirect += diffuseWeight * HakiHemisphereAmbient(normal);
    }
    vec3 reflection = reflect(-viewDirection, normal);
    float specularOcclusion = HakiSpecularOcclusion(nDotV, ambientOcclusion, roughness);
    if (HakiIndirectSpecularEnabled != 0)
    {
        vec3 prefiltered = textureLod(HakiPrefilterMap, HakiRotateEnvironment(reflection),
                                     roughness * HakiPrefilterMaxLod).rgb;
        vec2 brdf = texture(HakiBrdfLut, vec2(nDotV, roughness)).rg;
        iblIndirect += prefiltered * (fresnel * brdf.x + brdf.y) * specularOcclusion;
    }
    else
    {
        fallbackIndirect += HakiHemisphereAmbient(reflection) * EnvBrdfApprox(f0, roughness, nDotV) *
                            specularOcclusion;
    }
    vec3 indirect = iblIndirect * HakiEnvironmentIntensity + fallbackIndirect;
    vec3 transmitted = vec3(0.0);
    float dielectricTransmission = transmission * (1.0 - metallic);
    if (dielectricTransmission > 0.0 && HakiTransmissionSceneValid != 0)
    {
        vec3 refractedDirection = refract(-viewDirection, normal, 1.0 / materialIor);
        if (dot(refractedDirection, refractedDirection) < 0.0001)
        {
            refractedDirection = -viewDirection;
        }
        vec3 exitPosition = vWorldPosition + refractedDirection * thickness;
        vec4 exitClip = ViewProjection * vec4(exitPosition, 1.0);
        vec2 sceneUv = exitClip.xy / max(abs(exitClip.w), 0.0001) * 0.5 + 0.5;
        bool onScreen = exitClip.w > 0.0 && all(greaterThanEqual(sceneUv, vec2(0.0))) &&
                        all(lessThanEqual(sceneUv, vec2(1.0)));
        if (onScreen || HakiIndirectSpecularEnabled == 0)
        {
            transmitted = textureLod(HakiTransmissionSceneColor, clamp(sceneUv, vec2(0.0), vec2(1.0)),
                                     roughness * HakiTransmissionMaxLod).rgb;
        }
        else
        {
            transmitted = textureLod(HakiPrefilterMap, HakiRotateEnvironment(refractedDirection),
                                     roughness * HakiPrefilterMaxLod).rgb * HakiEnvironmentIntensity;
        }
        if (AttenuationDistance > 0.0 && thickness > 0.0)
        {
            transmitted *= pow(max(AttenuationColor, vec3(0.0001)),
                               vec3(thickness / AttenuationDistance));
        }
        transmitted *= surface.rgb * (vec3(1.0) - fresnel) * dielectricTransmission;
    }
    vec3 finalColor = HakiCascadeDebugTint(indirect + lighting + transmitted + emissive, vWorldPosition);
    FragColor = vec4(finalColor, surface.a);
}
