#pragma once

#include "Haki/Renderer/Canvas.h"
#include "Haki/Renderer/DebugDraw.h"
#include "Haki/Renderer/RendererTypes.h"

#include <memory>
#include <string>

namespace Haki
{

class Renderer
{
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) noexcept;
    Renderer &operator=(Renderer &&) noexcept;

    bool Initialize(const RendererConfig &config = {}, std::string *diagnostic = nullptr);
    void Shutdown();
    bool IsInitialized() const;

    bool BeginFrame(const RenderFrameData &frame);
    void Submit(const RenderItem &item);
    void Render(RenderPath path = RenderPath::Forward);
    void EndFrame();

    RendererSettings &GetSettings();
    const RendererSettings &GetSettings() const;
    const RendererStats &GetStats() const;
    const RendererGpuTimings &GetGpuTimings() const;

    // Queue debug lines and shapes here; they are drawn at the end of the next frame and cleared.
    DebugDraw &GetDebugDraw();

    // Queue 2D rects, sprites and text in window pixels; drawn over the frame at EndFrame and cleared.
    Canvas &GetCanvas();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Haki
