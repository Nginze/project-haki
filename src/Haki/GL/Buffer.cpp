#include "Haki/GL/Buffer.h"

#include <glad/glad.h>

#include <utility>

namespace Haki
{
namespace
{

GLenum ToGLTarget(BufferType type)
{
    switch (type)
    {
    case BufferType::Vertex:
        return GL_ARRAY_BUFFER;
    case BufferType::Index:
        return GL_ELEMENT_ARRAY_BUFFER;
    case BufferType::Uniform:
        return GL_UNIFORM_BUFFER;
    }
    return 0;
}

GLenum ToGLUsage(BufferUsage usage)
{
    switch (usage)
    {
    case BufferUsage::Static:
        return GL_STATIC_DRAW;
    case BufferUsage::Dynamic:
        return GL_DYNAMIC_DRAW;
    case BufferUsage::Stream:
        return GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}

} // namespace

Buffer::~Buffer()
{
    Reset();
}

Buffer::Buffer(Buffer &&other) noexcept
    : id_(std::exchange(other.id_, 0)), target_(std::exchange(other.target_, 0)),
      size_(std::exchange(other.size_, 0))
{
}

Buffer &Buffer::operator=(Buffer &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        id_ = std::exchange(other.id_, 0);
        target_ = std::exchange(other.target_, 0);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

bool Buffer::Initialize(BufferType type, std::size_t size, const void *data, BufferUsage usage)
{
    Reset();
    if (size == 0)
    {
        return false;
    }

    target_ = ToGLTarget(type);
    glGenBuffers(1, &id_);
    glBindBuffer(target_, id_);
    glBufferData(target_, static_cast<GLsizeiptr>(size), data, ToGLUsage(usage));
    size_ = size;
    return id_ != 0;
}

bool Buffer::Update(std::size_t offset, std::span<const std::byte> data)
{
    if (!IsValid() || data.empty() || offset > size_ || data.size() > size_ - offset)
    {
        return false;
    }
    glBindBuffer(target_, id_);
    glBufferSubData(target_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(data.size()), data.data());
    return true;
}

void Buffer::Bind() const
{
    glBindBuffer(target_, id_);
}

void Buffer::BindBase(std::uint32_t binding) const
{
    if (target_ == GL_UNIFORM_BUFFER)
    {
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, id_);
    }
}

void Buffer::Reset()
{
    if (id_ != 0)
    {
        glDeleteBuffers(1, &id_);
    }
    id_ = 0;
    target_ = 0;
    size_ = 0;
}

} // namespace Haki
