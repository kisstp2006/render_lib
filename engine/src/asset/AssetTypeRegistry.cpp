#include "engine/asset/AssetTypeRegistry.h"

#include "engine/asset/AssetFileSystem.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace engine::assets
{

std::string AssetTypeRegistry::NormalizeExtension(std::string_view extension)
{
    if (!extension.empty() && extension.front() == '.')
        extension.remove_prefix(1);
    std::string normalized(extension);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return normalized;
}

bool AssetTypeRegistry::Register(AssetTypeRegistration registration, std::string *error)
{
    if (registration.TypeId.empty() || registration.DisplayName.empty() || registration.DescriptorExtension.empty() ||
        registration.RuntimeType.empty() || registration.DescriptorVersion == 0 || registration.ImporterVersion == 0 ||
        registration.TransformerVersion == 0 || registration.ResourceVersion == 0)
    {
        if (error)
            *error = "Asset type registration is missing an ID, name, extension, runtime type or version";
        return false;
    }
    registration.DescriptorExtension = NormalizeExtension(registration.DescriptorExtension);
    for (std::string &extension : registration.SourceExtensions)
        extension = NormalizeExtension(extension);
    std::sort(registration.SourceExtensions.begin(), registration.SourceExtensions.end());
    registration.SourceExtensions.erase(
        std::unique(registration.SourceExtensions.begin(), registration.SourceExtensions.end()),
        registration.SourceExtensions.end());
    if (!registration.LoadDescriptor)
        registration.LoadDescriptor = LoadAssetDescriptor;
    if (!registration.SaveDescriptor)
        registration.SaveDescriptor = SaveAssetDescriptor;

    std::unique_lock lock(m_mutex);
    if (m_types.contains(registration.TypeId))
    {
        if (error)
            *error = "Asset type is already registered: " + registration.TypeId;
        return false;
    }
    if (m_descriptorExtensions.contains(registration.DescriptorExtension))
    {
        if (error)
            *error = "Descriptor extension is already registered: ." + registration.DescriptorExtension;
        return false;
    }

    const std::string typeId = registration.TypeId;
    m_descriptorExtensions.emplace(registration.DescriptorExtension, typeId);
    for (const std::string &extension : registration.SourceExtensions)
        m_sourceExtensions.emplace(extension, typeId);
    m_types.emplace(typeId, std::move(registration));
    return true;
}

bool AssetTypeRegistry::Unregister(std::string_view typeId)
{
    std::unique_lock lock(m_mutex);
    const auto found = m_types.find(std::string(typeId));
    if (found == m_types.end())
        return false;
    m_descriptorExtensions.erase(found->second.DescriptorExtension);
    for (auto iterator = m_sourceExtensions.begin(); iterator != m_sourceExtensions.end();)
    {
        if (iterator->second == typeId)
            iterator = m_sourceExtensions.erase(iterator);
        else
            ++iterator;
    }
    m_types.erase(found);
    return true;
}

const AssetTypeRegistration *AssetTypeRegistry::Find(std::string_view typeId) const
{
    std::shared_lock lock(m_mutex);
    const auto found = m_types.find(std::string(typeId));
    return found == m_types.end() ? nullptr : &found->second;
}

const AssetTypeRegistration *AssetTypeRegistry::FindByDescriptorExtension(std::string_view extension) const
{
    const std::string normalized = NormalizeExtension(extension);
    std::shared_lock lock(m_mutex);
    const auto extensionEntry = m_descriptorExtensions.find(normalized);
    if (extensionEntry == m_descriptorExtensions.end())
        return nullptr;
    const auto type = m_types.find(extensionEntry->second);
    return type == m_types.end() ? nullptr : &type->second;
}

std::vector<const AssetTypeRegistration *> AssetTypeRegistry::FindImportersForExtension(
    std::string_view extension) const
{
    const std::string normalized = NormalizeExtension(extension);
    std::shared_lock lock(m_mutex);
    std::vector<const AssetTypeRegistration *> results;
    const auto [first, last] = m_sourceExtensions.equal_range(normalized);
    for (auto iterator = first; iterator != last; ++iterator)
    {
        const auto type = m_types.find(iterator->second);
        if (type != m_types.end() && type->second.Import)
            results.push_back(&type->second);
    }
    std::sort(results.begin(), results.end(), [](const auto *left, const auto *right) {
        const int leftPriority =
            left->ImportModes.empty()
                ? 0
                : std::max_element(left->ImportModes.begin(), left->ImportModes.end(),
                                   [](const auto &a, const auto &b) { return a.Priority < b.Priority; })
                      ->Priority;
        const int rightPriority =
            right->ImportModes.empty()
                ? 0
                : std::max_element(right->ImportModes.begin(), right->ImportModes.end(),
                                   [](const auto &a, const auto &b) { return a.Priority < b.Priority; })
                      ->Priority;
        if (leftPriority != rightPriority)
            return leftPriority > rightPriority;
        return left->TypeId < right->TypeId;
    });
    return results;
}

std::vector<const AssetTypeRegistration *> AssetTypeRegistry::ListTypes() const
{
    std::shared_lock lock(m_mutex);
    std::vector<const AssetTypeRegistration *> types;
    types.reserve(m_types.size());
    for (const auto &[id, registration] : m_types)
    {
        (void)id;
        types.push_back(&registration);
    }
    return types;
}

bool MigrateAssetDescriptor(AssetDescriptor &descriptor, const AssetTypeRegistration &type, std::string *error)
{
    if (descriptor.Type != type.TypeId)
    {
        if (error)
            *error = "Descriptor type '" + descriptor.Type + "' does not match migration type '" + type.TypeId + "'";
        return false;
    }
    if (descriptor.DescriptorVersion > type.DescriptorVersion)
    {
        if (error)
            *error = "Descriptor version " + std::to_string(descriptor.DescriptorVersion) +
                     " is newer than supported version " + std::to_string(type.DescriptorVersion);
        return false;
    }
    while (descriptor.DescriptorVersion < type.DescriptorVersion)
    {
        const uint32_t oldVersion = descriptor.DescriptorVersion;
        const auto migration = type.Migrations.find(oldVersion);
        if (migration == type.Migrations.end())
        {
            if (error)
                *error = "No migration registered for asset type '" + type.TypeId + "' from version " +
                         std::to_string(oldVersion);
            return false;
        }
        if (!migration->second(descriptor, error))
        {
            if (error && error->empty())
                *error =
                    "Migration failed for asset type '" + type.TypeId + "' from version " + std::to_string(oldVersion);
            return false;
        }
        if (descriptor.DescriptorVersion != oldVersion)
        {
            if (error)
                *error = "Migration callback modified DescriptorVersion directly";
            return false;
        }
        descriptor.DescriptorVersion = oldVersion + 1;
    }
    return true;
}

bool MigrateAssetDescriptorFile(const std::filesystem::path &path, const AssetTypeRegistry &registry,
                                std::filesystem::path *backupPath, std::string *error)
{
    AssetDescriptor descriptor;
    if (!LoadAssetDescriptor(path, descriptor, error))
        return false;
    const AssetTypeRegistration *type = registry.Find(descriptor.Type);
    if (!type)
    {
        if (error)
            *error = "Cannot migrate unknown asset type '" + descriptor.Type + "'";
        return false;
    }
    if (descriptor.DescriptorVersion == type->DescriptorVersion)
    {
        if (backupPath)
            backupPath->clear();
        return true;
    }

    AssetDescriptor migrated = descriptor;
    if (!MigrateAssetDescriptor(migrated, *type, error))
        return false;
    std::filesystem::path backup;
    if (!CreateFileBackup(path, backup, error))
        return false;
    if (!type->SaveDescriptor(path, migrated, error))
        return false;
    if (backupPath)
        *backupPath = std::move(backup);
    return true;
}

} // namespace engine::assets
