#include "Haki/AssetManager/AssetManager.h"

#include "Haki/AssetLoader/AssetLoaderRegistry.h"
#include "Haki/AssetManager/MeshManager.h"
#include "Haki/AssetManager/MaterialManager.h"
#include "Haki/AssetManager/ShaderManager.h"
#include "Haki/AssetManager/TextureManager.h"
#include "Haki/AssetManager/Skeleton.h"
#include "Haki/AssetManager/AnimationClip.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace Haki
{
namespace
{

enum class AssetKind
{
    Texture,
    Shader,
    Mesh,
    Material,
    Skeleton,
    AnimationClip
};

struct AssetRecord
{
    AssetKind Kind;
    std::filesystem::path Source;
    std::vector<std::filesystem::path> SourceFiles;
    std::filesystem::file_time_type SourceTimestamp = std::filesystem::file_time_type::min();
};

std::filesystem::file_time_type NewestWriteTime(const std::vector<std::filesystem::path> &files)
{
    // file_clock's epoch is implementation-defined (libstdc++ uses 2174), so real times can be negative.
    std::filesystem::file_time_type newest = std::filesystem::file_time_type::min();
    for (const std::filesystem::path &file : files)
    {
        std::error_code error;
        const auto time = std::filesystem::last_write_time(file, error);
        if (!error)
        {
            newest = std::max(newest, time);
        }
    }
    return newest;
}

struct AssetManagerState
{
    Detail::TextureManager Textures;
    Detail::ShaderManager Shaders;
    Detail::MeshManager Meshes;
    Detail::MaterialManager Materials;
    Detail::ResourceManager<Skeleton> Skeletons;
    Detail::ResourceManager<AnimationClip> AnimationClips;
    Detail::AssetLoaderRegistry<Texture> TextureLoaders;
    Detail::AssetLoaderRegistry<Shader> ShaderLoaders;
    Detail::AssetLoaderRegistry<Mesh> MeshLoaders;
    Detail::AssetLoaderRegistry<Material> MaterialLoaders;
    Detail::AssetLoaderRegistry<Skeleton> SkeletonLoaders;
    Detail::AssetLoaderRegistry<AnimationClip> AnimationClipLoaders;
    std::unordered_map<AssetID, AssetRecord> Records;
};

std::unique_ptr<AssetManagerState> state;

template <typename Handle>
AssetResult<Handle> Failure(AssetLoadError error, std::string message)
{
    return {{}, error, std::move(message)};
}

template <typename Resource>
using ResourceHandle = AssetHandle<Resource>;

template <typename Resource>
AssetResult<ResourceHandle<Resource>> LoadAsset(
    const AssetLoadRequest &request,
    AssetKind kind,
    Detail::ResourceManager<Resource> &manager,
    Detail::AssetLoaderRegistry<Resource> &loaders)
{
    using Handle = ResourceHandle<Resource>;

    if (request.Id == InvalidAssetID)
    {
        return Failure<Handle>(AssetLoadError::InvalidRequest, "Asset ID is invalid");
    }

    const std::filesystem::path source = request.Source.lexically_normal();
    if (const auto record = state->Records.find(request.Id); record != state->Records.end())
    {
        if (record->second.Kind != kind || record->second.Source != source)
        {
            return Failure<Handle>(AssetLoadError::AssetIdConflict,
                                   "Asset ID is already associated with another asset");
        }

        const Handle existing = manager.Find(request.Id);
        if (existing.IsValid())
        {
            return {existing, AssetLoadError::None, {}};
        }
    }

    AssetLoader<Resource> *loader = loaders.Find(request);
    if (!loader)
    {
        return Failure<Handle>(AssetLoadError::NoCompatibleLoader, "No compatible asset loader is registered");
    }

    AssetLoaderResult<Resource> loaded = loader->Load(request);
    if (!loaded)
    {
        const AssetLoadError error = loaded.Error == AssetLoadError::None ? AssetLoadError::LoaderFailed : loaded.Error;
        std::string message = std::move(loaded.Message);
        if (message.empty())
        {
            message = "Asset loader failed to create a resource";
        }
        return Failure<Handle>(error, std::move(message));
    }

    const Handle handle = manager.Store(request.Id, std::move(loaded.Resource));
    if (!handle.IsValid())
    {
        return Failure<Handle>(AssetLoadError::LoaderFailed, "Asset manager could not store the resource");
    }

    std::vector<std::filesystem::path> sourceFiles = loader->GetSourceFiles(request);
    const std::filesystem::file_time_type timestamp = NewestWriteTime(sourceFiles);
    state->Records.insert_or_assign(request.Id, AssetRecord{kind, source, std::move(sourceFiles), timestamp});
    return {handle, AssetLoadError::None, {}};
}

template <typename Resource>
bool UnloadAsset(ResourceHandle<Resource> handle, Detail::ResourceManager<Resource> &manager)
{
    const auto id = manager.GetAssetId(handle);
    if (!id || !manager.Unload(handle))
    {
        return false;
    }
    state->Records.erase(*id);
    return true;
}

} // namespace

bool AssetManager::Initialize()
{
    if (!state)
    {
        state = std::make_unique<AssetManagerState>();
    }
    return true;
}

void AssetManager::Shutdown()
{
    if (!state)
    {
        return;
    }
    Clear();
    state.reset();
}

bool AssetManager::IsInitialized()
{
    return state != nullptr;
}

bool AssetManager::RegisterTextureLoader(std::unique_ptr<AssetLoader<Texture>> loader)
{
    return state && state->TextureLoaders.Register(std::move(loader));
}

bool AssetManager::RegisterShaderLoader(std::unique_ptr<AssetLoader<Shader>> loader)
{
    return state && state->ShaderLoaders.Register(std::move(loader));
}

bool AssetManager::RegisterMeshLoader(std::unique_ptr<AssetLoader<Mesh>> loader)
{
    return state && state->MeshLoaders.Register(std::move(loader));
}

bool AssetManager::RegisterMaterialLoader(std::unique_ptr<AssetLoader<Material>> loader)
{
    return state && state->MaterialLoaders.Register(std::move(loader));
}

bool AssetManager::RegisterSkeletonLoader(std::unique_ptr<AssetLoader<Skeleton>> loader)
{
    return state && state->SkeletonLoaders.Register(std::move(loader));
}

bool AssetManager::RegisterAnimationClipLoader(std::unique_ptr<AssetLoader<AnimationClip>> loader)
{
    return state && state->AnimationClipLoaders.Register(std::move(loader));
}

AssetResult<TextureHandle> AssetManager::LoadTexture(const AssetLoadRequest &request)
{
    return state ? LoadAsset(request, AssetKind::Texture, state->Textures, state->TextureLoaders)
                 : Failure<TextureHandle>(AssetLoadError::NotInitialized, "Asset manager is not initialized");
}

AssetResult<ShaderHandle> AssetManager::LoadShader(const AssetLoadRequest &request)
{
    return state ? LoadAsset(request, AssetKind::Shader, state->Shaders, state->ShaderLoaders)
                 : Failure<ShaderHandle>(AssetLoadError::NotInitialized, "Asset manager is not initialized");
}

AssetResult<MeshHandle> AssetManager::LoadMesh(const AssetLoadRequest &request)
{
    return state ? LoadAsset(request, AssetKind::Mesh, state->Meshes, state->MeshLoaders)
                 : Failure<MeshHandle>(AssetLoadError::NotInitialized, "Asset manager is not initialized");
}

AssetResult<MaterialHandle> AssetManager::LoadMaterial(const AssetLoadRequest &request)
{
    return state ? LoadAsset(request, AssetKind::Material, state->Materials, state->MaterialLoaders)
                 : Failure<MaterialHandle>(AssetLoadError::NotInitialized, "Asset manager is not initialized");
}

AssetResult<SkeletonHandle> AssetManager::LoadSkeleton(const AssetLoadRequest &request)
{
    return state ? LoadAsset(request, AssetKind::Skeleton, state->Skeletons, state->SkeletonLoaders)
                 : Failure<SkeletonHandle>(AssetLoadError::NotInitialized, "Asset manager is not initialized");
}

AssetResult<AnimationClipHandle> AssetManager::LoadAnimationClip(const AssetLoadRequest &request)
{
    return state ? LoadAsset(request, AssetKind::AnimationClip, state->AnimationClips, state->AnimationClipLoaders)
                 : Failure<AnimationClipHandle>(AssetLoadError::NotInitialized, "Asset manager is not initialized");
}

Texture *AssetManager::GetTexture(TextureHandle handle)
{
    return state ? state->Textures.Get(handle) : nullptr;
}

Shader *AssetManager::GetShader(ShaderHandle handle)
{
    return state ? state->Shaders.Get(handle) : nullptr;
}

Mesh *AssetManager::GetMesh(MeshHandle handle)
{
    return state ? state->Meshes.Get(handle) : nullptr;
}

Material *AssetManager::GetMaterial(MaterialHandle handle)
{
    return state ? state->Materials.Get(handle) : nullptr;
}

Skeleton *AssetManager::GetSkeleton(SkeletonHandle handle)
{
    return state ? state->Skeletons.Get(handle) : nullptr;
}

AnimationClip *AssetManager::GetAnimationClip(AnimationClipHandle handle)
{
    return state ? state->AnimationClips.Get(handle) : nullptr;
}

bool AssetManager::UnloadTexture(TextureHandle handle)
{
    return state && UnloadAsset(handle, state->Textures);
}

bool AssetManager::UnloadShader(ShaderHandle handle)
{
    return state && UnloadAsset(handle, state->Shaders);
}

bool AssetManager::UnloadMesh(MeshHandle handle)
{
    return state && UnloadAsset(handle, state->Meshes);
}

bool AssetManager::UnloadMaterial(MaterialHandle handle)
{
    return state && UnloadAsset(handle, state->Materials);
}

bool AssetManager::UnloadSkeleton(SkeletonHandle handle)
{
    return state && UnloadAsset(handle, state->Skeletons);
}

bool AssetManager::UnloadAnimationClip(AnimationClipHandle handle)
{
    return state && UnloadAsset(handle, state->AnimationClips);
}

bool AssetManager::IsTextureLoaded(AssetID id)
{
    return state && state->Textures.IsLoaded(id);
}

bool AssetManager::IsShaderLoaded(AssetID id)
{
    return state && state->Shaders.IsLoaded(id);
}

bool AssetManager::IsMeshLoaded(AssetID id)
{
    return state && state->Meshes.IsLoaded(id);
}

bool AssetManager::IsMaterialLoaded(AssetID id)
{
    return state && state->Materials.IsLoaded(id);
}

bool AssetManager::IsSkeletonLoaded(AssetID id)
{
    return state && state->Skeletons.IsLoaded(id);
}

bool AssetManager::IsAnimationClipLoaded(AssetID id)
{
    return state && state->AnimationClips.IsLoaded(id);
}

std::size_t AssetManager::GetLoadedTextureCount()
{
    return state ? state->Textures.GetLoadedCount() : 0;
}

std::size_t AssetManager::GetLoadedShaderCount()
{
    return state ? state->Shaders.GetLoadedCount() : 0;
}

std::size_t AssetManager::GetLoadedMeshCount()
{
    return state ? state->Meshes.GetLoadedCount() : 0;
}

std::size_t AssetManager::GetLoadedMaterialCount()
{
    return state ? state->Materials.GetLoadedCount() : 0;
}

std::size_t AssetManager::GetLoadedSkeletonCount()
{
    return state ? state->Skeletons.GetLoadedCount() : 0;
}

std::size_t AssetManager::GetLoadedAnimationClipCount()
{
    return state ? state->AnimationClips.GetLoadedCount() : 0;
}

void AssetManager::Clear()
{
    if (!state)
    {
        return;
    }
    state->Textures.Clear();
    state->Shaders.Clear();
    state->Meshes.Clear();
    state->Materials.Clear();
    state->Skeletons.Clear();
    state->AnimationClips.Clear();
    state->Records.clear();
}

ShaderReloadResult AssetManager::ReloadChangedShaders(bool force)
{
    ShaderReloadResult result;
    if (!state)
    {
        return result;
    }

    for (auto &[id, record] : state->Records)
    {
        if (record.Kind != AssetKind::Shader || record.SourceFiles.empty())
        {
            continue;
        }
        const std::filesystem::file_time_type timestamp = NewestWriteTime(record.SourceFiles);
        if (!force && timestamp <= record.SourceTimestamp)
        {
            continue;
        }
        // Remember the timestamp even if the rebuild fails, so a broken file is reported once per save.
        record.SourceTimestamp = timestamp;

        Shader *shader = state->Shaders.Get(state->Shaders.Find(id));
        const AssetLoadRequest request{id, record.Source};
        AssetLoader<Shader> *loader = state->ShaderLoaders.Find(request);
        if (!shader || !loader)
        {
            continue;
        }

        AssetLoaderResult<Shader> loaded = loader->Load(request);
        if (!loaded)
        {
            ++result.Failed;
            result.Errors.push_back(loaded.Message.empty() ? "Shader reload failed: " + record.Source.string()
                                                           : std::move(loaded.Message));
            continue;
        }
        *shader = std::move(*loaded.Resource);
        ++result.Reloaded;
    }
    return result;
}

} // namespace Haki
