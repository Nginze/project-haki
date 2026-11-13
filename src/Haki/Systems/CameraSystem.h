#pragma once

#include "Haki/Scene/Components.h"
#include "Haki/Scene/SceneCamera.h"

#include <entt/entt.hpp>

#include <glm/vec2.hpp>

namespace Haki
{

struct DebugCameraInput
{
    glm::vec2 LookDeltaPixels{0.0f};
    float SpeedAdjustment = 0.0f;
    bool LookActive = false;
    bool MoveForward = false;
    bool MoveBackward = false;
    bool MoveLeft = false;
    bool MoveRight = false;
    bool MoveDown = false;
    bool MoveUp = false;
    bool Boost = false;
};

struct DebugCameraSettings
{
    float MovementSpeed = 5.0f;
    float BoostMultiplier = 4.0f;
    float LookSensitivity = 0.0025f;
    float MaximumPitchDegrees = 89.0f;
    float SpeedStep = 1.25f;
    float MinimumSpeed = 0.1f;
    float MaximumSpeed = 1000.0f;
    float MinimumOrthographicSize = 0.5f;
    float MaximumOrthographicSize = 500.0f;
};

// Debug free-look camera. It flies its own pose and is rendered through a SceneCameraOverride, so the
// scene's cameras are never modified and switching back restores the scene view exactly.
class CameraSystem
{
public:
    explicit CameraSystem(DebugCameraSettings settings = {});

    // Starts from the scene's primary camera pose and projection. Returns false (keeping the current pose)
    // when the registry has no primary camera.
    bool Activate(const entt::registry *registry);

    // Reads WASD/QE, shift, the mouse wheel and left-mouse look from the InputManager.
    // The wheel changes fly speed, or zooms while orthographic zoom is enabled (shift+wheel: speed).
    void Update(float deltaTime);
    // Returns true when the pose changed.
    bool Update(float deltaTime, const DebugCameraInput &input);
    // Releases the cursor and restores the default speed.
    void Reset();

    const TransformComponent &GetPose() const { return pose_; }
    SceneCameraOverride GetRenderCamera() const;
    float GetMovementSpeed() const { return movementSpeed_; }

    // While enabled, the mouse wheel zooms this orthographic size instead of changing the fly speed.
    void SetOrthographicZoom(bool enabled, float size);
    float GetOrthographicSize() const { return orthographicSize_; }

private:
    DebugCameraSettings settings_;
    TransformComponent pose_;
    CameraComponent projection_;
    float movementSpeed_ = 0.0f;
    bool orthographic_ = false;
    float orthographicSize_ = 10.0f;
};

} // namespace Haki
