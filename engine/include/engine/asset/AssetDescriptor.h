#pragma once

#include "engine/asset/AssetGuid.h"
#include "engine/asset/AssetHash.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets
{

inline constexpr uint32_t kAssetDescriptorEnvelopeVersion = 1;

enum class AssetImportState : uint8_t
{
    Unknown,
    Dirty,
    Transforming,
    Ready,
    Warning,
    Error,
};

enum class AssetDiagnosticSeverity : uint8_t
{
    Info,
    Warning,
    RecoverableError,
    FatalError,
};

struct AssetDiagnostic
{
    AssetDiagnosticSeverity Severity = AssetDiagnosticSeverity::Info;
    std::string Code;
    std::string Message;
    std::string Suggestion;
    std::filesystem::path Source;
    std::string Step;
};

struct AssetPlatformOverride
{
    std::string Profile;
    std::map<std::string, std::string> Settings;
};

struct AssetTransformRecord
{
    std::string Platform;
    std::string Profile;
    std::filesystem::path CookedResource;
    AssetFingerprint Fingerprint;
    uint64_t UnixTimestampSeconds = 0;
    bool Succeeded = false;
};

struct AssetDescriptor
{
    AssetGuid Guid;
    std::string Type;
    uint32_t DescriptorVersion = 1;
    uint32_t ImporterVersion = 1;
    uint32_t TransformerVersion = 1;
    std::string Name;

    std::vector<std::filesystem::path> Sources;
    std::map<std::string, std::string> Settings;
    std::vector<AssetGuid> AssetDependencies;
    std::vector<std::filesystem::path> SourceDependencies;
    std::vector<AssetPlatformOverride> PlatformOverrides;
    std::vector<std::string> Tags;
    std::map<std::string, std::string> UserMetadata;

    AssetFingerprint SourceFingerprint;
    AssetFingerprint SettingsFingerprint;
    AssetFingerprint DependencyFingerprint;
    AssetFingerprint LastTransformFingerprint;
    AssetTransformRecord LastTransform;
    AssetImportState State = AssetImportState::Unknown;
    std::vector<AssetDiagnostic> Diagnostics;

    bool Generated = false;
    AssetGuid GeneratedBy;
    bool UserModified = false;

    [[nodiscard]] std::map<std::string, std::string> ResolveSettings(std::string_view profile) const;
    [[nodiscard]] std::optional<std::string> GetSetting(std::string_view key, std::string_view profile = {}) const;
};

bool SaveAssetDescriptor(const std::filesystem::path &path, const AssetDescriptor &descriptor,
                         std::string *error = nullptr);
bool LoadAssetDescriptor(const std::filesystem::path &path, AssetDescriptor &descriptor, std::string *error = nullptr);

// Canonical, deterministic settings representation used for fingerprints.
std::string SerializeAssetSettings(const AssetDescriptor &descriptor, std::string_view profile = {});

std::string ToString(AssetImportState state);
std::string ToString(AssetDiagnosticSeverity severity);
bool ParseAssetImportState(std::string_view text, AssetImportState &state);
bool ParseAssetDiagnosticSeverity(std::string_view text, AssetDiagnosticSeverity &severity);

} // namespace engine::assets
