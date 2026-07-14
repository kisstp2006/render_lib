#include "SampleAssetPipeline.h"

#include "engine/asset/AssetDatabase.h"
#include "engine/asset/AssetPipeline.h"
#include "engine/asset/BuiltinAssetTypes.h"
#include "engine/asset/ColorGrading.h"
#include "engine/asset/ColorGradingAsset.h"
#include "engine/asset/CubemapAsset.h"
#include "engine/asset/ModelAsset.h"
#include "engine/core/Log.h"
#include "engine/resource/ResourceManager.h"
#include "engine/scene/Environment.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace engine;

namespace {

assets::AssetPipelineConfig MakePipelineConfig()
{
    assets::AssetPipelineConfig config;
    config.ProjectRoot = ENGINE_SAMPLE_PROJECT_ROOT;
    config.AssetRoot = ENGINE_SAMPLE_ASSET_ROOT;
    config.CacheRoot = ENGINE_SAMPLE_CACHE_ROOT;
    config.Platform = "desktop";
    config.Profile = "default";
    config.AllowDevelopmentSourceFallback = false;
    return config;
}

std::string DescribeDiagnostics(const std::vector<assets::AssetDiagnostic>& diagnostics)
{
    std::string message;
    for (const assets::AssetDiagnostic& diagnostic : diagnostics)
    {
        if (!message.empty())
            message += '\n';
        message += '[' + assets::ToString(diagnostic.Severity) + "] " + diagnostic.Message;
        if (!diagnostic.Source.empty())
            message += " (" + diagnostic.Source.generic_string() + ')';
        if (!diagnostic.Suggestion.empty())
            message += "\n  " + diagnostic.Suggestion;
    }
    return message.empty() ? "No asset diagnostic was produced." : message;
}

glm::vec4 ReadFace(const assets::CubemapMip& cube, uint32_t face, int x, int y)
{
    x = std::clamp(x, 0, static_cast<int>(cube.Size) - 1);
    y = std::clamp(y, 0, static_cast<int>(cube.Size) - 1);
    const std::vector<float>& pixels = cube.Faces[face];
    const size_t offset = (static_cast<size_t>(y) * cube.Size + static_cast<size_t>(x)) * 4u;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

std::pair<uint32_t, glm::vec2> DirectionToFace(glm::vec3 direction)
{
    direction = glm::normalize(direction);
    const glm::vec3 magnitude = glm::abs(direction);
    uint32_t face = 0;
    glm::vec2 uv(0.0f);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        if (direction.x >= 0.0f)
        {
            face = 0;
            uv = {-direction.z, -direction.y};
        }
        else
        {
            face = 1;
            uv = {direction.z, -direction.y};
        }
        uv /= magnitude.x;
    }
    else if (magnitude.y >= magnitude.z)
    {
        if (direction.y >= 0.0f)
        {
            face = 2;
            uv = {direction.x, direction.z};
        }
        else
        {
            face = 3;
            uv = {direction.x, -direction.z};
        }
        uv /= magnitude.y;
    }
    else
    {
        if (direction.z >= 0.0f)
        {
            face = 4;
            uv = {direction.x, -direction.y};
        }
        else
        {
            face = 5;
            uv = {-direction.x, -direction.y};
        }
        uv /= magnitude.z;
    }
    return {face, uv};
}

glm::vec4 SampleCube(const assets::CubemapMip& cube, glm::vec3 direction)
{
    const auto [face, uv] = DirectionToFace(direction);
    const float x = (uv.x * 0.5f + 0.5f) * static_cast<float>(cube.Size) - 0.5f;
    const float y = (uv.y * 0.5f + 0.5f) * static_cast<float>(cube.Size) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    return glm::mix(glm::mix(ReadFace(cube, face, x0, y0), ReadFace(cube, face, x0 + 1, y0), tx),
                    glm::mix(ReadFace(cube, face, x0, y0 + 1), ReadFace(cube, face, x0 + 1, y0 + 1), tx), ty);
}

std::shared_ptr<HdrImageData> MakePanorama(const assets::CubemapData& cubemap, assets::AssetGuid guid)
{
    if (cubemap.MipLevels.empty() || cubemap.MipLevels.front().Size == 0)
        throw std::runtime_error("Cooked environment asset contains no cubemap mip levels.");
    const assets::CubemapMip& cube = cubemap.MipLevels.front();
    for (const std::vector<float>& face : cube.Faces)
        if (face.size() != static_cast<size_t>(cube.Size) * cube.Size * 4u)
            throw std::runtime_error("Cooked environment asset contains a malformed cubemap face.");

    auto panorama = std::make_shared<HdrImageData>();
    panorama->Width = static_cast<int>(cube.Size * 4u);
    panorama->Height = static_cast<int>(cube.Size * 2u);
    panorama->Channels = 3;
    panorama->Pixels.resize(static_cast<size_t>(panorama->Width) * panorama->Height * 3u);
    panorama->SourcePath = "asset://" + guid.ToString();
    constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
    for (int y = 0; y < panorama->Height; ++y)
    {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(panorama->Height);
        const float polar = v * std::numbers::pi_v<float>;
        const float sinPolar = std::sin(polar);
        for (int x = 0; x < panorama->Width; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(panorama->Width);
            const float longitude = (u - 0.5f) * twoPi;
            const glm::vec3 direction{
                std::cos(longitude) * sinPolar, std::cos(polar), std::sin(longitude) * sinPolar};
            const glm::vec4 color = glm::max(SampleCube(cube, direction), glm::vec4(0.0f));
            const size_t offset = (static_cast<size_t>(y) * panorama->Width + x) * 3u;
            panorama->Pixels[offset] = color.r;
            panorama->Pixels[offset + 1] = color.g;
            panorama->Pixels[offset + 2] = color.b;
        }
    }
    return panorama;
}

} // namespace

struct SampleAssetPipeline::Impl
{
    resources::ResourceManager Resources;
    assets::AssetTypeRegistry Types;
    assets::AssetDatabase Database;
    assets::AssetPipeline Pipeline;
    size_t LoadedResources = 0;
    size_t CookedResources = 0;
    size_t CacheHits = 0;
    std::vector<assets::AssetGuid> ImportedAssets;

    Impl() : Database(Types), Pipeline(Types, Database, Resources, MakePipelineConfig())
    {
        std::error_code filesystemError;
        std::filesystem::create_directories(Pipeline.Config().AssetRoot, filesystemError);
        if (filesystemError)
            throw std::runtime_error("Cannot create the sample asset descriptor directory: " +
                                     filesystemError.message());
        std::filesystem::create_directories(Pipeline.Config().CacheRoot, filesystemError);
        if (filesystemError)
            throw std::runtime_error("Cannot create the sample cooked-asset cache: " + filesystemError.message());

        std::string error;
        if (!assets::RegisterBuiltinAssetTypes(Types, Resources, {}, &error))
            throw std::runtime_error("Cannot register sample asset types: " + error);
        const assets::AssetDatabaseScanResult scan = Database.Scan(Pipeline.Config().AssetRoot);
        if (!scan.Succeeded())
            throw std::runtime_error("Cannot scan the sample asset database:\n" + DescribeDiagnostics(scan.Diagnostics));

        if (std::filesystem::is_regular_file(Pipeline.RuntimeRegistryPath()) &&
            !Resources.Registry().Load(Pipeline.RuntimeRegistryPath(), &error))
            log::Warn("Sample asset registry will be rebuilt: " + error);
    }

    assets::AssetGuid ImportAndCook(const std::filesystem::path& source, std::string_view type,
                                    std::string_view mode = {})
    {
        const auto importStart = std::chrono::steady_clock::now();
        assets::ImportResult imported = Pipeline.CreateAssetFromSource(source, "Imported", type, mode);
        if (!imported.Succeeded)
            throw std::runtime_error("Asset import failed for '" + source.generic_string() + "':\n" +
                                     DescribeDiagnostics(imported.Diagnostics));

        const auto findPrimary = [&](const std::vector<assets::ImportedAsset>& candidates) {
            return std::find_if(candidates.begin(), candidates.end(), [&](const assets::ImportedAsset& asset) {
                return asset.Descriptor.Type == type;
            });
        };
        auto primary = findPrimary(imported.GeneratedAssets);
        assets::AssetGuid guid;
        if (primary != imported.GeneratedAssets.end())
            guid = primary->Descriptor.Guid;
        else
        {
            const auto reused = findPrimary(imported.ReusedAssets);
            if (reused != imported.ReusedAssets.end())
                guid = reused->Descriptor.Guid;
        }
        if (!guid.IsValid())
            throw std::runtime_error("Asset importer did not produce the requested '" + std::string(type) +
                                     "' asset for '" + source.generic_string() + "'.");

        if (std::find(ImportedAssets.begin(), ImportedAssets.end(), guid) == ImportedAssets.end())
            ImportedAssets.push_back(guid);

        const bool registryEntryMissing = !Resources.Registry().Resolve(guid).has_value();
        const auto transformStart = std::chrono::steady_clock::now();
        assets::AssetTransformResult transformed = Pipeline.TransformAsset(guid, registryEntryMissing);
        if (!transformed.Succeeded)
            throw std::runtime_error("Asset cook failed for '" + source.generic_string() + "':\n" +
                                     DescribeDiagnostics(transformed.Diagnostics));
        if (transformed.SkippedAsUpToDate)
            ++CacheHits;
        else
            ++CookedResources;
        std::string registryError;
        if (!Pipeline.SaveRuntimeRegistry(&registryError))
            throw std::runtime_error("Cannot save the sample runtime asset registry: " + registryError);
        const auto now = std::chrono::steady_clock::now();
        const float importMs = std::chrono::duration<float, std::milli>(transformStart - importStart).count();
        const float transformMs = std::chrono::duration<float, std::milli>(now - transformStart).count();
        log::Info("Sample asset: " + source.filename().generic_string() + " -> " + guid.ToString() +
                  (transformed.SkippedAsUpToDate ? " (cache hit, " : " (cooked, ") +
                  std::to_string(importMs) + " ms import, " + std::to_string(transformMs) + " ms transform)");
        return guid;
    }

    std::shared_ptr<TextureData> LoadTexture(assets::AssetHandle<TextureData> handle)
    {
        if (!handle)
            return {};
        std::string error;
        std::shared_ptr<TextureData> texture = Resources.Load(handle, &error);
        if (!texture)
            throw std::runtime_error(error);
        ++LoadedResources;
        return texture;
    }

    Material LoadMaterial(assets::AssetHandle<assets::MaterialAssetData> handle)
    {
        if (!handle)
            return {};
        std::string error;
        const std::shared_ptr<assets::MaterialAssetData> asset = Resources.Load(handle, &error);
        if (!asset)
            throw std::runtime_error(error);
        ++LoadedResources;
        Material material = asset->Value;
        material.AlbedoMap = LoadTexture(asset->BaseColor);
        material.NormalMap = LoadTexture(asset->Normal);
        material.MetallicRoughnessMap = LoadTexture(asset->MetallicRoughness);
        material.OcclusionMap = LoadTexture(asset->Occlusion);
        material.EmissiveMap = LoadTexture(asset->Emissive);
        return material;
    }
};

SampleAssetPipeline::SampleAssetPipeline() : m_impl(std::make_unique<Impl>())
{
}

SampleAssetPipeline::~SampleAssetPipeline() = default;

void SampleAssetPipeline::AddStaticMesh(Scene& scene, const std::filesystem::path& source,
                                        const glm::mat4& rootTransform)
{
    const assets::AssetGuid guid = m_impl->ImportAndCook(source, assets::kStaticMeshAssetType, "scene");
    const auto loadStart = std::chrono::steady_clock::now();
    std::string error;
    const std::shared_ptr<assets::StaticMeshData> mesh = m_impl->Resources.Load<assets::StaticMeshData>(guid, &error);
    if (!mesh)
        throw std::runtime_error(error);
    ++m_impl->LoadedResources;
    for (const assets::StaticMeshPart& part : mesh->Parts)
    {
        if (!part.Mesh)
            continue;
        scene.AddInstance(part.Mesh, m_impl->LoadMaterial(part.Material), rootTransform * part.Transform);
    }
    const float loadMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - loadStart).count();
    log::Info("Sample asset: instantiated " + std::to_string(mesh->Parts.size()) +
              " mesh part(s) in " + std::to_string(loadMs) + " ms");
}

std::shared_ptr<HdrImageData> SampleAssetPipeline::LoadEnvironment(const std::filesystem::path& source)
{
    const assets::AssetGuid guid = m_impl->ImportAndCook(source, assets::kCubemapAssetType, "panorama");
    std::string error;
    const std::shared_ptr<assets::CubemapData> cubemap = m_impl->Resources.Load<assets::CubemapData>(guid, &error);
    if (!cubemap)
        throw std::runtime_error(error);
    ++m_impl->LoadedResources;
    const auto conversionStart = std::chrono::steady_clock::now();
    std::shared_ptr<HdrImageData> panorama = MakePanorama(*cubemap, guid);
    const float conversionMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - conversionStart).count();
    log::Info("Sample asset: prepared runtime HDR panorama in " + std::to_string(conversionMs) + " ms");
    return panorama;
}

std::shared_ptr<ColorGradingLutData> SampleAssetPipeline::LoadColorGrading(const std::filesystem::path& source)
{
    const assets::AssetGuid guid =
        m_impl->ImportAndCook(source, assets::kColorGradingLutAssetType, "3d-lut");
    std::string error;
    std::shared_ptr<ColorGradingLutData> lut = m_impl->Resources.Load<ColorGradingLutData>(guid, &error);
    if (!lut)
        throw std::runtime_error(error);
    ++m_impl->LoadedResources;
    return lut;
}

size_t SampleAssetPipeline::AssetCount() const
{
    return m_impl->Database.Size();
}

size_t SampleAssetPipeline::LoadedResourceCount() const
{
    return m_impl->LoadedResources;
}

size_t SampleAssetPipeline::RuntimeResourceCount() const
{
    return m_impl->Resources.Registry().List().size();
}

size_t SampleAssetPipeline::CacheHitCount() const
{
    return m_impl->CacheHits;
}

size_t SampleAssetPipeline::InvalidateImportedResources()
{
    for (const assets::AssetGuid guid : m_impl->ImportedAssets)
        m_impl->Resources.NotifyResourceChanged(guid);
    return m_impl->ImportedAssets.size();
}
