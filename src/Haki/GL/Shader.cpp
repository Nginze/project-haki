#include "Haki/GL/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace Haki
{
namespace
{

GLuint CompileStage(GLenum type, std::string_view source, std::string &diagnostic)
{
    const GLuint shader = glCreateShader(type);
    const char *text = source.data();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint succeeded = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &succeeded);
    if (succeeded == GL_TRUE)
    {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    diagnostic.resize(static_cast<std::size_t>(std::max(logLength, 1)));
    glGetShaderInfoLog(shader, logLength, nullptr, diagnostic.data());
    glDeleteShader(shader);
    return 0;
}

ShaderValueType ToValueType(GLenum type)
{
    switch (type)
    {
    case GL_INT:
        return ShaderValueType::Int;
    case GL_BOOL:
        return ShaderValueType::Bool;
    case GL_FLOAT:
        return ShaderValueType::Float;
    case GL_FLOAT_VEC2:
        return ShaderValueType::Vec2;
    case GL_FLOAT_VEC3:
        return ShaderValueType::Vec3;
    case GL_FLOAT_VEC4:
        return ShaderValueType::Vec4;
    case GL_FLOAT_MAT3:
        return ShaderValueType::Mat3;
    case GL_FLOAT_MAT4:
        return ShaderValueType::Mat4;
    case GL_SAMPLER_2D:
        return ShaderValueType::Sampler2D;
    case GL_SAMPLER_CUBE:
        return ShaderValueType::SamplerCube;
    default:
        return ShaderValueType::Unknown;
    }
}

std::string NormalizeUniformName(std::string name)
{
    constexpr std::string_view suffix = "[0]";
    if (name.size() >= suffix.size() && name.ends_with(suffix))
    {
        name.resize(name.size() - suffix.size());
    }
    return name;
}

} // namespace

Shader::~Shader()
{
    Reset();
}

Shader::Shader(Shader &&other) noexcept
    : program_(std::exchange(other.program_, 0)), uniforms_(std::move(other.uniforms_)),
      blocks_(std::move(other.blocks_)), attributes_(std::move(other.attributes_)),
      uniformLookup_(std::move(other.uniformLookup_)), blockLookup_(std::move(other.blockLookup_))
{
}

Shader &Shader::operator=(Shader &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        program_ = std::exchange(other.program_, 0);
        uniforms_ = std::move(other.uniforms_);
        blocks_ = std::move(other.blocks_);
        attributes_ = std::move(other.attributes_);
        uniformLookup_ = std::move(other.uniformLookup_);
        blockLookup_ = std::move(other.blockLookup_);
    }
    return *this;
}

bool Shader::Initialize(const ShaderDescriptor &descriptor, std::string &diagnostic)
{
    Reset();
    diagnostic.clear();
    if (descriptor.VertexSource.empty() || descriptor.FragmentSource.empty())
    {
        diagnostic = "Vertex and fragment shader sources are required";
        return false;
    }

    std::string stageDiagnostic;
    const GLuint vertex = CompileStage(GL_VERTEX_SHADER, descriptor.VertexSource, stageDiagnostic);
    if (vertex == 0)
    {
        diagnostic = "Vertex shader compilation failed: " + stageDiagnostic;
        return false;
    }

    const GLuint fragment = CompileStage(GL_FRAGMENT_SHADER, descriptor.FragmentSource, stageDiagnostic);
    if (fragment == 0)
    {
        glDeleteShader(vertex);
        diagnostic = "Fragment shader compilation failed: " + stageDiagnostic;
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint succeeded = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &succeeded);
    if (succeeded != GL_TRUE)
    {
        GLint logLength = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLength);
        diagnostic.resize(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetProgramInfoLog(program_, logLength, nullptr, diagnostic.data());
        Reset();
        return false;
    }

    Reflect();
    BindStandardBlocks();
    return true;
}

void Shader::Bind() const
{
    glUseProgram(program_);
}

void Shader::Reset()
{
    if (program_ != 0)
    {
        glDeleteProgram(program_);
    }
    program_ = 0;
    uniforms_.clear();
    blocks_.clear();
    attributes_.clear();
    uniformLookup_.clear();
    blockLookup_.clear();
}

const ShaderUniform *Shader::FindUniform(std::string_view name) const
{
    const auto found = uniformLookup_.find(std::string(name));
    return found != uniformLookup_.end() ? &uniforms_[found->second] : nullptr;
}

const ShaderUniformBlock *Shader::FindUniformBlock(std::string_view name) const
{
    const auto found = blockLookup_.find(std::string(name));
    return found != blockLookup_.end() ? &blocks_[found->second] : nullptr;
}

bool Shader::Set(std::string_view name, int value) const
{
    const ShaderUniform *uniform = FindUniform(name);
    if (!uniform || (uniform->Type != ShaderValueType::Int && uniform->Type != ShaderValueType::Bool &&
                      uniform->Type != ShaderValueType::Sampler2D && uniform->Type != ShaderValueType::SamplerCube))
    {
        return false;
    }
    glUniform1i(uniform->Location, value);
    return true;
}

bool Shader::Set(std::string_view name, float value) const
{
    const ShaderUniform *uniform = FindUniform(name);
    if (!uniform || uniform->Type != ShaderValueType::Float)
    {
        return false;
    }
    glUniform1f(uniform->Location, value);
    return true;
}

bool Shader::Set(std::string_view name, const glm::vec2 &value) const
{
    const ShaderUniform *uniform = FindUniform(name);
    if (!uniform || uniform->Type != ShaderValueType::Vec2)
    {
        return false;
    }
    glUniform2fv(uniform->Location, 1, glm::value_ptr(value));
    return true;
}

bool Shader::Set(std::string_view name, const glm::vec3 &value) const
{
    const ShaderUniform *uniform = FindUniform(name);
    if (!uniform || uniform->Type != ShaderValueType::Vec3)
    {
        return false;
    }
    glUniform3fv(uniform->Location, 1, glm::value_ptr(value));
    return true;
}

bool Shader::Set(std::string_view name, const glm::vec4 &value) const
{
    const ShaderUniform *uniform = FindUniform(name);
    if (!uniform || uniform->Type != ShaderValueType::Vec4)
    {
        return false;
    }
    glUniform4fv(uniform->Location, 1, glm::value_ptr(value));
    return true;
}

bool Shader::Set(std::string_view name, const glm::mat3 &value) const
{
    const ShaderUniform *uniform = FindUniform(name);
    if (!uniform || uniform->Type != ShaderValueType::Mat3)
    {
        return false;
    }
    glUniformMatrix3fv(uniform->Location, 1, GL_FALSE, glm::value_ptr(value));
    return true;
}

bool Shader::Set(std::string_view name, const glm::mat4 &value) const
{
    const ShaderUniform *uniform = FindUniform(name);
    if (!uniform || uniform->Type != ShaderValueType::Mat4)
    {
        return false;
    }
    glUniformMatrix4fv(uniform->Location, 1, GL_FALSE, glm::value_ptr(value));
    return true;
}

void Shader::Reflect()
{
    GLint uniformCount = 0;
    GLint maxUniformName = 0;
    glGetProgramiv(program_, GL_ACTIVE_UNIFORMS, &uniformCount);
    glGetProgramiv(program_, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxUniformName);
    std::vector<char> name(static_cast<std::size_t>(std::max(maxUniformName, 1)));

    std::int32_t nextTextureUnit = 0;
    for (GLint index = 0; index < uniformCount; ++index)
    {
        GLsizei length = 0;
        GLint arraySize = 0;
        GLenum type = 0;
        glGetActiveUniform(program_, static_cast<GLuint>(index), static_cast<GLsizei>(name.size()), &length, &arraySize,
                           &type, name.data());
        std::string uniformName = NormalizeUniformName(std::string(name.data(), static_cast<std::size_t>(length)));
        const GLint location = glGetUniformLocation(program_, uniformName.c_str());
        if (location < 0)
        {
            continue;
        }

        ShaderUniform uniform{uniformName, ToValueType(type), location, arraySize, -1};
        if (uniform.Type == ShaderValueType::Sampler2D || uniform.Type == ShaderValueType::SamplerCube)
        {
            uniform.TextureUnit = nextTextureUnit++;
        }
        uniformLookup_.emplace(uniform.Name, uniforms_.size());
        uniforms_.push_back(std::move(uniform));
    }

    GLint blockCount = 0;
    GLint maxBlockName = 0;
    glGetProgramiv(program_, GL_ACTIVE_UNIFORM_BLOCKS, &blockCount);
    glGetProgramiv(program_, GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH, &maxBlockName);
    name.resize(static_cast<std::size_t>(std::max(maxBlockName, 1)));
    for (GLint index = 0; index < blockCount; ++index)
    {
        GLsizei length = 0;
        GLint size = 0;
        glGetActiveUniformBlockName(program_, static_cast<GLuint>(index), static_cast<GLsizei>(name.size()), &length,
                                    name.data());
        glGetActiveUniformBlockiv(program_, static_cast<GLuint>(index), GL_UNIFORM_BLOCK_DATA_SIZE, &size);
        ShaderUniformBlock block{std::string(name.data(), static_cast<std::size_t>(length)),
                                 static_cast<std::uint32_t>(index), size};
        blockLookup_.emplace(block.Name, blocks_.size());
        blocks_.push_back(std::move(block));
    }

    GLint attributeCount = 0;
    GLint maxAttributeName = 0;
    glGetProgramiv(program_, GL_ACTIVE_ATTRIBUTES, &attributeCount);
    glGetProgramiv(program_, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxAttributeName);
    name.resize(static_cast<std::size_t>(std::max(maxAttributeName, 1)));
    for (GLint index = 0; index < attributeCount; ++index)
    {
        GLsizei length = 0;
        GLint arraySize = 0;
        GLenum type = 0;
        glGetActiveAttrib(program_, static_cast<GLuint>(index), static_cast<GLsizei>(name.size()), &length, &arraySize,
                          &type, name.data());
        std::string attributeName(name.data(), static_cast<std::size_t>(length));
        attributes_.push_back(
            {attributeName, ToValueType(type), glGetAttribLocation(program_, attributeName.c_str()), arraySize});
    }
}

void Shader::BindStandardBlocks()
{
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 4> standardBlocks{{
        {"HakiCamera", 0},
        {"HakiLights", 1},
        {"HakiObject", 2},
        {"HakiSkin", 3},
    }};

    for (const auto &[name, binding] : standardBlocks)
    {
        if (const ShaderUniformBlock *block = FindUniformBlock(name))
        {
            glUniformBlockBinding(program_, block->Index, binding);
        }
    }

    Bind();
    for (const ShaderUniform &uniform : uniforms_)
    {
        if (uniform.Type == ShaderValueType::Sampler2D || uniform.Type == ShaderValueType::SamplerCube)
        {
            glUniform1i(uniform.Location, uniform.TextureUnit);
        }
    }
}

} // namespace Haki
