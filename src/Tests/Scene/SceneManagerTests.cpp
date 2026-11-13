#include "Haki/Scene/SceneManager.h"
#include "Haki/Scene/Components.h"

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using Haki::Scene;
using Haki::SceneCreateContext;
using Haki::SceneEnterContext;
using Haki::SceneManager;
using Haki::SceneRegistrationResult;
using Haki::SceneRequestResult;
using Haki::SceneTransitionPhase;
using Haki::SceneTransitionSpec;

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

struct EnterPayload
{
    int SpawnPoint = 0;
};

class RecordingScene final : public Scene
{
public:
    RecordingScene(std::string name, std::shared_ptr<std::vector<std::string>> events)
        : name_(std::move(name)), events_(std::move(events))
    {
        events_->push_back(name_ + ":Construct");
        transformEntity_ = GetRegistry().create();
        GetRegistry().emplace<Haki::TransformComponent>(transformEntity_);
    }

    ~RecordingScene() override { events_->push_back(name_ + ":Destroy"); }

    void OnLoad() override { events_->push_back(name_ + ":Load"); }

    void OnEnter(const SceneEnterContext &context) override
    {
        events_->push_back(name_ + ":Enter");
        if (const auto *payload = context.TryGet<EnterPayload>())
        {
            spawnPoint_ = payload->SpawnPoint;
        }
    }

    void OnUpdate(float /*deltaTime*/) override
    {
        events_->push_back(name_ + ":Update");
        GetRegistry().get<Haki::TransformComponent>(transformEntity_).Translation.x = 3.0f;
    }
    void OnRender(const Haki::SceneRenderContext &context) override
    {
        events_->push_back(name_ + ":Render");
        renderWidth_ = context.Width;
        renderHeight_ = context.Height;
    }
    void OnExit() override { events_->push_back(name_ + ":Exit"); }
    void OnUnload() override { events_->push_back(name_ + ":Unload"); }

    int GetSpawnPoint() const { return spawnPoint_; }
    int GetRenderWidth() const { return renderWidth_; }
    int GetRenderHeight() const { return renderHeight_; }
    float GetWorldX() const { return GetRegistry().get<Haki::TransformComponent>(transformEntity_).WorldMatrix[3].x; }

private:
    std::string name_;
    std::shared_ptr<std::vector<std::string>> events_;
    int spawnPoint_ = -1;
    int renderWidth_ = 0;
    int renderHeight_ = 0;
    entt::entity transformEntity_ = entt::null;
};

Haki::SceneFactory MakeRecordingFactory(std::string name, std::shared_ptr<std::vector<std::string>> events,
                                        int *creationCount = nullptr, std::string *observedSource = nullptr)
{
    return [name = std::move(name), events = std::move(events), creationCount,
            observedSource](const SceneCreateContext &context)
    {
        if (creationCount)
        {
            ++*creationCount;
        }
        if (observedSource)
        {
            *observedSource = context.Source;
        }
        return std::make_unique<RecordingScene>(name, events);
    };
}

int RunLifecycleTests()
{
    TestRunner tests("SceneManager.Lifecycle");
    auto events = std::make_shared<std::vector<std::string>>();
    SceneManager manager;

    tests.Check(manager.RegisterScene("Village", MakeRecordingFactory("Village", events)) ==
                    SceneRegistrationResult::Registered,
                "scene factory registers");
    tests.Check(manager.RequestScene("Village", EnterPayload{7}) == SceneRequestResult::Accepted,
                "initial scene request is accepted");
    tests.Check(manager.GetActiveScene() == nullptr, "scene request is deferred until update");

    manager.Update(0.0f);
    manager.Render({nullptr, 1280, 720});
    auto *village = manager.GetActiveSceneAs<RecordingScene>();
    tests.Check(village != nullptr, "requested scene becomes active");
    tests.Check(village && village->GetSpawnPoint() == 7, "typed entry payload reaches OnEnter");
    tests.Check(village && village->GetRenderWidth() == 1280 && village->GetRenderHeight() == 720,
                 "render context reaches active scene");
    tests.Check(village && village->GetWorldX() == 3.0f,
                "engine transform updates run even when the scene overrides OnUpdate");

    const std::vector<std::string> expectedBeforeShutdown{
        "Village:Construct", "Village:Load", "Village:Enter", "Village:Update", "Village:Render"};
    tests.Check(*events == expectedBeforeShutdown, "load, enter, update, and render hooks run in order");

    manager.Shutdown();
    const std::vector<std::string> expectedAfterShutdown{
        "Village:Construct", "Village:Load",   "Village:Enter",  "Village:Update",
        "Village:Render",    "Village:Exit", "Village:Unload", "Village:Destroy"};
    tests.Check(*events == expectedAfterShutdown, "shutdown runs exit and unload before destruction");
    tests.Check(manager.GetActiveScene() == nullptr, "shutdown clears active scene");
    tests.Check(manager.RequestScene("Village") == SceneRequestResult::ManagerShuttingDown,
                "shutdown manager rejects new requests");
    return tests.Result();
}

int RunFactoryTests()
{
    TestRunner tests("SceneManager.Factories");
    auto events = std::make_shared<std::vector<std::string>>();
    SceneManager manager;
    int villageCreations = 0;
    std::string observedSource;

    tests.Check(manager.RegisterScene("", MakeRecordingFactory("Invalid", events)) ==
                    SceneRegistrationResult::InvalidId,
                "empty scene IDs are rejected");
    tests.Check(manager.RegisterScene("NullFactory", {}) == SceneRegistrationResult::InvalidFactory,
                "empty factories are rejected");
    tests.Check(manager.RegisterScene("Village",
                                      MakeRecordingFactory("Village", events, &villageCreations, &observedSource),
                                      "Scenes/Village.hscene") == SceneRegistrationResult::Registered,
                "factory may store a future serialized source");
    tests.Check(manager.RegisterScene("Village", MakeRecordingFactory("Duplicate", events)) ==
                    SceneRegistrationResult::AlreadyRegistered,
                "duplicate IDs are rejected");
    tests.Check(manager.RequestScene("Missing") == SceneRequestResult::UnknownScene,
                "unknown scene requests are rejected before transitioning");

    manager.RegisterScene("Menu", MakeRecordingFactory("Menu", events));
    manager.RequestScene("Village");
    manager.Update(0.0f);
    manager.Update(0.0f);
    manager.RequestScene("Menu");
    manager.Update(0.0f);
    manager.Update(0.0f);
    manager.RequestScene("Village");
    manager.Update(0.0f);

    tests.Check(villageCreations == 2, "re-entering a scene creates a fresh instance");
    tests.Check(observedSource == "Scenes/Village.hscene", "factory receives its source metadata");
    tests.Check(manager.RequestScene("Village") == SceneRequestResult::AlreadyActive,
                "active scene is not accidentally recreated");
    return tests.Result();
}

int RunTransitionTests()
{
    TestRunner tests("SceneManager.Transitions");
    auto events = std::make_shared<std::vector<std::string>>();
    SceneManager manager;
    int villageCreations = 0;
    int castleCreations = 0;
    int islandCreations = 0;

    manager.RegisterScene("Village", MakeRecordingFactory("Village", events, &villageCreations));
    manager.RegisterScene("Castle", MakeRecordingFactory("Castle", events, &castleCreations));
    manager.RegisterScene("Island", MakeRecordingFactory("Island", events, &islandCreations));
    manager.RequestScene("Village");
    manager.Update(0.0f);
    manager.Update(0.0f);

    tests.Check(manager.RequestScene("Castle", {}, SceneTransitionSpec::Fade(1.0f)) ==
                    SceneRequestResult::Accepted,
                "fade transition request is accepted");
    manager.Update(0.4f);
    tests.Check(manager.GetActiveSceneId() == "Village", "old scene remains active while covering");
    tests.Check(manager.GetTransitionPhase() == SceneTransitionPhase::Covering,
                "transition reports covering phase");
    tests.Check(manager.GetTransitionProgress() == 0.4f, "transition exposes cover progress");

    tests.Check(manager.RequestScene("Island") == SceneRequestResult::ReplacedPendingRequest,
                "latest request replaces pending target");
    manager.Update(0.6f);
    tests.Check(manager.GetActiveSceneId() == "Island", "replacement target activates at switch point");
    tests.Check(castleCreations == 0, "replaced target is never constructed");
    tests.Check(islandCreations == 1, "latest target is constructed once");
    tests.Check(manager.GetTransitionPhase() == SceneTransitionPhase::Revealing,
                "transition reports revealing phase after switch");

    manager.Update(1.0f);
    tests.Check(!manager.IsTransitioning(), "transition returns to idle after reveal");
    tests.Check(manager.GetTransitionProgress() == 0.0f, "transition progress returns to zero");
    return tests.Result();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: SceneManagerTests <lifecycle|factories|transitions>\n";
        return 2;
    }

    const std::string_view testCase = argv[1];
    if (testCase == "lifecycle")
    {
        return RunLifecycleTests();
    }
    if (testCase == "factories")
    {
        return RunFactoryTests();
    }
    if (testCase == "transitions")
    {
        return RunTransitionTests();
    }

    std::cerr << "Unknown test case: " << testCase << '\n';
    return 2;
}
