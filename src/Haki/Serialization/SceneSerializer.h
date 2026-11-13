#pragma once

#include "Haki/Scene/SceneDescription.h"
#include "Haki/Serialization/SerializationError.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace Haki
{

class AssetCatalog;
class Scene;

struct SceneLoadResult
{
    std::unique_ptr<Scene> Value;
    std::vector<SerializationError> Errors;

    explicit operator bool() const { return Value != nullptr && Errors.empty(); }
};

class SceneSerializer
{
public:
    explicit SceneSerializer(std::shared_ptr<AssetCatalog> catalog) : catalog_(std::move(catalog)) {}

    bool LoadManifest(const std::filesystem::path &source, SceneManifest &manifest,
                      std::vector<SerializationError> &errors);
    SceneLoadResult Deserialize(const std::filesystem::path &source) const;

    // Folder that script paths in scene files are relative to (default "Assets/Scripts").
    void SetScriptRoot(std::filesystem::path root) { scriptRoot_ = std::move(root); }
    const std::filesystem::path &GetScriptRoot() const { return scriptRoot_; }

private:
    std::shared_ptr<AssetCatalog> catalog_;
    std::filesystem::path scriptRoot_ = "Assets/Scripts";
};

} // namespace Haki
