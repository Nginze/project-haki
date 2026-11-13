#pragma once

#include "Haki/AssetLoader/AssetLoadRequest.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Haki
{

template <typename T>
struct AssetLoaderResult
{
    std::unique_ptr<T> Resource;
    AssetLoadError Error = AssetLoadError::None;
    std::string Message;

    explicit operator bool() const { return Error == AssetLoadError::None && Resource != nullptr; }
};

template <typename T>
class AssetLoader
{
public:
    virtual ~AssetLoader() = default;

    virtual bool Supports(const AssetLoadRequest &request) const = 0;
    virtual AssetLoaderResult<T> Load(const AssetLoadRequest &request) = 0;

    // Files the loaded asset is built from, used to detect on-disk changes for hot reload.
    virtual std::vector<std::filesystem::path> GetSourceFiles(const AssetLoadRequest &) const { return {}; }
};

} // namespace Haki
