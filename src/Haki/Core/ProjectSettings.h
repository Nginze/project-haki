#pragma once
#include "Haki/Renderer/RendererTypes.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

namespace Haki {
struct ProjectSettings {
    std::string Title = "Sandbox";
    int Width = 1280, Height = 720;
    bool Fullscreen = false, Maximized = false, Vsync = true, Autosave = true;
    RendererSettings Rendering;
    static ProjectSettings Load(const std::filesystem::path &path, ProjectSettings defaults);
    nlohmann::json Sections() const;
    bool Save(const std::filesystem::path &path, std::string &error) const;
};
}
