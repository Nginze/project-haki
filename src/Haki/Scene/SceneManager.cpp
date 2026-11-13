#include "Haki/Scene/SceneManager.h"

#include "Haki/Logging/Logger.h"
#include "Haki/Serialization/SceneSerializer.h"

#include <algorithm>

namespace Haki
{

SceneTransitionSpec SceneTransitionSpec::Instant()
{
    return {};
}

SceneTransitionSpec SceneTransitionSpec::Fade(float phaseDuration, glm::vec4 color)
{
    SceneTransitionSpec transition;
    transition.Effect = "fade";
    transition.CoverDuration = std::max(phaseDuration, 0.0f);
    transition.RevealDuration = std::max(phaseDuration, 0.0f);
    transition.Color = color;
    return transition;
}

SceneManager::~SceneManager()
{
    Shutdown();
}

bool SceneManager::Initialize(const std::filesystem::path &manifestPath, std::shared_ptr<SceneSerializer> serializer)
{
    if (!serializer || shuttingDown_ || !registrations_.empty())
    {
        return false;
    }

    SceneManifest manifest;
    lastSerializationErrors_.clear();
    if (!serializer->LoadManifest(manifestPath, manifest, lastSerializationErrors_))
    {
        return false;
    }

    serializer_ = std::move(serializer);
    for (const SceneManifestEntry &entry : manifest.Scenes)
    {
        const std::filesystem::path source = entry.Source;
        const SceneRegistrationResult registered = RegisterScene(
            entry.Id,
            [this, source](const SceneCreateContext &)
            {
                SceneLoadResult loaded = serializer_->Deserialize(source);
                for (const SerializationError &error : loaded.Errors)
                {
                    Logger::Error() << error.Source.string() << error.JsonPointer << ": " << error.Message;
                }
                lastSerializationErrors_ = std::move(loaded.Errors);
                return std::move(loaded.Value);
            },
            source.string());
        if (registered != SceneRegistrationResult::Registered)
        {
            registrations_.clear();
            serializer_.reset();
            return false;
        }
    }

    return RequestScene(manifest.EntryScene) == SceneRequestResult::Accepted;
}

SceneRegistrationResult SceneManager::RegisterScene(SceneId id, SceneFactory factory, std::string source)
{
    if (id.empty())
    {
        return SceneRegistrationResult::InvalidId;
    }
    if (!factory)
    {
        return SceneRegistrationResult::InvalidFactory;
    }
    if (registrations_.contains(id))
    {
        return SceneRegistrationResult::AlreadyRegistered;
    }

    registrations_.emplace(std::move(id), SceneRegistration{std::move(factory), std::move(source)});
    return SceneRegistrationResult::Registered;
}

bool SceneManager::IsRegistered(std::string_view id) const
{
    return registrations_.find(std::string(id)) != registrations_.end();
}

SceneRequestResult SceneManager::RequestScene(SceneRequest request)
{
    if (shuttingDown_)
    {
        return SceneRequestResult::ManagerShuttingDown;
    }
    if (!IsRegistered(request.Target))
    {
        return SceneRequestResult::UnknownScene;
    }
    if (activeSceneId_ == request.Target && !request.ReloadIfActive && !pendingRequest_)
    {
        return SceneRequestResult::AlreadyActive;
    }

    const bool replacedPendingRequest = pendingRequest_.has_value();
    pendingRequest_ = std::move(request);

    if (transitionPhase_ == SceneTransitionPhase::Idle)
    {
        BeginCovering(pendingRequest_->Transition);
    }

    return replacedPendingRequest ? SceneRequestResult::ReplacedPendingRequest : SceneRequestResult::Accepted;
}

SceneRequestResult SceneManager::RequestScene(SceneId target, std::any payload, SceneTransitionSpec transition,
                                              bool reloadIfActive)
{
    return RequestScene(SceneRequest{std::move(target), std::move(payload), std::move(transition), reloadIfActive});
}

void SceneManager::Update(float deltaTime)
{
    if (shuttingDown_)
    {
        return;
    }

    AdvanceTransition(std::max(deltaTime, 0.0f));
    if (activeScene_)
    {
        activeScene_->OnUpdate(std::max(deltaTime, 0.0f));
        activeScene_->UpdateRuntime(std::max(deltaTime, 0.0f));
    }
}

void SceneManager::Render(const SceneRenderContext &context)
{
    if (activeScene_)
    {
        activeScene_->OnRender(context);
    }
}

void SceneManager::Shutdown()
{
    if (shuttingDown_)
    {
        return;
    }

    shuttingDown_ = true;
    pendingRequest_.reset();
    transitionPhase_ = SceneTransitionPhase::Idle;
    transitionProgress_ = 0.0f;

    if (activeScene_)
    {
        activeScene_->OnExit();
        activeScene_->OnUnload();
        activeScene_.reset();
    }

    activeSceneId_.clear();
    registrations_.clear();
    serializer_.reset();
    lastSerializationErrors_.clear();
}

void SceneManager::BeginCovering(const SceneTransitionSpec &transition)
{
    activeTransition_ = transition;
    transitionPhase_ = SceneTransitionPhase::Covering;
    transitionProgress_ = 0.0f;
}

void SceneManager::AdvanceTransition(float deltaTime)
{
    switch (transitionPhase_)
    {
    case SceneTransitionPhase::Idle:
        if (pendingRequest_)
        {
            BeginCovering(pendingRequest_->Transition);
        }
        break;

    case SceneTransitionPhase::Covering:
        if (activeTransition_.CoverDuration <= 0.0f)
        {
            transitionProgress_ = 1.0f;
        }
        else
        {
            transitionProgress_ =
                std::min(transitionProgress_ + deltaTime / activeTransition_.CoverDuration, 1.0f);
        }

        if (transitionProgress_ >= 1.0f)
        {
            ActivatePendingScene();
            transitionPhase_ = SceneTransitionPhase::Revealing;
        }
        break;

    case SceneTransitionPhase::Revealing:
        if (activeTransition_.RevealDuration <= 0.0f)
        {
            transitionProgress_ = 0.0f;
        }
        else
        {
            transitionProgress_ =
                std::max(transitionProgress_ - deltaTime / activeTransition_.RevealDuration, 0.0f);
        }

        if (transitionProgress_ <= 0.0f)
        {
            transitionPhase_ = SceneTransitionPhase::Idle;
            if (pendingRequest_)
            {
                BeginCovering(pendingRequest_->Transition);
            }
        }
        break;
    }
}

bool SceneManager::ActivatePendingScene()
{
    if (!pendingRequest_)
    {
        return false;
    }

    SceneRequest request = std::move(*pendingRequest_);
    pendingRequest_.reset();

    const auto registration = registrations_.find(request.Target);
    if (registration == registrations_.end())
    {
        lastActivationStatus_ = SceneActivationStatus::FactoryReturnedNull;
        return false;
    }

    const SceneCreateContext createContext{registration->first, registration->second.Source};
    std::unique_ptr<Scene> nextScene = registration->second.Factory(createContext);
    if (!nextScene)
    {
        lastActivationStatus_ = lastSerializationErrors_.empty() ? SceneActivationStatus::FactoryReturnedNull
                                                                  : SceneActivationStatus::LoadFailed;
        return false;
    }

    nextScene->OnLoad();

    if (activeScene_)
    {
        activeScene_->OnExit();
        activeScene_->OnUnload();
        activeScene_.reset();
    }

    activeScene_ = std::move(nextScene);
    activeSceneId_ = std::move(request.Target);
    const SceneEnterContext enterContext(request.Payload);
    activeScene_->OnEnter(enterContext);
    lastActivationStatus_ = SceneActivationStatus::Succeeded;
    return true;
}

} // namespace Haki
