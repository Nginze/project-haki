#include <cgltf.h>

#include "Haki/AssetLoader/GltfAnimationClipLoader.h"

#include "Haki/AssetManager/AssetCatalog.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <glm/geometric.hpp>

namespace Haki
{
namespace
{

template <typename T>
const T *Select(const T *values, cgltf_size count, std::string_view selector)
{
    if (selector.starts_with('#'))
    {
        std::size_t index = 0;
        const auto parsed = std::from_chars(selector.data() + 1, selector.data() + selector.size(), index);
        return parsed.ec == std::errc{} && parsed.ptr == selector.data() + selector.size() && index < count
                   ? &values[index]
                   : nullptr;
    }
    for (cgltf_size index = 0; index < count; ++index)
    {
        if (values[index].name && selector == values[index].name)
        {
            return &values[index];
        }
    }
    return nullptr;
}

bool ReadTimes(const cgltf_accessor &accessor, std::vector<float> &times)
{
    times.resize(accessor.count);
    for (cgltf_size index = 0; index < accessor.count; ++index)
    {
        if (!cgltf_accessor_read_float(&accessor, index, &times[index], 1) || !std::isfinite(times[index]) ||
            (index > 0 && times[index] < times[index - 1]))
        {
            return false;
        }
    }
    return true;
}

AnimationInterpolation ToInterpolation(cgltf_interpolation_type interpolation)
{
    return interpolation == cgltf_interpolation_type_step ? AnimationInterpolation::Step
                                                           : AnimationInterpolation::Linear;
}

} // namespace

GltfAnimationClipLoader::GltfAnimationClipLoader(std::shared_ptr<const AssetCatalog> catalog,
                                                 std::shared_ptr<GltfDocumentCache> cache)
    : catalog_(std::move(catalog)), cache_(cache ? std::move(cache) : std::make_shared<GltfDocumentCache>())
{
}

bool GltfAnimationClipLoader::Supports(const AssetLoadRequest &request) const
{
    std::string extension = request.Source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".gltf" || extension == ".glb";
}

AssetLoaderResult<AnimationClip> GltfAnimationClipLoader::Load(const AssetLoadRequest &request)
{
    const AssetCatalogEntry *entry = catalog_ ? catalog_->Find(request.Id) : nullptr;
    const AssetCatalogEntry *skeletonEntry = entry ? catalog_->Find(entry->SkeletonRuntimeId) : nullptr;
    if (!entry || entry->Type != CatalogAssetType::AnimationClip || entry->AnimationSelector.empty() ||
        !skeletonEntry || skeletonEntry->Type != CatalogAssetType::Skeleton)
    {
        return {nullptr, AssetLoadError::LoaderFailed,
                "glTF animation clip catalog entry requires animation and skeleton selectors"};
    }

    std::string error;
    const auto animationDocument = cache_->Load(request.Source, error);
    if (!animationDocument)
    {
        return {nullptr, AssetLoadError::LoaderFailed, std::move(error)};
    }
    const auto skeletonDocument = cache_->Load(skeletonEntry->Source, error);
    if (!skeletonDocument)
    {
        return {nullptr, AssetLoadError::LoaderFailed, std::move(error)};
    }
    const cgltf_data &animationData = *animationDocument->GetData();
    const cgltf_data &skeletonData = *skeletonDocument->GetData();
    const cgltf_animation *animation = Select(animationData.animations, animationData.animations_count,
                                               entry->AnimationSelector);
    const cgltf_skin *skin = Select(skeletonData.skins, skeletonData.skins_count, skeletonEntry->SkinSelector);
    if (!animation || !skin || skin->joints_count == 0)
    {
        return {nullptr, AssetLoadError::LoaderFailed, "Selected glTF animation or skeleton skin was not found"};
    }

    std::unordered_set<const cgltf_node *> included;
    for (cgltf_size index = 0; index < skin->joints_count; ++index)
    {
        for (const cgltf_node *node = skin->joints[index]; node; node = node->parent)
        {
            included.insert(node);
        }
    }
    std::vector<const cgltf_node *> poseNodes;
    std::unordered_map<const cgltf_node *, std::uint32_t> nodeIndices;
    std::unordered_map<std::string, std::uint32_t> names;
    std::function<void(const cgltf_node *)> addNode = [&](const cgltf_node *node) {
        if (nodeIndices.contains(node))
        {
            return;
        }
        if (node->parent && included.contains(node->parent))
        {
            addNode(node->parent);
        }
        const std::uint32_t index = static_cast<std::uint32_t>(poseNodes.size());
        poseNodes.push_back(node);
        nodeIndices.emplace(node, index);
        if (node->name)
        {
            names.emplace(node->name, index);
        }
    };
    for (cgltf_size index = 0; index < skin->joints_count; ++index)
    {
        addNode(skin->joints[index]);
    }

    auto clip = std::make_unique<AnimationClip>();
    clip->Name = animation->name ? animation->name : entry->AnimationSelector;
    clip->SkeletonId = skeletonEntry->RuntimeId;
    clip->Tracks.resize(poseNodes.size());
    for (cgltf_size channelIndex = 0; channelIndex < animation->channels_count; ++channelIndex)
    {
        const cgltf_animation_channel &channel = animation->channels[channelIndex];
        if (!channel.target_node || !channel.sampler || !channel.sampler->input || !channel.sampler->output)
        {
            return {nullptr, AssetLoadError::LoaderFailed, "glTF animation contains an incomplete channel"};
        }
        // Pack import validates that ignored morph-weight channels remain identically zero.
        if (channel.target_path == cgltf_animation_path_type_weights)
        {
            continue;
        }
        if (channel.sampler->interpolation != cgltf_interpolation_type_step &&
            channel.sampler->interpolation != cgltf_interpolation_type_linear)
        {
            return {nullptr, AssetLoadError::LoaderFailed, "CUBICSPLINE animation interpolation is not supported"};
        }

        std::uint32_t nodeIndex = 0;
        if (&animationData == &skeletonData)
        {
            const auto found = nodeIndices.find(channel.target_node);
            if (found == nodeIndices.end())
            {
                continue;
            }
            nodeIndex = found->second;
        }
        else
        {
            if (!channel.target_node->name)
            {
                continue;
            }
            const auto found = names.find(channel.target_node->name);
            if (found == names.end())
            {
                continue;
            }
            nodeIndex = found->second;
        }

        std::vector<float> times;
        if (channel.sampler->input->type != cgltf_type_scalar ||
            channel.sampler->output->count != channel.sampler->input->count ||
            !ReadTimes(*channel.sampler->input, times))
        {
            return {nullptr, AssetLoadError::LoaderFailed, "glTF animation channel has invalid keyframe data"};
        }
        NodeAnimationTrack &track = clip->Tracks[nodeIndex];
        if (((channel.target_path == cgltf_animation_path_type_translation ||
              channel.target_path == cgltf_animation_path_type_scale) &&
             channel.sampler->output->type != cgltf_type_vec3) ||
            (channel.target_path == cgltf_animation_path_type_rotation &&
             channel.sampler->output->type != cgltf_type_vec4))
        {
            return {nullptr, AssetLoadError::LoaderFailed, "glTF animation channel has an invalid output type"};
        }
        clip->Duration = std::max(clip->Duration, times.empty() ? 0.0f : times.back());
        const AnimationInterpolation interpolation = ToInterpolation(channel.sampler->interpolation);
        if ((channel.target_path == cgltf_animation_path_type_translation && !track.Translations.empty()) ||
            (channel.target_path == cgltf_animation_path_type_rotation && !track.Rotations.empty()) ||
            (channel.target_path == cgltf_animation_path_type_scale && !track.Scales.empty()))
        {
            return {nullptr, AssetLoadError::LoaderFailed, "glTF animation has duplicate node channels"};
        }
        for (cgltf_size key = 0; key < times.size(); ++key)
        {
            if (channel.target_path == cgltf_animation_path_type_translation ||
                channel.target_path == cgltf_animation_path_type_scale)
            {
                float value[3];
                if (!cgltf_accessor_read_float(channel.sampler->output, key, value, 3))
                {
                    return {nullptr, AssetLoadError::LoaderFailed, "Could not decode glTF animation values"};
                }
                auto &keys = channel.target_path == cgltf_animation_path_type_translation ? track.Translations
                                                                                           : track.Scales;
                keys.push_back({times[key], glm::vec3(value[0], value[1], value[2])});
                if (channel.target_path == cgltf_animation_path_type_translation)
                {
                    track.TranslationInterpolation = interpolation;
                }
                else
                {
                    track.ScaleInterpolation = interpolation;
                }
            }
            else if (channel.target_path == cgltf_animation_path_type_rotation)
            {
                float value[4];
                if (!cgltf_accessor_read_float(channel.sampler->output, key, value, 4))
                {
                    return {nullptr, AssetLoadError::LoaderFailed, "Could not decode glTF animation values"};
                }
                const glm::quat rotation(value[3], value[0], value[1], value[2]);
                if (!std::isfinite(glm::length(rotation)) || glm::length(rotation) < 0.000001f)
                {
                    return {nullptr, AssetLoadError::LoaderFailed, "glTF animation contains an invalid rotation"};
                }
                track.Rotations.push_back({times[key], glm::normalize(rotation)});
                track.RotationInterpolation = interpolation;
            }
        }
    }
    return {std::move(clip), AssetLoadError::None, {}};
}

} // namespace Haki
