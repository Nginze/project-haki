#pragma once

#include "Haki/Scene/Scene.h"
#include "Haki/Serialization/SerializationError.h"

#include <glm/vec4.hpp>

#include <any>
#include <concepts>
#include <cstddef>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Haki
{

class SceneSerializer;

using SceneId = std::string;

struct SceneCreateContext
{
    std::string_view Id;
    std::string_view Source;
};

using SceneFactory = std::function<std::unique_ptr<Scene>(const SceneCreateContext &)>;

struct SceneTransitionSpec
{
    std::string Effect = "instant";
    float CoverDuration = 0.0f;
    float RevealDuration = 0.0f;
    glm::vec4 Color{0.0f, 0.0f, 0.0f, 1.0f};

    static SceneTransitionSpec Instant();
    static SceneTransitionSpec Fade(float phaseDuration = 0.25f,
                                    glm::vec4 color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
};

struct SceneRequest
{
    SceneId Target;
    std::any Payload;
    SceneTransitionSpec Transition = SceneTransitionSpec::Instant();
    bool ReloadIfActive = false;
};

enum class SceneRegistrationResult
{
    Registered,
    AlreadyRegistered,
    InvalidId,
    InvalidFactory
};

enum class SceneRequestResult
{
    Accepted,
    ReplacedPendingRequest,
    UnknownScene,
    AlreadyActive,
    ManagerShuttingDown
};

enum class SceneTransitionPhase
{
    Idle,
    Covering,
    Revealing
};

enum class SceneActivationStatus
{
    None,
    Succeeded,
    FactoryReturnedNull,
    LoadFailed
};

class SceneManager
{
public:
    SceneManager() = default;
    ~SceneManager();

    SceneManager(const SceneManager &) = delete;
    SceneManager &operator=(const SceneManager &) = delete;
    SceneManager(SceneManager &&) = delete;
    SceneManager &operator=(SceneManager &&) = delete;

    SceneRegistrationResult RegisterScene(SceneId id, SceneFactory factory, std::string source = {});
    bool Initialize(const std::filesystem::path &manifestPath, std::shared_ptr<SceneSerializer> serializer);

    template <typename T>
        requires std::derived_from<T, Scene> && std::default_initializable<T>
    SceneRegistrationResult RegisterScene(SceneId id, std::string source = {})
    {
        return RegisterScene(
            std::move(id), [](const SceneCreateContext &) { return std::make_unique<T>(); }, std::move(source));
    }

    bool IsRegistered(std::string_view id) const;
    std::size_t GetRegisteredSceneCount() const { return registrations_.size(); }

    SceneRequestResult RequestScene(SceneRequest request);
    SceneRequestResult RequestScene(SceneId target, std::any payload = {},
                                    SceneTransitionSpec transition = SceneTransitionSpec::Instant(),
                                    bool reloadIfActive = false);

    void Update(float deltaTime);
    void Render(const SceneRenderContext &context);
    void Shutdown();

    Scene *GetActiveScene() { return activeScene_.get(); }
    const Scene *GetActiveScene() const { return activeScene_.get(); }

    template <typename T>
        requires std::derived_from<T, Scene>
    T *GetActiveSceneAs()
    {
        return dynamic_cast<T *>(activeScene_.get());
    }

    std::string_view GetActiveSceneId() const { return activeSceneId_; }
    SceneTransitionPhase GetTransitionPhase() const { return transitionPhase_; }
    float GetTransitionProgress() const { return transitionProgress_; }
    const SceneTransitionSpec &GetTransitionSpec() const { return activeTransition_; }
    SceneActivationStatus GetLastActivationStatus() const { return lastActivationStatus_; }
    const std::vector<SerializationError> &GetLastSerializationErrors() const { return lastSerializationErrors_; }
    bool IsTransitioning() const { return transitionPhase_ != SceneTransitionPhase::Idle; }

private:
    struct SceneRegistration
    {
        SceneFactory Factory;
        std::string Source;
    };

    void BeginCovering(const SceneTransitionSpec &transition);
    void AdvanceTransition(float deltaTime);
    bool ActivatePendingScene();

    std::unordered_map<SceneId, SceneRegistration> registrations_;
    std::unique_ptr<Scene> activeScene_;
    SceneId activeSceneId_;
    std::optional<SceneRequest> pendingRequest_;
    SceneTransitionSpec activeTransition_ = SceneTransitionSpec::Instant();
    SceneTransitionPhase transitionPhase_ = SceneTransitionPhase::Idle;
    SceneActivationStatus lastActivationStatus_ = SceneActivationStatus::None;
    float transitionProgress_ = 0.0f;
    bool shuttingDown_ = false;
    std::shared_ptr<SceneSerializer> serializer_;
    std::vector<SerializationError> lastSerializationErrors_;
};

} // namespace Haki
