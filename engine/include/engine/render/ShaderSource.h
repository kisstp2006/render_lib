#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace engine {

struct ShaderSourceDocument
{
    std::string Source;
    std::vector<std::filesystem::path> Dependencies;
};

// Loads a shader source tree and expands quoted #include directives. Include
// paths are resolved relative to the including file first, then against the
// supplied roots. Both graphics backends use this so include behavior and
// diagnostics stay identical.
ShaderSourceDocument LoadShaderSource(
    const std::filesystem::path& entryPoint,
    const std::vector<std::filesystem::path>& includeRoots = {});

} // namespace engine
