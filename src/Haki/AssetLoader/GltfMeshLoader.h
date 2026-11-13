#pragma once

#include "Haki/AssetLoader/AssetLoader.h"
#include "Haki/AssetLoader/GltfDocumentCache.h"
#include "Haki/GL/Mesh.h"

#include <cstddef>
#include <memory>

namespace Haki
{

class AssetCatalog;

class GltfMeshLoader final : public AssetLoader<Mesh>
{
public:
    explicit GltfMeshLoader(std::shared_ptr<const AssetCatalog> catalog);
    GltfMeshLoader(std::shared_ptr<const AssetCatalog> catalog, std::shared_ptr<GltfDocumentCache> cache);

    bool Supports(const AssetLoadRequest &request) const override;
    AssetLoaderResult<Mesh> Load(const AssetLoadRequest &request) override;
    std::size_t GetParseCount() const { return cache_->GetParseCount(); }

private:
    std::shared_ptr<const AssetCatalog> catalog_;
    std::shared_ptr<GltfDocumentCache> cache_;
};

} // namespace Haki
