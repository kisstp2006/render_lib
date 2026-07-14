#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets
{

bool ReadFileBytes(const std::filesystem::path &path, std::vector<std::byte> &bytes, std::string *error = nullptr,
                   size_t maximumBytes = 1024ull * 1024ull * 1024ull);
bool ReadTextFile(const std::filesystem::path &path, std::string &text, std::string *error = nullptr,
                  size_t maximumBytes = 64ull * 1024ull * 1024ull);
bool WriteFileAtomic(const std::filesystem::path &path, std::span<const std::byte> bytes, std::string *error = nullptr);
bool WriteTextFileAtomic(const std::filesystem::path &path, std::string_view text, std::string *error = nullptr);
bool CreateFileBackup(const std::filesystem::path &path, std::filesystem::path &backupPath,
                      std::string *error = nullptr);

std::filesystem::path NormalizeProjectRelative(const std::filesystem::path &projectRoot,
                                               const std::filesystem::path &path, std::string *error = nullptr);

} // namespace engine::assets
