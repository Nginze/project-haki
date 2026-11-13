#include "Haki/GL/Texture.h"
#include "Haki/Renderer/Canvas.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

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

} // namespace

int main()
{
    TestRunner tests("Canvas");
    // Textures are only used as batch keys here, so no GL context is needed.
    const Haki::Texture white;
    const Haki::Texture atlasA;
    const Haki::Texture atlasB;
    std::vector<Haki::CanvasVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Haki::CanvasBatch> batches;

    Haki::Canvas canvas;
    tests.Check(canvas.IsEmpty(), "new canvas is empty");

    canvas.FillRect({10.0f, 20.0f, 30.0f, 40.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
    canvas.BuildBatches(&white, vertices, indices, batches);
    tests.Check(canvas.GetQuadCount() == 1 && vertices.size() == 4 && indices.size() == 6,
                "fill rect emits one quad");
    tests.Check(vertices[0].Position == glm::vec2(10.0f, 20.0f) && vertices[2].Position == glm::vec2(40.0f, 60.0f),
                "quad corners use top-left origin pixel coordinates");
    tests.Check(indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}, "quads are two triangles");
    tests.Check(batches.size() == 1 && batches[0].BatchTexture == &white,
                "untextured quads use the white texture");
    canvas.Clear();

    canvas.FillRect({0.0f, 0.0f, 0.0f, 10.0f}, glm::vec4(1.0f));
    canvas.FillRect({0.0f, 0.0f, 10.0f, -5.0f}, glm::vec4(1.0f));
    tests.Check(canvas.IsEmpty(), "zero and negative sized rects are skipped");

    canvas.StrokeRect({0.0f, 0.0f, 100.0f, 50.0f}, glm::vec4(1.0f), 2.0f);
    tests.Check(canvas.GetQuadCount() == 4, "stroke rect draws four edges");
    canvas.Clear();

    // Layer 1 is queued first but must draw after layer 0; within a layer submission order is kept.
    canvas.DrawSprite(&atlasA, {0.0f, 0.0f, 1.0f, 1.0f}, glm::vec4(1.0f), {0.0f, 0.0f, 1.0f, 1.0f}, 1);
    canvas.FillRect({1.0f, 0.0f, 1.0f, 1.0f}, glm::vec4(1.0f), 0);
    canvas.FillRect({2.0f, 0.0f, 1.0f, 1.0f}, glm::vec4(1.0f), 0);
    canvas.DrawSprite(&atlasB, {3.0f, 0.0f, 1.0f, 1.0f}, glm::vec4(1.0f), {0.0f, 0.0f, 1.0f, 1.0f}, 0);
    canvas.BuildBatches(&white, vertices, indices, batches);
    tests.Check(vertices[0].Position.x == 1.0f && vertices[4].Position.x == 2.0f &&
                    vertices[8].Position.x == 3.0f && vertices[12].Position.x == 0.0f,
                "quads are ordered by layer, stable within a layer");
    tests.Check(batches.size() == 3 && batches[0].BatchTexture == &white && batches[0].IndexCount == 12 &&
                    batches[1].BatchTexture == &atlasB && batches[1].FirstIndex == 12 &&
                    batches[2].BatchTexture == &atlasA && batches[2].FirstIndex == 18,
                "neighbouring quads with the same texture merge; texture changes split batches");
    canvas.Clear();

    canvas.DrawSprite(&atlasA, {0.0f, 0.0f, 8.0f, 8.0f}, glm::vec4(0.5f), {0.25f, 0.5f, 0.5f, 0.25f});
    canvas.BuildBatches(&white, vertices, indices, batches);
    tests.Check(vertices[0].TexCoord == glm::vec2(0.25f, 0.5f) && vertices[2].TexCoord == glm::vec2(0.75f, 0.75f) &&
                    vertices[0].Color == glm::vec4(0.5f),
                "sprites carry their UV rect and tint");

    // World-anchored billboards: camera at z = 3 looking at the origin on a 100x100 screen.
    Haki::CanvasProjection projection;
    projection.ViewProjection =
        glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) *
        glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    projection.CameraPosition = {0.0f, 0.0f, 3.0f};
    projection.ScreenSize = {100.0f, 100.0f};
    canvas.Clear();
    std::size_t culled = 99;
    canvas.FillRect(Haki::CanvasAnchor{glm::vec3(0.0f)}, {-5.0f, -10.0f, 10.0f, 4.0f}, glm::vec4(1.0f));
    canvas.BuildBatches(&white, vertices, indices, batches, &projection, &culled);
    tests.Check(culled == 0 && vertices.size() == 4 && vertices[0].Position == glm::vec2(45.0f, 40.0f) &&
                    vertices[2].Position == glm::vec2(55.0f, 44.0f),
                "anchored rect is offset in pixels from the anchor's screen position");
    tests.Check(std::abs(vertices[0].Anchor.x - 0.5f) < 1e-4f && std::abs(vertices[0].Anchor.y - 0.5f) < 1e-4f &&
                    vertices[0].Anchor.z > 0.0f && vertices[0].Anchor.z < 1.0f,
                "anchored quads carry the anchor's viewport UV and depth for occlusion");

    Haki::CanvasProjection offsetCamera = projection;
    canvas.Clear();
    canvas.FillRect(Haki::CanvasAnchor{glm::vec3(0.5f, 0.5f, 0.0f)}, {0.0f, 0.0f, 1.0f, 1.0f}, glm::vec4(1.0f));
    canvas.BuildBatches(&white, vertices, indices, batches, &offsetCamera);
    tests.Check(vertices[0].Position.x > 50.0f && vertices[0].Position.y < 50.0f,
                "anchors up and to the right appear up and to the right on screen (y down)");

    canvas.Clear();
    canvas.FillRect(Haki::CanvasAnchor{glm::vec3(0.0f, 0.0f, 10.0f)}, {0.0f, 0.0f, 4.0f, 4.0f}, glm::vec4(1.0f));
    canvas.FillRect(Haki::CanvasAnchor{glm::vec3(0.0f, 0.0f, -20.0f), true, 10.0f}, {0.0f, 0.0f, 4.0f, 4.0f},
                    glm::vec4(1.0f));
    canvas.FillRect(Haki::CanvasAnchor{glm::vec3(0.0f), false}, {0.0f, 0.0f, 4.0f, 4.0f}, glm::vec4(1.0f));
    canvas.FillRect({0.0f, 0.0f, 4.0f, 4.0f}, glm::vec4(1.0f));
    canvas.BuildBatches(&white, vertices, indices, batches, &projection, &culled);
    tests.Check(culled == 2 && vertices.size() == 8, "anchors behind the camera or past MaxDistance are culled");
    tests.Check(vertices[0].Anchor.z < 0.0f && vertices[4].Anchor.z < 0.0f,
                "screen quads and depth-test-free anchors skip the occlusion test");

    canvas.BuildBatches(&white, vertices, indices, batches, nullptr, &culled);
    tests.Check(culled == 3 && vertices.size() == 4, "world quads are dropped when no camera is supplied");

    canvas.SetSize({1920.0f, 1080.0f});
    tests.Check(canvas.GetSize() == glm::vec2(1920.0f, 1080.0f), "canvas reports the viewport size");
    canvas.Clear();
    tests.Check(canvas.IsEmpty(), "clear removes all quads");
    return tests.Result();
}
