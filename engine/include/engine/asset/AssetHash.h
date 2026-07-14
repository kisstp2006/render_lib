#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace engine::assets
{

struct AssetFingerprint
{
    std::array<uint8_t, 32> Bytes{};

    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] std::string ToString() const;
    static bool Parse(std::string_view text, AssetFingerprint &fingerprint);

    friend bool operator==(const AssetFingerprint &, const AssetFingerprint &) = default;
};

AssetFingerprint HashBytes(std::span<const std::byte> bytes);
AssetFingerprint HashBytes(std::span<const uint8_t> bytes);
AssetFingerprint HashString(std::string_view text);
AssetFingerprint HashFile(const std::filesystem::path &path, std::string *error = nullptr);
AssetFingerprint CombineFingerprints(std::span<const AssetFingerprint> fingerprints);

} // namespace engine::assets
