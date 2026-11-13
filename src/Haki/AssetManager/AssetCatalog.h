#pragma once

#include "Haki/AssetManager/AssetTypes.h"
#include "Haki/Serialization/SerializationError.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Haki
{

enum class CatalogAssetType
{
    Shader,
    Texture,
    Mesh,
    Material,
    Skeleton,
    AnimationClip
};

struct AssetCatalogEntry
{
    std::string LogicalId;
    AssetID RuntimeId = InvalidAssetID;
    CatalogAssetType Type = CatalogAssetType::Texture;
    std::filesystem::path Source;
    std::string MeshSelector;
    std::string NodeSelector;
    std::string SkinSelector;
    std::string AnimationSelector;
    std::string SkeletonAssetId;
    AssetID SkeletonRuntimeId = InvalidAssetID;
    bool SRGB = true;
    bool FlipY = true;
};

class AssetCatalog
{
public:
    bool Load(const std::filesystem::path &source, std::vector<SerializationError> &errors);
    void Clear();

    const AssetCatalogEntry *Find(std::string_view logicalId) const;
    const AssetCatalogEntry *Find(AssetID runtimeId) const;
    std::size_t GetCount() const { return entries_.size(); }

    static AssetID MakeRuntimeId(std::string_view logicalId);

private:
    std::vector<AssetCatalogEntry> entries_;
    std::unordered_map<std::string, std::size_t> logicalLookup_;
    std::unordered_map<AssetID, std::size_t> runtimeLookup_;
};

} // namespace Haki
