#pragma once

#include "Haki/Scene/Components.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Haki
{

struct MeshRendererDescription
{
    std::string Mesh;
    std::vector<std::string> Materials;
};

struct SkinnedMeshRendererDescription
{
    std::string Mesh;
    std::string Skeleton;
    std::vector<std::string> Materials;
};

struct AnimatorDescription
{
    std::unordered_map<std::string, std::string> Clips;
    std::string CurrentClip;
    float PlaybackSpeed = 1.0f;
    bool Playing = true;
    bool Loop = true;
    float DefaultFade = 0.2f;
};

struct EnvironmentDescription
{
    std::string Texture;
    float Intensity = 1.0f;
    float RotationDegrees = 0.0f;
};

struct EntityDescription
{
    std::string Id;
    std::string Name;
    std::optional<TransformComponent> Transform;
    std::optional<ParentTransformComponent> Parent;
    std::optional<LocomotionAnimationComponent> Locomotion;
    std::optional<CameraComponent> Camera;
    std::optional<MeshRendererDescription> MeshRenderer;
    std::optional<SkinnedMeshRendererDescription> SkinnedMeshRenderer;
    std::optional<AnimatorDescription> Animator;
    std::optional<DirectionalLightComponent> DirectionalLight;
    std::optional<PointLightComponent> PointLight;
    std::optional<SpotLightComponent> SpotLight;
    std::optional<RigidBodyComponent> RigidBody;
    std::optional<ColliderComponent> Collider;
    std::optional<CharacterControllerComponent> CharacterController;
    std::optional<PlayerInputComponent> PlayerInput;
    std::optional<ScriptComponent> Script;
};

struct SceneDescription
{
    std::string Id;
    std::optional<EnvironmentDescription> Environment;
    std::string Script; // optional scene-level Lua script, relative to the scripts root
    std::vector<EntityDescription> Entities;
};

struct SceneManifestEntry
{
    std::string Id;
    std::filesystem::path Source;
};

struct SceneManifest
{
    std::filesystem::path AssetCatalog;
    std::string EntryScene;
    std::vector<SceneManifestEntry> Scenes;
};

} // namespace Haki
