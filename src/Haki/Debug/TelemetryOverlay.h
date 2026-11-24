#pragma once

#include "Haki/Renderer/GpuInfo.h"
#include "Haki/Renderer/RendererTypes.h"

#include <glm/vec2.hpp>

#include <deque>
#include <string>
#include <vector>

namespace Haki {

class Canvas;
class Font;
struct SceneCpuTimings;

// Rolling frame-rate statistics over the last `window` seconds.
class FrameStats {
public:
  explicit FrameStats(float window = 1.0f) : window_(window) {}

  void AddFrame(float seconds);
  float GetAverageFps() const;
  // The slowest frame in the window, as frames per second.
  float GetMinimumFps() const;
  float GetAverageFrameMs() const;
  std::size_t GetFrameCount() const { return frames_.size(); }

private:
  float window_;
  float total_ = 0.0f;
  std::deque<float> frames_;
};

// Top-left of the telemetry block: just inside the gameplay area, past the menu
// bar and the left pane when they are visible.
glm::vec2 ComputeTelemetryOrigin(bool menuBarVisible, float menuBarHeight,
                                 bool hierarchyVisible, float hierarchyWidth,
                                 float margin = 12.0f);

// Per-frame CPU milliseconds of the engine's main stages.
struct CpuTimings {
  float PhysicsMs = 0.0f;
  float ScriptsMs = 0.0f;
  float AnimationMs = 0.0f;
  float SceneMs = 0.0f;
  float RenderMs = 0.0f;
  float EditorMs = 0.0f;
};

// Everything the telemetry block shows; built once per refresh.
struct TelemetrySnapshot {
  int WindowWidth = 0, WindowHeight = 0;
  int GameWidth = 0, GameHeight = 0;
  int RenderWidth = 0, RenderHeight = 0;
  std::string Gpu;
  float AverageFps = 0.0f;
  float MinimumFps = 0.0f;
  float FrameMs = 0.0f;
  GpuInfo::MemorySource MemorySource = GpuInfo::MemorySource::None;
  int VramUsedMb = -1;
  int VramTotalMb = -1;
  int VramFreeMb = -1;
  RendererGpuTimings GpuTimings;
  bool GpuTimingsEnabled = true;
  bool ShadowsEnabled = true;
  bool SsaoEnabled = true;
  bool BloomEnabled = true;
  CpuTimings Cpu;
  bool Paused = false;
};

// One HUD row: a short label column and its value text.
struct TelemetryLine {
  std::string Label;
  std::string Value;
};
std::vector<TelemetryLine>
BuildTelemetryLines(const TelemetrySnapshot &snapshot);

// Performance HUD drawn with the engine's own Canvas text (not ImGui).
class TelemetryOverlay {
public:
  // Requires a current OpenGL context.
  void Initialize();
  // Call once per frame. sceneCpu may be null (no active scene).
  void Update(float frameSeconds, const SceneCpuTimings *sceneCpu,
              float renderMs, float editorMs,
              const RendererGpuTimings &gpuTimings,
              const RendererSettings &settings, bool paused,
              int windowWidth = 0, int windowHeight = 0, int gameWidth = 0,
              int gameHeight = 0, int renderWidth = 0, int renderHeight = 0);
  void Draw(Canvas &canvas, const Font &font, glm::vec2 origin,
            float pixelSize = 30.0f) const;

  const std::vector<TelemetryLine> &GetLines() const { return lines_; }

private:
  GpuInfo gpu_;
  FrameStats frames_;
  CpuTimings smoothedCpu_;
  bool hasCpu_ = false;
  float sinceRefresh_ = 1.0f;
  float sinceMemory_ = 1.0f;
  std::vector<TelemetryLine> lines_;
};

} // namespace Haki
