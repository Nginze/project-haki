#pragma once

#include "Haki/GL/Texture.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Haki
{

struct GlyphQuad
{
    glm::vec2 Min{0.0f};   // top-left in pixels
    glm::vec2 Max{0.0f};   // bottom-right in pixels
    glm::vec2 UvMin{0.0f};
    glm::vec2 UvMax{0.0f};
};

// A TrueType font baked into a texture atlas (printable ASCII). Text can be drawn at any pixel size;
// glyphs are scaled from the bake size, so bake near the largest size you need for the crispest text.
// Creating a font requires a current OpenGL context.
class Font
{
public:
    static constexpr std::uint32_t FirstCodepoint = 32;
    static constexpr std::uint32_t LastCodepoint = 126;
    static constexpr std::uint32_t AtlasSize = 1024;

    Font();
    ~Font();

    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;
    Font(Font &&) noexcept;
    Font &operator=(Font &&) noexcept;

    bool LoadFromFile(const std::filesystem::path &path, float bakePixelHeight = 48.0f,
                      std::string *error = nullptr);
    bool LoadFromMemory(std::span<const std::byte> data, float bakePixelHeight = 48.0f,
                        std::string *error = nullptr);
    void Reset();

    bool IsValid() const;
    const Texture &GetAtlas() const;

    float GetLineHeight(float pixelSize) const;
    float GetAscent(float pixelSize) const;
    // Width of the widest line and total height (line count * line height).
    glm::vec2 MeasureText(std::string_view text, float pixelSize) const;
    // Appends one quad per visible glyph; topLeft is the top of the first line.
    void GetGlyphQuads(std::string_view text, glm::vec2 topLeft, float pixelSize,
                       std::vector<GlyphQuad> &quads) const;

private:
    struct Data;
    std::unique_ptr<Data> data_;
};

} // namespace Haki
