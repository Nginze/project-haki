#include "Haki/Renderer/Canvas.h"

#include "Haki/AssetManager/AssetManager.h"
#include "Haki/Renderer/Font.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace Haki
{

void Canvas::AddQuad(glm::vec2 min, glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4 &color,
                     const Texture *texture, int layer, const CanvasAnchor *anchor)
{
    if (!(max.x > min.x) || !(max.y > min.y) || quads_.size() >= MaxQuads)
    {
        return;
    }
    Quad quad;
    quad.Vertices = {{
        {{min.x, min.y}, {uvMin.x, uvMin.y}, color},
        {{max.x, min.y}, {uvMax.x, uvMin.y}, color},
        {{max.x, max.y}, {uvMax.x, uvMax.y}, color},
        {{min.x, max.y}, {uvMin.x, uvMax.y}, color},
    }};
    quad.QuadTexture = texture;
    quad.Layer = layer;
    if (anchor)
    {
        quad.WorldAnchored = true;
        quad.Anchor = *anchor;
    }
    quads_.push_back(quad);
}

void Canvas::AddStroke(const CanvasRect &rect, const glm::vec4 &color, float thickness, int layer,
                       const CanvasAnchor *anchor)
{
    const float t = std::min({std::max(thickness, 0.0f), rect.Width * 0.5f, rect.Height * 0.5f});
    if (!(t > 0.0f))
    {
        return;
    }
    const auto edge = [&](float x, float y, float width, float height)
    {
        AddQuad({x, y}, {x + width, y + height}, glm::vec2(0.0f), glm::vec2(1.0f), color, nullptr, layer, anchor);
    };
    edge(rect.X, rect.Y, rect.Width, t);                                  // top
    edge(rect.X, rect.Y + rect.Height - t, rect.Width, t);                // bottom
    edge(rect.X, rect.Y + t, t, rect.Height - t * 2.0f);                  // left
    edge(rect.X + rect.Width - t, rect.Y + t, t, rect.Height - t * 2.0f); // right
}

void Canvas::AddText(const Font &font, std::string_view text, glm::vec2 topLeft, float pixelSize,
                     const glm::vec4 &color, int layer, const CanvasAnchor *anchor)
{
    if (!font.IsValid())
    {
        return;
    }
    thread_local std::vector<GlyphQuad> glyphs;
    glyphs.clear();
    font.GetGlyphQuads(text, topLeft, pixelSize, glyphs);
    for (const GlyphQuad &glyph : glyphs)
    {
        AddQuad(glyph.Min, glyph.Max, glyph.UvMin, glyph.UvMax, color, &font.GetAtlas(), layer, anchor);
    }
}

void Canvas::FillRect(const CanvasRect &rect, const glm::vec4 &color, int layer)
{
    AddQuad({rect.X, rect.Y}, {rect.X + rect.Width, rect.Y + rect.Height}, glm::vec2(0.0f), glm::vec2(1.0f), color,
            nullptr, layer);
}

void Canvas::StrokeRect(const CanvasRect &rect, const glm::vec4 &color, float thickness, int layer)
{
    AddStroke(rect, color, thickness, layer, nullptr);
}

void Canvas::DrawSprite(const Texture *texture, const CanvasRect &rect, const glm::vec4 &tint, const CanvasRect &uv,
                        int layer)
{
    AddQuad({rect.X, rect.Y}, {rect.X + rect.Width, rect.Y + rect.Height}, {uv.X, uv.Y},
            {uv.X + uv.Width, uv.Y + uv.Height}, tint, texture, layer);
}

void Canvas::DrawSprite(TextureHandle texture, const CanvasRect &rect, const glm::vec4 &tint, const CanvasRect &uv,
                        int layer)
{
    if (const Texture *resolved = AssetManager::GetTexture(texture))
    {
        DrawSprite(resolved, rect, tint, uv, layer);
    }
}

void Canvas::DrawText(const Font &font, std::string_view text, glm::vec2 topLeft, float pixelSize,
                      const glm::vec4 &color, int layer)
{
    AddText(font, text, topLeft, pixelSize, color, layer, nullptr);
}

void Canvas::FillRect(const CanvasAnchor &anchor, const CanvasRect &rect, const glm::vec4 &color, int layer)
{
    AddQuad({rect.X, rect.Y}, {rect.X + rect.Width, rect.Y + rect.Height}, glm::vec2(0.0f), glm::vec2(1.0f), color,
            nullptr, layer, &anchor);
}

void Canvas::StrokeRect(const CanvasAnchor &anchor, const CanvasRect &rect, const glm::vec4 &color, float thickness,
                        int layer)
{
    AddStroke(rect, color, thickness, layer, &anchor);
}

void Canvas::DrawSprite(const CanvasAnchor &anchor, const Texture *texture, const CanvasRect &rect,
                        const glm::vec4 &tint, const CanvasRect &uv, int layer)
{
    AddQuad({rect.X, rect.Y}, {rect.X + rect.Width, rect.Y + rect.Height}, {uv.X, uv.Y},
            {uv.X + uv.Width, uv.Y + uv.Height}, tint, texture, layer, &anchor);
}

void Canvas::DrawText(const CanvasAnchor &anchor, const Font &font, std::string_view text, glm::vec2 topLeft,
                      float pixelSize, const glm::vec4 &color, int layer)
{
    AddText(font, text, topLeft, pixelSize, color, layer, &anchor);
}

void Canvas::DrawLabel(const CanvasAnchor &anchor, const Font &font, std::string_view text, float pixelSize,
                       const glm::vec4 &color, float offsetY, int layer)
{
    const glm::vec2 size = font.MeasureText(text, pixelSize);
    AddText(font, text, {-size.x * 0.5f, -size.y - offsetY}, pixelSize, color, layer, &anchor);
}

bool Canvas::WorldToScreen(const CanvasProjection &projection, const glm::vec3 &world, glm::vec2 &screen,
                           float &depth)
{
    const glm::vec4 clip = projection.ViewProjection * glm::vec4(world, 1.0f);
    if (!(clip.w > 1e-6f))
    {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || ndc.z < -1.0f || ndc.z > 1.0f)
    {
        return false;
    }
    screen = {(ndc.x * 0.5f + 0.5f) * projection.ScreenSize.x, (0.5f - ndc.y * 0.5f) * projection.ScreenSize.y};
    depth = ndc.z * 0.5f + 0.5f;
    return true;
}

void Canvas::Clear()
{
    quads_.clear();
}

void Canvas::BuildBatches(const Texture *whiteTexture, std::vector<CanvasVertex> &vertices,
                          std::vector<std::uint32_t> &indices, std::vector<CanvasBatch> &batches,
                          const CanvasProjection *projection, std::size_t *culledWorldQuads) const
{
    vertices.clear();
    indices.clear();
    batches.clear();
    std::size_t culled = 0;

    std::vector<std::size_t> order(quads_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [this](std::size_t left, std::size_t right) { return quads_[left].Layer < quads_[right].Layer; });

    vertices.reserve(quads_.size() * 4);
    indices.reserve(quads_.size() * 6);
    for (const std::size_t quadIndex : order)
    {
        const Quad &quad = quads_[quadIndex];
        std::array<CanvasVertex, 4> corners = quad.Vertices;
        if (quad.WorldAnchored)
        {
            glm::vec2 screen(0.0f);
            float depth = 0.0f;
            const float maxDistance = quad.Anchor.MaxDistance;
            if (!projection || !WorldToScreen(*projection, quad.Anchor.Position, screen, depth) ||
                (maxDistance > 0.0f && glm::distance(projection->CameraPosition, quad.Anchor.Position) > maxDistance))
            {
                ++culled;
                continue;
            }
            // Snap the anchor to whole pixels so text stays crisp while the camera moves.
            screen = glm::round(screen);
            const glm::vec2 uv = screen / glm::max(projection->ScreenSize, glm::vec2(1.0f));
            const glm::vec3 anchor(uv.x, 1.0f - uv.y, quad.Anchor.DepthTest ? depth : -1.0f);
            for (CanvasVertex &vertex : corners)
            {
                vertex.Position += screen;
                vertex.Anchor = anchor;
            }
        }

        const Texture *texture = quad.QuadTexture ? quad.QuadTexture : whiteTexture;
        const auto base = static_cast<std::uint32_t>(vertices.size());
        vertices.insert(vertices.end(), corners.begin(), corners.end());
        const auto firstIndex = static_cast<std::uint32_t>(indices.size());
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        if (!batches.empty() && batches.back().BatchTexture == texture)
        {
            batches.back().IndexCount += 6;
        }
        else
        {
            batches.push_back({texture, firstIndex, 6});
        }
    }
    if (culledWorldQuads)
    {
        *culledWorldQuads = culled;
    }
}

} // namespace Haki
