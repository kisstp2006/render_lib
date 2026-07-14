#pragma once

#include "engine/asset/AssetPipeline.h"

#include <future>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace engine::assets
{

// UI-toolkit independent model consumed by a future inspector implementation.
struct AssetInspectorField
{
    AssetPropertySchema Schema;
    std::string Value;
    bool Visible = true;
    bool Modified = false;
    std::string ValidationError;
};

struct AssetInspectorDocument
{
    AssetGuid Guid;
    std::filesystem::path DescriptorPath;
    std::string Name;
    std::string Type;
    AssetImportState State = AssetImportState::Unknown;
    bool Modified = false;
    std::vector<std::filesystem::path> Sources;
    std::vector<AssetGuid> Dependencies;
    std::vector<AssetGuid> Dependents;
    std::vector<AssetDiagnostic> Diagnostics;
    std::vector<AssetInspectorField> Fields;
};

class AssetInspectorModel
{
  public:
    explicit AssetInspectorModel(AssetPipeline &pipeline);

    bool Open(AssetGuid guid, std::string *error = nullptr);
    void Close();
    [[nodiscard]] const AssetInspectorDocument *Document() const;

    bool SetValue(std::string_view key, std::string value, std::string *error = nullptr);
    bool ResetValue(std::string_view key, std::string *error = nullptr);
    void ResetAll();
    bool Apply(std::string *error = nullptr);
    AssetTransformResult Transform(bool force = false, std::string *error = nullptr);
    [[nodiscard]] bool HasErrors() const;

  private:
    bool Rebuild(std::string *error);
    void UpdateVisibility();
    static std::string ValidateValue(const AssetPropertySchema &schema, std::string_view value,
                                     const AssetDatabase &database);

    AssetPipeline &m_pipeline;
    std::optional<AssetRecord> m_record;
    AssetInspectorDocument m_document;
};

class AssetBrowserModel
{
  public:
    explicit AssetBrowserModel(AssetPipeline &pipeline) : m_pipeline(pipeline)
    {
    }

    [[nodiscard]] std::vector<AssetBrowserEntry> Query(const AssetBrowserQuery &query = {}) const;
    void Select(AssetGuid guid, bool additive = false);
    void ClearSelection();
    [[nodiscard]] std::vector<AssetGuid> Selection() const;
    [[nodiscard]] std::vector<AssetGuid> UsageList(AssetGuid guid) const;
    [[nodiscard]] std::vector<AssetGuid> Dependencies(AssetGuid guid) const;

  private:
    AssetPipeline &m_pipeline;
    std::unordered_set<AssetGuid, AssetGuidHash> m_selection;
};

inline constexpr std::string_view kAssetDragMimeType = "application/x-sla-asset-drop";

struct AssetDragPayload
{
    std::vector<AssetGuid> Assets;
    std::vector<std::filesystem::path> SourceFiles;
};

struct AssetDropResult
{
    bool Succeeded = false;
    std::vector<AssetGuid> ReferencedAssets;
    ImportResult Imported;
    std::vector<AssetDiagnostic> Diagnostics;
};

bool EncodeAssetDragPayload(const AssetDragPayload &payload, std::vector<std::byte> &bytes,
                            std::string *error = nullptr);
bool DecodeAssetDragPayload(std::span<const std::byte> bytes, AssetDragPayload &payload, std::string *error = nullptr);
AssetDropResult ProcessAssetDrop(AssetPipeline &pipeline, const AssetDragPayload &payload,
                                 const std::filesystem::path &destinationDirectory = {});

struct AssetPreviewResult
{
    bool Succeeded = false;
    AssetPreview Preview;
    std::string Error;
};

// Thread-safe, de-duplicating preview service. Providers only create CPU-side preview data;
// uploading it to a UI texture remains the responsibility of the eventual editor renderer.
class AssetPreviewService
{
  public:
    AssetPreviewService(const AssetTypeRegistry &types, const AssetDatabase &database,
                        std::filesystem::path projectRoot);

    std::shared_future<AssetPreviewResult> Request(AssetGuid guid, uint32_t maximumWidth = 256,
                                                   uint32_t maximumHeight = 256);
    void Invalidate(AssetGuid guid);
    void Clear();
    [[nodiscard]] size_t CachedPreviewCount() const;

  private:
    std::string BuildRequestKey(const AssetRecord &record, uint32_t width, uint32_t height) const;

    const AssetTypeRegistry &m_types;
    const AssetDatabase &m_database;
    std::filesystem::path m_projectRoot;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_future<AssetPreviewResult>> m_requests;
    std::unordered_multimap<AssetGuid, std::string, AssetGuidHash> m_assetKeys;
};

} // namespace engine::assets
