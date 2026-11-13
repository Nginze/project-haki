#pragma once

#include "Haki/AssetManager/AssetTypes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Haki::Detail
{

template <typename Resource>
class ResourceManager
{
public:
    using Handle = AssetHandle<Resource>;

    ResourceManager() = default;
    ~ResourceManager() { Clear(); }

    ResourceManager(const ResourceManager &) = delete;
    ResourceManager &operator=(const ResourceManager &) = delete;
    ResourceManager(ResourceManager &&) = delete;
    ResourceManager &operator=(ResourceManager &&) = delete;

    Handle Store(AssetID id, std::unique_ptr<Resource> resource)
    {
        if (!resource || id == InvalidAssetID)
        {
            return {};
        }

        if (const auto existing = loaded_.find(id); existing != loaded_.end())
        {
            return existing->second;
        }

        std::uint32_t index = 0;
        if (freeSlots_.empty())
        {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        else
        {
            index = freeSlots_.back();
            freeSlots_.pop_back();
        }

        Slot &slot = slots_[index];
        slot.Id = id;
        slot.Generation = NextGeneration();
        slot.Value = std::move(resource);

        const Handle handle{index, slot.Generation};
        loaded_.emplace(id, handle);
        return handle;
    }

    Handle Find(AssetID id) const
    {
        const auto found = loaded_.find(id);
        return found != loaded_.end() ? found->second : Handle{};
    }

    Resource *Get(Handle handle)
    {
        return IsAlive(handle) ? slots_[handle.Index].Value.get() : nullptr;
    }

    const Resource *Get(Handle handle) const
    {
        return IsAlive(handle) ? slots_[handle.Index].Value.get() : nullptr;
    }

    bool Unload(Handle handle)
    {
        if (!IsAlive(handle))
        {
            return false;
        }

        Slot &slot = slots_[handle.Index];
        const auto loaded = loaded_.find(slot.Id);
        if (loaded != loaded_.end() && loaded->second == handle)
        {
            loaded_.erase(loaded);
        }

        slot.Value.reset();
        slot.Id = InvalidAssetID;
        freeSlots_.push_back(handle.Index);
        return true;
    }

    std::optional<AssetID> GetAssetId(Handle handle) const
    {
        if (!IsAlive(handle))
        {
            return std::nullopt;
        }
        return slots_[handle.Index].Id;
    }

    bool IsLoaded(AssetID id) const { return loaded_.contains(id); }
    std::size_t GetLoadedCount() const { return loaded_.size(); }

    void Clear()
    {
        loaded_.clear();
        freeSlots_.clear();
        freeSlots_.reserve(slots_.size());

        for (std::uint32_t index = 0; index < slots_.size(); ++index)
        {
            Slot &slot = slots_[index];
            if (slot.Value)
            {
                slot.Value.reset();
            }
            slot.Id = InvalidAssetID;
            freeSlots_.push_back(index);
        }
    }

private:
    struct Slot
    {
        std::unique_ptr<Resource> Value;
        AssetID Id = InvalidAssetID;
        std::uint32_t Generation = 0;
    };

    bool IsAlive(Handle handle) const
    {
        return handle.IsValid() && handle.Index < slots_.size() &&
               slots_[handle.Index].Generation == handle.Generation && slots_[handle.Index].Value != nullptr;
    }

    static std::uint32_t NextGeneration()
    {
        static std::atomic<std::uint32_t> next{1};
        std::uint32_t generation = next.fetch_add(1, std::memory_order_relaxed);
        if (generation == 0)
        {
            generation = next.fetch_add(1, std::memory_order_relaxed);
        }
        return generation;
    }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeSlots_;
    std::unordered_map<AssetID, Handle> loaded_;
};

} // namespace Haki::Detail
