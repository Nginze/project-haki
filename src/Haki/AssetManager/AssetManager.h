#pragma once

#include "Haki/AssetLoader/AssetLoadRequest.h"
#include "Haki/AssetLoader/AssetLoader.h"
#include "Haki/AssetManager/AssetTypes.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Haki
{

struct ShaderReloadResult
{
    std::size_t Reloaded = 0;
    std::size_t Failed = 0;
    std::vector<std::string> Errors;
};

class AssetManager
{
public:
    AssetManager() = delete;

    static bool Initialize();
    static void Shutdown();
    static bool IsInitialized();

    static bool RegisterTextureLoader(std::unique_ptr<AssetLoader<Texture>> loader);
    static bool RegisterShaderLoader(std::unique_ptr<AssetLoader<Shader>> loader);
    static bool RegisterMeshLoader(std::unique_ptr<AssetLoader<Mesh>> loader);
    static bool RegisterMaterialLoader(std::unique_ptr<AssetLoader<Material>> loader);
    static bool RegisterSkeletonLoader(std::unique_ptr<AssetLoader<Skeleton>> loader);
    static bool RegisterAnimationClipLoader(std::unique_ptr<AssetLoader<AnimationClip>> loader);

    static AssetResult<TextureHandle> LoadTexture(const AssetLoadRequest &request);
    static AssetResult<ShaderHandle> LoadShader(const AssetLoadRequest &request);
    static AssetResult<MeshHandle> LoadMesh(const AssetLoadRequest &request);
    static AssetResult<MaterialHandle> LoadMaterial(const AssetLoadRequest &request);
    static AssetResult<SkeletonHandle> LoadSkeleton(const AssetLoadRequest &request);
    static AssetResult<AnimationClipHandle> LoadAnimationClip(const AssetLoadRequest &request);

    static Texture *GetTexture(TextureHandle handle);
    static Shader *GetShader(ShaderHandle handle);
    static Mesh *GetMesh(MeshHandle handle);
    static Material *GetMaterial(MaterialHandle handle);
    static Skeleton *GetSkeleton(SkeletonHandle handle);
    static AnimationClip *GetAnimationClip(AnimationClipHandle handle);

    static bool UnloadTexture(TextureHandle handle);
    static bool UnloadShader(ShaderHandle handle);
    static bool UnloadMesh(MeshHandle handle);
    static bool UnloadMaterial(MaterialHandle handle);
    static bool UnloadSkeleton(SkeletonHandle handle);
    static bool UnloadAnimationClip(AnimationClipHandle handle);

    static bool IsTextureLoaded(AssetID id);
    static bool IsShaderLoaded(AssetID id);
    static bool IsMeshLoaded(AssetID id);
    static bool IsMaterialLoaded(AssetID id);
    static bool IsSkeletonLoaded(AssetID id);
    static bool IsAnimationClipLoaded(AssetID id);

    static std::size_t GetLoadedTextureCount();
    static std::size_t GetLoadedShaderCount();
    static std::size_t GetLoadedMeshCount();
    static std::size_t GetLoadedMaterialCount();
    static std::size_t GetLoadedSkeletonCount();
    static std::size_t GetLoadedAnimationClipCount();

    static void Clear();

    // Rebuilds shaders whose source files changed on disk (or all of them when force is true).
    // Handles stay valid; a shader that fails to compile keeps its previous program.
    static ShaderReloadResult ReloadChangedShaders(bool force = false);
};

} // namespace Haki
