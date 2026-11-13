#include "Haki/Renderer/DebugDraw.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

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

bool Near(const glm::vec3 &left, const glm::vec3 &right, float epsilon = 0.001f)
{
    return glm::length(left - right) <= epsilon;
}

} // namespace

int main()
{
    TestRunner tests("Debug draw");
    const glm::vec4 white(1.0f);

    Haki::DebugDraw draw;
    tests.Check(draw.IsEmpty() && draw.GetLineCount() == 0, "new debug draw is empty");

    draw.Line(glm::vec3(0.0f), glm::vec3(1.0f, 2.0f, 3.0f), white);
    draw.Line(glm::vec3(0.0f), glm::vec3(1.0f), white, false);
    tests.Check(draw.GetDepthTestedVertices().size() == 2 && draw.GetOverlayVertices().size() == 2,
                "depth-tested and overlay lines are kept in separate batches");
    tests.Check(Near(draw.GetDepthTestedVertices()[1].Position, glm::vec3(1.0f, 2.0f, 3.0f)),
                "line endpoints are stored in order");
    draw.Clear();
    tests.Check(draw.IsEmpty(), "clear empties both batches");

    draw.Box(glm::vec3(1.0f), glm::vec3(0.5f), white);
    tests.Check(draw.GetLineCount() == 12, "box draws 12 edges");
    bool cornersOnBox = true;
    for (const Haki::DebugVertex &vertex : draw.GetDepthTestedVertices())
    {
        const glm::vec3 offset = glm::abs(vertex.Position - glm::vec3(1.0f));
        cornersOnBox = cornersOnBox && Near(offset, glm::vec3(0.5f));
    }
    tests.Check(cornersOnBox, "box edges connect its corners");
    draw.Clear();

    draw.Sphere(glm::vec3(0.0f), 2.0f, white, true, 16);
    bool onSphere = true;
    for (const Haki::DebugVertex &vertex : draw.GetDepthTestedVertices())
    {
        onSphere = onSphere && std::abs(glm::length(vertex.Position) - 2.0f) < 0.001f;
    }
    tests.Check(draw.GetLineCount() == 48 && onSphere, "sphere draws three rings on its surface");
    draw.Clear();

    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.5f, 10.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    draw.Frustum(projection * view, white);
    float nearest = 1e9f;
    float farthest = 0.0f;
    for (const Haki::DebugVertex &vertex : draw.GetDepthTestedVertices())
    {
        const float depth = 5.0f - vertex.Position.z;
        nearest = std::min(nearest, depth);
        farthest = std::max(farthest, depth);
    }
    tests.Check(draw.GetLineCount() == 12 && std::abs(nearest - 0.5f) < 0.01f && std::abs(farthest - 10.0f) < 0.05f,
                "frustum unprojects to its near and far planes");
    draw.Clear();

    draw.Axes(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)), 3.0f);
    const auto &axes = draw.GetDepthTestedVertices();
    tests.Check(draw.GetLineCount() == 3 && Near(axes[0].Position, glm::vec3(2.0f, 0.0f, 0.0f)) &&
                    Near(axes[1].Position, glm::vec3(5.0f, 0.0f, 0.0f)) &&
                    Near(axes[3].Position, glm::vec3(2.0f, 3.0f, 0.0f)),
                "axes start at the transform origin and scale with size");
    draw.Clear();

    draw.Grid(glm::vec3(0.0f), 5.0f, 10, white);
    tests.Check(draw.GetLineCount() == 22, "grid draws divisions + 1 lines per axis");

    return tests.Result();
}
