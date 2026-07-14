#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace engine::resources
{

class BinaryWriter
{
  public:
    void WriteU8(uint8_t value);
    void WriteU16(uint16_t value);
    void WriteU32(uint32_t value);
    void WriteU64(uint64_t value);
    void WriteI32(int32_t value)
    {
        WriteU32(static_cast<uint32_t>(value));
    }
    void WriteF32(float value);
    void WriteString(std::string_view value);
    void WriteBytes(std::span<const std::byte> bytes);

    [[nodiscard]] const std::vector<std::byte> &Data() const
    {
        return m_data;
    }
    [[nodiscard]] std::vector<std::byte> TakeData()
    {
        return std::move(m_data);
    }

  private:
    std::vector<std::byte> m_data;
};

class BinaryReader
{
  public:
    explicit BinaryReader(std::span<const std::byte> data) : m_data(data)
    {
    }

    bool ReadU8(uint8_t &value);
    bool ReadU16(uint16_t &value);
    bool ReadU32(uint32_t &value);
    bool ReadU64(uint64_t &value);
    bool ReadI32(int32_t &value);
    bool ReadF32(float &value);
    bool ReadString(std::string &value, uint32_t maximumLength = 16u * 1024u * 1024u);
    bool ReadBytes(size_t count, std::span<const std::byte> &bytes);
    bool Skip(size_t count);

    [[nodiscard]] size_t Remaining() const
    {
        return m_data.size() - m_offset;
    }
    [[nodiscard]] size_t Offset() const
    {
        return m_offset;
    }
    [[nodiscard]] std::string Error() const
    {
        return m_error;
    }

  private:
    bool Require(size_t count);

    std::span<const std::byte> m_data;
    size_t m_offset = 0;
    std::string m_error;
};

} // namespace engine::resources
