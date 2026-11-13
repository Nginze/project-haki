#pragma once

#include "Haki/AssetManager/AssetTypes.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Haki
{

enum class BlendMode
{
    Opaque,
    AlphaBlend
};

enum class MaterialPass
{
    Default,
    Transmission
};

enum class CullMode
{
    None,
    Back,
    Front
};

struct MaterialState
{
    MaterialPass Pass = MaterialPass::Default;
    BlendMode Blend = BlendMode::Opaque;
    CullMode Cull = CullMode::Back;
    bool DepthTest = true;
    bool DepthWrite = true;
    bool Wireframe = false;
};

using MaterialParameter =
    std::variant<int, float, glm::vec2, glm::vec3, glm::vec4, glm::mat3, glm::mat4, TextureHandle>;

class Material
{
public:
    Material() = default;
    explicit Material(ShaderHandle shader) : shader_(shader) {}

    void SetShader(ShaderHandle shader) { shader_ = shader; }
    ShaderHandle GetShader() const { return shader_; }

    void SetState(const MaterialState &state) { state_ = state; }
    const MaterialState &GetState() const { return state_; }

    void SetParameter(std::string name, MaterialParameter value)
    {
        parameters_.insert_or_assign(std::move(name), std::move(value));
    }
    bool RemoveParameter(std::string_view name) { return parameters_.erase(std::string(name)) != 0; }
    const std::unordered_map<std::string, MaterialParameter> &GetParameters() const { return parameters_; }

private:
    ShaderHandle shader_;
    MaterialState state_;
    std::unordered_map<std::string, MaterialParameter> parameters_;
};

} // namespace Haki
