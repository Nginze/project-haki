#pragma once

#include "Haki/AssetManager/AssetTypes.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Haki
{

class Font;
class Texture;

struct CanvasRect
{
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
};

struct CanvasVertex
{
    glm::vec2 Position{0.0f};
    glm::vec2 TexCoord{0.0f};
    glm::vec4 Color{1.0f};
    // For world-anchored quads: the anchor's viewport UV (xy) and window depth (z), used to hide the
    // whole billboard when scene geometry covers its anchor. z < 0 disables the test.
    glm::vec3 Anchor{0.0f, 0.0f, -1.0f};
};

// A point in the 3D world that 2D canvas shapes are attached to (billboards: health bars, name
// tags, markers). Shapes drawn with an anchor use pixel coordinates relative to the anchor's
// on-screen position, so they keep a constant pixel size and always face the camera.
struct CanvasAnchor
{
    glm::vec3 Position{0.0f};
    // Hide the shapes when scene geometry is in front of the anchor point.
    bool DepthTest = true;
    // Hide the shapes beyond this distance from the camera (0 = no limit).
    float MaxDistance = 0.0f;
};

// Camera data the renderer supplies when resolving world-anchored shapes.
struct CanvasProjection
{
    glm::mat4 ViewProjection{1.0f};
    glm::vec3 CameraPosition{0.0f};
    glm::vec2 ScreenSize{0.0f};
};

struct CanvasBatch
{
    const Texture *BatchTexture = nullptr;
    std::uint32_t FirstIndex = 0;
    std::uint32_t IndexCount = 0;
};

// Immediate-mode 2D drawing in window pixels (origin top-left, y down). Queue rects, sprites and
// text every frame; the renderer draws them over the finished 3D frame (under ImGui) and clears
// the canvas. Higher layers draw on top; within a layer, later calls draw on top.
// Colours are display (sRGB) values with straight alpha.
class Canvas
{
public:
    void FillRect(const CanvasRect &rect, const glm::vec4 &color, int layer = 0);
    void StrokeRect(const CanvasRect &rect, const glm::vec4 &color, float thickness = 1.0f, int layer = 0);
    void DrawSprite(const Texture *texture, const CanvasRect &rect, const glm::vec4 &tint = glm::vec4(1.0f),
                    const CanvasRect &uv = {0.0f, 0.0f, 1.0f, 1.0f}, int layer = 0);
    void DrawSprite(TextureHandle texture, const CanvasRect &rect, const glm::vec4 &tint = glm::vec4(1.0f),
                    const CanvasRect &uv = {0.0f, 0.0f, 1.0f, 1.0f}, int layer = 0);
    // topLeft is the top of the first line; '\n' starts a new line.
    void DrawText(const Font &font, std::string_view text, glm::vec2 topLeft, float pixelSize,
                  const glm::vec4 &color, int layer = 0);

    // World-anchored versions: rect / topLeft are pixel offsets from the anchor's screen position
    // (e.g. {-50, -40, 100, 8} is a 100px bar centred 40px above the anchor). Resolved with the
    // camera of the frame they are drawn in; anchors behind the camera are skipped.
    void FillRect(const CanvasAnchor &anchor, const CanvasRect &rect, const glm::vec4 &color, int layer = 0);
    void StrokeRect(const CanvasAnchor &anchor, const CanvasRect &rect, const glm::vec4 &color,
                    float thickness = 1.0f, int layer = 0);
    void DrawSprite(const CanvasAnchor &anchor, const Texture *texture, const CanvasRect &rect,
                    const glm::vec4 &tint = glm::vec4(1.0f), const CanvasRect &uv = {0.0f, 0.0f, 1.0f, 1.0f},
                    int layer = 0);
    void DrawText(const CanvasAnchor &anchor, const Font &font, std::string_view text, glm::vec2 topLeft,
                  float pixelSize, const glm::vec4 &color, int layer = 0);
    // Convenience: text horizontally centred on the anchor with its bottom offsetY pixels above it.
    void DrawLabel(const CanvasAnchor &anchor, const Font &font, std::string_view text, float pixelSize,
                   const glm::vec4 &color, float offsetY = 0.0f, int layer = 0);

    // Projects a world position to window pixels. Returns false if it is behind the camera.
    static bool WorldToScreen(const CanvasProjection &projection, const glm::vec3 &world, glm::vec2 &screen,
                              float &depth);

    // Size of the area the canvas covers (the window viewport), updated by the renderer each frame.
    glm::vec2 GetSize() const { return size_; }
    void SetSize(glm::vec2 size) { size_ = size; }

    void Clear();
    bool IsEmpty() const { return quads_.empty(); }
    std::size_t GetQuadCount() const { return quads_.size(); }

    // Orders quads by layer (stable within a layer) and merges neighbours that share a texture into
    // one indexed batch. Untextured quads use whiteTexture.
    // World-anchored quads are projected with `projection` (skipped when it is null) and the
    // number dropped as off-screen, behind the camera or too far is returned via `culledWorldQuads`.
    void BuildBatches(const Texture *whiteTexture, std::vector<CanvasVertex> &vertices,
                      std::vector<std::uint32_t> &indices, std::vector<CanvasBatch> &batches,
                      const CanvasProjection *projection = nullptr, std::size_t *culledWorldQuads = nullptr) const;

    static constexpr std::size_t MaxQuads = 1u << 18;

private:
    struct Quad
    {
        std::array<CanvasVertex, 4> Vertices; // top-left, top-right, bottom-right, bottom-left
        const Texture *QuadTexture = nullptr;
        int Layer = 0;
        bool WorldAnchored = false;
        CanvasAnchor Anchor;
    };

    void AddQuad(glm::vec2 min, glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4 &color,
                 const Texture *texture, int layer, const CanvasAnchor *anchor = nullptr);
    void AddText(const Font &font, std::string_view text, glm::vec2 topLeft, float pixelSize,
                 const glm::vec4 &color, int layer, const CanvasAnchor *anchor);
    void AddStroke(const CanvasRect &rect, const glm::vec4 &color, float thickness, int layer,
                   const CanvasAnchor *anchor);

    std::vector<Quad> quads_;
    glm::vec2 size_{0.0f};
};

} // namespace Haki
