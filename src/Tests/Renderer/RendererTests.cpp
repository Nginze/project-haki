#include "Haki/AssetLoader/ShaderFileLoader.h"
#include "Haki/AssetLoader/TextureFileLoader.h"
#include "Haki/AssetLoader/GltfMeshLoader.h"
#include "Haki/AssetLoader/MaterialFileLoader.h"
#include "Haki/AssetManager/AssetCatalog.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/GL/Framebuffer.h"
#include "Haki/GL/Mesh.h"
#include "Haki/GL/Shader.h"
#include "Haki/Renderer/Font.h"
#include "Haki/Renderer/Material.h"
#include "Haki/Renderer/Renderer.h"
#include "Haki/Renderer/ShadowCascades.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scene/Scene.h"
#include "Haki/Serialization/SceneSerializer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace
{

class TestRunner
{
public:
    explicit TestRunner(std::string_view name) { std::cout << "\n=== " << name << " ===\n"; }

    void Check(bool condition, std::string_view name)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
        failures_ += condition ? 0 : 1;
    }

    int Result() const { return failures_ == 0 ? 0 : 1; }

private:
    int failures_ = 0;
};

class HiddenContext
{
public:
    bool Initialize()
    {
#if defined(__linux__) && defined(GLFW_PLATFORM_X11)
        // GLFW's Wayland teardown can dispatch a stale fractional-scale event for a destroyed hidden window.
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
        if (!glfwInit())
        {
            return false;
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window_ = glfwCreateWindow(64, 64, "RendererTests", nullptr, nullptr);
        if (!window_)
        {
            glfwTerminate();
            return false;
        }
        glfwMakeContextCurrent(window_);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            glfwDestroyWindow(window_);
            window_ = nullptr;
            glfwTerminate();
            return false;
        }
        return true;
    }

    ~HiddenContext()
    {
        if (window_)
        {
            glfwDestroyWindow(window_);
        }
        glfwTerminate();
    }

private:
    GLFWwindow *window_ = nullptr;
};

class TriangleMeshLoader final : public Haki::AssetLoader<Haki::Mesh>
{
public:
    bool Supports(const Haki::AssetLoadRequest &) const override { return true; }

    Haki::AssetLoaderResult<Haki::Mesh> Load(const Haki::AssetLoadRequest &) override
    {
        struct Vertex
        {
            float Position[3];
            float Normal[3];
            float TexCoord[2];
            float Tangent[4]{1.0f, 0.0f, 0.0f, 1.0f};
            float Color[4]{1.0f, 1.0f, 1.0f, 1.0f};
            std::uint16_t Joints[4]{};
            float Weights[4]{1.0f, 0.0f, 0.0f, 0.0f};
        };
        const std::array<Vertex, 3> vertices{{
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
        }};

        Haki::MeshDescriptor descriptor;
        descriptor.Layout.Stride = sizeof(Vertex);
        descriptor.Layout.Attributes = {
            {0, 3, Haki::VertexComponentType::Float, false, false, offsetof(Vertex, Position)},
            {1, 3, Haki::VertexComponentType::Float, false, false, offsetof(Vertex, Normal)},
            {2, 2, Haki::VertexComponentType::Float, false, false, offsetof(Vertex, TexCoord)},
            {3, 4, Haki::VertexComponentType::Float, false, false, offsetof(Vertex, Tangent)},
            {4, 4, Haki::VertexComponentType::Float, false, false, offsetof(Vertex, Color)},
            {5, 4, Haki::VertexComponentType::UnsignedShort, false, true, offsetof(Vertex, Joints)},
            {6, 4, Haki::VertexComponentType::Float, false, false, offsetof(Vertex, Weights)},
        };
        descriptor.VertexData = std::as_bytes(std::span(vertices));
        descriptor.Bounds.Extents = {0.5f, 0.5f, 0.0f};

        auto mesh = std::make_unique<Haki::Mesh>();
        if (!mesh->Initialize(descriptor))
        {
            return {nullptr, Haki::AssetLoadError::LoaderFailed, "Triangle mesh creation failed"};
        }
        return {std::move(mesh), Haki::AssetLoadError::None, {}};
    }
};

class TestMaterialLoader final : public Haki::AssetLoader<Haki::Material>
{
public:
    explicit TestMaterialLoader(Haki::ShaderHandle shader) : shader_(shader) {}

    bool Supports(const Haki::AssetLoadRequest &) const override { return true; }

    Haki::AssetLoaderResult<Haki::Material> Load(const Haki::AssetLoadRequest &request) override
    {
        auto material = std::make_unique<Haki::Material>(shader_);
        material->SetParameter("BaseColor", glm::vec4(1.0f));
        material->SetParameter("UseBaseColorTexture", 0);
        if (request.Id == 4)
        {
            Haki::MaterialState state;
            state.Blend = Haki::BlendMode::AlphaBlend;
            material->SetState(state);
        }
        else if (request.Id == 5)
        {
            Haki::MaterialState state;
            state.Pass = Haki::MaterialPass::Transmission;
            material->SetState(state);
        }
        return {std::move(material), Haki::AssetLoadError::None, {}};
    }

private:
    Haki::ShaderHandle shader_;
};

int RunRendererTests()
{
    HiddenContext context;
    if (!context.Initialize())
    {
        std::cout << "OpenGL context unavailable; skipping renderer tests\n";
        return 77;
    }

    TestRunner tests("Renderer.Forward");
    struct IntegerVertex
    {
        std::uint16_t Joints[4];
    };
    const std::array<IntegerVertex, 1> integerVertices{{{{0, 1, 2, 3}}}};
    Haki::MeshDescriptor integerDescriptor;
    integerDescriptor.Layout.Stride = sizeof(IntegerVertex);
    integerDescriptor.Layout.Attributes = {
        {5, 4, Haki::VertexComponentType::UnsignedShort, false, true, offsetof(IntegerVertex, Joints)},
    };
    integerDescriptor.VertexData = std::as_bytes(std::span(integerVertices));
    Haki::Mesh integerMesh;
    tests.Check(integerMesh.Initialize(integerDescriptor), "integer vertex attribute mesh initializes");
    integerMesh.Bind();
    GLint integerAttribute = GL_FALSE;
    GLint integerAttributeType = 0;
    glGetVertexAttribiv(5, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integerAttribute);
    glGetVertexAttribiv(5, GL_VERTEX_ATTRIB_ARRAY_TYPE, &integerAttributeType);
    tests.Check(integerAttribute == GL_TRUE && integerAttributeType == GL_UNSIGNED_SHORT && glGetError() == GL_NO_ERROR,
                "integer vertex attribute uses glVertexAttribIPointer");

    const auto uniformSplits = Haki::CalculatePssmSplits(0.1f, 200.0f, 0.0f);
    const auto logarithmicSplits = Haki::CalculatePssmSplits(0.1f, 200.0f, 1.0f);
    const auto practicalSplits = Haki::CalculatePssmSplits(0.1f, 200.0f, 0.75f);
    tests.Check(practicalSplits[0] < practicalSplits[1] && practicalSplits[1] < practicalSplits[2] &&
                    practicalSplits[2] < practicalSplits[3] && practicalSplits[3] == 200.0f,
                "PSSM splits are ordered and end at the shadow distance");
    tests.Check(logarithmicSplits[0] < practicalSplits[0] && practicalSplits[0] < uniformSplits[0],
                "PSSM lambda blends logarithmic and uniform distributions");
    Haki::AssetManager::Initialize();
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    std::vector<Haki::SerializationError> catalogErrors;
    tests.Check(catalog->Load("src/Tests/Serialization/Fixtures/AssetCatalog.json", catalogErrors),
                "renderer fixture asset catalog loads");
    Haki::AssetManager::RegisterShaderLoader(std::make_unique<Haki::ShaderFileLoader>());
    Haki::AssetManager::RegisterTextureLoader(std::make_unique<Haki::TextureFileLoader>(false, true));
    Haki::AssetManager::RegisterMeshLoader(std::make_unique<Haki::GltfMeshLoader>(catalog));
    Haki::AssetManager::RegisterMeshLoader(std::make_unique<TriangleMeshLoader>());
    Haki::AssetManager::RegisterMaterialLoader(std::make_unique<Haki::MaterialFileLoader>(catalog));

    const Haki::AssetCatalogEntry *gltfEntry = catalog->Find("mesh.triangle");
    const auto gltfMesh = gltfEntry
                              ? Haki::AssetManager::LoadMesh({gltfEntry->RuntimeId, gltfEntry->Source})
                              : Haki::AssetResult<Haki::MeshHandle>{};
    tests.Check(static_cast<bool>(gltfMesh), "named glTF mesh imports through cgltf");
    tests.Check(gltfMesh && Haki::AssetManager::GetMesh(gltfMesh.Value)->GetSubmeshCount() == 1,
                "glTF primitive becomes one submesh");
    const Haki::AssetCatalogEntry *indexedEntry = catalog->Find("mesh.triangle-indexed");
    const auto indexedMesh = indexedEntry
                                 ? Haki::AssetManager::LoadMesh({indexedEntry->RuntimeId, indexedEntry->Source})
                                 : Haki::AssetResult<Haki::MeshHandle>{};
    tests.Check(static_cast<bool>(indexedMesh), "indexed glTF mesh selector imports through cgltf");
    if (gltfEntry && indexedEntry)
    {
        Haki::GltfMeshLoader cachedLoader(catalog);
        const auto firstSelection = cachedLoader.Load({gltfEntry->RuntimeId, gltfEntry->Source});
        const auto secondSelection = cachedLoader.Load({indexedEntry->RuntimeId, indexedEntry->Source});
        tests.Check(firstSelection && secondSelection && cachedLoader.GetParseCount() == 1,
                    "glTF loader parses one source once across mesh selectors");
    }
    const Haki::AssetCatalogEntry *skinnedEntry = catalog->Find("mesh.skinned-triangle");
    const auto skinnedMesh = skinnedEntry
                                 ? Haki::AssetManager::LoadMesh({skinnedEntry->RuntimeId, skinnedEntry->Source})
                                 : Haki::AssetResult<Haki::MeshHandle>{};
    const Haki::Mesh *skinnedResource = skinnedMesh ? Haki::AssetManager::GetMesh(skinnedMesh.Value) : nullptr;
    tests.Check(skinnedResource && glm::distance(skinnedResource->GetBounds().Center, glm::vec3(1.5f, 0.5f, 0.0f)) < 0.0001f,
                "skinned glTF mesh evaluates its default joint pose");

    const Haki::AssetCatalogEntry *materialEntry = catalog->Find("material.test");
    const auto fileMaterial = materialEntry
                                  ? Haki::AssetManager::LoadMaterial({materialEntry->RuntimeId, materialEntry->Source})
                                  : Haki::AssetResult<Haki::MaterialHandle>{};
    tests.Check(static_cast<bool>(fileMaterial), "material JSON resolves and loads its shader dependency");

    const Haki::AssetCatalogEntry *pbrMaterialEntry = catalog->Find("material.pbr-test");
    const auto pbrMaterial = pbrMaterialEntry
                                 ? Haki::AssetManager::LoadMaterial({pbrMaterialEntry->RuntimeId, pbrMaterialEntry->Source})
                                 : Haki::AssetResult<Haki::MaterialHandle>{};
    if (!pbrMaterial)
    {
        std::cout << "PBR material diagnostic: " << pbrMaterial.Message << '\n';
    }
    tests.Check(static_cast<bool>(pbrMaterial), "generated-style glTF PBR material compiles and loads");
    Haki::Shader *pbrShaderResource = nullptr;
    if (pbrMaterial)
    {
        const Haki::Material *materialResource = Haki::AssetManager::GetMaterial(pbrMaterial.Value);
        pbrShaderResource = materialResource ? Haki::AssetManager::GetShader(materialResource->GetShader()) : nullptr;
    }
    tests.Check(pbrShaderResource && pbrShaderResource->FindUniform("HakiShadowFilterMode"),
                "glTF PBR shader reflects the shadow filter mode");
    tests.Check(pbrShaderResource && pbrShaderResource->FindUniform("HakiShadowMap3") &&
                    pbrShaderResource->FindUniform("HakiLightViewProjection3") &&
                    pbrShaderResource->FindUniform("HakiCascadeSplits") &&
                    pbrShaderResource->FindUniform("HakiShowCascadeZones"),
                "glTF PBR shader reflects four-cascade shadow data");
    const Haki::ShaderUniform *pbrSsaoMap =
        pbrShaderResource ? pbrShaderResource->FindUniform("HakiSsaoMap") : nullptr;
    tests.Check(pbrSsaoMap && pbrSsaoMap->Type == Haki::ShaderValueType::Sampler2D &&
                    pbrShaderResource->FindUniform("HakiSsaoEnabled"),
                "glTF PBR shader reflects SSAO globals");
    {
        // Importer-generated UV set, KHR_texture_transform and KHR_materials_specular parameters must be
        // active uniforms of the right type, or the material loader would reject imported materials.
        bool uvUniformsValid = pbrShaderResource != nullptr;
        for (const std::string_view input : {"BaseColorTexture", "MetallicRoughnessTexture", "NormalTexture",
                                             "EmissiveTexture", "OcclusionTexture", "TransmissionTexture",
                                             "ThicknessTexture"})
        {
            const std::string name(input);
            const Haki::ShaderUniform *set = pbrShaderResource ? pbrShaderResource->FindUniform(name + "TexCoord") : nullptr;
            const Haki::ShaderUniform *transform =
                pbrShaderResource ? pbrShaderResource->FindUniform(name + "UvTransform") : nullptr;
            uvUniformsValid = uvUniformsValid && set && set->Type == Haki::ShaderValueType::Int && transform &&
                              transform->Type == Haki::ShaderValueType::Mat3;
        }
        const Haki::ShaderUniform *dielectric =
            pbrShaderResource ? pbrShaderResource->FindUniform("DielectricSpecular") : nullptr;
        tests.Check(uvUniformsValid && dielectric && dielectric->Type == Haki::ShaderValueType::Vec3,
                    "glTF PBR shader reflects per-texture UV set, UV transform and dielectric specular");
        const Haki::Material *loaded = pbrMaterial ? Haki::AssetManager::GetMaterial(pbrMaterial.Value) : nullptr;
        const auto &parameters = loaded ? loaded->GetParameters() : decltype(loaded->GetParameters()){};
        const auto transform = parameters.find("NormalTextureUvTransform");
        const glm::mat3 *matrix = transform != parameters.end() ? std::get_if<glm::mat3>(&transform->second) : nullptr;
        tests.Check(matrix && (*matrix)[0][0] == 2.0f && (*matrix)[2][0] == 0.25f && (*matrix)[2][1] == 0.5f,
                    "material JSON loads column-major mat3 UV transforms");
    }
    tests.Check(pbrShaderResource && pbrShaderResource->FindUniform("HakiAmbientIntensity") &&
                    pbrShaderResource->FindUniform("HakiAmbientSkyColor") &&
                    pbrShaderResource->FindUniform("HakiAmbientGroundColor"),
                "glTF PBR shader reflects ambient fallback uniforms");
    const Haki::ShaderUniform *irradianceUniform =
        pbrShaderResource ? pbrShaderResource->FindUniform("HakiIrradianceMap") : nullptr;
    const Haki::ShaderUniform *prefilterUniform =
        pbrShaderResource ? pbrShaderResource->FindUniform("HakiPrefilterMap") : nullptr;
    tests.Check(irradianceUniform && irradianceUniform->Type == Haki::ShaderValueType::SamplerCube &&
                    prefilterUniform && prefilterUniform->Type == Haki::ShaderValueType::SamplerCube,
                "glTF PBR shader reflects IBL cubemap samplers");
    const Haki::ShaderUniform *transmissionScene =
        pbrShaderResource ? pbrShaderResource->FindUniform("HakiTransmissionSceneColor") : nullptr;
    tests.Check(pbrShaderResource && pbrShaderResource->FindUniform("TransmissionFactor") &&
                    pbrShaderResource->FindUniform("ThicknessFactor") &&
                    pbrShaderResource->FindUniform("AttenuationColor") &&
                    pbrShaderResource->FindUniform("AttenuationDistance") &&
                    pbrShaderResource->FindUniform("Ior") && transmissionScene &&
                    transmissionScene->Type == Haki::ShaderValueType::Sampler2D,
                "glTF PBR shader reflects transmission and volume inputs");
    tests.Check(pbrShaderResource && pbrShaderResource->FindUniform("PbrWorkflow") &&
                    pbrShaderResource->FindUniform("SpecularFactor") &&
                    pbrShaderResource->FindUniform("GlossinessFactor"),
                "glTF PBR shader reflects specular-glossiness inputs");
    const Haki::ShaderUniformBlock *skinBlock =
        pbrShaderResource ? pbrShaderResource->FindUniformBlock("HakiSkin") : nullptr;
    GLint skinBinding = -1;
    if (pbrShaderResource && skinBlock)
    {
        pbrShaderResource->Bind();
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetActiveUniformBlockiv(static_cast<GLuint>(program), skinBlock->Index,
                                  GL_UNIFORM_BLOCK_BINDING, &skinBinding);
    }
    const auto hasAttributeAt = [pbrShaderResource](std::string_view name, std::int32_t location)
    {
        if (!pbrShaderResource)
            return false;
        return std::any_of(pbrShaderResource->GetAttributes().begin(), pbrShaderResource->GetAttributes().end(),
                           [name, location](const Haki::ShaderAttribute &attribute)
                           { return attribute.Name == name && attribute.Location == location; });
    };
    tests.Check(skinBlock && skinBlock->Size == static_cast<std::int32_t>(sizeof(glm::mat4) * Haki::MaxSkinJoints) &&
                    skinBinding == 3 && pbrShaderResource->FindUniform("HakiSkinningEnabled"),
                "glTF PBR shader reflects HakiSkin at binding 3");
    tests.Check(hasAttributeAt("aJoints", 5) && hasAttributeAt("aWeights", 6),
                "glTF PBR shader uses the unified skin attribute locations");
    if (pbrMaterial)
    {
        const Haki::Material *materialResource = Haki::AssetManager::GetMaterial(pbrMaterial.Value);
        const auto &parameters = materialResource->GetParameters();
        const auto transmission = parameters.find("TransmissionFactor");
        const auto thickness = parameters.find("ThicknessFactor");
        const auto workflow = parameters.find("PbrWorkflow");
        const auto specular = parameters.find("SpecularFactor");
        const auto glossiness = parameters.find("GlossinessFactor");
        tests.Check(materialResource->GetState().Pass == Haki::MaterialPass::Default &&
                        transmission != parameters.end() && std::get<float>(transmission->second) == 0.0f &&
                        thickness != parameters.end() && std::get<float>(thickness->second) == 0.0f,
                     "generated PBR material loads transmission defaults");
        tests.Check(workflow != parameters.end() && std::get<int>(workflow->second) == 0 &&
                        specular != parameters.end() &&
                        std::get<glm::vec3>(specular->second) == glm::vec3(1.0f) &&
                        glossiness != parameters.end() && std::get<float>(glossiness->second) == 1.0f,
                    "generated PBR material loads specular-glossiness defaults");
    }

    const auto shader = Haki::AssetManager::LoadShader({1, "Assets/Engine/Shaders/ForwardLit"});
    tests.Check(static_cast<bool>(shader), "same-stem shader files compile and link");
    Haki::Shader *shaderResource = Haki::AssetManager::GetShader(shader.Value);
    tests.Check(shaderResource && shaderResource->FindUniformBlock("HakiCamera"), "camera block is reflected");
    tests.Check(shaderResource && shaderResource->FindUniformBlock("HakiLights"), "light block is reflected");
    tests.Check(shaderResource && shaderResource->FindUniformBlock("HakiObject"), "object block is reflected");
    tests.Check(shaderResource && shaderResource->FindUniform("BaseColor"), "material uniform is reflected");
    tests.Check(shaderResource && shaderResource->FindUniform("HakiShadowMap0") &&
                    shaderResource->FindUniform("HakiShadowMap1") &&
                    shaderResource->FindUniform("HakiShadowMap2") &&
                    shaderResource->FindUniform("HakiShadowMap3"),
                "four cascade shadow samplers are reflected");
    tests.Check(shaderResource && shaderResource->FindUniform("HakiLightViewProjection0") &&
                    shaderResource->FindUniform("HakiLightViewProjection1") &&
                    shaderResource->FindUniform("HakiLightViewProjection2") &&
                    shaderResource->FindUniform("HakiLightViewProjection3") &&
                    shaderResource->FindUniform("HakiCascadeSplits") &&
                    shaderResource->FindUniform("HakiShowCascadeZones"),
                "four cascade matrices and splits are reflected");
    tests.Check(shaderResource && shaderResource->FindUniform("HakiShadowEnabled"),
                "shadow enable flag is reflected");
    tests.Check(shaderResource && shaderResource->FindUniform("HakiShadowBias"), "shadow bias is reflected");
    tests.Check(shaderResource && shaderResource->FindUniform("HakiShadowFilterMode"),
                "forward shader reflects the shadow filter mode");
    tests.Check(shaderResource && shaderResource->FindUniform("HakiSsaoMap") &&
                    shaderResource->FindUniform("HakiSsaoEnabled"),
                "forward shader reflects SSAO globals");

    Haki::AssetManager::RegisterMaterialLoader(std::make_unique<TestMaterialLoader>(shader.Value));
    const auto mesh = Haki::AssetManager::LoadMesh({2, "triangle"});
    const auto opaqueMaterial = Haki::AssetManager::LoadMaterial({3, "opaque"});
    const auto transparentMaterial = Haki::AssetManager::LoadMaterial({4, "transparent"});
    const auto transmissionMaterial = Haki::AssetManager::LoadMaterial({5, "transmission"});
    tests.Check(mesh && opaqueMaterial && transparentMaterial && transmissionMaterial,
                "mesh and materials load through asset handles");

    Haki::Renderer renderer;
    std::string diagnostic;
    tests.Check(renderer.Initialize({}, &diagnostic), "renderer initializes OpenGL state");
    tests.Check(renderer.GetSettings().ShadowsEnabled, "shadows are enabled by default");
    tests.Check(renderer.GetSettings().ShadowFilter == Haki::ShadowFilterMode::PCF,
                "PCF is the default shadow filter");
    tests.Check(renderer.GetSettings().ShadowDistance == 200.0f &&
                    renderer.GetSettings().CascadeSplitLambda == 0.75f,
                "cascade settings have tuned defaults");
    tests.Check(renderer.GetSettings().Exposure == 1.0f, "exposure defaults to one");
    tests.Check(renderer.GetSettings().SsaoEnabled && renderer.GetSettings().SsaoBlurEnabled &&
                    renderer.GetSettings().SsaoRadius == 0.5f &&
                    renderer.GetSettings().SsaoBias == 0.025f &&
                    renderer.GetSettings().SsaoPower == 1.5f,
                "SSAO settings have tuned defaults");
    tests.Check(renderer.GetSettings().SkyboxEnabled && renderer.GetSettings().IndirectDiffuseEnabled &&
                     renderer.GetSettings().IndirectSpecularEnabled,
                 "skybox and indirect lighting are enabled by default");
    tests.Check(renderer.GetSettings().FrustumCullingEnabled, "frustum culling is enabled by default");
    tests.Check(renderer.GetSettings().DepthPrepassEnabled && renderer.GetSettings().SsaoHalfResolution &&
                    renderer.GetSettings().SsaoSampleCount == 16 && renderer.GetSettings().RenderScale == 1.0f &&
                    renderer.GetSettings().ShadowMapResolution == 2048 && renderer.GetSettings().RenderHeight == 0,
                "performance settings have tuned defaults");
    tests.Check(renderer.GetSettings().AmbientSkyColor == glm::vec3(0.60f, 0.70f, 0.90f) &&
                    renderer.GetSettings().AmbientGroundColor == glm::vec3(0.30f, 0.25f, 0.20f) &&
                    renderer.GetSettings().AmbientIntensity == 0.3f,
                "ambient fallback has hemisphere defaults");
    tests.Check(renderer.GetSettings().BloomEnabled && renderer.GetSettings().BloomIntensity == 0.1f &&
                    renderer.GetSettings().BloomThreshold == 1.0f &&
                    renderer.GetSettings().BloomSoftKnee == 0.5f &&
                    renderer.GetSettings().BloomDiffusion == 7.0f,
                "bloom has subtle production defaults");
    tests.Check(renderer.GetSettings().FxaaEnabled &&
                    renderer.GetSettings().FxaaContrastThreshold == 0.0312f &&
                    renderer.GetSettings().FxaaRelativeThreshold == 0.063f &&
                    renderer.GetSettings().FxaaSubpixelBlending == 0.75f,
                "FXAA is enabled by default with quality-preset thresholds");

    Haki::Framebuffer forwardTarget;
    tests.Check(forwardTarget.Initialize({64, 64}), "forward test target initializes");
    forwardTarget.Bind();
    glViewport(3, 4, 48, 40);
    GLint expectedFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &expectedFramebuffer);

    const std::array<Haki::DirectionalLight, 1> lights{{}};
    Haki::RenderFrameData frame;
    frame.Camera.View = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f),
                                    glm::vec3(0.0f, 1.0f, 0.0f));
    frame.Camera.Projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    frame.Camera.WorldPosition = {0.0f, 0.0f, 3.0f};
    frame.Camera.Viewport = {0.0f, 0.0f, 64.0f, 64.0f};
    frame.Camera.NearPlane = 0.1f;
    frame.Camera.FarPlane = 100.0f;
    frame.DirectionalLights = lights;
    tests.Check(renderer.BeginFrame(frame), "frame uploads camera and light buffers");
    renderer.Submit({mesh.Value, transparentMaterial.Value});
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Submit({{}, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();

    GLint restoredFramebuffer = 0;
    std::array<GLint, 4> restoredViewport{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restoredFramebuffer);
    glGetIntegerv(GL_VIEWPORT, restoredViewport.data());

    const Haki::RendererStats &stats = renderer.GetStats();
    tests.Check(stats.SubmittedItems == 3, "submissions are buffered");
    tests.Check(stats.DrawCalls == 2, "valid opaque and transparent items draw");
    tests.Check(stats.ShadowDrawCalls == Haki::ShadowCascadeCount,
                "opaque items draw in all shadow cascades when shadows are enabled");
    tests.Check(stats.ShadowCascadesRendered == Haki::ShadowCascadeCount, "all shadow cascades render");
    tests.Check(stats.PostProcessDrawCalls == 1, "HDR frame is presented with one post-process draw");
    tests.Check(stats.FxaaDrawCalls == 1, "enabled FXAA resolves the LDR frame with one fullscreen draw");
    tests.Check(stats.SsaoPrepassDrawCalls == 1, "opaque geometry draws in the SSAO prepass");
    tests.Check(stats.SsaoDrawCalls == 3, "enabled SSAO renders raw and two separable blur passes");
    tests.Check(stats.BloomDrawCalls == 3, "64-pixel viewport renders a two-level bloom pyramid");
    tests.Check(stats.SkippedItems == 1, "invalid asset handles are skipped safely");
    tests.Check(restoredFramebuffer == expectedFramebuffer, "shadow pass restores the forward framebuffer");
    tests.Check(restoredViewport == std::array<GLint, 4>{3, 4, 48, 40}, "shadow pass restores the viewport");
    tests.Check(glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_FALSE, "shadow polygon offset does not leak");
    tests.Check(glGetError() == GL_NO_ERROR, "forward pass completes without OpenGL errors");

    renderer.GetSettings().BloomEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "frame begins with bloom disabled");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().BloomDrawCalls == 0 && renderer.GetStats().PostProcessDrawCalls == 1,
                "disabled bloom skips its pyramid but still presents HDR");
    renderer.GetSettings().BloomEnabled = true;

    {
        // Performance paths: depth-only prepass without SSAO, render scale, shadow resolution, many objects.
        renderer.GetSettings().SsaoEnabled = false;
        tests.Check(renderer.BeginFrame(frame), "depth prepass frame begins without SSAO");
        renderer.Submit({mesh.Value, opaqueMaterial.Value});
        renderer.Submit({mesh.Value, opaqueMaterial.Value, glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, -0.5f))});
        renderer.Submit({mesh.Value, transparentMaterial.Value, glm::translate(glm::mat4(1.0f), glm::vec3(-0.3f, 0.0f, 0.5f))});
        renderer.Render();
        renderer.EndFrame();
        tests.Check(renderer.GetStats().DepthPrepassDrawCalls == 2 && renderer.GetStats().SsaoPrepassDrawCalls == 0 &&
                        renderer.GetStats().DrawCalls == 3,
                    "depth prepass covers visible opaque items and every item still draws");
        tests.Check(glGetError() == GL_NO_ERROR, "depth prepass with per-frame object slots has no OpenGL errors");

        // The prepass must not change the image: identical depth maths means LEQUAL keeps every front surface.
        const auto renderPixels = [&](bool prepass)
        {
            renderer.GetSettings().DepthPrepassEnabled = prepass;
            renderer.BeginFrame(frame);
            renderer.Submit({mesh.Value, pbrMaterial.Value});
            renderer.Submit({mesh.Value, opaqueMaterial.Value,
                             glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, 0.1f, -0.4f))});
            renderer.Render();
            renderer.EndFrame();
            std::vector<unsigned char> pixels(48 * 40 * 4);
            glReadPixels(3, 4, 48, 40, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        const std::vector<unsigned char> withPrepass = renderPixels(true);
        const std::vector<unsigned char> withoutPrepass = renderPixels(false);
        int largestDifference = 0;
        for (std::size_t index = 0; index < withPrepass.size(); ++index)
        {
            largestDifference = std::max(largestDifference, std::abs(static_cast<int>(withPrepass[index]) -
                                                                     static_cast<int>(withoutPrepass[index])));
        }
        const bool hasGeometry = std::any_of(withPrepass.begin(), withPrepass.end(),
                                             [&](unsigned char value) { return value != withPrepass[0]; });
        tests.Check(hasGeometry && largestDifference <= 2,
                    "depth prepass produces the same image as direct forward rendering");

        renderer.GetSettings().DepthPrepassEnabled = false;
        tests.Check(renderer.BeginFrame(frame), "frame begins with depth prepass disabled");
        renderer.Submit({mesh.Value, opaqueMaterial.Value});
        renderer.Render();
        renderer.EndFrame();
        tests.Check(renderer.GetStats().DepthPrepassDrawCalls == 0 && renderer.GetStats().DrawCalls == 1,
                    "disabled depth prepass draws directly");
        renderer.GetSettings().DepthPrepassEnabled = true;
        renderer.GetSettings().SsaoEnabled = true;

        renderer.GetSettings().RenderScale = 0.5f;
        renderer.GetSettings().ShadowMapResolution = 1024;
        tests.Check(renderer.BeginFrame(frame), "half render scale frame begins");
        renderer.Submit({mesh.Value, opaqueMaterial.Value});
        renderer.Render();
        renderer.EndFrame();
        GLint scaledFramebuffer = 0;
        std::array<GLint, 4> scaledViewport{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &scaledFramebuffer);
        glGetIntegerv(GL_VIEWPORT, scaledViewport.data());
        tests.Check(renderer.GetStats().DrawCalls == 1 && renderer.GetStats().PostProcessDrawCalls == 1 &&
                        renderer.GetStats().SsaoDrawCalls == 3 &&
                        renderer.GetStats().ShadowCascadesRendered == Haki::ShadowCascadeCount,
                    "half render scale with 1024 shadow maps renders every pass");
        tests.Check(scaledFramebuffer == expectedFramebuffer &&
                        scaledViewport == std::array<GLint, 4>{3, 4, 48, 40},
                    "render scale still presents into the caller's framebuffer and viewport");
        tests.Check(glGetError() == GL_NO_ERROR, "render scale and shadow resize complete without OpenGL errors");
        renderer.GetSettings().RenderScale = 1.0f;
        renderer.GetSettings().ShadowMapResolution = 2048;

        renderer.GetSettings().RenderHeight = 32;
        tests.Check(renderer.BeginFrame(frame), "fixed render height frame begins");
        renderer.Submit({mesh.Value, opaqueMaterial.Value});
        renderer.Render();
        renderer.EndFrame();
        GLint fixedFramebuffer = 0;
        std::array<GLint, 4> fixedViewport{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fixedFramebuffer);
        glGetIntegerv(GL_VIEWPORT, fixedViewport.data());
        tests.Check(renderer.GetStats().RenderWidth == 32 && renderer.GetStats().RenderHeight == 32 &&
                        renderer.GetStats().PostProcessDrawCalls == 1,
                    "fixed render height renders internally at that size, keeping the window aspect");
        tests.Check(fixedFramebuffer == expectedFramebuffer && fixedViewport == std::array<GLint, 4>{3, 4, 48, 40} &&
                        glGetError() == GL_NO_ERROR,
                    "fixed render height upscales into the caller's framebuffer and viewport");
        renderer.GetSettings().RenderHeight = 0;

        renderer.GetSettings().SsaoHalfResolution = false;
        tests.Check(renderer.BeginFrame(frame), "full resolution SSAO frame begins");
        renderer.Submit({mesh.Value, opaqueMaterial.Value});
        renderer.Render();
        renderer.EndFrame();
        tests.Check(renderer.GetStats().SsaoDrawCalls == 3 && glGetError() == GL_NO_ERROR,
                    "full resolution SSAO still renders without OpenGL errors");
        renderer.GetSettings().SsaoHalfResolution = true;
    }

    renderer.GetSettings().FxaaEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "frame begins with FXAA disabled");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    {
        GLint fxaaOffFramebuffer = 0;
        std::array<GLint, 4> fxaaOffViewport{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fxaaOffFramebuffer);
        glGetIntegerv(GL_VIEWPORT, fxaaOffViewport.data());
        tests.Check(renderer.GetStats().FxaaDrawCalls == 0 && renderer.GetStats().PostProcessDrawCalls == 1,
                    "disabled FXAA tone maps directly to the output target");
        tests.Check(fxaaOffFramebuffer == expectedFramebuffer &&
                        fxaaOffViewport == std::array<GLint, 4>{3, 4, 48, 40},
                    "FXAA-disabled present restores framebuffer and viewport");
    }
    tests.Check(glGetError() == GL_NO_ERROR, "FXAA-disabled rendering completes without OpenGL errors");
    renderer.GetSettings().FxaaEnabled = true;

    const glm::mat4 behindCamera = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 50.0f));
    tests.Check(renderer.BeginFrame(frame), "frame begins with an off-screen item");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Submit({mesh.Value, opaqueMaterial.Value, behindCamera});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DrawCalls == 1 && renderer.GetStats().CulledItems == 1,
                "item behind the camera is frustum culled from the forward pass");
    tests.Check(glGetError() == GL_NO_ERROR, "frustum-culled rendering completes without OpenGL errors");

    renderer.GetSettings().FrustumCullingEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "frame begins with frustum culling disabled");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Submit({mesh.Value, opaqueMaterial.Value, behindCamera});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DrawCalls == 2 && renderer.GetStats().CulledItems == 0 &&
                    renderer.GetStats().ShadowCulledDraws == 0,
                "disabled frustum culling draws every item");
    renderer.GetSettings().FrustumCullingEnabled = true;

    // Debug draw: one depth-tested line plus an overlay box (12 lines) render in two draws and are cleared.
    renderer.GetDebugDraw().Line(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
                                 glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    renderer.GetDebugDraw().Box(glm::vec3(0.0f), glm::vec3(0.5f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), false);
    tests.Check(renderer.BeginFrame(frame), "frame begins with queued debug geometry");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    {
        GLint debugFramebuffer = 0;
        std::array<GLint, 4> debugViewport{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &debugFramebuffer);
        glGetIntegerv(GL_VIEWPORT, debugViewport.data());
        tests.Check(renderer.GetStats().DebugDrawCalls == 2 && renderer.GetStats().DebugLines == 13,
                    "depth-tested and overlay debug lines render in two draws");
        tests.Check(renderer.GetDebugDraw().IsEmpty(), "debug geometry is cleared after it renders");
        tests.Check(debugFramebuffer == expectedFramebuffer &&
                        debugViewport == std::array<GLint, 4>{3, 4, 48, 40},
                    "debug pass draws into the caller's framebuffer and viewport");
    }
    tests.Check(glGetError() == GL_NO_ERROR, "debug draw completes without OpenGL errors");

    renderer.GetSettings().ShowBounds = true;
    tests.Check(renderer.BeginFrame(frame), "frame begins with bounds visualisation");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Submit({mesh.Value, opaqueMaterial.Value, behindCamera});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DebugLines == 24, "show bounds draws a box for visible and culled items");
    renderer.GetSettings().ShowBounds = false;

    {
        // Fonts and the 2D canvas: metrics, then a red rect drawn into the caller's top-left corner.
        Haki::Font font;
        std::string fontError;
        tests.Check(!font.LoadFromFile("Assets/Engine/Fonts/does-not-exist.ttf", 32.0f, &fontError) && !fontError.empty(),
                    "missing font file reports an error");
        tests.Check(font.LoadFromFile("Assets/Engine/Fonts/Roboto-Medium.ttf", 48.0f, &fontError) && font.IsValid(),
                    "default font loads and bakes an atlas");
        const glm::vec2 hello = font.MeasureText("Hello", 32.0f);
        const glm::vec2 twoLines = font.MeasureText("A\nB", 32.0f);
        tests.Check(hello.x > 32.0f && hello.x < 160.0f && std::abs(hello.y - font.GetLineHeight(32.0f)) < 0.01f,
                    "text measures to a plausible width and one line height");
        tests.Check(std::abs(twoLines.y - 2.0f * font.GetLineHeight(32.0f)) < 0.01f,
                    "newlines add a line of height");
        tests.Check(font.MeasureText("Hello", 64.0f).x > hello.x * 1.9f, "text width scales with pixel size");

        Haki::Canvas &canvas = renderer.GetCanvas();
        canvas.FillRect({0.0f, 0.0f, 10.0f, 10.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
        canvas.DrawText(font, "Hi!", {12.0f, 0.0f}, 16.0f, {1.0f, 1.0f, 1.0f, 1.0f});
        const std::size_t queuedQuads = canvas.GetQuadCount();
        tests.Check(queuedQuads == 4, "text queues one quad per visible glyph");
        tests.Check(renderer.BeginFrame(frame), "canvas frame begins");
        tests.Check(canvas.GetSize() == glm::vec2(48.0f, 40.0f), "canvas size follows the output viewport");
        renderer.Submit({mesh.Value, opaqueMaterial.Value});
        renderer.Render();
        renderer.EndFrame();
        std::array<unsigned char, 4> topLeft{};
        glReadPixels(3 + 2, 4 + 40 - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, topLeft.data());
        tests.Check(renderer.GetStats().CanvasQuads == queuedQuads && renderer.GetStats().CanvasDrawCalls == 2,
                    "canvas draws its quads in one batch per texture");
        tests.Check(topLeft[0] > 240 && topLeft[1] < 15 && topLeft[2] < 15,
                    "canvas rect lands in the viewport's top-left corner");
        GLint canvasFramebuffer = 0;
        std::array<GLint, 4> canvasViewport{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &canvasFramebuffer);
        glGetIntegerv(GL_VIEWPORT, canvasViewport.data());
        tests.Check(canvasFramebuffer == expectedFramebuffer &&
                        canvasViewport == std::array<GLint, 4>{3, 4, 48, 40} && canvas.IsEmpty(),
                    "canvas pass restores state and clears after drawing");
        tests.Check(glGetError() == GL_NO_ERROR, "canvas rendering completes without OpenGL errors");

        // Billboards: a marker behind the triangle is hidden by its depth test, one in front is not.
        const auto centrePixel = [&](const glm::vec3 &anchorPosition, bool depthTest)
        {
            renderer.GetCanvas().FillRect(Haki::CanvasAnchor{anchorPosition, depthTest}, {-4.0f, -4.0f, 8.0f, 8.0f},
                                          {1.0f, 0.0f, 0.0f, 1.0f});
            renderer.BeginFrame(frame);
            renderer.Submit({mesh.Value, opaqueMaterial.Value});
            renderer.Render();
            renderer.EndFrame();
            std::array<unsigned char, 4> pixel{};
            glReadPixels(3 + 24, 4 + 20, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            return pixel[0] > 240 && pixel[1] < 15 && pixel[2] < 15;
        };
        tests.Check(centrePixel(glm::vec3(0.0f, 0.0f, 1.0f), true), "billboard in front of geometry is drawn");
        tests.Check(!centrePixel(glm::vec3(0.0f, 0.0f, -2.0f), true),
                    "depth-tested billboard behind geometry is hidden");
        tests.Check(centrePixel(glm::vec3(0.0f, 0.0f, -2.0f), false),
                    "billboard without depth test shows through geometry");
        renderer.GetCanvas().FillRect(Haki::CanvasAnchor{glm::vec3(0.0f, 0.0f, 10.0f)}, {0.0f, 0.0f, 4.0f, 4.0f},
                                      glm::vec4(1.0f));
        renderer.BeginFrame(frame);
        renderer.Render();
        renderer.EndFrame();
        tests.Check(renderer.GetStats().CanvasCulledQuads == 1 && renderer.GetStats().CanvasDrawCalls == 0,
                    "billboards behind the camera are culled before drawing");

        const glm::vec2 labelSize = font.MeasureText("Label", 20.0f);
        renderer.GetCanvas().DrawLabel(Haki::CanvasAnchor{glm::vec3(0.0f)}, font, "Label", 20.0f,
                                       glm::vec4(1.0f), 6.0f);
        tests.Check(renderer.GetCanvas().GetQuadCount() == 5 && labelSize.x > 0.0f,
                    "labels queue one quad per glyph");
        renderer.GetCanvas().Clear();
        tests.Check(glGetError() == GL_NO_ERROR, "billboard rendering completes without OpenGL errors");
    }

    // GPU timings arrive a few frames late; finish the GPU between frames so results are available.
    for (int index = 0; index < 6; ++index)
    {
        tests.Check(renderer.BeginFrame(frame), "timed frame begins");
        renderer.Submit({mesh.Value, opaqueMaterial.Value});
        renderer.Render();
        renderer.EndFrame();
        glFinish();
    }
    const Haki::RendererGpuTimings &timings = renderer.GetGpuTimings();
    tests.Check(timings.Valid, "GPU pass timings become available");
    tests.Check(timings.TotalMs >= 0.0f && std::isfinite(timings.TotalMs) &&
                    timings.TotalMs + 0.001f >= timings.ForwardMs + timings.BloomMs + timings.PostProcessMs,
                "GPU total covers the measured passes");
    tests.Check(glGetError() == GL_NO_ERROR, "GPU timer queries complete without OpenGL errors");

    const std::array<glm::mat4, 1> jointPalette{{glm::mat4(1.0f)}};
    tests.Check(renderer.BeginFrame(frame), "skinned frame begins");
    renderer.Submit({mesh.Value, pbrMaterial.Value, glm::mat4(1.0f), 0, jointPalette});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DrawCalls == 1 &&
                    renderer.GetStats().ShadowDrawCalls == Haki::ShadowCascadeCount &&
                    renderer.GetStats().SsaoPrepassDrawCalls == 1,
                "joint palette uploads in forward, cascaded shadow, and SSAO draws");
    tests.Check(glGetError() == GL_NO_ERROR, "skinned rendering completes without OpenGL errors");

    renderer.GetSettings().IndirectDiffuseEnabled = false;
    renderer.GetSettings().IndirectSpecularEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "frame begins with IBL indirect lighting disabled");
    renderer.Submit({mesh.Value, pbrMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DrawCalls == 1, "PBR item draws with the ambient fallback");
    tests.Check(glGetError() == GL_NO_ERROR, "ambient fallback rendering completes without OpenGL errors");
    renderer.GetSettings().IndirectDiffuseEnabled = true;
    renderer.GetSettings().IndirectSpecularEnabled = true;

    const std::array<glm::mat4, Haki::MaxSkinJoints + 1> oversizedPalette{};
    tests.Check(renderer.BeginFrame(frame), "oversized skin frame begins");
    renderer.Submit({mesh.Value, pbrMaterial.Value, glm::mat4(1.0f), 0, oversizedPalette});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().SkippedItems == 1 && renderer.GetStats().DrawCalls == 0 &&
                    renderer.GetStats().ShadowDrawCalls == 0 && renderer.GetStats().SsaoPrepassDrawCalls == 0,
                "palettes larger than MaxSkinJoints are rejected safely");
    tests.Check(glGetError() == GL_NO_ERROR, "oversized palette rejection produces no OpenGL errors");

    renderer.GetSettings().ShadowsEnabled = false;
    renderer.GetSettings().SsaoEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "frame begins with shadows disabled");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DrawCalls == 1, "forward rendering continues with shadows disabled");
    tests.Check(renderer.GetStats().ShadowDrawCalls == 0, "shadow pass is skipped when shadows are disabled");
    tests.Check(renderer.GetStats().ShadowCascadesRendered == 0, "disabled shadows render no cascades");
    tests.Check(renderer.GetStats().PostProcessDrawCalls == 1, "shadow-disabled frame is tone mapped");
    tests.Check(renderer.GetStats().SsaoPrepassDrawCalls == 0 && renderer.GetStats().SsaoDrawCalls == 0,
                "disabled SSAO performs no prepass or fullscreen draws");
    tests.Check(glGetError() == GL_NO_ERROR, "shadow-disabled rendering completes without OpenGL errors");

    renderer.GetSettings().SsaoEnabled = true;
    renderer.GetSettings().SsaoBlurEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "frame begins with SSAO blur disabled");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().SsaoPrepassDrawCalls == 1 && renderer.GetStats().SsaoDrawCalls == 1,
                "blur-disabled SSAO renders one prepass item and one fullscreen pass");
    tests.Check(glGetError() == GL_NO_ERROR, "blur-disabled SSAO completes without OpenGL errors");

    renderer.GetSettings().SsaoBlurEnabled = true;
    tests.Check(renderer.BeginFrame(frame), "transparent-only SSAO frame begins");
    renderer.Submit({mesh.Value, transparentMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restoredFramebuffer);
    glGetIntegerv(GL_VIEWPORT, restoredViewport.data());
    tests.Check(renderer.GetStats().SsaoPrepassDrawCalls == 0 && renderer.GetStats().SsaoDrawCalls == 0,
                "transparent-only frames skip all SSAO work");
    tests.Check(restoredFramebuffer == expectedFramebuffer &&
                    restoredViewport == std::array<GLint, 4>{3, 4, 48, 40},
                "SSAO frames preserve the output framebuffer and viewport");
    tests.Check(glGetError() == GL_NO_ERROR, "transparent-only rendering completes without OpenGL errors");

    tests.Check(renderer.BeginFrame(frame), "transmission-only frame begins");
    renderer.Submit({mesh.Value, transmissionMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DrawCalls == 1 && renderer.GetStats().TransmissionDrawCalls == 1 &&
                    renderer.GetStats().TransmissionSceneCopies == 1,
                "transmission renders after one HDR scene capture");
    tests.Check(renderer.GetStats().ShadowDrawCalls == 0 && renderer.GetStats().SsaoPrepassDrawCalls == 0 &&
                    renderer.GetStats().SsaoDrawCalls == 0,
                "transmission skips opaque shadow and SSAO passes");
    tests.Check(glGetError() == GL_NO_ERROR, "transmission rendering completes without OpenGL errors");

    frame.Camera.Viewport = {0.0f, 0.0f, 32.0f, 48.0f};
    tests.Check(renderer.BeginFrame(frame), "resized SSAO frame begins");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restoredFramebuffer);
    glGetIntegerv(GL_VIEWPORT, restoredViewport.data());
    tests.Check(renderer.GetStats().SsaoPrepassDrawCalls == 1 && renderer.GetStats().SsaoDrawCalls == 3,
                "SSAO targets resize with a non-square camera viewport");
    tests.Check(renderer.GetStats().BloomDrawCalls == 1,
                "bloom pyramid adapts its level count to a smaller non-square viewport");
    tests.Check(restoredFramebuffer == expectedFramebuffer &&
                    restoredViewport == std::array<GLint, 4>{3, 4, 48, 40} && glGetError() == GL_NO_ERROR,
                "resized SSAO rendering restores presentation state without OpenGL errors");
    frame.Camera.Viewport = {0.0f, 0.0f, 64.0f, 64.0f};

    renderer.GetSettings().ShadowsEnabled = true;
    renderer.GetSettings().ShadowFilter = Haki::ShadowFilterMode::PCSS;

    tests.Check(renderer.BeginFrame(frame), "frame begins with PCSS enabled");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DrawCalls == 1, "forward rendering continues with PCSS enabled");
    tests.Check(renderer.GetStats().ShadowDrawCalls == Haki::ShadowCascadeCount,
                "PCSS uses all generated cascade shadow maps");
    tests.Check(renderer.GetStats().PostProcessDrawCalls == 1, "PCSS frame is tone mapped");
    tests.Check(glGetError() == GL_NO_ERROR, "PCSS rendering completes without OpenGL errors");
    renderer.GetSettings().ShowCascadeZones = true;

    std::array<Haki::DirectionalLight, 1> brightLights{{}};
    brightLights[0].Direction = {0.0f, 0.0f, -1.0f};
    brightLights[0].Intensity = 8.0f;
    frame.DirectionalLights = brightLights;
    renderer.GetSettings().ShadowsEnabled = false;
    renderer.GetSettings().Exposure = 1.5f;
    renderer.GetSettings().BloomEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "bright HDR frame begins");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    std::array<float, 4> hdrPixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, hdrPixel.data());
    tests.Check(hdrPixel[0] > 1.0f, "RGBA16F scene target preserves values above one");
    renderer.EndFrame();
    std::array<float, 4> toneMappedPixel{};
    glReadPixels(27, 24, 1, 1, GL_RGBA, GL_FLOAT, toneMappedPixel.data());
    const float exposed = hdrPixel[0] * renderer.GetSettings().Exposure;
    const float aces = std::clamp((exposed * (2.51f * exposed + 0.03f)) /
                                      (exposed * (2.43f * exposed + 0.59f) + 0.14f),
                                  0.0f, 1.0f);
    const float expectedOutput = std::pow(aces, 1.0f / 2.2f);
    tests.Check(std::abs(toneMappedPixel[0] - expectedOutput) < 0.02f,
                "post-process applies exposure, ACES tone mapping, and gamma");
    tests.Check(renderer.GetStats().PostProcessDrawCalls == 1, "bright HDR frame uses one post-process draw");

    renderer.GetSettings().Exposure = 1.0f;
    renderer.GetSettings().SsaoEnabled = false;
    renderer.GetSettings().BloomIntensity = 10.0f;
    renderer.GetSettings().BloomThreshold = 1.0f;
    renderer.GetSettings().BloomSoftKnee = 0.5f;
    renderer.GetSettings().BloomDiffusion = 7.0f;
    tests.Check(renderer.BeginFrame(frame), "bright baseline frame begins with bloom disabled");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    std::array<float, 4> bloomDisabledPixel{};
    glReadPixels(35, 24, 1, 1, GL_RGBA, GL_FLOAT, bloomDisabledPixel.data());

    renderer.GetSettings().BloomEnabled = true;
    tests.Check(renderer.BeginFrame(frame), "bright frame begins with bloom enabled");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    std::array<float, 4> bloomEnabledPixel{};
    glReadPixels(35, 24, 1, 1, GL_RGBA, GL_FLOAT, bloomEnabledPixel.data());
    tests.Check(renderer.GetStats().BloomDrawCalls == 3,
                "enabled bloom renders downsample and tent-upsample passes");
    tests.Check(bloomEnabledPixel[0] > bloomDisabledPixel[0] + 0.01f,
                "bloom spreads bright HDR energy beyond rendered geometry");

    renderer.GetSettings().BloomThreshold = 100.0f;
    tests.Check(renderer.BeginFrame(frame), "high-threshold bloom frame begins");
    renderer.Submit({mesh.Value, opaqueMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    std::array<float, 4> thresholdedPixel{};
    glReadPixels(35, 24, 1, 1, GL_RGBA, GL_FLOAT, thresholdedPixel.data());
    tests.Check(std::abs(thresholdedPixel[0] - bloomDisabledPixel[0]) < 0.01f,
                "bloom threshold rejects HDR values below its cutoff");
    tests.Check(glGetError() == GL_NO_ERROR, "bloom rendering completes without OpenGL errors");

    renderer.GetSettings().ShadowsEnabled = true;
    renderer.GetSettings().SsaoEnabled = true;
    renderer.GetSettings().BloomIntensity = 0.1f;
    renderer.GetSettings().BloomThreshold = 1.0f;
    renderer.GetSettings().Exposure = 1.0f;
    frame.DirectionalLights = lights;

    const auto environment = Haki::AssetManager::LoadTexture(
        {700, "Assets/Engine/Skyboxes/qwantani_dusk_2_puresky_4k.hdr"});
    Haki::Texture *environmentTexture = environment ? Haki::AssetManager::GetTexture(environment.Value) : nullptr;
    tests.Check(environmentTexture && environmentTexture->GetFormat() == Haki::TextureFormat::RGBA16F &&
                    environmentTexture->GetWidth() == 4096 && environmentTexture->GetHeight() == 2048,
                "Radiance HDRI loads as a linear floating-point texture");
    frame.Environment.Texture = environment.Value;
    frame.Environment.Intensity = 1.0f;
    frame.Environment.RotationDegrees = 0.0f;
    frame.DirectionalLights = {};
    renderer.GetSettings().ShadowsEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "environment frame begins and bakes IBL maps");
    renderer.Render();
    std::array<float, 4> skyPixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, skyPixel.data());
    renderer.EndFrame();
    tests.Check(renderer.GetStats().SkyboxDrawCalls == 1 &&
                    skyPixel[0] + skyPixel[1] + skyPixel[2] > 0.0f,
                "generated environment cubemap renders as an HDR skybox");

    renderer.GetSettings().SkyboxEnabled = false;
    renderer.GetSettings().IndirectDiffuseEnabled = false;
    renderer.GetSettings().IndirectSpecularEnabled = false;
    tests.Check(renderer.BeginFrame(frame), "PBR frame begins with indirect lighting disabled");
    renderer.Submit({gltfMesh.Value, pbrMaterial.Value});
    renderer.Render();
    std::array<float, 4> unlitPixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, unlitPixel.data());
    renderer.EndFrame();

    renderer.GetSettings().IndirectDiffuseEnabled = true;
    tests.Check(renderer.BeginFrame(frame), "PBR frame begins with diffuse IBL enabled");
    renderer.Submit({gltfMesh.Value, pbrMaterial.Value});
    renderer.Render();
    std::array<float, 4> diffuseIblPixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, diffuseIblPixel.data());
    renderer.EndFrame();
    const float unlitEnergy = unlitPixel[0] + unlitPixel[1] + unlitPixel[2];
    const float diffuseEnergy = diffuseIblPixel[0] + diffuseIblPixel[1] + diffuseIblPixel[2];
    tests.Check(diffuseEnergy > unlitEnergy + 0.001f,
                "irradiance cubemap lights a PBR surface without direct lights");
    tests.Check(renderer.GetStats().SkyboxDrawCalls == 0, "skybox toggle does not disable indirect lighting");

    renderer.GetSettings().IndirectDiffuseEnabled = false;
    renderer.GetSettings().IndirectSpecularEnabled = true;
    tests.Check(renderer.BeginFrame(frame), "PBR frame begins with specular IBL enabled");
    renderer.Submit({gltfMesh.Value, pbrMaterial.Value});
    renderer.Render();
    renderer.EndFrame();
    tests.Check(renderer.GetStats().DrawCalls == 1 && glGetError() == GL_NO_ERROR,
                "prefiltered environment and BRDF LUT render independently");
    renderer.GetSettings().SkyboxEnabled = true;
    renderer.GetSettings().IndirectDiffuseEnabled = true;
    frame.Environment = {};
    frame.DirectionalLights = lights;
    renderer.GetSettings().ShadowsEnabled = true;

    Haki::Framebuffer::BindDefault();
    glViewport(0, 0, 64, 64);
    Haki::SceneSerializer sceneSerializer(catalog);
    auto pbrScene = sceneSerializer.Deserialize("src/Tests/Serialization/Fixtures/PBR.scene.json");
    tests.Check(static_cast<bool>(pbrScene), "scene material slots deserialize against mesh submeshes");
    if (pbrScene)
    {
        pbrScene.Value->OnRender({&renderer, 64, 64});
        tests.Check(renderer.GetStats().DrawCalls == 1, "ECS scene renders the PBR submesh material slot");
    }
    tests.Check(glGetError() == GL_NO_ERROR, "PBR scene render completes without OpenGL errors");

    {
        // Shader hot reload: edits rebuild the shader in place, and a broken edit keeps the old program.
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "haki-hot-reload-test";
        std::filesystem::create_directories(directory);
        const std::filesystem::path stem = directory / "Reloadable";
        const auto write = [&stem](std::string_view extension, std::string_view contents, int ageSeconds)
        {
            std::filesystem::path path = stem;
            path.replace_extension(extension);
            std::ofstream(path, std::ios::trunc) << contents;
            std::filesystem::last_write_time(
                path, std::filesystem::file_time_type::clock::now() - std::chrono::seconds(ageSeconds));
        };
        constexpr std::string_view vertex =
            "#version 330 core\nlayout(location = 0) in vec3 aPosition;\n"
            "void main() { gl_Position = vec4(aPosition, 1.0); }\n";
        write(".vert", vertex, 60);
        write(".frag",
              "#version 330 core\nuniform float HakiTestA;\nout vec4 FragColor;\n"
              "void main() { FragColor = vec4(HakiTestA); }\n",
              60);
        const auto reloadable = Haki::AssetManager::LoadShader({0x5eed0001u, stem});
        const Haki::Shader *before = reloadable ? Haki::AssetManager::GetShader(reloadable.Value) : nullptr;
        tests.Check(before && before->FindUniform("HakiTestA"), "hot reload test shader loads");
        const Haki::ShaderReloadResult unchanged = Haki::AssetManager::ReloadChangedShaders();
        tests.Check(unchanged.Reloaded == 0 && unchanged.Failed == 0, "unchanged shaders are not rebuilt");

        write(".frag",
              "#version 330 core\nuniform float HakiTestB;\nout vec4 FragColor;\n"
              "void main() { FragColor = vec4(HakiTestB); }\n",
              0);
        const Haki::ShaderReloadResult edited = Haki::AssetManager::ReloadChangedShaders();
        const Haki::Shader *after = reloadable ? Haki::AssetManager::GetShader(reloadable.Value) : nullptr;
        tests.Check(edited.Reloaded == 1 && edited.Failed == 0, "edited shader is rebuilt");
        tests.Check(after == before && after && after->FindUniform("HakiTestB") && !after->FindUniform("HakiTestA"),
                    "rebuilt shader keeps its handle and picks up the new source");

        std::filesystem::path fragmentPath = stem;
        fragmentPath.replace_extension(".frag");
        std::ofstream(fragmentPath, std::ios::trunc) << "#version 330 core\nvoid main() { this is not glsl }\n";
        std::filesystem::last_write_time(fragmentPath,
                                         std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
        const Haki::ShaderReloadResult broken = Haki::AssetManager::ReloadChangedShaders();
        tests.Check(broken.Failed == 1 && broken.Reloaded == 0 && !broken.Errors.empty(),
                    "broken shader edit reports a compile error");
        tests.Check(after && after->IsValid() && after->FindUniform("HakiTestB"),
                    "broken shader edit keeps the previous working program");
        tests.Check(Haki::AssetManager::ReloadChangedShaders().Failed == 0,
                    "a broken file is reported once per save, not every poll");
        tests.Check(Haki::AssetManager::ReloadChangedShaders(true).Failed == 1, "forced reload retries every shader");
        Haki::AssetManager::UnloadShader(reloadable.Value);
        std::filesystem::remove_all(directory);
        while (glGetError() != GL_NO_ERROR)
        {
        }
    }

    pbrScene.Value.reset();
    renderer.Shutdown();

    Haki::AssetManager::Shutdown();
    tests.Check(Haki::AssetManager::Initialize(), "asset manager reinitializes for project assets");
    auto projectCatalog = std::make_shared<Haki::AssetCatalog>();
    Haki::AssetManager::RegisterShaderLoader(std::make_unique<Haki::ShaderFileLoader>());
    Haki::AssetManager::RegisterTextureLoader(std::make_unique<Haki::TextureFileLoader>(projectCatalog));
    Haki::AssetManager::RegisterMeshLoader(std::make_unique<Haki::GltfMeshLoader>(projectCatalog));
    Haki::AssetManager::RegisterMaterialLoader(std::make_unique<Haki::MaterialFileLoader>(projectCatalog));

    Haki::SceneSerializer projectSerializer(projectCatalog);
    Haki::SceneManifest projectManifest;
    std::vector<Haki::SerializationError> projectErrors;
    tests.Check(projectSerializer.LoadManifest("src/Tests/Renderer/Fixtures/Project.json", projectManifest, projectErrors),
                "project scene manifest and catalog load");
    tests.Check(projectManifest.EntryScene == "bloom-lights-test", "bloom lighting scene is the manifest entry");

    Haki::SceneLoadResult bloomScene;
    for (const Haki::SceneManifestEntry &entry : projectManifest.Scenes)
    {
        if (entry.Id == projectManifest.EntryScene)
        {
            bloomScene = projectSerializer.Deserialize(entry.Source);
        }
    }
    for (const Haki::SerializationError &error : bloomScene.Errors)
    {
        std::cout << error.Source.string() << error.JsonPointer << ": " << error.Message << '\n';
    }
    tests.Check(static_cast<bool>(bloomScene), "bloom lighting scene deserializes with real assets");
    if (bloomScene)
    {
        auto &registry = bloomScene.Value->GetRegistry();
        tests.Check(registry.view<Haki::MeshRendererComponent>().size() == 8 &&
                        registry.view<Haki::PointLightComponent>().size() == 1 &&
                        registry.view<Haki::SpotLightComponent>().size() == 1,
                    "bloom scene loads geometry, an emissive panel, a point light, and a spot light");
    }

    const Haki::AssetCatalogEntry *emissiveEntry = projectCatalog->Find("test.material.bloom-emissive");
    const auto emissiveMaterial = emissiveEntry
                                      ? Haki::AssetManager::LoadMaterial(
                                            {emissiveEntry->RuntimeId, emissiveEntry->Source})
                                      : Haki::AssetResult<Haki::MaterialHandle>{};
    const Haki::Material *emissiveResource =
        emissiveMaterial ? Haki::AssetManager::GetMaterial(emissiveMaterial.Value) : nullptr;
    bool emissiveHdr = false;
    if (emissiveResource)
    {
        const auto &parameters = emissiveResource->GetParameters();
        const auto parameter = parameters.find("EmissiveFactor");
        const auto *value = parameter != parameters.end() ? std::get_if<glm::vec3>(&parameter->second) : nullptr;
        emissiveHdr = value && value->r > 1.0f;
    }
    tests.Check(emissiveHdr, "bloom emissive material stores HDR emission above one");

    bloomScene.Value.reset();
    Haki::AssetManager::Shutdown();
    return tests.Result();
}

} // namespace

int main()
{
    return RunRendererTests();
}
