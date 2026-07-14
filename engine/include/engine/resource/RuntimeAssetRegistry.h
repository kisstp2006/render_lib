#pragma once

#include "engine/asset/AssetGuid.h"
#include "engine/asset/AssetHash.h"

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::resources
{

struct RuntimeAssetEntry
{
    assets::AssetGuid Guid;
    std::string AssetType;
    std::string ResourceType;
    std::filesystem::path CookedPath;
    uint32_t ResourceVersion = 1;
    assets::AssetFingerprint TransformFingerprint;
};

class RuntimeAssetRegistry
{
  public:
    bool Register(RuntimeAssetEntry entry, std::string *error = nullptr);
    bool Remove(assets::AssetGuid guid);
    [[nodiscard]] std::optional<RuntimeAssetEntry> Resolve(assets::AssetGuid guid) const;
    [[nodiscard]] std::vector<RuntimeAssetEntry> List() const;
    void Clear();

    bool Save(const std::filesystem::path &path, std::string *error = nullptr) const;
    bool Load(const std::filesystem::path &path, std::string *error = nullptr);

  private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<assets::AssetGuid, RuntimeAssetEntry, assets::AssetGuidHash> m_entries;
};

} // namespace engine::resources
