#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>
#include "engine/render/PipelineCache.h"

namespace engine {

// Owns a compiled+linked GL program built from a vertex/fragment source file
// pair. Uniform locations are looked up lazily and cached.
class GLShader
{
public:
    GLShader(const std::string& vertPath, const std::string& fragPath,
             const std::vector<ShaderDefine>& defines = {});
    explicit GLShader(const std::string& computePath,
                      const std::vector<ShaderDefine>& defines = {});
    ~GLShader();

    GLShader(const GLShader&) = delete;
    GLShader& operator=(const GLShader&) = delete;

    void Use() const;

    void SetBool(const std::string& name, bool value);
    void SetInt(const std::string& name, int value);
    void SetFloat(const std::string& name, float value);
    void SetVec2(const std::string& name, const glm::vec2& value);
    void SetVec3(const std::string& name, const glm::vec3& value);
    void SetVec4(const std::string& name, const glm::vec4& value);
    void SetMat3(const std::string& name, const glm::mat3& value);
    void SetMat4(const std::string& name, const glm::mat4& value);

    static void ConfigureCache(const std::filesystem::path& root,
                               std::string deviceIdentity,
                               bool enabled, bool clear);
    static PipelineCacheStatistics CacheStatistics();

private:
    int GetUniformLocation(const std::string& name);
    static unsigned int CompileStage(unsigned int stage, const std::string& source, const std::string& debugName);

    unsigned int m_program = 0;
    std::unordered_map<std::string, int> m_uniformCache;
};

} // namespace engine
