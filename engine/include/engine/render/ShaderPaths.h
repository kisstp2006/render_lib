#pragma once

#include <filesystem>

namespace engine::shader_paths {

// Process-wide shader root. Configure it before backend initialization; the
// value stays immutable while any renderer instance is active.
void SetRoot(std::filesystem::path root);
const std::filesystem::path& Root();
std::filesystem::path Resolve(const std::filesystem::path& relativePath);

} // namespace engine::shader_paths
