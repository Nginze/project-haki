#include "Haki/GL/Texture.h"

#include <glad/glad.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace Haki
{
namespace
{

GLint ToInternalFormat(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8:
        return GL_R8;
    case TextureFormat::RGB8:
        return GL_RGB8;
    case TextureFormat::RGBA8:
        return GL_RGBA8;
    case TextureFormat::RG16F:
        return GL_RG16F;
    case TextureFormat::RGB16F:
        return GL_RGB16F;
    case TextureFormat::RGBA16F:
        return GL_RGBA16F;
    case TextureFormat::SRGB8:
        return GL_SRGB8;
    case TextureFormat::SRGB8Alpha8:
        return GL_SRGB8_ALPHA8;
    case TextureFormat::Depth24:
        return GL_DEPTH_COMPONENT24;
    }
    return GL_RGBA8;
}

GLenum ToSourceFormat(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8:
        return GL_RED;
    case TextureFormat::RGB8:
    case TextureFormat::SRGB8:
        return GL_RGB;
    case TextureFormat::RGBA8:
    case TextureFormat::SRGB8Alpha8:
        return GL_RGBA;
    case TextureFormat::RG16F:
        return GL_RG;
    case TextureFormat::RGB16F:
        return GL_RGB;
    case TextureFormat::RGBA16F:
        return GL_RGBA;
    case TextureFormat::Depth24:
        return GL_DEPTH_COMPONENT;
    }
    return GL_RGBA;
}

std::size_t ChannelCount(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8:
        return 1;
    case TextureFormat::RGB8:
    case TextureFormat::SRGB8:
        return 3;
    case TextureFormat::RGBA8:
    case TextureFormat::SRGB8Alpha8:
        return 4;
    case TextureFormat::RG16F:
        return 2;
    case TextureFormat::RGB16F:
        return 3;
    case TextureFormat::RGBA16F:
        return 4;
    case TextureFormat::Depth24:
        return 1;
    }
    return 0;
}

std::size_t ComponentSize(TextureFormat format)
{
    return format == TextureFormat::RG16F || format == TextureFormat::RGB16F ||
                   format == TextureFormat::RGBA16F
               ? sizeof(float)
               : sizeof(std::byte);
}

GLenum ToSourceType(TextureFormat format)
{
    if (format == TextureFormat::Depth24)
    {
        return GL_UNSIGNED_INT;
    }
    return format == TextureFormat::RG16F || format == TextureFormat::RGB16F ||
                   format == TextureFormat::RGBA16F
               ? GL_FLOAT
               : GL_UNSIGNED_BYTE;
}

GLint ToFilter(TextureFilter filter)
{
    switch (filter)
    {
    case TextureFilter::Nearest:
        return GL_NEAREST;
    case TextureFilter::Linear:
        return GL_LINEAR;
    case TextureFilter::LinearMipmapLinear:
        return GL_LINEAR_MIPMAP_LINEAR;
    }
    return GL_LINEAR;
}

GLint ToWrap(TextureWrap wrap)
{
    switch (wrap)
    {
    case TextureWrap::Repeat:
        return GL_REPEAT;
    case TextureWrap::ClampToEdge:
        return GL_CLAMP_TO_EDGE;
    case TextureWrap::MirroredRepeat:
        return GL_MIRRORED_REPEAT;
    }
    return GL_REPEAT;
}

} // namespace

Texture::~Texture()
{
    Reset();
}

Texture::Texture(Texture &&other) noexcept
    : id_(std::exchange(other.id_, 0)), width_(std::exchange(other.width_, 0)),
      height_(std::exchange(other.height_, 0)), mipLevels_(std::exchange(other.mipLevels_, 0)),
      type_(std::exchange(other.type_, TextureType::Texture2D)),
      format_(std::exchange(other.format_, TextureFormat::RGBA8))
{
}

Texture &Texture::operator=(Texture &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        id_ = std::exchange(other.id_, 0);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
        mipLevels_ = std::exchange(other.mipLevels_, 0);
        type_ = std::exchange(other.type_, TextureType::Texture2D);
        format_ = std::exchange(other.format_, TextureFormat::RGBA8);
    }
    return *this;
}

bool Texture::Initialize(const TextureDescriptor &descriptor, std::span<const std::byte> pixels)
{
    Reset();
    const std::size_t bytesPerPixel = ChannelCount(descriptor.Format) * ComponentSize(descriptor.Format);
    if (descriptor.Width == 0 || descriptor.Height == 0 || descriptor.MipLevels == 0 ||
        bytesPerPixel == 0 ||
        static_cast<std::size_t>(descriptor.Width) >
            std::numeric_limits<std::size_t>::max() / descriptor.Height / bytesPerPixel ||
        (descriptor.Format == TextureFormat::Depth24 && !pixels.empty()) ||
        (descriptor.Type == TextureType::Cubemap && !pixels.empty()))
    {
        return false;
    }
    const std::size_t requiredSize =
        static_cast<std::size_t>(descriptor.Width) * descriptor.Height * bytesPerPixel;
    GLint maximumSize = 0;
    glGetIntegerv(descriptor.Type == TextureType::Cubemap ? GL_MAX_CUBE_MAP_TEXTURE_SIZE : GL_MAX_TEXTURE_SIZE,
                  &maximumSize);
    if ((!pixels.empty() && pixels.size() < requiredSize) ||
        descriptor.Width > static_cast<std::uint32_t>(maximumSize) ||
        descriptor.Height > static_cast<std::uint32_t>(maximumSize))
    {
        return false;
    }

    glGenTextures(1, &id_);
    const GLenum target = descriptor.Type == TextureType::Cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glBindTexture(target, id_);
    GLint unpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (descriptor.Type == TextureType::Cubemap)
    {
        for (std::uint32_t mip = 0; mip < descriptor.MipLevels; ++mip)
        {
            const GLsizei width = static_cast<GLsizei>(std::max(descriptor.Width >> mip, 1u));
            const GLsizei height = static_cast<GLsizei>(std::max(descriptor.Height >> mip, 1u));
            for (std::uint32_t face = 0; face < 6; ++face)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, static_cast<GLint>(mip),
                             ToInternalFormat(descriptor.Format), width, height, 0,
                             ToSourceFormat(descriptor.Format), ToSourceType(descriptor.Format), nullptr);
            }
        }
    }
    else
    {
        for (std::uint32_t mip = 0; mip < descriptor.MipLevels; ++mip)
        {
            const GLsizei width = static_cast<GLsizei>(std::max(descriptor.Width >> mip, 1u));
            const GLsizei height = static_cast<GLsizei>(std::max(descriptor.Height >> mip, 1u));
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(mip), ToInternalFormat(descriptor.Format), width, height, 0,
                         ToSourceFormat(descriptor.Format), ToSourceType(descriptor.Format),
                         mip == 0 && !pixels.empty() ? pixels.data() : nullptr);
        }
    }
    const TextureFilter minFilter = !descriptor.GenerateMipmaps && descriptor.MipLevels == 1 &&
                                            descriptor.MinFilter == TextureFilter::LinearMipmapLinear
                                        ? TextureFilter::Linear
                                        : descriptor.MinFilter;
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, ToFilter(minFilter));
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, ToFilter(descriptor.MagFilter));
    glTexParameteri(target, GL_TEXTURE_WRAP_S, ToWrap(descriptor.WrapU));
    glTexParameteri(target, GL_TEXTURE_WRAP_T, ToWrap(descriptor.WrapV));
    if (descriptor.Type == TextureType::Cubemap)
    {
        glTexParameteri(target, GL_TEXTURE_WRAP_R, ToWrap(descriptor.WrapW));
    }
    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(descriptor.MipLevels - 1));
    if (descriptor.GenerateMipmaps)
    {
        glGenerateMipmap(target);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);

    width_ = descriptor.Width;
    height_ = descriptor.Height;
    mipLevels_ = descriptor.MipLevels;
    type_ = descriptor.Type;
    format_ = descriptor.Format;
    return id_ != 0;
}

void Texture::Bind(std::uint32_t unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(type_ == TextureType::Cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, id_);
}

void Texture::GenerateMipmaps() const
{
    glBindTexture(type_ == TextureType::Cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, id_);
    glGenerateMipmap(type_ == TextureType::Cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D);
}

void Texture::Reset()
{
    if (id_ != 0)
    {
        glDeleteTextures(1, &id_);
    }
    id_ = 0;
    width_ = 0;
    height_ = 0;
    mipLevels_ = 0;
    type_ = TextureType::Texture2D;
    format_ = TextureFormat::RGBA8;
}

} // namespace Haki
