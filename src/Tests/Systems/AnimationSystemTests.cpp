#include "Haki/AssetManager/AnimationClip.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/AssetManager/Skeleton.h"
#include "Haki/Scene/Components.h"
#include "Haki/Systems/AnimationSystem.h"

#include <entt/entt.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace
{

bool Near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

void Check(bool condition, const char *name, int &failures)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += condition ? 0 : 1;
}

// Serves prebuilt in-memory resources by asset ID.
template <typename T>
class MemoryLoader final : public Haki::AssetLoader<T>
{
public:
    explicit MemoryLoader(std::map<Haki::AssetID, T> resources) : resources_(std::move(resources)) {}
    bool Supports(const Haki::AssetLoadRequest &request) const override { return resources_.contains(request.Id); }
    Haki::AssetLoaderResult<T> Load(const Haki::AssetLoadRequest &request) override
    {
        return {std::make_unique<T>(resources_.at(request.Id))};
    }

private:
    std::map<Haki::AssetID, T> resources_;
};

// A clip that moves the child node's x from `from` to `to` over `duration` seconds.
Haki::AnimationClip SlideClip(const char *name, float duration, float from, float to)
{
    Haki::AnimationClip clip;
    clip.Name = name;
    clip.Duration = duration;
    clip.Tracks.resize(2);
    clip.Tracks[1].Translations = {{0.0f, glm::vec3(from, 2.0f, 0.0f)}, {duration, glm::vec3(to, 2.0f, 0.0f)}};
    return clip;
}

void BlendingTests(const Haki::Skeleton &skeleton, int &failures)
{
    using namespace Haki;
    std::cout << "=== blending ===\n";

    // Pose math.
    AnimationPose a;
    AnimationPose b;
    AnimationPose out;
    a.Translations = {glm::vec3(0.0f)};
    b.Translations = {glm::vec3(4.0f, 0.0f, 0.0f)};
    a.Scales = b.Scales = {glm::vec3(1.0f)};
    a.Rotations = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    b.Rotations = {-glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))};
    AnimationSystem::BlendPoses(a, b, 0.0f, out);
    Check(Near(out.Translations[0].x, 0.0f), "blend weight 0 returns the first pose", failures);
    AnimationSystem::BlendPoses(a, b, 1.0f, out);
    Check(Near(out.Translations[0].x, 4.0f), "blend weight 1 returns the second pose", failures);
    AnimationSystem::BlendPoses(a, b, 0.5f, out);
    Check(Near(out.Translations[0].x, 2.0f) && Near(glm::degrees(glm::angle(out.Rotations[0])), 45.0f) &&
              Near(glm::length(out.Rotations[0]), 1.0f),
          "blending takes the short rotation path even when quaternion signs differ", failures);

    const std::vector<LocomotionSample> samples = {{"Idle", 0.0f}, {"Walk", 1.0f}, {"Run", 3.0f}};
    Check(Near(AnimationSystem::LocomotionPositionForSpeed(samples, 0.0f), 0.0f) &&
              Near(AnimationSystem::LocomotionPositionForSpeed(samples, 0.5f), 0.5f) &&
              Near(AnimationSystem::LocomotionPositionForSpeed(samples, 2.0f), 1.5f) &&
              Near(AnimationSystem::LocomotionPositionForSpeed(samples, 9.0f), 2.0f),
          "speed maps to a position between neighbouring samples", failures);
    LocomotionAnimationComponent legacy;
    legacy.IdleClip = "Idle";
    legacy.MoveClip = "Run";
    legacy.SpeedThreshold = 0.5f;
    const auto legacySamples = AnimationSystem::GetLocomotionSamples(legacy);
    Check(legacySamples.size() == 2 && legacySamples[0].Clip == "Idle" && legacySamples[1].Clip == "Run" &&
              Near(legacySamples[1].Speed, 0.5f),
          "legacy idle/move locomotion becomes a two-sample blend", failures);

    // Runtime: a character root with a skinned child that has the animator.
    AssetManager::Initialize();
    AssetManager::RegisterSkeletonLoader(std::make_unique<MemoryLoader<Skeleton>>(std::map<AssetID, Skeleton>{{1, skeleton}}));
    AssetManager::RegisterAnimationClipLoader(std::make_unique<MemoryLoader<AnimationClip>>(std::map<AssetID, AnimationClip>{
        {10, SlideClip("Idle", 1.0f, 0.0f, 0.0f)},
        {11, SlideClip("Walk", 1.0f, 0.0f, 1.0f)},
        {12, SlideClip("Run", 2.0f, 0.0f, 10.0f)},
        {13, SlideClip("Punch", 0.5f, 100.0f, 100.0f)}}));
    const auto skeletonHandle = AssetManager::LoadSkeleton({1, "memory"});
    std::unordered_map<std::string, AnimationClipHandle> clips;
    for (const auto &[id, name] : std::map<AssetID, std::string>{{10, "Idle"}, {11, "Walk"}, {12, "Run"}, {13, "Punch"}})
    {
        const auto handle = AssetManager::LoadAnimationClip({id, "memory"});
        if (handle) clips.emplace(name, handle.Value);
    }
    Check(bool(skeletonHandle) && clips.size() == 4, "in-memory skeleton and clips load", failures);
    if (!skeletonHandle || clips.size() != 4)
    {
        AssetManager::Shutdown();
        return;
    }

    entt::registry registry;
    const entt::entity root = registry.create();
    registry.emplace<EntityIdComponent>(root, "player");
    auto &controller = registry.emplace<CharacterControllerComponent>(root);
    const entt::entity visual = registry.create();
    registry.emplace<EntityIdComponent>(visual, "visual");
    registry.emplace<ParentTransformComponent>(visual).ParentId = "player";
    registry.emplace<SkinnedMeshRendererComponent>(visual).Skeleton = skeletonHandle.Value;
    auto &animator = registry.emplace<AnimatorComponent>(visual);
    animator.Clips = clips;
    animator.CurrentClipName = "Idle";
    auto &locomotion = registry.emplace<LocomotionAnimationComponent>(visual);
    locomotion.Blend = samples;
    locomotion.Smoothing = 0.0f;
    locomotion.MaxPlaybackScale = 2.0f;
    locomotion.Fade = 0.25f;
    const auto x = [&] { return animator.LastPose.Size() == 2 ? animator.LastPose.Translations[1].x : -1.0f; };

    AnimationSystem::Update(registry, 0.0f);
    Check(animator.Source == AnimatorSource::Locomotion && Near(x(), 0.0f) &&
              registry.get<SkinnedMeshRendererComponent>(visual).FinalBoneMatrices.size() == 2,
          "a standing character starts in locomotion on the idle clip", failures);

    controller.Velocity = {2.0f, -5.0f, 0.0f};
    AnimationSystem::Update(registry, 0.25f);
    // Halfway between Walk (1 s) and Run (2 s): phase rate 0.75/s, so phase 0.1875 -> Walk 0.1875, Run 1.875.
    Check(Near(animator.LocomotionSpeed, 2.0f) && Near(animator.LocomotionPhase, 0.1875f) &&
              Near(x(), 1.03125f),
          "speed blends the neighbouring clips at one shared phase", failures);

    controller.Velocity = {6.0f, 0.0f, 0.0f};
    const float phaseBefore = animator.LocomotionPhase;
    AnimationSystem::Update(registry, 0.1f);
    Check(Near(animator.LocomotionPhase - phaseBefore, 0.1f),
          "above the last sample the clip plays faster, up to MaxPlaybackScale", failures);

    locomotion.Smoothing = 0.1f;
    controller.Velocity = {0.0f, 0.0f, 0.0f};
    AnimationSystem::Update(registry, 0.05f);
    Check(animator.LocomotionPosition > 0.1f && animator.LocomotionPosition < 2.0f,
          "smoothing moves through the blend instead of popping on a sudden stop", failures);
    locomotion.Smoothing = 0.0f;
    AnimationSystem::Update(registry, 0.0f);

    const float before = x();
    Check(AnimationSystem::Play(animator, "Punch", 0.2f), "play accepts a known clip", failures);
    Check(!AnimationSystem::Play(animator, "Missing"), "play rejects an unknown clip", failures);
    AnimationSystem::Update(registry, 0.0f);
    Check(animator.Source == AnimatorSource::Clip && Near(x(), before) && !animator.OneShotFinished,
          "a crossfade starts from the pose that was on screen", failures);
    AnimationSystem::Update(registry, 0.1f);
    Check(Near(x(), before + (100.0f - before) * 0.5f), "halfway through the fade the poses are mixed evenly",
          failures);
    AnimationSystem::Update(registry, 0.1f);
    Check(Near(x(), 100.0f) && animator.FadeDuration == 0.0f, "after the fade only the new clip plays", failures);

    AnimationSystem::Update(registry, 0.35f);
    Check(animator.OneShotFinished && !animator.IsPlaying, "a one-shot clip finishes at its end", failures);
    AnimationSystem::Update(registry, 0.0f);
    Check(animator.Source == AnimatorSource::Locomotion && Near(animator.FadeDuration, 0.25f) && Near(x(), 100.0f),
          "a finished one-shot fades back to locomotion", failures);
    AnimationSystem::Update(registry, 0.25f);
    Check(Near(x(), 0.0f), "locomotion is fully back after its fade", failures);

    AnimationSystem::Play(animator, "Punch", 0.2f);
    AnimationSystem::Update(registry, 0.1f);
    const float midFade = x();
    AnimationSystem::Play(animator, "Walk", 0.2f, true);
    AnimationSystem::Update(registry, 0.0f);
    Check(Near(x(), midFade), "interrupting a fade starts from the blended pose without a pop", failures);
    AnimationSystem::ReturnToLocomotion(animator, 0.0f);
    AnimationSystem::Update(registry, 0.0f);
    Check(animator.Source == AnimatorSource::Locomotion && Near(x(), 0.0f),
          "returning to locomotion with no fade switches immediately", failures);

    AnimationSystem::Play(animator, "Punch", 0.0f, false, 1.0f, true);
    AnimationSystem::Update(registry, 1.0f);
    AnimationSystem::Update(registry, 0.5f);
    Check(animator.Source == AnimatorSource::Clip && animator.OneShotFinished && Near(x(), 100.0f),
          "a held one-shot stays on its last pose instead of returning to locomotion", failures);
    AnimationSystem::ReturnToLocomotion(animator, 0.0f);
    AnimationSystem::Update(registry, 0.0f);
    Check(animator.Source == AnimatorSource::Locomotion && !animator.HoldOnFinish && Near(x(), 0.0f),
          "ReturnToLocomotion releases a held clip", failures);

    // An animator without locomotion still honours direct clip changes (snaps, no fade).
    registry.remove<LocomotionAnimationComponent>(visual);
    animator.Source = AnimatorSource::Clip;
    animator.CurrentClipName = "Punch";
    animator.IsPlaying = true;
    AnimationSystem::Update(registry, 0.0f);
    Check(Near(x(), 100.0f), "setting CurrentClipName directly switches immediately", failures);

    AssetManager::Shutdown();
}

} // namespace

int main()
{
    Haki::Skeleton skeleton;
    skeleton.Nodes.push_back({"root", -1, 0, {1.0f, 0.0f, 0.0f}});
    skeleton.Nodes.push_back({"child", 0, 1, {0.0f, 2.0f, 0.0f}});
    skeleton.Joints.push_back({1, glm::translate(glm::mat4(1.0f), {0.0f, -1.0f, 0.0f})});
    skeleton.Joints.push_back({0, glm::mat4(1.0f)});

    std::vector<glm::mat4> globalPoses;
    std::vector<glm::mat4> bones;
    Haki::AnimationSystem::Evaluate(skeleton, nullptr, 0.0f, globalPoses, bones);

    int failures = 0;
    Check(globalPoses.size() == 2 && Near(globalPoses[1][3].x, 1.0f) && Near(globalPoses[1][3].y, 2.0f),
          "null clip evaluates the hierarchical bind pose", failures);
    Check(bones.size() == 2 && Near(bones[0][3].x, 1.0f) && Near(bones[0][3].y, 1.0f) &&
              Near(bones[1][3].x, 1.0f),
          "bone palette order and inverse bind matrices are applied", failures);

    Haki::AnimationClip clip;
    clip.Tracks.resize(2);
    clip.Tracks[1].Translations = {{0.0f, glm::vec3(0.0f, 2.0f, 0.0f)},
                                   {2.0f, glm::vec3(4.0f, 2.0f, 0.0f)}};
    Haki::AnimationSystem::Evaluate(skeleton, &clip, 1.0f, globalPoses, bones);
    Check(Near(globalPoses[1][3].x, 3.0f), "linear channels interpolate and missing channels use bind values",
          failures);

    clip.Tracks[1].TranslationInterpolation = Haki::AnimationInterpolation::Step;
    Haki::AnimationSystem::Evaluate(skeleton, &clip, 1.0f, globalPoses, bones);
    Check(Near(globalPoses[1][3].x, 1.0f), "step channels hold the preceding key", failures);

    Haki::AnimationSystem::Evaluate(skeleton, &clip, 5.0f, globalPoses, bones);
    Check(Near(globalPoses[1][3].x, 5.0f), "sampling clamps after the final key", failures);

    BlendingTests(skeleton, failures);
    return failures == 0 ? 0 : 1;
}
