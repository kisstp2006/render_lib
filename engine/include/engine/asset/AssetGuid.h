#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine::assets
{

struct AssetGuid
{
    uint64_t High = 0;
    uint64_t Low = 0;

    [[nodiscard]] bool IsValid() const
    {
        return High != 0 || Low != 0;
    }
    [[nodiscard]] std::string ToString() const;

    static AssetGuid Generate();
    static std::optional<AssetGuid> Parse(std::string_view text);

    friend bool operator==(const AssetGuid &, const AssetGuid &) = default;
    friend auto operator<=>(const AssetGuid &, const AssetGuid &) = default;
};

struct AssetGuidHash
{
    size_t operator()(const AssetGuid &guid) const noexcept;
};

template <typename ResourceType> struct AssetHandle
{
    AssetGuid Guid;

    [[nodiscard]] bool IsValid() const
    {
        return Guid.IsValid();
    }
    explicit operator bool() const
    {
        return IsValid();
    }
    friend bool operator==(const AssetHandle &, const AssetHandle &) = default;
};

} // namespace engine::assets
