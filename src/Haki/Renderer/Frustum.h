#pragma once

#include "Haki/GL/Mesh.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

namespace Haki
{

struct WorldBounds
{
    glm::vec3 Center{0.0f};
    glm::vec3 Extents{0.0f};
};

// Tight world-space AABB around a transformed local AABB (Arvo's method).
WorldBounds TransformBounds(const MeshBounds &bounds, const glm::mat4 &transform);

class Frustum
{
public:
    // Extracts the six clip planes of an OpenGL (-w..w) view-projection matrix (Gribb-Hartmann).
    static Frustum FromViewProjection(const glm::mat4 &viewProjection);

    // Conservative AABB test: returns false only when the box is fully outside a plane.
    // Non-finite bounds are always treated as visible.
    bool Intersects(const WorldBounds &bounds) const;

private:
    std::array<glm::vec4, 6> planes_{};
};

} // namespace Haki
