#include "Haki/AssetLoader/TextureFileLoader.h"

#include "Haki/AssetManager/AssetCatalog.h"

#include <stb/stb_image.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <span>
#include <string>

namespace Haki
{

bool TextureFileLoader::Supports(const AssetLoadRequest &request) const
{
    std::string extension = request.Source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga" || extension == ".hdr";
}

AssetLoaderResult<Texture> TextureFileLoader::Load(const AssetLoadRequest &request)
{
    bool srgb = srgb_;
    bool flipVertically = flipVertically_;
    if (catalog_)
    {
        const AssetCatalogEntry *entry = catalog_->Find(request.Id);
        if (!entry || entry->Type != CatalogAssetType::Texture)
        {
            return {nullptr, AssetLoadError::LoaderFailed, "Texture is missing from the asset catalog"};
        }
        srgb = entry->SRGB;
        flipVertically = entry->FlipY;
    }

    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    const bool hdr = stbi_is_hdr(request.Source.string().c_str()) != 0;
    if (hdr)
    {
        float *pixels = stbi_loadf(request.Source.string().c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
        if (!pixels || width <= 0 || height <= 0)
        {
            const char *reason = stbi_failure_reason();
            stbi_image_free(pixels);
            return {nullptr, AssetLoadError::LoaderFailed,
                    "Could not load HDR texture '" + request.Source.string() + "': " +
                        (reason ? reason : "unknown error")};
        }

        TextureDescriptor descriptor;
        descriptor.Width = static_cast<std::uint32_t>(width);
        descriptor.Height = static_cast<std::uint32_t>(height);
        descriptor.Format = TextureFormat::RGBA16F;
        const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 * sizeof(float);
        auto texture = std::make_unique<Texture>();
        const bool initialized = texture->Initialize(
            descriptor, std::span<const std::byte>(reinterpret_cast<const std::byte *>(pixels), byteCount));
        stbi_image_free(pixels);
        if (!initialized)
        {
            return {nullptr, AssetLoadError::LoaderFailed,
                    "Could not create OpenGL HDR texture for: " + request.Source.string()};
        }
        return {std::move(texture), AssetLoadError::None, {}};
    }

    stbi_uc *pixels = stbi_load(request.Source.string().c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
    if (!pixels || width <= 0 || height <= 0)
    {
        const char *reason = stbi_failure_reason();
        return {nullptr, AssetLoadError::LoaderFailed,
                "Could not load texture '" + request.Source.string() + "': " + (reason ? reason : "unknown error")};
    }

    TextureDescriptor descriptor;
    descriptor.Width = static_cast<std::uint32_t>(width);
    descriptor.Height = static_cast<std::uint32_t>(height);
    descriptor.Format = srgb ? TextureFormat::SRGB8Alpha8 : TextureFormat::RGBA8;
    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;

    auto texture = std::make_unique<Texture>();
    const bool initialized = texture->Initialize(
        descriptor, std::span<const std::byte>(reinterpret_cast<const std::byte *>(pixels), byteCount));
    stbi_image_free(pixels);
    if (!initialized)
    {
        return {nullptr, AssetLoadError::LoaderFailed,
                "Could not create OpenGL texture for: " + request.Source.string()};
    }
    return {std::move(texture), AssetLoadError::None, {}};
}

} // namespace Haki
