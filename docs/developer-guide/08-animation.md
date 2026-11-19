# 8. Animation

[Guide index](README.md) · Previous: [Physics](07-physics.md) · Next: [Lua gameplay](09-lua-gameplay.md)

Animation changes the pose of the character's visual mesh. Character movement changes where its controller is in the world. Haki connects them through measured movement speed, but they remain separate jobs.

## The pieces

| Piece | What it means |
| --- | --- |
| Skinned mesh | A mesh whose vertices can follow joints |
| Skeleton | A hierarchy of pose nodes plus the joints used to deform the mesh |
| Bind pose | The reference pose used when the mesh was attached to the skeleton |
| Clip | Timed changes to node translation, rotation, or scale |
| Animator | Current playback and blending state for an entity |
| Pose | The set of local transforms for the skeleton at one moment |
| Joint palette | The final matrices sent to the GPU to deform vertices |

Blender calls the skeleton an armature. A joint often corresponds to a bone, but the imported pose hierarchy may also include non-joint ancestor nodes needed to preserve correct transforms.

The hierarchy is stored parent before child, so the system can build each node's world-within-the-skeleton transform after its parent.

## What a clip contains

A walking clip does not store a new full mesh for every frame. It stores keys such as “at 0.2 seconds this node has this rotation.” The runtime finds a pose between those keys.

**Interpolation** means finding an in-between value. Haki supports:

- **Step:** hold the previous key until the next one.
- **Linear:** blend positions and scales between adjacent keys; smoothly blend rotations using quaternion interpolation.

The glTF animation loader currently rejects `CUBICSPLINE` interpolation. Export clips in a supported form instead of assuming every Blender animation curve will load unchanged.

Tracks without a keyed value use the skeleton's bind transform for that part. A named clip in an animator maps to an animation asset handle, so Lua uses a short gameplay name rather than a model file path.

## From time to a deformed mesh

`AnimationSystem::Update()` performs this chain:

1. Find the skeleton and animator for a skinned mesh.
2. Advance the current clip or locomotion phase using simulation delta time.
3. Sample local pose transforms.
4. Blend poses if a transition is active.
5. Combine local transforms through the pose hierarchy.
6. Build final joint matrices using bind information.
7. Store those matrices on the skinned renderer for rendering.

For each joint, the code combines inverse mesh bind-world, the current global joint pose, and inverse bind matrix. The bind corrections ensure that “no animation” reproduces the reference mesh instead of applying its original placement twice.

You do not need to derive those matrices to play a clip. They become relevant when a mesh explodes, folds incorrectly, or jumps position even though its controller transform looks correct.

### How a vertex follows bones

Each skinned vertex has up to four joint indices and weights in the current shader input. A vertex near the elbow can partly follow the upper arm and partly follow the forearm. The GPU combines the selected joint transformations using those weights.

The renderer's current joint palette limit is **128 joints**. Check the imported model against that limit; the animation system is not an unlimited-bone renderer.

If a skinned renderer has a valid skeleton but no animator, the system can produce its bind pose. Missing skeleton data clears usable skin matrices instead.

## Locomotion uses speed

Locomotion means the repeating movement animation: idle, walk, run. Haki can blend along a one-dimensional speed list. “One-dimensional” just means speed is the input, rather than a larger graph with several inputs.

For example, an authored blend list could associate:

```text
0 m/s → Idle
2 m/s → Walk
5 m/s → Run
```

At a speed between two samples, the system blends the neighboring poses. It smooths the blend position over time so a small speed change does not jerk the pose. Clips share a normalized phase, helping their movement cycles stay aligned while blending.

The speed comes from the nearest character controller ancestor's **actual horizontal velocity**. This matters when a wall stops the character: the animation should not run at full speed simply because the movement button is held.

There are also older idle/move settings for simpler scenes. Read `LocomotionAnimationComponent` and its parser before adding new blend fields; names in Blender, JSON, and C++ are related but not always identical.

`MaxPlaybackScale` can allow faster playback beyond the final authored speed. It is a playback adjustment, not a root-motion movement system.

## Playing an action from Lua

The Lua entity methods can find the entity's animator or an attached child's animator. Clip names must already exist in that animator.

```lua
-- Run once when the gameplay action starts.
if self.entity:has_animation("Punch") then
    self.entity:play_animation("Punch", {
        fade = 0.12,
        loop = false,
        speed = 1.0
    })
end
```

This overrides locomotion for the one-shot. When it finishes, the animator can blend back to locomotion. Avoid calling `play_animation` every update while a button is held: repeatedly starting an action can prevent it from progressing as intended.

For a pose that should remain at the end:

```lua
self.entity:play_animation("Death", {
    fade = 0.15,
    loop = false,
    hold = true
})
```

Use `self.entity:stop_animation(0.2)` to return to locomotion with a fade. Without locomotion, stopping freezes clip playback instead.

Useful queries include `has_animator()`, `has_animation(name)`, `animation()`, `animation_time()`, `animation_length(name)`, `animation_finished()`, and `locomotion_speed()`. While locomotion owns the pose, `animation()` reports `"locomotion"` rather than one fixed clip name.

`animation_finished()` is a playback state flag, not a guaranteed queued event. Automatic return to locomotion resets it. If gameplay needs a reliable attack window or completion event, design that timing explicitly rather than assuming an animation-event system exists.

## Crossfading

A crossfade gradually mixes the previous pose with the new target pose. Haki keeps the last evaluated pose and uses it as the starting pose, then increases the new pose's weight over the fade duration.

Positions and scales are mixed normally. Rotations use quaternion interpolation and choose compatible signs so the blend takes the short rotation path.

This avoids a hard jump between idle and punch. It does not automatically align feet, prevent all sliding, or keep a weapon's hand contact fixed. Those need suitable source animation and, for more advanced behavior, additional systems.

## Movement and attacks remain gameplay work

There is no general root-motion path that extracts a clip's travel and moves the character controller through physics. Use in-place locomotion for the current controller workflow. A traveling root in a downloaded clip can move the visual away from its capsule.

Likewise, a fist passing through an enemy in the animation does not automatically deal damage. Gameplay must choose the hit time, query targets, apply rules, and manage repeated hits.

The current system is a clip player with fades and a speed blend. Full animation graphs, layered upper-body masks, inverse kinematics, and a generalized animation-event editor are additional work.

## Try this

Walk toward a wall while watching `locomotion_speed()`. Then trigger a one-shot and let it return to locomotion. This checks the whole chain: controller result → speed blend → action override → pose → skinned rendering.

## Source reading route

1. [Skeleton.h](../../src/Haki/AssetManager/Skeleton.h) and [AnimationClip.h](../../src/Haki/AssetManager/AnimationClip.h): loaded data.
2. [GltfSkeletonLoader.cpp](../../src/Haki/AssetLoader/GltfSkeletonLoader.cpp) and [GltfAnimationClipLoader.cpp](../../src/Haki/AssetLoader/GltfAnimationClipLoader.cpp): import rules.
3. [AnimationSystem.cpp](../../src/Haki/Systems/AnimationSystem.cpp): `SamplePose`, `BlendPoses`, `ComposePose`, `Play`, and `Update`.
4. [ScriptBindings.cpp](../../src/Haki/Scripting/ScriptBindings.cpp): animator lookup and Lua methods.
5. [GltfPBR.vert](../../Assets/Engine/Shaders/GltfPBR.vert): GPU skinning.
