#include "Haki/Debug/TelemetryOverlay.h"

#include "Haki/Renderer/Canvas.h"
#include "Haki/Renderer/Font.h"
#include "Haki/Scene/Scene.h"

#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Haki
{
namespace
{

constexpr float RefreshInterval = 0.25f; // seconds between HUD text updates, so numbers stay readable
constexpr float MemoryInterval = 0.5f;
constexpr float CpuSmoothing = 0.1f;     // exponential moving average weight of a new frame

std::string Format(const char *format, auto... values)
{
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), format, values...);
    return buffer;
}

void Smooth(float &value, float sample, bool first)
{
    value = first ? sample : value + (sample - value) * CpuSmoothing;
}

} // namespace

void FrameStats::AddFrame(float seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0f)
    {
        return;
    }
    frames_.push_back(seconds);
    total_ += seconds;
    // Keep at least one frame, and drop the oldest once the rest still fill the window.
    while (frames_.size() > 1 && total_ - frames_.front() >= window_)
    {
        total_ -= frames_.front();
        frames_.pop_front();
    }
}

float FrameStats::GetAverageFps() const
{
    return total_ > 0.0f ? static_cast<float>(frames_.size()) / total_ : 0.0f;
}

float FrameStats::GetMinimumFps() const
{
    if (frames_.empty())
    {
        return 0.0f;
    }
    const float slowest = *std::max_element(frames_.begin(), frames_.end());
    return slowest > 0.0f ? 1.0f / slowest : 0.0f;
}

float FrameStats::GetAverageFrameMs() const
{
    return frames_.empty() ? 0.0f : 1000.0f * total_ / static_cast<float>(frames_.size());
}

glm::vec2 ComputeTelemetryOrigin(bool menuBarVisible, float menuBarHeight, bool hierarchyVisible,
                                 float hierarchyWidth, float margin)
{
    return {margin + (hierarchyVisible ? std::max(hierarchyWidth, 0.0f) : 0.0f),
            margin + (menuBarVisible ? std::max(menuBarHeight, 0.0f) : 0.0f)};
}

std::vector<TelemetryLine> BuildTelemetryLines(const TelemetrySnapshot &snapshot)
{
    std::vector<TelemetryLine> lines;
    lines.push_back({"Window", Format("%d x %d (window units)",snapshot.WindowWidth,snapshot.WindowHeight)});
    lines.push_back({"Game", Format("%d x %d (framebuffer px)",snapshot.GameWidth,snapshot.GameHeight)});
    lines.push_back({"Render", snapshot.RenderWidth>0 && snapshot.RenderHeight>0
        ? Format("%d x %d (internal px)",snapshot.RenderWidth,snapshot.RenderHeight) : "waiting for first frame"});
    lines.push_back({"GPU", snapshot.Gpu});
    lines.push_back({"FPS", Format("avg %.0f  min %.0f  |  %.2f ms%s", snapshot.AverageFps, snapshot.MinimumFps,
                                   snapshot.FrameMs, snapshot.Paused ? "  |  PAUSED" : "")});

    switch (snapshot.MemorySource)
    {
    case GpuInfo::MemorySource::Nvidia:
        lines.push_back({"VRAM", Format("%d / %d MB", snapshot.VramUsedMb, snapshot.VramTotalMb)});
        break;
    case GpuInfo::MemorySource::Amd:
        lines.push_back({"VRAM", Format("%d MB free", snapshot.VramFreeMb)});
        break;
    case GpuInfo::MemorySource::None:
        lines.push_back({"VRAM", "n/a (driver has no memory query)"});
        break;
    }

    const RendererGpuTimings &gpu = snapshot.GpuTimings;
    if (!snapshot.GpuTimingsEnabled)
    {
        lines.push_back({"GPU ms", "timers off"});
    }
    else if (!gpu.Valid)
    {
        lines.push_back({"GPU ms", "waiting for timer queries..."});
    }
    else
    {
        std::string value;
        const auto add = [&value](const char *name, float ms) { value += Format("%s %.2f  ", name, ms); };
        if (snapshot.ShadowsEnabled) add("shadow", gpu.ShadowMs);
        if (snapshot.SsaoEnabled) add("ssao", gpu.SsaoMs);
        add("forward", gpu.ForwardMs);
        if (snapshot.BloomEnabled) add("bloom", gpu.BloomMs);
        add("post", gpu.PostProcessMs);
        add("ui", gpu.DebugDrawMs + gpu.CanvasMs);
        value += Format("| total %.2f", gpu.TotalMs);
        lines.push_back({"GPU ms", value});
    }

    const CpuTimings &cpu = snapshot.Cpu;
    lines.push_back({"CPU ms", Format("physics %.2f  lua %.2f  anim %.2f  scene %.2f  render %.2f  editor %.2f",
                                      cpu.PhysicsMs, cpu.ScriptsMs, cpu.AnimationMs, cpu.SceneMs, cpu.RenderMs,
                                      cpu.EditorMs)});
    return lines;
}

void TelemetryOverlay::Initialize()
{
    gpu_.Initialize();
}

void TelemetryOverlay::Update(float frameSeconds, const SceneCpuTimings *sceneCpu, float renderMs, float editorMs,
                              const RendererGpuTimings &gpuTimings, const RendererSettings &settings, bool paused,
                              int windowWidth, int windowHeight, int gameWidth, int gameHeight,
                              int renderWidth, int renderHeight)
{
    frames_.AddFrame(frameSeconds);
    const bool first = !hasCpu_;
    Smooth(smoothedCpu_.PhysicsMs, sceneCpu ? sceneCpu->PhysicsMs : 0.0f, first);
    Smooth(smoothedCpu_.ScriptsMs, sceneCpu ? sceneCpu->ScriptsMs : 0.0f, first);
    Smooth(smoothedCpu_.AnimationMs, sceneCpu ? sceneCpu->AnimationMs : 0.0f, first);
    Smooth(smoothedCpu_.SceneMs, sceneCpu ? sceneCpu->SceneMs : 0.0f, first);
    Smooth(smoothedCpu_.RenderMs, renderMs, first);
    Smooth(smoothedCpu_.EditorMs, editorMs, first);
    hasCpu_ = true;

    const float elapsed = std::isfinite(frameSeconds) ? std::max(frameSeconds, 0.0f) : 0.0f;
    sinceMemory_ += elapsed;
    if (sinceMemory_ >= MemoryInterval)
    {
        sinceMemory_ = 0.0f;
        gpu_.UpdateMemory();
    }
    sinceRefresh_ += elapsed;
    if (sinceRefresh_ < RefreshInterval && !lines_.empty())
    {
        return;
    }
    sinceRefresh_ = 0.0f;

    TelemetrySnapshot snapshot;
    snapshot.WindowWidth=windowWidth; snapshot.WindowHeight=windowHeight;
    snapshot.GameWidth=gameWidth; snapshot.GameHeight=gameHeight;
    snapshot.RenderWidth=renderWidth; snapshot.RenderHeight=renderHeight;
    snapshot.Gpu = gpu_.GetRenderer();
    snapshot.AverageFps = frames_.GetAverageFps();
    snapshot.MinimumFps = frames_.GetMinimumFps();
    snapshot.FrameMs = frames_.GetAverageFrameMs();
    snapshot.MemorySource = gpu_.GetMemorySource();
    snapshot.VramUsedMb = gpu_.GetUsedMemoryMb();
    snapshot.VramTotalMb = gpu_.GetTotalMemoryMb();
    snapshot.VramFreeMb = gpu_.GetFreeMemoryMb();
    snapshot.GpuTimings = gpuTimings;
    snapshot.GpuTimingsEnabled = settings.GpuTimingsEnabled;
    snapshot.ShadowsEnabled = settings.ShadowsEnabled;
    snapshot.SsaoEnabled = settings.SsaoEnabled && !settings.Wireframe;
    snapshot.BloomEnabled = settings.BloomEnabled;
    snapshot.Cpu = smoothedCpu_;
    snapshot.Paused = paused;
    lines_ = BuildTelemetryLines(snapshot);
}

void TelemetryOverlay::Draw(Canvas &canvas, const Font &font, glm::vec2 origin, float pixelSize) const
{
    if (lines_.empty() || !font.IsValid())
    {
        return;
    }
    constexpr int Layer = 1000;
    const glm::vec4 labelColor{0.55f, 0.75f, 1.0f, 1.0f};
    const glm::vec4 valueColor{0.92f, 0.94f, 0.96f, 1.0f};
    const glm::vec4 shadowColor{0.0f, 0.0f, 0.0f, 0.7f};
    const float lineHeight = font.GetLineHeight(pixelSize);
    const float labelWidth = font.MeasureText("GPU ms ", pixelSize).x;

    float width = 0.0f;
    for (const TelemetryLine &line : lines_)
    {
        width = std::max(width, labelWidth + font.MeasureText(line.Value, pixelSize).x);
    }
    const float padding = 6.0f;
    canvas.FillRect({origin.x - padding, origin.y - padding, width + 2.0f * padding,
                     lineHeight * static_cast<float>(lines_.size()) + 2.0f * padding},
                    {0.0f, 0.0f, 0.0f, 0.25f}, Layer);

    // Shadow pass under the text keeps it legible over bright sky or snow.
    const auto drawText = [&](const std::string &text, glm::vec2 position, const glm::vec4 &color) {
        canvas.DrawText(font, text, position + glm::vec2(1.0f, 1.0f), pixelSize, shadowColor, Layer + 1);
        canvas.DrawText(font, text, position + glm::vec2(-1.0f, 1.0f), pixelSize, shadowColor, Layer + 1);
        canvas.DrawText(font, text, position, pixelSize, color, Layer + 2);
    };
    glm::vec2 cursor = origin;
    for (const TelemetryLine &line : lines_)
    {
        drawText(line.Label, cursor, labelColor);
        drawText(line.Value, {cursor.x + labelWidth, cursor.y}, valueColor);
        cursor.y += lineHeight;
    }
}

} // namespace Haki
