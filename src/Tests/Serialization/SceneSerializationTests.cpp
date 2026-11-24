#include "Haki/AssetManager/AssetCatalog.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/AssetLoader/AssetLoader.h"
#include "Haki/GL/Texture.h"
#include "Haki/Physics/PhysicsCore.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scene/SceneManager.h"
#include "Haki/Serialization/SceneSerializer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{

constexpr std::string_view FixtureRoot = "src/Tests/Serialization/Fixtures";

class TestRunner
{
public:
    explicit TestRunner(std::string_view name) { std::cout << "\n=== " << name << " ===\n"; }
    void Check(bool condition, std::string_view name)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
        failures_ += condition ? 0 : 1;
    }
    int Result() const { return failures_ == 0 ? 0 : 1; }

private:
    int failures_ = 0;
};

class FakeTextureLoader final : public Haki::AssetLoader<Haki::Texture>
{
public:
    bool Supports(const Haki::AssetLoadRequest &) const override { return true; }
    Haki::AssetLoaderResult<Haki::Texture> Load(const Haki::AssetLoadRequest &) override
    {
        return {std::make_unique<Haki::Texture>(), Haki::AssetLoadError::None, {}};
    }
};

int RunCatalogTests()
{
    TestRunner tests("Serialization.Catalog");
    Haki::AssetCatalog catalog;
    std::vector<Haki::SerializationError> errors;
    tests.Check(catalog.Load(std::string(FixtureRoot) + "/AssetCatalog.json", errors), "valid catalog loads");
    tests.Check(errors.empty(), "valid catalog has no diagnostics");
    const auto *mesh = catalog.Find("mesh.crate");
    tests.Check(mesh && mesh->Type == Haki::CatalogAssetType::Mesh, "logical mesh ID resolves");
    tests.Check(mesh && mesh->MeshSelector == "Crate", "named glTF mesh selector is retained");
    tests.Check(mesh && catalog.Find(mesh->RuntimeId) == mesh, "runtime ID resolves to the same catalog entry");
    tests.Check(Haki::AssetCatalog::MakeRuntimeId("mesh.crate") == Haki::AssetCatalog::MakeRuntimeId("mesh.crate"),
                "runtime ID generation is deterministic");
    return tests.Result();
}

int RunSceneTests()
{
    TestRunner tests("Serialization.Scene");
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    Haki::SceneSerializer serializer(catalog);
    Haki::SceneManifest manifest;
    std::vector<Haki::SerializationError> errors;
    tests.Check(serializer.LoadManifest(std::string(FixtureRoot) + "/SceneManifest.json", manifest, errors),
                "manifest and referenced catalog load");
    tests.Check(manifest.EntryScene == "test" && manifest.Scenes.size() == 1, "entry scene is deterministic");

    Haki::SceneLoadResult loaded = serializer.Deserialize(manifest.Scenes.front().Source);
    tests.Check(static_cast<bool>(loaded), "scene and prefab deserialize");
    tests.Check(loaded.Value && loaded.Value->GetRegistry().storage<Haki::EntityIdComponent>().size() == 2,
                "scene owns expanded registry entities");
    tests.Check(loaded.Value && !loaded.Value->GetEnvironment().Texture.IsValid() &&
                    loaded.Value->GetEnvironment().Intensity == 1.0f &&
                    loaded.Value->GetEnvironment().RotationDegrees == 0.0f,
                "scene without environment retains defaults");

    bool foundPrefabEntity = false;
    if (loaded.Value)
    {
        auto view = loaded.Value->GetRegistry().view<Haki::EntityIdComponent, Haki::TransformComponent,
                                                     Haki::PointLightComponent>();
        for (const entt::entity entity : view)
        {
            if (view.get<Haki::EntityIdComponent>(entity).Id == "light-01/root")
            {
                foundPrefabEntity = view.get<Haki::TransformComponent>(entity).Translation.x == 4.0f &&
                                    view.get<Haki::PointLightComponent>(entity).Range == 8.0f;
            }
        }
    }
    tests.Check(foundPrefabEntity, "prefab IDs are namespaced and overrides are applied");

    Haki::AssetManager::Shutdown();
    Haki::AssetManager::Initialize();
    Haki::AssetManager::RegisterTextureLoader(std::make_unique<FakeTextureLoader>());
    auto environment = serializer.Deserialize(std::string(FixtureRoot) + "/Environment.scene.json");
    tests.Check(static_cast<bool>(environment), "scene environment deserializes");
    tests.Check(environment.Value && environment.Value->GetEnvironment().Texture.IsValid() &&
                    environment.Value->GetEnvironment().Intensity == 2.5f &&
                    environment.Value->GetEnvironment().RotationDegrees == -45.0f,
                "scene stores loaded environment texture and settings");
    auto environmentDefaults = serializer.Deserialize(std::string(FixtureRoot) + "/EnvironmentDefaults.scene.json");
    tests.Check(environmentDefaults.Value && environmentDefaults.Value->GetEnvironment().Texture.IsValid() &&
                    environmentDefaults.Value->GetEnvironment().Intensity == 1.0f &&
                    environmentDefaults.Value->GetEnvironment().RotationDegrees == 0.0f,
                "environment intensity and rotation use defaults when omitted");
    auto engineDefault = serializer.Deserialize(std::string(FixtureRoot) + "/DefaultEnvironment.scene.json");
    tests.Check(engineDefault && engineDefault.Value->GetEnvironment().Texture.IsValid() &&
                    engineDefault.Value->GetEnvironment().Intensity == 1.0f &&
                    engineDefault.Value->GetEnvironment().RotationDegrees == 0.0f,
                "omitted environment uses engine HDR for skybox and IBL");
    auto disabledEnvironment = serializer.Deserialize(std::string(FixtureRoot) + "/DisabledEnvironment.scene.json");
    tests.Check(disabledEnvironment && !disabledEnvironment.Value->GetEnvironment().Texture.IsValid(),
                "explicit null opts out even when the engine environment is available");
    auto customEnvironment = serializer.Deserialize(std::string(FixtureRoot) + "/Environment.scene.json");
    tests.Check(customEnvironment && engineDefault && customEnvironment.Value->GetEnvironment().Texture.IsValid() &&
                    customEnvironment.Value->GetEnvironment().Texture != engineDefault.Value->GetEnvironment().Texture &&
                    customEnvironment.Value->GetEnvironment().Intensity == 2.5f,
                "custom environment overrides the available engine default");
    Haki::AssetManager::Shutdown();
    return tests.Result();
}

int RunErrorTests()
{
    TestRunner tests("Serialization.Errors");
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    Haki::SceneSerializer serializer(catalog);

    auto invalid = serializer.Deserialize(std::string(FixtureRoot) + "/Invalid.scene.json");
    tests.Check(!invalid && !invalid.Errors.empty(), "unknown scene fields are rejected");
    tests.Check(!invalid.Errors.empty() && invalid.Errors.front().JsonPointer == "/unknown",
                "strict error includes JSON pointer");

    auto cycle = serializer.Deserialize(std::string(FixtureRoot) + "/Cycle.scene.json");
    tests.Check(!cycle && !cycle.Errors.empty(), "recursive prefab cycle is rejected");

    auto invalidSkinned = serializer.Deserialize(std::string(FixtureRoot) + "/InvalidSkinnedRenderer.scene.json");
    tests.Check(!invalidSkinned && !invalidSkinned.Errors.empty() &&
                    invalidSkinned.Errors.front().JsonPointer == "/entities/1/components/skinnedMeshRenderer/unknown",
                "skinned mesh renderer rejects unknown fields");
    auto invalidAnimator = serializer.Deserialize(std::string(FixtureRoot) + "/InvalidAnimator.scene.json");
    tests.Check(!invalidAnimator && !invalidAnimator.Errors.empty() &&
                    invalidAnimator.Errors.front().JsonPointer == "/entities/1/components/animator/currentClip",
                "animator current clip must name a mapped clip");

    Haki::SceneManifest manifest;
    std::vector<Haki::SerializationError> manifestErrors;
    tests.Check(serializer.LoadManifest(std::string(FixtureRoot) + "/SceneManifest.json", manifest, manifestErrors),
                "catalog loads for environment validation");
    auto invalidEnvironment = serializer.Deserialize(std::string(FixtureRoot) + "/InvalidEnvironment.scene.json");
    tests.Check(!invalidEnvironment && !invalidEnvironment.Errors.empty() &&
                    invalidEnvironment.Errors.front().JsonPointer == "/environment/intensity",
                "negative environment intensity is rejected");
    auto wrongEnvironmentType = serializer.Deserialize(std::string(FixtureRoot) + "/WrongEnvironmentType.scene.json");
    tests.Check(!wrongEnvironmentType && !wrongEnvironmentType.Errors.empty() &&
                    wrongEnvironmentType.Errors.front().JsonPointer == "/environment/texture",
                "environment texture requires a texture catalog asset");
    return tests.Result();
}

int RunManagerTests()
{
    TestRunner tests("Serialization.SceneManager");
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    auto serializer = std::make_shared<Haki::SceneSerializer>(catalog);
    Haki::SceneManager manager;
    tests.Check(manager.Initialize(std::string(FixtureRoot) + "/SceneManifest.json", serializer),
                "scene manager initializes from manifest");
    tests.Check(manager.IsRegistered("test"), "manifest scene is registered");
    manager.Update(0.0f);
    tests.Check(manager.GetActiveSceneId() == "test", "manifest entry scene activates");
    tests.Check(manager.GetLastActivationStatus() == Haki::SceneActivationStatus::Succeeded,
                "entry scene activation succeeds");
    return tests.Result();
}

int RunPhysicsTests()
{
    TestRunner tests("Serialization.Physics");
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    Haki::SceneSerializer serializer(catalog);

    auto invalid = serializer.Deserialize(std::string(FixtureRoot) + "/InvalidPhysics.scene.json");
    const auto hasError = [&](std::string_view pointer)
    {
        return std::any_of(invalid.Errors.begin(), invalid.Errors.end(),
                           [&](const Haki::SerializationError &error) { return error.JsonPointer == pointer; });
    };
    tests.Check(!invalid, "invalid physics components are rejected");
    tests.Check(hasError("/entities/0/components/collider/shape"), "unknown collider shape names its field");
    tests.Check(hasError("/entities/1/components"), "rigidBody without collider is rejected");
    tests.Check(hasError("/entities/2/components/rigidBody/mass"), "negative mass is rejected");
    tests.Check(hasError("/entities/2/components/collider/radius"), "zero sphere radius is rejected");
    tests.Check(hasError("/entities/3/components/characterController"),
                "characterController combined with rigidBody is rejected");
    tests.Check(hasError("/entities/4/components/playerInput"), "playerInput without a character is rejected");
    tests.Check(hasError("/entities/5/components/characterController/maxSlopeDegrees"),
                "out of range character slope is rejected");

    auto loaded = serializer.Deserialize(std::string(FixtureRoot) + "/Physics.scene.json");
    tests.Check(static_cast<bool>(loaded), "physics scene deserializes");
    if (!loaded.Value)
    {
        for (const auto &error : loaded.Errors) std::cout << "  " << error.JsonPointer << ": " << error.Message << '\n';
        return tests.Result();
    }

    entt::registry &registry = loaded.Value->GetRegistry();
    const auto find = [&](std::string_view id)
    {
        for (const auto [entity, entityId] : registry.view<Haki::EntityIdComponent>().each())
        {
            if (entityId.Id == id) return entity;
        }
        return entt::entity{entt::null};
    };

    const entt::entity player = find("player");
    const auto *playerBody = registry.try_get<Haki::RigidBodyComponent>(player);
    const auto *playerCollider = registry.try_get<Haki::ColliderComponent>(player);
    tests.Check(playerBody && playerBody->Motion == Haki::PhysicsMotionType::Kinematic &&
                    playerBody->Layer == Haki::PhysicsLayer::Character && playerBody->Friction == 0.0f,
                "rigid body fields and explicit layer are parsed");
    tests.Check(playerCollider && playerCollider->Shape == Haki::ColliderShape::Capsule &&
                    playerCollider->Radius == 0.35f && playerCollider->HalfHeight == 0.55f &&
                    playerCollider->Offset.y == 0.9f,
                "capsule collider fields are parsed");

    const auto *zone = registry.try_get<Haki::RigidBodyComponent>(find("zone"));
    tests.Check(zone && zone->IsSensor && zone->Layer == Haki::PhysicsLayer::Trigger,
                "sensors default to the trigger layer");
    const auto *floor = registry.try_get<Haki::RigidBodyComponent>(find("floor"));
    tests.Check(floor && floor->Layer == Haki::PhysicsLayer::Static, "static bodies default to the static layer");

    const entt::entity ball = find("ball/root");
    const auto *ballBody = registry.try_get<Haki::RigidBodyComponent>(ball);
    const auto *ballCollider = registry.try_get<Haki::ColliderComponent>(ball);
    tests.Check(ballBody && ballBody->Layer == Haki::PhysicsLayer::Dynamic && ballBody->Mass == 5.0f &&
                    ballBody->Restitution == 0.5f && ballBody->LockRotation,
                "prefab rigid body overrides merge onto prefab values");
    tests.Check(ballCollider && ballCollider->Shape == Haki::ColliderShape::Sphere && ballCollider->Radius == 0.25f,
                "prefab collider overrides merge onto prefab values");

    const entt::entity hero = find("hero");
    const auto *heroController = registry.try_get<Haki::CharacterControllerComponent>(hero);
    const auto *heroInput = registry.try_get<Haki::PlayerInputComponent>(hero);
    tests.Check(heroController && heroController->Radius == 0.4f && heroController->HalfHeight == 0.5f &&
                    heroController->MaxSlopeDegrees == 40.0f && heroController->StepHeight == 0.25f &&
                    heroController->Mass == 80.0f && heroController->MaxStrength == 150.0f,
                "character controller fields are parsed");
    tests.Check(heroInput && heroInput->MoveSpeed == 6.5f, "player input is parsed");

    tests.Check(!loaded.Value->GetPhysics().IsInitialized(), "scenes skip physics while PhysicsCore is down");
    Haki::PhysicsCore::Initialize();
    loaded.Value->UpdateRuntime(0.0f);
    tests.Check(loaded.Value->GetPhysics().GetBodyCount() == 5 && loaded.Value->GetPhysics().GetCharacterCount() == 1,
                "scene runtime creates a body per collider and one character with an inner body");
    for (int step = 0; step < 240; ++step) // restitution 0.5: ~2.5 s to settle
    {
        loaded.Value->UpdateRuntime(1.0f / 60.0f);
    }
    const float ballY = registry.get<Haki::TransformComponent>(ball).Translation.y;
    tests.Check(std::abs(ballY - 0.75f) < 0.05f, "scene runtime simulates the dropped ball onto the floor");
    if (std::abs(ballY - 0.75f) >= 0.05f) std::cout << "  ball y = " << ballY << '\n';
    tests.Check(registry.get<Haki::CharacterControllerComponent>(hero).Grounded &&
                    std::abs(registry.get<Haki::TransformComponent>(hero).Translation.y - 0.5f) < 0.05f,
                "scene character stands on the floor");
    loaded.Value.reset();
    Haki::PhysicsCore::Shutdown();
    return tests.Result();
}


int RunScriptingTests()
{
    TestRunner tests("Serialization.Scripting");
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    Haki::SceneSerializer serializer(catalog);
    serializer.SetScriptRoot(std::string(FixtureRoot) + "/Scripts");

    auto invalid = serializer.Deserialize(std::string(FixtureRoot) + "/InvalidScripting.scene.json");
    const auto hasError = [&](std::string_view pointer)
    {
        return std::any_of(invalid.Errors.begin(), invalid.Errors.end(),
                           [&](const Haki::SerializationError &error) { return error.JsonPointer == pointer; });
    };
    tests.Check(!invalid, "invalid scripts are rejected");
    tests.Check(hasError("/entities/missing/components/script/path"), "missing entity script file is reported");
    tests.Check(hasError("/entities/1/components/script/path"), "non-.lua script path is rejected");
    tests.Check(hasError("/entities/2/components/script/properties/bad"), "object-valued property is rejected");
    tests.Check(hasError("/entities/3/components/script/path"), "script path leaving the scripts folder is rejected");
    tests.Check(hasError("/entities/4/components/script/path"), "script without a path is rejected");
    tests.Check(hasError("/script"), "missing scene script file is reported");

    auto loaded = serializer.Deserialize(std::string(FixtureRoot) + "/Scripting.scene.json");
    tests.Check(static_cast<bool>(loaded), "scripting scene deserializes");
    if (!loaded.Value)
    {
        for (const auto &error : loaded.Errors) std::cout << "  " << error.JsonPointer << ": " << error.Message << '\n';
        return tests.Result();
    }
    tests.Check(loaded.Value->GetSceneScript() == "rules.lua", "scene script path is stored on the scene");

    entt::registry &registry = loaded.Value->GetRegistry();
    const auto find = [&](std::string_view id)
    {
        for (const auto [entity, entityId] : registry.view<Haki::EntityIdComponent>().each())
        {
            if (entityId.Id == id) return entity;
        }
        return entt::entity{entt::null};
    };
    const auto *mover = registry.try_get<Haki::ScriptComponent>(find("mover"));
    const auto property = [](const Haki::ScriptComponent *script, const std::string &name) -> const Haki::ScriptValue *
    {
        if (!script) return nullptr;
        const auto found = script->Properties.find(name);
        return found == script->Properties.end() ? nullptr : &found->second;
    };
    const auto *speed = property(mover, "speed");
    const auto *active = property(mover, "active");
    const auto *label = property(mover, "label");
    const auto *spawn = property(mover, "spawn");
    tests.Check(mover && mover->Path == "mover.lua", "entity script path is parsed");
    tests.Check(speed && std::get<double>(*speed) == 7.5 && active && std::get<bool>(*active) &&
                    label && std::get<std::string>(*label) == "hero" && spawn &&
                    std::get<glm::vec3>(*spawn) == glm::vec3(1.0f, 2.0f, 3.0f),
                "number, bool, string and vec3 properties are parsed");
    tests.Check(registry.all_of<Haki::ScriptComponent>(find("manager")) &&
                    !registry.all_of<Haki::TransformComponent>(find("manager")),
                "a script alone does not require a transform");

    const auto *minion = registry.try_get<Haki::ScriptComponent>(find("minion/root"));
    const auto *minionSpeed = property(minion, "speed");
    const auto *minionTeam = property(minion, "team");
    tests.Check(minionSpeed && std::get<double>(*minionSpeed) == 3.0 && minionTeam &&
                    std::get<std::string>(*minionTeam) == "blue",
                "prefab script property overrides merge onto prefab values");
    return tests.Result();
}

int RunCameraTests()
{
    TestRunner tests("Serialization.Cameras");
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    Haki::SceneSerializer serializer(catalog);

    auto invalid = serializer.Deserialize(std::string(FixtureRoot) + "/InvalidCamera.scene.json");
    const auto hasError = [&](std::string_view pointer)
    {
        return std::any_of(invalid.Errors.begin(), invalid.Errors.end(),
                           [&](const Haki::SerializationError &error) { return error.JsonPointer == pointer; });
    };
    tests.Check(!invalid, "invalid cameras are rejected");
    tests.Check(hasError("/entities/0/components/camera/orthoSize"), "zero orthographic size is rejected");
    tests.Check(hasError("/entities/1/components/camera/projection"), "unknown projection name is rejected");
    tests.Check(hasError("/entities/2/components/camera/fovYDegrees"), "180 degree field of view is rejected");
    tests.Check(hasError("/entities/3/components/camera"), "far plane before near plane is rejected");

    auto loaded = serializer.Deserialize(std::string(FixtureRoot) + "/Cameras.scene.json");
    tests.Check(static_cast<bool>(loaded), "camera scene deserializes");
    if (!loaded.Value)
    {
        for (const auto &error : loaded.Errors) std::cout << "  " << error.JsonPointer << ": " << error.Message << '\n';
        return tests.Result();
    }
    const auto view = loaded.Value->GetPrimaryCameraView();
    tests.Check(view && view->Projection == Haki::CameraProjection::Orthographic && view->OrthographicSize == 8.0f &&
                    view->NearPlane == 0.5f && view->FarPlane == 100.0f,
                "orthographic camera fields are parsed into the primary view");
    bool defaultIsPerspective = false;
    for (const auto [entity, camera] : loaded.Value->GetRegistry().view<Haki::CameraComponent>().each())
    {
        defaultIsPerspective = defaultIsPerspective || (!camera.Primary && camera.Projection ==
                                                        Haki::CameraProjection::Perspective);
    }
    tests.Check(defaultIsPerspective, "cameras default to perspective");
    return tests.Result();
}
int RunLocomotionTests()
{
    TestRunner tests("Serialization.Locomotion");
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    Haki::SceneSerializer serializer(catalog);

    // The fixture's assets don't exist, so loading fails; only parse errors at these pointers matter here.
    auto result = serializer.Deserialize(std::string(FixtureRoot) + "/LocomotionBlend.scene.json");
    const auto hasError = [&](std::string_view pointer)
    {
        return std::any_of(result.Errors.begin(), result.Errors.end(),
                           [&](const Haki::SerializationError &error) { return error.JsonPointer == pointer; });
    };
    const auto anyErrorUnder = [&](const std::string &prefix)
    {
        return std::any_of(result.Errors.begin(), result.Errors.end(),
                           [&](const Haki::SerializationError &error) { return error.JsonPointer.starts_with(prefix); });
    };
    tests.Check(!anyErrorUnder("/entities/2/components/locomotion"), "a valid speed blend parses");
    tests.Check(hasError("/entities/2/components/animator/defaultFade"), "a negative default fade is rejected");
    tests.Check(hasError("/entities/3/components/locomotion/blend/2/speed"), "blend speeds must increase");
    tests.Check(hasError("/entities/4/components/locomotion/blend/1/clip"), "blend clips must exist in the animator");
    tests.Check(hasError("/entities/5/components/locomotion"), "blend and the legacy idle/move pair can't be mixed");
    tests.Check(!anyErrorUnder("/entities/6/components/locomotion"), "the legacy idle/move form still parses");
    tests.Check(hasError("/entities/7/components/locomotion/maxPlaybackScale"), "playback scale below 1 is rejected");
    return tests.Result();
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        return 2;
    }
    const std::string_view testCase = argv[1];
    if (testCase == "catalog") return RunCatalogTests();
    if (testCase == "scene") return RunSceneTests();
    if (testCase == "errors") return RunErrorTests();
    if (testCase == "physics") return RunPhysicsTests();
    if (testCase == "scripting") return RunScriptingTests();
    if (testCase == "cameras") return RunCameraTests();
    if (testCase == "locomotion") return RunLocomotionTests();
    if (testCase == "manager") return RunManagerTests();
    return 2;
}
