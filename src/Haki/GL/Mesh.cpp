#include "Haki/GL/Mesh.h"

#include <glad/glad.h>

#include <utility>

namespace Haki
{
namespace
{

GLenum ToGLType(VertexComponentType type)
{
    switch (type)
    {
    case VertexComponentType::Float:
        return GL_FLOAT;
    case VertexComponentType::UnsignedByte:
        return GL_UNSIGNED_BYTE;
    case VertexComponentType::UnsignedShort:
        return GL_UNSIGNED_SHORT;
    case VertexComponentType::UnsignedInt:
        return GL_UNSIGNED_INT;
    }
    return GL_FLOAT;
}

GLenum ToGLIndexType(IndexType type)
{
    return type == IndexType::UnsignedShort ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
}

std::size_t IndexSize(IndexType type)
{
    return type == IndexType::UnsignedShort ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
}

GLenum ToGLTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::Triangles:
        return GL_TRIANGLES;
    case PrimitiveTopology::Lines:
        return GL_LINES;
    case PrimitiveTopology::Points:
        return GL_POINTS;
    }
    return GL_TRIANGLES;
}

} // namespace

Mesh::~Mesh()
{
    Reset();
}

Mesh::Mesh(Mesh &&other) noexcept
    : vertexArray_(std::exchange(other.vertexArray_, 0)), vertexBuffer_(std::move(other.vertexBuffer_)),
      indexBuffer_(std::move(other.indexBuffer_)), indexType_(std::exchange(other.indexType_, IndexType::None)),
      topology_(other.topology_), vertexCount_(std::exchange(other.vertexCount_, 0)),
       submeshes_(std::move(other.submeshes_)), bounds_(other.bounds_),
       isSkinned_(std::exchange(other.isSkinned_, false)),
       maximumJointIndex_(std::exchange(other.maximumJointIndex_, 0))
{
}

Mesh &Mesh::operator=(Mesh &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        vertexArray_ = std::exchange(other.vertexArray_, 0);
        vertexBuffer_ = std::move(other.vertexBuffer_);
        indexBuffer_ = std::move(other.indexBuffer_);
        indexType_ = std::exchange(other.indexType_, IndexType::None);
        topology_ = other.topology_;
        vertexCount_ = std::exchange(other.vertexCount_, 0);
        submeshes_ = std::move(other.submeshes_);
        bounds_ = other.bounds_;
        isSkinned_ = std::exchange(other.isSkinned_, false);
        maximumJointIndex_ = std::exchange(other.maximumJointIndex_, 0);
    }
    return *this;
}

bool Mesh::Initialize(const MeshDescriptor &descriptor)
{
    Reset();
    if (descriptor.VertexData.empty() || descriptor.Layout.Stride == 0 || descriptor.Layout.Attributes.empty() ||
        descriptor.VertexData.size() % descriptor.Layout.Stride != 0)
    {
        return false;
    }
    if (descriptor.Indices != IndexType::None &&
        (descriptor.IndexData.empty() || descriptor.IndexData.size() % IndexSize(descriptor.Indices) != 0))
    {
        return false;
    }

    glGenVertexArrays(1, &vertexArray_);
    glBindVertexArray(vertexArray_);
    if (!vertexBuffer_.Initialize(BufferType::Vertex, descriptor.VertexData.size(), descriptor.VertexData.data()))
    {
        Reset();
        return false;
    }

    for (const VertexAttribute &attribute : descriptor.Layout.Attributes)
    {
        if (attribute.Components == 0 || attribute.Components > 4 || attribute.Offset >= descriptor.Layout.Stride ||
            (attribute.IsInteger && (attribute.Type == VertexComponentType::Float || attribute.Normalized)))
        {
            Reset();
            return false;
        }
        glEnableVertexAttribArray(attribute.Location);
        if (attribute.IsInteger)
        {
            glVertexAttribIPointer(attribute.Location, static_cast<GLint>(attribute.Components),
                                   ToGLType(attribute.Type), static_cast<GLsizei>(descriptor.Layout.Stride),
                                   reinterpret_cast<const void *>(attribute.Offset));
        }
        else
        {
            glVertexAttribPointer(attribute.Location, static_cast<GLint>(attribute.Components),
                                  ToGLType(attribute.Type), attribute.Normalized ? GL_TRUE : GL_FALSE,
                                  static_cast<GLsizei>(descriptor.Layout.Stride),
                                  reinterpret_cast<const void *>(attribute.Offset));
        }
    }

    indexType_ = descriptor.Indices;
    if (indexType_ != IndexType::None &&
        !indexBuffer_.Initialize(BufferType::Index, descriptor.IndexData.size(), descriptor.IndexData.data()))
    {
        Reset();
        return false;
    }

    vertexCount_ = static_cast<std::uint32_t>(descriptor.VertexData.size() / descriptor.Layout.Stride);
    topology_ = descriptor.Topology;
    bounds_ = descriptor.Bounds;
    isSkinned_ = descriptor.IsSkinned;
    maximumJointIndex_ = descriptor.MaximumJointIndex;
    submeshes_ = descriptor.Submeshes;
    if (submeshes_.empty())
    {
        const std::uint32_t count = indexType_ == IndexType::None
                                        ? vertexCount_
                                        : static_cast<std::uint32_t>(descriptor.IndexData.size() / IndexSize(indexType_));
        submeshes_.push_back({0, count, 0});
    }
    return true;
}

void Mesh::Bind() const
{
    glBindVertexArray(vertexArray_);
}

bool Mesh::Draw(std::uint32_t submesh) const
{
    if (!IsValid() || submesh >= submeshes_.size())
    {
        return false;
    }
    const Submesh &range = submeshes_[submesh];
    Bind();
    if (indexType_ == IndexType::None)
    {
        glDrawArrays(ToGLTopology(topology_), static_cast<GLint>(range.First), static_cast<GLsizei>(range.Count));
    }
    else
    {
        const std::size_t offset = static_cast<std::size_t>(range.First) * IndexSize(indexType_);
        glDrawElementsBaseVertex(ToGLTopology(topology_), static_cast<GLsizei>(range.Count), ToGLIndexType(indexType_),
                                 reinterpret_cast<const void *>(offset), range.BaseVertex);
    }
    return true;
}

void Mesh::Reset()
{
    indexBuffer_.Reset();
    vertexBuffer_.Reset();
    if (vertexArray_ != 0)
    {
        glDeleteVertexArrays(1, &vertexArray_);
    }
    vertexArray_ = 0;
    indexType_ = IndexType::None;
    vertexCount_ = 0;
    submeshes_.clear();
    bounds_ = {};
    isSkinned_ = false;
    maximumJointIndex_ = 0;
}

} // namespace Haki
