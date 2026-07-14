#include "engine/asset/AssetWorkspace.h"
#include "engine/asset/BuiltinAssetTypes.h"

#include <iostream>

namespace
{

using namespace engine::assets;

void Usage()
{
    std::cout << "assetc [--project PATH] [--assets PATH] [--cache PATH] "
                 "[--platform NAME] [--profile NAME] COMMAND [ARGS]\n\n"
                 "Commands:\n"
                 "  scan                         scan and migrate descriptors\n"
                 "  import FILE...               import source files\n"
                 "  transform GUID|DESCRIPTOR    transform one asset\n"
                 "  force GUID|DESCRIPTOR        force-transform one asset\n"
                 "  transform-all                transform all dirty assets\n"
                 "  validate [GUID]              validate one or every asset\n"
                 "  list [SEARCH]                 list browser entries\n"
                 "  show GUID                     print descriptor summary\n"
                 "  deps GUID                     print dependencies/dependents\n";
}

void PrintDiagnostic(const AssetDiagnostic &diagnostic)
{
    std::cerr << ToString(diagnostic.Severity) << " [" << diagnostic.Code << "] " << diagnostic.Message;
    if (!diagnostic.Source.empty())
        std::cerr << " (" << diagnostic.Source.generic_string() << ')';
    if (!diagnostic.Suggestion.empty())
        std::cerr << "\n  suggestion: " << diagnostic.Suggestion;
    std::cerr << '\n';
}

std::optional<AssetGuid> Resolve(AssetPipeline &pipeline, std::string_view text)
{
    if (const auto guid = AssetGuid::Parse(text); guid && guid->IsValid())
        return guid;
    return pipeline.ResolveAssetGuid(std::filesystem::path(text));
}

int PrintTransform(const AssetTransformResult &result)
{
    for (const auto &diagnostic : result.Diagnostics)
        PrintDiagnostic(diagnostic);
    if (!result.Succeeded)
        return 2;
    std::cout << (result.SkippedAsUpToDate ? "up-to-date" : "transformed")
              << " fingerprint=" << result.Fingerprint.ToString() << '\n';
    for (const auto &path : result.GeneratedRuntimeFiles)
        std::cout << "  " << path.generic_string() << '\n';
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    using namespace engine;
    using namespace engine::assets;
    AssetPipelineConfig config;
    std::error_code filesystemError;
    config.ProjectRoot = std::filesystem::current_path(filesystemError);
    int argument = 1;
    while (argument < argc && std::string_view(argv[argument]).starts_with("--"))
    {
        const std::string option = argv[argument++];
        if (argument >= argc)
        {
            Usage();
            return 1;
        }
        const std::string value = argv[argument++];
        if (option == "--project")
            config.ProjectRoot = value;
        else if (option == "--assets")
            config.AssetRoot = value;
        else if (option == "--cache")
            config.CacheRoot = value;
        else if (option == "--platform")
            config.Platform = value;
        else if (option == "--profile")
            config.Profile = value;
        else
        {
            std::cerr << "Unknown option: " << option << '\n';
            return 1;
        }
    }
    if (argument >= argc)
    {
        Usage();
        return 1;
    }

    resources::ResourceManager resources;
    AssetTypeRegistry types;
    std::string error;
    if (!RegisterBuiltinAssetTypes(types, resources, {}, &error))
    {
        std::cerr << "Asset type registration failed: " << error << '\n';
        return 2;
    }
    AssetDatabase database(types);
    AssetPipeline pipeline(types, database, resources, config);
    const auto scan = database.Scan(pipeline.Config().AssetRoot);
    for (const auto &diagnostic : scan.Diagnostics)
        PrintDiagnostic(diagnostic);

    const std::string command = argv[argument++];
    if (command == "scan")
    {
        std::cout << scan.Assets << " assets, " << scan.Migrated << " migrated, " << scan.DuplicateGuids
                  << " duplicate GUIDs\n";
        return scan.Succeeded() ? 0 : 2;
    }
    if (command == "import")
    {
        if (argument >= argc)
        {
            Usage();
            return 1;
        }
        std::vector<std::filesystem::path> paths;
        while (argument < argc)
            paths.emplace_back(argv[argument++]);
        const ImportResult result = pipeline.CreateAssetsFromSources(paths);
        for (const auto &diagnostic : result.Diagnostics)
            PrintDiagnostic(diagnostic);
        for (const auto &asset : result.GeneratedAssets)
            std::cout << asset.Descriptor.Guid.ToString() << " " << asset.Descriptor.Type << " "
                      << asset.DescriptorPath.generic_string() << '\n';
        return result.Succeeded ? 0 : 2;
    }
    if (command == "transform" || command == "force")
    {
        if (argument >= argc)
        {
            Usage();
            return 1;
        }
        const auto guid = Resolve(pipeline, argv[argument]);
        if (!guid)
        {
            std::cerr << "Cannot resolve asset: " << argv[argument] << '\n';
            return 2;
        }
        const int code = PrintTransform(pipeline.TransformAsset(*guid, command == "force"));
        if (code == 0 && !pipeline.SaveRuntimeRegistry(&error))
        {
            std::cerr << "Registry save failed: " << error << '\n';
            return 2;
        }
        return code;
    }
    if (command == "transform-all")
    {
        int code = 0;
        for (const auto &result : pipeline.TransformAll())
            code = std::max(code, PrintTransform(result));
        if (code == 0 && !pipeline.SaveRuntimeRegistry(&error))
        {
            std::cerr << "Registry save failed: " << error << '\n';
            return 2;
        }
        return code;
    }
    if (command == "list")
    {
        AssetBrowserQuery query;
        if (argument < argc)
            query.Search = argv[argument];
        for (const auto &entry : database.Query(query))
            std::cout << entry.Guid.ToString() << "  " << entry.Type << "  " << ToString(entry.State) << "  "
                      << entry.Name << "\n";
        return 0;
    }
    if (command == "validate")
    {
        std::vector<AssetGuid> guids;
        if (argument < argc)
        {
            const auto guid = Resolve(pipeline, argv[argument]);
            if (!guid)
                return 2;
            guids.push_back(*guid);
        }
        else
            for (const auto &entry : database.Query())
                guids.push_back(entry.Guid);
        bool valid = true;
        for (AssetGuid guid : guids)
            for (const auto &diagnostic : pipeline.ValidateAsset(guid))
            {
                PrintDiagnostic(diagnostic);
                if (diagnostic.Severity == AssetDiagnosticSeverity::FatalError)
                    valid = false;
            }
        return valid ? 0 : 2;
    }
    if (command == "show" || command == "deps")
    {
        if (argument >= argc)
        {
            Usage();
            return 1;
        }
        const auto guid = Resolve(pipeline, argv[argument]);
        const auto record = guid ? database.Find(*guid) : std::nullopt;
        if (!record)
        {
            std::cerr << "Unknown asset\n";
            return 2;
        }
        std::cout << record->Descriptor.Name << " [" << record->Descriptor.Type << "]\n"
                  << record->Descriptor.Guid.ToString() << "\n"
                  << record->DescriptorPath.generic_string() << "\n";
        if (command == "show")
        {
            std::cout << "state=" << ToString(record->Descriptor.State)
                      << " sources=" << record->Descriptor.Sources.size()
                      << " settings=" << record->Descriptor.Settings.size() << '\n';
        }
        else
        {
            std::cout << "dependencies:\n";
            for (AssetGuid dependency : database.GetDependencies(*guid))
                std::cout << "  " << dependency.ToString() << '\n';
            std::cout << "dependents:\n";
            for (AssetGuid dependent : database.GetDependents(*guid))
                std::cout << "  " << dependent.ToString() << '\n';
        }
        return 0;
    }
    Usage();
    return 1;
}
