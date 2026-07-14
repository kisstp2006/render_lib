#include "engine/asset/AssetPipeline.h"

#include "engine/asset/AssetFileSystem.h"
#include "engine/resource/CookedResource.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

namespace engine::assets
{
namespace
{

bool HasFatal(const std::vector<AssetDiagnostic> &diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const AssetDiagnostic &diagnostic) {
        return diagnostic.Severity == AssetDiagnosticSeverity::FatalError;
    });
}

std::string ExtensionOf(const std::filesystem::path &path)
{
    std::string extension = path.extension().generic_string();
    if (!extension.empty() && extension.front() == '.')
        extension.erase(extension.begin());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

AssetFingerprint HashNamedFingerprints(std::vector<std::pair<std::string, AssetFingerprint>> values)
{
    std::sort(values.begin(), values.end(),
              [](const auto &left, const auto &right) { return left.first < right.first; });
    std::vector<AssetFingerprint> parts;
    parts.reserve(values.size() * 2);
    for (const auto &[name, fingerprint] : values)
    {
        parts.push_back(HashString(name));
        parts.push_back(fingerprint);
    }
    return CombineFingerprints(parts);
}

} // namespace

AssetPipeline::AssetPipeline(AssetTypeRegistry &types, AssetDatabase &database, resources::ResourceManager &resources,
                             AssetPipelineConfig config)
    : m_types(types), m_database(database), m_resources(resources), m_config(std::move(config))
{
    std::error_code error;
    if (m_config.ProjectRoot.empty())
        m_config.ProjectRoot = std::filesystem::current_path(error);
    m_config.ProjectRoot = std::filesystem::absolute(m_config.ProjectRoot, error).lexically_normal();
    if (m_config.AssetRoot.is_relative())
        m_config.AssetRoot = m_config.ProjectRoot / m_config.AssetRoot;
    if (m_config.CacheRoot.is_relative())
        m_config.CacheRoot = m_config.ProjectRoot / m_config.CacheRoot;
    m_config.AssetRoot = m_config.AssetRoot.lexically_normal();
    m_config.CacheRoot = m_config.CacheRoot.lexically_normal();
    m_resources.SetCookedRoot(m_config.CacheRoot);
}

std::filesystem::path AssetPipeline::AbsoluteProjectPath(const std::filesystem::path &path) const
{
    return (path.is_absolute() ? path : m_config.ProjectRoot / path).lexically_normal();
}

AssetDiagnostic AssetPipeline::Diagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message,
                                          const AssetDescriptor *descriptor, const std::filesystem::path &source,
                                          std::string step, std::string suggestion) const
{
    AssetDiagnostic diagnostic;
    diagnostic.Severity = severity;
    diagnostic.Code = std::move(code);
    if (descriptor)
        diagnostic.Message = "Asset '" + descriptor->Name + "' [" + descriptor->Guid.ToString() + "]: " + message;
    else
        diagnostic.Message = std::move(message);
    diagnostic.Source = source;
    diagnostic.Step = std::move(step);
    diagnostic.Suggestion = std::move(suggestion);
    return diagnostic;
}

std::filesystem::path AssetPipeline::GetAssetDescriptorPath(const std::filesystem::path &source,
                                                            const AssetTypeRegistration &type,
                                                            const std::filesystem::path &destinationDirectory) const
{
    std::filesystem::path directory = destinationDirectory;
    if (directory.empty())
    {
        std::string pathError;
        const std::filesystem::path relative =
            NormalizeProjectRelative(m_config.ProjectRoot, AbsoluteProjectPath(source), &pathError);
        directory = pathError.empty() ? m_config.AssetRoot / relative.parent_path() : m_config.AssetRoot;
    }
    else if (directory.is_relative())
        directory = m_config.AssetRoot / directory;
    return (directory / source.stem()).replace_extension('.' + type.DescriptorExtension);
}

ImportResult AssetPipeline::CreateAssetFromSource(const std::filesystem::path &source,
                                                  const std::filesystem::path &destinationDirectory,
                                                  std::string_view requestedType, std::string_view importMode)
{
    ImportResult result;
    const std::filesystem::path absoluteSource = AbsoluteProjectPath(source);
    if (!std::filesystem::is_regular_file(absoluteSource))
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "source_missing",
                                                "Source file does not exist: " + absoluteSource.generic_string(),
                                                nullptr, absoluteSource, "source recognition",
                                                "Correct the path or restore the source file."));
        return result;
    }

    const AssetTypeRegistration *type = nullptr;
    if (!requestedType.empty())
    {
        type = m_types.Find(requestedType);
        if (!type || !type->Import)
        {
            result.Diagnostics.push_back(
                Diagnostic(AssetDiagnosticSeverity::FatalError, "importer_missing",
                           "Requested asset type has no importer: " + std::string(requestedType), nullptr,
                           absoluteSource, "source recognition"));
            return result;
        }
    }
    else
    {
        const auto importers = m_types.FindImportersForExtension(ExtensionOf(absoluteSource));
        if (importers.empty())
        {
            result.Diagnostics.push_back(
                Diagnostic(AssetDiagnosticSeverity::FatalError, "unsupported_source_extension",
                           "No importer supports source extension ." + ExtensionOf(absoluteSource), nullptr,
                           absoluteSource, "source recognition"));
            return result;
        }
        type = importers.front();
    }

    AssetImportRequest request;
    request.ProjectRoot = m_config.ProjectRoot;
    request.SourcePath = absoluteSource;
    request.DestinationDirectory = destinationDirectory.empty()
                                       ? GetAssetDescriptorPath(absoluteSource, *type).parent_path()
                                       : destinationDirectory;
    if (request.DestinationDirectory.is_relative())
        request.DestinationDirectory = m_config.AssetRoot / request.DestinationDirectory;
    request.RequestedType = type->TypeId;
    request.Mode = std::string(importMode);
    if (request.Mode.empty() && !type->ImportModes.empty())
    {
        request.Mode =
            std::max_element(type->ImportModes.begin(), type->ImportModes.end(),
                             [](const auto &left, const auto &right) { return left.Priority < right.Priority; })
                ->Name;
    }

    result = type->Import(request);
    if (!result.Succeeded)
        return result;
    for (ImportedAsset &imported : result.GeneratedAssets)
    {
        const AssetTypeRegistration *importedType = m_types.Find(imported.Descriptor.Type);
        if (!importedType)
        {
            result.Succeeded = false;
            result.Diagnostics.push_back(
                Diagnostic(AssetDiagnosticSeverity::FatalError, "generated_unknown_type",
                           "Importer generated an unregistered asset type: " + imported.Descriptor.Type,
                           &imported.Descriptor, imported.DescriptorPath, "descriptor creation"));
            continue;
        }
        if (imported.DescriptorPath.empty())
            imported.DescriptorPath =
                GetAssetDescriptorPath(absoluteSource, *importedType, request.DestinationDirectory);
        std::string saveError;
        if (!importedType->SaveDescriptor(imported.DescriptorPath, imported.Descriptor, &saveError) ||
            !m_database.AddOrUpdate(imported.DescriptorPath, imported.Descriptor, &saveError))
        {
            result.Succeeded = false;
            result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "descriptor_save_failed",
                                                    saveError, &imported.Descriptor, imported.DescriptorPath,
                                                    "descriptor creation",
                                                    "Check directory permissions and duplicate GUID diagnostics."));
        }
    }
    return result;
}

ImportResult AssetPipeline::CreateAssetsFromSources(std::span<const std::filesystem::path> sources,
                                                    const std::filesystem::path &destinationDirectory)
{
    ImportResult combined;
    combined.Succeeded = true;
    for (const auto &source : sources)
    {
        ImportResult single = CreateAssetFromSource(source, destinationDirectory);
        combined.Succeeded = combined.Succeeded && single.Succeeded;
        combined.Diagnostics.insert(combined.Diagnostics.end(), std::make_move_iterator(single.Diagnostics.begin()),
                                    std::make_move_iterator(single.Diagnostics.end()));
        combined.GeneratedAssets.insert(combined.GeneratedAssets.end(),
                                        std::make_move_iterator(single.GeneratedAssets.begin()),
                                        std::make_move_iterator(single.GeneratedAssets.end()));
        combined.ReusedAssets.insert(combined.ReusedAssets.end(), std::make_move_iterator(single.ReusedAssets.begin()),
                                     std::make_move_iterator(single.ReusedAssets.end()));
        combined.GeneratedRuntimeFiles.insert(combined.GeneratedRuntimeFiles.end(),
                                              single.GeneratedRuntimeFiles.begin(), single.GeneratedRuntimeFiles.end());
        for (const auto &[key, value] : single.Statistics)
            combined.Statistics[key] += value;
    }
    return combined;
}

ImportResult AssetPipeline::ImportDroppedFiles(std::span<const std::filesystem::path> sources,
                                               const std::filesystem::path &destinationDirectory)
{
    return CreateAssetsFromSources(sources, destinationDirectory);
}

std::map<std::string, std::string> AssetPipeline::ResolveSettings(const AssetDescriptor &descriptor,
                                                                  const AssetTypeRegistration &type) const
{
    // Base descriptor values are followed by registered profile policy and finally
    // explicit per-asset profile overrides. This makes a mobile/low profile useful
    // even when an importer wrote its desktop defaults into the descriptor.
    std::map<std::string, std::string> settings = descriptor.Settings;
    if (type.ProfileDefaults)
        for (const auto &[key, value] : type.ProfileDefaults(m_config.Profile))
            settings.insert_or_assign(key, value);
    for (const AssetPlatformOverride &overrideSettings : descriptor.PlatformOverrides)
        if (overrideSettings.Profile == m_config.Profile)
            for (const auto &[key, value] : overrideSettings.Settings)
                settings.insert_or_assign(key, value);
    return settings;
}

AssetFingerprint AssetPipeline::ComputeSourceFingerprint(const AssetDescriptor &descriptor, std::string *error) const
{
    if (error)
        error->clear();
    std::map<std::string, AssetFingerprint> uniqueFiles;
    auto append = [&](const std::filesystem::path &path) {
        const std::filesystem::path absolute = AbsoluteProjectPath(path);
        std::string hashError;
        AssetFingerprint hash = HashFile(absolute, &hashError);
        if (!hash.IsValid() && error && error->empty())
            *error = hashError;
        uniqueFiles.insert_or_assign(path.lexically_normal().generic_string(), hash);
    };
    for (const auto &source : descriptor.Sources)
        append(source);
    for (const auto &dependency : descriptor.SourceDependencies)
        append(dependency);
    if (uniqueFiles.empty())
        return HashString("descriptor-authored-no-source-files");
    if (error && !error->empty())
        return {};
    std::vector<std::pair<std::string, AssetFingerprint>> files(uniqueFiles.begin(), uniqueFiles.end());
    return HashNamedFingerprints(std::move(files));
}

AssetFingerprint AssetPipeline::ComputeDependencyFingerprint(const AssetDescriptor &descriptor,
                                                             std::string *error) const
{
    if (error)
        error->clear();
    std::vector<std::pair<std::string, AssetFingerprint>> dependencies;
    for (AssetGuid guid : descriptor.AssetDependencies)
    {
        const auto dependency = m_database.Find(guid);
        if (!dependency)
        {
            if (error)
                *error = "Missing dependency asset: " + guid.ToString();
            return {};
        }
        if (!dependency->Descriptor.LastTransformFingerprint.IsValid())
        {
            if (error)
                *error = "Dependency has not been transformed: " + guid.ToString();
            return {};
        }
        dependencies.emplace_back(guid.ToString(), dependency->Descriptor.LastTransformFingerprint);
    }
    return dependencies.empty() ? HashString("no-asset-dependencies") : HashNamedFingerprints(std::move(dependencies));
}

AssetFingerprint AssetPipeline::ComputeAssetFingerprint(AssetGuid guid, std::string *error) const
{
    if (error)
        error->clear();
    const auto record = m_database.Find(guid);
    if (!record)
    {
        if (error)
            *error = "Unknown asset GUID: " + guid.ToString();
        return {};
    }
    const AssetTypeRegistration *type = m_types.Find(record->Descriptor.Type);
    if (!type)
    {
        if (error)
            *error = "Unknown asset type: " + record->Descriptor.Type;
        return {};
    }
    std::string sourceError;
    const AssetFingerprint source = ComputeSourceFingerprint(record->Descriptor, &sourceError);
    if (!source.IsValid())
    {
        if (error)
            *error = sourceError;
        return {};
    }
    std::string dependencyError;
    const AssetFingerprint dependencies = ComputeDependencyFingerprint(record->Descriptor, &dependencyError);
    if (!dependencies.IsValid())
    {
        if (error)
            *error = dependencyError;
        return {};
    }

    AssetDescriptor settingsDescriptor = record->Descriptor;
    settingsDescriptor.Settings = ResolveSettings(record->Descriptor, *type);
    settingsDescriptor.PlatformOverrides.clear();
    // Use the registered transformer versions, not stale descriptor metadata.
    // Otherwise the first transform after a version bump immediately appears
    // dirty again when finalization updates the descriptor versions.
    settingsDescriptor.ImporterVersion = type->ImporterVersion;
    settingsDescriptor.TransformerVersion = type->TransformerVersion;
    const AssetFingerprint settings = HashString(SerializeAssetSettings(settingsDescriptor, m_config.Profile));
    const std::array<AssetFingerprint, 7> parts = {
        source,
        settings,
        dependencies,
        HashString(std::to_string(type->ImporterVersion)),
        HashString(std::to_string(type->TransformerVersion)),
        HashString(m_config.Platform),
        HashString(m_config.Profile + ':' + std::to_string(type->ResourceVersion)),
    };
    return CombineFingerprints(parts);
}

std::filesystem::path AssetPipeline::GetCookedResourcePath(AssetGuid guid) const
{
    return m_config.CacheRoot / m_config.Platform / m_config.Profile / (guid.ToString() + ".slres");
}

bool AssetPipeline::IsAssetDirty(AssetGuid guid, std::string *reason) const
{
    const auto record = m_database.Find(guid);
    if (!record)
    {
        if (reason)
            *reason = "asset is not registered";
        return true;
    }
    std::string fingerprintError;
    const AssetFingerprint current = ComputeAssetFingerprint(guid, &fingerprintError);
    if (!current.IsValid())
    {
        if (reason)
            *reason = fingerprintError;
        return true;
    }
    if (current != record->Descriptor.LastTransformFingerprint)
    {
        if (reason)
            *reason = "source, settings, dependency, version, platform or profile changed";
        return true;
    }
    const std::filesystem::path cookedPath = GetCookedResourcePath(guid);
    resources::CookedResourceHeader header;
    std::string headerError;
    if (!resources::ReadCookedResourceHeader(cookedPath, header, &headerError) || header.Asset != guid ||
        header.TransformFingerprint != current)
    {
        if (reason)
            *reason = headerError.empty() ? "cooked header is stale" : headerError;
        return true;
    }
    return false;
}

std::shared_ptr<std::mutex> AssetPipeline::GetTransformMutex(AssetGuid guid)
{
    std::scoped_lock lock(m_transformMutexMapMutex);
    auto &weak = m_transformMutexes[guid];
    if (auto existing = weak.lock())
        return existing;
    auto created = std::make_shared<std::mutex>();
    weak = created;
    return created;
}

AssetTransformResult AssetPipeline::TransformAsset(AssetGuid guid, bool force)
{
    std::unordered_set<AssetGuid, AssetGuidHash> stack;
    return TransformAssetInternal(guid, force, stack);
}

AssetTransformResult AssetPipeline::TransformAsset(const std::filesystem::path &descriptorPath, bool force)
{
    const auto guid = m_database.ResolveGuid(descriptorPath);
    if (!guid)
    {
        AssetTransformResult result;
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "asset_not_registered",
                                                "Descriptor is not registered: " + descriptorPath.generic_string(),
                                                nullptr, descriptorPath, "transform dispatch",
                                                "Scan the asset root before transforming."));
        return result;
    }
    return TransformAsset(*guid, force);
}

AssetTransformResult AssetPipeline::TransformAssetInternal(AssetGuid guid, bool force,
                                                           std::unordered_set<AssetGuid, AssetGuidHash> &stack)
{
    AssetTransformResult result;
    if (!stack.insert(guid).second)
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "circular_asset_dependency",
                                                "Circular dependency reached while transforming " + guid.ToString(),
                                                nullptr, {}, "dependency transform"));
        return result;
    }
    const auto pop = [&] { stack.erase(guid); };

    const std::shared_ptr<std::mutex> assetMutex = GetTransformMutex(guid);
    std::unique_lock transformLock(*assetMutex);
    auto record = m_database.Find(guid);
    if (!record)
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "asset_not_registered",
                                                "Unknown asset GUID: " + guid.ToString(), nullptr, {},
                                                "transform dispatch"));
        pop();
        return result;
    }
    const AssetTypeRegistration *type = m_types.Find(record->Descriptor.Type);
    if (!type || !type->Transform)
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "transformer_missing",
                                                "No transformer registered for type '" + record->Descriptor.Type + "'",
                                                &record->Descriptor, record->DescriptorPath, "transform dispatch"));
        pop();
        return result;
    }

    for (AssetGuid dependency : record->Descriptor.AssetDependencies)
    {
        AssetTransformResult dependencyResult = TransformAssetInternal(dependency, force, stack);
        result.Diagnostics.insert(result.Diagnostics.end(), dependencyResult.Diagnostics.begin(),
                                  dependencyResult.Diagnostics.end());
        if (!dependencyResult.Succeeded)
        {
            result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "dependency_transform_failed",
                                                    "Dependency failed to transform: " + dependency.ToString(),
                                                    &record->Descriptor, record->DescriptorPath,
                                                    "dependency transform"));
            pop();
            return result;
        }
    }

    std::string dirtyReason;
    if (!force && !IsAssetDirty(guid, &dirtyReason))
    {
        result.Succeeded = true;
        result.SkippedAsUpToDate = true;
        result.Fingerprint = record->Descriptor.LastTransformFingerprint;
        pop();
        return result;
    }

    const AssetValidationContext validationContext{m_config.ProjectRoot, record->DescriptorPath, m_config.Platform,
                                                   m_config.Profile};
    if (type->Validate)
        result.Diagnostics = type->Validate(record->Descriptor, validationContext);
    if (HasFatal(result.Diagnostics))
    {
        record->Descriptor.State = AssetImportState::Error;
        record->Descriptor.Diagnostics = result.Diagnostics;
        std::string saveError;
        type->SaveDescriptor(record->DescriptorPath, record->Descriptor, &saveError);
        m_database.AddOrUpdate(record->DescriptorPath, record->Descriptor, nullptr);
        pop();
        return result;
    }

    std::string fingerprintError;
    result.Fingerprint = ComputeAssetFingerprint(guid, &fingerprintError);
    if (!result.Fingerprint.IsValid())
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "fingerprint_failed",
                                                fingerprintError, &record->Descriptor, record->DescriptorPath,
                                                "fingerprint"));
        pop();
        return result;
    }

    record->Descriptor.State = AssetImportState::Transforming;
    record->Descriptor.Diagnostics = result.Diagnostics;
    type->SaveDescriptor(record->DescriptorPath, record->Descriptor, nullptr);
    m_database.AddOrUpdate(record->DescriptorPath, record->Descriptor, nullptr);
    const auto persistTransformFailure = [&] {
        record->Descriptor.State = AssetImportState::Error;
        record->Descriptor.Diagnostics = result.Diagnostics;
        type->SaveDescriptor(record->DescriptorPath, record->Descriptor, nullptr);
        m_database.AddOrUpdate(record->DescriptorPath, record->Descriptor, nullptr);
    };

    AssetTransformContext transformContext;
    transformContext.ProjectRoot = m_config.ProjectRoot;
    transformContext.DescriptorPath = record->DescriptorPath;
    transformContext.Platform = m_config.Platform;
    transformContext.Profile = m_config.Profile;
    transformContext.CacheRoot = m_config.CacheRoot;
    transformContext.Fingerprint = result.Fingerprint;
    transformContext.ResolvedSettings = ResolveSettings(record->Descriptor, *type);

    AssetTransformOutput output;
    output.RuntimeType = type->RuntimeType;
    output.ResourceVersion = type->ResourceVersion;
    std::string transformError;
    if (!type->Transform(record->Descriptor, transformContext, output, &transformError))
    {
        result.Diagnostics.insert(result.Diagnostics.end(), output.Diagnostics.begin(), output.Diagnostics.end());
        result.Diagnostics.push_back(
            Diagnostic(AssetDiagnosticSeverity::FatalError, "transform_failed",
                       transformError.empty() ? "Transformer returned failure" : transformError, &record->Descriptor,
                       record->DescriptorPath, "transform", "The previous cooked resource was preserved."));
        record->Descriptor.State = AssetImportState::Error;
        record->Descriptor.Diagnostics = result.Diagnostics;
        type->SaveDescriptor(record->DescriptorPath, record->Descriptor, nullptr);
        m_database.AddOrUpdate(record->DescriptorPath, record->Descriptor, nullptr);
        pop();
        return result;
    }
    result.Diagnostics.insert(result.Diagnostics.end(), output.Diagnostics.begin(), output.Diagnostics.end());
    if (output.RuntimeType.empty() || output.ResourceVersion == 0)
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "invalid_transform_output",
                                                "Transformer produced no runtime type or resource version",
                                                &record->Descriptor, record->DescriptorPath, "transform"));
        persistTransformFailure();
        pop();
        return result;
    }

    const std::vector<AssetGuid> originalDependencies = record->Descriptor.AssetDependencies;
    record->Descriptor.AssetDependencies.insert(record->Descriptor.AssetDependencies.end(),
                                                output.AssetDependencies.begin(), output.AssetDependencies.end());
    record->Descriptor.SourceDependencies.insert(record->Descriptor.SourceDependencies.end(),
                                                 output.SourceDependencies.begin(), output.SourceDependencies.end());
    std::sort(record->Descriptor.AssetDependencies.begin(), record->Descriptor.AssetDependencies.end());
    record->Descriptor.AssetDependencies.erase(
        std::unique(record->Descriptor.AssetDependencies.begin(), record->Descriptor.AssetDependencies.end()),
        record->Descriptor.AssetDependencies.end());
    std::sort(record->Descriptor.SourceDependencies.begin(), record->Descriptor.SourceDependencies.end());
    record->Descriptor.SourceDependencies.erase(
        std::unique(record->Descriptor.SourceDependencies.begin(), record->Descriptor.SourceDependencies.end()),
        record->Descriptor.SourceDependencies.end());
    if (std::find(record->Descriptor.AssetDependencies.begin(), record->Descriptor.AssetDependencies.end(), guid) !=
        record->Descriptor.AssetDependencies.end())
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "self_dependency",
                                                "Transformer produced a dependency on the asset itself",
                                                &record->Descriptor, record->DescriptorPath, "transform"));
        persistTransformFailure();
        pop();
        return result;
    }
    m_database.AddOrUpdate(record->DescriptorPath, record->Descriptor, nullptr);
    for (AssetGuid dependency : record->Descriptor.AssetDependencies)
    {
        if (std::find(originalDependencies.begin(), originalDependencies.end(), dependency) !=
            originalDependencies.end())
            continue;
        AssetTransformResult dependencyResult = TransformAssetInternal(dependency, force, stack);
        result.Diagnostics.insert(result.Diagnostics.end(), dependencyResult.Diagnostics.begin(),
                                  dependencyResult.Diagnostics.end());
        if (!dependencyResult.Succeeded)
        {
            result.Diagnostics.push_back(
                Diagnostic(AssetDiagnosticSeverity::FatalError, "generated_dependency_transform_failed",
                           "Transformer-discovered dependency failed: " + dependency.ToString(), &record->Descriptor,
                           record->DescriptorPath, "dependency transform"));
            persistTransformFailure();
            pop();
            return result;
        }
    }
    std::string finalFingerprintError;
    result.Fingerprint = ComputeAssetFingerprint(guid, &finalFingerprintError);
    if (!result.Fingerprint.IsValid())
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "final_fingerprint_failed",
                                                finalFingerprintError, &record->Descriptor, record->DescriptorPath,
                                                "fingerprint"));
        persistTransformFailure();
        pop();
        return result;
    }

    resources::ResourceCompression compression = resources::ResourceCompression::None;
    if (!resources::ParseResourceCompression(output.Compression, compression))
    {
        result.Diagnostics.push_back(
            Diagnostic(AssetDiagnosticSeverity::FatalError, "unsupported_resource_compression",
                       "Transformer requested unknown compression '" + output.Compression + "'", &record->Descriptor,
                       record->DescriptorPath, "write cooked resource"));
        persistTransformFailure();
        pop();
        return result;
    }
    resources::CookedResourceHeader header;
    header.Asset = guid;
    header.AssetType = record->Descriptor.Type;
    header.ResourceType = output.RuntimeType;
    header.ResourceVersion = output.ResourceVersion;
    header.Platform = m_config.Platform;
    header.Profile = m_config.Profile;
    header.Flags = output.Flags;
    header.TransformFingerprint = result.Fingerprint;
    const std::filesystem::path cookedPath = GetCookedResourcePath(guid);
    if (!resources::WriteCookedResource(cookedPath, header, output.Payload, compression, &transformError))
    {
        result.Diagnostics.push_back(
            Diagnostic(AssetDiagnosticSeverity::FatalError, "cooked_write_failed", transformError, &record->Descriptor,
                       cookedPath, "write cooked resource", "The previous cooked resource was preserved."));
        record->Descriptor.State = AssetImportState::Error;
        record->Descriptor.Diagnostics = result.Diagnostics;
        type->SaveDescriptor(record->DescriptorPath, record->Descriptor, nullptr);
        m_database.AddOrUpdate(record->DescriptorPath, record->Descriptor, nullptr);
        pop();
        return result;
    }

    record->Descriptor.SourceFingerprint = ComputeSourceFingerprint(record->Descriptor, nullptr);
    AssetDescriptor settingsDescriptor = record->Descriptor;
    settingsDescriptor.Settings = transformContext.ResolvedSettings;
    settingsDescriptor.PlatformOverrides.clear();
    settingsDescriptor.ImporterVersion = type->ImporterVersion;
    settingsDescriptor.TransformerVersion = type->TransformerVersion;
    record->Descriptor.SettingsFingerprint = HashString(SerializeAssetSettings(settingsDescriptor, m_config.Profile));
    record->Descriptor.DependencyFingerprint = ComputeDependencyFingerprint(record->Descriptor, nullptr);
    record->Descriptor.LastTransformFingerprint = result.Fingerprint;
    record->Descriptor.ImporterVersion = type->ImporterVersion;
    record->Descriptor.TransformerVersion = type->TransformerVersion;
    record->Descriptor.LastTransform.Platform = m_config.Platform;
    record->Descriptor.LastTransform.Profile = m_config.Profile;
    record->Descriptor.LastTransform.CookedResource = cookedPath.lexically_relative(m_config.CacheRoot);
    record->Descriptor.LastTransform.Fingerprint = result.Fingerprint;
    record->Descriptor.LastTransform.UnixTimestampSeconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    record->Descriptor.LastTransform.Succeeded = true;
    record->Descriptor.Diagnostics = result.Diagnostics;
    record->Descriptor.State = std::any_of(result.Diagnostics.begin(), result.Diagnostics.end(),
                                           [](const AssetDiagnostic &diagnostic) {
                                               return diagnostic.Severity == AssetDiagnosticSeverity::Warning;
                                           })
                                   ? AssetImportState::Warning
                                   : AssetImportState::Ready;
    if (!type->SaveDescriptor(record->DescriptorPath, record->Descriptor, &transformError) ||
        !m_database.AddOrUpdate(record->DescriptorPath, record->Descriptor, &transformError))
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "transform_metadata_save_failed",
                                                transformError, &record->Descriptor, record->DescriptorPath,
                                                "finalize transform"));
        pop();
        return result;
    }

    resources::RuntimeAssetEntry runtimeEntry;
    runtimeEntry.Guid = guid;
    runtimeEntry.AssetType = record->Descriptor.Type;
    runtimeEntry.ResourceType = output.RuntimeType;
    runtimeEntry.CookedPath = cookedPath.lexically_relative(m_config.CacheRoot);
    runtimeEntry.ResourceVersion = output.ResourceVersion;
    runtimeEntry.TransformFingerprint = result.Fingerprint;
    if (!m_resources.Registry().Register(std::move(runtimeEntry), &transformError))
    {
        result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "runtime_registry_update_failed",
                                                transformError, &record->Descriptor, cookedPath, "finalize transform"));
        pop();
        return result;
    }
    m_resources.NotifyResourceChanged(guid);
    result.GeneratedRuntimeFiles.push_back(cookedPath);
    result.Statistics = std::move(output.Statistics);
    result.Succeeded = true;
    pop();
    return result;
}

AssetTransformResult AssetPipeline::ReimportAsset(AssetGuid guid)
{
    m_database.MarkDirty(guid, true);
    return TransformAsset(guid, true);
}

std::vector<AssetTransformResult> AssetPipeline::TransformAll(bool force)
{
    std::vector<AssetTransformResult> results;
    for (const AssetBrowserEntry &entry : m_database.Query())
        results.push_back(TransformAsset(entry.Guid, force));
    return results;
}

std::vector<AssetDiagnostic> AssetPipeline::ValidateAsset(AssetGuid guid) const
{
    const auto record = m_database.Find(guid);
    if (!record)
        return {Diagnostic(AssetDiagnosticSeverity::FatalError, "asset_not_registered",
                           "Unknown asset GUID: " + guid.ToString(), nullptr, {}, "validation")};
    const AssetTypeRegistration *type = m_types.Find(record->Descriptor.Type);
    if (!type)
        return {Diagnostic(AssetDiagnosticSeverity::FatalError, "unknown_asset_type",
                           "Unknown asset type: " + record->Descriptor.Type, &record->Descriptor,
                           record->DescriptorPath, "validation")};
    std::vector<AssetDiagnostic> diagnostics;
    for (AssetGuid missing : m_database.FindMissingDependencies(guid))
        diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "missing_dependency",
                                         "Missing asset dependency: " + missing.ToString(), &record->Descriptor,
                                         record->DescriptorPath, "validation"));
    for (const auto &source : record->Descriptor.Sources)
        if (!std::filesystem::is_regular_file(AbsoluteProjectPath(source)))
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "missing_source",
                                             "Missing source file: " + source.generic_string(), &record->Descriptor,
                                             source, "validation"));
    if (type->Validate)
    {
        const AssetValidationContext context{m_config.ProjectRoot, record->DescriptorPath, m_config.Platform,
                                             m_config.Profile};
        auto typed = type->Validate(record->Descriptor, context);
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(typed.begin()),
                           std::make_move_iterator(typed.end()));
    }
    return diagnostics;
}

void AssetPipeline::RefreshDirtyStates()
{
    for (const AssetBrowserEntry &entry : m_database.Query())
        if (IsAssetDirty(entry.Guid, nullptr))
            m_database.MarkDirty(entry.Guid, true);
}

std::vector<AssetGuid> AssetPipeline::GetAssetDependencies(AssetGuid guid) const
{
    return m_database.GetDependencies(guid);
}

std::vector<AssetGuid> AssetPipeline::GetAssetDependents(AssetGuid guid) const
{
    return m_database.GetDependents(guid);
}

std::optional<AssetRecord> AssetPipeline::ResolveAssetReference(AssetGuid guid) const
{
    return m_database.Find(guid);
}

std::optional<AssetGuid> AssetPipeline::ResolveAssetGuid(const std::filesystem::path &descriptorPath) const
{
    return m_database.ResolveGuid(descriptorPath);
}

std::filesystem::path AssetPipeline::RuntimeRegistryPath() const
{
    return m_config.CacheRoot / m_config.Platform / m_config.Profile / "assets.slareg";
}

bool AssetPipeline::SaveRuntimeRegistry(std::string *error) const
{
    return m_resources.Registry().Save(RuntimeRegistryPath(), error);
}

} // namespace engine::assets
