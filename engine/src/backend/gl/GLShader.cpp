#include "engine/backend/gl/GLShader.h"
#include "engine/core/Log.h"

#include <glad/gl.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace engine {

static std::string ReadFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Failed to open shader file: " + path);

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

unsigned int GLShader::CompileStage(unsigned int stage, const std::string& source, const std::string& debugName)
{
    unsigned int handle = glCreateShader(stage);
    const char* src = source.c_str();
    glShaderSource(handle, 1, &src, nullptr);
    glCompileShader(handle);

    int success = 0;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        int len = 0;
        glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 0 ? len : 1);
        glGetShaderInfoLog(handle, len, nullptr, log.data());
        throw std::runtime_error("Shader compile error (" + debugName + "): " + log.data());
    }

    return handle;
}

GLShader::GLShader(const std::string& vertPath, const std::string& fragPath)
{
    const unsigned int vert = CompileStage(GL_VERTEX_SHADER, ReadFile(vertPath), vertPath);
    const unsigned int frag = CompileStage(GL_FRAGMENT_SHADER, ReadFile(fragPath), fragPath);

    m_program = glCreateProgram();
    glAttachShader(m_program, vert);
    glAttachShader(m_program, frag);
    glLinkProgram(m_program);

    int success = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success)
    {
        int len = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 0 ? len : 1);
        glGetProgramInfoLog(m_program, len, nullptr, log.data());
        glDeleteShader(vert);
        glDeleteShader(frag);
        throw std::runtime_error("Shader link error (" + vertPath + " / " + fragPath + "): " + log.data());
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    log::Info("Compiled shader program: " + vertPath + " + " + fragPath);
}

GLShader::~GLShader()
{
    if (m_program)
        glDeleteProgram(m_program);
}

void GLShader::Use() const
{
    glUseProgram(m_program);
}

int GLShader::GetUniformLocation(const std::string& name)
{
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end())
        return it->second;

    const int loc = glGetUniformLocation(m_program, name.c_str());
    m_uniformCache[name] = loc;
    return loc;
}

void GLShader::SetBool(const std::string& name, bool value)
{
    glUniform1i(GetUniformLocation(name), value ? 1 : 0);
}

void GLShader::SetInt(const std::string& name, int value)
{
    glUniform1i(GetUniformLocation(name), value);
}

void GLShader::SetVec2(const std::string& name, const glm::vec2& value)
{
    glUniform2fv(GetUniformLocation(name), 1, &value[0]);
}

void GLShader::SetVec4(const std::string& name, const glm::vec4& value)
{
    glUniform4fv(GetUniformLocation(name), 1, &value[0]);
}

void GLShader::SetMat3(const std::string& name, const glm::mat3& value)
{
    glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void GLShader::SetFloat(const std::string& name, float value)
{
    glUniform1f(GetUniformLocation(name), value);
}

void GLShader::SetVec3(const std::string& name, const glm::vec3& value)
{
    glUniform3fv(GetUniformLocation(name), 1, &value[0]);
}

void GLShader::SetMat4(const std::string& name, const glm::mat4& value)
{
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

} // namespace engine
