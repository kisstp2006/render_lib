#include "engine/asset/ColorGradingAsset.h"

#include "engine/asset/AssetFileSystem.h"
#include "engine/resource/BinaryIO.h"

#include <cmath>
#include <cstring>
#include <exception>

namespace engine::assets
{
namespace
{

AssetDiagnostic Diagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message,
                           const std::filesystem::path &source = {})
{
    AssetDiagnostic diagnostic;
    diagnostic.Severity = severity;
    diagnostic.Code = std::move(code);
    diagnostic.Message = std::move(message);
    diagnostic.Source = source;
    diagnostic.Step = "color grading LUT import";
    return diagnostic;
}

std::vector<std::byte> Encode(const ColorGradingLutData &lut)
{
    resources::BinaryWriter writer;
    writer.WriteU32(1);
    writer.WriteU32(static_cast<uint32_t>(lut.Size));
    for (float value : {lut.DomainMin.x, lut.DomainMin.y, lut.DomainMin.z,
                        lut.DomainMax.x, lut.DomainMax.y, lut.DomainMax.z})
        writer.WriteF32(value);
    writer.WriteU64(lut.Values.size());
    for (const glm::vec3 &color : lut.Values)
        for (float value : {color.r, color.g, color.b})
            writer.WriteF32(value);
    return writer.TakeData();
}

std::shared_ptr<ColorGradingLutData> Load(const resources::ResourceLoadContext &context, std::string *error)
{
    resources::BinaryReader reader(context.Payload);
    uint32_t version = 0;
    uint32_t size = 0;
    uint64_t valueCount = 0;
    auto lut = std::make_shared<ColorGradingLutData>();
    if (!reader.ReadU32(version) || version != 1 || !reader.ReadU32(size) || size < 2 || size > 128)
    {
        if (error)
            *error = "Malformed color grading LUT header";
        return {};
    }
    lut->Size = static_cast<int>(size);
    float *domain[] = {&lut->DomainMin.x, &lut->DomainMin.y, &lut->DomainMin.z,
                       &lut->DomainMax.x, &lut->DomainMax.y, &lut->DomainMax.z};
    for (float *value : domain)
        if (!reader.ReadF32(*value) || !std::isfinite(*value))
        {
            if (error)
                *error = "Malformed color grading LUT domain";
            return {};
        }
    const uint64_t expected = static_cast<uint64_t>(size) * size * size;
    if (!reader.ReadU64(valueCount) || valueCount != expected)
    {
        if (error)
            *error = "Malformed color grading LUT sample count";
        return {};
    }
    lut->Values.resize(static_cast<size_t>(valueCount));
    for (glm::vec3 &color : lut->Values)
        for (int channel = 0; channel < 3; ++channel)
            if (!reader.ReadF32(color[channel]) || !std::isfinite(color[channel]))
            {
                if (error)
                    *error = "Truncated or non-finite color grading LUT samples";
                return {};
            }
    if (reader.Remaining() != 0 || glm::any(glm::lessThanEqual(lut->DomainMax, lut->DomainMin)))
    {
        if (error)
            *error = "Color grading LUT resource contains invalid domain data or trailing bytes";
        return {};
    }
    lut->SourcePath = "asset://" + context.Header.Asset.ToString();
    return lut;
}

} // namespace

bool RegisterColorGradingAssetType(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                                   std::string *error)
{
    AssetTypeRegistration type;
    type.TypeId = std::string(kColorGradingLutAssetType);
    type.DisplayName = "Color Grading LUT";
    type.DescriptorExtension = std::string(kColorGradingLutDescriptorExtension);
    type.SourceExtensions = {"cube"};
    type.ImportModes = {{"3d-lut", "3D color grading LUT", 100}};
    type.RuntimeType = std::string(kColorGradingLutResourceType);
    type.Icon = "color-grading-lut";
    type.Import = [](const AssetImportRequest &request) {
        ImportResult result;
        std::string pathError;
        const std::filesystem::path relative =
            NormalizeProjectRelative(request.ProjectRoot, request.SourcePath, &pathError);
        if (relative.empty())
        {
            result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "source_outside_project",
                                                    pathError, request.SourcePath));
            return result;
        }
        ImportedAsset asset;
        asset.DescriptorPath = request.DestinationDirectory /
                               (request.SourcePath.stem().generic_string() + "." +
                                std::string(kColorGradingLutDescriptorExtension));
        AssetDescriptor existing;
        std::string ignored;
        if (LoadAssetDescriptor(asset.DescriptorPath, existing, &ignored) &&
            existing.Type == kColorGradingLutAssetType)
        {
            asset.Descriptor = std::move(existing);
            asset.Reused = true;
        }
        else
        {
            asset.Descriptor.Guid = AssetGuid::Generate();
            asset.Descriptor.Type = std::string(kColorGradingLutAssetType);
            asset.Descriptor.Name = request.SourcePath.stem().generic_string();
        }
        asset.Descriptor.Sources = {relative};
        asset.Descriptor.SourceDependencies = {relative};
        asset.Descriptor.State = AssetImportState::Dirty;
        result.GeneratedAssets.push_back(std::move(asset));
        result.Succeeded = true;
        return result;
    };
    type.Validate = [](const AssetDescriptor &descriptor, const AssetValidationContext &context) {
        std::vector<AssetDiagnostic> diagnostics;
        if (descriptor.Sources.size() != 1)
        {
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "lut_source_count",
                                             "A color grading LUT requires exactly one .cube source."));
            return diagnostics;
        }
        try
        {
            color_grading::LoadCube((context.ProjectRoot / descriptor.Sources.front()).string());
        }
        catch (const std::exception &exception)
        {
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "lut_parse_failed",
                                             exception.what(), descriptor.Sources.front()));
        }
        return diagnostics;
    };
    type.Transform = [](const AssetDescriptor &descriptor, const AssetTransformContext &context,
                        AssetTransformOutput &output, std::string *transformError) {
        try
        {
            const std::shared_ptr<ColorGradingLutData> lut =
                color_grading::LoadCube((context.ProjectRoot / descriptor.Sources.front()).string());
            output.RuntimeType = std::string(kColorGradingLutResourceType);
            output.ResourceVersion = 1;
            output.Compression = "rle";
            output.Payload = Encode(*lut);
            output.Statistics["lut_size"] = static_cast<uint64_t>(lut->Size);
            output.Statistics["samples"] = lut->Values.size();
            output.Statistics["payload_bytes"] = output.Payload.size();
            return true;
        }
        catch (const std::exception &exception)
        {
            if (transformError)
                *transformError = exception.what();
            return false;
        }
    };
    type.Preview = [](const AssetDescriptor &descriptor, const AssetPreviewRequest &request, AssetPreview &preview,
                      std::string *previewError) {
        try
        {
            const std::shared_ptr<ColorGradingLutData> lut =
                color_grading::LoadCube((request.ProjectRoot / descriptor.Sources.front()).string());
            preview.Kind = "color-grading-lut";
            preview.MimeType = "application/x-rgb32f-3d";
            preview.Width = static_cast<uint32_t>(lut->Size);
            preview.Height = static_cast<uint32_t>(lut->Size * lut->Size);
            preview.Bytes.resize(lut->Values.size() * sizeof(glm::vec3));
            std::memcpy(preview.Bytes.data(), lut->Values.data(), preview.Bytes.size());
            preview.Metadata["size"] = std::to_string(lut->Size);
            preview.CacheKey = HashFile(request.ProjectRoot / descriptor.Sources.front(), nullptr);
            return true;
        }
        catch (const std::exception &exception)
        {
            if (previewError)
                *previewError = exception.what();
            return false;
        }
    };
    if (!registry.Register(std::move(type), error))
        return false;
    if (!resourceManager.RegisterLoader<ColorGradingLutData>(std::string(kColorGradingLutResourceType), Load, error))
    {
        registry.Unregister(kColorGradingLutAssetType);
        return false;
    }
    return true;
}

} // namespace engine::assets
