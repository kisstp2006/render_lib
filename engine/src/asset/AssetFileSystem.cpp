#include "engine/asset/AssetFileSystem.h"

#include "engine/asset/AssetGuid.h"

#include <fstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace engine::assets
{
namespace
{

std::filesystem::path TemporarySibling(const std::filesystem::path &path)
{
    return path.parent_path() / (path.filename().generic_string() + ".tmp-" + AssetGuid::Generate().ToString());
}

bool ReplaceFile(const std::filesystem::path &source, const std::filesystem::path &target, std::string *error)
{
#ifdef _WIN32
    if (::MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
        return true;
    if (error)
        *error = "Atomic replace failed for '" + target.generic_string() + "' (Win32 error " +
                 std::to_string(::GetLastError()) + ")";
    return false;
#else
    std::error_code renameError;
    std::filesystem::rename(source, target, renameError);
    if (!renameError)
        return true;
    if (error)
        *error = "Atomic replace failed for '" + target.generic_string() + "': " + renameError.message();
    return false;
#endif
}

} // namespace

bool ReadFileBytes(const std::filesystem::path &path, std::vector<std::byte> &bytes, std::string *error,
                   size_t maximumBytes)
{
    std::error_code sizeError;
    const uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError)
    {
        if (error)
            *error = "Cannot query file size for '" + path.generic_string() + "': " + sizeError.message();
        return false;
    }
    if (fileSize > maximumBytes || fileSize > static_cast<uintmax_t>(SIZE_MAX))
    {
        if (error)
            *error =
                "File exceeds the allowed size (" + std::to_string(maximumBytes) + " bytes): " + path.generic_string();
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        if (error)
            *error = "Cannot open file: " + path.generic_string();
        return false;
    }
    bytes.resize(static_cast<size_t>(fileSize));
    if (!bytes.empty())
        file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file && !bytes.empty())
    {
        bytes.clear();
        if (error)
            *error = "Could not read complete file: " + path.generic_string();
        return false;
    }
    return true;
}

bool ReadTextFile(const std::filesystem::path &path, std::string &text, std::string *error, size_t maximumBytes)
{
    std::vector<std::byte> bytes;
    if (!ReadFileBytes(path, bytes, error, maximumBytes))
        return false;
    text.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    return true;
}

bool WriteFileAtomic(const std::filesystem::path &path, std::span<const std::byte> bytes, std::string *error)
{
    std::error_code directoryError;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError)
    {
        if (error)
            *error = "Cannot create output directory for '" + path.generic_string() + "': " + directoryError.message();
        return false;
    }

    const std::filesystem::path temporary = TemporarySibling(path);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            if (error)
                *error = "Cannot create temporary file: " + temporary.generic_string();
            return false;
        }
        if (!bytes.empty())
            file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file)
        {
            file.close();
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            if (error)
                *error = "Failed to write temporary file: " + temporary.generic_string();
            return false;
        }
    }

    if (ReplaceFile(temporary, path, error))
        return true;
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    return false;
}

bool WriteTextFileAtomic(const std::filesystem::path &path, std::string_view text, std::string *error)
{
    return WriteFileAtomic(path, std::span(reinterpret_cast<const std::byte *>(text.data()), text.size()), error);
}

bool CreateFileBackup(const std::filesystem::path &path, std::filesystem::path &backupPath, std::string *error)
{
    if (!std::filesystem::is_regular_file(path))
    {
        if (error)
            *error = "Cannot back up missing file: " + path.generic_string();
        return false;
    }
    backupPath = path;
    backupPath += ".bak";
    uint32_t suffix = 1;
    while (std::filesystem::exists(backupPath))
    {
        backupPath = path;
        backupPath += ".bak." + std::to_string(suffix++);
    }
    std::error_code copyError;
    std::filesystem::copy_file(path, backupPath, std::filesystem::copy_options::none, copyError);
    if (!copyError)
        return true;
    if (error)
        *error = "Cannot create backup '" + backupPath.generic_string() + "': " + copyError.message();
    return false;
}

std::filesystem::path NormalizeProjectRelative(const std::filesystem::path &projectRoot,
                                               const std::filesystem::path &path, std::string *error)
{
    std::error_code absoluteError;
    const std::filesystem::path root = std::filesystem::weakly_canonical(projectRoot, absoluteError);
    if (absoluteError)
    {
        if (error)
            *error = "Cannot resolve project root '" + projectRoot.generic_string() + "': " + absoluteError.message();
        return {};
    }
    const std::filesystem::path absolute = path.is_absolute() ? path : root / path;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, absoluteError);
    if (absoluteError)
    {
        if (error)
            *error = "Cannot resolve project path '" + path.generic_string() + "': " + absoluteError.message();
        return {};
    }
    const std::filesystem::path relative = canonical.lexically_relative(root);
    if (relative.empty() || (!relative.empty() && *relative.begin() == ".."))
    {
        if (error)
            *error = "Path is outside the project root: " + canonical.generic_string();
        return {};
    }
    return relative.lexically_normal();
}

} // namespace engine::assets
