#include "Haki/AssetLoader/MaterialFileLoader.h"

#include "Haki/AssetManager/AssetCatalog.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/GL/Shader.h"
#include "Haki/Serialization/JsonUtils.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Haki
{
namespace
{

template <glm::length_t Length>
bool ParseVector(const JsonUtils::Json &value, glm::vec<Length, float> &result)
{
    if (!value.is_array() || value.size() != Length ||
        !std::all_of(value.begin(), value.end(), [](const auto &part) { return part.is_number(); }))
    {
        return false;
    }
    for (glm::length_t index = 0; index < Length; ++index)
    {
        result[index] = value[index].template get<float>();
    }
    return true;
}

// Column-major 3x3 matrix from nine numbers (same layout as glTF / GLSL).
bool ParseMatrix3(const JsonUtils::Json &value, glm::mat3 &result)
{
    if (!value.is_array() || value.size() != 9 ||
        !std::all_of(value.begin(), value.end(), [](const auto &part) { return part.is_number(); }))
    {
        return false;
    }
    for (int column = 0; column < 3; ++column)
    {
        for (int row = 0; row < 3; ++row)
        {
            result[column][row] = value[static_cast<std::size_t>(column * 3 + row)].template get<float>();
        }
    }
    return true;
}

std::string FirstError(const std::vector<SerializationError> &errors)
{
    if (errors.empty())
    {
        return "Invalid material document";
    }
    return errors.front().JsonPointer + ": " + errors.front().Message;
}

} // namespace

bool MaterialFileLoader::Supports(const AssetLoadRequest &request) const
{
    return request.Source.filename().string().ends_with(".material.json");
}

AssetLoaderResult<Material> MaterialFileLoader::Load(const AssetLoadRequest &request)
{
    std::vector<SerializationError> errors;
    JsonUtils::Json document;
    if (!JsonUtils::ReadFile(request.Source, document, errors))
    {
        return {nullptr, AssetLoadError::LoaderFailed, FirstError(errors)};
    }
    bool valid = JsonUtils::ValidateFields(document, {"type", "version", "shader", "state", "parameters"},
                                           request.Source, "", errors);
    valid = JsonUtils::ValidateHeader(document, "haki.material", request.Source, errors) && valid;
    if (!document.contains("shader") || !document["shader"].is_string())
    {
        JsonUtils::AddError(errors, request.Source, "/shader", "Expected a logical shader ID");
        valid = false;
    }
    if (!valid)
    {
        return {nullptr, AssetLoadError::LoaderFailed, FirstError(errors)};
    }

    const std::string shaderId = document["shader"].get<std::string>();
    const AssetCatalogEntry *shaderEntry = catalog_->Find(shaderId);
    if (!shaderEntry || shaderEntry->Type != CatalogAssetType::Shader)
    {
        return {nullptr, AssetLoadError::LoaderFailed, "Unknown shader asset '" + shaderId + "'"};
    }
    const auto shaderResult = AssetManager::LoadShader({shaderEntry->RuntimeId, shaderEntry->Source});
    if (!shaderResult)
    {
        return {nullptr, shaderResult.Error, shaderResult.Message};
    }
    const Shader *shader = AssetManager::GetShader(shaderResult.Value);

    auto material = std::make_unique<Material>(shaderResult.Value);
    MaterialState state;
    if (document.contains("state"))
    {
        const auto &jsonState = document["state"];
        valid = JsonUtils::ValidateFields(jsonState, {"pass", "blend", "cull", "depthTest", "depthWrite", "wireframe"},
                                           request.Source, "/state", errors) && valid;
        if (!jsonState.is_object())
        {
            valid = false;
        }
        else
        {
            std::string blend = "opaque";
            std::string pass = "default";
            std::string cull = "back";
            if (jsonState.contains("pass") && jsonState["pass"].is_string())
                pass = jsonState["pass"].get<std::string>();
            else if (jsonState.contains("pass")) valid = false;
            if (jsonState.contains("blend") && jsonState["blend"].is_string())
                blend = jsonState["blend"].get<std::string>();
            else if (jsonState.contains("blend")) valid = false;
            if (jsonState.contains("cull") && jsonState["cull"].is_string())
                cull = jsonState["cull"].get<std::string>();
            else if (jsonState.contains("cull")) valid = false;
            if (blend == "opaque")
                state.Blend = BlendMode::Opaque;
            else if (blend == "alpha")
                state.Blend = BlendMode::AlphaBlend;
            else
            {
                JsonUtils::AddError(errors, request.Source, "/state/blend", "Expected 'opaque' or 'alpha'");
                valid = false;
            }
            if (pass == "default")
                state.Pass = MaterialPass::Default;
            else if (pass == "transmission")
                state.Pass = MaterialPass::Transmission;
            else
            {
                JsonUtils::AddError(errors, request.Source, "/state/pass", "Expected 'default' or 'transmission'");
                valid = false;
            }
            if (cull == "none")
                state.Cull = CullMode::None;
            else if (cull == "back")
                state.Cull = CullMode::Back;
            else if (cull == "front")
                state.Cull = CullMode::Front;
            else
            {
                JsonUtils::AddError(errors, request.Source, "/state/cull", "Expected 'none', 'back', or 'front'");
                valid = false;
            }
            if (jsonState.contains("depthTest") && jsonState["depthTest"].is_boolean())
                state.DepthTest = jsonState["depthTest"].get<bool>();
            else if (jsonState.contains("depthTest")) valid = false;
            if (jsonState.contains("depthWrite") && jsonState["depthWrite"].is_boolean())
                state.DepthWrite = jsonState["depthWrite"].get<bool>();
            else if (jsonState.contains("depthWrite")) valid = false;
            if (jsonState.contains("wireframe") && jsonState["wireframe"].is_boolean())
                state.Wireframe = jsonState["wireframe"].get<bool>();
            else if (jsonState.contains("wireframe")) valid = false;
        }
    }
    material->SetState(state);

    if (document.contains("parameters"))
    {
        if (!document["parameters"].is_object())
        {
            JsonUtils::AddError(errors, request.Source, "/parameters", "Expected an object");
            valid = false;
        }
        else
        {
            for (const auto &[name, parameter] : document["parameters"].items())
            {
                const std::string pointer = "/parameters/" + name;
                valid = JsonUtils::ValidateFields(parameter, {"type", "value"}, request.Source, pointer, errors) && valid;
                if (!parameter.is_object() || !parameter.contains("type") || !parameter["type"].is_string() ||
                    !parameter.contains("value"))
                {
                    JsonUtils::AddError(errors, request.Source, pointer, "Parameter requires type and value");
                    valid = false;
                    continue;
                }
                const std::string type = parameter["type"].get<std::string>();
                const auto &value = parameter["value"];
                const ShaderUniform *uniform = shader ? shader->FindUniform(name) : nullptr;
                ShaderValueType expectedType = ShaderValueType::Unknown;
                if (type == "int") expectedType = ShaderValueType::Int;
                else if (type == "float") expectedType = ShaderValueType::Float;
                else if (type == "vec2") expectedType = ShaderValueType::Vec2;
                else if (type == "vec3") expectedType = ShaderValueType::Vec3;
                else if (type == "vec4") expectedType = ShaderValueType::Vec4;
                else if (type == "mat3") expectedType = ShaderValueType::Mat3;
                else if (type == "texture2D") expectedType = ShaderValueType::Sampler2D;
                if (!uniform || (uniform->Type != expectedType &&
                                 !(expectedType == ShaderValueType::Int && uniform->Type == ShaderValueType::Bool)))
                {
                    JsonUtils::AddError(errors, request.Source, pointer,
                                        "Parameter does not match an active shader uniform of the same type");
                    valid = false;
                    continue;
                }
                if (type == "int" && value.is_number_integer())
                    material->SetParameter(name, value.get<int>());
                else if (type == "float" && value.is_number())
                    material->SetParameter(name, value.get<float>());
                else if (type == "vec2")
                {
                    glm::vec2 parsed;
                    if (ParseVector(value, parsed)) material->SetParameter(name, parsed);
                    else
                    {
                        JsonUtils::AddError(errors, request.Source, pointer + "/value", "Expected two numbers");
                        valid = false;
                    }
                }
                else if (type == "vec3")
                {
                    glm::vec3 parsed;
                    if (ParseVector(value, parsed)) material->SetParameter(name, parsed);
                    else
                    {
                        JsonUtils::AddError(errors, request.Source, pointer + "/value", "Expected three numbers");
                        valid = false;
                    }
                }
                else if (type == "vec4")
                {
                    glm::vec4 parsed;
                    if (ParseVector(value, parsed)) material->SetParameter(name, parsed);
                    else
                    {
                        JsonUtils::AddError(errors, request.Source, pointer + "/value", "Expected four numbers");
                        valid = false;
                    }
                }
                else if (type == "mat3")
                {
                    glm::mat3 parsed(1.0f);
                    if (ParseMatrix3(value, parsed)) material->SetParameter(name, parsed);
                    else
                    {
                        JsonUtils::AddError(errors, request.Source, pointer + "/value",
                                            "Expected nine numbers (column-major 3x3)");
                        valid = false;
                    }
                }
                else if (type == "texture2D" && value.is_string())
                {
                    const std::string textureId = value.get<std::string>();
                    const AssetCatalogEntry *textureEntry = catalog_->Find(textureId);
                    if (!textureEntry || textureEntry->Type != CatalogAssetType::Texture)
                    {
                        JsonUtils::AddError(errors, request.Source, pointer + "/value",
                                            "Unknown texture asset '" + textureId + "'");
                        valid = false;
                    }
                    else
                    {
                        const auto texture = AssetManager::LoadTexture({textureEntry->RuntimeId, textureEntry->Source});
                        if (texture) material->SetParameter(name, texture.Value);
                        else
                        {
                            JsonUtils::AddError(errors, request.Source, pointer + "/value", texture.Message);
                            valid = false;
                        }
                    }
                }
                else
                {
                    JsonUtils::AddError(errors, request.Source, pointer, "Unsupported parameter type or value");
                    valid = false;
                }
            }
        }
    }
    if (!valid)
    {
        return {nullptr, AssetLoadError::LoaderFailed, FirstError(errors)};
    }
    return {std::move(material), AssetLoadError::None, {}};
}

} // namespace Haki
