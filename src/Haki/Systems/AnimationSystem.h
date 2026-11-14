#pragma once

#include <entt/entt.hpp>

#include <glm/mat4x4.hpp>

#include <string>
#include <vector>

namespace Haki
{

class AnimationClip;
class Skeleton;
struct AnimationPose;
struct AnimatorComponent;
struct LocomotionAnimationComponent;
struct LocomotionSample;

// Plays skeletal animation. Per animator and frame:
//   1. the target pose comes from one clip, or from the locomotion blend (two clips mixed by speed);
//   2. while a fade is running, it is blended with the pose that was on screen when the fade started;
//   3. the result is composed into joint and bone matrices for the renderer.
class AnimationSystem
{
public:
    static void Update(entt::registry &registry, float deltaTime);

    // Exposed for deterministic evaluation and tests; a null clip evaluates the bind pose.
    static void Evaluate(const Skeleton &skeleton, const AnimationClip *clip, float time,
                         std::vector<glm::mat4> &globalPoses, std::vector<glm::mat4> &boneMatrices);

    // Pose building blocks.
    static void SamplePose(const Skeleton &skeleton, const AnimationClip *clip, float time, AnimationPose &pose);
    // Lerps translation/scale and takes the short rotation path. Weight 0 = a, 1 = b. Sizes must match.
    static void BlendPoses(const AnimationPose &a, const AnimationPose &b, float weight, AnimationPose &out);
    static void ComposePose(const Skeleton &skeleton, const AnimationPose &pose,
                            std::vector<glm::mat4> &globalPoses, std::vector<glm::mat4> &boneMatrices);

    // Fades from the current on-screen pose to `clip` over `fade` seconds (negative = the animator's
    // DefaultFade). Suspends locomotion until the clip finishes (one-shot) or ReturnToLocomotion is called.
    // hold: a one-shot stays on its last pose when it ends (e.g. a death) until the next Play/Return.
    // Returns false if the animator has no clip with that name.
    static bool Play(AnimatorComponent &animator, const std::string &clip, float fade = -1.0f, bool loop = false,
                     float speed = 1.0f, bool hold = false);
    // Fades back to the locomotion blend (negative fade = the animator's DefaultFade).
    static void ReturnToLocomotion(AnimatorComponent &animator, float fade = -1.0f);

    // Samples used by a locomotion component: its Blend, or the legacy idle/move pair.
    static std::vector<LocomotionSample> GetLocomotionSamples(const LocomotionAnimationComponent &locomotion);
    // Continuous position in the sample list for a speed: index of the lower sample + weight to the next.
    static float LocomotionPositionForSpeed(const std::vector<LocomotionSample> &samples, float speed);
};

} // namespace Haki
