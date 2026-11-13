#include "Haki/AssetManager/AssetCatalog.h"

#include "Haki/Serialization/JsonUtils.h"

#include <array>
#include <cstdint>
#include <string>
#include <set>
#include <functional>

namespace Haki
{
namespace
{

bool ParseType(const std::string &value, CatalogAssetType &type)
{
    if (value == "shader")
    {
        type = CatalogAssetType::Shader;
    }
    else if (value == "texture")
    {
        type = CatalogAssetType::Texture;
    }
    else if (value == "mesh")
    {
        type = CatalogAssetType::Mesh;
    }
    else if (value == "material")
    {
        type = CatalogAssetType::Material;
    }
    else if (value == "skeleton")
    {
        type = CatalogAssetType::Skeleton;
    }
    else if (value == "animationClip")
    {
        type = CatalogAssetType::AnimationClip;
    }
    else
    {
        return false;
    }
    return true;
}

} // namespace

AssetID AssetCatalog::MakeRuntimeId(std::string_view logicalId)
{
    std::uint32_t hash = 2166136261u;
    for (const unsigned char character : logicalId)
    {
        hash ^= character;
        hash *= 16777619u;
    }
    return hash;
}

bool AssetCatalog::Load(const std::filesystem::path &source, std::vector<SerializationError> &errors)
{
    Clear();
    JsonUtils::Json document;
    // Flatten explicitly imported catalogs, retaining the declaring file's path base.
    document = {{"type", "haki.asset-catalog"}, {"version", 1}, {"assets", JsonUtils::Json::array()}};
    std::set<std::filesystem::path> active, visited;
    std::function<bool(std::filesystem::path)> collect = [&](std::filesystem::path path) {
        path = std::filesystem::weakly_canonical(path);
        if (active.contains(path)) { JsonUtils::AddError(errors, path, "/imports", "Catalog import cycle"); return false; }
        if (visited.contains(path)) return true;
        active.insert(path);
        JsonUtils::Json part;
        if (!JsonUtils::ReadFile(path, part, errors) ||
            !JsonUtils::ValidateFields(part, {"type", "version", "assets", "imports"}, path, "", errors) ||
            !JsonUtils::ValidateHeader(part, "haki.asset-catalog", path, errors)) return false;
        if (part.contains("imports")) {
            if (!part["imports"].is_array()) { JsonUtils::AddError(errors,path,"/imports","Expected array"); return false; }
            for (const auto &item : part["imports"]) {
                if (!item.is_string()) { JsonUtils::AddError(errors,path,"/imports","Expected catalog path"); return false; }
                if (!collect(path.parent_path()/item.get<std::string>())) return false;
            }
        }
        if (!part.contains("assets") || !part["assets"].is_array()) { JsonUtils::AddError(errors,path,"/assets","Expected array"); return false; }
        for (auto entry : part["assets"]) {
            if (entry.is_object() && entry.contains("source") && entry["source"].is_string())
                entry["source"] = (path.parent_path()/entry["source"].get<std::string>()).lexically_normal().string();
            document["assets"].push_back(entry);
        }
        active.erase(path); visited.insert(path); return true;
    };
    if (!collect(source)) return false;

    bool valid = JsonUtils::ValidateFields(document, {"type", "version", "assets"}, source, "", errors);
    valid = JsonUtils::ValidateHeader(document, "haki.asset-catalog", source, errors) && valid;
    if (!document.contains("assets") || !document["assets"].is_array())
    {
        JsonUtils::AddError(errors, source, "/assets", "Expected an array");
        return false;
    }

    const std::filesystem::path base = source.parent_path();
    for (std::size_t index = 0; index < document["assets"].size(); ++index)
    {
        const auto &jsonEntry = document["assets"][index];
        const std::string pointer = "/assets/" + std::to_string(index);
        valid = JsonUtils::ValidateFields(jsonEntry, {"id", "type", "source", "mesh", "node", "skin",
                                                   "animation", "skeleton", "srgb", "flipY"},
                                          source, pointer, errors) && valid;
        if (!jsonEntry.is_object() || !jsonEntry.contains("id") || !jsonEntry["id"].is_string() ||
            !jsonEntry.contains("type") || !jsonEntry["type"].is_string() || !jsonEntry.contains("source") ||
            !jsonEntry["source"].is_string())
        {
            JsonUtils::AddError(errors, source, pointer, "Asset requires string id, type, and source fields");
            valid = false;
            continue;
        }

        AssetCatalogEntry entry;
        entry.LogicalId = jsonEntry["id"].get<std::string>();
        entry.RuntimeId = MakeRuntimeId(entry.LogicalId);
        entry.Source = (base / jsonEntry["source"].get<std::string>()).lexically_normal();
        if (entry.LogicalId.empty() || entry.RuntimeId == InvalidAssetID)
        {
            JsonUtils::AddError(errors, source, pointer + "/id", "Asset ID is empty or hashes to a reserved value");
            valid = false;
            continue;
        }
        if (!ParseType(jsonEntry["type"].get<std::string>(), entry.Type))
        {
            JsonUtils::AddError(errors, source, pointer + "/type", "Unknown asset type");
            valid = false;
            continue;
        }
        if (jsonEntry.contains("mesh"))
        {
            if (!jsonEntry["mesh"].is_string())
            {
                JsonUtils::AddError(errors, source, pointer + "/mesh", "Expected a mesh name");
                valid = false;
            }
            else
            {
                entry.MeshSelector = jsonEntry["mesh"].get<std::string>();
            }
        }
        if (jsonEntry.contains("node"))
        {
            if (!jsonEntry["node"].is_string())
            {
                JsonUtils::AddError(errors, source, pointer + "/node", "Expected a node name or index selector");
                valid = false;
            }
            else
            {
                entry.NodeSelector = jsonEntry["node"].get<std::string>();
            }
        }
        if (jsonEntry.contains("skin"))
        {
            if (!jsonEntry["skin"].is_string())
            {
                JsonUtils::AddError(errors, source, pointer + "/skin", "Expected a skin name or index selector");
                valid = false;
            }
            else
            {
                entry.SkinSelector = jsonEntry["skin"].get<std::string>();
            }
        }
        if (jsonEntry.contains("animation"))
        {
            if (!jsonEntry["animation"].is_string())
            {
                JsonUtils::AddError(errors, source, pointer + "/animation", "Expected an animation name or index selector");
                valid = false;
            }
            else
            {
                entry.AnimationSelector = jsonEntry["animation"].get<std::string>();
            }
        }
        if (jsonEntry.contains("skeleton"))
        {
            if (!jsonEntry["skeleton"].is_string())
            {
                JsonUtils::AddError(errors, source, pointer + "/skeleton", "Expected a skeleton asset ID");
                valid = false;
            }
            else
            {
                entry.SkeletonAssetId = jsonEntry["skeleton"].get<std::string>();
                entry.SkeletonRuntimeId = MakeRuntimeId(entry.SkeletonAssetId);
            }
        }
        if (jsonEntry.contains("srgb") && jsonEntry["srgb"].is_boolean())
        {
            entry.SRGB = jsonEntry["srgb"].get<bool>();
        }
        else if (jsonEntry.contains("srgb"))
        {
            JsonUtils::AddError(errors, source, pointer + "/srgb", "Expected a boolean");
            valid = false;
        }
        if (jsonEntry.contains("flipY") && jsonEntry["flipY"].is_boolean())
        {
            entry.FlipY = jsonEntry["flipY"].get<bool>();
        }
        else if (jsonEntry.contains("flipY"))
        {
            JsonUtils::AddError(errors, source, pointer + "/flipY", "Expected a boolean");
            valid = false;
        }
        if (entry.Type == CatalogAssetType::Mesh && entry.MeshSelector.empty())
        {
            JsonUtils::AddError(errors, source, pointer + "/mesh", "Mesh assets require a named glTF mesh selector");
            valid = false;
        }
        if (entry.Type != CatalogAssetType::Mesh && jsonEntry.contains("mesh"))
        {
            JsonUtils::AddError(errors, source, pointer + "/mesh", "Only mesh assets may specify a mesh selector");
            valid = false;
        }
        if (entry.Type != CatalogAssetType::Mesh && entry.Type != CatalogAssetType::Skeleton &&
            jsonEntry.contains("node"))
        {
            JsonUtils::AddError(errors, source, pointer + "/node",
                                "Only mesh and skeleton assets may specify a node selector");
            valid = false;
        }
        if (entry.Type == CatalogAssetType::Skeleton &&
            (entry.SkinSelector.empty() || entry.NodeSelector.empty()))
        {
            JsonUtils::AddError(errors, source, pointer,
                                "Skeleton assets require glTF skin and skinned node selectors");
            valid = false;
        }
        if (entry.Type != CatalogAssetType::Skeleton && jsonEntry.contains("skin"))
        {
            JsonUtils::AddError(errors, source, pointer + "/skin", "Only skeleton assets may specify a skin selector");
            valid = false;
        }
        if (entry.Type == CatalogAssetType::AnimationClip &&
            (entry.AnimationSelector.empty() || entry.SkeletonAssetId.empty()))
        {
            JsonUtils::AddError(errors, source, pointer,
                                "Animation clip assets require animation and skeleton selectors");
            valid = false;
        }
        if (entry.Type != CatalogAssetType::AnimationClip &&
            (jsonEntry.contains("animation") || jsonEntry.contains("skeleton")))
        {
            JsonUtils::AddError(errors, source, pointer,
                                "Only animation clip assets may specify animation or skeleton fields");
            valid = false;
        }

        if (logicalLookup_.contains(entry.LogicalId))
        {
            JsonUtils::AddError(errors, source, pointer + "/id", "Duplicate logical asset ID");
            valid = false;
            continue;
        }
        if (const auto collision = runtimeLookup_.find(entry.RuntimeId); collision != runtimeLookup_.end())
        {
            JsonUtils::AddError(errors, source, pointer + "/id",
                                "Asset ID hash collides with '" + entries_[collision->second].LogicalId + "'");
            valid = false;
            continue;
        }

        logicalLookup_.emplace(entry.LogicalId, entries_.size());
        runtimeLookup_.emplace(entry.RuntimeId, entries_.size());
        entries_.push_back(std::move(entry));
    }

    for (std::size_t index = 0; index < entries_.size(); ++index)
    {
        AssetCatalogEntry &entry = entries_[index];
        if (entry.Type != CatalogAssetType::AnimationClip)
        {
            continue;
        }
        const auto skeleton = logicalLookup_.find(entry.SkeletonAssetId);
        if (skeleton == logicalLookup_.end() || entries_[skeleton->second].Type != CatalogAssetType::Skeleton)
        {
            JsonUtils::AddError(errors, source, "/assets/" + std::to_string(index) + "/skeleton",
                                "Animation clip skeleton must reference a skeleton asset");
            valid = false;
        }
    }

    if (!valid)
    {
        Clear();
    }
    return valid;
}

void AssetCatalog::Clear()
{
    entries_.clear();
    logicalLookup_.clear();
    runtimeLookup_.clear();
}

const AssetCatalogEntry *AssetCatalog::Find(std::string_view logicalId) const
{
    const auto found = logicalLookup_.find(std::string(logicalId));
    return found != logicalLookup_.end() ? &entries_[found->second] : nullptr;
}

const AssetCatalogEntry *AssetCatalog::Find(AssetID runtimeId) const
{
    const auto found = runtimeLookup_.find(runtimeId);
    return found != runtimeLookup_.end() ? &entries_[found->second] : nullptr;
}

} // namespace Haki
