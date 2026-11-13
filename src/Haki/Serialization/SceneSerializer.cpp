#include "Haki/Scripting/ScriptPaths.h"
#include "Haki/Serialization/SceneSerializer.h"

#include "Haki/AssetManager/AssetCatalog.h"
#include "Haki/AssetManager/AnimationClip.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/AssetManager/Skeleton.h"
#include "Haki/GL/Mesh.h"
#include "Haki/Renderer/RendererTypes.h"
#include "Haki/Scene/Scene.h"
#include "Haki/Serialization/JsonUtils.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace Haki
{
namespace
{

using Json = JsonUtils::Json;

bool ParseVec3(const Json &value, glm::vec3 &result, const std::filesystem::path &source,
               const std::string &pointer, std::vector<SerializationError> &errors)
{
    if (!value.is_array() || value.size() != 3 ||
        !std::all_of(value.begin(), value.end(), [](const Json &part) { return part.is_number(); }))
    {
        JsonUtils::AddError(errors, source, pointer, "Expected an array of three numbers");
        return false;
    }
    result = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    return true;
}

bool ParseQuaternion(const Json &value, glm::quat &result, const std::filesystem::path &source,
                     const std::string &pointer, std::vector<SerializationError> &errors)
{
    if (!value.is_array() || value.size() != 4 ||
        !std::all_of(value.begin(), value.end(), [](const Json &part) { return part.is_number(); }))
    {
        JsonUtils::AddError(errors, source, pointer, "Expected quaternion [x, y, z, w]");
        return false;
    }
    result = glm::quat(value[3].get<float>(), value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    const float length = glm::length(result);
    if (length <= 0.00001f)
    {
        JsonUtils::AddError(errors, source, pointer, "Quaternion cannot have zero length");
        return false;
    }
    result = glm::normalize(result);
    return true;
}

bool ReadFiniteFloat(const Json &value, const char *name, float &result, const std::filesystem::path &source,
                     const std::string &pointer, std::vector<SerializationError> &errors)
{
    if (!value.contains(name))
    {
        return true;
    }
    if (!value[name].is_number() || !std::isfinite(value[name].get<float>()))
    {
        JsonUtils::AddError(errors, source, pointer + "/" + name, "Expected a finite number");
        return false;
    }
    result = value[name].get<float>();
    return true;
}

bool ReadBool(const Json &value, const char *name, bool &result, const std::filesystem::path &source,
              const std::string &pointer, std::vector<SerializationError> &errors)
{
    if (!value.contains(name))
    {
        return true;
    }
    if (!value[name].is_boolean())
    {
        JsonUtils::AddError(errors, source, pointer + "/" + name, "Expected a boolean");
        return false;
    }
    result = value[name].get<bool>();
    return true;
}

template <typename Enum, std::size_t Count>
bool ReadEnum(const Json &value, const char *name, Enum &result,
              const std::array<std::pair<std::string_view, Enum>, Count> &names,
              const std::filesystem::path &source, const std::string &pointer,
              std::vector<SerializationError> &errors)
{
    if (!value.contains(name))
    {
        return true;
    }
    if (value[name].is_string())
    {
        const std::string text = value[name].get<std::string>();
        for (const auto &[candidate, enumValue] : names)
        {
            if (text == candidate)
            {
                result = enumValue;
                return true;
            }
        }
    }
    std::string expected = "Expected one of:";
    for (const auto &[candidate, enumValue] : names)
    {
        (void)enumValue;
        expected += " \"" + std::string(candidate) + "\"";
    }
    JsonUtils::AddError(errors, source, pointer + "/" + name, expected);
    return false;
}

constexpr std::array<std::pair<std::string_view, PhysicsMotionType>, 3> MotionTypeNames{{
    {"static", PhysicsMotionType::Static},
    {"kinematic", PhysicsMotionType::Kinematic},
    {"dynamic", PhysicsMotionType::Dynamic},
}};

constexpr std::array<std::pair<std::string_view, PhysicsLayer>, 4> PhysicsLayerNames{{
    {"static", PhysicsLayer::Static},
    {"dynamic", PhysicsLayer::Dynamic},
    {"character", PhysicsLayer::Character},
    {"trigger", PhysicsLayer::Trigger},
}};

constexpr std::array<std::pair<std::string_view, ColliderShape>, 3> ColliderShapeNames{{
    {"box", ColliderShape::Box},
    {"sphere", ColliderShape::Sphere},
    {"capsule", ColliderShape::Capsule},
}};

// Applies the fields present in value onto body, so it serves both new components and prefab overrides.
// When deriveLayer is set and no layer is given, the layer follows from the motion type and sensor flag.
bool ParseRigidBody(const Json &value, RigidBodyComponent &body, bool deriveLayer,
                    const std::filesystem::path &source, const std::string &pointer,
                    std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value,
                                           {"motion", "layer", "mass", "friction", "restitution", "linearDamping",
                                            "angularDamping", "gravityFactor", "lockRotation", "sensor"},
                                           source, pointer, errors);
    if (!value.is_object())
    {
        return false;
    }
    valid = ReadEnum(value, "motion", body.Motion, MotionTypeNames, source, pointer, errors) && valid;
    valid = ReadEnum(value, "layer", body.Layer, PhysicsLayerNames, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "mass", body.Mass, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "friction", body.Friction, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "restitution", body.Restitution, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "linearDamping", body.LinearDamping, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "angularDamping", body.AngularDamping, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "gravityFactor", body.GravityFactor, source, pointer, errors) && valid;
    valid = ReadBool(value, "lockRotation", body.LockRotation, source, pointer, errors) && valid;
    valid = ReadBool(value, "sensor", body.IsSensor, source, pointer, errors) && valid;

    if (deriveLayer && !value.contains("layer"))
    {
        body.Layer = body.IsSensor ? PhysicsLayer::Trigger
                     : body.Motion == PhysicsMotionType::Static ? PhysicsLayer::Static
                                                                : PhysicsLayer::Dynamic;
    }
    if (body.Mass < 0.0f)
    {
        JsonUtils::AddError(errors, source, pointer + "/mass", "Mass cannot be negative (0 = from collider volume)");
        valid = false;
    }
    if (body.Friction < 0.0f || body.Restitution < 0.0f || body.Restitution > 1.0f || body.LinearDamping < 0.0f ||
        body.AngularDamping < 0.0f)
    {
        JsonUtils::AddError(errors, source, pointer,
                            "Friction and damping must be >= 0 and restitution must be in [0, 1]");
        valid = false;
    }
    if (body.Layer == PhysicsLayer::Static && body.Motion != PhysicsMotionType::Static)
    {
        JsonUtils::AddError(errors, source, pointer + "/layer", "Only static bodies can use the static layer");
        valid = false;
    }
    return valid;
}

bool ParseCollider(const Json &value, ColliderComponent &collider, const std::filesystem::path &source,
                   const std::string &pointer, std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value, {"shape", "halfExtents", "radius", "halfHeight", "offset"}, source,
                                           pointer, errors);
    if (!value.is_object())
    {
        return false;
    }
    valid = ReadEnum(value, "shape", collider.Shape, ColliderShapeNames, source, pointer, errors) && valid;
    if (value.contains("halfExtents"))
    {
        valid = ParseVec3(value["halfExtents"], collider.HalfExtents, source, pointer + "/halfExtents", errors) &&
                valid;
    }
    if (value.contains("offset"))
    {
        valid = ParseVec3(value["offset"], collider.Offset, source, pointer + "/offset", errors) && valid;
    }
    valid = ReadFiniteFloat(value, "radius", collider.Radius, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "halfHeight", collider.HalfHeight, source, pointer, errors) && valid;

    const bool positiveExtents = std::isfinite(collider.HalfExtents.x) && std::isfinite(collider.HalfExtents.y) &&
                                 std::isfinite(collider.HalfExtents.z) && collider.HalfExtents.x > 0.0f &&
                                 collider.HalfExtents.y > 0.0f && collider.HalfExtents.z > 0.0f;
    if (collider.Shape == ColliderShape::Box && !positiveExtents)
    {
        JsonUtils::AddError(errors, source, pointer + "/halfExtents", "Box half extents must be positive");
        valid = false;
    }
    if (collider.Shape != ColliderShape::Box && !(collider.Radius > 0.0f))
    {
        JsonUtils::AddError(errors, source, pointer + "/radius", "Radius must be positive");
        valid = false;
    }
    if (collider.Shape == ColliderShape::Capsule && !(collider.HalfHeight > 0.0f))
    {
        JsonUtils::AddError(errors, source, pointer + "/halfHeight", "Capsule half height must be positive");
        valid = false;
    }
    if (!std::isfinite(collider.Offset.x) || !std::isfinite(collider.Offset.y) || !std::isfinite(collider.Offset.z))
    {
        JsonUtils::AddError(errors, source, pointer + "/offset", "Offset must be finite");
        valid = false;
    }
    return valid;
}

bool ParseCharacterController(const Json &value, CharacterControllerComponent &character,
                              const std::filesystem::path &source, const std::string &pointer,
                              std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value,
                                           {"radius", "halfHeight", "maxSlopeDegrees", "stepHeight", "mass",
                                            "maxStrength", "gravityFactor"},
                                           source, pointer, errors);
    if (!value.is_object())
    {
        return false;
    }
    valid = ReadFiniteFloat(value, "radius", character.Radius, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "halfHeight", character.HalfHeight, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "maxSlopeDegrees", character.MaxSlopeDegrees, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "stepHeight", character.StepHeight, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "mass", character.Mass, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "maxStrength", character.MaxStrength, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "gravityFactor", character.GravityFactor, source, pointer, errors) && valid;

    if (!(character.Radius > 0.0f) || !(character.HalfHeight > 0.0f))
    {
        JsonUtils::AddError(errors, source, pointer, "Character radius and halfHeight must be positive");
        valid = false;
    }
    if (character.MaxSlopeDegrees < 0.0f || character.MaxSlopeDegrees >= 90.0f)
    {
        JsonUtils::AddError(errors, source, pointer + "/maxSlopeDegrees", "Max slope must be in [0, 90)");
        valid = false;
    }
    if (character.StepHeight < 0.0f || !(character.Mass > 0.0f) || character.MaxStrength < 0.0f)
    {
        JsonUtils::AddError(errors, source, pointer,
                            "stepHeight and maxStrength must be >= 0 and mass must be positive");
        valid = false;
    }
    return valid;
}

constexpr std::array<std::pair<std::string_view, CameraProjection>, 2> CameraProjectionNames{{
    {"perspective", CameraProjection::Perspective},
    {"orthographic", CameraProjection::Orthographic},
}};

// Applies onto an existing component so prefab overrides only change the fields they name.
bool ParseCamera(const Json &value, CameraComponent &camera, const std::filesystem::path &source,
                 const std::string &pointer, std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value, {"projection", "fovYDegrees", "orthoSize", "near", "far", "primary"},
                                           source, pointer, errors);
    if (!value.is_object())
    {
        return false;
    }
    valid = ReadEnum(value, "projection", camera.Projection, CameraProjectionNames, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "fovYDegrees", camera.FieldOfViewDegrees, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "orthoSize", camera.OrthographicSize, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "near", camera.NearPlane, source, pointer, errors) && valid;
    valid = ReadFiniteFloat(value, "far", camera.FarPlane, source, pointer, errors) && valid;
    valid = ReadBool(value, "primary", camera.Primary, source, pointer, errors) && valid;

    if (camera.Projection == CameraProjection::Perspective &&
        (camera.FieldOfViewDegrees <= 0.0f || camera.FieldOfViewDegrees >= 180.0f))
    {
        JsonUtils::AddError(errors, source, pointer + "/fovYDegrees", "Field of view must be in (0, 180) degrees");
        valid = false;
    }
    if (camera.Projection == CameraProjection::Orthographic && !(camera.OrthographicSize > 0.0f))
    {
        JsonUtils::AddError(errors, source, pointer + "/orthoSize", "Orthographic size must be positive");
        valid = false;
    }
    if (camera.NearPlane <= 0.0f || camera.FarPlane <= camera.NearPlane)
    {
        JsonUtils::AddError(errors, source, pointer, "Invalid camera projection values (need 0 < near < far)");
        valid = false;
    }
    return valid;
}

// Script paths are relative to the scripts root and may not leave it.
bool ValidateScriptPath(const std::string &path, const std::filesystem::path &source, const std::string &pointer,
                        std::vector<SerializationError> &errors)
{
    const std::filesystem::path relative(path);
    bool escapes = relative.is_absolute() || relative.has_root_name() || relative.has_root_directory();
    for (const std::filesystem::path &part : relative)
    {
        escapes = escapes || part == "..";
    }
    if (path.empty() || escapes)
    {
        JsonUtils::AddError(errors, source, pointer, "Script path must be relative to the scripts folder");
        return false;
    }
    if (relative.extension() != ".lua")
    {
        JsonUtils::AddError(errors, source, pointer, "Script path must end in .lua");
        return false;
    }
    return true;
}

// Applies onto an existing component so prefab overrides can change the path or merge properties.
bool ParseScript(const Json &value, ScriptComponent &script, const std::filesystem::path &source,
                 const std::string &pointer, std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value, {"path", "properties"}, source, pointer, errors);
    if (!value.is_object())
    {
        return false;
    }
    if (value.contains("path"))
    {
        if (value["path"].is_string())
        {
            script.Path = value["path"].get<std::string>();
        }
        else
        {
            JsonUtils::AddError(errors, source, pointer + "/path", "Expected a string");
            valid = false;
        }
    }
    if (script.Path.empty())
    {
        JsonUtils::AddError(errors, source, pointer + "/path", "Script requires a path");
        valid = false;
    }
    else if (value.contains("path"))
    {
        valid = ValidateScriptPath(script.Path, source, pointer + "/path", errors) && valid;
    }
    if (!value.contains("properties"))
    {
        return valid;
    }
    const Json &properties = value["properties"];
    if (!properties.is_object())
    {
        JsonUtils::AddError(errors, source, pointer + "/properties", "Expected an object");
        return false;
    }
    for (const auto &[name, property] : properties.items())
    {
        const std::string propertyPointer = pointer + "/properties/" + name;
        if (property.is_boolean())
        {
            script.Properties[name] = property.get<bool>();
        }
        else if (property.is_number() && std::isfinite(property.get<double>()))
        {
            script.Properties[name] = property.get<double>();
        }
        else if (property.is_string())
        {
            script.Properties[name] = property.get<std::string>();
        }
        else if (property.is_array() && property.size() == 3 &&
                 std::all_of(property.begin(), property.end(),
                             [](const Json &n) { return n.is_number() && std::isfinite(n.get<double>()); }))
        {
            script.Properties[name] = glm::vec3(property[0].get<float>(), property[1].get<float>(),
                                                property[2].get<float>());
        }
        else
        {
            JsonUtils::AddError(errors, source, propertyPointer,
                                "Script properties must be a finite number, bool, string or [x, y, z]");
            valid = false;
        }
    }
    return valid;
}

bool ParsePlayerInput(const Json &value, PlayerInputComponent &player, const std::filesystem::path &source,
                      const std::string &pointer, std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value, {"moveSpeed"}, source, pointer, errors);
    if (!value.is_object())
    {
        return false;
    }
    valid = ReadFiniteFloat(value, "moveSpeed", player.MoveSpeed, source, pointer, errors) && valid;
    if (player.MoveSpeed < 0.0f)
    {
        JsonUtils::AddError(errors, source, pointer + "/moveSpeed", "Move speed cannot be negative");
        valid = false;
    }
    return valid;
}

bool ParseTransform(const Json &value, TransformComponent &transform, const std::filesystem::path &source,
                    const std::string &pointer, std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value, {"translation", "rotation", "scale"}, source, pointer, errors);
    if (!value.is_object())
    {
        return false;
    }
    if (value.contains("translation"))
    {
        valid = ParseVec3(value["translation"], transform.Translation, source, pointer + "/translation", errors) && valid;
    }
    if (value.contains("rotation"))
    {
        valid = ParseQuaternion(value["rotation"], transform.Rotation, source, pointer + "/rotation", errors) && valid;
    }
    if (value.contains("scale"))
    {
        valid = ParseVec3(value["scale"], transform.Scale, source, pointer + "/scale", errors) && valid;
    }
    return valid;
}

TransformComponent ComposeTransform(const TransformComponent &parent, const TransformComponent &child)
{
    TransformComponent result;
    result.Translation = parent.Translation + parent.Rotation * (parent.Scale * child.Translation);
    result.Rotation = glm::normalize(parent.Rotation * child.Rotation);
    result.Scale = parent.Scale * child.Scale;
    return result;
}

bool ParseColorIntensity(const Json &value, glm::vec3 &color, float &intensity,
                         std::initializer_list<std::string_view> extraFields, const std::filesystem::path &source,
                         const std::string &pointer, std::vector<SerializationError> &errors)
{
    std::vector<std::string_view> fields{"color", "intensity"};
    fields.insert(fields.end(), extraFields.begin(), extraFields.end());
    bool valid = true;
    if (!value.is_object())
    {
        JsonUtils::AddError(errors, source, pointer, "Expected an object");
        return false;
    }
    std::unordered_set<std::string_view> allowed(fields.begin(), fields.end());
    for (const auto &[name, ignored] : value.items())
    {
        (void)ignored;
        if (!allowed.contains(name))
        {
            JsonUtils::AddError(errors, source, pointer + "/" + name, "Unknown field");
            valid = false;
        }
    }
    if (value.contains("color"))
    {
        valid = ParseVec3(value["color"], color, source, pointer + "/color", errors) && valid;
        if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z))
        {
            JsonUtils::AddError(errors, source, pointer + "/color", "Expected finite color components");
            valid = false;
        }
    }
    if (value.contains("intensity"))
    {
        if (!value["intensity"].is_number() || !std::isfinite(value["intensity"].get<float>()) ||
            value["intensity"].get<float>() < 0.0f)
        {
            JsonUtils::AddError(errors, source, pointer + "/intensity", "Expected a non-negative number");
            valid = false;
        }
        else
        {
            intensity = value["intensity"].get<float>();
        }
    }
    return valid;
}

bool ParseEnvironment(const Json &value, EnvironmentDescription &environment,
                      const std::filesystem::path &source, const std::string &pointer,
                      std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value, {"texture", "intensity", "rotationDegrees"},
                                           source, pointer, errors);
    if (!value.is_object() || !value.contains("texture") || !value["texture"].is_string())
    {
        JsonUtils::AddError(errors, source, pointer, "Environment requires a string texture ID");
        return false;
    }

    environment.Texture = value["texture"].get<std::string>();
    if (value.contains("intensity"))
    {
        if (!value["intensity"].is_number())
        {
            JsonUtils::AddError(errors, source, pointer + "/intensity", "Expected a non-negative finite number");
            valid = false;
        }
        else
        {
            const float intensity = value["intensity"].get<float>();
            if (!std::isfinite(intensity) || intensity < 0.0f)
            {
                JsonUtils::AddError(errors, source, pointer + "/intensity", "Expected a non-negative finite number");
                valid = false;
            }
            else
            {
                environment.Intensity = intensity;
            }
        }
    }
    if (value.contains("rotationDegrees"))
    {
        if (!value["rotationDegrees"].is_number())
        {
            JsonUtils::AddError(errors, source, pointer + "/rotationDegrees", "Expected a finite number");
            valid = false;
        }
        else
        {
            const float rotation = value["rotationDegrees"].get<float>();
            if (!std::isfinite(rotation))
            {
                JsonUtils::AddError(errors, source, pointer + "/rotationDegrees", "Expected a finite number");
                valid = false;
            }
            else
            {
                environment.RotationDegrees = rotation;
            }
        }
    }
    return valid;
}

bool ParseSkinnedMeshRenderer(const Json &value, SkinnedMeshRendererDescription &renderer,
                              const std::filesystem::path &source, const std::string &pointer,
                              std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value, {"mesh", "skeleton", "materials"}, source, pointer, errors);
    if (!value.is_object() || !value.contains("mesh") || !value["mesh"].is_string() ||
        !value.contains("skeleton") || !value["skeleton"].is_string() ||
        !value.contains("materials") || !value["materials"].is_array() || value["materials"].empty() ||
        !std::all_of(value["materials"].begin(), value["materials"].end(),
                     [](const Json &material) { return material.is_string(); }))
    {
        JsonUtils::AddError(errors, source, pointer,
                            "Skinned mesh renderer requires mesh and skeleton IDs and a non-empty material ID array");
        return false;
    }
    renderer.Mesh = value["mesh"].get<std::string>();
    renderer.Skeleton = value["skeleton"].get<std::string>();
    renderer.Materials = value["materials"].get<std::vector<std::string>>();
    return valid;
}

bool ParseAnimator(const Json &value, AnimatorDescription &animator, const std::filesystem::path &source,
                   const std::string &pointer, std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(
        value, {"clips", "currentClip", "playbackSpeed", "playing", "loop", "defaultFade"}, source, pointer, errors);
    if (!value.is_object() || !value.contains("clips") || !value["clips"].is_object() ||
        value["clips"].empty() || !value.contains("currentClip") || !value["currentClip"].is_string())
    {
        JsonUtils::AddError(errors, source, pointer,
                            "Animator requires a non-empty clip map and a currentClip string");
        return false;
    }
    for (const auto &[name, clip] : value["clips"].items())
    {
        if (name.empty() || !clip.is_string() || clip.get<std::string>().empty())
        {
            JsonUtils::AddError(errors, source, pointer + "/clips/" + name,
                                "Clip names and asset IDs must be non-empty strings");
            valid = false;
        }
        else
        {
            animator.Clips.emplace(name, clip.get<std::string>());
        }
    }
    animator.CurrentClip = value["currentClip"].get<std::string>();
    if (!animator.Clips.contains(animator.CurrentClip))
    {
        JsonUtils::AddError(errors, source, pointer + "/currentClip", "Current clip is not present in clips");
        valid = false;
    }
    if (value.contains("playbackSpeed"))
    {
        if (!value["playbackSpeed"].is_number() || !std::isfinite(value["playbackSpeed"].get<float>()))
        {
            JsonUtils::AddError(errors, source, pointer + "/playbackSpeed", "Expected a finite number");
            valid = false;
        }
        else animator.PlaybackSpeed = value["playbackSpeed"].get<float>();
    }
    if (ReadFiniteFloat(value, "defaultFade", animator.DefaultFade, source, pointer, errors))
    {
        if (animator.DefaultFade < 0.0f)
        {
            JsonUtils::AddError(errors, source, pointer + "/defaultFade", "Expected a fade time >= 0 seconds");
            valid = false;
        }
    }
    else valid = false;
    for (const auto &[field, target] :
         {std::pair<const char *, bool *>{"playing", &animator.Playing}, {"loop", &animator.Loop}})
    {
        if (value.contains(field))
        {
            if (!value[field].is_boolean())
            {
                JsonUtils::AddError(errors, source, pointer + "/" + field, "Expected a boolean");
                valid = false;
            }
            else *target = value[field].get<bool>();
        }
    }
    return valid;
}

// Either the legacy pair {"idleClip", "moveClip", "speedThreshold"} or a speed blend:
// {"blend": [{"clip": "Idle", "speed": 0}, {"clip": "Run", "speed": 4.5}], "fade", "smoothing", "maxPlaybackScale"}.
bool ParseLocomotion(const Json &value, const AnimatorDescription *animator, LocomotionAnimationComponent &locomotion,
                     const std::filesystem::path &source, const std::string &pointer,
                     std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value,
                                           {"idleClip", "moveClip", "speedThreshold", "blend", "fade", "smoothing",
                                            "maxPlaybackScale"},
                                           source, pointer, errors);
    if (!value.is_object())
    {
        JsonUtils::AddError(errors, source, pointer, "Expected a locomotion object");
        return false;
    }
    const auto hasClip = [&](const std::string &clip) { return animator && animator->Clips.contains(clip); };
    const bool legacy = value.contains("idleClip") || value.contains("moveClip") || value.contains("speedThreshold");
    if (value.contains("blend"))
    {
        if (legacy)
        {
            JsonUtils::AddError(errors, source, pointer,
                                "Use either blend or idleClip/moveClip/speedThreshold, not both");
            valid = false;
        }
        const Json &blend = value["blend"];
        if (!blend.is_array() || blend.empty())
        {
            JsonUtils::AddError(errors, source, pointer + "/blend", "Expected a non-empty array of {clip, speed}");
            return false;
        }
        for (std::size_t index = 0; index < blend.size(); ++index)
        {
            const std::string samplePointer = pointer + "/blend/" + std::to_string(index);
            const Json &entry = blend[index];
            valid = JsonUtils::ValidateFields(entry, {"clip", "speed"}, source, samplePointer, errors) && valid;
            if (!entry.is_object() || !entry.contains("clip") || !entry["clip"].is_string() ||
                !entry.contains("speed") || !entry["speed"].is_number())
            {
                JsonUtils::AddError(errors, source, samplePointer, "Expected {\"clip\": name, \"speed\": m/s}");
                valid = false;
                continue;
            }
            LocomotionSample sample{entry["clip"].get<std::string>(), entry["speed"].get<float>()};
            if (!hasClip(sample.Clip))
            {
                JsonUtils::AddError(errors, source, samplePointer + "/clip",
                                    "Clip '" + sample.Clip + "' is not in the animator's clips");
                valid = false;
            }
            if (!std::isfinite(sample.Speed) || sample.Speed < 0.0f ||
                (!locomotion.Blend.empty() && sample.Speed <= locomotion.Blend.back().Speed))
            {
                JsonUtils::AddError(errors, source, samplePointer + "/speed",
                                    "Speeds must be finite, >= 0 and strictly increasing");
                valid = false;
            }
            locomotion.Blend.push_back(std::move(sample));
        }
        locomotion.MaxPlaybackScale = 2.0f;
    }
    else if (!value.contains("idleClip") || !value["idleClip"].is_string() || !value.contains("moveClip") ||
             !value["moveClip"].is_string())
    {
        JsonUtils::AddError(errors, source, pointer, "Locomotion requires a blend array or idleClip and moveClip names");
        return false;
    }
    else
    {
        locomotion.IdleClip = value["idleClip"].get<std::string>();
        locomotion.MoveClip = value["moveClip"].get<std::string>();
        valid = ReadFiniteFloat(value, "speedThreshold", locomotion.SpeedThreshold, source, pointer, errors) && valid;
        if (!hasClip(locomotion.IdleClip) || !hasClip(locomotion.MoveClip) || locomotion.SpeedThreshold < 0)
        {
            JsonUtils::AddError(errors, source, pointer, "Locomotion needs valid animator clips and nonnegative threshold");
            valid = false;
        }
    }

    for (const auto &[field, target, minimum] :
         {std::tuple<const char *, float *, float>{"fade", &locomotion.Fade, 0.0f},
          {"smoothing", &locomotion.Smoothing, 0.0f},
          {"maxPlaybackScale", &locomotion.MaxPlaybackScale, 1.0f}})
    {
        if (!ReadFiniteFloat(value, field, *target, source, pointer, errors))
        {
            valid = false;
        }
        else if (*target < minimum)
        {
            JsonUtils::AddError(errors, source, pointer + "/" + field,
                                "Expected a value >= " + std::to_string(static_cast<int>(minimum)));
            valid = false;
        }
    }
    return valid;
}

bool ParseEntity(const Json &value, EntityDescription &entity, const std::filesystem::path &source,
                 const std::string &pointer, std::vector<SerializationError> &errors)
{
    bool valid = JsonUtils::ValidateFields(value, {"id", "name", "components"}, source, pointer, errors);
    if (!value.is_object() || !value.contains("id") || !value["id"].is_string() ||
        !value.contains("components") || !value["components"].is_object())
    {
        JsonUtils::AddError(errors, source, pointer, "Entity requires string id and object components");
        return false;
    }
    entity.Id = value["id"].get<std::string>();
    entity.Name = entity.Id;
    if (value.contains("name"))
    {
        if (value["name"].is_string())
        {
            entity.Name = value["name"].get<std::string>();
        }
        else
        {
            JsonUtils::AddError(errors, source, pointer + "/name", "Expected a string");
            valid = false;
        }
    }
    if (entity.Id.empty())
    {
        JsonUtils::AddError(errors, source, pointer + "/id", "Entity ID cannot be empty");
        valid = false;
    }

    const Json &components = value["components"];
    valid = JsonUtils::ValidateFields(components,
                                      {"transform", "camera", "meshRenderer", "skinnedMeshRenderer", "animator",
                                       "directionalLight", "pointLight", "spotLight", "rigidBody", "collider",
                                       "characterController", "playerInput", "parent", "locomotion", "script"},
                                      source, pointer + "/components", errors) && valid;
    if (components.contains("transform"))
    {
        entity.Transform.emplace();
        valid = ParseTransform(components["transform"], *entity.Transform, source,
                               pointer + "/components/transform", errors) && valid;
    }
    if (components.contains("camera"))
    {
        entity.Camera.emplace();
        valid = ParseCamera(components["camera"], *entity.Camera, source, pointer + "/components/camera", errors) &&
                valid;
    }
    if (components.contains("meshRenderer"))
    {
        const Json &renderer = components["meshRenderer"];
        valid = JsonUtils::ValidateFields(renderer, {"mesh", "materials"}, source,
                                          pointer + "/components/meshRenderer", errors) && valid;
        if (!renderer.is_object() || !renderer.contains("mesh") || !renderer["mesh"].is_string() ||
            !renderer.contains("materials") || !renderer["materials"].is_array() || renderer["materials"].empty() ||
            !std::all_of(renderer["materials"].begin(), renderer["materials"].end(),
                         [](const Json &material) { return material.is_string(); }))
        {
            JsonUtils::AddError(errors, source, pointer + "/components/meshRenderer",
                                "Mesh renderer requires a mesh ID and a non-empty material ID array");
            valid = false;
        }
        else
        {
            MeshRendererDescription description;
            description.Mesh = renderer["mesh"].get<std::string>();
            description.Materials = renderer["materials"].get<std::vector<std::string>>();
            entity.MeshRenderer = std::move(description);
        }
    }
    if (components.contains("skinnedMeshRenderer"))
    {
        entity.SkinnedMeshRenderer.emplace();
        valid = ParseSkinnedMeshRenderer(components["skinnedMeshRenderer"], *entity.SkinnedMeshRenderer, source,
                                         pointer + "/components/skinnedMeshRenderer", errors) && valid;
    }
    if (components.contains("animator"))
    {
        entity.Animator.emplace();
        valid = ParseAnimator(components["animator"], *entity.Animator, source,
                              pointer + "/components/animator", errors) && valid;
    }
    if (components.contains("directionalLight"))
    {
        entity.DirectionalLight.emplace();
        valid = ParseColorIntensity(components["directionalLight"], entity.DirectionalLight->Color,
                                    entity.DirectionalLight->Intensity, {}, source,
                                    pointer + "/components/directionalLight", errors) && valid;
    }
    if (components.contains("pointLight"))
    {
        entity.PointLight.emplace();
        const Json &light = components["pointLight"];
        valid = ParseColorIntensity(light, entity.PointLight->Color, entity.PointLight->Intensity, {"range"}, source,
                                    pointer + "/components/pointLight", errors) && valid;
        if (light.contains("range") && light["range"].is_number())
            entity.PointLight->Range = light["range"].get<float>();
        else if (light.contains("range")) valid = false;
        if (!std::isfinite(entity.PointLight->Range) || entity.PointLight->Range <= 0.0f)
        {
            JsonUtils::AddError(errors, source, pointer + "/components/pointLight/range", "Range must be positive");
            valid = false;
        }
    }
    if (components.contains("spotLight"))
    {
        entity.SpotLight.emplace();
        const Json &light = components["spotLight"];
        valid = ParseColorIntensity(light, entity.SpotLight->Color, entity.SpotLight->Intensity,
                                    {"range", "innerAngleDegrees", "outerAngleDegrees"}, source,
                                    pointer + "/components/spotLight", errors) && valid;
        if (light.contains("range") && light["range"].is_number())
            entity.SpotLight->Range = light["range"].get<float>();
        else if (light.contains("range")) valid = false;
        if (light.contains("innerAngleDegrees") && light["innerAngleDegrees"].is_number())
            entity.SpotLight->InnerAngleDegrees = light["innerAngleDegrees"].get<float>();
        else if (light.contains("innerAngleDegrees")) valid = false;
        if (light.contains("outerAngleDegrees") && light["outerAngleDegrees"].is_number())
            entity.SpotLight->OuterAngleDegrees = light["outerAngleDegrees"].get<float>();
        else if (light.contains("outerAngleDegrees")) valid = false;
        if (!std::isfinite(entity.SpotLight->Range) || !std::isfinite(entity.SpotLight->InnerAngleDegrees) ||
            !std::isfinite(entity.SpotLight->OuterAngleDegrees) || entity.SpotLight->Range <= 0.0f ||
            entity.SpotLight->InnerAngleDegrees < 0.0f ||
            entity.SpotLight->OuterAngleDegrees <= entity.SpotLight->InnerAngleDegrees ||
            entity.SpotLight->OuterAngleDegrees > 90.0f)
        {
            JsonUtils::AddError(errors, source, pointer + "/components/spotLight", "Invalid spot light range or angles");
            valid = false;
        }
    }

    if (components.contains("parent"))
    {
        const auto &parent = components["parent"];
        entity.Parent.emplace();
        valid = JsonUtils::ValidateFields(parent, {"entity", "local"}, source, pointer+"/components/parent", errors) && valid;
        if (!parent.is_object() || !parent.contains("entity") || !parent["entity"].is_string() ||
            parent["entity"].get<std::string>().empty() || !parent.contains("local"))
        {
            JsonUtils::AddError(errors, source, pointer+"/components/parent", "Parent requires entity ID and local transform");
            valid = false;
        }
        else
        {
            entity.Parent->ParentId = parent["entity"].get<std::string>();
            valid = ParseTransform(parent["local"], entity.Parent->Local, source, pointer+"/components/parent/local", errors) && valid;
        }
        if (components.contains("rigidBody") || components.contains("characterController"))
        {
            JsonUtils::AddError(errors, source, pointer+"/components/parent", "Physics entities must be world roots");
            valid = false;
        }
        if (!entity.Transform) entity.Transform.emplace();
    }
    if (components.contains("locomotion"))
    {
        valid = ParseLocomotion(components["locomotion"], entity.Animator ? &*entity.Animator : nullptr,
                                entity.Locomotion.emplace(), source, pointer + "/components/locomotion", errors) &&
                valid;
    }
    if (components.contains("rigidBody"))
    {
        entity.RigidBody.emplace();
        valid = ParseRigidBody(components["rigidBody"], *entity.RigidBody, true, source,
                               pointer + "/components/rigidBody", errors) && valid;
    }
    if (components.contains("collider"))
    {
        entity.Collider.emplace();
        valid = ParseCollider(components["collider"], *entity.Collider, source, pointer + "/components/collider",
                              errors) && valid;
    }
    if (entity.RigidBody.has_value() != entity.Collider.has_value())
    {
        JsonUtils::AddError(errors, source, pointer + "/components",
                            "rigidBody and collider must be used together on the same entity");
        valid = false;
    }
    if (components.contains("characterController"))
    {
        entity.CharacterController.emplace();
        valid = ParseCharacterController(components["characterController"], *entity.CharacterController, source,
                                         pointer + "/components/characterController", errors) && valid;
    }
    if (components.contains("playerInput"))
    {
        entity.PlayerInput.emplace();
        valid = ParsePlayerInput(components["playerInput"], *entity.PlayerInput, source,
                                 pointer + "/components/playerInput", errors) && valid;
    }
    if (components.contains("script"))
    {
        entity.Script.emplace();
        valid = ParseScript(components["script"], *entity.Script, source, pointer + "/components/script", errors) &&
                valid;
    }
    if (entity.CharacterController && (entity.RigidBody || entity.Collider))
    {
        JsonUtils::AddError(errors, source, pointer + "/components/characterController",
                            "characterController cannot be combined with rigidBody or collider");
        valid = false;
    }
    if (entity.PlayerInput && !entity.CharacterController)
    {
        JsonUtils::AddError(errors, source, pointer + "/components/playerInput",
                            "playerInput requires a characterController on the same entity");
        valid = false;
    }

    if (entity.MeshRenderer && entity.SkinnedMeshRenderer)
    {
        JsonUtils::AddError(errors, source, pointer + "/components",
                            "An entity cannot have both mesh renderer component types");
        valid = false;
    }
    if (entity.Animator && !entity.SkinnedMeshRenderer)
    {
        JsonUtils::AddError(errors, source, pointer + "/components/animator",
                            "Animator requires a skinnedMeshRenderer on the same entity");
        valid = false;
    }
    if ((entity.Camera || entity.MeshRenderer || entity.SkinnedMeshRenderer || entity.DirectionalLight ||
         entity.PointLight || entity.SpotLight || entity.RigidBody || entity.CharacterController) &&
        !entity.Transform)
    {
        entity.Transform.emplace();
    }
    return valid;
}

bool ApplyComponentOverride(EntityDescription &entity, const Json &components, const std::filesystem::path &source,
                            const std::string &pointer, std::vector<SerializationError> &errors)
{
    if (!components.is_object())
    {
        JsonUtils::AddError(errors, source, pointer, "Expected component override object");
        return false;
    }
    bool valid = true;
    for (const auto &[name, value] : components.items())
    {
        if (name == "transform" && entity.Transform)
        {
            valid = ParseTransform(value, *entity.Transform, source, pointer + "/transform", errors) && valid;
        }
        else if (name == "meshRenderer" && entity.MeshRenderer)
        {
            valid = JsonUtils::ValidateFields(value, {"mesh", "materials"}, source,
                                              pointer + "/meshRenderer", errors) && valid;
            if (value.contains("mesh") && value["mesh"].is_string())
            {
                entity.MeshRenderer->Mesh = value["mesh"].get<std::string>();
            }
            if (value.contains("materials") && value["materials"].is_array() && !value["materials"].empty() &&
                std::all_of(value["materials"].begin(), value["materials"].end(),
                            [](const Json &material) { return material.is_string(); }))
            {
                entity.MeshRenderer->Materials = value["materials"].get<std::vector<std::string>>();
            }
            else if (value.contains("materials"))
            {
                JsonUtils::AddError(errors, source, pointer + "/meshRenderer/materials",
                                    "Expected a non-empty material ID array");
                valid = false;
            }
        }
        else if (name == "skinnedMeshRenderer" && entity.SkinnedMeshRenderer)
        {
            valid = JsonUtils::ValidateFields(value, {"mesh", "skeleton", "materials"}, source,
                                               pointer + "/skinnedMeshRenderer", errors) && valid;
            if (!value.is_object())
            {
                valid = false;
                continue;
            }
            if (value.contains("mesh") && value["mesh"].is_string())
                entity.SkinnedMeshRenderer->Mesh = value["mesh"].get<std::string>();
            else if (value.contains("mesh")) valid = false;
            if (value.contains("skeleton") && value["skeleton"].is_string())
                entity.SkinnedMeshRenderer->Skeleton = value["skeleton"].get<std::string>();
            else if (value.contains("skeleton")) valid = false;
            if (value.contains("materials") && value["materials"].is_array() && !value["materials"].empty() &&
                std::all_of(value["materials"].begin(), value["materials"].end(),
                            [](const Json &material) { return material.is_string(); }))
                entity.SkinnedMeshRenderer->Materials = value["materials"].get<std::vector<std::string>>();
            else if (value.contains("materials"))
            {
                JsonUtils::AddError(errors, source, pointer + "/skinnedMeshRenderer/materials",
                                    "Expected a non-empty material ID array");
                valid = false;
            }
        }
        else if (name == "animator" && entity.Animator)
        {
            valid = JsonUtils::ValidateFields(
                        value, {"clips", "currentClip", "playbackSpeed", "playing", "loop", "defaultFade"}, source,
                        pointer + "/animator", errors) && valid;
            if (!value.is_object())
            {
                valid = false;
                continue;
            }
            if (value.contains("clips"))
            {
                Json replacement = value;
                if (!replacement.contains("currentClip")) replacement["currentClip"] = entity.Animator->CurrentClip;
                AnimatorDescription parsed = *entity.Animator;
                parsed.Clips.clear();
                valid = ParseAnimator(replacement, parsed, source, pointer + "/animator", errors) && valid;
                entity.Animator = std::move(parsed);
            }
            else
            {
                if (value.contains("currentClip") && value["currentClip"].is_string())
                    entity.Animator->CurrentClip = value["currentClip"].get<std::string>();
                else if (value.contains("currentClip")) valid = false;
                if (value.contains("playbackSpeed") && value["playbackSpeed"].is_number() &&
                    std::isfinite(value["playbackSpeed"].get<float>()))
                    entity.Animator->PlaybackSpeed = value["playbackSpeed"].get<float>();
                else if (value.contains("playbackSpeed")) valid = false;
                if (value.contains("playing") && value["playing"].is_boolean())
                    entity.Animator->Playing = value["playing"].get<bool>();
                else if (value.contains("playing")) valid = false;
                if (value.contains("loop") && value["loop"].is_boolean())
                    entity.Animator->Loop = value["loop"].get<bool>();
                else if (value.contains("loop")) valid = false;
                float fade = entity.Animator->DefaultFade;
                if (ReadFiniteFloat(value, "defaultFade", fade, source, pointer + "/animator", errors) && fade >= 0.0f)
                    entity.Animator->DefaultFade = fade;
                else
                {
                    if (fade < 0.0f)
                        JsonUtils::AddError(errors, source, pointer + "/animator/defaultFade",
                                            "Expected a fade time >= 0 seconds");
                    valid = false;
                }
                if (!entity.Animator->Clips.contains(entity.Animator->CurrentClip))
                {
                    JsonUtils::AddError(errors, source, pointer + "/animator/currentClip",
                                        "Current clip is not present in clips");
                    valid = false;
                }
            }
        }
        else if (name == "camera" && entity.Camera)
        {
            valid = ParseCamera(value, *entity.Camera, source, pointer + "/camera", errors) && valid;
        }
        else if (name == "directionalLight" && entity.DirectionalLight)
        {
            valid = ParseColorIntensity(value, entity.DirectionalLight->Color, entity.DirectionalLight->Intensity,
                                        {}, source, pointer + "/directionalLight", errors) && valid;
        }
        else if (name == "pointLight" && entity.PointLight)
        {
            valid = ParseColorIntensity(value, entity.PointLight->Color, entity.PointLight->Intensity,
                                        {"range"}, source, pointer + "/pointLight", errors) && valid;
            if (value.contains("range") && value["range"].is_number())
                entity.PointLight->Range = value["range"].get<float>();
            else if (value.contains("range")) valid = false;
            if (!std::isfinite(entity.PointLight->Range) || entity.PointLight->Range <= 0.0f)
            {
                JsonUtils::AddError(errors, source, pointer + "/pointLight/range", "Range must be positive and finite");
                valid = false;
            }
        }
        else if (name == "spotLight" && entity.SpotLight)
        {
            valid = ParseColorIntensity(value, entity.SpotLight->Color, entity.SpotLight->Intensity,
                                        {"range", "innerAngleDegrees", "outerAngleDegrees"}, source,
                                        pointer + "/spotLight", errors) && valid;
            if (value.contains("range") && value["range"].is_number())
                entity.SpotLight->Range = value["range"].get<float>();
            else if (value.contains("range")) valid = false;
            if (value.contains("innerAngleDegrees") && value["innerAngleDegrees"].is_number())
                entity.SpotLight->InnerAngleDegrees = value["innerAngleDegrees"].get<float>();
            else if (value.contains("innerAngleDegrees")) valid = false;
            if (value.contains("outerAngleDegrees") && value["outerAngleDegrees"].is_number())
                entity.SpotLight->OuterAngleDegrees = value["outerAngleDegrees"].get<float>();
            else if (value.contains("outerAngleDegrees")) valid = false;
            if (!std::isfinite(entity.SpotLight->Range) || !std::isfinite(entity.SpotLight->InnerAngleDegrees) ||
                !std::isfinite(entity.SpotLight->OuterAngleDegrees) || entity.SpotLight->Range <= 0.0f ||
                entity.SpotLight->InnerAngleDegrees < 0.0f ||
                entity.SpotLight->OuterAngleDegrees <= entity.SpotLight->InnerAngleDegrees ||
                entity.SpotLight->OuterAngleDegrees > 90.0f)
            {
                JsonUtils::AddError(errors, source, pointer + "/spotLight", "Invalid spot light range or angles");
                valid = false;
            }
        }
        else if (name == "rigidBody" && entity.RigidBody)
        {
            valid = ParseRigidBody(value, *entity.RigidBody, false, source, pointer + "/rigidBody", errors) && valid;
        }
        else if (name == "collider" && entity.Collider)
        {
            valid = ParseCollider(value, *entity.Collider, source, pointer + "/collider", errors) && valid;
        }
        else if (name == "characterController" && entity.CharacterController)
        {
            valid = ParseCharacterController(value, *entity.CharacterController, source,
                                             pointer + "/characterController", errors) && valid;
        }
        else if (name == "playerInput" && entity.PlayerInput)
        {
            valid = ParsePlayerInput(value, *entity.PlayerInput, source, pointer + "/playerInput", errors) && valid;
        }
        else if (name == "script" && entity.Script)
        {
            valid = ParseScript(value, *entity.Script, source, pointer + "/script", errors) && valid;
        }
        else
        {
            JsonUtils::AddError(errors, source, pointer + "/" + name,
                                "Override targets a missing or unsupported component");
            valid = false;
        }
    }
    return valid;
}

bool ExpandPrefab(const std::filesystem::path &source, const std::string &instanceNamespace,
                  const TransformComponent &instanceTransform, const Json &overrides,
                  std::vector<std::filesystem::path> &stack, std::vector<EntityDescription> &entities,
                  std::vector<SerializationError> &errors);

bool ExpandPrefabInstances(const Json &instances, const std::filesystem::path &owner,
                           const std::string &parentNamespace, const TransformComponent &parentTransform,
                           std::vector<std::filesystem::path> &stack, std::vector<EntityDescription> &entities,
                           std::vector<SerializationError> &errors)
{
    if (!instances.is_array())
    {
        JsonUtils::AddError(errors, owner, "/prefabs", "Expected an array");
        return false;
    }
    bool valid = true;
    for (std::size_t index = 0; index < instances.size(); ++index)
    {
        const Json &instance = instances[index];
        const std::string pointer = "/prefabs/" + std::to_string(index);
        valid = JsonUtils::ValidateFields(instance, {"source", "instance", "transform", "overrides"},
                                          owner, pointer, errors) && valid;
        if (!instance.is_object() || !instance.contains("source") || !instance["source"].is_string() ||
            !instance.contains("instance") || !instance["instance"].is_string())
        {
            JsonUtils::AddError(errors, owner, pointer, "Prefab instance requires string source and instance fields");
            valid = false;
            continue;
        }
        TransformComponent local;
        if (instance.contains("transform"))
        {
            valid = ParseTransform(instance["transform"], local, owner, pointer + "/transform", errors) && valid;
        }
        const TransformComponent combined = ComposeTransform(parentTransform, local);
        const std::string localName = instance["instance"].get<std::string>();
        const std::string nameSpace = parentNamespace.empty() ? localName : parentNamespace + "/" + localName;
        const Json emptyOverrides = Json::object();
        const Json &instanceOverrides = instance.contains("overrides") ? instance["overrides"] : emptyOverrides;
        const std::filesystem::path prefabSource =
            (owner.parent_path() / instance["source"].get<std::string>()).lexically_normal();
        valid = ExpandPrefab(prefabSource, nameSpace, combined, instanceOverrides, stack, entities, errors) && valid;
    }
    return valid;
}

bool ExpandPrefab(const std::filesystem::path &source, const std::string &instanceNamespace,
                  const TransformComponent &instanceTransform, const Json &overrides,
                  std::vector<std::filesystem::path> &stack, std::vector<EntityDescription> &entities,
                  std::vector<SerializationError> &errors)
{
    if (std::find(stack.begin(), stack.end(), source) != stack.end())
    {
        JsonUtils::AddError(errors, source, "", "Prefab cycle detected");
        return false;
    }
    stack.push_back(source);

    Json document;
    bool valid = JsonUtils::ReadFile(source, document, errors);
    if (valid)
    {
        valid = JsonUtils::ValidateFields(document, {"type", "version", "entities", "prefabs"}, source, "", errors) && valid;
        valid = JsonUtils::ValidateHeader(document, "haki.prefab", source, errors) && valid;
    }

    const std::size_t firstEntity = entities.size();
    if (valid && document.contains("entities"))
    {
        if (!document["entities"].is_array())
        {
            JsonUtils::AddError(errors, source, "/entities", "Expected an array");
            valid = false;
        }
        else
        {
            for (std::size_t index = 0; index < document["entities"].size(); ++index)
            {
                EntityDescription entity;
                if (ParseEntity(document["entities"][index], entity, source,
                                "/entities/" + std::to_string(index), errors))
                {
                    entity.Id = instanceNamespace + "/" + entity.Id;
                    if (entity.Parent) entity.Parent->ParentId = instanceNamespace + "/" + entity.Parent->ParentId;
                    if (!entity.Transform)
                    {
                        entity.Transform.emplace();
                    }
                    *entity.Transform = ComposeTransform(instanceTransform, *entity.Transform);
                    entities.push_back(std::move(entity));
                }
                else
                {
                    valid = false;
                }
            }
        }
    }
    if (valid && document.contains("prefabs"))
    {
        valid = ExpandPrefabInstances(document["prefabs"], source, instanceNamespace, instanceTransform,
                                      stack, entities, errors) && valid;
    }

    if (!overrides.is_object())
    {
        JsonUtils::AddError(errors, source, "/overrides", "Expected an object");
        valid = false;
    }
    else
    {
        for (const auto &[target, componentOverrides] : overrides.items())
        {
            const std::string expandedId = instanceNamespace + "/" + target;
            const auto entity = std::find_if(entities.begin() + static_cast<std::ptrdiff_t>(firstEntity), entities.end(),
                                             [&](const EntityDescription &candidate) { return candidate.Id == expandedId; });
            if (entity == entities.end())
            {
                JsonUtils::AddError(errors, source, "/overrides/" + target, "Override target does not exist");
                valid = false;
            }
            else
            {
                valid = ApplyComponentOverride(*entity, componentOverrides, source,
                                               "/overrides/" + target, errors) && valid;
            }
        }
    }

    stack.pop_back();
    return valid;
}

const AssetCatalogEntry *RequireAsset(const AssetCatalog &catalog, std::string_view id, CatalogAssetType type,
                                      const std::filesystem::path &source, const std::string &pointer,
                                      std::vector<SerializationError> &errors)
{
    const AssetCatalogEntry *entry = catalog.Find(id);
    if (!entry)
    {
        JsonUtils::AddError(errors, source, pointer, "Unknown asset ID '" + std::string(id) + "'");
        return nullptr;
    }
    if (entry->Type != type)
    {
        JsonUtils::AddError(errors, source, pointer, "Asset has the wrong type");
        return nullptr;
    }
    return entry;
}

} // namespace

bool SceneSerializer::LoadManifest(const std::filesystem::path &source, SceneManifest &manifest,
                                   std::vector<SerializationError> &errors)
{
    manifest = {};
    Json document;
    if (!JsonUtils::ReadFile(source, document, errors))
    {
        return false;
    }
    bool valid = JsonUtils::ValidateFields(document, {"type", "version", "assetCatalog", "entry", "scenes", "window", "rendering", "editor"},
                                           source, "", errors);
    valid = JsonUtils::ValidateHeader(document, "haki.scene-manifest", source, errors) && valid;
    if (!document.contains("assetCatalog") || !document["assetCatalog"].is_string() ||
        !document.contains("entry") || !document["entry"].is_string() ||
        !document.contains("scenes") || !document["scenes"].is_array())
    {
        JsonUtils::AddError(errors, source, "", "Manifest requires assetCatalog, entry, and scenes");
        return false;
    }

    manifest.AssetCatalog = (source.parent_path() / document["assetCatalog"].get<std::string>()).lexically_normal();
    manifest.EntryScene = document["entry"].get<std::string>();
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < document["scenes"].size(); ++index)
    {
        const Json &scene = document["scenes"][index];
        const std::string pointer = "/scenes/" + std::to_string(index);
        valid = JsonUtils::ValidateFields(scene, {"id", "source"}, source, pointer, errors) && valid;
        if (!scene.is_object() || !scene.contains("id") || !scene["id"].is_string() ||
            !scene.contains("source") || !scene["source"].is_string())
        {
            JsonUtils::AddError(errors, source, pointer, "Scene requires string id and source");
            valid = false;
            continue;
        }
        const std::string id = scene["id"].get<std::string>();
        if (!ids.insert(id).second)
        {
            JsonUtils::AddError(errors, source, pointer + "/id", "Duplicate scene ID");
            valid = false;
            continue;
        }
        manifest.Scenes.push_back({id, (source.parent_path() / scene["source"].get<std::string>()).lexically_normal()});
    }
    if (!ids.contains(manifest.EntryScene))
    {
        JsonUtils::AddError(errors, source, "/entry", "Entry scene is not registered");
        valid = false;
    }
    if (valid)
    {
        valid = catalog_->Load(manifest.AssetCatalog, errors);
    }
    return valid;
}

SceneLoadResult SceneSerializer::Deserialize(const std::filesystem::path &source) const
{
    SceneLoadResult result;
    Json document;
    if (!JsonUtils::ReadFile(source, document, result.Errors))
    {
        return result;
    }
    struct CatalogRollback {
        AssetCatalog &target; AssetCatalog old; bool committed = false;
        ~CatalogRollback() { if (!committed) target = std::move(old); }
    } rollback{*catalog_, *catalog_};
    if (document.contains("assetCatalog")) {
        if (!document["assetCatalog"].is_string()) {
            JsonUtils::AddError(result.Errors,source,"/assetCatalog","Expected catalog path"); return result;
        }
        AssetCatalog next;
        if (!next.Load(source.parent_path()/document["assetCatalog"].get<std::string>(),result.Errors)) return result;
        *catalog_ = std::move(next);
    }
    bool valid = JsonUtils::ValidateFields(document,
                                            {"type", "version", "id", "environment", "entities", "prefabs", "script", "assetCatalog"},
                                            source, "", result.Errors);
    valid = JsonUtils::ValidateHeader(document, "haki.scene", source, result.Errors) && valid;
    if (!document.contains("id") || !document["id"].is_string())
    {
        JsonUtils::AddError(result.Errors, source, "/id", "Scene requires a string ID");
        valid = false;
    }

    SceneDescription description;
    description.Id = document.value("id", std::string{});
    if (document.contains("environment"))
    {
        description.Environment.emplace();
        valid = ParseEnvironment(document["environment"], *description.Environment, source,
                                 "/environment", result.Errors) && valid;
    }
    bool sceneScriptPathValid = false;
    if (document.contains("script"))
    {
        if (document["script"].is_string())
        {
            description.Script = document["script"].get<std::string>();
            sceneScriptPathValid = ValidateScriptPath(description.Script, source, "/script", result.Errors);
            valid = sceneScriptPathValid && valid;
        }
        else
        {
            JsonUtils::AddError(result.Errors, source, "/script", "Expected a script path string");
            valid = false;
        }
    }
    if (document.contains("entities"))
    {
        if (!document["entities"].is_array())
        {
            JsonUtils::AddError(result.Errors, source, "/entities", "Expected an array");
            valid = false;
        }
        else
        {
            for (std::size_t index = 0; index < document["entities"].size(); ++index)
            {
                EntityDescription entity;
                valid = ParseEntity(document["entities"][index], entity, source,
                                    "/entities/" + std::to_string(index), result.Errors) && valid;
                description.Entities.push_back(std::move(entity));
            }
        }
    }
    if (document.contains("prefabs"))
    {
        std::vector<std::filesystem::path> stack;
        valid = ExpandPrefabInstances(document["prefabs"], source, {}, TransformComponent{}, stack,
                                      description.Entities, result.Errors) && valid;
    }

    std::unordered_map<std::string, const EntityDescription *> byId;
    for (const auto &entity : description.Entities) byId.emplace(entity.Id, &entity);
    for (const auto &entity : description.Entities)
    {
        std::unordered_set<std::string> chain;
        const EntityDescription *current = &entity;
        while (current->Parent)
        {
            if (!chain.insert(current->Id).second || !byId.contains(current->Parent->ParentId))
            {
                JsonUtils::AddError(result.Errors, source, "/entities/"+entity.Id+"/components/parent", "Missing parent or parent cycle");
                valid = false;
                break;
            }
            current = byId.at(current->Parent->ParentId);
            if (!current->Transform)
            {
                JsonUtils::AddError(result.Errors, source, "/entities/"+entity.Id+"/components/parent", "Parent requires a transform");
                valid = false;
                break;
            }
        }
        if (entity.Locomotion && !current->CharacterController)
        {
            JsonUtils::AddError(result.Errors, source, "/entities/"+entity.Id+"/components/locomotion", "Locomotion requires a character controller ancestor");
            valid = false;
        }
    }
    const auto sceneScriptRoot = document.contains("assetCatalog") ? source.parent_path()/"Scripts" : scriptRoot_;
    // Script files must exist, so a typo fails at load instead of silently doing nothing in game.
    if (sceneScriptPathValid && !std::filesystem::is_regular_file(ScriptSourcePath(sceneScriptRoot, description.Script)))
    {
        JsonUtils::AddError(result.Errors, source, "/script",
                            "Script '" + (ScriptSourcePath(sceneScriptRoot, description.Script)).generic_string() + "' does not exist");
        valid = false;
    }
    for (const EntityDescription &entity : description.Entities)
    {
        if (entity.Script && !entity.Script->Path.empty() &&
            !std::filesystem::is_regular_file(ScriptSourcePath(sceneScriptRoot, entity.Script->Path)))
        {
            JsonUtils::AddError(result.Errors, source, "/entities/" + entity.Id + "/components/script/path",
                                "Script '" + (ScriptSourcePath(sceneScriptRoot, entity.Script->Path)).generic_string() +
                                    "' does not exist");
            valid = false;
        }
    }

    std::unordered_set<std::string> entityIds;
    std::size_t primaryCameras = 0;
    for (const EntityDescription &entity : description.Entities)
    {
        if (!entityIds.insert(entity.Id).second)
        {
            JsonUtils::AddError(result.Errors, source, "/entities", "Duplicate expanded entity ID '" + entity.Id + "'");
            valid = false;
        }
        primaryCameras += entity.Camera && entity.Camera->Primary ? 1 : 0;
    }
    if (primaryCameras != 1)
    {
        JsonUtils::AddError(result.Errors, source, "/entities", "Scene must contain exactly one primary camera");
        valid = false;
    }
    if (!valid)
    {
        return result;
    }

    auto scene = std::make_unique<Scene>();
    if (description.Environment)
    {
        const auto *textureEntry = RequireAsset(*catalog_, description.Environment->Texture,
                                                CatalogAssetType::Texture, source,
                                                "/environment/texture", result.Errors);
        if (textureEntry)
        {
            const auto texture = AssetManager::LoadTexture({textureEntry->RuntimeId, textureEntry->Source});
            if (!texture)
            {
                JsonUtils::AddError(result.Errors, source, "/environment/texture", texture.Message);
            }
            else
            {
                scene->SetEnvironment({texture.Value, description.Environment->Intensity,
                                       description.Environment->RotationDegrees});
            }
        }
    }
    for (const EntityDescription &entityDescription : description.Entities)
    {
        const entt::entity entity = scene->GetRegistry().create();
        scene->GetRegistry().emplace<EntityIdComponent>(entity, entityDescription.Id);
        scene->GetRegistry().emplace<NameComponent>(entity, entityDescription.Name);
        if (entityDescription.Transform)
        {
            scene->GetRegistry().emplace<TransformComponent>(entity, *entityDescription.Transform);
        }
        if (entityDescription.Camera)
        {
            scene->GetRegistry().emplace<CameraComponent>(entity, *entityDescription.Camera);
        }
        if (entityDescription.MeshRenderer)
        {
            const auto *meshEntry = RequireAsset(*catalog_, entityDescription.MeshRenderer->Mesh,
                                                 CatalogAssetType::Mesh, source, "/entities", result.Errors);
            if (meshEntry)
            {
                const auto mesh = AssetManager::LoadMesh({meshEntry->RuntimeId, meshEntry->Source});
                if (!mesh)
                {
                    JsonUtils::AddError(result.Errors, source, "/entities", mesh.Message);
                }
                std::vector<MaterialHandle> materials;
                for (const std::string &materialId : entityDescription.MeshRenderer->Materials)
                {
                    const auto *materialEntry = RequireAsset(*catalog_, materialId, CatalogAssetType::Material,
                                                             source, "/entities", result.Errors);
                    if (!materialEntry)
                    {
                        continue;
                    }
                    const auto material = AssetManager::LoadMaterial({materialEntry->RuntimeId, materialEntry->Source});
                    if (!material)
                    {
                        JsonUtils::AddError(result.Errors, source, "/entities", material.Message);
                    }
                    else
                    {
                        materials.push_back(material.Value);
                    }
                }
                const Mesh *meshResource = mesh ? AssetManager::GetMesh(mesh.Value) : nullptr;
                if (meshResource && materials.size() != 1 && materials.size() != meshResource->GetSubmeshCount())
                {
                    JsonUtils::AddError(result.Errors, source, "/entities",
                                        "Material slot count must be one or match the mesh submesh count");
                }
                else if (meshResource && materials.size() == entityDescription.MeshRenderer->Materials.size())
                {
                    scene->GetRegistry().emplace<MeshRendererComponent>(entity, mesh.Value, std::move(materials));
                }
            }
        }
        if (entityDescription.SkinnedMeshRenderer)
        {
            const std::string componentPointer = "/entities/" + entityDescription.Id + "/components/skinnedMeshRenderer";
            const auto *meshEntry = RequireAsset(*catalog_, entityDescription.SkinnedMeshRenderer->Mesh,
                                                 CatalogAssetType::Mesh, source, componentPointer + "/mesh",
                                                 result.Errors);
            const auto *skeletonEntry = RequireAsset(*catalog_, entityDescription.SkinnedMeshRenderer->Skeleton,
                                                     CatalogAssetType::Skeleton, source,
                                                     componentPointer + "/skeleton", result.Errors);
            AssetResult<MeshHandle> mesh;
            AssetResult<SkeletonHandle> skeleton;
            if (meshEntry)
            {
                mesh = AssetManager::LoadMesh({meshEntry->RuntimeId, meshEntry->Source});
                if (!mesh) JsonUtils::AddError(result.Errors, source, componentPointer + "/mesh", mesh.Message);
            }
            if (skeletonEntry)
            {
                skeleton = AssetManager::LoadSkeleton({skeletonEntry->RuntimeId, skeletonEntry->Source});
                if (!skeleton)
                    JsonUtils::AddError(result.Errors, source, componentPointer + "/skeleton", skeleton.Message);
            }

            std::vector<MaterialHandle> materials;
            for (std::size_t index = 0; index < entityDescription.SkinnedMeshRenderer->Materials.size(); ++index)
            {
                const std::string &materialId = entityDescription.SkinnedMeshRenderer->Materials[index];
                const auto *materialEntry = RequireAsset(*catalog_, materialId, CatalogAssetType::Material, source,
                                                         componentPointer + "/materials/" + std::to_string(index),
                                                         result.Errors);
                if (!materialEntry) continue;
                const auto material = AssetManager::LoadMaterial({materialEntry->RuntimeId, materialEntry->Source});
                if (!material)
                    JsonUtils::AddError(result.Errors, source,
                                        componentPointer + "/materials/" + std::to_string(index), material.Message);
                else materials.push_back(material.Value);
            }
            const Mesh *meshResource = mesh ? AssetManager::GetMesh(mesh.Value) : nullptr;
            const Skeleton *skeletonResource = skeleton ? AssetManager::GetSkeleton(skeleton.Value) : nullptr;
            if (meshResource && !meshResource->IsSkinned())
            {
                JsonUtils::AddError(result.Errors, source, componentPointer + "/mesh",
                                    "Skinned mesh renderer requires JOINTS_0 and WEIGHTS_0 attributes");
            }
            else if (skeletonResource && skeletonResource->Joints.size() > MaxSkinJoints)
            {
                JsonUtils::AddError(result.Errors, source, componentPointer + "/skeleton",
                                    "Skeleton exceeds the renderer joint palette limit");
            }
            else if (meshResource && skeletonResource &&
                     meshResource->GetMaximumJointIndex() >= skeletonResource->Joints.size())
            {
                JsonUtils::AddError(result.Errors, source, componentPointer + "/skeleton",
                                    "Mesh joint indices exceed the skeleton palette");
            }
            else if (meshResource && materials.size() != 1 && materials.size() != meshResource->GetSubmeshCount())
            {
                JsonUtils::AddError(result.Errors, source, componentPointer + "/materials",
                                    "Material slot count must be one or match the mesh submesh count");
            }
            else if (mesh && skeleton && materials.size() == entityDescription.SkinnedMeshRenderer->Materials.size())
            {
                scene->GetRegistry().emplace<SkinnedMeshRendererComponent>(
                    entity, mesh.Value, skeleton.Value, std::move(materials));
            }
        }
        if (entityDescription.Animator)
        {
            const std::string componentPointer = "/entities/" + entityDescription.Id + "/components/animator";
            AnimatorComponent animator;
            animator.CurrentClipName = entityDescription.Animator->CurrentClip;
            animator.PlaybackSpeed = entityDescription.Animator->PlaybackSpeed;
            animator.IsPlaying = entityDescription.Animator->Playing;
            animator.Loop = entityDescription.Animator->Loop;
            animator.DefaultFade = entityDescription.Animator->DefaultFade;
            AssetID expectedSkeletonId = InvalidAssetID;
            if (entityDescription.SkinnedMeshRenderer)
            {
                if (const auto *entry = catalog_->Find(entityDescription.SkinnedMeshRenderer->Skeleton))
                    expectedSkeletonId = entry->RuntimeId;
            }
            for (const auto &[name, clipId] : entityDescription.Animator->Clips)
            {
                const auto *clipEntry = RequireAsset(*catalog_, clipId, CatalogAssetType::AnimationClip, source,
                                                     componentPointer + "/clips/" + name, result.Errors);
                if (!clipEntry) continue;
                const auto clip = AssetManager::LoadAnimationClip({clipEntry->RuntimeId, clipEntry->Source});
                if (!clip)
                {
                    JsonUtils::AddError(result.Errors, source, componentPointer + "/clips/" + name, clip.Message);
                    continue;
                }
                const AnimationClip *resource = AssetManager::GetAnimationClip(clip.Value);
                if (!resource || resource->SkeletonId != expectedSkeletonId)
                {
                    JsonUtils::AddError(result.Errors, source, componentPointer + "/clips/" + name,
                                        resource ? "Animation clip targets a different skeleton"
                                                 : "Loaded animation clip is unavailable");
                    continue;
                }
                animator.Clips.emplace(name, clip.Value);
            }
            if (animator.Clips.size() == entityDescription.Animator->Clips.size())
                scene->GetRegistry().emplace<AnimatorComponent>(entity, std::move(animator));
        }
        if (entityDescription.DirectionalLight)
        {
            scene->GetRegistry().emplace<DirectionalLightComponent>(entity, *entityDescription.DirectionalLight);
        }
        if (entityDescription.PointLight)
        {
            scene->GetRegistry().emplace<PointLightComponent>(entity, *entityDescription.PointLight);
        }
        if (entityDescription.SpotLight)
        {
            scene->GetRegistry().emplace<SpotLightComponent>(entity, *entityDescription.SpotLight);
        }
        if (entityDescription.RigidBody && entityDescription.Collider)
        {
            scene->GetRegistry().emplace<RigidBodyComponent>(entity, *entityDescription.RigidBody);
            scene->GetRegistry().emplace<ColliderComponent>(entity, *entityDescription.Collider);
        }
        if (entityDescription.CharacterController)
        {
            scene->GetRegistry().emplace<CharacterControllerComponent>(entity, *entityDescription.CharacterController);
        }
        if (entityDescription.Parent)
            scene->GetRegistry().emplace<ParentTransformComponent>(entity, *entityDescription.Parent);
        if (entityDescription.Locomotion)
            scene->GetRegistry().emplace<LocomotionAnimationComponent>(entity, *entityDescription.Locomotion);
        if (entityDescription.PlayerInput)
        {
            scene->GetRegistry().emplace<PlayerInputComponent>(entity, *entityDescription.PlayerInput);
        }
        if (entityDescription.Script)
        {
            scene->GetRegistry().emplace<ScriptComponent>(entity, *entityDescription.Script);
        }
    }
    scene->SetScripting(sceneScriptRoot, description.Script);
    if (!result.Errors.empty())
    {
        return result;
    }
    scene->UpdateRuntime(0.0f);
    rollback.committed = true;
    result.Value = std::move(scene);
    return result;
}

} // namespace Haki
