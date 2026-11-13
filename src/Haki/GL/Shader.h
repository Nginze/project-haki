#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Haki
{

struct ShaderDescriptor
{
    std::string_view VertexSource;
    std::string_view FragmentSource;
};

enum class ShaderValueType
{
    Unknown,
    Int,
    Bool,
    Float,
    Vec2,
    Vec3,
    Vec4,
    Mat3,
    Mat4,
    Sampler2D,
    SamplerCube
};

struct ShaderUniform
{
    std::string Name;
    ShaderValueType Type = ShaderValueType::Unknown;
    std::int32_t Location = -1;
    std::int32_t ArraySize = 1;
    std::int32_t TextureUnit = -1;
};

struct ShaderUniformBlock
{
    std::string Name;
    std::uint32_t Index = 0;
    std::int32_t Size = 0;
};

struct ShaderAttribute
{
    std::string Name;
    ShaderValueType Type = ShaderValueType::Unknown;
    std::int32_t Location = -1;
    std::int32_t ArraySize = 1;
};

class Shader
{
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;
    Shader(Shader &&other) noexcept;
    Shader &operator=(Shader &&other) noexcept;

    bool Initialize(const ShaderDescriptor &descriptor, std::string &diagnostic);
    void Bind() const;
    void Reset();

    bool Set(std::string_view name, int value) const;
    bool Set(std::string_view name, float value) const;
    bool Set(std::string_view name, const glm::vec2 &value) const;
    bool Set(std::string_view name, const glm::vec3 &value) const;
    bool Set(std::string_view name, const glm::vec4 &value) const;
    bool Set(std::string_view name, const glm::mat3 &value) const;
    bool Set(std::string_view name, const glm::mat4 &value) const;

    bool IsValid() const { return program_ != 0; }
    const ShaderUniform *FindUniform(std::string_view name) const;
    const ShaderUniformBlock *FindUniformBlock(std::string_view name) const;
    const std::vector<ShaderUniform> &GetUniforms() const { return uniforms_; }
    const std::vector<ShaderUniformBlock> &GetUniformBlocks() const { return blocks_; }
    const std::vector<ShaderAttribute> &GetAttributes() const { return attributes_; }

private:
    void Reflect();
    void BindStandardBlocks();

    std::uint32_t program_ = 0;
    std::vector<ShaderUniform> uniforms_;
    std::vector<ShaderUniformBlock> blocks_;
    std::vector<ShaderAttribute> attributes_;
    std::unordered_map<std::string, std::size_t> uniformLookup_;
    std::unordered_map<std::string, std::size_t> blockLookup_;
};

} // namespace Haki
