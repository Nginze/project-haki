#pragma once

#include "Haki/AssetLoader/AssetLoader.h"
#include "Haki/Renderer/Material.h"

#include <memory>

namespace Haki
{

class AssetCatalog;

class MaterialFileLoader final : public AssetLoader<Material>
{
public:
    explicit MaterialFileLoader(std::shared_ptr<const AssetCatalog> catalog) : catalog_(std::move(catalog)) {}

    bool Supports(const AssetLoadRequest &request) const override;
    AssetLoaderResult<Material> Load(const AssetLoadRequest &request) override;

private:
    std::shared_ptr<const AssetCatalog> catalog_;
};

} // namespace Haki
