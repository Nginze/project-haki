#include <cgltf.h>

#include "Haki/AssetLoader/GltfMeshLoader.h"

#include "Haki/AssetManager/AssetCatalog.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Haki
{
namespace
{

struct ImportedVertex
{
    float Position[3]{0.0f, 0.0f, 0.0f};
    float Normal[3]{0.0f, 0.0f, 1.0f};
    float TexCoord[2]{0.0f, 0.0f};
    float TexCoord1[2]{0.0f, 0.0f};
    float Tangent[4]{1.0f, 0.0f, 0.0f, 1.0f};
    float Color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    std::uint16_t Joints[4]{};
    float Weights[4]{};
};

const cgltf_accessor *FindAttribute(const cgltf_primitive &primitive, cgltf_attribute_type type)
{
    for (cgltf_size index = 0; index < primitive.attributes_count; ++index)
    {
        if (primitive.attributes[index].type == type && primitive.attributes[index].index == 0)
        {
            return primitive.attributes[index].data;
        }
    }
    return nullptr;
}

const cgltf_accessor *FindAttributeSet(const cgltf_primitive &primitive, cgltf_attribute_type type, cgltf_int set)
{
    for (cgltf_size index = 0; index < primitive.attributes_count; ++index)
    {
        if (primitive.attributes[index].type == type && primitive.attributes[index].index == set)
        {
            return primitive.attributes[index].data;
        }
    }
    return nullptr;
}

bool HasAttributeSet(const cgltf_primitive &primitive, cgltf_attribute_type type, cgltf_int set)
{
    for (cgltf_size index = 0; index < primitive.attributes_count; ++index)
    {
        if (primitive.attributes[index].type == type && primitive.attributes[index].index == set)
        {
            return true;
        }
    }
    return false;
}

bool ReadAttribute(const cgltf_accessor *accessor, cgltf_size index, float *destination, cgltf_size components)
{
    return !accessor || cgltf_accessor_read_float(accessor, index, destination, components) != 0;
}

void GenerateTangents(std::vector<ImportedVertex> &vertices, std::uint32_t baseVertex, std::uint32_t vertexCount,
                      const std::vector<std::uint32_t> &indices, std::uint32_t firstIndex, std::uint32_t indexCount)
{
    std::vector<glm::vec3> tangents(vertexCount, glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(vertexCount, glm::vec3(0.0f));
    for (std::uint32_t index = 0; index + 2 < indexCount; index += 3)
    {
        const std::uint32_t i0 = indices[firstIndex + index];
        const std::uint32_t i1 = indices[firstIndex + index + 1];
        const std::uint32_t i2 = indices[firstIndex + index + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
        {
            continue;
        }
        const ImportedVertex &v0 = vertices[baseVertex + i0];
        const ImportedVertex &v1 = vertices[baseVertex + i1];
        const ImportedVertex &v2 = vertices[baseVertex + i2];
        const glm::vec3 p0(v0.Position[0], v0.Position[1], v0.Position[2]);
        const glm::vec3 p1(v1.Position[0], v1.Position[1], v1.Position[2]);
        const glm::vec3 p2(v2.Position[0], v2.Position[1], v2.Position[2]);
        const glm::vec2 uv0(v0.TexCoord[0], v0.TexCoord[1]);
        const glm::vec2 uv1(v1.TexCoord[0], v1.TexCoord[1]);
        const glm::vec2 uv2(v2.TexCoord[0], v2.TexCoord[1]);
        const glm::vec3 edge1 = p1 - p0;
        const glm::vec3 edge2 = p2 - p0;
        const glm::vec2 delta1 = uv1 - uv0;
        const glm::vec2 delta2 = uv2 - uv0;
        const float denominator = delta1.x * delta2.y - delta1.y * delta2.x;
        if (std::abs(denominator) < 0.000001f)
        {
            continue;
        }
        const float inverse = 1.0f / denominator;
        const glm::vec3 tangent = (edge1 * delta2.y - edge2 * delta1.y) * inverse;
        const glm::vec3 bitangent = (edge2 * delta1.x - edge1 * delta2.x) * inverse;
        for (const std::uint32_t vertex : {i0, i1, i2})
        {
            tangents[vertex] += tangent;
            bitangents[vertex] += bitangent;
        }
    }

    for (std::uint32_t index = 0; index < vertexCount; ++index)
    {
        ImportedVertex &vertex = vertices[baseVertex + index];
        const glm::vec3 normal = glm::normalize(glm::vec3(vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]));
        glm::vec3 tangent = tangents[index] - normal * glm::dot(normal, tangents[index]);
        if (glm::length(tangent) < 0.000001f)
        {
            continue;
        }
        tangent = glm::normalize(tangent);
        vertex.Tangent[0] = tangent.x;
        vertex.Tangent[1] = tangent.y;
        vertex.Tangent[2] = tangent.z;
        vertex.Tangent[3] = glm::dot(glm::cross(normal, tangent), bitangents[index]) < 0.0f ? -1.0f : 1.0f;
    }
}

} // namespace

GltfMeshLoader::GltfMeshLoader(std::shared_ptr<const AssetCatalog> catalog)
    : GltfMeshLoader(std::move(catalog), std::make_shared<GltfDocumentCache>())
{
}

GltfMeshLoader::GltfMeshLoader(std::shared_ptr<const AssetCatalog> catalog,
                               std::shared_ptr<GltfDocumentCache> cache)
    : catalog_(std::move(catalog)), cache_(cache ? std::move(cache) : std::make_shared<GltfDocumentCache>())
{
}

bool GltfMeshLoader::Supports(const AssetLoadRequest &request) const
{
    std::string extension = request.Source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".gltf" || extension == ".glb";
}

AssetLoaderResult<Mesh> GltfMeshLoader::Load(const AssetLoadRequest &request)
{
    const AssetCatalogEntry *entry = catalog_ ? catalog_->Find(request.Id) : nullptr;
    if (!entry || entry->Type != CatalogAssetType::Mesh || entry->MeshSelector.empty())
    {
        return {nullptr, AssetLoadError::LoaderFailed, "glTF mesh catalog entry requires a named mesh selector"};
    }

    std::string cacheError;
    const std::shared_ptr<const GltfDocument> document = cache_->Load(request.Source, cacheError);
    if (!document)
    {
        return {nullptr, AssetLoadError::LoaderFailed, std::move(cacheError)};
    }
    const cgltf_data *data = document->GetData();

    const cgltf_mesh *selectedMesh = nullptr;
    if (entry->MeshSelector.starts_with('#'))
    {
        std::size_t index = 0;
        const std::string_view selector(entry->MeshSelector);
        const auto parsed = std::from_chars(selector.data() + 1, selector.data() + selector.size(), index);
        if (parsed.ec == std::errc{} && parsed.ptr == selector.data() + selector.size() && index < data->meshes_count)
        {
            selectedMesh = &data->meshes[index];
        }
    }
    else
    {
        for (cgltf_size index = 0; index < data->meshes_count; ++index)
        {
            if (data->meshes[index].name && entry->MeshSelector == data->meshes[index].name)
            {
                selectedMesh = &data->meshes[index];
                break;
            }
        }
    }
    if (!selectedMesh)
    {
        return {nullptr, AssetLoadError::LoaderFailed,
                "glTF mesh '" + entry->MeshSelector + "' was not found in " + request.Source.string()};
    }

    const cgltf_node *selectedNode = nullptr;
    if (!entry->NodeSelector.empty())
    {
        if (entry->NodeSelector.starts_with('#'))
        {
            std::size_t index = 0;
            const std::string_view selector(entry->NodeSelector);
            const auto parsed = std::from_chars(selector.data() + 1, selector.data() + selector.size(), index);
            if (parsed.ec == std::errc{} && parsed.ptr == selector.data() + selector.size() &&
                index < data->nodes_count)
            {
                selectedNode = &data->nodes[index];
            }
        }
        else
        {
            for (cgltf_size index = 0; index < data->nodes_count; ++index)
            {
                if (data->nodes[index].name && entry->NodeSelector == data->nodes[index].name)
                {
                    selectedNode = &data->nodes[index];
                    break;
                }
            }
        }
        if (!selectedNode || selectedNode->mesh != selectedMesh || !selectedNode->skin)
        {
            return {nullptr, AssetLoadError::LoaderFailed,
                    "glTF skinned node '" + entry->NodeSelector + "' was not found or does not use the selected mesh"};
        }
    }

    std::vector<glm::mat4> skinMatrices;
    if (selectedNode)
    {
        const cgltf_skin &skin = *selectedNode->skin;
        if (skin.joints_count == 0 ||
            (skin.inverse_bind_matrices && skin.inverse_bind_matrices->count < skin.joints_count))
        {
            return {nullptr, AssetLoadError::LoaderFailed, "glTF skin has invalid joints or inverse bind matrices"};
        }
        glm::mat4 meshWorld(1.0f);
        cgltf_node_transform_world(selectedNode, glm::value_ptr(meshWorld));
        const float determinant = glm::determinant(glm::mat3(meshWorld));
        if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001f)
        {
            return {nullptr, AssetLoadError::LoaderFailed, "glTF skinned node has a singular world transform"};
        }
        const glm::mat4 inverseMeshWorld = glm::inverse(meshWorld);
        skinMatrices.reserve(skin.joints_count);
        for (cgltf_size jointIndex = 0; jointIndex < skin.joints_count; ++jointIndex)
        {
            glm::mat4 jointWorld(1.0f);
            cgltf_node_transform_world(skin.joints[jointIndex], glm::value_ptr(jointWorld));
            glm::mat4 inverseBind(1.0f);
            if (skin.inverse_bind_matrices &&
                !cgltf_accessor_read_float(skin.inverse_bind_matrices, jointIndex, glm::value_ptr(inverseBind), 16))
            {
                return {nullptr, AssetLoadError::LoaderFailed, "Could not decode glTF inverse bind matrix"};
            }
            skinMatrices.push_back(inverseMeshWorld * jointWorld * inverseBind);
        }
    }

    std::vector<ImportedVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Submesh> submeshes;
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    bool meshIsSkinned = false;
    std::uint32_t maximumJointIndex = 0;

    for (cgltf_size primitiveIndex = 0; primitiveIndex < selectedMesh->primitives_count; ++primitiveIndex)
    {
        const cgltf_primitive &primitive = selectedMesh->primitives[primitiveIndex];
        if (primitive.type != cgltf_primitive_type_triangles)
        {
            return {nullptr, AssetLoadError::LoaderFailed, "Only triangle glTF primitives are supported"};
        }
        const cgltf_accessor *positions = FindAttribute(primitive, cgltf_attribute_type_position);
        if (!positions)
        {
            return {nullptr, AssetLoadError::LoaderFailed, "glTF primitive has no POSITION attribute"};
        }
        const cgltf_accessor *normals = FindAttribute(primitive, cgltf_attribute_type_normal);
        const cgltf_accessor *texCoords = FindAttribute(primitive, cgltf_attribute_type_texcoord);
        // Second UV set (lightmaps, AO, detail maps); materials pick a set per texture.
        const cgltf_accessor *texCoords1 = FindAttributeSet(primitive, cgltf_attribute_type_texcoord, 1);
        if (texCoords1 && texCoords1->count != positions->count)
        {
            texCoords1 = nullptr;
        }
        const cgltf_accessor *tangents = FindAttribute(primitive, cgltf_attribute_type_tangent);
        const cgltf_accessor *colors = FindAttribute(primitive, cgltf_attribute_type_color);
        const cgltf_accessor *joints = FindAttribute(primitive, cgltf_attribute_type_joints);
        const cgltf_accessor *weights = FindAttribute(primitive, cgltf_attribute_type_weights);
        if (HasAttributeSet(primitive, cgltf_attribute_type_joints, 1))
        {
            return {nullptr, AssetLoadError::LoaderFailed, "JOINTS_1 is not supported"};
        }
        if (selectedNode && (!joints || !weights || joints->count != positions->count || weights->count != positions->count))
        {
            return {nullptr, AssetLoadError::LoaderFailed,
                    "glTF skinned primitive requires matching JOINTS_0 and WEIGHTS_0 attributes"};
        }
        const bool preserveSkinning = !selectedNode && (joints || weights);
        if (preserveSkinning && (!joints || !weights || joints->count != positions->count ||
                                 weights->count != positions->count))
        {
            return {nullptr, AssetLoadError::LoaderFailed,
                    "glTF skinned primitive requires matching JOINTS_0 and WEIGHTS_0 attributes"};
        }
        if (primitiveIndex > 0 && meshIsSkinned != preserveSkinning)
        {
            return {nullptr, AssetLoadError::LoaderFailed, "A glTF mesh cannot mix skinned and unskinned primitives"};
        }
        meshIsSkinned = meshIsSkinned || preserveSkinning;

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(vertices.size());
        const std::uint32_t firstIndex = static_cast<std::uint32_t>(indices.size());
        vertices.resize(vertices.size() + positions->count);
        for (cgltf_size vertexIndex = 0; vertexIndex < positions->count; ++vertexIndex)
        {
            ImportedVertex &vertex = vertices[baseVertex + vertexIndex];
            if (!ReadAttribute(positions, vertexIndex, vertex.Position, 3) ||
                !ReadAttribute(normals, vertexIndex, vertex.Normal, 3) ||
                !ReadAttribute(texCoords, vertexIndex, vertex.TexCoord, 2) ||
                !ReadAttribute(texCoords1 ? texCoords1 : texCoords, vertexIndex, vertex.TexCoord1, 2) ||
                !ReadAttribute(tangents, vertexIndex, vertex.Tangent, 4) ||
                !ReadAttribute(colors, vertexIndex, vertex.Color, 4))
            {
                return {nullptr, AssetLoadError::LoaderFailed, "Could not decode a glTF vertex attribute"};
            }
            if (selectedNode)
            {
                std::array<cgltf_uint, 4> jointIndices{};
                std::array<float, 4> jointWeights{};
                if (!cgltf_accessor_read_uint(joints, vertexIndex, jointIndices.data(), jointIndices.size()) ||
                    !cgltf_accessor_read_float(weights, vertexIndex, jointWeights.data(), jointWeights.size()))
                {
                    return {nullptr, AssetLoadError::LoaderFailed, "Could not decode glTF skin attributes"};
                }
                float weightSum = 0.0f;
                glm::mat4 blended(0.0f);
                for (std::size_t influence = 0; influence < jointIndices.size(); ++influence)
                {
                    if (jointIndices[influence] >= skinMatrices.size() || !std::isfinite(jointWeights[influence]) ||
                        jointWeights[influence] < 0.0f)
                    {
                        return {nullptr, AssetLoadError::LoaderFailed, "glTF skin contains an invalid joint or weight"};
                    }
                    weightSum += jointWeights[influence];
                    blended += skinMatrices[jointIndices[influence]] * jointWeights[influence];
                }
                if (!std::isfinite(weightSum) || weightSum < 0.000001f)
                {
                    return {nullptr, AssetLoadError::LoaderFailed, "glTF skin vertex has no joint weight"};
                }
                blended /= weightSum;

                const glm::vec4 skinnedPosition = blended * glm::vec4(vertex.Position[0], vertex.Position[1],
                                                                       vertex.Position[2], 1.0f);
                vertex.Position[0] = skinnedPosition.x;
                vertex.Position[1] = skinnedPosition.y;
                vertex.Position[2] = skinnedPosition.z;
                const glm::mat3 linear(blended);
                const float linearDeterminant = glm::determinant(linear);
                if (!std::isfinite(linearDeterminant) || std::abs(linearDeterminant) < 0.000001f)
                {
                    return {nullptr, AssetLoadError::LoaderFailed, "glTF skin produced a singular vertex transform"};
                }
                if (normals)
                {
                    const glm::vec3 normal = glm::normalize(glm::inverseTranspose(linear) *
                        glm::vec3(vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]));
                    vertex.Normal[0] = normal.x;
                    vertex.Normal[1] = normal.y;
                    vertex.Normal[2] = normal.z;
                }
                if (tangents)
                {
                    glm::vec3 tangent = linear * glm::vec3(vertex.Tangent[0], vertex.Tangent[1], vertex.Tangent[2]);
                    const glm::vec3 normal(vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]);
                    tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));
                    vertex.Tangent[0] = tangent.x;
                    vertex.Tangent[1] = tangent.y;
                    vertex.Tangent[2] = tangent.z;
                    if (linearDeterminant < 0.0f)
                    {
                        vertex.Tangent[3] = -vertex.Tangent[3];
                    }
                }
            }
            else if (preserveSkinning)
            {
                std::array<cgltf_uint, 4> jointIndices{};
                if (!cgltf_accessor_read_uint(joints, vertexIndex, jointIndices.data(), jointIndices.size()) ||
                    !cgltf_accessor_read_float(weights, vertexIndex, vertex.Weights, 4))
                {
                    return {nullptr, AssetLoadError::LoaderFailed, "Could not decode glTF skin attributes"};
                }
                float weightSum = 0.0f;
                for (std::size_t influence = 0; influence < jointIndices.size(); ++influence)
                {
                    if (jointIndices[influence] > std::numeric_limits<std::uint16_t>::max() ||
                        !std::isfinite(vertex.Weights[influence]) || vertex.Weights[influence] < 0.0f)
                    {
                        return {nullptr, AssetLoadError::LoaderFailed, "glTF skin contains an invalid joint or weight"};
                    }
                    vertex.Joints[influence] = static_cast<std::uint16_t>(jointIndices[influence]);
                    weightSum += vertex.Weights[influence];
                    maximumJointIndex = std::max(maximumJointIndex,
                                                 static_cast<std::uint32_t>(jointIndices[influence]));
                }
                if (!std::isfinite(weightSum) || weightSum < 0.000001f)
                {
                    return {nullptr, AssetLoadError::LoaderFailed, "glTF skin vertex has no joint weight"};
                }
                for (float &weight : vertex.Weights)
                {
                    weight /= weightSum;
                }
            }
            const glm::vec3 position(vertex.Position[0], vertex.Position[1], vertex.Position[2]);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }

        if (primitive.indices)
        {
            indices.reserve(indices.size() + primitive.indices->count);
            for (cgltf_size index = 0; index < primitive.indices->count; ++index)
            {
                indices.push_back(static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive.indices, index)));
            }
        }
        else
        {
            for (std::uint32_t index = 0; index < positions->count; ++index)
            {
                indices.push_back(index);
            }
        }
        if (!tangents && normals && texCoords)
        {
            GenerateTangents(vertices, baseVertex, static_cast<std::uint32_t>(positions->count), indices, firstIndex,
                             static_cast<std::uint32_t>(indices.size()) - firstIndex);
        }
        submeshes.push_back({firstIndex, static_cast<std::uint32_t>(indices.size()) - firstIndex,
                             static_cast<std::int32_t>(baseVertex)});
    }

    if (vertices.empty() || indices.empty())
    {
        return {nullptr, AssetLoadError::LoaderFailed, "Selected glTF mesh contains no geometry"};
    }

    MeshDescriptor descriptor;
    descriptor.Layout.Stride = sizeof(ImportedVertex);
    descriptor.Layout.Attributes = {
        {0, 3, VertexComponentType::Float, false, false, offsetof(ImportedVertex, Position)},
        {1, 3, VertexComponentType::Float, false, false, offsetof(ImportedVertex, Normal)},
        {2, 2, VertexComponentType::Float, false, false, offsetof(ImportedVertex, TexCoord)},
        {3, 4, VertexComponentType::Float, false, false, offsetof(ImportedVertex, Tangent)},
        {4, 4, VertexComponentType::Float, false, false, offsetof(ImportedVertex, Color)},
        {7, 2, VertexComponentType::Float, false, false, offsetof(ImportedVertex, TexCoord1)},
    };
    if (meshIsSkinned)
    {
        descriptor.Layout.Attributes.push_back(
            {5, 4, VertexComponentType::UnsignedShort, false, true, offsetof(ImportedVertex, Joints)});
        descriptor.Layout.Attributes.push_back(
            {6, 4, VertexComponentType::Float, false, false, offsetof(ImportedVertex, Weights)});
    }
    descriptor.VertexData = std::as_bytes(std::span(vertices));
    descriptor.IndexData = std::as_bytes(std::span(indices));
    descriptor.Indices = IndexType::UnsignedInt;
    descriptor.Submeshes = std::move(submeshes);
    descriptor.Bounds.Center = (minimum + maximum) * 0.5f;
    descriptor.Bounds.Extents = (maximum - minimum) * 0.5f;
    descriptor.IsSkinned = meshIsSkinned;
    descriptor.MaximumJointIndex = maximumJointIndex;

    auto mesh = std::make_unique<Mesh>();
    if (!mesh->Initialize(descriptor))
    {
        return {nullptr, AssetLoadError::LoaderFailed, "Could not create OpenGL mesh from glTF geometry"};
    }
    return {std::move(mesh), AssetLoadError::None, {}};
}

} // namespace Haki
