#include "engine/resource/CookedResource.h"

#include "engine/asset/AssetFileSystem.h"
#include "engine/resource/BinaryIO.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace engine::resources
{
namespace
{

constexpr std::array<std::byte, 8> kMagic = {
    static_cast<std::byte>('S'), static_cast<std::byte>('L'), static_cast<std::byte>('A'), static_cast<std::byte>('_'),
    static_cast<std::byte>('R'), static_cast<std::byte>('E'), static_cast<std::byte>('S'), static_cast<std::byte>(0),
};

std::vector<std::byte> CompressRle(std::span<const std::byte> payload)
{
    std::vector<std::byte> output;
    output.reserve(payload.size());
    size_t index = 0;
    while (index < payload.size())
    {
        uint8_t count = 1;
        while (index + count < payload.size() && count < 255 && payload[index + count] == payload[index])
            ++count;
        output.push_back(static_cast<std::byte>(count));
        output.push_back(payload[index]);
        index += count;
    }
    return output;
}

bool DecompressRle(std::span<const std::byte> stored, uint64_t expectedSize, std::vector<std::byte> &output,
                   std::string *error)
{
    if ((stored.size() % 2) != 0 || expectedSize > static_cast<uint64_t>(SIZE_MAX))
    {
        if (error)
            *error = "Corrupt RLE payload";
        return false;
    }
    output.clear();
    output.reserve(static_cast<size_t>(expectedSize));
    for (size_t index = 0; index < stored.size(); index += 2)
    {
        const uint8_t count = static_cast<uint8_t>(stored[index]);
        if (count == 0 || output.size() + count > expectedSize)
        {
            if (error)
                *error = "RLE payload expands beyond its declared size";
            return false;
        }
        output.insert(output.end(), count, stored[index + 1]);
    }
    if (output.size() != expectedSize)
    {
        if (error)
            *error = "RLE payload does not match its declared size";
        return false;
    }
    return true;
}

void WriteHeader(BinaryWriter &writer, const CookedResourceHeader &header)
{
    writer.WriteBytes(kMagic);
    writer.WriteU16(header.HeaderVersion);
    writer.WriteU16(0);
    writer.WriteU32(header.ResourceVersion);
    writer.WriteU64(header.Asset.High);
    writer.WriteU64(header.Asset.Low);
    writer.WriteU32(header.Flags);
    writer.WriteU8(static_cast<uint8_t>(header.Compression));
    writer.WriteU8(0);
    writer.WriteU8(0);
    writer.WriteU8(0);
    writer.WriteU64(header.PayloadSize);
    writer.WriteU64(header.StoredPayloadSize);
    writer.WriteBytes(std::span(reinterpret_cast<const std::byte *>(header.PayloadChecksum.Bytes.data()),
                                header.PayloadChecksum.Bytes.size()));
    writer.WriteBytes(std::span(reinterpret_cast<const std::byte *>(header.TransformFingerprint.Bytes.data()),
                                header.TransformFingerprint.Bytes.size()));
    writer.WriteString(header.AssetType);
    writer.WriteString(header.ResourceType);
    writer.WriteString(header.Platform);
    writer.WriteString(header.Profile);
}

bool ReadHeader(BinaryReader &reader, CookedResourceHeader &header, std::string *error)
{
    std::span<const std::byte> magic;
    if (!reader.ReadBytes(kMagic.size(), magic) || !std::equal(magic.begin(), magic.end(), kMagic.begin()))
    {
        if (error)
            *error = "Invalid cooked resource magic number";
        return false;
    }
    uint16_t reserved16 = 0;
    uint8_t compression = 0, reserved8 = 0;
    if (!reader.ReadU16(header.HeaderVersion) || !reader.ReadU16(reserved16) ||
        !reader.ReadU32(header.ResourceVersion) || !reader.ReadU64(header.Asset.High) ||
        !reader.ReadU64(header.Asset.Low) || !reader.ReadU32(header.Flags) || !reader.ReadU8(compression) ||
        !reader.ReadU8(reserved8) || !reader.ReadU8(reserved8) || !reader.ReadU8(reserved8) ||
        !reader.ReadU64(header.PayloadSize) || !reader.ReadU64(header.StoredPayloadSize))
    {
        if (error)
            *error = reader.Error();
        return false;
    }
    if (header.HeaderVersion != kCookedResourceHeaderVersion || header.ResourceVersion == 0 ||
        !header.Asset.IsValid() || compression > static_cast<uint8_t>(ResourceCompression::Rle))
    {
        if (error)
            *error = "Unsupported or malformed cooked resource header";
        return false;
    }
    header.Compression = static_cast<ResourceCompression>(compression);
    std::span<const std::byte> checksum;
    if (!reader.ReadBytes(header.PayloadChecksum.Bytes.size(), checksum))
    {
        if (error)
            *error = reader.Error();
        return false;
    }
    std::copy(checksum.begin(), checksum.end(), reinterpret_cast<std::byte *>(header.PayloadChecksum.Bytes.data()));
    if (!reader.ReadBytes(header.TransformFingerprint.Bytes.size(), checksum))
    {
        if (error)
            *error = reader.Error();
        return false;
    }
    std::copy(checksum.begin(), checksum.end(),
              reinterpret_cast<std::byte *>(header.TransformFingerprint.Bytes.data()));
    if (!reader.ReadString(header.AssetType, 1024) || !reader.ReadString(header.ResourceType, 1024) ||
        !reader.ReadString(header.Platform, 1024) || !reader.ReadString(header.Profile, 1024))
    {
        if (error)
            *error = reader.Error();
        return false;
    }
    if (header.AssetType.empty() || header.ResourceType.empty() || header.Platform.empty())
    {
        if (error)
            *error = "Cooked resource header is missing a type or platform";
        return false;
    }
    return true;
}

} // namespace

std::string ToString(ResourceCompression compression)
{
    switch (compression)
    {
    case ResourceCompression::None:
        return "none";
    case ResourceCompression::Rle:
        return "rle";
    }
    return "none";
}

bool ParseResourceCompression(std::string_view text, ResourceCompression &compression)
{
    if (text == "none")
        compression = ResourceCompression::None;
    else if (text == "rle")
        compression = ResourceCompression::Rle;
    else
        return false;
    return true;
}

bool WriteCookedResource(const std::filesystem::path &path, CookedResourceHeader header,
                         std::span<const std::byte> payload, ResourceCompression preferredCompression,
                         std::string *error)
{
    if (!header.Asset.IsValid() || header.AssetType.empty() || header.ResourceType.empty() || header.Platform.empty() ||
        header.ResourceVersion == 0)
    {
        if (error)
            *error = "Cannot write cooked resource with an incomplete header";
        return false;
    }
    header.HeaderVersion = kCookedResourceHeaderVersion;
    header.PayloadSize = payload.size();
    header.PayloadChecksum = assets::HashBytes(payload);

    std::vector<std::byte> compressed;
    std::span<const std::byte> stored = payload;
    header.Compression = ResourceCompression::None;
    if (preferredCompression == ResourceCompression::Rle && !payload.empty())
    {
        compressed = CompressRle(payload);
        if (compressed.size() < payload.size())
        {
            stored = compressed;
            header.Compression = ResourceCompression::Rle;
        }
    }
    header.StoredPayloadSize = stored.size();

    BinaryWriter writer;
    WriteHeader(writer, header);
    writer.WriteBytes(stored);
    return assets::WriteFileAtomic(path, writer.Data(), error);
}

bool ReadCookedResource(const std::filesystem::path &path, CookedResourceData &resource, std::string *error,
                        uint64_t maximumPayload)
{
    std::vector<std::byte> bytes;
    if (!assets::ReadFileBytes(path, bytes, error,
                               static_cast<size_t>(std::min<uint64_t>(maximumPayload + 64 * 1024, SIZE_MAX))))
        return false;
    BinaryReader reader(bytes);
    CookedResourceData loaded;
    if (!ReadHeader(reader, loaded.Header, error))
        return false;
    if (loaded.Header.PayloadSize > maximumPayload || loaded.Header.StoredPayloadSize > maximumPayload ||
        loaded.Header.StoredPayloadSize != reader.Remaining())
    {
        if (error)
            *error = "Cooked resource payload size is invalid or exceeds the configured limit";
        return false;
    }
    std::span<const std::byte> stored;
    if (!reader.ReadBytes(static_cast<size_t>(loaded.Header.StoredPayloadSize), stored))
    {
        if (error)
            *error = reader.Error();
        return false;
    }
    if (loaded.Header.Compression == ResourceCompression::None)
    {
        if (loaded.Header.PayloadSize != stored.size())
        {
            if (error)
                *error = "Uncompressed resource payload has inconsistent sizes";
            return false;
        }
        loaded.Payload.assign(stored.begin(), stored.end());
    }
    else if (!DecompressRle(stored, loaded.Header.PayloadSize, loaded.Payload, error))
        return false;

    if (assets::HashBytes(loaded.Payload) != loaded.Header.PayloadChecksum)
    {
        if (error)
            *error = "Cooked resource payload checksum mismatch: " + path.generic_string();
        return false;
    }
    resource = std::move(loaded);
    return true;
}

bool ReadCookedResourceHeader(const std::filesystem::path &path, CookedResourceHeader &header, std::string *error)
{
    // Header strings are bounded to 1 KiB each; 8 KiB covers the complete
    // current and foreseeable v1 header without reading a large payload.
    std::vector<std::byte> bytes;
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        if (error)
            *error = "Cannot open cooked resource: " + path.generic_string();
        return false;
    }
    bytes.resize(8192);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<size_t>(file.gcount()));
    BinaryReader reader(bytes);
    return ReadHeader(reader, header, error);
}

} // namespace engine::resources
