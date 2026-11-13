#include "Haki/GL/Framebuffer.h"

#include <glad/glad.h>

#include <utility>

namespace Haki
{

Framebuffer::~Framebuffer()
{
    Reset();
}

Framebuffer::Framebuffer(Framebuffer &&other) noexcept
    : id_(std::exchange(other.id_, 0)),
      depthStencilRenderbuffer_(std::exchange(other.depthStencilRenderbuffer_, 0)),
      width_(std::exchange(other.width_, 0)), height_(std::exchange(other.height_, 0)),
      mode_(std::exchange(other.mode_, FramebufferMode::ColorDepthStencil)),
      colorFormat_(std::exchange(other.colorFormat_, TextureFormat::RGBA8)),
      colorAttachment_(std::move(other.colorAttachment_)), depthAttachment_(std::move(other.depthAttachment_))
{
}

Framebuffer &Framebuffer::operator=(Framebuffer &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        id_ = std::exchange(other.id_, 0);
        depthStencilRenderbuffer_ = std::exchange(other.depthStencilRenderbuffer_, 0);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
        mode_ = std::exchange(other.mode_, FramebufferMode::ColorDepthStencil);
        colorFormat_ = std::exchange(other.colorFormat_, TextureFormat::RGBA8);
        colorAttachment_ = std::move(other.colorAttachment_);
        depthAttachment_ = std::move(other.depthAttachment_);
    }
    return *this;
}

bool Framebuffer::Initialize(const FramebufferDescriptor &descriptor)
{
    Reset();
    if (descriptor.Width == 0 || descriptor.Height == 0 ||
        (descriptor.Mode != FramebufferMode::DepthOnly && descriptor.ColorFormat == TextureFormat::Depth24))
    {
        return false;
    }

    glGenFramebuffers(1, &id_);
    glBindFramebuffer(GL_FRAMEBUFFER, id_);

    if (id_ == 0)
    {
        Reset();
        return false;
    }
    if (descriptor.Mode != FramebufferMode::DepthOnly)
    {
        TextureDescriptor colorDescriptor;
        colorDescriptor.Width = descriptor.Width;
        colorDescriptor.Height = descriptor.Height;
        colorDescriptor.Format = descriptor.ColorFormat;
        colorDescriptor.MinFilter = TextureFilter::Linear;
        colorDescriptor.MagFilter = TextureFilter::Linear;
        colorDescriptor.WrapU = TextureWrap::ClampToEdge;
        colorDescriptor.WrapV = TextureWrap::ClampToEdge;
        colorDescriptor.GenerateMipmaps = false;
        if (!colorAttachment_.Initialize(colorDescriptor))
        {
            Reset();
            return false;
        }
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorAttachment_.id_, 0);
    }

    if (descriptor.Mode == FramebufferMode::DepthOnly ||
        descriptor.Mode == FramebufferMode::ColorDepthTexture)
    {
        TextureDescriptor depthDescriptor;
        depthDescriptor.Width = descriptor.Width;
        depthDescriptor.Height = descriptor.Height;
        depthDescriptor.Format = TextureFormat::Depth24;
        depthDescriptor.MinFilter = TextureFilter::Nearest;
        depthDescriptor.MagFilter = TextureFilter::Nearest;
        depthDescriptor.WrapU = TextureWrap::ClampToEdge;
        depthDescriptor.WrapV = TextureWrap::ClampToEdge;
        depthDescriptor.GenerateMipmaps = false;
        if (!depthAttachment_.Initialize(depthDescriptor))
        {
            Reset();
            return false;
        }
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthAttachment_.id_, 0);
        if (descriptor.Mode == FramebufferMode::DepthOnly)
        {
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }
    }
    else if (descriptor.Mode == FramebufferMode::ColorDepthStencil)
    {
        glGenRenderbuffers(1, &depthStencilRenderbuffer_);
        if (depthStencilRenderbuffer_ == 0)
        {
            Reset();
            return false;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, depthStencilRenderbuffer_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast<GLsizei>(descriptor.Width),
                              static_cast<GLsizei>(descriptor.Height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                  depthStencilRenderbuffer_);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        Reset();
        return false;
    }

    width_ = descriptor.Width;
    height_ = descriptor.Height;
    mode_ = descriptor.Mode;
    colorFormat_ = descriptor.ColorFormat;
    return true;
}

bool Framebuffer::Resize(std::uint32_t width, std::uint32_t height)
{
    if (IsValid() && width == width_ && height == height_)
    {
        return true;
    }
    return Initialize({width, height, mode_, colorFormat_});
}

void Framebuffer::Bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
}

void Framebuffer::BindDefault()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Reset()
{
    if (depthStencilRenderbuffer_ != 0)
    {
        glDeleteRenderbuffers(1, &depthStencilRenderbuffer_);
    }
    colorAttachment_.Reset();
    depthAttachment_.Reset();
    if (id_ != 0)
    {
        glDeleteFramebuffers(1, &id_);
    }
    id_ = 0;
    depthStencilRenderbuffer_ = 0;
    width_ = 0;
    height_ = 0;
    mode_ = FramebufferMode::ColorDepthStencil;
    colorFormat_ = TextureFormat::RGBA8;
}

} // namespace Haki
