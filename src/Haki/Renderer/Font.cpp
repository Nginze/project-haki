#include "Haki/Renderer/Font.h"

#include <stb/stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace Haki
{
namespace
{

constexpr std::size_t GlyphCount = Font::LastCodepoint - Font::FirstCodepoint + 1;

void SetError(std::string *error, std::string message)
{
    if (error)
    {
        *error = std::move(message);
    }
}

// Decodes one UTF-8 code point and advances index; malformed bytes decode as '?'.
std::uint32_t NextCodepoint(std::string_view text, std::size_t &index)
{
    const auto byte = [&](std::size_t offset) { return static_cast<unsigned char>(text[index + offset]); };
    const unsigned char lead = byte(0);
    std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || index + length > text.size())
    {
        ++index;
        return '?';
    }
    std::uint32_t codepoint = length == 1 ? lead : lead & (0x7F >> length);
    for (std::size_t offset = 1; offset < length; ++offset)
    {
        if ((byte(offset) & 0xC0) != 0x80)
        {
            ++index;
            return '?';
        }
        codepoint = (codepoint << 6) | (byte(offset) & 0x3F);
    }
    index += length;
    return codepoint;
}

} // namespace

struct Font::Data
{
    std::vector<unsigned char> FontBytes;
    stbtt_fontinfo Info{};
    std::array<stbtt_packedchar, GlyphCount> Glyphs{};
    Texture Atlas;
    float BakePixelHeight = 48.0f;
    float BakeScale = 1.0f; // stbtt scale for the bake height (font units -> bake pixels)
    float Ascent = 0.0f;    // in bake pixels
    float Descent = 0.0f;
    float LineGap = 0.0f;

    std::size_t GlyphIndex(std::uint32_t codepoint) const
    {
        if (codepoint < FirstCodepoint || codepoint > LastCodepoint)
        {
            codepoint = '?';
        }
        return codepoint - FirstCodepoint;
    }

    float Kerning(std::uint32_t previous, std::uint32_t current) const
    {
        return static_cast<float>(stbtt_GetCodepointKernAdvance(&Info, static_cast<int>(previous),
                                                                static_cast<int>(current))) *
               BakeScale;
    }
};

Font::Font() = default;
Font::~Font() = default;
Font::Font(Font &&) noexcept = default;
Font &Font::operator=(Font &&) noexcept = default;

bool Font::LoadFromFile(const std::filesystem::path &path, float bakePixelHeight, std::string *error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        SetError(error, "Could not open font file: " + path.string());
        return false;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return LoadFromMemory(std::as_bytes(std::span(bytes)), bakePixelHeight, error);
}

bool Font::LoadFromMemory(std::span<const std::byte> bytes, float bakePixelHeight, std::string *error)
{
    Reset();
    if (bytes.empty())
    {
        SetError(error, "Font data is empty");
        return false;
    }

    auto data = std::make_unique<Data>();
    data->FontBytes.resize(bytes.size());
    std::memcpy(data->FontBytes.data(), bytes.data(), bytes.size());
    data->BakePixelHeight = std::clamp(std::isfinite(bakePixelHeight) ? bakePixelHeight : 48.0f, 8.0f, 128.0f);

    const int offset = stbtt_GetFontOffsetForIndex(data->FontBytes.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&data->Info, data->FontBytes.data(), offset))
    {
        SetError(error, "Font data is not a valid TrueType font");
        return false;
    }

    std::vector<unsigned char> coverage(static_cast<std::size_t>(AtlasSize) * AtlasSize);
    stbtt_pack_context pack;
    if (!stbtt_PackBegin(&pack, coverage.data(), AtlasSize, AtlasSize, 0, 1, nullptr))
    {
        SetError(error, "Could not start font atlas packing");
        return false;
    }
    stbtt_PackSetOversampling(&pack, 2, 2);
    const bool packed = stbtt_PackFontRange(&pack, data->FontBytes.data(), 0, data->BakePixelHeight,
                                            FirstCodepoint, static_cast<int>(GlyphCount), data->Glyphs.data()) != 0;
    stbtt_PackEnd(&pack);
    if (!packed)
    {
        SetError(error, "Font glyphs do not fit in the atlas; use a smaller bake size");
        return false;
    }

    // White RGB with coverage in alpha lets text share the sprite shader (vertex colour * texture).
    std::vector<std::byte> rgba(coverage.size() * 4);
    for (std::size_t index = 0; index < coverage.size(); ++index)
    {
        rgba[index * 4 + 0] = std::byte{0xff};
        rgba[index * 4 + 1] = std::byte{0xff};
        rgba[index * 4 + 2] = std::byte{0xff};
        rgba[index * 4 + 3] = static_cast<std::byte>(coverage[index]);
    }
    TextureDescriptor descriptor;
    descriptor.Width = AtlasSize;
    descriptor.Height = AtlasSize;
    descriptor.Format = TextureFormat::RGBA8;
    descriptor.MinFilter = TextureFilter::LinearMipmapLinear;
    descriptor.MagFilter = TextureFilter::Linear;
    descriptor.WrapU = TextureWrap::ClampToEdge;
    descriptor.WrapV = TextureWrap::ClampToEdge;
    descriptor.GenerateMipmaps = true;
    if (!data->Atlas.Initialize(descriptor, rgba))
    {
        SetError(error, "Could not create the font atlas texture");
        return false;
    }

    data->BakeScale = stbtt_ScaleForPixelHeight(&data->Info, data->BakePixelHeight);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&data->Info, &ascent, &descent, &lineGap);
    data->Ascent = static_cast<float>(ascent) * data->BakeScale;
    data->Descent = static_cast<float>(descent) * data->BakeScale;
    data->LineGap = static_cast<float>(lineGap) * data->BakeScale;
    data_ = std::move(data);
    return true;
}

void Font::Reset()
{
    data_.reset();
}

bool Font::IsValid() const
{
    return data_ && data_->Atlas.IsValid();
}

const Texture &Font::GetAtlas() const
{
    static const Texture empty;
    return data_ ? data_->Atlas : empty;
}

float Font::GetLineHeight(float pixelSize) const
{
    if (!data_)
    {
        return 0.0f;
    }
    return (data_->Ascent - data_->Descent + data_->LineGap) * pixelSize / data_->BakePixelHeight;
}

float Font::GetAscent(float pixelSize) const
{
    return data_ ? data_->Ascent * pixelSize / data_->BakePixelHeight : 0.0f;
}

glm::vec2 Font::MeasureText(std::string_view text, float pixelSize) const
{
    if (!data_ || text.empty())
    {
        return glm::vec2(0.0f);
    }
    const float scale = pixelSize / data_->BakePixelHeight;
    float widest = 0.0f;
    float lineWidth = 0.0f;
    int lines = 1;
    std::uint32_t previous = 0;
    for (std::size_t index = 0; index < text.size();)
    {
        const std::uint32_t codepoint = NextCodepoint(text, index);
        if (codepoint == '\n')
        {
            widest = std::max(widest, lineWidth);
            lineWidth = 0.0f;
            previous = 0;
            ++lines;
            continue;
        }
        const stbtt_packedchar &glyph = data_->Glyphs[data_->GlyphIndex(codepoint)];
        if (previous != 0)
        {
            lineWidth += data_->Kerning(previous, codepoint) * scale;
        }
        lineWidth += glyph.xadvance * scale;
        previous = codepoint;
    }
    widest = std::max(widest, lineWidth);
    return {widest, static_cast<float>(lines) * GetLineHeight(pixelSize)};
}

void Font::GetGlyphQuads(std::string_view text, glm::vec2 topLeft, float pixelSize,
                         std::vector<GlyphQuad> &quads) const
{
    if (!data_ || text.empty() || !(pixelSize > 0.0f))
    {
        return;
    }
    const float scale = pixelSize / data_->BakePixelHeight;
    const float lineHeight = GetLineHeight(pixelSize);
    // Baseline of the first line, snapped so glyph edges land on whole pixels.
    float penX = std::round(topLeft.x);
    float baseline = std::round(topLeft.y + GetAscent(pixelSize));
    std::uint32_t previous = 0;
    for (std::size_t index = 0; index < text.size();)
    {
        const std::uint32_t codepoint = NextCodepoint(text, index);
        if (codepoint == '\n')
        {
            penX = std::round(topLeft.x);
            baseline = std::round(baseline + lineHeight);
            previous = 0;
            continue;
        }
        const stbtt_packedchar &glyph = data_->Glyphs[data_->GlyphIndex(codepoint)];
        if (previous != 0)
        {
            penX += data_->Kerning(previous, codepoint) * scale;
        }
        previous = codepoint;
        if (glyph.x1 > glyph.x0 && glyph.y1 > glyph.y0)
        {
            const float invAtlas = 1.0f / static_cast<float>(AtlasSize);
            GlyphQuad quad;
            quad.Min = {penX + glyph.xoff * scale, baseline + glyph.yoff * scale};
            quad.Max = {penX + glyph.xoff2 * scale, baseline + glyph.yoff2 * scale};
            quad.UvMin = {glyph.x0 * invAtlas, glyph.y0 * invAtlas};
            quad.UvMax = {glyph.x1 * invAtlas, glyph.y1 * invAtlas};
            quads.push_back(quad);
        }
        penX += glyph.xadvance * scale;
    }
}

} // namespace Haki
