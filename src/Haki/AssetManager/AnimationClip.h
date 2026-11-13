#pragma once
#include "Haki/AssetManager/AssetTypes.h"
#include <string>
#include <vector>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Haki
{

template <typename T>
struct Keyframe
{
    float TimeStamp = 0.0f; // In seconds
    T Value{};
};

enum class AnimationInterpolation
{
    Step,
    Linear
};

struct NodeAnimationTrack
{
    std::vector<Keyframe<glm::vec3>> Translations;
    std::vector<Keyframe<glm::quat>> Rotations;
    std::vector<Keyframe<glm::vec3>> Scales;
    AnimationInterpolation TranslationInterpolation = AnimationInterpolation::Linear;
    AnimationInterpolation RotationInterpolation = AnimationInterpolation::Linear;
    AnimationInterpolation ScaleInterpolation = AnimationInterpolation::Linear;

    bool IsEmpty() const 
    { 
        return Translations.empty() && Rotations.empty() && Scales.empty(); 
    }
};

using JointAnimationTrack = NodeAnimationTrack;

class AnimationClip
{
public:
    std::string Name;
    AssetID SkeletonId = InvalidAssetID;
    float Duration = 0.0f; // Total length of the clip in seconds

    // Flat array indexed by pose node (Tracks.size() == Skeleton.Nodes.size()).
    std::vector<NodeAnimationTrack> Tracks;
};

} // namespace Haki
