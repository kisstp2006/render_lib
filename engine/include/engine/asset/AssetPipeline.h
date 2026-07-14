#pragma once

#include "engine/asset/AssetDatabase.h"
#include "engine/resource/ResourceManager.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::assets
{

struct AssetPipelineConfig
{
    std::filesystem::path ProjectRoot;
    std::filesystem::path AssetRoot = "Assets";
    std::filesystem::path CacheRoot = ".cache/assets";
    std::string Platform = "desktop";
    std::string Profile = "default";
    bool AllowDevelopmentSourceFallback = true;
};

struct AssetTransformResult
{
    bool Succeeded = false;
    bool SkippedAsUpToDate = false;
    AssetFingerprint Fingerprint;
    std::vector<std::filesystem::path> GeneratedRuntimeFiles;
    std::vector<AssetDiagnostic> Diagnostics;
    std::map<std::string, uint64_t> Statistics;
};

class AssetPipeline
{
  public:
    AssetPipeline(AssetTypeRegistry &types, AssetDatabase &database, resources::ResourceManager &resources,
                  AssetPipelineConfig config);

    [[nodiscard]] const AssetPipelineConfig &Config() const
    {
        return m_config;
    }
    [[nodiscard]] AssetTypeRegistry &Types()
    {
        return m_types;
    }
    [[nodiscard]] AssetDatabase &Database()
    {
        return m_database;
    }

    ImportResult CreateAssetFromSource(const std::filesystem::path &source,
                                       const std::filesystem::path &destinationDirectory = {},
                                       std::string_view requestedType = {}, std::string_view importMode = {});
    ImportResult CreateAssetsFromSources(std::span<const std::filesystem::path> sources,
                                         const std::filesystem::path &destinationDirectory = {});
    ImportResult ImportDroppedFiles(std::span<const std::filesystem::path> sources,
                                    const std::filesystem::path &destinationDirectory = {});

    AssetTransformResult TransformAsset(AssetGuid guid, bool force = false);
    AssetTransformResult TransformAsset(const std::filesystem::path &descriptorPath, bool force = false);
    AssetTransformResult ForceTransformAsset(AssetGuid guid)
    {
        return TransformAsset(guid, true);
    }
    AssetTransformResult ReimportAsset(AssetGuid guid);
    std::vector<AssetTransformResult> TransformAll(bool force = false);

    [[nodiscard]] std::vector<AssetDiagnostic> ValidateAsset(AssetGuid guid) const;
    [[nodiscard]] bool IsAssetDirty(AssetGuid guid, std::string *reason = nullptr) const;
    [[nodiscard]] AssetFingerprint ComputeAssetFingerprint(AssetGuid guid, std::string *error = nullptr) const;
    [[nodiscard]] AssetFingerprint ComputeSourceFingerprint(const AssetDescriptor &descriptor,
                                                            std::string *error = nullptr) const;
    [[nodiscard]] AssetFingerprint ComputeDependencyFingerprint(const AssetDescriptor &descriptor,
                                                                std::string *error = nullptr) const;
    void RefreshDirtyStates();

    [[nodiscard]] std::vector<AssetGuid> GetAssetDependencies(AssetGuid guid) const;
    [[nodiscard]] std::vector<AssetGuid> GetAssetDependents(AssetGuid guid) const;
    [[nodiscard]] std::optional<AssetRecord> ResolveAssetReference(AssetGuid guid) const;
    [[nodiscard]] std::optional<AssetGuid> ResolveAssetGuid(const std::filesystem::path &descriptorPath) const;
    [[nodiscard]] std::filesystem::path GetCookedResourcePath(AssetGuid guid) const;
    [[nodiscard]] std::filesystem::path GetAssetDescriptorPath(
        const std::filesystem::path &source, const AssetTypeRegistration &type,
        const std::filesystem::path &destinationDirectory = {}) const;

    bool SaveRuntimeRegistry(std::string *error = nullptr) const;
    [[nodiscard]] std::filesystem::path RuntimeRegistryPath() const;

  private:
    AssetTransformResult TransformAssetInternal(AssetGuid guid, bool force,
                                                std::unordered_set<AssetGuid, AssetGuidHash> &stack);
    std::shared_ptr<std::mutex> GetTransformMutex(AssetGuid guid);
    std::filesystem::path AbsoluteProjectPath(const std::filesystem::path &path) const;
    std::map<std::string, std::string> ResolveSettings(const AssetDescriptor &descriptor,
                                                       const AssetTypeRegistration &type) const;
    AssetDiagnostic Diagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message,
                               const AssetDescriptor *descriptor, const std::filesystem::path &source, std::string step,
                               std::string suggestion = {}) const;

    AssetTypeRegistry &m_types;
    AssetDatabase &m_database;
    resources::ResourceManager &m_resources;
    AssetPipelineConfig m_config;
    mutable std::mutex m_transformMutexMapMutex;
    std::unordered_map<AssetGuid, std::weak_ptr<std::mutex>, AssetGuidHash> m_transformMutexes;
};

} // namespace engine::assets
