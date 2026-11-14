#pragma once

#include "Haki/AssetManager/AssetTypes.h"
#include "Haki/Physics/PhysicsTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Haki
{

    struct EntityIdComponent
    {
        std::string Id;
    };

    struct NameComponent
    {
        std::string Name;
    };

    struct TransformComponent
    {
        glm::vec3 Translation{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 Scale{1.0f};
        glm::mat4 WorldMatrix{1.0f};
    };

    // Runtime parent relationship, resolved by stable authored entity ID.
    // Physics bodies/controllers are world roots; attached visuals have no physics of their own.
    struct ParentTransformComponent
    {
        std::string ParentId;
        TransformComponent Local;
    };

    // One point of a 1D locomotion blend: play Clip when the character moves at Speed (m/s).
    struct LocomotionSample
    {
        std::string Clip;
        float Speed = 0.0f;
    };

    // Drives the animator on the same entity from the horizontal speed of the nearest character controller
    // (itself or an ancestor). Neighbouring samples are blended by speed; clips stay phase-synced.
    struct LocomotionAnimationComponent
    {
        // Legacy two-clip form, used when Blend is empty: Idle at 0 m/s, MoveClip from SpeedThreshold.
        std::string IdleClip = "Idle";
        std::string MoveClip = "Run";
        float SpeedThreshold = 0.1f;

        std::vector<LocomotionSample> Blend; // speeds strictly increasing
        float Fade = 0.2f;                   // seconds to fade back after a one-shot clip
        float Smoothing = 0.1f;              // time constant (s) for moving through the blend; 0 = instant
        float MaxPlaybackScale = 1.0f;       // above the last sample, speed the clip up by speed/lastSpeed, up to this
    };

    enum class CameraProjection : std::uint8_t
    {
        Perspective,
        Orthographic
    };

    struct CameraComponent
    {
        CameraProjection Projection = CameraProjection::Perspective;
        float FieldOfViewDegrees = 60.0f; // vertical; perspective only
        float OrthographicSize = 10.0f;   // half the view height in metres; orthographic only
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        bool Primary = false;
    };

    struct SkinnedMeshRendererComponent
    {
        MeshHandle Mesh;
        SkeletonHandle Skeleton;
        std::vector<MaterialHandle> Materials;

        // Runtime output, indexed by the skeleton's skin-joint palette.
        std::vector<glm::mat4> FinalBoneMatrices;
    };

    struct MeshRendererComponent
    {
        MeshHandle Mesh;
        std::vector<MaterialHandle> Materials;
    };

    struct DirectionalLightComponent
    {
        glm::vec3 Color{1.0f};
        float Intensity = 1.0f;
    };

    struct PointLightComponent
    {
        glm::vec3 Color{1.0f};
        float Intensity = 1.0f;
        float Range = 10.0f;
    };

    struct SpotLightComponent
    {
        glm::vec3 Color{1.0f};
        float Intensity = 1.0f;
        float Range = 10.0f;
        float InnerAngleDegrees = 20.0f;
        float OuterAngleDegrees = 30.0f;
    };

    // Local (parent-relative) transform of every skeleton pose node.
    struct AnimationPose
    {
        std::vector<glm::vec3> Translations;
        std::vector<glm::quat> Rotations;
        std::vector<glm::vec3> Scales;

        std::size_t Size() const { return Rotations.size(); }
    };

    enum class AnimatorSource : std::uint8_t
    {
        Clip,       // CurrentClipName plays on its own
        Locomotion  // LocomotionAnimationComponent picks and blends clips
    };

    struct AnimatorComponent
    {
        std::unordered_map<std::string, AnimationClipHandle> Clips;

        std::string CurrentClipName;
        float CurrentTime = 0.0f;
        float PlaybackSpeed = 1.0f; // authored multiplier for everything this animator plays
        bool IsPlaying = true;
        bool Loop = true;
        float DefaultFade = 0.2f;   // seconds; used when a play request doesn't give a fade

        // Runtime state. Use AnimationSystem::Play / ReturnToLocomotion instead of writing these directly.
        AnimatorSource Source = AnimatorSource::Clip;
        float ClipSpeed = 1.0f;          // per-play multiplier on top of PlaybackSpeed
        bool OneShotFinished = false;    // a non-looping clip reached its end
        bool HoldOnFinish = false;       // a finished one-shot keeps its last pose instead of returning to locomotion
        float FadeDuration = 0.0f;
        float FadeElapsed = 0.0f;
        AnimationPose FadeFrom;          // pose on screen when the current fade started
        AnimationPose LastPose;          // last evaluated output
        std::string EvaluatedClipName;   // detects CurrentClipName being changed directly (snaps, no fade)
        bool LocomotionStarted = false;
        float LocomotionSpeed = 0.0f;    // measured horizontal speed (m/s)
        float LocomotionPosition = 0.0f; // smoothed position in the blend: sample index + weight to the next
        float LocomotionPhase = 0.0f;    // shared normalized cycle time (0..1) of the blended clips

        // Runtime output, indexed by skeleton pose node.
        std::vector<glm::mat4> GlobalJointPoses;
    };

    // Simulation properties of an entity. Needs a TransformComponent and a ColliderComponent;
    // the PhysicsSystem creates the Jolt body on its next update.
    struct RigidBodyComponent
    {
        PhysicsMotionType Motion = PhysicsMotionType::Static;
        PhysicsLayer Layer = PhysicsLayer::Static;
        float Mass = 0.0f; // kilograms; 0 = computed from the collider volume (density 1000 kg/m^3)
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        bool LockRotation = false; // translation-only (pushable props that must stay upright)
        bool IsSensor = false;     // trigger volume: reports contact events but never pushes back

        // Runtime, owned by the PhysicsSystem.
        std::uint32_t BodyId = InvalidPhysicsBodyId;
    };

    // Collision shape in the entity's local space, in metres before the transform's scale is applied.
    struct ColliderComponent
    {
        ColliderShape Shape = ColliderShape::Box;
        glm::vec3 HalfExtents{0.5f}; // Box
        float Radius = 0.5f;         // Sphere, Capsule
        float HalfHeight = 0.5f;     // Capsule cylinder half-length (excludes the caps)
        glm::vec3 Offset{0.0f};      // shape centre relative to the entity origin
    };

    // Capsule character moved by Jolt's CharacterVirtual. The entity origin is at the feet.
    // Must not be combined with RigidBodyComponent or ColliderComponent.
    struct CharacterControllerComponent
    {
        float Radius = 0.35f;        // capsule radius (m)
        float HalfHeight = 0.55f;    // cylinder half-length; total height = 2 * (HalfHeight + Radius)
        float MaxSlopeDegrees = 45.0f;
        float StepHeight = 0.3f;     // how high a ledge or stair the character can step onto
        float Mass = 70.0f;          // kg; pushes down on whatever it stands on
        float MaxStrength = 200.0f;  // N; how hard it can push dynamic bodies
        float GravityFactor = 1.0f;

        // Input: horizontal velocity (m/s) requested by gameplay, written every tick.
        glm::vec3 DesiredVelocity{0.0f};

        // Runtime outputs, owned by the PhysicsSystem.
        glm::vec3 Velocity{0.0f};
        glm::vec3 GroundNormal{0.0f, 1.0f, 0.0f};
        bool Grounded = false;
        std::uint32_t InnerBodyId = InvalidPhysicsBodyId; // kinematic hit capsule seen by queries and sensors
    };

    // Value of a script property authored in scene JSON: number, bool, string or vec3.
    using ScriptValue = std::variant<bool, double, std::string, glm::vec3>;

    // Lua behaviour attached to an entity. Path is relative to the scripts root (Assets/Scripts).
    // Properties override the defaults in the script's `properties` table.
    struct ScriptComponent
    {
        std::string Path;
        std::unordered_map<std::string, ScriptValue> Properties;
    };

    // Marks the locally controlled character (read by the PlayerControllerSystem).
    struct PlayerInputComponent
    {
        float MoveSpeed = 5.0f; // m/s
    };

} // namespace Haki
