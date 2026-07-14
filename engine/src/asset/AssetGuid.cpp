#include "engine/asset/AssetGuid.h"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>

namespace engine::assets
{
namespace
{

std::atomic<uint64_t> g_counter{1};
std::mutex g_randomMutex;

uint64_t Random64()
{
    std::scoped_lock lock(g_randomMutex);
    static std::mt19937_64 generator([] {
        std::random_device random;
        std::seed_seq seed{
            random(),
            random(),
            random(),
            random(),
            static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
        };
        return std::mt19937_64(seed);
    }());
    return generator();
}

bool ParseHex64(std::string_view text, uint64_t &value)
{
    if (text.size() != 16)
        return false;
    const char *first = text.data();
    const char *last = first + text.size();
    const auto result = std::from_chars(first, last, value, 16);
    return result.ec == std::errc{} && result.ptr == last;
}

} // namespace

AssetGuid AssetGuid::Generate()
{
    AssetGuid guid;
    const uint64_t sequence = g_counter.fetch_add(1, std::memory_order_relaxed);
    guid.High =
        Random64() ^ static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    guid.Low = Random64() ^ (sequence * 0x9e3779b97f4a7c15ull);

    // RFC 4122 version 4 and variant bits. The representation is deliberately
    // stored as two integers; formatting is the only place that knows groups.
    guid.High = (guid.High & 0xffffffffffff0fffull) | 0x0000000000004000ull;
    guid.Low = (guid.Low & 0x3fffffffffffffffull) | 0x8000000000000000ull;
    if (!guid.IsValid())
        guid.Low = 1;
    return guid;
}

std::string AssetGuid::ToString() const
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::nouppercase << std::setw(8) << static_cast<uint32_t>(High >> 32u)
           << '-' << std::setw(4) << static_cast<uint16_t>(High >> 16u) << '-' << std::setw(4)
           << static_cast<uint16_t>(High) << '-' << std::setw(4) << static_cast<uint16_t>(Low >> 48u) << '-'
           << std::setw(12) << (Low & 0x0000ffffffffffffull);
    return stream.str();
}

std::optional<AssetGuid> AssetGuid::Parse(std::string_view text)
{
    std::array<char, 32> compact{};
    size_t count = 0;
    for (const char character : text)
    {
        if (character == '-' || character == '{' || character == '}' || character == ' ')
            continue;
        if (count >= compact.size())
            return std::nullopt;
        compact[count++] = character;
    }
    if (count != compact.size())
        return std::nullopt;

    AssetGuid guid;
    if (!ParseHex64(std::string_view(compact.data(), 16), guid.High) ||
        !ParseHex64(std::string_view(compact.data() + 16, 16), guid.Low) || !guid.IsValid())
        return std::nullopt;
    return guid;
}

size_t AssetGuidHash::operator()(const AssetGuid &guid) const noexcept
{
    uint64_t value = guid.High ^ (guid.Low + 0x9e3779b97f4a7c15ull + (guid.High << 6u) + (guid.High >> 2u));
    if constexpr (sizeof(size_t) >= sizeof(uint64_t))
        return static_cast<size_t>(value);
    else
        return static_cast<size_t>(value ^ (value >> 32u));
}

} // namespace engine::assets
