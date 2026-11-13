#pragma once

#include "Haki/AssetLoader/AssetLoader.h"

#include <memory>
#include <vector>

namespace Haki::Detail
{

template <typename T>
class AssetLoaderRegistry
{
public:
    bool Register(std::unique_ptr<AssetLoader<T>> loader)
    {
        if (!loader)
        {
            return false;
        }
        loaders_.push_back(std::move(loader));
        return true;
    }

    AssetLoader<T> *Find(const AssetLoadRequest &request) const
    {
        for (const auto &loader : loaders_)
        {
            if (loader->Supports(request))
            {
                return loader.get();
            }
        }
        return nullptr;
    }

    void Clear() { loaders_.clear(); }
    std::size_t GetCount() const { return loaders_.size(); }

private:
    std::vector<std::unique_ptr<AssetLoader<T>>> loaders_;
};

} // namespace Haki::Detail
