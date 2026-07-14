#pragma once

#include "engine/asset/AssetGuid.h"
#include "engine/asset/AssetHash.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace engine::resources
{

inline constexpr uint16_t kCookedResourceHeaderVersion = 1;
inline constexpr uint64_t kDefaultMaximumCookedPayload = 4ull * 1024ull * 1024ull * 1024ull;

enum class ResourceCompression : uint8_t
{
    None = 0,
    Rle = 1,
};

struct CookedResourceHeader
{
    uint16_t HeaderVersion = kCookedResourceHeaderVersion;
    uint32_t ResourceVersion = 1;
    assets::AssetGuid Asset;
    std::string AssetType;
    std::string ResourceType;
    std::string Platform;
    std::string Profile;
    uint32_t Flags = 0;
    ResourceCompression Compression = ResourceCompression::None;
    uint64_t PayloadSize = 0;
    uint64_t StoredPayloadSize = 0;
    assets::AssetFingerprint PayloadChecksum;
    assets::AssetFingerprint TransformFingerprint;
};

struct CookedResourceData
{
    CookedResourceHeader Header;
    std::vector<std::byte> Payload;
};

bool WriteCookedResource(const std::filesystem::path &path, CookedResourceHeader header,
                         std::span<const std::byte> payload,
                         ResourceCompression preferredCompression = ResourceCompression::None,
                         std::string *error = nullptr);
bool ReadCookedResource(const std::filesystem::path &path, CookedResourceData &resource, std::string *error = nullptr,
                        uint64_t maximumPayload = kDefaultMaximumCookedPayload);
bool ReadCookedResourceHeader(const std::filesystem::path &path, CookedResourceHeader &header,
                              std::string *error = nullptr);

std::string ToString(ResourceCompression compression);
bool ParseResourceCompression(std::string_view text, ResourceCompression &compression);

} // namespace engine::resources
