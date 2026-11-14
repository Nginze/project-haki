#include "Haki/Systems/AnimationSystem.h"

#include "Haki/AssetManager/AnimationClip.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/AssetManager/Skeleton.h"
#include "Haki/Scene/Components.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

namespace Haki
{
namespace
{

template <typename T, typename Interpolate>
T Sample(const std::vector<Keyframe<T>> &keys, AnimationInterpolation interpolation, float time,
         const T &fallback, Interpolate interpolate)
{
    if (keys.empty())
    {
        return fallback;
    }
    if (keys.size() == 1 || time <= keys.front().TimeStamp)
    {
        return keys.front().Value;
    }
    if (time >= keys.back().TimeStamp)
    {
        return keys.back().Value;
    }

    const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                                        [](float value, const auto &key) { return value < key.TimeStamp; });
    const auto &next = *upper;
    const auto &previous = *(upper - 1);
    const float span = next.TimeStamp - previous.TimeStamp;
    if (interpolation == AnimationInterpolation::Step || !std::isfinite(span) || span <= 0.0f)
    {
        return previous.Value;
    }
    const float alpha = std::clamp((time - previous.TimeStamp) / span, 0.0f, 1.0f);
    return interpolate(previous.Value, next.Value, alpha);
}

glm::vec3 SampleTranslation(const NodeAnimationTrack &track, float time, const glm::vec3 &fallback)
{
    return Sample(track.Translations, track.TranslationInterpolation, time, fallback,
                  [](const auto &a, const auto &b, float t) { return glm::mix(a, b, t); });
}

glm::quat SampleRotation(const NodeAnimationTrack &track, float time, const glm::quat &fallback)
{
    const glm::quat value = Sample(track.Rotations, track.RotationInterpolation, time, fallback,
                                   [](const auto &a, const auto &b, float t) { return glm::slerp(a, b, t); });
    const float length = glm::length(value);
    return std::isfinite(length) && length > 0.00001f ? glm::normalize(value) : fallback;
}

glm::vec3 SampleScale(const NodeAnimationTrack &track, float time, const glm::vec3 &fallback)
{
    return Sample(track.Scales, track.ScaleInterpolation, time, fallback,
                  [](const auto &a, const auto &b, float t) { return glm::mix(a, b, t); });
}

glm::mat4 LocalMatrix(const glm::vec3 &translation, const glm::quat &rotation, const glm::vec3 &scale)
{
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

float SafeDelta(float deltaTime)
{
    return std::isfinite(deltaTime) ? std::max(deltaTime, 0.0f) : 0.0f;
}

float ClipDuration(const AnimationClip *clip)
{
    return clip && std::isfinite(clip->Duration) && clip->Duration > 0.0f ? clip->Duration : 0.0f;
}

const AnimationClip *FindClip(const AnimatorComponent &animator, const std::string &name)
{
    const auto found = animator.Clips.find(name);
    return found != animator.Clips.end() ? AssetManager::GetAnimationClip(found->second) : nullptr;
}

float Wrap01(float value)
{
    value -= std::floor(value);
    return std::isfinite(value) && value < 1.0f ? value : 0.0f;
}

void StartFade(AnimatorComponent &animator, float fade)
{
    if (!(fade >= 0.0f) || !std::isfinite(fade))
    {
        fade = std::isfinite(animator.DefaultFade) ? std::max(animator.DefaultFade, 0.0f) : 0.0f;
    }
    animator.FadeElapsed = 0.0f;
    if (fade > 0.0f && animator.LastPose.Size() > 0)
    {
        animator.FadeFrom = animator.LastPose;
        animator.FadeDuration = fade;
    }
    else
    {
        animator.FadeDuration = 0.0f;
    }
}

// Advances CurrentTime of a single clip; a non-looping clip stops at its end and flags OneShotFinished.
void AdvanceClip(AnimatorComponent &animator, const AnimationClip *clip, float deltaTime)
{
    const float duration = ClipDuration(clip);
    if (!std::isfinite(animator.CurrentTime) || (clip && duration == 0.0f))
    {
        animator.CurrentTime = 0.0f;
    }
    const float speed = animator.PlaybackSpeed * animator.ClipSpeed;
    if (!clip || !animator.IsPlaying || duration == 0.0f || !std::isfinite(speed))
    {
        return;
    }

    animator.CurrentTime += deltaTime * speed;
    if (animator.Loop)
    {
        animator.CurrentTime = std::fmod(animator.CurrentTime, duration);
        if (animator.CurrentTime < 0.0f) animator.CurrentTime += duration;
        return;
    }
    const bool ended = (speed > 0.0f && animator.CurrentTime >= duration) ||
                       (speed < 0.0f && animator.CurrentTime <= 0.0f);
    animator.CurrentTime = std::clamp(animator.CurrentTime, 0.0f, duration);
    if (ended)
    {
        animator.IsPlaying = false;
        animator.OneShotFinished = true;
    }
}

// The character controller that moves this entity: on itself, or on the nearest ancestor.
const CharacterControllerComponent *FindController(const entt::registry &registry, entt::entity entity,
                                                   const std::unordered_map<std::string, entt::entity> &ids)
{
    std::unordered_set<entt::entity> visited;
    while (visited.insert(entity).second)
    {
        if (const auto *controller = registry.try_get<CharacterControllerComponent>(entity))
        {
            return controller;
        }
        const auto *parent = registry.try_get<ParentTransformComponent>(entity);
        if (!parent)
        {
            return nullptr;
        }
        const auto found = ids.find(parent->ParentId);
        if (found == ids.end() || !registry.valid(found->second))
        {
            return nullptr;
        }
        entity = found->second;
    }
    return nullptr;
}

struct PoseScratch
{
    AnimationPose Target;
    AnimationPose A;
    AnimationPose B;
};

void EvaluateLocomotion(const Skeleton &skeleton, AnimatorComponent &animator,
                        const LocomotionAnimationComponent &locomotion, float deltaTime, bool snap,
                        PoseScratch &scratch)
{
    const std::vector<LocomotionSample> samples = AnimationSystem::GetLocomotionSamples(locomotion);
    if (samples.empty())
    {
        AnimationSystem::SamplePose(skeleton, nullptr, 0.0f, scratch.Target);
        return;
    }

    // Move through the blend smoothly: controllers can jump from 0 to full speed in one tick, and a
    // straight speed lookup would then pop from Idle to Run.
    const float target = AnimationSystem::LocomotionPositionForSpeed(samples, animator.LocomotionSpeed);
    if (snap || !(locomotion.Smoothing > 0.0f) || !std::isfinite(animator.LocomotionPosition))
    {
        animator.LocomotionPosition = target;
    }
    else
    {
        animator.LocomotionPosition += (target - animator.LocomotionPosition) *
                                       (1.0f - std::exp(-deltaTime / locomotion.Smoothing));
        if (std::abs(target - animator.LocomotionPosition) < 0.0001f) animator.LocomotionPosition = target;
    }

    const auto last = static_cast<float>(samples.size() - 1);
    const float position = std::clamp(animator.LocomotionPosition, 0.0f, last);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = std::min(lower + 1, samples.size() - 1);
    const float weight = upper == lower ? 0.0f : position - static_cast<float>(lower);

    const AnimationClip *clipA = FindClip(animator, samples[lower].Clip);
    const AnimationClip *clipB = FindClip(animator, samples[upper].Clip);
    const float durationA = ClipDuration(clipA);
    const float durationB = ClipDuration(clipB);

    // Both clips share one normalized phase so their cycles (e.g. footfalls) line up; the phase advances
    // at the blended rate of the two clips.
    const float rateA = durationA > 0.0f ? 1.0f / durationA : 0.0f;
    const float rateB = durationB > 0.0f ? 1.0f / durationB : 0.0f;
    float rate = rateA == 0.0f ? rateB : rateB == 0.0f ? rateA : rateA + (rateB - rateA) * weight;

    float scale = 1.0f;
    const float lastSpeed = samples.back().Speed;
    if (lastSpeed > 0.0f && animator.LocomotionSpeed > lastSpeed)
    {
        scale = std::clamp(animator.LocomotionSpeed / lastSpeed, 1.0f, std::max(locomotion.MaxPlaybackScale, 1.0f));
    }
    rate *= scale * (std::isfinite(animator.PlaybackSpeed) ? animator.PlaybackSpeed : 1.0f);
    animator.LocomotionPhase = Wrap01(animator.LocomotionPhase + deltaTime * rate);

    AnimationSystem::SamplePose(skeleton, clipA, animator.LocomotionPhase * durationA, scratch.A);
    if (weight <= 0.0f)
    {
        std::swap(scratch.Target, scratch.A);
        return;
    }
    AnimationSystem::SamplePose(skeleton, clipB, animator.LocomotionPhase * durationB, scratch.B);
    AnimationSystem::BlendPoses(scratch.A, scratch.B, weight, scratch.Target);
}

} // namespace

void AnimationSystem::SamplePose(const Skeleton &skeleton, const AnimationClip *clip, float time,
                                 AnimationPose &pose)
{
    const std::size_t count = skeleton.Nodes.size();
    pose.Translations.resize(count);
    pose.Rotations.resize(count);
    pose.Scales.resize(count);
    if (!std::isfinite(time)) time = 0.0f;
    for (std::size_t index = 0; index < count; ++index)
    {
        const PoseNode &node = skeleton.Nodes[index];
        glm::vec3 translation = node.BindTranslation;
        glm::quat rotation = node.BindRotation;
        glm::vec3 scale = node.BindScale;
        if (clip && index < clip->Tracks.size())
        {
            const auto &track = clip->Tracks[index];
            translation = SampleTranslation(track, time, translation);
            rotation = SampleRotation(track, time, rotation);
            scale = SampleScale(track, time, scale);
        }
        pose.Translations[index] = translation;
        pose.Rotations[index] = rotation;
        pose.Scales[index] = scale;
    }
}

void AnimationSystem::BlendPoses(const AnimationPose &a, const AnimationPose &b, float weight, AnimationPose &out)
{
    const std::size_t count = std::min(a.Size(), b.Size());
    weight = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 0.0f;
    out.Translations.resize(count);
    out.Rotations.resize(count);
    out.Scales.resize(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        out.Translations[index] = glm::mix(a.Translations[index], b.Translations[index], weight);
        out.Scales[index] = glm::mix(a.Scales[index], b.Scales[index], weight);

        // q and -q are the same rotation; flip b onto a's hemisphere so we never take the long way round.
        const glm::quat from = a.Rotations[index];
        glm::quat to = b.Rotations[index];
        if (glm::dot(from, to) < 0.0f) to = -to;
        const glm::quat mixed = glm::slerp(from, to, weight);
        const float length = glm::length(mixed);
        out.Rotations[index] = std::isfinite(length) && length > 0.00001f ? mixed / length : from;
    }
}

void AnimationSystem::ComposePose(const Skeleton &skeleton, const AnimationPose &pose,
                                  std::vector<glm::mat4> &globalPoses, std::vector<glm::mat4> &boneMatrices)
{
    const std::size_t count = skeleton.Nodes.size();
    globalPoses.assign(count, glm::mat4(1.0f));
    for (std::size_t index = 0; index < count && index < pose.Size(); ++index)
    {
        const glm::mat4 local = LocalMatrix(pose.Translations[index], pose.Rotations[index], pose.Scales[index]);
        const std::int32_t parent = skeleton.Nodes[index].ParentIndex;
        globalPoses[index] = parent >= 0 && static_cast<std::size_t>(parent) < index
                                 ? globalPoses[static_cast<std::size_t>(parent)] * local
                                 : local;
    }
    boneMatrices.assign(skeleton.Joints.size(), glm::mat4(1.0f));
    for (std::size_t index = 0; index < skeleton.Joints.size(); ++index)
    {
        const Joint &joint = skeleton.Joints[index];
        if (joint.NodeIndex < globalPoses.size())
        {
            boneMatrices[index] = skeleton.InverseMeshBindWorld * globalPoses[joint.NodeIndex] *
                                  joint.InverseBindMatrix;
        }
    }
}

void AnimationSystem::Evaluate(const Skeleton &skeleton, const AnimationClip *clip, float time,
                               std::vector<glm::mat4> &globalPoses, std::vector<glm::mat4> &boneMatrices)
{
    AnimationPose pose;
    SamplePose(skeleton, clip, std::isfinite(time) ? time : 0.0f, pose);
    ComposePose(skeleton, pose, globalPoses, boneMatrices);
}

bool AnimationSystem::Play(AnimatorComponent &animator, const std::string &clip, float fade, bool loop, float speed,
                          bool hold)
{
    if (!animator.Clips.contains(clip))
    {
        return false;
    }
    StartFade(animator, fade);
    animator.Source = AnimatorSource::Clip;
    animator.CurrentClipName = clip;
    animator.EvaluatedClipName = clip;
    animator.ClipSpeed = std::isfinite(speed) ? speed : 1.0f;
    animator.CurrentTime = 0.0f;
    if (animator.ClipSpeed < 0.0f)
    {
        // Reversed playback starts from the end.
        animator.CurrentTime = ClipDuration(FindClip(animator, clip));
    }
    animator.Loop = loop;
    animator.IsPlaying = true;
    animator.OneShotFinished = false;
    animator.HoldOnFinish = hold && !loop;
    return true;
}

void AnimationSystem::ReturnToLocomotion(AnimatorComponent &animator, float fade)
{
    StartFade(animator, fade);
    animator.Source = AnimatorSource::Locomotion;
    animator.OneShotFinished = false;
    animator.HoldOnFinish = false;
    animator.ClipSpeed = 1.0f;
}

std::vector<LocomotionSample> AnimationSystem::GetLocomotionSamples(const LocomotionAnimationComponent &locomotion)
{
    if (!locomotion.Blend.empty())
    {
        return locomotion.Blend;
    }
    const float threshold = std::isfinite(locomotion.SpeedThreshold) ? locomotion.SpeedThreshold : 0.0f;
    return {{locomotion.IdleClip, 0.0f}, {locomotion.MoveClip, std::max(threshold, 0.001f)}};
}

float AnimationSystem::LocomotionPositionForSpeed(const std::vector<LocomotionSample> &samples, float speed)
{
    if (samples.empty() || !std::isfinite(speed) || speed <= samples.front().Speed)
    {
        return 0.0f;
    }
    for (std::size_t index = 0; index + 1 < samples.size(); ++index)
    {
        const float low = samples[index].Speed;
        const float high = samples[index + 1].Speed;
        if (speed < high)
        {
            const float span = high - low;
            return static_cast<float>(index) + (span > 0.0f ? std::clamp((speed - low) / span, 0.0f, 1.0f) : 1.0f);
        }
    }
    return static_cast<float>(samples.size() - 1);
}

void AnimationSystem::Update(entt::registry &registry, float deltaTime)
{
    deltaTime = SafeDelta(deltaTime);

    // Entity ID lookup for walking from an animated mesh up to its character controller.
    std::unordered_map<std::string, entt::entity> ids;
    if (!registry.view<LocomotionAnimationComponent>().empty())
    {
        for (const entt::entity entity : registry.view<EntityIdComponent>())
        {
            ids.emplace(registry.get<EntityIdComponent>(entity).Id, entity);
        }
    }

    static thread_local PoseScratch scratch;
    auto skinned = registry.view<SkinnedMeshRendererComponent>();
    for (const entt::entity entity : skinned)
    {
        auto &renderer = skinned.get<SkinnedMeshRendererComponent>(entity);
        AnimatorComponent *animator = registry.try_get<AnimatorComponent>(entity);
        const Skeleton *skeleton = AssetManager::GetSkeleton(renderer.Skeleton);
        if (!skeleton)
        {
            if (animator)
            {
                animator->GlobalJointPoses.clear();
            }
            renderer.FinalBoneMatrices.clear();
            continue;
        }
        if (!animator)
        {
            std::vector<glm::mat4> bindGlobalPoses;
            SamplePose(*skeleton, nullptr, 0.0f, scratch.Target);
            ComposePose(*skeleton, scratch.Target, bindGlobalPoses, renderer.FinalBoneMatrices);
            continue;
        }

        const LocomotionAnimationComponent *locomotion = registry.try_get<LocomotionAnimationComponent>(entity);
        const CharacterControllerComponent *controller =
            locomotion ? FindController(registry, entity, ids) : nullptr;
        bool snapLocomotion = false;
        if (locomotion && controller)
        {
            animator->LocomotionSpeed = glm::length(glm::vec2(controller->Velocity.x, controller->Velocity.z));
            if (!std::isfinite(animator->LocomotionSpeed)) animator->LocomotionSpeed = 0.0f;
            if (!animator->LocomotionStarted)
            {
                animator->LocomotionStarted = true;
                animator->Source = AnimatorSource::Locomotion;
                snapLocomotion = true;
            }
            else if (animator->Source == AnimatorSource::Clip && animator->OneShotFinished && !animator->HoldOnFinish)
            {
                ReturnToLocomotion(*animator, locomotion->Fade);
            }
        }

        if (animator->Source == AnimatorSource::Locomotion && locomotion && controller)
        {
            EvaluateLocomotion(*skeleton, *animator, *locomotion, deltaTime, snapLocomotion, scratch);
        }
        else
        {
            if (animator->CurrentClipName != animator->EvaluatedClipName)
            {
                // Set directly rather than through Play(): switch immediately.
                animator->EvaluatedClipName = animator->CurrentClipName;
                animator->FadeDuration = 0.0f;
                animator->OneShotFinished = false;
            }
            const AnimationClip *clip = FindClip(*animator, animator->CurrentClipName);
            AdvanceClip(*animator, clip, deltaTime);
            SamplePose(*skeleton, clip, animator->CurrentTime, scratch.Target);
        }

        if (animator->FadeDuration > 0.0f && animator->FadeFrom.Size() == scratch.Target.Size())
        {
            animator->FadeElapsed = std::min(animator->FadeElapsed + deltaTime, animator->FadeDuration);
            BlendPoses(animator->FadeFrom, scratch.Target, animator->FadeElapsed / animator->FadeDuration,
                       animator->LastPose);
            if (animator->FadeElapsed >= animator->FadeDuration)
            {
                animator->FadeDuration = 0.0f;
            }
        }
        else
        {
            animator->FadeDuration = 0.0f;
            animator->LastPose = scratch.Target;
        }
        ComposePose(*skeleton, animator->LastPose, animator->GlobalJointPoses, renderer.FinalBoneMatrices);
    }
}

} // namespace Haki
