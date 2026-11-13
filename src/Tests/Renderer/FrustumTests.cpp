#include "Haki/Renderer/Frustum.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <iostream>
#include <limits>
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

bool Near(const glm::vec3 &left, const glm::vec3 &right, float epsilon = 0.0001f)
{
    return glm::length(left - right) <= epsilon;
}

Haki::WorldBounds Box(const glm::vec3 &center, const glm::vec3 &extents = glm::vec3(0.5f))
{
    return {center, extents};
}

} // namespace

int main()
{
    TestRunner tests("Frustum culling");

    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    const Haki::Frustum camera = Haki::Frustum::FromViewProjection(projection * view);

    tests.Check(camera.Intersects(Box(glm::vec3(0.0f))), "box in front of the camera is visible");
    tests.Check(!camera.Intersects(Box(glm::vec3(0.0f, 0.0f, 50.0f))), "box behind the camera is culled");
    tests.Check(!camera.Intersects(Box(glm::vec3(100.0f, 0.0f, 0.0f))), "box far to the side is culled");
    tests.Check(!camera.Intersects(Box(glm::vec3(0.0f, 0.0f, -500.0f))), "box beyond the far plane is culled");
    // The left plane at view depth 3 sits at x = -3 * tan(30deg) ~= -1.732; this box straddles it.
    tests.Check(camera.Intersects(Box(glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(0.5f))),
                "box straddling a side plane is visible");
    tests.Check(camera.Intersects(Box(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.2f))),
                "box straddling the near plane is visible");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    tests.Check(camera.Intersects(Box(glm::vec3(nan, 0.0f, 0.0f))), "non-finite bounds are treated as visible");

    const glm::mat4 ortho = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 50.0f) * view;
    const Haki::Frustum orthographic = Haki::Frustum::FromViewProjection(ortho);
    tests.Check(orthographic.Intersects(Box(glm::vec3(9.0f, 0.0f, 0.0f))), "orthographic frustum keeps inside box");
    tests.Check(!orthographic.Intersects(Box(glm::vec3(12.0f, 0.0f, 0.0f))),
                "orthographic frustum culls box outside its width");

    const Haki::MeshBounds local{glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 2.0f, 3.0f)};
    const glm::mat4 translateScale = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)),
                                                glm::vec3(2.0f));
    const Haki::WorldBounds scaled = Haki::TransformBounds(local, translateScale);
    tests.Check(Near(scaled.Center, glm::vec3(2.0f, 5.0f, 0.0f)) && Near(scaled.Extents, glm::vec3(2.0f, 4.0f, 6.0f)),
                "translation and scale transform AABB center and extents");

    const glm::mat4 rotateY = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const Haki::WorldBounds rotated = Haki::TransformBounds(local, rotateY);
    tests.Check(Near(rotated.Center, glm::vec3(0.0f, 0.0f, -1.0f)) &&
                    Near(rotated.Extents, glm::vec3(3.0f, 2.0f, 1.0f)),
                "90 degree rotation swaps AABB extents");

    const glm::mat4 rotate45 = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const Haki::WorldBounds diagonal =
        Haki::TransformBounds({glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 1.0f)}, rotate45);
    const float expected = std::sqrt(2.0f);
    tests.Check(Near(diagonal.Extents, glm::vec3(expected, expected, 1.0f)),
                "45 degree rotation grows AABB to enclose the rotated box");

    return tests.Result();
}
