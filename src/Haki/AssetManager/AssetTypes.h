#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace Haki
{

class Texture;
class Shader;
class Mesh;
class Material;
class Skeleton;
class AnimationClip;

using AssetID = std::uint32_t;
inline constexpr AssetID InvalidAssetID = std::numeric_limits<AssetID>::max();

template <typename T>
struct AssetHandle
{
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t Index = InvalidIndex;
    std::uint32_t Generation = 0;

    bool IsValid() const { return Index != InvalidIndex; }
    bool operator==(const AssetHandle &) const = default;
};

using TextureHandle = AssetHandle<Texture>;
using ShaderHandle = AssetHandle<Shader>;
using MeshHandle = AssetHandle<Mesh>;
using MaterialHandle = AssetHandle<Material>;
using SkeletonHandle = AssetHandle<Skeleton>;
using AnimationClipHandle = AssetHandle<AnimationClip>;

enum class AssetLoadError
{
    None,
    NotInitialized,
    InvalidRequest,
    NoCompatibleLoader,
    LoaderFailed,
    AssetIdConflict
};

template <typename Handle>
struct AssetResult
{
    Handle Value;
    AssetLoadError Error = AssetLoadError::None;
    std::string Message;

    explicit operator bool() const { return Error == AssetLoadError::None && Value.IsValid(); }
};

} // namespace Haki
