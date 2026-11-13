#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Haki
{

class Framebuffer;

enum class TextureFormat
{
    R8,
    RGB8,
    RGBA8,
    RG16F,
    RGB16F,
    RGBA16F,
    SRGB8,
    SRGB8Alpha8,
    Depth24
};

enum class TextureType
{
    Texture2D,
    Cubemap
};

enum class TextureFilter
{
    Nearest,
    Linear,
    LinearMipmapLinear
};

enum class TextureWrap
{
    Repeat,
    ClampToEdge,
    MirroredRepeat
};

struct TextureDescriptor
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    TextureFormat Format = TextureFormat::RGBA8;
    TextureType Type = TextureType::Texture2D;
    TextureFilter MinFilter = TextureFilter::LinearMipmapLinear;
    TextureFilter MagFilter = TextureFilter::Linear;
    TextureWrap WrapU = TextureWrap::Repeat;
    TextureWrap WrapV = TextureWrap::Repeat;
    TextureWrap WrapW = TextureWrap::Repeat;
    std::uint32_t MipLevels = 1;
    bool GenerateMipmaps = true;
};

class Texture
{
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;
    Texture(Texture &&other) noexcept;
    Texture &operator=(Texture &&other) noexcept;

    bool Initialize(const TextureDescriptor &descriptor, std::span<const std::byte> pixels = {});
    void Bind(std::uint32_t unit) const;
    void GenerateMipmaps() const;
    void Reset();

    bool IsValid() const { return id_ != 0; }
    std::uint32_t GetWidth() const { return width_; }
    std::uint32_t GetHeight() const { return height_; }
    std::uint32_t GetMipLevels() const { return mipLevels_; }
    std::uint32_t GetNativeId() const { return id_; }
    TextureType GetType() const { return type_; }
    TextureFormat GetFormat() const { return format_; }

private:
    friend class Framebuffer;

    std::uint32_t id_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t mipLevels_ = 0;
    TextureType type_ = TextureType::Texture2D;
    TextureFormat format_ = TextureFormat::RGBA8;
};

} // namespace Haki
