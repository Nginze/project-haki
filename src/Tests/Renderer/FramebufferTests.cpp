#include "Haki/GL/Framebuffer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <string_view>
#include <utility>

namespace
{

class TestRunner
{
public:
    explicit TestRunner(std::string_view name) { std::cout << "\n=== " << name << " ===\n"; }

    void Check(bool condition, std::string_view name)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
        failures_ += condition ? 0 : 1;
    }

    int Result() const { return failures_ == 0 ? 0 : 1; }

private:
    int failures_ = 0;
};

class HiddenContext
{
public:
    bool Initialize()
    {
#if defined(__linux__) && defined(GLFW_PLATFORM_X11)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
        if (!glfwInit())
        {
            return false;
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window_ = glfwCreateWindow(64, 64, "FramebufferTests", nullptr, nullptr);
        if (!window_)
        {
            glfwTerminate();
            return false;
        }
        glfwMakeContextCurrent(window_);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            glfwDestroyWindow(window_);
            window_ = nullptr;
            glfwTerminate();
            return false;
        }
        return true;
    }

    ~HiddenContext()
    {
        if (window_)
        {
            glfwDestroyWindow(window_);
        }
        glfwTerminate();
    }

private:
    GLFWwindow *window_ = nullptr;
};

} // namespace

int main()
{
    HiddenContext context;
    if (!context.Initialize())
    {
        std::cout << "OpenGL context unavailable; skipping framebuffer tests\n";
        return 77;
    }

    TestRunner tests("Framebuffer.Lifecycle");
    Haki::Framebuffer framebuffer;
    tests.Check(!framebuffer.IsValid(), "default framebuffer object is invalid");
    tests.Check(!framebuffer.Initialize({0, 32}), "zero-sized framebuffer is rejected");
    tests.Check(framebuffer.Initialize({32, 16}), "framebuffer initializes with color and depth attachments");
    tests.Check(framebuffer.GetWidth() == 32 && framebuffer.GetHeight() == 16,
                "framebuffer dimensions are retained");
    tests.Check(framebuffer.GetColorAttachment().IsValid(), "color attachment is a valid texture");
    tests.Check(framebuffer.GetColorAttachment().GetWidth() == 32 &&
                    framebuffer.GetColorAttachment().GetHeight() == 16,
                "color attachment dimensions match the framebuffer");

    framebuffer.Bind();
    GLint binding = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &binding);
    tests.Check(binding != 0, "Bind selects the framebuffer");
    tests.Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "bound framebuffer is complete");
    Haki::Framebuffer::BindDefault();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &binding);
    tests.Check(binding == 0, "BindDefault restores the window framebuffer");

    tests.Check(framebuffer.Resize(16, 8), "framebuffer resizes");
    tests.Check(framebuffer.GetWidth() == 16 && framebuffer.GetHeight() == 8,
                "resized dimensions are retained");
    tests.Check(framebuffer.GetColorAttachment().GetWidth() == 16 &&
                    framebuffer.GetColorAttachment().GetHeight() == 8,
                "resize recreates the color attachment");

    Haki::Framebuffer shadowFramebuffer;
    tests.Check(shadowFramebuffer.Initialize({32, 32, Haki::FramebufferMode::DepthOnly}),
                "depth-only framebuffer initializes");
    tests.Check(!shadowFramebuffer.GetColorAttachment().IsValid(),
                "depth-only framebuffer has no color attachment");
    tests.Check(shadowFramebuffer.GetDepthAttachment().IsValid(),
                "depth-only framebuffer exposes a depth texture");
    shadowFramebuffer.Bind();
    tests.Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "depth-only framebuffer is complete");
    shadowFramebuffer.GetDepthAttachment().Bind(0);
    GLint internalFormat = 0;
    GLint minFilter = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    tests.Check(internalFormat == GL_DEPTH_COMPONENT24, "depth attachment uses Depth24 storage");
    tests.Check(minFilter == GL_NEAREST, "depth attachment uses nearest filtering");
    tests.Check(shadowFramebuffer.Resize(16, 16) && !shadowFramebuffer.GetColorAttachment().IsValid() &&
                    shadowFramebuffer.GetDepthAttachment().IsValid(),
                "resize preserves depth-only framebuffer mode");
    Haki::Framebuffer::BindDefault();

    Haki::Framebuffer hdrFramebuffer;
    tests.Check(hdrFramebuffer.Initialize(
                    {32, 16, Haki::FramebufferMode::ColorDepthStencil, Haki::TextureFormat::RGBA16F}),
                "HDR framebuffer initializes");
    hdrFramebuffer.GetColorAttachment().Bind(0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    tests.Check(internalFormat == GL_RGBA16F, "HDR framebuffer uses RGBA16F color storage");
    tests.Check(hdrFramebuffer.Resize(16, 8), "HDR framebuffer resizes");
    hdrFramebuffer.GetColorAttachment().Bind(0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    tests.Check(internalFormat == GL_RGBA16F, "HDR resize preserves the color format");

    Haki::Framebuffer ssaoFramebuffer;
    tests.Check(ssaoFramebuffer.Initialize(
                    {24, 12, Haki::FramebufferMode::ColorDepthTexture, Haki::TextureFormat::RG16F}),
                "color-depth-texture framebuffer initializes");
    tests.Check(ssaoFramebuffer.GetColorAttachment().IsValid() &&
                    ssaoFramebuffer.GetDepthAttachment().IsValid(),
                "color-depth-texture mode owns color and depth textures");
    ssaoFramebuffer.Bind();
    GLint colorAttachmentType = 0;
    GLint depthAttachmentType = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthAttachmentType);
    tests.Check(colorAttachmentType == GL_TEXTURE && depthAttachmentType == GL_TEXTURE,
                "color-depth-texture mode attaches both textures");
    tests.Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "color-depth-texture framebuffer is complete");
    ssaoFramebuffer.GetColorAttachment().Bind(0);
    GLint magFilter = 0;
    GLint wrapS = 0;
    GLint wrapT = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrapT);
    tests.Check(internalFormat == GL_RG16F, "color-depth-texture mode uses the requested color format");
    tests.Check(minFilter == GL_LINEAR && magFilter == GL_LINEAR && wrapS == GL_CLAMP_TO_EDGE &&
                    wrapT == GL_CLAMP_TO_EDGE,
                "owned color texture uses linear filtering and clamp wrapping");
    ssaoFramebuffer.GetDepthAttachment().Bind(0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrapT);
    tests.Check(internalFormat == GL_DEPTH_COMPONENT24, "sampled depth attachment uses Depth24 storage");
    tests.Check(minFilter == GL_NEAREST && magFilter == GL_NEAREST && wrapS == GL_CLAMP_TO_EDGE &&
                    wrapT == GL_CLAMP_TO_EDGE,
                "sampled depth texture uses nearest filtering and clamp wrapping");
    tests.Check(ssaoFramebuffer.Resize(12, 6) && ssaoFramebuffer.GetColorAttachment().GetWidth() == 12 &&
                    ssaoFramebuffer.GetDepthAttachment().GetWidth() == 12 &&
                    ssaoFramebuffer.GetColorAttachment().GetFormat() == Haki::TextureFormat::RG16F &&
                    ssaoFramebuffer.GetDepthAttachment().GetFormat() == Haki::TextureFormat::Depth24,
                "resize preserves color-depth-texture mode and formats");
    ssaoFramebuffer.Bind();
    tests.Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "resized color-depth-texture framebuffer is complete");

    Haki::Framebuffer colorOnlyFramebuffer;
    tests.Check(colorOnlyFramebuffer.Initialize(
                    {24, 12, Haki::FramebufferMode::ColorOnly, Haki::TextureFormat::R8}),
                "color-only framebuffer initializes");
    tests.Check(colorOnlyFramebuffer.GetColorAttachment().IsValid() &&
                    !colorOnlyFramebuffer.GetDepthAttachment().IsValid(),
                "color-only mode owns only a color texture");
    colorOnlyFramebuffer.Bind();
    colorAttachmentType = 0;
    depthAttachmentType = 0;
    GLint stencilAttachmentType = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthAttachmentType);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &stencilAttachmentType);
    tests.Check(colorAttachmentType == GL_TEXTURE && depthAttachmentType == GL_NONE &&
                    stencilAttachmentType == GL_NONE,
                "color-only mode has no depth or stencil attachment");
    tests.Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "color-only framebuffer is complete");
    colorOnlyFramebuffer.GetColorAttachment().Bind(0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrapT);
    tests.Check(internalFormat == GL_R8, "color-only mode uses the requested color format");
    tests.Check(minFilter == GL_LINEAR && magFilter == GL_LINEAR && wrapS == GL_CLAMP_TO_EDGE &&
                    wrapT == GL_CLAMP_TO_EDGE,
                "color-only texture uses linear filtering and clamp wrapping");
    tests.Check(colorOnlyFramebuffer.Resize(10, 5) &&
                    colorOnlyFramebuffer.GetColorAttachment().GetWidth() == 10 &&
                    !colorOnlyFramebuffer.GetDepthAttachment().IsValid() &&
                    colorOnlyFramebuffer.GetColorAttachment().GetFormat() == Haki::TextureFormat::R8,
                "resize preserves color-only mode and format");
    colorOnlyFramebuffer.Bind();
    tests.Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "resized color-only framebuffer is complete");
    Haki::Framebuffer::BindDefault();

    Haki::Texture cubemap;
    Haki::TextureDescriptor cubemapDescriptor;
    cubemapDescriptor.Width = 16;
    cubemapDescriptor.Height = 16;
    cubemapDescriptor.Format = Haki::TextureFormat::RGB16F;
    cubemapDescriptor.Type = Haki::TextureType::Cubemap;
    cubemapDescriptor.MinFilter = Haki::TextureFilter::LinearMipmapLinear;
    cubemapDescriptor.WrapU = Haki::TextureWrap::ClampToEdge;
    cubemapDescriptor.WrapV = Haki::TextureWrap::ClampToEdge;
    cubemapDescriptor.WrapW = Haki::TextureWrap::ClampToEdge;
    cubemapDescriptor.MipLevels = 5;
    cubemapDescriptor.GenerateMipmaps = false;
    tests.Check(cubemap.Initialize(cubemapDescriptor), "floating-point cubemap initializes");
    cubemap.Bind(0);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glGetTexParameteriv(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, &minFilter);
    GLint mipWidth = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 4, GL_TEXTURE_WIDTH, &mipWidth);
    tests.Check(internalFormat == GL_RGB16F, "cubemap uses RGB16F storage");
    tests.Check(mipWidth == 1 && cubemap.GetMipLevels() == 5,
                "cubemap allocates every face across the requested mip chain");
    tests.Check(minFilter == GL_LINEAR_MIPMAP_LINEAR,
                "explicit cubemap mip chains retain trilinear filtering");
    cubemap.GenerateMipmaps();

    Haki::Texture mipmappedTexture;
    Haki::TextureDescriptor mipmappedDescriptor;
    mipmappedDescriptor.Width = 16;
    mipmappedDescriptor.Height = 8;
    mipmappedDescriptor.Format = Haki::TextureFormat::RGBA16F;
    mipmappedDescriptor.MinFilter = Haki::TextureFilter::LinearMipmapLinear;
    mipmappedDescriptor.WrapU = Haki::TextureWrap::ClampToEdge;
    mipmappedDescriptor.WrapV = Haki::TextureWrap::ClampToEdge;
    mipmappedDescriptor.MipLevels = 5;
    mipmappedDescriptor.GenerateMipmaps = false;
    tests.Check(mipmappedTexture.Initialize(mipmappedDescriptor), "floating-point 2D mip chain initializes");
    mipmappedTexture.Bind(0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 4, GL_TEXTURE_WIDTH, &mipWidth);
    tests.Check(mipWidth == 1 && mipmappedTexture.GetMipLevels() == 5,
                "2D textures allocate every requested mip level");
    mipmappedTexture.GenerateMipmaps();

    Haki::Framebuffer moved(std::move(framebuffer));
    tests.Check(!framebuffer.IsValid() && moved.IsValid(), "move construction transfers ownership");
    Haki::Framebuffer assigned;
    assigned = std::move(moved);
    tests.Check(!moved.IsValid() && assigned.IsValid(), "move assignment transfers ownership");

    assigned.Reset();
    assigned.Reset();
    shadowFramebuffer.Reset();
    hdrFramebuffer.Reset();
    ssaoFramebuffer.Reset();
    colorOnlyFramebuffer.Reset();
    cubemap.Reset();
    mipmappedTexture.Reset();
    tests.Check(!assigned.IsValid() && !assigned.GetColorAttachment().IsValid(), "Reset is idempotent");
    tests.Check(glGetError() == GL_NO_ERROR, "framebuffer lifecycle completes without OpenGL errors");
    return tests.Result();
}
