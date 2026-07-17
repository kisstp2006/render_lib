#include "engine/scene/Material.h"

#include <bit>

namespace engine
{
namespace
{

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashWord(uint64_t& hash, uint64_t value) noexcept
{
    for (uint32_t shift = 0; shift < 64; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= kFnvPrime;
    }
}

void HashFloat(uint64_t& hash, float value) noexcept
{
    HashWord(hash, std::bit_cast<uint32_t>(value));
}

void HashVector(uint64_t& hash, const glm::vec3& value) noexcept
{
    HashFloat(hash, value.x);
    HashFloat(hash, value.y);
    HashFloat(hash, value.z);
}

bool EqualFloat(float left, float right) noexcept
{
    return std::bit_cast<uint32_t>(left) == std::bit_cast<uint32_t>(right);
}

bool EqualVector(const glm::vec3& left, const glm::vec3& right) noexcept
{
    return EqualFloat(left.x, right.x) && EqualFloat(left.y, right.y) &&
           EqualFloat(left.z, right.z);
}

} // namespace

uint64_t MaterialRenderStateHash(const Material& material) noexcept
{
    uint64_t hash = kFnvOffset;
    HashVector(hash, material.Albedo);
    HashFloat(hash, material.BaseColorAlpha);
    HashFloat(hash, material.Metallic);
    HashFloat(hash, material.Roughness);
    HashVector(hash, material.Emissive);
    HashFloat(hash, material.AmbientOcclusion);
    HashFloat(hash, material.SpecularF0);
    HashWord(hash, reinterpret_cast<uintptr_t>(material.AlbedoMap.get()));
    HashWord(hash, reinterpret_cast<uintptr_t>(material.NormalMap.get()));
    HashWord(hash, reinterpret_cast<uintptr_t>(material.MetallicRoughnessMap.get()));
    HashWord(hash, reinterpret_cast<uintptr_t>(material.OcclusionMap.get()));
    HashWord(hash, reinterpret_cast<uintptr_t>(material.EmissiveMap.get()));
    HashWord(hash, reinterpret_cast<uintptr_t>(material.MraoMap.get()));
    HashWord(hash, static_cast<uint64_t>(material.Alpha));
    HashFloat(hash, material.AlphaCutoff);
    return hash;
}

bool MaterialRenderStatesEqual(const Material& left,
                               const Material& right) noexcept
{
    return EqualVector(left.Albedo, right.Albedo) &&
           EqualFloat(left.BaseColorAlpha, right.BaseColorAlpha) &&
           EqualFloat(left.Metallic, right.Metallic) &&
           EqualFloat(left.Roughness, right.Roughness) &&
           EqualVector(left.Emissive, right.Emissive) &&
           EqualFloat(left.AmbientOcclusion, right.AmbientOcclusion) &&
           EqualFloat(left.SpecularF0, right.SpecularF0) &&
           left.AlbedoMap.get() == right.AlbedoMap.get() &&
           left.NormalMap.get() == right.NormalMap.get() &&
           left.MetallicRoughnessMap.get() == right.MetallicRoughnessMap.get() &&
           left.OcclusionMap.get() == right.OcclusionMap.get() &&
           left.EmissiveMap.get() == right.EmissiveMap.get() &&
           left.MraoMap.get() == right.MraoMap.get() &&
           left.Alpha == right.Alpha && EqualFloat(left.AlphaCutoff, right.AlphaCutoff);
}

} // namespace engine
