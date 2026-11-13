#pragma once

#include "Haki/Scene/Components.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace Haki
{

// View > Camera > Projection: a debug-only override of how the current view is projected.
enum class DebugProjectionMode
{
    AsAuthored, // whatever the scene camera (or the free-look copy of it) says
    Perspective,
    Orthographic
};

// A camera that replaces the scene's primary camera for one render, e.g. the debug free-look camera.
// The scene's own camera entities are left untouched.
struct SceneCameraOverride
{
    glm::mat4 WorldMatrix{1.0f};
    glm::vec3 Position{0.0f};
    CameraProjection Projection = CameraProjection::Perspective;
    float FieldOfViewDegrees = 60.0f;
    float OrthographicSize = 10.0f; // half the view height in metres
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
};

// Perspective: vertical field of view. Orthographic: the view is 2 * orthographicSize metres tall and
// as wide as the aspect ratio makes it.
inline glm::mat4 BuildProjection(CameraProjection projection, float fieldOfViewDegrees, float orthographicSize,
                                 float aspect, float nearPlane, float farPlane)
{
    if (projection == CameraProjection::Orthographic)
    {
        const float halfHeight = std::max(orthographicSize, 0.001f);
        const float halfWidth = halfHeight * aspect;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
    }
    return glm::perspective(glm::radians(fieldOfViewDegrees), aspect, nearPlane, farPlane);
}

inline glm::mat4 BuildProjection(const SceneCameraOverride &camera, float aspect)
{
    return BuildProjection(camera.Projection, camera.FieldOfViewDegrees, camera.OrthographicSize, aspect,
                           camera.NearPlane, camera.FarPlane);
}

// Orthographic size that frames the ground (y = 0) about as large as the given view does, so switching a
// perspective view to orthographic keeps the scene roughly the same size on screen. Falls back to 10 m when
// the camera does not look at the ground.
inline float OrthographicSizeMatchingView(const SceneCameraOverride &view)
{
    if (view.Projection == CameraProjection::Orthographic)
    {
        return view.OrthographicSize;
    }
    const glm::vec3 forward = -glm::vec3(view.WorldMatrix[2]);
    const float forwardLength = std::max(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z, 0.0f);
    if (!(forwardLength > 1e-8f))
    {
        return 10.0f;
    }
    const float down = forward.y / std::sqrt(forwardLength);
    if (down > -0.01f || view.Position.y <= 0.0f)
    {
        return 10.0f;
    }
    const float distance = view.Position.y / -down; // along the view ray to the ground plane
    const float size = distance * std::tan(glm::radians(view.FieldOfViewDegrees) * 0.5f);
    return std::isfinite(size) ? std::clamp(size, 0.5f, 500.0f) : 10.0f;
}

} // namespace Haki
