#include "Haki/Scene/Components.h"
#include "Haki/Systems/CameraSystem.h"

#include <entt/entt.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{

class TestRunner
{
public:
    explicit TestRunner(std::string_view name) { std::cout << "\n=== " << name << " ===\n"; }

    void Check(bool condition, std::string_view name)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
        failures_ += condition ? 0 : 1;
    }

    int Result() const { return failures_ == 0 ? 0 : 1; }

private:
    int failures_ = 0;
};

bool Near(float left, float right, float epsilon = 0.0001f)
{
    return std::abs(left - right) <= epsilon;
}

bool Near(const glm::vec3 &left, const glm::vec3 &right, float epsilon = 0.0001f)
{
    return glm::length(left - right) <= epsilon;
}

Haki::TransformComponent &CreateCamera(entt::registry &registry, bool primary = true)
{
    const entt::entity entity = registry.create();
    auto &transform = registry.emplace<Haki::TransformComponent>(entity);
    registry.emplace<Haki::CameraComponent>(entity).Primary = primary;
    return transform;
}

} // namespace

Haki::CameraSystem ActivatedSystem(const Haki::DebugCameraSettings &settings)
{
    entt::registry registry;
    CreateCamera(registry);
    Haki::CameraSystem system(settings);
    system.Activate(&registry);
    return system;
}

int main()
{
    TestRunner tests("CameraSystem.DebugFreeLook");

    Haki::DebugCameraSettings settings;
    settings.MovementSpeed = 6.0f;
    settings.BoostMultiplier = 3.0f;
    settings.LookSensitivity = 0.01f;
    settings.SpeedStep = 2.0f;

    Haki::DebugCameraInput activeInput;
    activeInput.MoveForward = true;

    {
        entt::registry noPrimary;
        CreateCamera(noPrimary, false);
        Haki::CameraSystem system(settings);
        tests.Check(!system.Activate(&noPrimary), "activation without a primary camera is refused");
        tests.Check(!system.Activate(nullptr), "activation without a scene is refused");
    }

    entt::registry registry;
    Haki::TransformComponent &secondary = CreateCamera(registry, false);
    secondary.Translation = {20.0f, 0.0f, 0.0f};
    Haki::TransformComponent &primary = CreateCamera(registry);
    primary.Translation = {1.0f, 2.0f, 3.0f};
    primary.Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    for (const entt::entity entity : registry.view<Haki::CameraComponent>())
    {
        auto &camera = registry.get<Haki::CameraComponent>(entity);
        if (camera.Primary) camera.FieldOfViewDegrees = 45.0f;
    }

    Haki::CameraSystem system(settings);
    tests.Check(system.Activate(&registry), "activation finds the primary camera");
    tests.Check(Near(system.GetPose().Translation, primary.Translation) &&
                    Near(glm::abs(glm::dot(system.GetPose().Rotation, primary.Rotation)), 1.0f),
                "activation starts from the scene camera pose");
    tests.Check(Near(system.GetRenderCamera().FieldOfViewDegrees, 45.0f),
                "activation copies the scene camera projection");

    Haki::DebugCameraInput inactiveInput;
    tests.Check(!system.Update(1.0f, inactiveInput), "idle input leaves the camera unchanged");

    Haki::DebugCameraInput diagonalInput;
    diagonalInput.MoveForward = true;
    diagonalInput.MoveRight = true;
    tests.Check(system.Update(1.0f, diagonalInput), "movement works without a mouse button");
    tests.Check(Near(glm::length(system.GetPose().Translation - glm::vec3(1.0f, 2.0f, 3.0f)), 6.0f),
                "diagonal movement is normalized");
    tests.Check(Near(primary.Translation, glm::vec3(1.0f, 2.0f, 3.0f)) &&
                    Near(secondary.Translation, glm::vec3(20.0f, 0.0f, 0.0f)),
                "scene cameras are never modified");
    tests.Check(Near(glm::vec3(system.GetRenderCamera().WorldMatrix[3]), system.GetPose().Translation) &&
                    Near(system.GetRenderCamera().Position, system.GetPose().Translation),
                "render camera follows the free-look pose immediately");

    Haki::CameraSystem boosted = ActivatedSystem(settings);
    Haki::DebugCameraInput boostedInput;
    boostedInput.MoveForward = true;
    boostedInput.Boost = true;
    boosted.Update(0.5f, boostedInput);
    tests.Check(Near(boosted.GetPose().Translation, glm::vec3(0.0f, 0.0f, -9.0f)),
                "shift boost multiplies movement speed");

    Haki::CameraSystem rotated = ActivatedSystem(settings);
    Haki::DebugCameraInput lookInput;
    lookInput.LookActive = true;
    lookInput.LookDeltaPixels = {100.0f, -10000.0f};
    rotated.Update(0.0f, lookInput);
    const glm::vec3 rotatedForward = rotated.GetPose().Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    tests.Check(rotatedForward.x > 0.0f, "moving the mouse right turns the camera right");
    tests.Check(rotatedForward.y < 1.0f && rotatedForward.y > std::sin(glm::radians(88.0f)),
                "pitch is clamped below ninety degrees");

    Haki::CameraSystem speedSystem = ActivatedSystem(settings);
    Haki::DebugCameraInput speedInput;
    speedInput.MoveForward = true;
    speedInput.SpeedAdjustment = 1.0f;
    speedSystem.Update(1.0f, speedInput);
    tests.Check(Near(speedSystem.GetMovementSpeed(), 12.0f), "mouse wheel adjusts persistent movement speed");
    tests.Check(Near(speedSystem.GetPose().Translation.z, -12.0f), "adjusted speed applies in the same frame");

    Haki::CameraSystem zoomSystem = ActivatedSystem(settings);
    zoomSystem.SetOrthographicZoom(true, 10.0f);
    Haki::DebugCameraInput zoomInput;
    zoomInput.SpeedAdjustment = 1.0f;
    const float speedBeforeZoom = zoomSystem.GetMovementSpeed();
    zoomSystem.Update(1.0f, zoomInput);
    tests.Check(Near(zoomSystem.GetOrthographicSize(), 10.0f / settings.SpeedStep) &&
                    Near(zoomSystem.GetMovementSpeed(), speedBeforeZoom),
                "in orthographic zoom the wheel zooms in instead of changing speed");
    tests.Check(Near(zoomSystem.GetRenderCamera().OrthographicSize, zoomSystem.GetOrthographicSize()),
                "the render camera uses the zoomed orthographic size");
    zoomInput.Boost = true;
    zoomSystem.Update(1.0f, zoomInput);
    tests.Check(Near(zoomSystem.GetOrthographicSize(), 10.0f / settings.SpeedStep) &&
                    Near(zoomSystem.GetMovementSpeed(), speedBeforeZoom * settings.SpeedStep),
                "shift+wheel still changes speed while zooming orthographically");

    Haki::CameraSystem wholeFrameSystem = ActivatedSystem(settings);
    Haki::CameraSystem splitFrameSystem = ActivatedSystem(settings);
    wholeFrameSystem.Update(1.0f, activeInput);
    splitFrameSystem.Update(0.5f, activeInput);
    splitFrameSystem.Update(0.5f, activeInput);
    tests.Check(Near(wholeFrameSystem.GetPose().Translation, splitFrameSystem.GetPose().Translation),
                "translation is frame-rate independent");

    return tests.Result();
}
