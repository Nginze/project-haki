#pragma once

#include "Haki/AssetManager/AssetTypes.h"

#include <filesystem>

namespace Haki
{

struct AssetLoadRequest
{
    AssetID Id = InvalidAssetID;
    std::filesystem::path Source;
};

} // namespace Haki
