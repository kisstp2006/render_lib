#pragma once

#include "engine/asset/AssetPipeline.h"

#include <array>
#include <glm/glm.hpp>

namespace engine::assets
{

inline constexpr std::string_view kCubemapAssetType = "cubemap";
inline constexpr std::string_view kCubemapDescriptorExtension = "sla-cubemap";
inline constexpr std::string_view kCubemapResourceType = "texture-cubemap";
inline constexpr std::string_view kSkyboxAssetType = "skybox";
inline constexpr std::string_view kSkyboxDescriptorExtension = "sla-skybox";
inline constexpr std::string_view kSkyboxResourceType = "skybox";

enum class CubemapSourceMode : uint8_t
{
    Equirectangular,
    SixFaces
};
enum class CubemapFace : uint8_t
{
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ
};

struct CubemapFaceCorrection
{
    uint16_t RotationDegrees = 0;
    bool FlipHorizontal = false;
    bool FlipVertical = false;
};

struct CubemapAssetSettings
{
    CubemapSourceMode SourceMode = CubemapSourceMode::Equirectangular;
    uint32_t FaceSize = 256;
    bool GenerateMipmaps = true;
    bool ReduceSeams = true;
    bool GenerateIrradiance = true;
    uint32_t IrradianceSize = 32;
    bool GenerateSpecularPrefilter = true;
    uint32_t PrefilterSize = 128;
    uint32_t FilterSampleCount = 64;
    float RotationDegrees = 0.0f;
    float ExposureEV = 0.0f;
    std::array<CubemapFaceCorrection, 6> FaceCorrections;
};

struct CubemapMip
{
    uint32_t Size = 0;
    std::array<std::vector<float>, 6> Faces; // linear RGBA32F
};

struct CubemapData
{
    std::vector<CubemapMip> MipLevels;
    std::optional<CubemapMip> DiffuseIrradiance;
    std::vector<CubemapMip> SpecularPrefilter;
};

struct SkyboxAssetSettings
{
    AssetHandle<CubemapData> Cubemap;
    float ExposureEV = 0.0f;
    float Intensity = 1.0f;
    glm::vec3 Tint{1.0f};
    float Saturation = 1.0f;
    float RotationDegrees = 0.0f;
    float BlurMip = 0.0f;
    bool EnvironmentLighting = true;
    bool DiffuseIrradiance = true;
    bool SpecularIbl = true;
    std::string SkyMaterial;
    glm::vec3 FallbackColor{0.04f};
};

struct SkyboxData : SkyboxAssetSettings
{
};

struct CubemapFaceInfo
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Hdr = false;
};

bool ValidateCubemapFaces(std::span<const CubemapFaceInfo> faces, std::vector<AssetDiagnostic> &diagnostics);
CubemapAssetSettings ReadCubemapAssetSettings(const AssetDescriptor &descriptor, std::string_view profile = {});
void WriteCubemapAssetSettings(AssetDescriptor &descriptor, const CubemapAssetSettings &settings);
SkyboxAssetSettings ReadSkyboxAssetSettings(const AssetDescriptor &descriptor, std::string_view profile = {});
void WriteSkyboxAssetSettings(AssetDescriptor &descriptor, const SkyboxAssetSettings &settings);

ImportResult CreateCubemapAsset(AssetPipeline &pipeline, const std::filesystem::path &panorama,
                                const std::filesystem::path &destinationDirectory = {});
ImportResult CreateCubemapAsset(AssetPipeline &pipeline, const std::array<std::filesystem::path, 6> &faces,
                                const std::filesystem::path &destinationDirectory, std::string_view name,
                                std::string *error = nullptr);
bool CreateSkyboxAsset(AssetPipeline &pipeline, AssetGuid cubemap, std::string_view name,
                       const std::filesystem::path &descriptorPath, AssetGuid *createdGuid = nullptr,
                       std::string *error = nullptr);

bool RegisterCubemapAssetTypes(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                               std::string *error = nullptr);

} // namespace engine::assets
