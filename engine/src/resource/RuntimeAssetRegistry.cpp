#include "engine/resource/RuntimeAssetRegistry.h"

#include "engine/asset/AssetFileSystem.h"
#include "engine/resource/BinaryIO.h"

#include <algorithm>
#include <array>
#include <mutex>

namespace engine::resources
{
namespace
{

constexpr std::array<std::byte, 8> kRegistryMagic = {
    static_cast<std::byte>('S'), static_cast<std::byte>('L'), static_cast<std::byte>('A'), static_cast<std::byte>('R'),
    static_cast<std::byte>('E'), static_cast<std::byte>('G'), static_cast<std::byte>(0),   static_cast<std::byte>(1),
};

} // namespace

bool RuntimeAssetRegistry::Register(RuntimeAssetEntry entry, std::string *error)
{
    if (!entry.Guid.IsValid() || entry.AssetType.empty() || entry.ResourceType.empty() || entry.CookedPath.empty() ||
        entry.ResourceVersion == 0)
    {
        if (error)
            *error = "Runtime asset registry entry is incomplete";
        return false;
    }
    std::unique_lock lock(m_mutex);
    m_entries.insert_or_assign(entry.Guid, std::move(entry));
    return true;
}

bool RuntimeAssetRegistry::Remove(assets::AssetGuid guid)
{
    std::unique_lock lock(m_mutex);
    return m_entries.erase(guid) != 0;
}

std::optional<RuntimeAssetEntry> RuntimeAssetRegistry::Resolve(assets::AssetGuid guid) const
{
    std::shared_lock lock(m_mutex);
    const auto found = m_entries.find(guid);
    return found == m_entries.end() ? std::nullopt : std::optional<RuntimeAssetEntry>(found->second);
}

std::vector<RuntimeAssetEntry> RuntimeAssetRegistry::List() const
{
    std::shared_lock lock(m_mutex);
    std::vector<RuntimeAssetEntry> entries;
    entries.reserve(m_entries.size());
    for (const auto &[guid, entry] : m_entries)
    {
        (void)guid;
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto &left, const auto &right) { return left.Guid < right.Guid; });
    return entries;
}

void RuntimeAssetRegistry::Clear()
{
    std::unique_lock lock(m_mutex);
    m_entries.clear();
}

bool RuntimeAssetRegistry::Save(const std::filesystem::path &path, std::string *error) const
{
    const std::vector<RuntimeAssetEntry> entries = List();
    BinaryWriter writer;
    writer.WriteBytes(kRegistryMagic);
    writer.WriteU32(1);
    writer.WriteU32(static_cast<uint32_t>(entries.size()));
    for (const RuntimeAssetEntry &entry : entries)
    {
        writer.WriteU64(entry.Guid.High);
        writer.WriteU64(entry.Guid.Low);
        writer.WriteString(entry.AssetType);
        writer.WriteString(entry.ResourceType);
        writer.WriteString(entry.CookedPath.generic_string());
        writer.WriteU32(entry.ResourceVersion);
        writer.WriteBytes(std::span(reinterpret_cast<const std::byte *>(entry.TransformFingerprint.Bytes.data()),
                                    entry.TransformFingerprint.Bytes.size()));
    }
    return assets::WriteFileAtomic(path, writer.Data(), error);
}

bool RuntimeAssetRegistry::Load(const std::filesystem::path &path, std::string *error)
{
    std::vector<std::byte> bytes;
    if (!assets::ReadFileBytes(path, bytes, error, 256ull * 1024ull * 1024ull))
        return false;
    BinaryReader reader(bytes);
    std::span<const std::byte> magic;
    uint32_t version = 0;
    uint32_t count = 0;
    if (!reader.ReadBytes(kRegistryMagic.size(), magic) ||
        !std::equal(magic.begin(), magic.end(), kRegistryMagic.begin()) || !reader.ReadU32(version) || version != 1 ||
        !reader.ReadU32(count) || count > 4'000'000)
    {
        if (error)
            *error = "Invalid or unsupported runtime asset registry";
        return false;
    }
    std::unordered_map<assets::AssetGuid, RuntimeAssetEntry, assets::AssetGuidHash> loaded;
    loaded.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        RuntimeAssetEntry entry;
        std::string pathText;
        std::span<const std::byte> fingerprint;
        if (!reader.ReadU64(entry.Guid.High) || !reader.ReadU64(entry.Guid.Low) ||
            !reader.ReadString(entry.AssetType, 1024) || !reader.ReadString(entry.ResourceType, 1024) ||
            !reader.ReadString(pathText, 1024 * 1024) || !reader.ReadU32(entry.ResourceVersion) ||
            !reader.ReadBytes(entry.TransformFingerprint.Bytes.size(), fingerprint))
        {
            if (error)
                *error = reader.Error();
            return false;
        }
        if (!entry.Guid.IsValid() || entry.AssetType.empty() || entry.ResourceType.empty() || pathText.empty() ||
            entry.ResourceVersion == 0)
        {
            if (error)
                *error = "Runtime asset registry contains an incomplete entry";
            return false;
        }
        std::copy(fingerprint.begin(), fingerprint.end(),
                  reinterpret_cast<std::byte *>(entry.TransformFingerprint.Bytes.data()));
        entry.CookedPath = std::move(pathText);
        if (!loaded.emplace(entry.Guid, std::move(entry)).second)
        {
            if (error)
                *error = "Runtime asset registry contains a duplicate GUID";
            return false;
        }
    }
    if (reader.Remaining() != 0)
    {
        if (error)
            *error = "Runtime asset registry has trailing data";
        return false;
    }
    std::unique_lock lock(m_mutex);
    m_entries = std::move(loaded);
    return true;
}

} // namespace engine::resources
