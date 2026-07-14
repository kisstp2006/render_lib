#include "engine/asset/AssetWorkspace.h"
#include "engine/concurrency/TaskSystem.h"

#include "engine/resource/BinaryIO.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace engine::assets
{
namespace
{

bool ParseNumber(std::string_view text, double &number)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && std::isfinite(number);
}

bool ParseInteger(std::string_view text, int64_t &number)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

AssetDiagnostic DropDiagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message,
                               const std::filesystem::path &source = {})
{
    AssetDiagnostic result;
    result.Severity = severity;
    result.Code = std::move(code);
    result.Message = std::move(message);
    result.Source = source;
    result.Step = "drag-and-drop import";
    return result;
}

} // namespace

AssetInspectorModel::AssetInspectorModel(AssetPipeline &pipeline) : m_pipeline(pipeline)
{
}

bool AssetInspectorModel::Open(AssetGuid guid, std::string *error)
{
    m_record = m_pipeline.Database().Find(guid);
    if (!m_record)
    {
        if (error)
            *error = "Cannot inspect unknown asset GUID: " + guid.ToString();
        Close();
        return false;
    }
    return Rebuild(error);
}

void AssetInspectorModel::Close()
{
    m_record.reset();
    m_document = {};
}

const AssetInspectorDocument *AssetInspectorModel::Document() const
{
    return m_record ? &m_document : nullptr;
}

std::string AssetInspectorModel::ValidateValue(const AssetPropertySchema &schema, std::string_view value,
                                               const AssetDatabase &database)
{
    if (schema.ReadOnly)
        return "This property is read-only";
    if (schema.Type == AssetPropertyType::Boolean && value != "true" && value != "false")
        return "Expected 'true' or 'false'";
    if (schema.Type == AssetPropertyType::Integer)
    {
        int64_t number = 0;
        if (!ParseInteger(value, number))
            return "Expected an integer";
        if (schema.Minimum && static_cast<double>(number) < *schema.Minimum)
            return "Value is below the allowed minimum";
        if (schema.Maximum && static_cast<double>(number) > *schema.Maximum)
            return "Value is above the allowed maximum";
    }
    if (schema.Type == AssetPropertyType::Number)
    {
        double number = 0.0;
        if (!ParseNumber(value, number))
            return "Expected a finite number";
        if (schema.Minimum && number < *schema.Minimum)
            return "Value is below the allowed minimum";
        if (schema.Maximum && number > *schema.Maximum)
            return "Value is above the allowed maximum";
    }
    if (schema.Type == AssetPropertyType::Enumeration &&
        std::find(schema.Choices.begin(), schema.Choices.end(), value) == schema.Choices.end())
        return "Value is not one of the registered choices";
    if (schema.Type == AssetPropertyType::AssetReference && !value.empty())
    {
        const auto guid = AssetGuid::Parse(value);
        if (!guid)
            return "Expected an asset GUID";
        const auto target = database.Find(*guid);
        if (!target)
            return "Referenced asset does not exist";
        if (!schema.AssetReferenceType.empty() && target->Descriptor.Type != schema.AssetReferenceType)
            return "Referenced asset has type '" + target->Descriptor.Type + "', expected '" +
                   schema.AssetReferenceType + "'";
    }
    return {};
}

bool AssetInspectorModel::Rebuild(std::string *error)
{
    if (!m_record)
        return false;
    const AssetTypeRegistration *type = m_pipeline.Types().Find(m_record->Descriptor.Type);
    if (!type)
    {
        if (error)
            *error = "No asset type registration for '" + m_record->Descriptor.Type + "'";
        return false;
    }
    m_document = {};
    m_document.Guid = m_record->Descriptor.Guid;
    m_document.DescriptorPath = m_record->DescriptorPath;
    m_document.Name = m_record->Descriptor.Name;
    m_document.Type = m_record->Descriptor.Type;
    m_document.State = m_record->Descriptor.State;
    m_document.Sources = m_record->Descriptor.Sources;
    m_document.Dependencies = m_pipeline.GetAssetDependencies(m_record->Descriptor.Guid);
    m_document.Dependents = m_pipeline.GetAssetDependents(m_record->Descriptor.Guid);
    m_document.Diagnostics = m_record->Descriptor.Diagnostics;

    const std::vector<AssetPropertySchema> schemas =
        type->Inspector ? type->Inspector(m_record->Descriptor) : type->Properties;
    m_document.Fields.reserve(schemas.size());
    for (const AssetPropertySchema &schema : schemas)
    {
        AssetInspectorField field;
        field.Schema = schema;
        const auto found = m_record->Descriptor.Settings.find(schema.Key);
        field.Value = found == m_record->Descriptor.Settings.end() ? schema.DefaultValue : found->second;
        field.ValidationError = ValidateValue(schema, field.Value, m_pipeline.Database());
        m_document.Fields.push_back(std::move(field));
    }
    UpdateVisibility();
    return true;
}

void AssetInspectorModel::UpdateVisibility()
{
    for (AssetInspectorField &field : m_document.Fields)
    {
        field.Visible = true;
        if (field.Schema.VisibleWhenKey.empty())
            continue;
        const auto controller =
            std::find_if(m_document.Fields.begin(), m_document.Fields.end(), [&](const AssetInspectorField &candidate) {
                return candidate.Schema.Key == field.Schema.VisibleWhenKey;
            });
        field.Visible = controller != m_document.Fields.end() && controller->Value == field.Schema.VisibleWhenValue;
    }
}

bool AssetInspectorModel::SetValue(std::string_view key, std::string value, std::string *error)
{
    if (!m_record)
    {
        if (error)
            *error = "No asset is open in the inspector";
        return false;
    }
    const auto found = std::find_if(m_document.Fields.begin(), m_document.Fields.end(),
                                    [&](const AssetInspectorField &field) { return field.Schema.Key == key; });
    if (found == m_document.Fields.end())
    {
        if (error)
            *error = "Unknown asset property: " + std::string(key);
        return false;
    }
    found->ValidationError = ValidateValue(found->Schema, value, m_pipeline.Database());
    if (!found->ValidationError.empty())
    {
        if (error)
            *error = found->ValidationError;
        return false;
    }
    if (found->Value != value)
    {
        found->Value = std::move(value);
        found->Modified = true;
        m_document.Modified = true;
        m_document.State = AssetImportState::Dirty;
    }
    UpdateVisibility();
    return true;
}

bool AssetInspectorModel::ResetValue(std::string_view key, std::string *error)
{
    const auto found = std::find_if(m_document.Fields.begin(), m_document.Fields.end(),
                                    [&](const AssetInspectorField &field) { return field.Schema.Key == key; });
    if (found == m_document.Fields.end())
    {
        if (error)
            *error = "Unknown asset property: " + std::string(key);
        return false;
    }
    return SetValue(key, found->Schema.DefaultValue, error);
}

void AssetInspectorModel::ResetAll()
{
    for (AssetInspectorField &field : m_document.Fields)
    {
        if (field.Schema.ReadOnly)
            continue;
        if (field.Value != field.Schema.DefaultValue)
        {
            field.Value = field.Schema.DefaultValue;
            field.Modified = true;
            m_document.Modified = true;
        }
        field.ValidationError = ValidateValue(field.Schema, field.Value, m_pipeline.Database());
    }
    if (m_document.Modified)
        m_document.State = AssetImportState::Dirty;
    UpdateVisibility();
}

bool AssetInspectorModel::HasErrors() const
{
    return std::any_of(m_document.Fields.begin(), m_document.Fields.end(),
                       [](const AssetInspectorField &field) { return !field.ValidationError.empty(); });
}

bool AssetInspectorModel::Apply(std::string *error)
{
    if (!m_record)
    {
        if (error)
            *error = "No asset is open in the inspector";
        return false;
    }
    if (HasErrors())
    {
        if (error)
            *error = "Asset properties contain validation errors";
        return false;
    }
    if (!m_document.Modified)
        return true;
    const AssetTypeRegistration *type = m_pipeline.Types().Find(m_record->Descriptor.Type);
    if (!type || !type->SaveDescriptor)
    {
        if (error)
            *error = "Asset type has no descriptor saver";
        return false;
    }
    for (const AssetInspectorField &field : m_document.Fields)
        if (!field.Schema.ReadOnly)
            m_record->Descriptor.Settings[field.Schema.Key] = field.Value;
    m_record->Descriptor.UserModified = true;
    m_record->Descriptor.State = AssetImportState::Dirty;
    if (!type->SaveDescriptor(m_record->DescriptorPath, m_record->Descriptor, error) ||
        !m_pipeline.Database().AddOrUpdate(m_record->DescriptorPath, m_record->Descriptor, error))
        return false;
    m_pipeline.Database().MarkDirty(m_record->Descriptor.Guid, true);
    return Open(m_record->Descriptor.Guid, error);
}

AssetTransformResult AssetInspectorModel::Transform(bool force, std::string *error)
{
    AssetTransformResult result;
    if (!m_record)
    {
        if (error)
            *error = "No asset is open in the inspector";
        return result;
    }
    if (!Apply(error))
        return result;
    result = m_pipeline.TransformAsset(m_record->Descriptor.Guid, force);
    Open(m_record->Descriptor.Guid, nullptr);
    return result;
}

std::vector<AssetBrowserEntry> AssetBrowserModel::Query(const AssetBrowserQuery &query) const
{
    return m_pipeline.Database().Query(query);
}

void AssetBrowserModel::Select(AssetGuid guid, bool additive)
{
    if (!additive)
        m_selection.clear();
    if (m_pipeline.Database().Find(guid))
        m_selection.insert(guid);
}

void AssetBrowserModel::ClearSelection()
{
    m_selection.clear();
}

std::vector<AssetGuid> AssetBrowserModel::Selection() const
{
    std::vector<AssetGuid> result(m_selection.begin(), m_selection.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<AssetGuid> AssetBrowserModel::UsageList(AssetGuid guid) const
{
    return m_pipeline.GetAssetDependents(guid);
}

std::vector<AssetGuid> AssetBrowserModel::Dependencies(AssetGuid guid) const
{
    return m_pipeline.GetAssetDependencies(guid);
}

bool EncodeAssetDragPayload(const AssetDragPayload &payload, std::vector<std::byte> &bytes, std::string *error)
{
    if (payload.Assets.size() > 4096 || payload.SourceFiles.size() > 4096)
    {
        if (error)
            *error = "Drag payload contains too many items";
        return false;
    }
    resources::BinaryWriter writer;
    writer.WriteString("SLA_ASSET_DROP");
    writer.WriteU32(1);
    writer.WriteU32(static_cast<uint32_t>(payload.Assets.size()));
    for (AssetGuid guid : payload.Assets)
    {
        writer.WriteU64(guid.High);
        writer.WriteU64(guid.Low);
    }
    writer.WriteU32(static_cast<uint32_t>(payload.SourceFiles.size()));
    for (const auto &path : payload.SourceFiles)
        writer.WriteString(path.generic_string());
    bytes = writer.TakeData();
    return true;
}

bool DecodeAssetDragPayload(std::span<const std::byte> bytes, AssetDragPayload &payload, std::string *error)
{
    if (bytes.size() > 4 * 1024 * 1024)
    {
        if (error)
            *error = "Drag payload is larger than 4 MiB";
        return false;
    }
    resources::BinaryReader reader(bytes);
    const auto failed = [&] {
        if (error && error->empty())
            *error = reader.Error();
        return false;
    };
    std::string magic;
    uint32_t version = 0, assetCount = 0, sourceCount = 0;
    if (!reader.ReadString(magic, 32) || magic != "SLA_ASSET_DROP" || !reader.ReadU32(version) || version != 1 ||
        !reader.ReadU32(assetCount) || assetCount > 4096)
    {
        if (error && error->empty())
            *error = "Invalid asset drag payload header";
        return false;
    }
    AssetDragPayload decoded;
    decoded.Assets.reserve(assetCount);
    for (uint32_t index = 0; index < assetCount; ++index)
    {
        uint64_t high = 0, low = 0;
        if (!reader.ReadU64(high) || !reader.ReadU64(low))
            return failed();
        decoded.Assets.push_back({high, low});
    }
    if (!reader.ReadU32(sourceCount) || sourceCount > 4096)
    {
        if (error && error->empty())
            *error = "Invalid source count in asset drag payload";
        return false;
    }
    decoded.SourceFiles.reserve(sourceCount);
    for (uint32_t index = 0; index < sourceCount; ++index)
    {
        std::string path;
        if (!reader.ReadString(path, 32 * 1024))
            return failed();
        decoded.SourceFiles.emplace_back(std::move(path));
    }
    if (reader.Remaining() != 0)
    {
        if (error)
            *error = "Asset drag payload contains trailing data";
        return false;
    }
    payload = std::move(decoded);
    return true;
}

AssetDropResult ProcessAssetDrop(AssetPipeline &pipeline, const AssetDragPayload &payload,
                                 const std::filesystem::path &destinationDirectory)
{
    AssetDropResult result;
    for (AssetGuid guid : payload.Assets)
    {
        if (pipeline.Database().Find(guid))
            result.ReferencedAssets.push_back(guid);
        else
            result.Diagnostics.push_back(DropDiagnostic(AssetDiagnosticSeverity::RecoverableError,
                                                        "dropped_asset_missing",
                                                        "Dropped asset no longer exists: " + guid.ToString()));
    }
    if (!payload.SourceFiles.empty())
    {
        result.Imported = pipeline.ImportDroppedFiles(payload.SourceFiles, destinationDirectory);
        result.Diagnostics.insert(result.Diagnostics.end(), result.Imported.Diagnostics.begin(),
                                  result.Imported.Diagnostics.end());
    }
    result.Succeeded = std::none_of(result.Diagnostics.begin(), result.Diagnostics.end(),
                                    [](const AssetDiagnostic &diagnostic) {
                                        return diagnostic.Severity == AssetDiagnosticSeverity::FatalError;
                                    }) &&
                       (payload.SourceFiles.empty() || result.Imported.Succeeded);
    return result;
}

AssetPreviewService::AssetPreviewService(const AssetTypeRegistry &types, const AssetDatabase &database,
                                         std::filesystem::path projectRoot)
    : m_types(types), m_database(database), m_projectRoot(std::move(projectRoot))
{
}

std::string AssetPreviewService::BuildRequestKey(const AssetRecord &record, uint32_t width, uint32_t height) const
{
    AssetFingerprint fingerprint = record.Descriptor.LastTransformFingerprint;
    if (!fingerprint.IsValid())
        fingerprint = HashString(record.Descriptor.Guid.ToString() + ':' + SerializeAssetSettings(record.Descriptor));
    return record.Descriptor.Guid.ToString() + ':' + fingerprint.ToString() + ':' + std::to_string(width) + 'x' +
           std::to_string(height);
}

std::shared_future<AssetPreviewResult> AssetPreviewService::Request(AssetGuid guid, uint32_t maximumWidth,
                                                                    uint32_t maximumHeight)
{
    maximumWidth = std::clamp(maximumWidth, 1u, 4096u);
    maximumHeight = std::clamp(maximumHeight, 1u, 4096u);
    const auto record = m_database.Find(guid);
    if (!record)
    {
        auto promise = std::make_shared<std::promise<AssetPreviewResult>>();
        AssetPreviewResult result;
        result.Error = "Cannot preview unknown asset GUID: " + guid.ToString();
        promise->set_value(std::move(result));
        return promise->get_future().share();
    }
    const std::string key = BuildRequestKey(*record, maximumWidth, maximumHeight);
    std::scoped_lock lock(m_mutex);
    if (const auto existing = m_requests.find(key); existing != m_requests.end())
        return existing->second;

    const AssetTypeRegistration *type = m_types.Find(record->Descriptor.Type);
    const AssetPreviewProvider provider = type ? type->Preview : AssetPreviewProvider{};
    const std::filesystem::path root = m_projectRoot;
    const AssetRecord recordCopy = *record;
    auto task = concurrency::TaskSystem::Global().SubmitFuture(
        [provider, root, recordCopy, maximumWidth, maximumHeight] {
                      AssetPreviewResult result;
                      if (!provider)
                      {
                          result.Error = "Asset type '" + recordCopy.Descriptor.Type + "' has no preview provider";
                          return result;
                      }
                      AssetPreviewRequest request;
                      request.ProjectRoot = root;
                      request.DescriptorPath = recordCopy.DescriptorPath;
                      request.MaximumWidth = maximumWidth;
                      request.MaximumHeight = maximumHeight;
                      result.Succeeded = provider(recordCopy.Descriptor, request, result.Preview, &result.Error);
                      return result;
                  }, concurrency::TaskPriority::Low);
    auto future = task.SharedFuture();
    m_requests.emplace(key, future);
    m_assetKeys.emplace(guid, key);
    return future;
}

void AssetPreviewService::Invalidate(AssetGuid guid)
{
    std::scoped_lock lock(m_mutex);
    const auto range = m_assetKeys.equal_range(guid);
    for (auto it = range.first; it != range.second; ++it)
        m_requests.erase(it->second);
    m_assetKeys.erase(guid);
}

void AssetPreviewService::Clear()
{
    std::scoped_lock lock(m_mutex);
    m_requests.clear();
    m_assetKeys.clear();
}

size_t AssetPreviewService::CachedPreviewCount() const
{
    std::scoped_lock lock(m_mutex);
    return m_requests.size();
}

} // namespace engine::assets
