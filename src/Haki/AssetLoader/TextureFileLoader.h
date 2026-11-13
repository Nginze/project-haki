#pragma once

#include "Haki/AssetLoader/AssetLoader.h"
#include "Haki/GL/Texture.h"

#include <memory>

namespace Haki
{

class AssetCatalog;

class TextureFileLoader final : public AssetLoader<Texture>
{
public:
    explicit TextureFileLoader(bool srgb = true, bool flipVertically = true)
        : srgb_(srgb), flipVertically_(flipVertically)
    {
    }
    explicit TextureFileLoader(std::shared_ptr<const AssetCatalog> catalog) : catalog_(std::move(catalog)) {}

    bool Supports(const AssetLoadRequest &request) const override;
    AssetLoaderResult<Texture> Load(const AssetLoadRequest &request) override;

private:
    bool srgb_ = true;
    bool flipVertically_ = true;
    std::shared_ptr<const AssetCatalog> catalog_;
};

} // namespace Haki
