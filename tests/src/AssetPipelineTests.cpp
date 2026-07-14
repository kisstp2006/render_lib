#include "engine/asset/AssetFileSystem.h"
#include "engine/asset/AssetWorkspace.h"
#include "engine/asset/BuiltinAssetTypes.h"
#include "engine/asset/ColorGradingAsset.h"
#include "engine/asset/ModelAsset.h"
#include "engine/asset/cook/MeshProcessing.h"
#include "engine/asset/cook/TextureBlockCompression.h"
#include "engine/resource/BinaryIO.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <span>

using namespace engine;
using namespace engine::assets;

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition)
    {
        std::fprintf(stderr, "ASSET TEST FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

std::vector<std::byte> Bytes(std::string_view text)
{
    return {reinterpret_cast<const std::byte *>(text.data()),
            reinterpret_cast<const std::byte *>(text.data() + text.size())};
}

void WriteText(const std::filesystem::path &path, std::string_view text)
{
    std::string error;
    const auto bytes = Bytes(text);
    Require(WriteFileAtomic(path, bytes, &error), error.c_str());
}

void WritePpm(const std::filesystem::path &path, uint8_t red, uint8_t green, uint8_t blue)
{
    std::string header = "P6\n4 4\n255\n";
    std::vector<std::byte> bytes = Bytes(header);
    for (int pixel = 0; pixel < 16; ++pixel)
    {
        bytes.push_back(static_cast<std::byte>(red));
        bytes.push_back(static_cast<std::byte>(green));
        bytes.push_back(static_cast<std::byte>(blue));
    }
    std::string error;
    Require(WriteFileAtomic(path, bytes, &error), error.c_str());
}

void WriteHdr(const std::filesystem::path &path)
{
    std::vector<std::byte> bytes = Bytes("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n");
    for (int pixel = 0; pixel < 8; ++pixel)
    {
        bytes.push_back(std::byte{128});
        bytes.push_back(std::byte{96});
        bytes.push_back(std::byte{64});
        bytes.push_back(std::byte{129});
    }
    std::string error;
    Require(WriteFileAtomic(path, bytes, &error), error.c_str());
}

struct TestContext
{
    std::filesystem::path Root;
    resources::ResourceManager Resources;
    AssetTypeRegistry Types;
    AssetDatabase Database;
    AssetPipeline Pipeline;

    TestContext()
        : Root(std::filesystem::temp_directory_path() / ("sla-asset-tests-" + AssetGuid::Generate().ToString())),
          Database(Types),
          Pipeline(Types, Database, Resources, AssetPipelineConfig{Root, "Assets", "Cache", "desktop", "default", true})
    {
        std::filesystem::create_directories(Root / "Assets");
        std::filesystem::create_directories(Root / "Source");
        std::string error;
        Require(RegisterBuiltinAssetTypes(Types, Resources, {}, &error), error.c_str());
        Require(Database.Scan(Root / "Assets").Succeeded(), "empty asset database scan must succeed");
    }

    ~TestContext()
    {
        std::error_code error;
        std::filesystem::remove_all(Root, error);
    }
};

AssetGuid TestDescriptorAndTexture(TestContext &context)
{
    std::fprintf(stderr, "  descriptor roundtrip\n");
    const AssetGuid guid = AssetGuid::Generate();
    Require(guid.IsValid(), "generated GUID must be valid");
    const auto parsed = AssetGuid::Parse(guid.ToString());
    Require(parsed && *parsed == guid, "GUID text round-trip must be stable");

    AssetDescriptor descriptor;
    descriptor.Guid = guid;
    descriptor.Type = std::string(kTexture2DAssetType);
    descriptor.Name = "RoundTrip";
    descriptor.Sources = {"Source/a.ppm"};
    descriptor.Tags = {"test", "color"};
    descriptor.Settings = {{"usage", "color"}, {"filter", "trilinear"}};
    const auto path = context.Root / "Assets/roundtrip.sla-texture";
    std::string error;
    Require(SaveAssetDescriptor(path, descriptor, &error), error.c_str());
    AssetDescriptor loaded;
    Require(LoadAssetDescriptor(path, loaded, &error), error.c_str());
    std::sort(descriptor.Tags.begin(), descriptor.Tags.end());
    Require(loaded.Guid == descriptor.Guid && loaded.Settings == descriptor.Settings && loaded.Tags == descriptor.Tags,
            "descriptor save/load round-trip must preserve metadata");
    const auto hashA = HashString(SerializeAssetSettings(descriptor));
    descriptor.Settings = {{"filter", "trilinear"}, {"usage", "color"}};
    const auto hashB = HashString(SerializeAssetSettings(descriptor));
    Require(hashA == hashB, "settings hash must be independent of insertion order");
    std::filesystem::remove(path);

    std::fprintf(stderr, "  texture import\n");
    const auto source = context.Root / "Source/albedo.ppm";
    WritePpm(source, 220, 80, 30);
    const ImportResult imported = context.Pipeline.CreateAssetFromSource(source, {}, kTexture2DAssetType);
    Require(imported.Succeeded && imported.GeneratedAssets.size() == 1, "texture import must create one descriptor");
    const AssetGuid texture = imported.GeneratedAssets[0].Descriptor.Guid;
    std::fprintf(stderr, "  inspector open\n");
    AssetInspectorModel inspector(context.Pipeline);
    Require(inspector.Open(texture, &error), error.c_str());
    std::fprintf(stderr, "  inspector set\n");
    Require(inspector.SetValue("maximum_resolution", "64", &error), error.c_str());
    std::fprintf(stderr, "  inspector apply\n");
    Require(inspector.Apply(&error), error.c_str());
    std::fprintf(stderr, "  inspector dirty check\n");
    Require(context.Pipeline.IsAssetDirty(texture),
            "inspector setting edit must mark asset dirty without transforming");
    std::fprintf(stderr, "  transform\n");
    const AssetTransformResult transformed = context.Pipeline.TransformAsset(texture);
    Require(transformed.Succeeded && !transformed.SkippedAsUpToDate, "texture must transform into a cooked resource");
    Require(!context.Pipeline.IsAssetDirty(texture), "transformed texture must become clean");
    std::fprintf(stderr, "  runtime load\n");
    const auto runtime = context.Resources.Load<TextureData>(texture, &error);
    Require(runtime && runtime->Width == 4 && runtime->Height == 4,
            "runtime must load cooked texture rather than source");
    Require(runtime->MipLevels.size() == 3, "4x4 texture must contain three cooked mip levels");
    Require(runtime->Storage == TexturePixelStorage::Bc7Rgba &&
                runtime->MipLevels.front().Pixels.size() == TextureMipByteSize(runtime->Storage, 4, 4),
            "desktop high-quality color textures must cook to valid BC7 blocks");
    const AssetTransformResult cached = context.Pipeline.TransformAsset(texture);
    Require(cached.Succeeded && cached.SkippedAsUpToDate, "unchanged asset must not be transformed twice");
    const auto red = context.Root / "Source/pack_red.ppm", green = context.Root / "Source/pack_green.ppm";
    WritePpm(red, 255, 0, 0);
    WritePpm(green, 0, 255, 0);
    AssetDescriptor packed;
    packed.Guid = AssetGuid::Generate();
    packed.Type = std::string(kTexture2DAssetType);
    packed.Name = "Packed";
    packed.Sources = {"Source/pack_red.ppm", "Source/pack_green.ppm"};
    packed.SourceDependencies = packed.Sources;
    packed.State = AssetImportState::Dirty;
    TextureAssetSettings packSettings;
    packSettings.Usage = TextureUsage::LinearData;
    packSettings.ColorSpace = TextureColorSpace::Linear;
    packSettings.GenerateMips = false;
    packSettings.Compression = TextureCompressionQuality::None;
    packSettings.ChannelSources = {"source0.r", "source1.g", "black", "white"};
    WriteTextureAssetSettings(packed, packSettings);
    const auto packedPath = context.Root / "Assets/packed.sla-texture";
    Require(SaveAssetDescriptor(packedPath, packed, &error) && context.Database.AddOrUpdate(packedPath, packed, &error),
            error.c_str());
    Require(context.Pipeline.TransformAsset(packed.Guid).Succeeded,
            "multi-source channel-packed texture must transform");
    const auto packedRuntime = context.Resources.Load<TextureData>(packed.Guid, &error);
    Require(packedRuntime && packedRuntime->Pixels[0] > 250 && packedRuntime->Pixels[1] > 250 &&
                packedRuntime->Pixels[2] == 0 && packedRuntime->Pixels[3] == 255,
            "channel packing must select channels and constants deterministically");
    const auto before = context.Pipeline.ComputeSourceFingerprint(context.Database.Find(texture)->Descriptor, &error);
    WritePpm(source, 30, 80, 220);
    const auto after = context.Pipeline.ComputeSourceFingerprint(context.Database.Find(texture)->Descriptor, &error);
    Require(before != after && context.Pipeline.IsAssetDirty(texture),
            "content hash change must invalidate texture even without timestamp reliance");
    Require(context.Pipeline.TransformAsset(texture).Succeeded, "modified source must retransform");
    AssetPipeline mobile(context.Types, context.Database, context.Resources,
                         AssetPipelineConfig{context.Root, "Assets", "Cache", "desktop", "mobile", true});
    Require(mobile.IsAssetDirty(texture), "profile change must invalidate cooked output");
    Require(mobile.TransformAsset(texture).Succeeded, "mobile profile must cook an ASTC resource");
    const auto mobileRuntime = context.Resources.Load<TextureData>(texture, &error);
    Require(mobileRuntime && mobileRuntime->Storage == TexturePixelStorage::Astc4x4Rgba &&
                mobileRuntime->MipLevels.front().Pixels.size() ==
                    TextureMipByteSize(TexturePixelStorage::Astc4x4Rgba, 4, 4),
            "mobile profile must load native ASTC blocks from the cooked registry");
    return texture;
}

void TestColorGradingLut(TestContext &context)
{
    const auto source = context.Root / "Source/neutral.cube";
    WriteText(source,
              "TITLE \"Neutral 2\"\n"
              "LUT_3D_SIZE 2\n"
              "DOMAIN_MIN 0 0 0\n"
              "DOMAIN_MAX 1 1 1\n"
              "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
              "0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    const ImportResult imported =
        context.Pipeline.CreateAssetFromSource(source, {}, kColorGradingLutAssetType, "3d-lut");
    Require(imported.Succeeded && imported.GeneratedAssets.size() == 1,
            ".cube import must create one color grading asset");
    const AssetGuid guid = imported.GeneratedAssets.front().Descriptor.Guid;
    Require(context.Pipeline.TransformAsset(guid).Succeeded, "color grading LUT must cook");
    std::string error;
    const auto lut = context.Resources.Load<ColorGradingLutData>(guid, &error);
    Require(lut && lut->Size == 2 && lut->Values.size() == 8 &&
                glm::length(lut->Values.back() - glm::vec3(1.0f)) < 0.0001f &&
                lut->SourcePath == "asset://" + guid.ToString(),
            "runtime must load the complete cook-only LUT payload");
}

void TestTexturePolicy()
{
    Require(DetectTextureUsage("wall_normal.png", false) == TextureUsage::NormalMap,
            "normal filename must auto-detect normal usage");
    Require(DetectTextureUsage("wall_roughness.png", false) == TextureUsage::LinearData,
            "roughness must auto-detect linear usage");
    Require(DetectTextureUsage("studio.hdr", true) == TextureUsage::Hdr, "HDR metadata must override filename");
    TextureAssetSettings settings;
    settings.Usage = TextureUsage::Color;
    settings.ColorSpace = TextureColorSpace::Srgb;
    Require(ChooseTextureFormat(settings, true, false, "desktop") == RuntimeTextureFormat::Bc7Srgb,
            "high-quality desktop color texture must choose sRGB BC7");
    settings.Usage = TextureUsage::NormalMap;
    settings.ColorSpace = TextureColorSpace::Linear;
    Require(ChooseTextureFormat(settings, false, false, "desktop") == RuntimeTextureFormat::Bc5Unorm,
            "desktop normal map must choose two-channel BC5");
    settings.Usage = TextureUsage::Color;
    settings.ColorSpace = TextureColorSpace::Srgb;
    Require(ChooseTextureFormat(settings, true, false, "android") == RuntimeTextureFormat::Astc4x4Srgb,
            "mobile color texture must choose sRGB ASTC");
    settings.Compression = TextureCompressionQuality::None;
    Require(ChooseTextureFormat(settings, true, false, "desktop") == RuntimeTextureFormat::Rgba8Srgb,
            "disabled compression must preserve raw sRGB pixels");
    Require(CalculateTextureMipCount(8, 4, settings) == 4, "mipmap count must reach 1x1");
    settings.MaximumMipLevel = 1;
    Require(CalculateTextureMipCount(8, 4, settings) == 2, "maximum mip level must truncate chain");
}

void TestBlockCompression()
{
    constexpr uint32_t width = 7, height = 5;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
            pixels[offset + 0] = static_cast<uint8_t>(x * 31u);
            pixels[offset + 1] = static_cast<uint8_t>(y * 47u);
            pixels[offset + 2] = static_cast<uint8_t>((x + y) * 19u);
            pixels[offset + 3] = static_cast<uint8_t>(64u + x * 20u);
        }
    const TexturePixelStorage formats[] = {TexturePixelStorage::Bc1Rgb, TexturePixelStorage::Bc3Rgba,
                                           TexturePixelStorage::Bc5Rg, TexturePixelStorage::Bc7Rgba,
                                           TexturePixelStorage::Astc4x4Rgba};
    for (TexturePixelStorage format : formats)
    {
        std::vector<std::byte> first, second;
        std::string error;
        Require(cook::CompressTextureBlocks(format, width, height, pixels, cook::BlockCompressionQuality::Fast,
                                            true, first, &error),
                error.c_str());
        Require(first.size() == TextureMipByteSize(format, width, height),
                "GPU block encoder must emit the exact padded block payload size");
        Require(cook::CompressTextureBlocks(format, width, height, pixels, cook::BlockCompressionQuality::Fast,
                                            true, second, &error) && first == second,
                "GPU block encoding must be deterministic");
        Require(std::any_of(first.begin(), first.end(), [](std::byte value) { return value != std::byte{0}; }),
                "GPU block encoder must produce a non-empty coding result");
    }
}

void TestMeshOptimizationLodsAndCollision()
{
    MeshData grid = primitives::MakePlane(10.0f, 32);
    const size_t originalTriangles = grid.Indices.size() / 3u;
    cook::MeshOptimizationReport optimization;
    std::string error;
    Require(cook::OptimizeMesh(grid, true, &optimization, &error), error.c_str());
    Require(optimization.OutputVertices <= optimization.InputVertices &&
                optimization.VertexCacheAcmrAfter <= optimization.VertexCacheAcmrBefore + 0.05f,
            "meshoptimizer must preserve/deduplicate vertices and not materially regress cache efficiency");
    Require(std::all_of(grid.Indices.begin(), grid.Indices.end(),
                        [&](uint32_t index) { return index < grid.Vertices.size(); }),
            "optimized index data must remain in range");

    std::vector<cook::MeshLod> lods;
    Require(cook::GenerateMeshLods(grid, 4, 0.5f, 0.1f, true, lods, &error), error.c_str());
    Require(lods.size() >= 3u && lods.front().Mesh.Indices.size() / 3u == originalTriangles,
            "real simplification must create a multi-level LOD chain from LOD0");
    for (size_t level = 1; level < lods.size(); ++level)
    {
        Require(lods[level].Mesh.Indices.size() < lods[level - 1].Mesh.Indices.size(),
                "each generated LOD must contain fewer triangles");
        Require(lods[level].RelativeError >= 0.0f && lods[level].RelativeError <= 1.0f,
                "meshoptimizer must report a normalized geometric LOD error");
        Require(std::all_of(lods[level].Mesh.Indices.begin(), lods[level].Mesh.Indices.end(),
                            [&](uint32_t index) { return index < lods[level].Mesh.Vertices.size(); }),
                "LOD indices must remain valid after vertex-fetch compaction");
    }

    const MeshData cube = primitives::MakeCube();
    const cook::CollisionSourceMesh source{&cube, glm::mat4(1.0f)};
    cook::CollisionCookSettings triangleSettings;
    triangleSettings.Type = cook::CollisionType::SimplifiedTriangleMesh;
    triangleSettings.TriangleRatio = 0.5f;
    triangleSettings.SimplificationError = 0.2f;
    cook::CollisionData triangleCollision;
    Require(cook::CookCollision(std::span(&source, 1), triangleSettings, triangleCollision, &error), error.c_str());
    Require(triangleCollision.Meshes.size() == 1u && !triangleCollision.Meshes.front().Bvh.empty() &&
                triangleCollision.Meshes.front().Indices.size() <= cube.Indices.size(),
            "triangle collision cooker must emit simplified geometry with a traversal BVH");

    cook::CollisionCookSettings convexSettings;
    convexSettings.Type = cook::CollisionType::ConvexDecomposition;
    convexSettings.MaxConvexHulls = 4;
    convexSettings.MaxVerticesPerHull = 32;
    convexSettings.VoxelResolution = 10000;
    cook::CollisionData convexCollision;
    Require(cook::CookCollision(std::span(&source, 1), convexSettings, convexCollision, &error), error.c_str());
    Require(!convexCollision.Meshes.empty() && convexCollision.Meshes.size() <= 4u &&
                std::all_of(convexCollision.Meshes.begin(), convexCollision.Meshes.end(),
                            [](const cook::CollisionMesh &mesh) {
                                return mesh.Convex && !mesh.Vertices.empty() && !mesh.Indices.empty() &&
                                       !mesh.Bvh.empty();
                            }),
            "convex collision cooker must emit bounded, runtime-ready hull meshes");
}

AssetGuid TestCubemaps(TestContext &context)
{
    std::vector<AssetDiagnostic> diagnostics;
    std::array<CubemapFaceInfo, 6> valid{};
    for (auto &face : valid)
        face = {4, 4, true};
    Require(ValidateCubemapFaces(valid, diagnostics), "equal square cubemap faces must validate");
    valid[3].Width = 8;
    diagnostics.clear();
    Require(!ValidateCubemapFaces(valid, diagnostics), "mismatched cubemap face must fail validation");
    const auto panorama = context.Root / "Source/studio.hdr";
    WriteHdr(panorama);
    const ImportResult imported = CreateCubemapAsset(context.Pipeline, panorama);
    Require(imported.Succeeded, "HDR panorama must import as cubemap");
    auto record = context.Database.Find(imported.GeneratedAssets[0].Descriptor.Guid);
    Require(record.has_value(), "cubemap descriptor must enter database");
    CubemapAssetSettings settings = ReadCubemapAssetSettings(record->Descriptor);
    settings.FaceSize = 16;
    settings.IrradianceSize = 4;
    settings.PrefilterSize = 16;
    settings.FilterSampleCount = 8;
    WriteCubemapAssetSettings(record->Descriptor, settings);
    std::string error;
    Require(SaveAssetDescriptor(record->DescriptorPath, record->Descriptor, &error) &&
                context.Database.AddOrUpdate(record->DescriptorPath, record->Descriptor, &error),
            error.c_str());
    Require(context.Pipeline.TransformAsset(record->Descriptor.Guid).Succeeded,
            "HDR cubemap must cook environment maps");
    const auto cube = context.Resources.Load<CubemapData>(record->Descriptor.Guid, &error);
    Require(cube && cube->MipLevels.size() == 5 && cube->DiffuseIrradiance && cube->SpecularPrefilter.size() == 5,
            "cooked cubemap must contain mips, irradiance and GGX prefilter chain");
    std::array<std::filesystem::path, 6> faces;
    for (size_t index = 0; index < faces.size(); ++index)
    {
        faces[index] = context.Root / ("Source/face" + std::to_string(index) + ".ppm");
        WritePpm(faces[index], static_cast<uint8_t>(20 + index * 30), 80, 120);
    }
    const ImportResult six = CreateCubemapAsset(context.Pipeline, faces, context.Root / "Assets", "SixFace", &error);
    Require(six.Succeeded, "six-face cubemap helper must create descriptor");
    auto sixRecord = context.Database.Find(six.GeneratedAssets[0].Descriptor.Guid);
    settings = ReadCubemapAssetSettings(sixRecord->Descriptor);
    settings.FaceSize = 16;
    settings.IrradianceSize = 4;
    settings.PrefilterSize = 16;
    settings.FilterSampleCount = 8;
    WriteCubemapAssetSettings(sixRecord->Descriptor, settings);
    Require(SaveAssetDescriptor(sixRecord->DescriptorPath, sixRecord->Descriptor, &error) &&
                context.Database.AddOrUpdate(sixRecord->DescriptorPath, sixRecord->Descriptor, &error),
            error.c_str());
    Require(context.Pipeline.TransformAsset(sixRecord->Descriptor.Guid).Succeeded, "six-face cubemap must transform");
    AssetGuid skybox;
    Require(CreateSkyboxAsset(context.Pipeline, record->Descriptor.Guid, "Studio",
                              context.Root / "Assets/studio.sla-skybox", &skybox, &error),
            error.c_str());
    Require(context.Pipeline.TransformAsset(skybox).Succeeded, "skybox dependency must transform and cook");
    const auto sky = context.Resources.Load<SkyboxData>(skybox, &error);
    Require(sky && sky->Cubemap.Guid == record->Descriptor.Guid, "runtime skybox must retain typed cubemap handle");
    return skybox;
}

void WriteTriangleGltf(const std::filesystem::path &directory)
{
    WritePpm(directory / "triangle.ppm", 200, 150, 50);
    resources::BinaryWriter writer;
    for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
        writer.WriteF32(value);
    for (float value : {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f})
        writer.WriteF32(value);
    for (float value : {0.f, 0.f, 1.f, 0.f, 0.f, 1.f})
        writer.WriteF32(value);
    writer.WriteU16(0);
    writer.WriteU16(1);
    writer.WriteU16(2);
    std::string error;
    Require(WriteFileAtomic(directory / "triangle.bin", writer.Data(), &error), error.c_str());
    WriteText(
        directory / "triangle.gltf",
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":102}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"images":[{"uri":"triangle.ppm"}],"textures":[{"source":0}],"materials":[{"name":"TriangleMaterial","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0.2,"roughnessFactor":0.6}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
}

AssetGuid TestModel(TestContext &context)
{
    const glm::mat4 identity =
        ConvertCoordinateSystem(MeshAxis::PositiveY, MeshAxis::PositiveZ, MeshHandedness::RightHanded);
    Require(std::abs(identity[0][0] - 1) < 1e-6f && std::abs(identity[1][1] - 1) < 1e-6f,
            "default coordinate conversion must be identity");
    StaticMeshAssetSettings settings;
    settings.Translation = {1, 2, 3};
    settings.Scale = {2, 2, 2};
    const glm::mat4 transform = BuildMeshImportTransform(settings);
    Require(glm::length(glm::vec3(transform[3]) - glm::vec3(1, 2, 3)) < 1e-6f,
            "mesh import transform must include translation");
    std::vector<AssetDiagnostic> diagnostics;
    settings.SourceForward = MeshAxis::PositiveY;
    Require(!ValidateMeshImportSettings(settings, diagnostics), "parallel up/forward axes must fail validation");
    WriteTriangleGltf(context.Root / "Source");
    const ImportResult imported =
        context.Pipeline.CreateAssetFromSource(context.Root / "Source/triangle.gltf", {}, kStaticMeshAssetType);
    Require(imported.Succeeded && imported.GeneratedAssets.size() >= 3,
            "glTF import must generate texture, material and mesh descriptors");
    AssetGuid mesh;
    AssetGuid generatedTexture;
    AssetGuid generatedMaterial;
    size_t textures = 0, materials = 0;
    for (const auto &asset : imported.GeneratedAssets)
    {
        if (asset.Descriptor.Type == kStaticMeshAssetType)
            mesh = asset.Descriptor.Guid;
        else if (asset.Descriptor.Type == kTexture2DAssetType)
        {
            generatedTexture = asset.Descriptor.Guid;
            ++textures;
        }
        else if (asset.Descriptor.Type == kMaterialAssetType)
        {
            generatedMaterial = asset.Descriptor.Guid;
            ++materials;
        }
    }
    Require(mesh.IsValid() && textures == 1 && materials == 1,
            "model dependency generation must classify generated assets");
    const ImportResult reimported =
        context.Pipeline.CreateAssetFromSource(context.Root / "Source/triangle.gltf", {}, kStaticMeshAssetType);
    AssetGuid reimportedMesh;
    for (const auto &asset : reimported.GeneratedAssets)
        if (asset.Descriptor.Type == kStaticMeshAssetType)
            reimportedMesh = asset.Descriptor.Guid;
    Require(reimported.Succeeded && reimportedMesh == mesh,
            "model reimport must preserve stable generated and primary GUIDs");
    std::string error;
    auto meshRecord = context.Database.Find(mesh);
    Require(meshRecord && meshRecord->Descriptor.AssetDependencies.size() == 1,
            "mesh descriptor must depend on generated material");
    StaticMeshAssetSettings cookSettings = ReadStaticMeshAssetSettings(meshRecord->Descriptor);
    cookSettings.Collision = cook::CollisionType::TriangleMesh;
    WriteStaticMeshAssetSettings(meshRecord->Descriptor, cookSettings);
    Require(SaveAssetDescriptor(meshRecord->DescriptorPath, meshRecord->Descriptor, &error) &&
                context.Database.AddOrUpdate(meshRecord->DescriptorPath, meshRecord->Descriptor, &error),
            error.c_str());
    Require(context.Pipeline.TransformAsset(mesh).Succeeded, "dependency-ordered glTF transform must succeed");
    const ImportResult cachedReimport =
        context.Pipeline.CreateAssetFromSource(context.Root / "Source/triangle.gltf", {}, kStaticMeshAssetType);
    Require(cachedReimport.Succeeded, "unchanged glTF reimport must succeed");
    Require(context.Pipeline.TransformAsset(generatedTexture).SkippedAsUpToDate &&
                context.Pipeline.TransformAsset(generatedMaterial).SkippedAsUpToDate &&
                context.Pipeline.TransformAsset(mesh).SkippedAsUpToDate,
            "unchanged glTF reimport must preserve generated dependency cook fingerprints");
    const auto runtime = context.Resources.Load<StaticMeshData>(mesh, &error);
    Require(runtime && runtime->Parts.size() == 1 && runtime->Parts[0].Mesh->Vertices.size() == 3 &&
                runtime->Parts[0].Mesh->Indices.size() == 3 && runtime->Collision.Meshes.size() == 1 &&
                !runtime->Collision.Meshes.front().Bvh.empty(),
            "runtime static mesh must load optimized geometry and versioned collision BVH payload");
    return mesh;
}

void TestScene(TestContext &context, AssetGuid mesh, AssetGuid skybox)
{
    std::fprintf(stderr, "  scene descriptor roundtrip\n");
    SceneAssetData scene;
    scene.Skybox.Guid = skybox;
    scene.Layers = {"Default", "Gameplay"};
    scene.Environment = {{"fog.enabled", "true"}};
    scene.EditorMetadata = {{"camera.position", "10,5,2"}};
    SceneObjectRecord parent;
    parent.Guid = AssetGuid::Generate();
    parent.Name = "Parent";
    parent.AssetReferences = {mesh};
    parent.Components.push_back({"MeshRenderer", 1, true, {{"mesh", mesh.ToString()}}});
    SceneObjectRecord child;
    child.Guid = AssetGuid::Generate();
    child.Parent = parent.Guid;
    child.Name = "Child";
    child.Position = {1, 2, 3};
    scene.ActiveCameraObject = child.Guid;
    scene.Objects = {parent, child};
    AssetDescriptor descriptor;
    descriptor.Guid = AssetGuid::Generate();
    descriptor.Type = std::string(kSceneAssetType);
    descriptor.Name = "RoundTripScene";
    WriteSceneAssetData(descriptor, scene);
    SceneAssetData decoded;
    std::string error;
    const bool read = ReadSceneAssetData(descriptor, decoded, &error);
    if (!read)
        std::fprintf(stderr, "scene read error: %s\n", error.c_str());
    Require(read, "scene descriptor must deserialize");
    Require(decoded.Objects.size() == 2 && decoded.Objects[1].Parent == parent.Guid &&
                decoded.EditorMetadata == scene.EditorMetadata,
            "scene hierarchy descriptor round-trip must preserve authoring data");
    std::fprintf(stderr, "  scene validation\n");
    std::vector<AssetDiagnostic> diagnostics;
    const bool valid = ValidateSceneReferences(
        decoded, context.Database, [](std::string_view type, uint32_t) { return type == "MeshRenderer"; }, diagnostics);
    if (!valid)
        for (const auto &diagnostic : diagnostics)
            std::fprintf(stderr, "scene validation: %s\n", diagnostic.Message.c_str());
    Require(valid, "valid scene references and components must pass");
    decoded.Objects[0].Parent = decoded.Objects[1].Guid;
    diagnostics.clear();
    Require(!ValidateSceneReferences(decoded, context.Database, {}, diagnostics),
            "parent hierarchy cycle must be rejected");
    decoded = scene;
    std::fprintf(stderr, "  scene create\n");
    AssetGuid sceneGuid;
    const bool created = CreateSceneAsset(context.Pipeline, scene, "TestScene", context.Root / "Assets/test.sla-scene",
                                          &sceneGuid, &error);
    if (!created)
        std::fprintf(stderr, "scene create error: %s\n", error.c_str());
    Require(created, "scene helper must create asset");
    std::fprintf(stderr, "  scene transform\n");
    const auto transformed = context.Pipeline.TransformAsset(sceneGuid);
    if (!transformed.Succeeded)
        for (const auto &diagnostic : transformed.Diagnostics)
            std::fprintf(stderr, "scene transform: %s\n", diagnostic.Message.c_str());
    Require(transformed.Succeeded, "scene must export after dependencies");
    std::fprintf(stderr, "  scene runtime load\n");
    const auto runtime = context.Resources.Load<SceneAssetData>(sceneGuid, &error);
    Require(runtime && runtime->Objects.size() == 2 && runtime->EditorMetadata.empty(),
            "runtime scene must load and omit editor-only metadata");
}

void TestEditorFoundations(TestContext &context, AssetGuid texture)
{
    AssetBrowserModel browser(context.Pipeline);
    AssetBrowserQuery query;
    query.Search = "albedo";
    const auto entries = browser.Query(query);
    Require(entries.size() == 1 && entries[0].Guid == texture, "asset browser search must return indexed entries");
    browser.Select(texture);
    Require(browser.Selection() == std::vector<AssetGuid>{texture}, "asset browser selection must retain GUIDs");
    AssetDragPayload payload;
    payload.Assets = {texture};
    payload.SourceFiles = {context.Root / "Source/albedo.ppm"};
    std::vector<std::byte> encoded;
    std::string error;
    Require(EncodeAssetDragPayload(payload, encoded, &error), error.c_str());
    AssetDragPayload decoded;
    Require(DecodeAssetDragPayload(encoded, decoded, &error) && decoded.Assets == payload.Assets &&
                decoded.SourceFiles == payload.SourceFiles,
            "versioned drag payload must round-trip");
    encoded.push_back(std::byte{0});
    Require(!DecodeAssetDragPayload(encoded, decoded, &error), "drag payload with trailing bytes must be rejected");
    AssetPreviewService previews(context.Types, context.Database, context.Root);
    const auto a = previews.Request(texture, 32, 32);
    const auto b = previews.Request(texture, 32, 32);
    Require(a.valid() && b.valid() && a.get().Succeeded, "texture preview provider must produce CPU preview data");
    Require(previews.CachedPreviewCount() == 1, "identical concurrent preview requests must be de-duplicated");
    previews.Invalidate(texture);
    Require(previews.CachedPreviewCount() == 0, "preview invalidation must remove cached requests");
}

void TestMigrationDuplicateAndCorruption(TestContext &context)
{
    AssetTypeRegistry registry;
    AssetTypeRegistration type;
    type.TypeId = "dummy";
    type.DisplayName = "Dummy";
    type.DescriptorExtension = "sla-dummy";
    type.RuntimeType = "dummy";
    type.DescriptorVersion = 3;
    type.Migrations[1] = [](AssetDescriptor &descriptor, std::string *) {
        descriptor.Settings["v2"] = "yes";
        return true;
    };
    type.Migrations[2] = [](AssetDescriptor &descriptor, std::string *) {
        descriptor.Settings["v3"] = "yes";
        return true;
    };
    std::string error;
    Require(registry.Register(std::move(type), &error), error.c_str());
    AssetDescriptor descriptor;
    descriptor.Guid = AssetGuid::Generate();
    descriptor.Type = "dummy";
    descriptor.Name = "Old";
    descriptor.DescriptorVersion = 1;
    const auto root = context.Root / "Migration";
    std::filesystem::create_directories(root);
    const auto first = root / "a.sla-dummy";
    Require(SaveAssetDescriptor(first, descriptor, &error), error.c_str());
    std::filesystem::path backup;
    Require(MigrateAssetDescriptorFile(first, registry, &backup, &error) && std::filesystem::is_regular_file(backup),
            "migration must create backup and apply sequential steps");
    AssetDescriptor migrated;
    Require(LoadAssetDescriptor(first, migrated, &error) && migrated.DescriptorVersion == 3 &&
                migrated.Settings.contains("v2") && migrated.Settings.contains("v3"),
            "descriptor migration must reach current version");
    const auto second = root / "b.sla-dummy";
    Require(SaveAssetDescriptor(second, migrated, &error), error.c_str());
    AssetDatabase database(registry);
    const auto scan = database.Scan(root);
    Require(scan.DuplicateGuids == 1 && !scan.Succeeded(), "database scan must detect copied duplicate GUIDs");
    const auto bad = context.Root / "bad.slres";
    WriteText(bad, "not-a-resource");
    resources::CookedResourceData resource;
    Require(!resources::ReadCookedResource(bad, resource, &error), "unknown cooked magic must be rejected");
    const auto atomic = context.Root / "atomic.txt";
    WriteText(atomic, "old");
    WriteText(atomic, "new");
    std::string text;
    Require(ReadTextFile(atomic, text, &error) && text == "new", "atomic replacement must leave a complete new file");
}

} // namespace

int main()
{
    std::fprintf(stderr, "asset-test: texture policy\n");
    TestTexturePolicy();
    std::fprintf(stderr, "asset-test: BC/ASTC compression\n");
    TestBlockCompression();
    std::fprintf(stderr, "asset-test: mesh optimization, LOD and collision\n");
    TestMeshOptimizationLodsAndCollision();
    TestContext context;
    std::fprintf(stderr, "asset-test: descriptor and texture\n");
    const AssetGuid texture = TestDescriptorAndTexture(context);
    std::fprintf(stderr, "asset-test: color grading LUT\n");
    TestColorGradingLut(context);
    std::fprintf(stderr, "asset-test: cubemaps\n");
    const AssetGuid skybox = TestCubemaps(context);
    std::fprintf(stderr, "asset-test: model\n");
    const AssetGuid mesh = TestModel(context);
    std::fprintf(stderr, "asset-test: scene\n");
    TestScene(context, mesh, skybox);
    std::fprintf(stderr, "asset-test: editor foundations\n");
    TestEditorFoundations(context, texture);
    std::fprintf(stderr, "asset-test: migration/corruption\n");
    TestMigrationDuplicateAndCorruption(context);
    std::puts("Asset pipeline tests passed");
    return EXIT_SUCCESS;
}
