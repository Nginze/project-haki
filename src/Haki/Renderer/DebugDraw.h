#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace Haki
{

struct DebugVertex
{
    glm::vec3 Position{0.0f};
    glm::vec4 Color{1.0f};
};

// Immediate-mode debug geometry. Shapes queued here are drawn by the renderer at the end of the
// next frame (after tone mapping, so colours are exact) and then cleared. Queue shapes every frame
// you want them visible. Depth-tested shapes are hidden behind scene geometry; overlay shapes
// always draw on top.
class DebugDraw
{
public:
    void Line(const glm::vec3 &from, const glm::vec3 &to, const glm::vec4 &color, bool depthTest = true);
    void Box(const glm::vec3 &center, const glm::vec3 &extents, const glm::vec4 &color, bool depthTest = true);
    void Box(const glm::mat4 &transform, const glm::vec3 &center, const glm::vec3 &extents,
             const glm::vec4 &color, bool depthTest = true);
    void Sphere(const glm::vec3 &center, float radius, const glm::vec4 &color, bool depthTest = true,
                int segments = 24);
    // Draws the frustum of any view-projection matrix (camera or light) by unprojecting the NDC cube.
    void Frustum(const glm::mat4 &viewProjection, const glm::vec4 &color, bool depthTest = true);
    // Red, green and blue lines along the transform's X, Y and Z axes.
    void Axes(const glm::mat4 &transform, float size = 1.0f, bool depthTest = true);
    void Grid(const glm::vec3 &center, float halfSize, int divisions, const glm::vec4 &color,
              bool depthTest = true);

    void Clear();
    bool IsEmpty() const { return depthTested_.empty() && overlay_.empty(); }
    std::size_t GetLineCount() const { return (depthTested_.size() + overlay_.size()) / 2; }
    const std::vector<DebugVertex> &GetDepthTestedVertices() const { return depthTested_; }
    const std::vector<DebugVertex> &GetOverlayVertices() const { return overlay_; }

    // Caps queued lines so a runaway loop cannot exhaust memory; extra lines are dropped.
    static constexpr std::size_t MaxLines = 1u << 20;

private:
    void Corners(const std::array<glm::vec3, 8> &corners, const glm::vec4 &color, bool depthTest);

    std::vector<DebugVertex> depthTested_;
    std::vector<DebugVertex> overlay_;
};

} // namespace Haki
