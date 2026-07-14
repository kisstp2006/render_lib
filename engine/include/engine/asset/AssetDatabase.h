#pragma once

#include "engine/asset/AssetDescriptor.h"
#include "engine/asset/AssetTypeRegistry.h"

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::assets
{

struct AssetRecord
{
    std::filesystem::path DescriptorPath;
    AssetDescriptor Descriptor;
    bool MissingDescriptor = false;
    bool DuplicateGuid = false;
};

struct AssetDatabaseScanOptions
{
    bool MigrateDescriptors = true;
    bool RepairDuplicateGuids = false;
};

struct AssetDatabaseScanResult
{
    size_t Assets = 0;
    size_t Migrated = 0;
    size_t DuplicateGuids = 0;
    size_t UnknownTypes = 0;
    std::vector<AssetDiagnostic> Diagnostics;

    [[nodiscard]] bool Succeeded() const;
};

struct AssetBrowserQuery
{
    std::string Search;
    std::string Type;
    std::string Tag;
    std::optional<AssetImportState> State;
};

struct AssetBrowserEntry
{
    AssetGuid Guid;
    std::string Name;
    std::string Type;
    std::filesystem::path DescriptorPath;
    AssetImportState State = AssetImportState::Unknown;
    std::vector<std::string> Tags;
    AssetFingerprint ThumbnailKey;
    size_t DependencyCount = 0;
    size_t DependentCount = 0;
};

class AssetDatabase
{
  public:
    explicit AssetDatabase(const AssetTypeRegistry &types);

    AssetDatabaseScanResult Scan(const std::filesystem::path &assetRoot, const AssetDatabaseScanOptions &options = {});
    bool AddOrUpdate(const std::filesystem::path &descriptorPath, const AssetDescriptor &descriptor,
                     std::string *error = nullptr);
    bool Remove(AssetGuid guid);
    void Clear();

    [[nodiscard]] std::optional<AssetRecord> Find(AssetGuid guid) const;
    [[nodiscard]] std::optional<AssetRecord> FindByPath(const std::filesystem::path &descriptorPath) const;
    [[nodiscard]] std::optional<AssetGuid> ResolveGuid(const std::filesystem::path &descriptorPath) const;
    [[nodiscard]] std::optional<std::filesystem::path> ResolvePath(AssetGuid guid) const;

    [[nodiscard]] std::vector<AssetGuid> GetDependencies(AssetGuid guid) const;
    [[nodiscard]] std::vector<AssetGuid> GetDependents(AssetGuid guid) const;
    [[nodiscard]] std::vector<AssetGuid> GetSourceDependents(const std::filesystem::path &projectRelativeSource) const;
    [[nodiscard]] std::vector<std::vector<AssetGuid>> FindDependencyCycles() const;
    [[nodiscard]] std::vector<AssetGuid> FindMissingDependencies(AssetGuid guid) const;

    void MarkDirty(AssetGuid guid, bool includeDependents = true);
    [[nodiscard]] std::vector<AssetBrowserEntry> Query(const AssetBrowserQuery &query = {}) const;
    [[nodiscard]] size_t Size() const;

    bool RegenerateDuplicateGuid(const std::filesystem::path &descriptorPath, AssetGuid *newGuid = nullptr,
                                 std::string *error = nullptr);

  private:
    static std::string NormalizePathKey(const std::filesystem::path &path);
    void RebuildEdgesLocked();
    void MarkDirtyLocked(AssetGuid guid, bool includeDependents, std::unordered_set<AssetGuid, AssetGuidHash> &visited);

    const AssetTypeRegistry &m_types;
    mutable std::shared_mutex m_mutex;
    std::unordered_map<AssetGuid, AssetRecord, AssetGuidHash> m_assets;
    std::unordered_map<std::string, AssetGuid> m_paths;
    std::unordered_map<AssetGuid, std::vector<AssetGuid>, AssetGuidHash> m_dependencies;
    std::unordered_map<AssetGuid, std::vector<AssetGuid>, AssetGuidHash> m_dependents;
    std::unordered_map<std::string, std::vector<AssetGuid>> m_sourceDependents;
    std::unordered_map<std::string, std::vector<std::filesystem::path>> m_duplicatePaths;
};

} // namespace engine::assets
