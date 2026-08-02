#include "engine/render/ShaderPaths.h"

#include <utility>

namespace engine::shader_paths {
namespace {

std::filesystem::path& MutableRoot()
{
    static std::filesystem::path root = std::filesystem::path(ENGINE_SHADER_DIR);
    return root;
}

} // namespace

void SetRoot(std::filesystem::path root)
{
    MutableRoot() = root.empty() ? std::filesystem::path(ENGINE_SHADER_DIR)
                                 : std::move(root);
}

const std::filesystem::path& Root()
{
    return MutableRoot();
}

std::filesystem::path Resolve(const std::filesystem::path& relativePath)
{
    return Root() / relativePath;
}

} // namespace engine::shader_paths
