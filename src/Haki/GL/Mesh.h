#pragma once

#include "Haki/GL/Buffer.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

namespace Haki
{

enum class VertexComponentType
{
    Float,
    UnsignedByte,
    UnsignedShort,
    UnsignedInt
};

enum class IndexType
{
    None,
    UnsignedShort,
    UnsignedInt
};

enum class PrimitiveTopology
{
    Triangles,
    Lines,
    Points
};

struct VertexAttribute
{
    std::uint32_t Location = 0;
    std::uint32_t Components = 0;
    VertexComponentType Type = VertexComponentType::Float;
    bool Normalized = false;
    bool IsInteger = false;
    std::size_t Offset = 0;
};

struct VertexLayout
{
    std::size_t Stride = 0;
    std::vector<VertexAttribute> Attributes;
};

struct Submesh
{
    std::uint32_t First = 0;
    std::uint32_t Count = 0;
    std::int32_t BaseVertex = 0;
};

struct MeshBounds
{
    glm::vec3 Center{0.0f};
    glm::vec3 Extents{0.0f};
};

struct MeshDescriptor
{
    VertexLayout Layout;
    std::span<const std::byte> VertexData;
    std::span<const std::byte> IndexData;
    IndexType Indices = IndexType::None;
    PrimitiveTopology Topology = PrimitiveTopology::Triangles;
    std::vector<Submesh> Submeshes;
    MeshBounds Bounds;
    bool IsSkinned = false;
    std::uint32_t MaximumJointIndex = 0;
};

class Mesh
{
public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;
    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;

    bool Initialize(const MeshDescriptor &descriptor);
    void Bind() const;
    bool Draw(std::uint32_t submesh = 0) const;
    void Reset();

    bool IsValid() const { return vertexArray_ != 0; }
    const MeshBounds &GetBounds() const { return bounds_; }
    std::size_t GetSubmeshCount() const { return submeshes_.size(); }
    bool IsSkinned() const { return isSkinned_; }
    std::uint32_t GetMaximumJointIndex() const { return maximumJointIndex_; }

private:
    std::uint32_t vertexArray_ = 0;
    Buffer vertexBuffer_;
    Buffer indexBuffer_;
    IndexType indexType_ = IndexType::None;
    PrimitiveTopology topology_ = PrimitiveTopology::Triangles;
    std::uint32_t vertexCount_ = 0;
    std::vector<Submesh> submeshes_;
    MeshBounds bounds_;
    bool isSkinned_ = false;
    std::uint32_t maximumJointIndex_ = 0;
};

} // namespace Haki
