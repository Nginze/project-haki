#include "Haki/AssetManager/AssetManager.h"
#include "Haki/AssetManager/AssetCatalog.h"
#include "Haki/AssetManager/Skeleton.h"
#include "Haki/AssetManager/AnimationClip.h"
#include "Haki/AssetLoader/GltfDocumentCache.h"
#include "Haki/AssetLoader/GltfSkeletonLoader.h"
#include "Haki/AssetLoader/GltfAnimationClipLoader.h"
#include "Haki/GL/Mesh.h"
#include "Haki/GL/Shader.h"
#include "Haki/GL/Texture.h"

#include <iostream>
#include <memory>
#include <string_view>

namespace
{

class TestRunner
{
public:
    explicit TestRunner(std::string_view name)
    {
        std::cout << "\n=== " << name << " ===\n";
    }

    void Check(bool condition, std::string_view name)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
        failures_ += condition ? 0 : 1;
    }

    int Result() const { return failures_ == 0 ? 0 : 1; }

private:
    int failures_ = 0;
};

template <typename Resource>
class FakeLoader final : public Haki::AssetLoader<Resource>
{
public:
    FakeLoader(int &supportCalls, int &loadCalls, bool supported = true, bool fail = false)
        : supportCalls_(supportCalls), loadCalls_(loadCalls), supported_(supported), fail_(fail)
    {
    }

    bool Supports(const Haki::AssetLoadRequest &) const override
    {
        ++supportCalls_;
        return supported_;
    }

    Haki::AssetLoaderResult<Resource> Load(const Haki::AssetLoadRequest &) override
    {
        ++loadCalls_;
        if (fail_)
        {
            return {nullptr, Haki::AssetLoadError::LoaderFailed, "Fake loader failure"};
        }
        return {std::make_unique<Resource>(), Haki::AssetLoadError::None, {}};
    }

private:
    int &supportCalls_;
    int &loadCalls_;
    bool supported_;
    bool fail_;
};

int RunLifecycleTests()
{
    TestRunner tests("AssetManager.Lifecycle");
    Haki::AssetManager::Shutdown();

    const auto beforeInitialization = Haki::AssetManager::LoadTexture({1, "texture.fake"});
    tests.Check(beforeInitialization.Error == Haki::AssetLoadError::NotInitialized,
                "loads fail safely before initialization");
    tests.Check(Haki::AssetManager::GetTexture({}) == nullptr, "queries are safe before initialization");
    tests.Check(!Haki::AssetManager::RegisterTextureLoader(nullptr),
                "loader registration fails before initialization");

    tests.Check(Haki::AssetManager::Initialize(), "manager initializes");
    tests.Check(Haki::AssetManager::Initialize(), "initialization is idempotent");
    tests.Check(Haki::AssetManager::IsInitialized(), "initialized state is reported");

    int supportCalls = 0;
    int loadCalls = 0;
    tests.Check(Haki::AssetManager::RegisterTextureLoader(
                    std::make_unique<FakeLoader<Haki::Texture>>(supportCalls, loadCalls)),
                "loader registers after initialization");
    tests.Check(!Haki::AssetManager::RegisterTextureLoader(nullptr), "null loaders are rejected");

    const auto loaded = Haki::AssetManager::LoadTexture({2, "texture.fake"});
    tests.Check(static_cast<bool>(loaded), "registered loader creates a texture");
    tests.Check(Haki::AssetManager::GetTexture(loaded.Value) != nullptr, "loaded texture is accessible");

    Haki::AssetManager::Shutdown();
    tests.Check(!Haki::AssetManager::IsInitialized(), "shutdown clears initialized state");
    tests.Check(Haki::AssetManager::GetTexture(loaded.Value) == nullptr, "shutdown invalidates asset access");

    Haki::AssetManager::Initialize();
    Haki::AssetManager::RegisterTextureLoader(
        std::make_unique<FakeLoader<Haki::Texture>>(supportCalls, loadCalls));
    const auto afterRestart = Haki::AssetManager::LoadTexture({3, "restarted.fake"});
    tests.Check(afterRestart && Haki::AssetManager::GetTexture(loaded.Value) == nullptr,
                "pre-shutdown handles stay stale after reinitialization");

    Haki::AssetManager::Shutdown();
    Haki::AssetManager::Shutdown();
    tests.Check(!Haki::AssetManager::IsInitialized(), "repeated shutdown is safe");
    return tests.Result();
}

int RunLoaderTests()
{
    TestRunner tests("AssetManager.Loaders");
    Haki::AssetManager::Shutdown();
    Haki::AssetManager::Initialize();

    const auto noLoader = Haki::AssetManager::LoadTexture({10, "texture.fake"});
    tests.Check(noLoader.Error == Haki::AssetLoadError::NoCompatibleLoader,
                "missing compatible loader returns an explicit error");

    int unsupportedChecks = 0;
    int unsupportedLoads = 0;
    int firstChecks = 0;
    int firstLoads = 0;
    int secondChecks = 0;
    int secondLoads = 0;
    Haki::AssetManager::RegisterTextureLoader(
        std::make_unique<FakeLoader<Haki::Texture>>(unsupportedChecks, unsupportedLoads, false));
    Haki::AssetManager::RegisterTextureLoader(
        std::make_unique<FakeLoader<Haki::Texture>>(firstChecks, firstLoads));
    Haki::AssetManager::RegisterTextureLoader(
        std::make_unique<FakeLoader<Haki::Texture>>(secondChecks, secondLoads));

    const auto selected = Haki::AssetManager::LoadTexture({11, "selected.fake"});
    tests.Check(static_cast<bool>(selected), "first compatible loader succeeds");
    tests.Check(unsupportedChecks == 1 && unsupportedLoads == 0, "unsupported loader is skipped");
    tests.Check(firstChecks == 1 && firstLoads == 1, "first compatible loader is used");
    tests.Check(secondChecks == 0 && secondLoads == 0, "later loaders are not inspected");

    const auto duplicate = Haki::AssetManager::LoadTexture({11, "selected.fake"});
    tests.Check(duplicate && duplicate.Value == selected.Value, "duplicate load returns the cached handle");
    tests.Check(firstLoads == 1, "cached asset is loaded only once");

    Haki::AssetManager::Shutdown();
    Haki::AssetManager::Initialize();
    int failureChecks = 0;
    int failureLoads = 0;
    int fallbackChecks = 0;
    int fallbackLoads = 0;
    Haki::AssetManager::RegisterTextureLoader(
        std::make_unique<FakeLoader<Haki::Texture>>(failureChecks, failureLoads, true, true));
    Haki::AssetManager::RegisterTextureLoader(
        std::make_unique<FakeLoader<Haki::Texture>>(fallbackChecks, fallbackLoads));

    const auto failed = Haki::AssetManager::LoadTexture({12, "failure.fake"});
    tests.Check(failed.Error == Haki::AssetLoadError::LoaderFailed, "loader failure is propagated");
    tests.Check(failed.Message == "Fake loader failure", "loader diagnostic is preserved");
    tests.Check(failureLoads == 1 && fallbackChecks == 0 && fallbackLoads == 0,
                "loader failure does not trigger fallback");

    Haki::AssetManager::Shutdown();
    return tests.Result();
}

int RunHandleTests()
{
    TestRunner tests("AssetManager.Handles");
    Haki::AssetManager::Shutdown();
    Haki::AssetManager::Initialize();
    int supportCalls = 0;
    int loadCalls = 0;
    Haki::AssetManager::RegisterTextureLoader(
        std::make_unique<FakeLoader<Haki::Texture>>(supportCalls, loadCalls));

    const auto first = Haki::AssetManager::LoadTexture({20, "first.fake"});
    tests.Check(first && Haki::AssetManager::GetLoadedTextureCount() == 1, "loaded count tracks resources");
    tests.Check(Haki::AssetManager::UnloadTexture(first.Value), "valid handle unloads its resource");
    tests.Check(Haki::AssetManager::GetTexture(first.Value) == nullptr, "unloaded handle becomes stale");
    tests.Check(!Haki::AssetManager::UnloadTexture(first.Value), "stale handle cannot unload twice");

    const auto second = Haki::AssetManager::LoadTexture({21, "second.fake"});
    tests.Check(second.Value.Index == first.Value.Index, "unloaded slots are reused");
    tests.Check(second.Value.Generation != first.Value.Generation, "reused slots advance generation");
    tests.Check(Haki::AssetManager::GetTexture(first.Value) == nullptr, "old generation cannot access reused slot");
    tests.Check(Haki::AssetManager::GetTexture(second.Value) != nullptr, "new generation accesses reused slot");

    Haki::AssetManager::Clear();
    tests.Check(Haki::AssetManager::GetLoadedTextureCount() == 0, "clear removes all resources");
    tests.Check(Haki::AssetManager::GetTexture(second.Value) == nullptr, "clear invalidates existing handles");

    Haki::AssetManager::Shutdown();
    return tests.Result();
}

int RunIdentityTests()
{
    TestRunner tests("AssetManager.Identity");
    Haki::AssetManager::Shutdown();
    Haki::AssetManager::Initialize();
    int textureChecks = 0;
    int textureLoads = 0;
    int shaderChecks = 0;
    int shaderLoads = 0;
    Haki::AssetManager::RegisterTextureLoader(
        std::make_unique<FakeLoader<Haki::Texture>>(textureChecks, textureLoads));
    Haki::AssetManager::RegisterShaderLoader(
        std::make_unique<FakeLoader<Haki::Shader>>(shaderChecks, shaderLoads));

    const auto texture = Haki::AssetManager::LoadTexture({30, "Assets/./wall.fake"});
    const auto normalizedDuplicate = Haki::AssetManager::LoadTexture({30, "Assets/wall.fake"});
    tests.Check(texture && normalizedDuplicate && texture.Value == normalizedDuplicate.Value,
                "normalized equivalent paths identify the same asset");

    const auto sourceConflict = Haki::AssetManager::LoadTexture({30, "Assets/floor.fake"});
    tests.Check(sourceConflict.Error == Haki::AssetLoadError::AssetIdConflict,
                "same ID with another source is rejected");

    const auto typeConflict = Haki::AssetManager::LoadShader({30, "Assets/wall.fake"});
    tests.Check(typeConflict.Error == Haki::AssetLoadError::AssetIdConflict,
                "asset IDs are unique across resource types");
    tests.Check(shaderLoads == 0, "type conflict is rejected before invoking a loader");

    const auto invalid = Haki::AssetManager::LoadTexture({Haki::InvalidAssetID, "invalid.fake"});
    tests.Check(invalid.Error == Haki::AssetLoadError::InvalidRequest, "invalid asset IDs are rejected");

    Haki::AssetManager::Shutdown();
    return tests.Result();
}

int RunSkeletalLoaderTests()
{
    TestRunner tests("AssetManager.SkeletalLoaders");
    auto catalog = std::make_shared<Haki::AssetCatalog>();
    std::vector<Haki::SerializationError> errors;
    tests.Check(catalog->Load("src/Tests/Serialization/Fixtures/AssetCatalog.json", errors),
                "skeletal fixture catalog loads");
    const auto *skeletonEntry = catalog->Find("skeleton.translated");
    const auto *clipEntry = catalog->Find("animation.joint-motion");
    tests.Check(skeletonEntry && clipEntry &&
                    clipEntry->SkeletonRuntimeId == skeletonEntry->RuntimeId,
                "animation clip resolves its skeleton asset ID");

    Haki::AssetManager::Shutdown();
    Haki::AssetManager::Initialize();
    const auto cache = std::make_shared<Haki::GltfDocumentCache>();
    Haki::AssetManager::RegisterSkeletonLoader(
        std::make_unique<Haki::GltfSkeletonLoader>(catalog, cache));
    Haki::AssetManager::RegisterAnimationClipLoader(
        std::make_unique<Haki::GltfAnimationClipLoader>(catalog, cache));
    const auto skeletonResult = skeletonEntry
                                    ? Haki::AssetManager::LoadSkeleton(
                                          {skeletonEntry->RuntimeId, skeletonEntry->Source})
                                    : Haki::AssetResult<Haki::SkeletonHandle>{};
    const auto clipResult = clipEntry
                                ? Haki::AssetManager::LoadAnimationClip({clipEntry->RuntimeId, clipEntry->Source})
                                : Haki::AssetResult<Haki::AnimationClipHandle>{};
    const Haki::Skeleton *skeleton = skeletonResult ? Haki::AssetManager::GetSkeleton(skeletonResult.Value) : nullptr;
    const Haki::AnimationClip *clip = clipResult ? Haki::AssetManager::GetAnimationClip(clipResult.Value) : nullptr;
    tests.Check(skeleton && skeleton->Nodes.size() == 1 && skeleton->Joints.size() == 1 &&
                    skeleton->Joints[0].NodeIndex == 0,
                "skeleton separates topological pose nodes from palette joints");
    tests.Check(clip && skeletonEntry && clip->SkeletonId == skeletonEntry->RuntimeId &&
                    clip->Tracks.size() == 1 && clip->Tracks[0].Translations.size() == 3 &&
                    clip->Tracks[0].TranslationInterpolation == Haki::AnimationInterpolation::Step,
                "clip targets pose nodes and retains STEP interpolation");
    tests.Check(cache->GetParseCount() == 1, "mesh-family loaders can share one parsed glTF document");
    tests.Check(Haki::AssetManager::GetLoadedSkeletonCount() == 1 &&
                    Haki::AssetManager::GetLoadedAnimationClipCount() == 1,
                "asset manager stores skeletal resources");
    Haki::AssetManager::Shutdown();
    return tests.Result();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: AssetManagerTests <lifecycle|loaders|handles|identity|skeletal-loaders>\n";
        return 2;
    }

    const std::string_view testCase = argv[1];
    if (testCase == "lifecycle")
    {
        return RunLifecycleTests();
    }
    if (testCase == "loaders")
    {
        return RunLoaderTests();
    }
    if (testCase == "handles")
    {
        return RunHandleTests();
    }
    if (testCase == "identity")
    {
        return RunIdentityTests();
    }
    if (testCase == "skeletal-loaders")
    {
        return RunSkeletalLoaderTests();
    }

    std::cerr << "Unknown test case: " << testCase << '\n';
    return 2;
}
