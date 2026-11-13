#include "Haki/Scripting/ScriptPaths.h"
#include "Haki/Scripting/ScriptSystem.h"

#include "Haki/Logging/Logger.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scripting/ScriptRuntime.h"
#include "Haki/Systems/PhysicsSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Haki
{
namespace
{

namespace fs = std::filesystem;

constexpr std::size_t MaximumStoredErrors = 32;

std::optional<fs::file_time_type> ModifiedTime(const fs::path &path)
{
    std::error_code error;
    const fs::file_time_type time = fs::last_write_time(path, error);
    return error ? std::nullopt : std::optional<fs::file_time_type>(time);
}

// Script and module paths stay inside the scripts root: relative, no "..", and a .lua extension.
bool ResolveScriptPath(const fs::path &root, const std::string &relative, fs::path &resolved, std::string &error)
{
    const fs::path path(relative);
    if (relative.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    {
        error = "script path '" + relative + "' must be relative to the scripts folder";
        return false;
    }
    for (const fs::path &part : path)
    {
        if (part == "..")
        {
            error = "script path '" + relative + "' must not leave the scripts folder";
            return false;
        }
    }
    if (path.extension() != ".lua")
    {
        error = "script path '" + relative + "' must end in .lua";
        return false;
    }
    resolved = ScriptSourcePath(root, relative);
    if (!fs::is_regular_file(resolved))
    {
        error = "script '" + resolved.generic_string() + "' does not exist";
        return false;
    }
    return true;
}

// "util.math" -> "util/math.lua". Module names are identifiers separated by dots.
bool ModuleNameToPath(const std::string &name, std::string &path)
{
    if (name.empty() || name.front() == '.' || name.back() == '.' || name.find("..") != std::string::npos)
    {
        return false;
    }
    path.clear();
    for (const char c : name)
    {
        if (c == '.')
        {
            path += '/';
        }
        else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
        {
            path += c;
        }
        else
        {
            return false;
        }
    }
    path += ".lua";
    return true;
}

sol::object CopyPropertyValue(sol::state &lua, const sol::object &value)
{
    // vec3 values are userdata: copy them so instances never share (and mutate) the class default.
    if (value.is<glm::vec3>())
    {
        return sol::make_object(lua, value.as<glm::vec3>());
    }
    if (value.get_type() == sol::type::table)
    {
        sol::table copy = lua.create_table();
        for (const auto &[key, element] : value.as<sol::table>())
        {
            copy[key] = CopyPropertyValue(lua, element);
        }
        return copy;
    }
    return value;
}

} // namespace

struct ScriptSystem::State
{
    struct ClassRecord
    {
        sol::table Class;        // the table the script returned; updated in place on reload
        sol::table InstanceMeta; // { __index = Class }
        std::optional<fs::file_time_type> Time;
        bool Loaded = false;
    };

    struct ModuleRecord
    {
        fs::path Source;
        std::optional<fs::file_time_type> Time;
    };

    struct Instance
    {
        std::string Path;
        entt::entity Entity = entt::null;
        sol::table Self;
        bool Started = false;
        bool Disabled = false;
    };

    // Declared first so it is destroyed last: every sol reference below points into it.
    sol::state Lua;
    ScriptRuntime Runtime;
    fs::path Root;
    std::string SceneScriptPath;

    std::unordered_map<std::string, ClassRecord> Classes;
    std::unordered_map<std::string, ModuleRecord> Modules;
    std::unordered_set<std::string> ModulesLoading;
    sol::table ModuleCache;

    std::unordered_map<entt::entity, Instance> Instances;
    std::optional<Instance> SceneInstance;
    std::unordered_set<entt::entity> Rejected; // scripts that failed to load; retried after a reload
    bool SceneScriptRejected = false;

    std::vector<std::string> Errors;

    void ReportError(std::string message)
    {
        Logger::Error() << "[script] " << message;
        Errors.push_back(std::move(message));
        if (Errors.size() > MaximumStoredErrors)
        {
            Errors.erase(Errors.begin(), Errors.begin() + static_cast<std::ptrdiff_t>(Errors.size() - MaximumStoredErrors));
        }
    }

    // Runs a script file and returns the table it produced.
    std::optional<sol::table> RunClassFile(const std::string &path, std::string &error)
    {
        fs::path resolved;
        if (!ResolveScriptPath(Root, path, resolved, error))
        {
            return std::nullopt;
        }
        sol::load_result chunk = Lua.load_file(resolved.generic_string());
        if (!chunk.valid())
        {
            const sol::error loadError = chunk;
            error = loadError.what();
            return std::nullopt;
        }
        sol::protected_function function = chunk;
        sol::protected_function_result result = function();
        if (!result.valid())
        {
            const sol::error runError = result;
            error = runError.what();
            return std::nullopt;
        }
        if (result.return_count() == 0 || result.get_type() != sol::type::table)
        {
            error = resolved.generic_string() + ": a behaviour script must return a table";
            return std::nullopt;
        }
        return result.get<sol::table>();
    }

    ClassRecord *GetClass(const std::string &path)
    {
        auto found = Classes.find(path);
        if (found != Classes.end())
        {
            return found->second.Loaded ? &found->second : nullptr;
        }
        ClassRecord &record = Classes[path];
        fs::path resolved;
        std::string error;
        if (ResolveScriptPath(Root, path, resolved, error))
        {
            record.Time = ModifiedTime(resolved);
        }
        const std::optional<sol::table> table = RunClassFile(path, error);
        if (!table)
        {
            ReportError(error);
            return nullptr;
        }
        record.Class = *table;
        record.InstanceMeta = Lua.create_table();
        record.InstanceMeta["__index"] = record.Class;
        record.Loaded = true;
        return &record;
    }

    // Re-runs one script; on success its new fields replace the old ones inside the same class table.
    bool ReloadClass(const std::string &path, ClassRecord &record)
    {
        fs::path resolved;
        std::string error;
        if (ResolveScriptPath(Root, path, resolved, error))
        {
            record.Time = ModifiedTime(resolved);
        }
        const std::optional<sol::table> table = RunClassFile(path, error);
        if (!table)
        {
            ReportError(error + " (keeping the previous version)");
            return false;
        }
        if (!record.Loaded)
        {
            record.Class = *table;
            record.InstanceMeta = Lua.create_table();
            record.InstanceMeta["__index"] = record.Class;
            record.Loaded = true;
            return true;
        }
        std::vector<sol::object> oldKeys;
        for (const auto &entry : record.Class)
        {
            oldKeys.push_back(entry.first);
        }
        for (const sol::object &key : oldKeys)
        {
            record.Class[key] = sol::lua_nil;
        }
        for (const auto &[key, value] : *table)
        {
            record.Class[key] = value;
        }
        return true;
    }

    std::optional<Instance> CreateInstance(const std::string &path, entt::entity entity,
                                           const std::unordered_map<std::string, ScriptValue> *overrides)
    {
        ClassRecord *record = GetClass(path);
        if (!record)
        {
            return std::nullopt;
        }
        sol::table self = Lua.create_table();
        if (entity != entt::null)
        {
            self["entity"] = LuaEntity{entity};
        }
        const sol::object defaults = record->Class["properties"];
        if (defaults.get_type() == sol::type::table)
        {
            for (const auto &[key, value] : defaults.as<sol::table>())
            {
                self[key] = CopyPropertyValue(Lua, value);
            }
        }
        if (overrides)
        {
            for (const auto &[name, value] : *overrides)
            {
                std::visit([&](const auto &v) { self[name] = v; }, value);
            }
        }
        self[sol::metatable_key] = record->InstanceMeta;
        return Instance{path, entity, self};
    }

    // Calls self:name(args...). A failure logs and disables the instance until its script is reloaded.
    template <typename... Args>
    void Call(Instance &instance, const char *name, Args &&...args)
    {
        if (instance.Disabled)
        {
            return;
        }
        const sol::object callback = instance.Self[name];
        if (callback.get_type() != sol::type::function)
        {
            return;
        }
        sol::protected_function function = callback;
        const sol::protected_function_result result = function(instance.Self, std::forward<Args>(args)...);
        if (!result.valid())
        {
            const sol::error error = result;
            std::string where = instance.Path;
            if (instance.Entity != entt::null && Runtime.Registry && Runtime.Registry->valid(instance.Entity))
            {
                if (const auto *id = Runtime.Registry->try_get<EntityIdComponent>(instance.Entity))
                {
                    where += " on '" + id->Id + "'";
                }
            }
            ReportError(where + " " + name + ": " + error.what() + " (disabled until the script is reloaded)");
            instance.Disabled = true;
        }
    }

    sol::object Require(const std::string &name)
    {
        const sol::object cached = ModuleCache[name];
        if (cached.valid() && cached.get_type() != sol::type::lua_nil)
        {
            return cached;
        }
        std::string relative;
        if (!ModuleNameToPath(name, relative))
        {
            throw std::runtime_error("invalid module name '" + name + "' (use dots, e.g. 'util.math')");
        }
        fs::path resolved;
        std::string error;
        if (!ResolveScriptPath(Root, relative, resolved, error))
        {
            throw std::runtime_error("module '" + name + "': " + error);
        }
        if (!ModulesLoading.insert(name).second)
        {
            throw std::runtime_error("circular require of module '" + name + "'");
        }
        struct LoadingGuard
        {
            std::unordered_set<std::string> &Set;
            std::string Name;
            ~LoadingGuard() { Set.erase(Name); }
        } guard{ModulesLoading, name};

        Modules[name] = {resolved, ModifiedTime(resolved)};
        sol::load_result chunk = Lua.load_file(resolved.generic_string());
        if (!chunk.valid())
        {
            const sol::error loadError = chunk;
            throw std::runtime_error(loadError.what());
        }
        sol::protected_function function = chunk;
        sol::protected_function_result result = function(name);
        if (!result.valid())
        {
            const sol::error runError = result;
            throw std::runtime_error(runError.what());
        }
        sol::object value = result.return_count() == 0 || result.get_type() == sol::type::lua_nil
                                ? sol::make_object(Lua, true)
                                : result.get<sol::object>();
        ModuleCache[name] = value;
        return value;
    }

    void FlushDestroys(entt::registry &registry)
    {
        // Destroying can run on_destroy callbacks that request more destroys; loop until settled.
        while (!Runtime.PendingDestroy.empty())
        {
            std::vector<entt::entity> pending;
            pending.swap(Runtime.PendingDestroy);
            for (const entt::entity entity : pending)
            {
                if (registry.valid(entity))
                {
                    registry.destroy(entity);
                }
            }
        }
    }
};

ScriptSystem::ScriptSystem() = default;

ScriptSystem::~ScriptSystem()
{
    Shutdown();
}

bool ScriptSystem::Initialize(entt::registry &registry, PhysicsSystem *physics, std::filesystem::path scriptRoot,
                              std::string sceneScript)
{
    Shutdown();

    auto state = std::make_unique<State>();
    // Sandboxed: no io, os, debug or package, and no loading files outside the scripts folder.
    state->Lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table,
                              sol::lib::coroutine, sol::lib::utf8);
    state->Lua["dofile"] = sol::lua_nil;
    state->Lua["loadfile"] = sol::lua_nil;
    state->Lua["load"] = sol::lua_nil;

    state->Runtime.ViewportAspect = viewportAspect_;
    state->Runtime.Registry = &registry;
    state->Runtime.Physics = physics;
    state->Root = std::move(scriptRoot);
    state->SceneScriptPath = std::move(sceneScript);
    state->ModuleCache = state->Lua.create_table();

    State *raw = state.get();
    state->Runtime.FindInstance = [raw](entt::entity entity) -> sol::object {
        const auto found = raw->Instances.find(entity);
        return found != raw->Instances.end() ? sol::object(found->second.Self) : sol::make_object(raw->Lua, sol::lua_nil);
    };
    RegisterScriptBindings(state->Lua, state->Runtime);
    state->Lua["require"] = [raw](const std::string &name) { return raw->Require(name); };

    state_ = std::move(state);
    registry_ = &registry;
    registry.on_destroy<ScriptComponent>().connect<&ScriptSystem::OnScriptDestroyed>(*this);
    return true;
}

void ScriptSystem::Shutdown()
{
    if (registry_)
    {
        registry_->on_destroy<ScriptComponent>().disconnect<&ScriptSystem::OnScriptDestroyed>(*this);
    }
    if (state_)
    {
        // Drop every Lua reference before the state itself goes away.
        state_->Instances.clear();
        state_->SceneInstance.reset();
        state_->Classes.clear();
        state_->ModuleCache = sol::table();
    }
    state_.reset();
    registry_ = nullptr;
}

void ScriptSystem::Update(entt::registry &registry, float deltaTime)
{
    if (!state_ || &registry != registry_)
    {
        return;
    }
    State &state = *state_;
    const float dt = std::isfinite(deltaTime) ? std::max(deltaTime, 0.0f) : 0.0f;
    state.Runtime.DeltaTime = dt;
    state.Runtime.Elapsed += dt;
    ++state.Runtime.Frame;
    UpdateScriptTime(state.Lua, state.Runtime);

    if (!state.SceneScriptPath.empty() && !state.SceneInstance && !state.SceneScriptRejected)
    {
        state.SceneInstance = state.CreateInstance(state.SceneScriptPath, entt::null, nullptr);
        state.SceneScriptRejected = !state.SceneInstance;
    }

    // Snapshot in registry order so callbacks run in a stable order and may safely request destroys.
    std::vector<entt::entity> scripted;
    {
        auto view = registry.view<ScriptComponent>();
        scripted.assign(view.begin(), view.end());
        std::reverse(scripted.begin(), scripted.end()); // EnTT iterates newest first; run oldest first
    }
    for (const entt::entity entity : scripted)
    {
        if (state.Instances.contains(entity) || state.Rejected.contains(entity))
        {
            continue;
        }
        const ScriptComponent &component = registry.get<ScriptComponent>(entity);
        if (std::optional<State::Instance> instance = state.CreateInstance(component.Path, entity, &component.Properties))
        {
            state.Instances.emplace(entity, std::move(*instance));
        }
        else
        {
            state.Rejected.insert(entity);
        }
    }

    if (state.SceneInstance && !state.SceneInstance->Started)
    {
        state.SceneInstance->Started = true;
        state.Call(*state.SceneInstance, "on_start");
    }
    for (const entt::entity entity : scripted)
    {
        auto found = state.Instances.find(entity);
        if (found != state.Instances.end() && !found->second.Started)
        {
            found->second.Started = true;
            state.Call(found->second, "on_start");
        }
    }

    if (state.SceneInstance)
    {
        state.Call(*state.SceneInstance, "on_update", dt);
    }
    for (const entt::entity entity : scripted)
    {
        auto found = state.Instances.find(entity);
        if (found != state.Instances.end() && registry.valid(entity))
        {
            state.Call(found->second, "on_update", dt);
        }
    }
    state.FlushDestroys(registry);
}

void ScriptSystem::DispatchContacts(entt::registry &registry)
{
    if (!state_ || &registry != registry_ || !state_->Runtime.Physics)
    {
        return;
    }
    State &state = *state_;
    const std::vector<PhysicsContactEvent> events = state.Runtime.Physics->GetContactEvents();
    for (const PhysicsContactEvent &event : events)
    {
        const bool began = event.Type == PhysicsContactType::Began;
        const char *callback = event.IsSensor ? (began ? "on_trigger_enter" : "on_trigger_exit")
                                              : (began ? "on_contact_begin" : "on_contact_end");
        const std::pair<entt::entity, entt::entity> pairs[] = {{event.First, event.Second},
                                                               {event.Second, event.First}};
        for (const auto &[self, other] : pairs)
        {
            if (self == entt::null || !registry.valid(self))
            {
                continue;
            }
            auto found = state.Instances.find(self);
            if (found == state.Instances.end() || !found->second.Started)
            {
                continue;
            }
            state.Call(found->second, callback, LuaEntity{other});
        }
        if (state.SceneInstance)
        {
            // The scene script sees every contact: on_contact_begin(a, b) etc.
            state.Call(*state.SceneInstance, callback, LuaEntity{event.First}, LuaEntity{event.Second});
        }
    }
    state.FlushDestroys(registry);
}

ScriptReloadResult ScriptSystem::ReloadChanged(bool force)
{
    ScriptReloadResult result;
    if (!state_)
    {
        return result;
    }
    State &state = *state_;

    // Scripts capture the modules they require, so an edited module reloads every script.
    bool modulesChanged = false;
    for (const auto &[name, module] : state.Modules)
    {
        if (ModifiedTime(module.Source) != module.Time)
        {
            modulesChanged = true;
            break;
        }
    }
    if (force || modulesChanged)
    {
        state.Modules.clear();
        state.ModuleCache = state.Lua.create_table();
    }

    for (auto &[path, record] : state.Classes)
    {
        fs::path resolved;
        std::string error;
        const bool exists = ResolveScriptPath(state.Root, path, resolved, error);
        const bool changed = !exists || ModifiedTime(resolved) != record.Time;
        if (!force && !modulesChanged && !changed)
        {
            continue;
        }
        if (!exists && !record.Time)
        {
            continue; // still missing; already reported
        }
        if (state.ReloadClass(path, record))
        {
            ++result.Reloaded;
            Logger::Info() << "[script] reloaded " << path;
            for (auto &[entity, instance] : state.Instances)
            {
                if (instance.Path == path)
                {
                    instance.Disabled = false;
                }
            }
            if (state.SceneInstance && state.SceneInstance->Path == path)
            {
                state.SceneInstance->Disabled = false;
            }
        }
        else
        {
            ++result.Failed;
        }
    }
    if (force || result.Reloaded > 0)
    {
        state.Rejected.clear();
        state.SceneScriptRejected = false;
    }
    return result;
}

void ScriptSystem::SetViewportSize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    viewportAspect_ = static_cast<float>(width) / static_cast<float>(height);
    if (state_) state_->Runtime.ViewportAspect = viewportAspect_;
}

void ScriptSystem::SetInputEnabled(bool enabled, bool mouseEnabled)
{
    if (state_)
    {
        state_->Runtime.InputEnabled = enabled;
        state_->Runtime.MouseEnabled = mouseEnabled;
    }
}

bool ScriptSystem::Execute(const std::string &code, const std::string &chunkName)
{
    if (!state_)
    {
        return false;
    }
    const sol::protected_function_result result =
        state_->Lua.safe_script(code, sol::script_pass_on_error, "=" + chunkName);
    if (!result.valid())
    {
        const sol::error error = result;
        state_->ReportError(error.what());
        return false;
    }
    if (registry_)
    {
        state_->FlushDestroys(*registry_);
    }
    return true;
}

std::size_t ScriptSystem::GetLoadedScriptCount() const
{
    if (!state_)
    {
        return 0;
    }
    return static_cast<std::size_t>(std::count_if(state_->Classes.begin(), state_->Classes.end(),
                                                  [](const auto &entry) { return entry.second.Loaded; }));
}

std::size_t ScriptSystem::GetInstanceCount() const
{
    if (!state_)
    {
        return 0;
    }
    return state_->Instances.size() + (state_->SceneInstance ? 1 : 0);
}

const std::vector<std::string> &ScriptSystem::GetErrors() const
{
    static const std::vector<std::string> empty;
    return state_ ? state_->Errors : empty;
}

void ScriptSystem::ClearErrors()
{
    if (state_)
    {
        state_->Errors.clear();
    }
}

std::vector<std::pair<std::string, ScriptValue>> ScriptSystem::GetInstanceProperties(entt::entity entity) const
{
    std::vector<std::pair<std::string, ScriptValue>> properties;
    if (!state_)
    {
        return properties;
    }
    const auto found = state_->Instances.find(entity);
    if (found == state_->Instances.end())
    {
        return properties;
    }
    const auto record = state_->Classes.find(found->second.Path);
    if (record == state_->Classes.end())
    {
        return properties;
    }
    const sol::object defaults = record->second.Class["properties"];
    if (defaults.get_type() != sol::type::table)
    {
        return properties;
    }
    const sol::table &self = found->second.Self;
    for (const auto &[key, unused] : defaults.as<sol::table>())
    {
        if (key.get_type() != sol::type::string)
        {
            continue;
        }
        const std::string name = key.as<std::string>();
        const sol::object value = self.raw_get<sol::object>(name);
        switch (value.get_type())
        {
        case sol::type::boolean: properties.emplace_back(name, value.as<bool>()); break;
        case sol::type::number: properties.emplace_back(name, value.as<double>()); break;
        case sol::type::string: properties.emplace_back(name, value.as<std::string>()); break;
        case sol::type::userdata:
            if (value.is<glm::vec3>()) properties.emplace_back(name, value.as<glm::vec3>());
            break;
        default: break;
        }
    }
    std::sort(properties.begin(), properties.end(),
              [](const auto &left, const auto &right) { return left.first < right.first; });
    return properties;
}

bool ScriptSystem::SetInstanceProperty(entt::entity entity, const std::string &name, const ScriptValue &value)
{
    if (!state_)
    {
        return false;
    }
    auto found = state_->Instances.find(entity);
    if (found == state_->Instances.end())
    {
        return false;
    }
    std::visit([&](const auto &v) { found->second.Self[name] = v; }, value);
    return true;
}

void ScriptSystem::OnScriptDestroyed(entt::registry & /*registry*/, entt::entity entity)
{
    if (!state_)
    {
        return;
    }
    state_->Rejected.erase(entity);
    auto found = state_->Instances.find(entity);
    if (found == state_->Instances.end())
    {
        return;
    }
    State::Instance instance = std::move(found->second);
    state_->Instances.erase(found);
    if (instance.Started)
    {
        state_->Call(instance, "on_destroy");
    }
}

} // namespace Haki
