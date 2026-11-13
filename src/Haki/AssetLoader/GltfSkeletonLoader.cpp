#include <cgltf.h>

#include "Haki/AssetLoader/GltfSkeletonLoader.h"

#include "Haki/AssetManager/AssetCatalog.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>

namespace Haki
{
namespace
{

const cgltf_skin *SelectSkin(const cgltf_data &data, std::string_view selector)
{
    if (selector.starts_with('#'))
    {
        std::size_t index = 0;
        const auto parsed = std::from_chars(selector.data() + 1, selector.data() + selector.size(), index);
        return parsed.ec == std::errc{} && parsed.ptr == selector.data() + selector.size() &&
                       index < data.skins_count
                   ? &data.skins[index]
                   : nullptr;
    }
    for (cgltf_size index = 0; index < data.skins_count; ++index)
    {
        if (data.skins[index].name && selector == data.skins[index].name)
        {
            return &data.skins[index];
        }
    }
    return nullptr;
}

const cgltf_node *SelectNode(const cgltf_data &data, std::string_view selector)
{
    if (selector.starts_with('#'))
    {
        std::size_t index = 0;
        const auto parsed = std::from_chars(selector.data() + 1, selector.data() + selector.size(), index);
        return parsed.ec == std::errc{} && parsed.ptr == selector.data() + selector.size() &&
                       index < data.nodes_count
                   ? &data.nodes[index]
                   : nullptr;
    }
    for (cgltf_size index = 0; index < data.nodes_count; ++index)
    {
        if (data.nodes[index].name && selector == data.nodes[index].name)
        {
            return &data.nodes[index];
        }
    }
    return nullptr;
}

bool DecomposeNode(const cgltf_node &node, PoseNode &poseNode)
{
    if (!node.has_matrix)
    {
        poseNode.BindTranslation = node.has_translation
                                       ? glm::vec3(node.translation[0], node.translation[1], node.translation[2])
                                       : glm::vec3(0.0f);
        poseNode.BindRotation = node.has_rotation
                                    ? glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2])
                                    : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        poseNode.BindScale = node.has_scale ? glm::vec3(node.scale[0], node.scale[1], node.scale[2])
                                            : glm::vec3(1.0f);
        return true;
    }

    const glm::mat4 local = glm::make_mat4(node.matrix);
    poseNode.BindTranslation = glm::vec3(local[3]);
    poseNode.BindScale = {glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])),
                          glm::length(glm::vec3(local[2]))};
    if (poseNode.BindScale.x == 0.0f || poseNode.BindScale.y == 0.0f || poseNode.BindScale.z == 0.0f)
    {
        return false;
    }
    glm::mat3 rotation(local);
    rotation[0] /= poseNode.BindScale.x;
    rotation[1] /= poseNode.BindScale.y;
    rotation[2] /= poseNode.BindScale.z;
    if (glm::determinant(rotation) < 0.0f)
    {
        poseNode.BindScale.x = -poseNode.BindScale.x;
        rotation[0] = -rotation[0];
    }
    poseNode.BindRotation = glm::quat_cast(rotation);
    return true;
}

} // namespace

GltfSkeletonLoader::GltfSkeletonLoader(std::shared_ptr<const AssetCatalog> catalog,
                                       std::shared_ptr<GltfDocumentCache> cache)
    : catalog_(std::move(catalog)), cache_(cache ? std::move(cache) : std::make_shared<GltfDocumentCache>())
{
}

bool GltfSkeletonLoader::Supports(const AssetLoadRequest &request) const
{
    std::string extension = request.Source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".gltf" || extension == ".glb";
}

AssetLoaderResult<Skeleton> GltfSkeletonLoader::Load(const AssetLoadRequest &request)
{
    const AssetCatalogEntry *entry = catalog_ ? catalog_->Find(request.Id) : nullptr;
    if (!entry || entry->Type != CatalogAssetType::Skeleton || entry->SkinSelector.empty() ||
        entry->NodeSelector.empty())
    {
        return {nullptr, AssetLoadError::LoaderFailed, "glTF skeleton catalog entry requires a skin selector"};
    }
    std::string error;
    const auto document = cache_->Load(request.Source, error);
    if (!document)
    {
        return {nullptr, AssetLoadError::LoaderFailed, std::move(error)};
    }
    const cgltf_data &data = *document->GetData();
    const cgltf_skin *skin = SelectSkin(data, entry->SkinSelector);
    const cgltf_node *meshNode = SelectNode(data, entry->NodeSelector);
    if (!skin || skin->joints_count == 0 ||
        (skin->inverse_bind_matrices && skin->inverse_bind_matrices->count < skin->joints_count) ||
        !meshNode || meshNode->skin != skin)
    {
        return {nullptr, AssetLoadError::LoaderFailed,
                "glTF skin '" + entry->SkinSelector + "' was not found or is invalid"};
    }

    std::unordered_set<const cgltf_node *> included;
    for (cgltf_size index = 0; index < skin->joints_count; ++index)
    {
        for (const cgltf_node *node = skin->joints[index]; node; node = node->parent)
        {
            included.insert(node);
        }
    }

    auto skeleton = std::make_unique<Skeleton>();
    glm::mat4 meshBindWorld(1.0f);
    cgltf_node_transform_world(meshNode, glm::value_ptr(meshBindWorld));
    const float meshDeterminant = glm::determinant(glm::mat3(meshBindWorld));
    if (!std::isfinite(meshDeterminant) || std::abs(meshDeterminant) < 0.000001f)
    {
        return {nullptr, AssetLoadError::LoaderFailed, "glTF skeleton mesh node has a singular bind transform"};
    }
    skeleton->InverseMeshBindWorld = glm::inverse(meshBindWorld);
    std::unordered_map<const cgltf_node *, std::uint32_t> nodeIndices;
    std::function<bool(const cgltf_node *)> addNode = [&](const cgltf_node *node) {
        if (nodeIndices.contains(node))
        {
            return true;
        }
        std::int32_t parentIndex = -1;
        if (node->parent && included.contains(node->parent))
        {
            if (!addNode(node->parent))
            {
                return false;
            }
            parentIndex = static_cast<std::int32_t>(nodeIndices[node->parent]);
        }
        PoseNode poseNode;
        poseNode.Name = node->name ? node->name : "";
        poseNode.ParentIndex = parentIndex;
        poseNode.SourceNodeIndex = static_cast<std::uint32_t>(node - data.nodes);
        if (!DecomposeNode(*node, poseNode))
        {
            return false;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(skeleton->Nodes.size());
        nodeIndices.emplace(node, index);
        if (!poseNode.Name.empty())
        {
            skeleton->NodeNameToIndex.emplace(poseNode.Name, index);
        }
        skeleton->Nodes.push_back(std::move(poseNode));
        return true;
    };
    for (cgltf_size index = 0; index < skin->joints_count; ++index)
    {
        if (!addNode(skin->joints[index]))
        {
            return {nullptr, AssetLoadError::LoaderFailed, "Could not decompose a glTF skeleton node transform"};
        }
    }

    skeleton->Joints.reserve(skin->joints_count);
    for (cgltf_size index = 0; index < skin->joints_count; ++index)
    {
        Joint joint;
        joint.NodeIndex = nodeIndices.at(skin->joints[index]);
        if (skin->inverse_bind_matrices &&
            !cgltf_accessor_read_float(skin->inverse_bind_matrices, index, glm::value_ptr(joint.InverseBindMatrix), 16))
        {
            return {nullptr, AssetLoadError::LoaderFailed, "Could not decode a glTF inverse bind matrix"};
        }
        const char *name = skin->joints[index]->name;
        if (name)
        {
            skeleton->JointNameToIndex.emplace(name, static_cast<std::uint32_t>(index));
        }
        skeleton->Joints.push_back(joint);
    }
    return {std::move(skeleton), AssetLoadError::None, {}};
}

} // namespace Haki
