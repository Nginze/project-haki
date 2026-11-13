#include "Haki/Renderer/Frustum.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <cmath>

namespace Haki
{

WorldBounds TransformBounds(const MeshBounds &bounds, const glm::mat4 &transform)
{
    const glm::mat3 linear(transform);
    glm::mat3 absolute;
    for (int column = 0; column < 3; ++column)
    {
        absolute[column] = glm::abs(linear[column]);
    }
    return {glm::vec3(transform * glm::vec4(bounds.Center, 1.0f)), absolute * glm::abs(bounds.Extents)};
}

Frustum Frustum::FromViewProjection(const glm::mat4 &viewProjection)
{
    // glm is column-major: viewProjection[column][row]. Build the matrix rows explicitly.
    glm::vec4 rows[4];
    for (int row = 0; row < 4; ++row)
    {
        rows[row] = glm::vec4(viewProjection[0][row], viewProjection[1][row], viewProjection[2][row],
                              viewProjection[3][row]);
    }

    Frustum frustum;
    frustum.planes_ = {
        rows[3] + rows[0], // left
        rows[3] - rows[0], // right
        rows[3] + rows[1], // bottom
        rows[3] - rows[1], // top
        rows[3] + rows[2], // near
        rows[3] - rows[2], // far
    };
    for (glm::vec4 &plane : frustum.planes_)
    {
        const float length = glm::length(glm::vec3(plane));
        if (length > 0.0f && std::isfinite(length))
        {
            plane /= length;
        }
    }
    return frustum;
}

bool Frustum::Intersects(const WorldBounds &bounds) const
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(bounds.Center[axis]) || !std::isfinite(bounds.Extents[axis]))
        {
            return true;
        }
    }

    for (const glm::vec4 &plane : planes_)
    {
        const glm::vec3 normal(plane);
        const float radius = glm::dot(bounds.Extents, glm::abs(normal));
        const float distance = glm::dot(normal, bounds.Center) + plane.w;
        if (distance < -radius)
        {
            return false;
        }
    }
    return true;
}

} // namespace Haki
