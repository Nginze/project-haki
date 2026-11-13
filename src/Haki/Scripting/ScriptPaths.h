#pragma once

#include <filesystem>
#include <string>

namespace Haki
{

// Call after validating the relative Lua path. Shared scripts are explicitly namespaced,
// so a scene-local file cannot accidentally shadow a shared behaviour/module.
inline std::filesystem::path ScriptSourcePath(const std::filesystem::path &root, const std::string &relative)
{
    if (relative.starts_with("shared/"))
    {
        return (root.parent_path().parent_path().parent_path() / "Shared/Scripts" / relative.substr(7))
            .lexically_normal();
    }
    return (root / relative).lexically_normal();
}

} // namespace Haki
