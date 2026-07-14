#include "engine/asset/AssetHash.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace engine::assets
{
namespace
{

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr uint32_t RotateRight(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32u - shift));
}

class Sha256
{
  public:
    void Update(std::span<const uint8_t> bytes)
    {
        m_totalBytes += bytes.size();
        size_t offset = 0;
        if (m_bufferSize != 0)
        {
            const size_t copyCount = std::min(bytes.size(), m_buffer.size() - m_bufferSize);
            std::copy_n(bytes.data(), copyCount, m_buffer.data() + m_bufferSize);
            m_bufferSize += copyCount;
            offset += copyCount;
            if (m_bufferSize == m_buffer.size())
            {
                Transform(m_buffer.data());
                m_bufferSize = 0;
            }
        }
        while (offset + m_buffer.size() <= bytes.size())
        {
            Transform(bytes.data() + offset);
            offset += m_buffer.size();
        }
        if (offset < bytes.size())
        {
            m_bufferSize = bytes.size() - offset;
            std::copy_n(bytes.data() + offset, m_bufferSize, m_buffer.data());
        }
    }

    AssetFingerprint Finish()
    {
        const uint64_t bitCount = m_totalBytes * 8ull;
        m_buffer[m_bufferSize++] = 0x80u;
        if (m_bufferSize > 56)
        {
            std::fill(m_buffer.begin() + static_cast<ptrdiff_t>(m_bufferSize), m_buffer.end(), uint8_t{0});
            Transform(m_buffer.data());
            m_bufferSize = 0;
        }
        std::fill(m_buffer.begin() + static_cast<ptrdiff_t>(m_bufferSize), m_buffer.begin() + 56, uint8_t{0});
        for (size_t index = 0; index < 8; ++index)
            m_buffer[63 - index] = static_cast<uint8_t>(bitCount >> (index * 8u));
        Transform(m_buffer.data());

        AssetFingerprint result;
        for (size_t index = 0; index < m_state.size(); ++index)
        {
            result.Bytes[index * 4 + 0] = static_cast<uint8_t>(m_state[index] >> 24u);
            result.Bytes[index * 4 + 1] = static_cast<uint8_t>(m_state[index] >> 16u);
            result.Bytes[index * 4 + 2] = static_cast<uint8_t>(m_state[index] >> 8u);
            result.Bytes[index * 4 + 3] = static_cast<uint8_t>(m_state[index]);
        }
        return result;
    }

  private:
    void Transform(const uint8_t *block)
    {
        std::array<uint32_t, 64> words{};
        for (size_t index = 0; index < 16; ++index)
        {
            const size_t offset = index * 4;
            words[index] = (static_cast<uint32_t>(block[offset]) << 24u) |
                           (static_cast<uint32_t>(block[offset + 1]) << 16u) |
                           (static_cast<uint32_t>(block[offset + 2]) << 8u) | static_cast<uint32_t>(block[offset + 3]);
        }
        for (size_t index = 16; index < words.size(); ++index)
        {
            const uint32_t s0 =
                RotateRight(words[index - 15], 7) ^ RotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3u);
            const uint32_t s1 =
                RotateRight(words[index - 2], 17) ^ RotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10u);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }

        uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
        uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
        for (size_t index = 0; index < words.size(); ++index)
        {
            const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choice + kRoundConstants[index] + words[index];
            const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::array<uint32_t, 8> m_state{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::array<uint8_t, 64> m_buffer{};
    size_t m_bufferSize = 0;
    uint64_t m_totalBytes = 0;
};

} // namespace

bool AssetFingerprint::IsValid() const
{
    return std::any_of(Bytes.begin(), Bytes.end(), [](uint8_t value) { return value != 0; });
}

std::string AssetFingerprint::ToString() const
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (uint8_t value : Bytes)
        stream << std::setw(2) << static_cast<unsigned int>(value);
    return stream.str();
}

bool AssetFingerprint::Parse(std::string_view text, AssetFingerprint &fingerprint)
{
    if (text.size() != fingerprint.Bytes.size() * 2)
        return false;
    AssetFingerprint parsed;
    for (size_t index = 0; index < parsed.Bytes.size(); ++index)
    {
        unsigned int value = 0;
        const char *first = text.data() + index * 2;
        const auto result = std::from_chars(first, first + 2, value, 16);
        if (result.ec != std::errc{} || result.ptr != first + 2)
            return false;
        parsed.Bytes[index] = static_cast<uint8_t>(value);
    }
    fingerprint = parsed;
    return true;
}

AssetFingerprint HashBytes(std::span<const std::byte> bytes)
{
    return HashBytes(std::span(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()));
}

AssetFingerprint HashBytes(std::span<const uint8_t> bytes)
{
    Sha256 hash;
    hash.Update(bytes);
    return hash.Finish();
}

AssetFingerprint HashString(std::string_view text)
{
    return HashBytes(std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
}

AssetFingerprint HashFile(const std::filesystem::path &path, std::string *error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        if (error)
            *error = "Cannot open file for hashing: " + path.generic_string();
        return {};
    }

    Sha256 hash;
    // Keep the streaming chunk off the comparatively small Windows thread
    // stack (often 1 MiB). Importers may hash from worker/plugin threads whose
    // stacks are smaller still.
    std::vector<uint8_t> buffer(1024 * 1024);
    while (file)
    {
        file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0)
            hash.Update(std::span(buffer.data(), static_cast<size_t>(count)));
    }
    if (!file.eof())
    {
        if (error)
            *error = "Failed while hashing file: " + path.generic_string();
        return {};
    }
    return hash.Finish();
}

AssetFingerprint CombineFingerprints(std::span<const AssetFingerprint> fingerprints)
{
    Sha256 hash;
    for (const AssetFingerprint &fingerprint : fingerprints)
        hash.Update(fingerprint.Bytes);
    return hash.Finish();
}

} // namespace engine::assets
