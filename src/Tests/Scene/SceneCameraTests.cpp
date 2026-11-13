#include "Haki/Scene/SceneCamera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>

namespace
{

void Check(bool condition, const char *name, int &failures)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += condition ? 0 : 1;
}

bool Near(float a, float b, float epsilon = 0.0001f)
{
    return std::abs(a - b) <= epsilon;
}

glm::vec3 ToNdc(const glm::mat4 &projection, const glm::vec3 &viewPosition)
{
    const glm::vec4 clip = projection * glm::vec4(viewPosition, 1.0f);
    return glm::vec3(clip) / clip.w;
}

} // namespace

int main()
{
    using namespace Haki;
    int failures = 0;
    const float aspect = 16.0f / 9.0f;

    const glm::mat4 ortho = BuildProjection(CameraProjection::Orthographic, 60.0f, 5.0f, aspect, 0.5f, 50.0f);
    const glm::vec3 nearCorner = ToNdc(ortho, {5.0f * aspect, 5.0f, -0.5f});
    const glm::vec3 farCorner = ToNdc(ortho, {-5.0f * aspect, -5.0f, -50.0f});
    Check(Near(nearCorner.x, 1.0f) && Near(nearCorner.y, 1.0f) && Near(nearCorner.z, -1.0f),
          "orthographic: top-right of the near plane maps to the clip corner", failures);
    Check(Near(farCorner.x, -1.0f) && Near(farCorner.y, -1.0f) && Near(farCorner.z, 1.0f),
          "orthographic: bottom-left of the far plane maps to the opposite corner", failures);
    const glm::vec3 close = ToNdc(ortho, {2.0f, 1.0f, -1.0f});
    const glm::vec3 distant = ToNdc(ortho, {2.0f, 1.0f, -40.0f});
    Check(Near(close.x, distant.x) && Near(close.y, distant.y),
          "orthographic: objects keep their screen size at any distance", failures);
    Check(ortho[3][3] == 1.0f, "orthographic projections are affine (w stays 1)", failures);

    const glm::mat4 perspective = BuildProjection(CameraProjection::Perspective, 50.0f, 5.0f, aspect, 0.1f, 200.0f);
    const glm::mat4 expected = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 200.0f);
    bool same = true;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            same = same && Near(perspective[column][row], expected[column][row]);
    Check(same, "perspective matches glm::perspective", failures);

    SceneCameraOverride view;
    view.Position = {0.0f, 10.0f, 10.0f};
    const glm::quat lookDown = glm::angleAxis(glm::radians(-45.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    view.WorldMatrix = glm::translate(glm::mat4(1.0f), view.Position) * glm::mat4_cast(lookDown);
    view.FieldOfViewDegrees = 60.0f;
    const float distance = std::sqrt(200.0f); // camera to the ground along a 45 degree ray
    Check(Near(OrthographicSizeMatchingView(view), distance * std::tan(glm::radians(30.0f)), 0.001f),
          "switching to orthographic keeps the ground at about the same size", failures);
    view.WorldMatrix = glm::translate(glm::mat4(1.0f), view.Position); // looking at the horizon
    Check(Near(OrthographicSizeMatchingView(view), 10.0f), "a camera not facing the ground falls back to 10 m",
          failures);
    view.Projection = CameraProjection::Orthographic;
    view.OrthographicSize = 3.0f;
    Check(Near(OrthographicSizeMatchingView(view), 3.0f), "an orthographic view keeps its own size", failures);
    return failures == 0 ? 0 : 1;
}
