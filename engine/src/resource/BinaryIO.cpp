#include "engine/resource/BinaryIO.h"

#include <bit>
#include <limits>

namespace engine::resources
{

void BinaryWriter::WriteU8(uint8_t value)
{
    m_data.push_back(static_cast<std::byte>(value));
}

void BinaryWriter::WriteU16(uint16_t value)
{
    WriteU8(static_cast<uint8_t>(value));
    WriteU8(static_cast<uint8_t>(value >> 8u));
}

void BinaryWriter::WriteU32(uint32_t value)
{
    for (uint32_t shift = 0; shift < 32; shift += 8)
        WriteU8(static_cast<uint8_t>(value >> shift));
}

void BinaryWriter::WriteU64(uint64_t value)
{
    for (uint32_t shift = 0; shift < 64; shift += 8)
        WriteU8(static_cast<uint8_t>(value >> shift));
}

void BinaryWriter::WriteF32(float value)
{
    WriteU32(std::bit_cast<uint32_t>(value));
}

void BinaryWriter::WriteString(std::string_view value)
{
    if (value.size() > std::numeric_limits<uint32_t>::max())
        value = value.substr(0, std::numeric_limits<uint32_t>::max());
    WriteU32(static_cast<uint32_t>(value.size()));
    WriteBytes(std::span(reinterpret_cast<const std::byte *>(value.data()), value.size()));
}

void BinaryWriter::WriteBytes(std::span<const std::byte> bytes)
{
    m_data.insert(m_data.end(), bytes.begin(), bytes.end());
}

bool BinaryReader::Require(size_t count)
{
    if (count <= Remaining())
        return true;
    if (m_error.empty())
        m_error = "Binary data ends before the requested field";
    return false;
}

bool BinaryReader::ReadU8(uint8_t &value)
{
    if (!Require(1))
        return false;
    value = static_cast<uint8_t>(m_data[m_offset++]);
    return true;
}

bool BinaryReader::ReadU16(uint16_t &value)
{
    uint8_t a = 0, b = 0;
    if (!ReadU8(a) || !ReadU8(b))
        return false;
    value = static_cast<uint16_t>(a | (static_cast<uint16_t>(b) << 8u));
    return true;
}

bool BinaryReader::ReadU32(uint32_t &value)
{
    value = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8)
    {
        uint8_t byte = 0;
        if (!ReadU8(byte))
            return false;
        value |= static_cast<uint32_t>(byte) << shift;
    }
    return true;
}

bool BinaryReader::ReadU64(uint64_t &value)
{
    value = 0;
    for (uint32_t shift = 0; shift < 64; shift += 8)
    {
        uint8_t byte = 0;
        if (!ReadU8(byte))
            return false;
        value |= static_cast<uint64_t>(byte) << shift;
    }
    return true;
}

bool BinaryReader::ReadI32(int32_t &value)
{
    uint32_t bits = 0;
    if (!ReadU32(bits))
        return false;
    value = static_cast<int32_t>(bits);
    return true;
}

bool BinaryReader::ReadF32(float &value)
{
    uint32_t bits = 0;
    if (!ReadU32(bits))
        return false;
    value = std::bit_cast<float>(bits);
    return true;
}

bool BinaryReader::ReadString(std::string &value, uint32_t maximumLength)
{
    uint32_t length = 0;
    if (!ReadU32(length))
        return false;
    if (length > maximumLength)
    {
        if (m_error.empty())
            m_error = "Binary string exceeds its allowed size";
        return false;
    }
    if (!Require(length))
        return false;
    value.assign(reinterpret_cast<const char *>(m_data.data() + m_offset), length);
    m_offset += length;
    return true;
}

bool BinaryReader::ReadBytes(size_t count, std::span<const std::byte> &bytes)
{
    if (!Require(count))
        return false;
    bytes = m_data.subspan(m_offset, count);
    m_offset += count;
    return true;
}

bool BinaryReader::Skip(size_t count)
{
    if (!Require(count))
        return false;
    m_offset += count;
    return true;
}

} // namespace engine::resources
