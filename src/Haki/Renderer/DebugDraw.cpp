#include "Haki/Renderer/DebugDraw.h"

#include <glm/gtc/constants.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>

namespace Haki
{
namespace
{

// Pairs of corner indices forming the 12 edges of a box whose corners are ordered by bits (x, y, z).
constexpr std::array<std::array<int, 2>, 12> BoxEdges{{
    {0, 1}, {2, 3}, {4, 5}, {6, 7}, // along x
    {0, 2}, {1, 3}, {4, 6}, {5, 7}, // along y
    {0, 4}, {1, 5}, {2, 6}, {3, 7}, // along z
}};

glm::vec3 CornerSign(int index)
{
    return {(index & 1) ? 1.0f : -1.0f, (index & 2) ? 1.0f : -1.0f, (index & 4) ? 1.0f : -1.0f};
}

} // namespace

void DebugDraw::Line(const glm::vec3 &from, const glm::vec3 &to, const glm::vec4 &color, bool depthTest)
{
    if (GetLineCount() >= MaxLines)
    {
        return;
    }
    std::vector<DebugVertex> &target = depthTest ? depthTested_ : overlay_;
    target.push_back({from, color});
    target.push_back({to, color});
}

void DebugDraw::Corners(const std::array<glm::vec3, 8> &corners, const glm::vec4 &color, bool depthTest)
{
    for (const auto &edge : BoxEdges)
    {
        Line(corners[edge[0]], corners[edge[1]], color, depthTest);
    }
}

void DebugDraw::Box(const glm::vec3 &center, const glm::vec3 &extents, const glm::vec4 &color, bool depthTest)
{
    std::array<glm::vec3, 8> corners;
    for (int index = 0; index < 8; ++index)
    {
        corners[index] = center + extents * CornerSign(index);
    }
    Corners(corners, color, depthTest);
}

void DebugDraw::Box(const glm::mat4 &transform, const glm::vec3 &center, const glm::vec3 &extents,
                    const glm::vec4 &color, bool depthTest)
{
    std::array<glm::vec3, 8> corners;
    for (int index = 0; index < 8; ++index)
    {
        corners[index] = glm::vec3(transform * glm::vec4(center + extents * CornerSign(index), 1.0f));
    }
    Corners(corners, color, depthTest);
}

void DebugDraw::Sphere(const glm::vec3 &center, float radius, const glm::vec4 &color, bool depthTest, int segments)
{
    segments = std::clamp(segments, 4, 256);
    const float step = glm::two_pi<float>() / static_cast<float>(segments);
    for (int index = 0; index < segments; ++index)
    {
        const float a0 = step * static_cast<float>(index);
        const float a1 = step * static_cast<float>(index + 1);
        const glm::vec2 p0(std::cos(a0) * radius, std::sin(a0) * radius);
        const glm::vec2 p1(std::cos(a1) * radius, std::sin(a1) * radius);
        Line(center + glm::vec3(p0.x, p0.y, 0.0f), center + glm::vec3(p1.x, p1.y, 0.0f), color, depthTest);
        Line(center + glm::vec3(p0.x, 0.0f, p0.y), center + glm::vec3(p1.x, 0.0f, p1.y), color, depthTest);
        Line(center + glm::vec3(0.0f, p0.x, p0.y), center + glm::vec3(0.0f, p1.x, p1.y), color, depthTest);
    }
}

void DebugDraw::Frustum(const glm::mat4 &viewProjection, const glm::vec4 &color, bool depthTest)
{
    const glm::mat4 inverse = glm::inverse(viewProjection);
    std::array<glm::vec3, 8> corners;
    for (int index = 0; index < 8; ++index)
    {
        const glm::vec4 world = inverse * glm::vec4(CornerSign(index), 1.0f);
        if (std::abs(world.w) <= 1e-8f)
        {
            return;
        }
        corners[index] = glm::vec3(world) / world.w;
    }
    Corners(corners, color, depthTest);
}

void DebugDraw::Axes(const glm::mat4 &transform, float size, bool depthTest)
{
    const glm::vec3 origin(transform[3]);
    Line(origin, origin + glm::vec3(transform[0]) * size, {1.0f, 0.2f, 0.2f, 1.0f}, depthTest);
    Line(origin, origin + glm::vec3(transform[1]) * size, {0.2f, 1.0f, 0.2f, 1.0f}, depthTest);
    Line(origin, origin + glm::vec3(transform[2]) * size, {0.3f, 0.5f, 1.0f, 1.0f}, depthTest);
}

void DebugDraw::Grid(const glm::vec3 &center, float halfSize, int divisions, const glm::vec4 &color, bool depthTest)
{
    divisions = std::clamp(divisions, 1, 1024);
    const float spacing = halfSize * 2.0f / static_cast<float>(divisions);
    for (int index = 0; index <= divisions; ++index)
    {
        const float offset = -halfSize + spacing * static_cast<float>(index);
        Line(center + glm::vec3(offset, 0.0f, -halfSize), center + glm::vec3(offset, 0.0f, halfSize), color,
             depthTest);
        Line(center + glm::vec3(-halfSize, 0.0f, offset), center + glm::vec3(halfSize, 0.0f, offset), color,
             depthTest);
    }
}

void DebugDraw::Clear()
{
    depthTested_.clear();
    overlay_.clear();
}

} // namespace Haki
