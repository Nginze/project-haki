#pragma once

#include "Haki/AssetLoader/AssetLoader.h"
#include "Haki/GL/Shader.h"

namespace Haki
{

class ShaderFileLoader final : public AssetLoader<Shader>
{
public:
    bool Supports(const AssetLoadRequest &request) const override;
    AssetLoaderResult<Shader> Load(const AssetLoadRequest &request) override;
    std::vector<std::filesystem::path> GetSourceFiles(const AssetLoadRequest &request) const override;
};

} // namespace Haki
