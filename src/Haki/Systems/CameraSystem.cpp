#include "Haki/Systems/CameraSystem.h"

#include "Haki/Input/InputManager.h"
#include "Haki/Scene/Components.h"

#include <entt/entt.hpp>

#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace Haki
{

CameraSystem::CameraSystem(DebugCameraSettings settings)
    : settings_(settings), movementSpeed_(settings.MovementSpeed)
{
    pose_.WorldMatrix = glm::translate(glm::mat4(1.0f), pose_.Translation);
}

bool CameraSystem::Activate(const entt::registry *registry)
{
    if (!registry)
    {
        return false;
    }
    const auto cameras = registry->view<TransformComponent, CameraComponent>();
    for (const entt::entity entity : cameras)
    {
        const CameraComponent &camera = cameras.get<CameraComponent>(entity);
        if (!camera.Primary)
        {
            continue;
        }
        const TransformComponent &transform = cameras.get<TransformComponent>(entity);
        pose_.Translation = transform.Translation;
        pose_.Rotation = glm::normalize(transform.Rotation);
        pose_.Scale = glm::vec3(1.0f);
        pose_.WorldMatrix = glm::translate(glm::mat4(1.0f), pose_.Translation) * glm::mat4_cast(pose_.Rotation);
        projection_ = camera;
        orthographicSize_ = camera.OrthographicSize;
        return true;
    }
    return false;
}

void CameraSystem::Update(float deltaTime)
{
    using Input::InputManager;

    const bool lookActive = InputManager::isMouseButtonDown(gainput::MouseButtonLeft);
    const bool wasCaptured = InputManager::isCursorCaptured();
    if (wasCaptured != lookActive)
    {
        InputManager::setCursorCaptured(lookActive);
    }

    DebugCameraInput input;
    input.LookActive = lookActive;
    input.LookDeltaPixels = lookActive && wasCaptured ? InputManager::getMouseDeltaPixels() : glm::vec2(0.0f);
    input.SpeedAdjustment = InputManager::getMouseWheelDelta();
    input.MoveForward = InputManager::isKeyDown(gainput::KeyW);
    input.MoveBackward = InputManager::isKeyDown(gainput::KeyS);
    input.MoveLeft = InputManager::isKeyDown(gainput::KeyA);
    input.MoveRight = InputManager::isKeyDown(gainput::KeyD);
    input.MoveDown = InputManager::isKeyDown(gainput::KeyQ);
    input.MoveUp = InputManager::isKeyDown(gainput::KeyE);
    input.Boost = InputManager::isKeyDown(gainput::KeyShiftL) ||
                  InputManager::isKeyDown(gainput::KeyShiftR);
    Update(deltaTime, input);
}

bool CameraSystem::Update(float deltaTime, const DebugCameraInput &input)
{
    TransformComponent *transform = &pose_;

    const bool hasMovement = input.MoveForward || input.MoveBackward || input.MoveLeft || input.MoveRight ||
                             input.MoveDown || input.MoveUp;
    if (!input.LookActive && !hasMovement && input.SpeedAdjustment == 0.0f)
    {
        return false;
    }

    if (input.SpeedAdjustment != 0.0f)
    {
        if (orthographic_ && !input.Boost)
        {
            // Orthographic has no depth to fly through: the wheel zooms instead (shift+wheel still sets speed).
            orthographicSize_ *= std::pow(settings_.SpeedStep, -input.SpeedAdjustment);
            orthographicSize_ = std::clamp(orthographicSize_, settings_.MinimumOrthographicSize,
                                           settings_.MaximumOrthographicSize);
        }
        else
        {
            movementSpeed_ *= std::pow(settings_.SpeedStep, input.SpeedAdjustment);
            movementSpeed_ = std::clamp(movementSpeed_, settings_.MinimumSpeed, settings_.MaximumSpeed);
        }
    }

    if (input.LookActive)
    {
        const glm::quat currentRotation = glm::normalize(transform->Rotation);
        const glm::vec3 currentForward = currentRotation * glm::vec3(0.0f, 0.0f, -1.0f);
        float yaw = std::atan2(-currentForward.x, -currentForward.z);
        float pitch = std::asin(glm::clamp(currentForward.y, -1.0f, 1.0f));
        yaw -= input.LookDeltaPixels.x * settings_.LookSensitivity;
        pitch -= input.LookDeltaPixels.y * settings_.LookSensitivity;
        const float maximumPitch = glm::radians(settings_.MaximumPitchDegrees);
        pitch = glm::clamp(pitch, -maximumPitch, maximumPitch);
        transform->Rotation = glm::normalize(glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                             glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)));
    }

    const glm::vec3 forward = transform->Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right = transform->Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 movement(0.0f);
    movement += forward * static_cast<float>(input.MoveForward - input.MoveBackward);
    movement += right * static_cast<float>(input.MoveRight - input.MoveLeft);
    movement.y += static_cast<float>(input.MoveUp - input.MoveDown);
    if (const float length = glm::length(movement); length > 0.0f)
    {
        const float speed = movementSpeed_ * (input.Boost ? settings_.BoostMultiplier : 1.0f);
        transform->Translation += movement / length * speed * std::max(deltaTime, 0.0f);
    }

    transform->WorldMatrix = glm::translate(glm::mat4(1.0f), transform->Translation) *
                             glm::mat4_cast(transform->Rotation) *
                             glm::scale(glm::mat4(1.0f), transform->Scale);
    return true;
}

void CameraSystem::SetOrthographicZoom(bool enabled, float size)
{
    orthographic_ = enabled;
    if (std::isfinite(size) && size > 0.0f)
    {
        orthographicSize_ = std::clamp(size, settings_.MinimumOrthographicSize, settings_.MaximumOrthographicSize);
    }
}

void CameraSystem::Reset()
{
    Input::InputManager::setCursorCaptured(false);
    movementSpeed_ = settings_.MovementSpeed;
}

SceneCameraOverride CameraSystem::GetRenderCamera() const
{
    SceneCameraOverride camera;
    camera.WorldMatrix = pose_.WorldMatrix;
    camera.Position = pose_.Translation;
    camera.Projection = projection_.Projection;
    camera.FieldOfViewDegrees = projection_.FieldOfViewDegrees;
    camera.OrthographicSize = orthographicSize_; // starts at the scene camera's size; the wheel zooms it
    camera.NearPlane = projection_.NearPlane;
    camera.FarPlane = projection_.FarPlane;
    return camera;
}

} // namespace Haki
