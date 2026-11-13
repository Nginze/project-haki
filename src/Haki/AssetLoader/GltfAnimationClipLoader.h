#pragma once

#include "Haki/AssetLoader/AssetLoader.h"
#include "Haki/AssetLoader/GltfDocumentCache.h"
#include "Haki/AssetManager/AnimationClip.h"

#include <memory>

namespace Haki
{

class AssetCatalog;

class GltfAnimationClipLoader final : public AssetLoader<AnimationClip>
{
public:
    GltfAnimationClipLoader(std::shared_ptr<const AssetCatalog> catalog,
                            std::shared_ptr<GltfDocumentCache> cache);

    bool Supports(const AssetLoadRequest &request) const override;
    AssetLoaderResult<AnimationClip> Load(const AssetLoadRequest &request) override;

private:
    std::shared_ptr<const AssetCatalog> catalog_;
    std::shared_ptr<GltfDocumentCache> cache_;
};

} // namespace Haki
