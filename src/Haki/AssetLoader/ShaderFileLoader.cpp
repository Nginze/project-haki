#include "Haki/AssetLoader/ShaderFileLoader.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace Haki
{
namespace
{

bool ReadTextFile(const std::filesystem::path &path, std::string &contents)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }
    contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

} // namespace

bool ShaderFileLoader::Supports(const AssetLoadRequest &request) const
{
    return !request.Source.empty() && request.Source.extension().empty();
}

std::vector<std::filesystem::path> ShaderFileLoader::GetSourceFiles(const AssetLoadRequest &request) const
{
    std::filesystem::path vertexPath = request.Source;
    std::filesystem::path fragmentPath = request.Source;
    vertexPath.replace_extension(".vert");
    fragmentPath.replace_extension(".frag");
    return {vertexPath, fragmentPath};
}

AssetLoaderResult<Shader> ShaderFileLoader::Load(const AssetLoadRequest &request)
{
    const std::filesystem::path stem = request.Source;
    const std::vector<std::filesystem::path> sources = GetSourceFiles(request);
    const std::filesystem::path &vertexPath = sources[0];
    const std::filesystem::path &fragmentPath = sources[1];

    std::string vertexSource;
    std::string fragmentSource;
    if (!ReadTextFile(vertexPath, vertexSource))
    {
        return {nullptr, AssetLoadError::LoaderFailed, "Could not read vertex shader: " + vertexPath.string()};
    }
    if (!ReadTextFile(fragmentPath, fragmentSource))
    {
        return {nullptr, AssetLoadError::LoaderFailed, "Could not read fragment shader: " + fragmentPath.string()};
    }

    auto shader = std::make_unique<Shader>();
    std::string diagnostic;
    if (!shader->Initialize({vertexSource, fragmentSource}, diagnostic))
    {
        return {nullptr, AssetLoadError::LoaderFailed,
                "Could not build shader '" + stem.string() + "': " + diagnostic};
    }
    return {std::move(shader), AssetLoadError::None, {}};
}

} // namespace Haki
