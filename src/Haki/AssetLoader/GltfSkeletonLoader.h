#pragma once

#include "Haki/AssetLoader/AssetLoader.h"
#include "Haki/AssetLoader/GltfDocumentCache.h"
#include "Haki/AssetManager/Skeleton.h"

#include <memory>

namespace Haki
{

class AssetCatalog;

class GltfSkeletonLoader final : public AssetLoader<Skeleton>
{
public:
    GltfSkeletonLoader(std::shared_ptr<const AssetCatalog> catalog, std::shared_ptr<GltfDocumentCache> cache);

    bool Supports(const AssetLoadRequest &request) const override;
    AssetLoaderResult<Skeleton> Load(const AssetLoadRequest &request) override;

private:
    std::shared_ptr<const AssetCatalog> catalog_;
    std::shared_ptr<GltfDocumentCache> cache_;
};

} // namespace Haki
