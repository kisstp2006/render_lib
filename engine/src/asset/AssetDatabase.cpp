#include "engine/asset/AssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <mutex>
#include <set>

namespace engine::assets
{
namespace
{

AssetDiagnostic MakeDiagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message,
                               const std::filesystem::path &source, std::string suggestion = {})
{
    AssetDiagnostic diagnostic;
    diagnostic.Severity = severity;
    diagnostic.Code = std::move(code);
    diagnostic.Message = std::move(message);
    diagnostic.Source = source;
    diagnostic.Suggestion = std::move(suggestion);
    diagnostic.Step = "asset database scan";
    return diagnostic;
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

} // namespace

bool AssetDatabaseScanResult::Succeeded() const
{
    return std::none_of(Diagnostics.begin(), Diagnostics.end(), [](const AssetDiagnostic &diagnostic) {
        return diagnostic.Severity == AssetDiagnosticSeverity::FatalError;
    });
}

AssetDatabase::AssetDatabase(const AssetTypeRegistry &types) : m_types(types)
{
}

std::string AssetDatabase::NormalizePathKey(const std::filesystem::path &path)
{
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(path, error);
    if (error)
        normalized = path;
    std::string key = normalized.lexically_normal().generic_string();
#ifdef _WIN32
    key = Lower(std::move(key));
#endif
    return key;
}

AssetDatabaseScanResult AssetDatabase::Scan(const std::filesystem::path &assetRoot,
                                            const AssetDatabaseScanOptions &options)
{
    AssetDatabaseScanResult result;
    std::error_code iteratorError;
    if (!std::filesystem::is_directory(assetRoot))
    {
        result.Diagnostics.push_back(
            MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "asset_root_missing",
                           "Asset root directory does not exist: " + assetRoot.generic_string(), assetRoot,
                           "Create the asset directory or correct the configured project path."));
        return result;
    }

    std::vector<std::pair<std::filesystem::path, AssetDescriptor>> loaded;
    for (std::filesystem::recursive_directory_iterator
             iterator(assetRoot, std::filesystem::directory_options::skip_permission_denied, iteratorError),
         end;
         iterator != end; iterator.increment(iteratorError))
    {
        if (iteratorError)
        {
            result.Diagnostics.push_back(
                MakeDiagnostic(AssetDiagnosticSeverity::Warning, "asset_scan_io",
                               "Could not inspect part of the asset tree: " + iteratorError.message(), assetRoot));
            iteratorError.clear();
            continue;
        }
        if (!iterator->is_regular_file())
            continue;
        const AssetTypeRegistration *extensionType =
            m_types.FindByDescriptorExtension(iterator->path().extension().generic_string());
        if (!extensionType)
            continue;

        AssetDescriptor descriptor;
        std::string error;
        if (!extensionType->LoadDescriptor(iterator->path(), descriptor, &error))
        {
            result.Diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "descriptor_load_failed",
                                                        error, iterator->path(),
                                                        "Repair or restore the descriptor from version control."));
            continue;
        }
        const AssetTypeRegistration *declaredType = m_types.Find(descriptor.Type);
        if (!declaredType)
        {
            ++result.UnknownTypes;
            result.Diagnostics.push_back(
                MakeDiagnostic(AssetDiagnosticSeverity::RecoverableError, "unknown_asset_type",
                               "Asset type is not registered: " + descriptor.Type, iterator->path(),
                               "Load the plugin that owns this type or migrate the descriptor."));
            continue;
        }
        if (descriptor.DescriptorVersion != declaredType->DescriptorVersion)
        {
            if (!options.MigrateDescriptors)
            {
                result.Diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::RecoverableError,
                                                            "descriptor_migration_required",
                                                            "Descriptor version requires migration", iterator->path()));
                continue;
            }
            std::filesystem::path backup;
            if (!MigrateAssetDescriptorFile(iterator->path(), m_types, &backup, &error) ||
                !declaredType->LoadDescriptor(iterator->path(), descriptor, &error))
            {
                result.Diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError,
                                                            "descriptor_migration_failed", error, iterator->path(),
                                                            "The original file was preserved; inspect its .bak copy."));
                continue;
            }
            ++result.Migrated;
        }
        loaded.emplace_back(iterator->path(), std::move(descriptor));
    }

    {
        std::unique_lock lock(m_mutex);
        m_assets.clear();
        m_paths.clear();
        m_duplicatePaths.clear();
        for (auto &[path, descriptor] : loaded)
        {
            const std::string pathKey = NormalizePathKey(path);
            const auto existing = m_assets.find(descriptor.Guid);
            if (existing != m_assets.end())
            {
                ++result.DuplicateGuids;
                m_duplicatePaths[descriptor.Guid.ToString()].push_back(path);
                m_duplicatePaths[descriptor.Guid.ToString()].push_back(existing->second.DescriptorPath);
                result.Diagnostics.push_back(MakeDiagnostic(
                    AssetDiagnosticSeverity::FatalError, "duplicate_asset_guid",
                    "Duplicate GUID " + descriptor.Guid.ToString() + " is used by '" +
                        existing->second.DescriptorPath.generic_string() + "' and '" + path.generic_string() + "'",
                    path, "Regenerate the GUID of the copied descriptor."));
                continue;
            }
            AssetRecord record;
            record.DescriptorPath = path;
            record.Descriptor = std::move(descriptor);
            m_paths[pathKey] = record.Descriptor.Guid;
            m_assets.emplace(record.Descriptor.Guid, std::move(record));
        }
        RebuildEdgesLocked();
        result.Assets = m_assets.size();
    }

    if (options.RepairDuplicateGuids && result.DuplicateGuids != 0)
    {
        // Repairing during scan would make ownership depend on filesystem
        // enumeration order. Call RegenerateDuplicateGuid explicitly instead.
        result.Diagnostics.push_back(MakeDiagnostic(
            AssetDiagnosticSeverity::Warning, "duplicate_repair_deferred",
            "Automatic duplicate repair is deliberately deferred; choose the copied descriptor explicitly.",
            assetRoot));
    }

    for (const auto &cycle : FindDependencyCycles())
    {
        std::string message = "Circular asset dependency:";
        for (AssetGuid guid : cycle)
            message += ' ' + guid.ToString();
        result.Diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "circular_asset_dependency",
                                                    message, assetRoot,
                                                    "Remove one of the references in the dependency cycle."));
    }
    return result;
}

bool AssetDatabase::AddOrUpdate(const std::filesystem::path &descriptorPath, const AssetDescriptor &descriptor,
                                std::string *error)
{
    if (!descriptor.Guid.IsValid())
    {
        if (error)
            *error = "Cannot register an asset with an invalid GUID";
        return false;
    }
    if (!m_types.Find(descriptor.Type))
    {
        if (error)
            *error = "Cannot register unknown asset type '" + descriptor.Type + "'";
        return false;
    }
    std::unique_lock lock(m_mutex);
    const std::string pathKey = NormalizePathKey(descriptorPath);
    const auto pathOwner = m_paths.find(pathKey);
    if (pathOwner != m_paths.end() && pathOwner->second != descriptor.Guid)
    {
        if (error)
            *error = "Descriptor path is already owned by another asset";
        return false;
    }
    const auto guidOwner = m_assets.find(descriptor.Guid);
    if (guidOwner != m_assets.end() && NormalizePathKey(guidOwner->second.DescriptorPath) != pathKey)
    {
        if (error)
            *error = "Asset GUID is already used by " + guidOwner->second.DescriptorPath.generic_string();
        return false;
    }
    if (guidOwner != m_assets.end())
        m_paths.erase(NormalizePathKey(guidOwner->second.DescriptorPath));
    m_assets[descriptor.Guid] = AssetRecord{descriptorPath, descriptor};
    m_paths[pathKey] = descriptor.Guid;
    RebuildEdgesLocked();
    return true;
}

bool AssetDatabase::Remove(AssetGuid guid)
{
    std::unique_lock lock(m_mutex);
    const auto found = m_assets.find(guid);
    if (found == m_assets.end())
        return false;
    m_paths.erase(NormalizePathKey(found->second.DescriptorPath));
    m_assets.erase(found);
    RebuildEdgesLocked();
    return true;
}

void AssetDatabase::Clear()
{
    std::unique_lock lock(m_mutex);
    m_assets.clear();
    m_paths.clear();
    m_dependencies.clear();
    m_dependents.clear();
    m_sourceDependents.clear();
    m_duplicatePaths.clear();
}

std::optional<AssetRecord> AssetDatabase::Find(AssetGuid guid) const
{
    std::shared_lock lock(m_mutex);
    const auto found = m_assets.find(guid);
    return found == m_assets.end() ? std::nullopt : std::optional<AssetRecord>(found->second);
}

std::optional<AssetRecord> AssetDatabase::FindByPath(const std::filesystem::path &descriptorPath) const
{
    std::shared_lock lock(m_mutex);
    const auto path = m_paths.find(NormalizePathKey(descriptorPath));
    if (path == m_paths.end())
        return std::nullopt;
    const auto found = m_assets.find(path->second);
    return found == m_assets.end() ? std::nullopt : std::optional<AssetRecord>(found->second);
}

std::optional<AssetGuid> AssetDatabase::ResolveGuid(const std::filesystem::path &descriptorPath) const
{
    std::shared_lock lock(m_mutex);
    const auto found = m_paths.find(NormalizePathKey(descriptorPath));
    return found == m_paths.end() ? std::nullopt : std::optional<AssetGuid>(found->second);
}

std::optional<std::filesystem::path> AssetDatabase::ResolvePath(AssetGuid guid) const
{
    std::shared_lock lock(m_mutex);
    const auto found = m_assets.find(guid);
    return found == m_assets.end() ? std::nullopt : std::optional<std::filesystem::path>(found->second.DescriptorPath);
}

std::vector<AssetGuid> AssetDatabase::GetDependencies(AssetGuid guid) const
{
    std::shared_lock lock(m_mutex);
    const auto found = m_dependencies.find(guid);
    return found == m_dependencies.end() ? std::vector<AssetGuid>{} : found->second;
}

std::vector<AssetGuid> AssetDatabase::GetDependents(AssetGuid guid) const
{
    std::shared_lock lock(m_mutex);
    const auto found = m_dependents.find(guid);
    return found == m_dependents.end() ? std::vector<AssetGuid>{} : found->second;
}

std::vector<AssetGuid> AssetDatabase::GetSourceDependents(const std::filesystem::path &projectRelativeSource) const
{
    std::shared_lock lock(m_mutex);
    const auto found = m_sourceDependents.find(projectRelativeSource.lexically_normal().generic_string());
    return found == m_sourceDependents.end() ? std::vector<AssetGuid>{} : found->second;
}

std::vector<std::vector<AssetGuid>> AssetDatabase::FindDependencyCycles() const
{
    std::shared_lock lock(m_mutex);
    enum class Visit : uint8_t
    {
        None,
        Active,
        Done
    };
    std::unordered_map<AssetGuid, Visit, AssetGuidHash> visits;
    std::vector<AssetGuid> stack;
    std::vector<std::vector<AssetGuid>> cycles;
    std::set<std::string> unique;
    std::function<void(AssetGuid)> visit = [&](AssetGuid guid) {
        visits[guid] = Visit::Active;
        stack.push_back(guid);
        const auto dependencies = m_dependencies.find(guid);
        if (dependencies != m_dependencies.end())
        {
            for (AssetGuid dependency : dependencies->second)
            {
                if (!m_assets.contains(dependency))
                    continue;
                if (visits[dependency] == Visit::None)
                    visit(dependency);
                else if (visits[dependency] == Visit::Active)
                {
                    const auto first = std::find(stack.begin(), stack.end(), dependency);
                    std::vector<AssetGuid> cycle(first, stack.end());
                    cycle.push_back(dependency);
                    std::vector<std::string> ids;
                    for (AssetGuid item : cycle)
                        ids.push_back(item.ToString());
                    std::sort(ids.begin(), ids.end());
                    std::string key;
                    for (const auto &id : ids)
                        key += id;
                    if (unique.insert(key).second)
                        cycles.push_back(std::move(cycle));
                }
            }
        }
        stack.pop_back();
        visits[guid] = Visit::Done;
    };
    for (const auto &[guid, record] : m_assets)
    {
        (void)record;
        if (visits[guid] == Visit::None)
            visit(guid);
    }
    return cycles;
}

std::vector<AssetGuid> AssetDatabase::FindMissingDependencies(AssetGuid guid) const
{
    std::shared_lock lock(m_mutex);
    std::vector<AssetGuid> missing;
    const auto found = m_dependencies.find(guid);
    if (found == m_dependencies.end())
        return missing;
    for (AssetGuid dependency : found->second)
        if (!m_assets.contains(dependency))
            missing.push_back(dependency);
    return missing;
}

void AssetDatabase::MarkDirtyLocked(AssetGuid guid, bool includeDependents,
                                    std::unordered_set<AssetGuid, AssetGuidHash> &visited)
{
    if (!visited.insert(guid).second)
        return;
    const auto asset = m_assets.find(guid);
    if (asset == m_assets.end())
        return;
    asset->second.Descriptor.State = AssetImportState::Dirty;
    if (!includeDependents)
        return;
    const auto dependents = m_dependents.find(guid);
    if (dependents != m_dependents.end())
        for (AssetGuid dependent : dependents->second)
            MarkDirtyLocked(dependent, true, visited);
}

void AssetDatabase::MarkDirty(AssetGuid guid, bool includeDependents)
{
    std::unique_lock lock(m_mutex);
    std::unordered_set<AssetGuid, AssetGuidHash> visited;
    MarkDirtyLocked(guid, includeDependents, visited);
}

std::vector<AssetBrowserEntry> AssetDatabase::Query(const AssetBrowserQuery &query) const
{
    std::shared_lock lock(m_mutex);
    const std::string search = Lower(query.Search);
    std::vector<AssetBrowserEntry> result;
    for (const auto &[guid, record] : m_assets)
    {
        const AssetDescriptor &descriptor = record.Descriptor;
        if (!query.Type.empty() && descriptor.Type != query.Type)
            continue;
        if (query.State && descriptor.State != *query.State)
            continue;
        if (!query.Tag.empty() &&
            std::find(descriptor.Tags.begin(), descriptor.Tags.end(), query.Tag) == descriptor.Tags.end())
            continue;
        if (!search.empty())
        {
            const std::string haystack =
                Lower(descriptor.Name + ' ' + descriptor.Type + ' ' + record.DescriptorPath.generic_string());
            if (haystack.find(search) == std::string::npos)
                continue;
        }
        AssetBrowserEntry entry;
        entry.Guid = guid;
        entry.Name = descriptor.Name;
        entry.Type = descriptor.Type;
        entry.DescriptorPath = record.DescriptorPath;
        entry.State = descriptor.State;
        entry.Tags = descriptor.Tags;
        entry.ThumbnailKey = descriptor.LastTransformFingerprint;
        if (const auto deps = m_dependencies.find(guid); deps != m_dependencies.end())
            entry.DependencyCount = deps->second.size();
        if (const auto users = m_dependents.find(guid); users != m_dependents.end())
            entry.DependentCount = users->second.size();
        result.push_back(std::move(entry));
    }
    std::sort(result.begin(), result.end(), [](const AssetBrowserEntry &left, const AssetBrowserEntry &right) {
        if (left.Type != right.Type)
            return left.Type < right.Type;
        if (left.Name != right.Name)
            return left.Name < right.Name;
        return left.Guid < right.Guid;
    });
    return result;
}

size_t AssetDatabase::Size() const
{
    std::shared_lock lock(m_mutex);
    return m_assets.size();
}

bool AssetDatabase::RegenerateDuplicateGuid(const std::filesystem::path &descriptorPath, AssetGuid *newGuid,
                                            std::string *error)
{
    const AssetTypeRegistration *type = m_types.FindByDescriptorExtension(descriptorPath.extension().generic_string());
    if (!type)
    {
        if (error)
            *error = "Descriptor extension is not registered";
        return false;
    }
    AssetDescriptor descriptor;
    if (!type->LoadDescriptor(descriptorPath, descriptor, error))
        return false;
    descriptor.Guid = AssetGuid::Generate();
    descriptor.State = AssetImportState::Dirty;
    descriptor.LastTransform = {};
    descriptor.LastTransformFingerprint = {};
    if (!type->SaveDescriptor(descriptorPath, descriptor, error))
        return false;
    if (newGuid)
        *newGuid = descriptor.Guid;
    return true;
}

void AssetDatabase::RebuildEdgesLocked()
{
    m_dependencies.clear();
    m_dependents.clear();
    m_sourceDependents.clear();
    for (const auto &[guid, record] : m_assets)
    {
        auto dependencies = record.Descriptor.AssetDependencies;
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        m_dependencies[guid] = dependencies;
        for (AssetGuid dependency : dependencies)
            m_dependents[dependency].push_back(guid);
        for (const auto &source : record.Descriptor.SourceDependencies)
            m_sourceDependents[source.lexically_normal().generic_string()].push_back(guid);
        for (const auto &source : record.Descriptor.Sources)
            m_sourceDependents[source.lexically_normal().generic_string()].push_back(guid);
    }
    for (auto &[dependency, values] : m_dependents)
    {
        (void)dependency;
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    }
    for (auto &[source, values] : m_sourceDependents)
    {
        (void)source;
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    }
}

} // namespace engine::assets
