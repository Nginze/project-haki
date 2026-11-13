#pragma once

#include "Haki/GL/Texture.h"

#include <cstdint>

namespace Haki
{

enum class FramebufferMode
{
    ColorDepthStencil,
    ColorDepthTexture,
    ColorOnly,
    DepthOnly
};

struct FramebufferDescriptor
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    FramebufferMode Mode = FramebufferMode::ColorDepthStencil;
    TextureFormat ColorFormat = TextureFormat::RGBA8;
};

class Framebuffer
{
public:
    Framebuffer() = default;
    ~Framebuffer();

    Framebuffer(const Framebuffer &) = delete;
    Framebuffer &operator=(const Framebuffer &) = delete;
    Framebuffer(Framebuffer &&other) noexcept;
    Framebuffer &operator=(Framebuffer &&other) noexcept;

    bool Initialize(const FramebufferDescriptor &descriptor);
    bool Resize(std::uint32_t width, std::uint32_t height);
    void Bind() const;
    static void BindDefault();
    void Reset();

    bool IsValid() const { return id_ != 0; }
    std::uint32_t GetId() const { return id_; }
    std::uint32_t GetWidth() const { return width_; }
    std::uint32_t GetHeight() const { return height_; }
    const Texture &GetColorAttachment() const { return colorAttachment_; }
    const Texture &GetDepthAttachment() const { return depthAttachment_; }

private:
    std::uint32_t id_ = 0;
    std::uint32_t depthStencilRenderbuffer_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    FramebufferMode mode_ = FramebufferMode::ColorDepthStencil;
    TextureFormat colorFormat_ = TextureFormat::RGBA8;
    Texture colorAttachment_;
    Texture depthAttachment_;
};

} // namespace Haki
