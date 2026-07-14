#pragma once

#include "engine/asset/AssetDescriptor.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::assets
{

enum class AssetPropertyType : uint8_t
{
    Boolean,
    Integer,
    Number,
    String,
    Path,
    Enumeration,
    Color,
    AssetReference,
};

struct AssetPropertySchema
{
    std::string Key;
    std::string DisplayName;
    std::string Group;
    AssetPropertyType Type = AssetPropertyType::String;
    std::string DefaultValue;
    std::vector<std::string> Choices;
    std::optional<double> Minimum;
    std::optional<double> Maximum;
    std::string AssetReferenceType;
    std::string VisibleWhenKey;
    std::string VisibleWhenValue;
    bool Advanced = false;
    bool ReadOnly = false;
};

struct AssetImportMode
{
    std::string Name;
    std::string DisplayName;
    int Priority = 0;
};

struct AssetImportRequest
{
    std::filesystem::path ProjectRoot;
    std::filesystem::path SourcePath;
    std::filesystem::path DestinationDirectory;
    std::string Mode;
    std::string RequestedType;
};

struct ImportedAsset
{
    AssetDescriptor Descriptor;
    std::filesystem::path DescriptorPath;
    bool Reused = false;
};

struct ImportResult
{
    bool Succeeded = false;
    std::vector<AssetDiagnostic> Diagnostics;
    std::vector<ImportedAsset> GeneratedAssets;
    std::vector<ImportedAsset> ReusedAssets;
    std::vector<std::filesystem::path> GeneratedRuntimeFiles;
    std::map<std::string, uint64_t> Statistics;
};

struct AssetValidationContext
{
    std::filesystem::path ProjectRoot;
    std::filesystem::path DescriptorPath;
    std::string Platform;
    std::string Profile;
};

struct AssetTransformContext : AssetValidationContext
{
    std::filesystem::path CacheRoot;
    AssetFingerprint Fingerprint;
    std::map<std::string, std::string> ResolvedSettings;
};

struct AssetTransformOutput
{
    std::string RuntimeType;
    uint32_t ResourceVersion = 1;
    uint32_t Flags = 0;
    std::string Compression = "none";
    std::vector<std::byte> Payload;
    std::vector<AssetGuid> AssetDependencies;
    std::vector<std::filesystem::path> SourceDependencies;
    std::map<std::string, uint64_t> Statistics;
    std::vector<AssetDiagnostic> Diagnostics;
};

struct AssetPreviewRequest
{
    std::filesystem::path ProjectRoot;
    std::filesystem::path DescriptorPath;
    uint32_t MaximumWidth = 256;
    uint32_t MaximumHeight = 256;
};

struct AssetPreview
{
    std::string Kind;
    std::string MimeType;
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::vector<std::byte> Bytes;
    std::map<std::string, std::string> Metadata;
    AssetFingerprint CacheKey;
};

using DescriptorLoader = std::function<bool(const std::filesystem::path &, AssetDescriptor &, std::string *)>;
using DescriptorSaver = std::function<bool(const std::filesystem::path &, const AssetDescriptor &, std::string *)>;
using AssetImporter = std::function<ImportResult(const AssetImportRequest &)>;
using AssetValidator =
    std::function<std::vector<AssetDiagnostic>(const AssetDescriptor &, const AssetValidationContext &)>;
using AssetTransformer =
    std::function<bool(const AssetDescriptor &, const AssetTransformContext &, AssetTransformOutput &, std::string *)>;
using AssetPreviewProvider =
    std::function<bool(const AssetDescriptor &, const AssetPreviewRequest &, AssetPreview &, std::string *)>;
using AssetInspectorProvider = std::function<std::vector<AssetPropertySchema>(const AssetDescriptor &)>;
using AssetMigration = std::function<bool(AssetDescriptor &, std::string *)>;
using AssetProfileProvider = std::function<std::map<std::string, std::string>(std::string_view)>;

struct AssetTypeRegistration
{
    std::string TypeId;
    std::string DisplayName;
    std::string DescriptorExtension;
    std::vector<std::string> SourceExtensions;
    std::vector<AssetImportMode> ImportModes;
    std::string RuntimeType;
    std::string Icon;
    uint32_t DescriptorVersion = 1;
    uint32_t ImporterVersion = 1;
    uint32_t TransformerVersion = 1;
    uint32_t ResourceVersion = 1;

    std::vector<AssetPropertySchema> Properties;
    DescriptorLoader LoadDescriptor;
    DescriptorSaver SaveDescriptor;
    AssetImporter Import;
    AssetValidator Validate;
    AssetTransformer Transform;
    AssetPreviewProvider Preview;
    AssetInspectorProvider Inspector;
    AssetProfileProvider ProfileDefaults;
    std::map<uint32_t, AssetMigration> Migrations;
};

class AssetTypeRegistry
{
  public:
    bool Register(AssetTypeRegistration registration, std::string *error = nullptr);
    bool Unregister(std::string_view typeId);

    [[nodiscard]] const AssetTypeRegistration *Find(std::string_view typeId) const;
    [[nodiscard]] const AssetTypeRegistration *FindByDescriptorExtension(std::string_view extension) const;
    [[nodiscard]] std::vector<const AssetTypeRegistration *> FindImportersForExtension(
        std::string_view extension) const;
    [[nodiscard]] std::vector<const AssetTypeRegistration *> ListTypes() const;

  private:
    static std::string NormalizeExtension(std::string_view extension);

    mutable std::shared_mutex m_mutex;
    std::map<std::string, AssetTypeRegistration> m_types;
    std::unordered_map<std::string, std::string> m_descriptorExtensions;
    std::unordered_multimap<std::string, std::string> m_sourceExtensions;
};

bool MigrateAssetDescriptor(AssetDescriptor &descriptor, const AssetTypeRegistration &type,
                            std::string *error = nullptr);
bool MigrateAssetDescriptorFile(const std::filesystem::path &path, const AssetTypeRegistry &registry,
                                std::filesystem::path *backupPath = nullptr, std::string *error = nullptr);

} // namespace engine::assets
