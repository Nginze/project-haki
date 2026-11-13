#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Haki
{

enum class BufferType
{
    Vertex,
    Index,
    Uniform
};

enum class BufferUsage
{
    Static,
    Dynamic,
    Stream
};

class Buffer
{
public:
    Buffer() = default;
    ~Buffer();

    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(Buffer &&other) noexcept;

    bool Initialize(BufferType type, std::size_t size, const void *data = nullptr,
                    BufferUsage usage = BufferUsage::Static);
    bool Update(std::size_t offset, std::span<const std::byte> data);
    void Bind() const;
    void BindBase(std::uint32_t binding) const;
    void Reset();

    bool IsValid() const { return id_ != 0; }
    std::size_t GetSize() const { return size_; }

private:
    std::uint32_t id_ = 0;
    std::uint32_t target_ = 0;
    std::size_t size_ = 0;
};

} // namespace Haki
