#include "engine/render/ShaderSource.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace engine {
namespace {

std::filesystem::path NormalizeExistingPath(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : normalized;
}

std::filesystem::path ResolveInclude(
    const std::filesystem::path& includingFile,
    const std::filesystem::path& requested,
    const std::vector<std::filesystem::path>& includeRoots)
{
    std::vector<std::filesystem::path> candidates;
    candidates.reserve(includeRoots.size() + 1);
    candidates.push_back(includingFile.parent_path() / requested);
    for (const auto& root : includeRoots)
        candidates.push_back(root / requested);

    for (const auto& candidate : candidates)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return NormalizeExistingPath(candidate);
    }

    std::ostringstream message;
    message << "Shader include not found: \"" << requested.string()
            << "\" included by " << includingFile.string();
    if (!includeRoots.empty())
    {
        message << " (include roots:";
        for (const auto& root : includeRoots)
            message << ' ' << root.string();
        message << ')';
    }
    throw std::runtime_error(message.str());
}

void ExpandFile(const std::filesystem::path& path,
                const std::vector<std::filesystem::path>& includeRoots,
                std::vector<std::filesystem::path>& includeStack,
                ShaderSourceDocument& document)
{
    const std::filesystem::path normalized = NormalizeExistingPath(path);
    if (std::find(includeStack.begin(), includeStack.end(), normalized) != includeStack.end())
    {
        std::ostringstream chain;
        for (const auto& file : includeStack)
            chain << file.string() << " -> ";
        chain << normalized.string();
        throw std::runtime_error("Cyclic shader include: " + chain.str());
    }

    std::ifstream file(normalized);
    if (!file)
        throw std::runtime_error("Failed to open shader file: " + normalized.string());

    includeStack.push_back(normalized);
    if (std::find(document.Dependencies.begin(), document.Dependencies.end(), normalized)
        == document.Dependencies.end())
    {
        document.Dependencies.push_back(normalized);
    }

    std::string line;
    while (std::getline(file, line))
    {
        const size_t directive = line.find("#include");
        const size_t firstQuote = directive == std::string::npos
            ? std::string::npos : line.find('"', directive);
        const size_t secondQuote = firstQuote == std::string::npos
            ? std::string::npos : line.find('"', firstQuote + 1);
        if (directive != std::string::npos && firstQuote != std::string::npos
            && secondQuote != std::string::npos)
        {
            const auto requested = std::filesystem::path(
                line.substr(firstQuote + 1, secondQuote - firstQuote - 1));
            ExpandFile(ResolveInclude(normalized, requested, includeRoots), includeRoots,
                       includeStack, document);
            document.Source.push_back('\n');
        }
        else
        {
            document.Source += line;
            document.Source.push_back('\n');
        }
    }

    includeStack.pop_back();
}

} // namespace

ShaderSourceDocument LoadShaderSource(
    const std::filesystem::path& entryPoint,
    const std::vector<std::filesystem::path>& includeRoots)
{
    ShaderSourceDocument document;
    std::vector<std::filesystem::path> includeStack;
    ExpandFile(entryPoint, includeRoots, includeStack, document);
    return document;
}

} // namespace engine
