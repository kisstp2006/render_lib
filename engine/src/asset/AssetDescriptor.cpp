#include "engine/asset/AssetDescriptor.h"

#include "engine/asset/AssetFileSystem.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <unordered_map>

namespace engine::assets
{
namespace
{

std::string Escape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char character : value)
    {
        switch (character)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '=':
            escaped += "\\e";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

bool Unescape(std::string_view value, std::string &decoded, std::string *error)
{
    decoded.clear();
    decoded.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index)
    {
        const char character = value[index];
        if (character != '\\')
        {
            decoded += character;
            continue;
        }
        if (++index >= value.size())
        {
            if (error)
                *error = "Descriptor contains a trailing escape character";
            return false;
        }
        switch (value[index])
        {
        case '\\':
            decoded += '\\';
            break;
        case 'n':
            decoded += '\n';
            break;
        case 'r':
            decoded += '\r';
            break;
        case 'e':
            decoded += '=';
            break;
        default:
            if (error)
                *error = "Descriptor contains an unknown escape sequence";
            return false;
        }
    }
    return true;
}

void Put(std::ostringstream &stream, std::string_view key, std::string_view value)
{
    stream << key << '=' << Escape(value) << '\n';
}

template <typename Number> void PutNumber(std::ostringstream &stream, std::string_view key, Number value)
{
    stream << key << '=' << value << '\n';
}

bool ParseUnsigned(std::string_view text, uint64_t &value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

struct KeyValues
{
    std::unordered_map<std::string, std::string> Values;

    bool Parse(std::string_view text, std::string *error)
    {
        size_t lineNumber = 0;
        while (!text.empty())
        {
            ++lineNumber;
            const size_t end = text.find('\n');
            std::string_view line = text.substr(0, end);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
            if (line.empty() || line.starts_with('#'))
                continue;
            const size_t separator = line.find('=');
            if (separator == std::string_view::npos || separator == 0)
            {
                if (error)
                    *error = "Malformed asset descriptor line " + std::to_string(lineNumber);
                return false;
            }
            std::string value;
            if (!Unescape(line.substr(separator + 1), value, error))
            {
                if (error && !error->empty())
                    *error += " on line " + std::to_string(lineNumber);
                return false;
            }
            std::string key(line.substr(0, separator));
            if (!Values.emplace(std::move(key), std::move(value)).second)
            {
                if (error)
                    *error = "Duplicate descriptor key on line " + std::to_string(lineNumber);
                return false;
            }
        }
        return true;
    }

    bool Required(std::string_view key, std::string &value, std::string *error) const
    {
        const auto found = Values.find(std::string(key));
        if (found == Values.end())
        {
            if (error)
                *error = "Asset descriptor is missing required key '" + std::string(key) + "'";
            return false;
        }
        value = found->second;
        return true;
    }

    std::string Get(std::string_view key, std::string defaultValue = {}) const
    {
        const auto found = Values.find(std::string(key));
        return found == Values.end() ? std::move(defaultValue) : found->second;
    }

    bool Count(std::string_view key, size_t &count, std::string *error) const
    {
        const std::string value = Get(key, "0");
        uint64_t parsed = 0;
        if (!ParseUnsigned(value, parsed) || parsed > 1'000'000)
        {
            if (error)
                *error = "Invalid or excessive descriptor count in '" + std::string(key) + "'";
            return false;
        }
        count = static_cast<size_t>(parsed);
        return true;
    }
};

template <typename Container, typename Formatter>
void WriteArray(std::ostringstream &stream, std::string_view prefix, const Container &values, Formatter formatter)
{
    PutNumber(stream, std::string(prefix) + ".count", values.size());
    for (size_t index = 0; index < values.size(); ++index)
        Put(stream, std::string(prefix) + '.' + std::to_string(index), formatter(values[index]));
}

void WriteMap(std::ostringstream &stream, std::string_view prefix, const std::map<std::string, std::string> &values)
{
    PutNumber(stream, std::string(prefix) + ".count", values.size());
    size_t index = 0;
    for (const auto &[key, value] : values)
    {
        Put(stream, std::string(prefix) + '.' + std::to_string(index) + ".key", key);
        Put(stream, std::string(prefix) + '.' + std::to_string(index) + ".value", value);
        ++index;
    }
}

bool ReadMap(const KeyValues &values, std::string_view prefix, std::map<std::string, std::string> &output,
             std::string *error)
{
    size_t count = 0;
    if (!values.Count(std::string(prefix) + ".count", count, error))
        return false;
    output.clear();
    for (size_t index = 0; index < count; ++index)
    {
        const std::string item = std::string(prefix) + '.' + std::to_string(index);
        std::string key;
        std::string value;
        if (!values.Required(item + ".key", key, error) || !values.Required(item + ".value", value, error))
            return false;
        if (!output.emplace(std::move(key), std::move(value)).second)
        {
            if (error)
                *error = "Duplicate map key in descriptor section '" + std::string(prefix) + "'";
            return false;
        }
    }
    return true;
}

bool ReadVersion(const KeyValues &values, std::string_view key, uint32_t &version, std::string *error)
{
    std::string text;
    if (!values.Required(key, text, error))
        return false;
    uint64_t parsed = 0;
    if (!ParseUnsigned(text, parsed) || parsed == 0 || parsed > UINT32_MAX)
    {
        if (error)
            *error = "Invalid version in descriptor key '" + std::string(key) + "'";
        return false;
    }
    version = static_cast<uint32_t>(parsed);
    return true;
}

std::string BoolString(bool value)
{
    return value ? "true" : "false";
}

bool ParseBool(std::string_view text, bool &value)
{
    if (text == "true" || text == "1")
    {
        value = true;
        return true;
    }
    if (text == "false" || text == "0")
    {
        value = false;
        return true;
    }
    return false;
}

} // namespace

std::map<std::string, std::string> AssetDescriptor::ResolveSettings(std::string_view profile) const
{
    std::map<std::string, std::string> resolved = Settings;
    if (!profile.empty())
    {
        for (const AssetPlatformOverride &overrideSettings : PlatformOverrides)
        {
            if (overrideSettings.Profile != profile)
                continue;
            for (const auto &[key, value] : overrideSettings.Settings)
                resolved.insert_or_assign(key, value);
        }
    }
    return resolved;
}

std::optional<std::string> AssetDescriptor::GetSetting(std::string_view key, std::string_view profile) const
{
    const auto resolved = ResolveSettings(profile);
    const auto found = resolved.find(std::string(key));
    if (found == resolved.end())
        return std::nullopt;
    return found->second;
}

std::string ToString(AssetImportState state)
{
    switch (state)
    {
    case AssetImportState::Unknown:
        return "unknown";
    case AssetImportState::Dirty:
        return "dirty";
    case AssetImportState::Transforming:
        return "transforming";
    case AssetImportState::Ready:
        return "ready";
    case AssetImportState::Warning:
        return "warning";
    case AssetImportState::Error:
        return "error";
    }
    return "unknown";
}

std::string ToString(AssetDiagnosticSeverity severity)
{
    switch (severity)
    {
    case AssetDiagnosticSeverity::Info:
        return "info";
    case AssetDiagnosticSeverity::Warning:
        return "warning";
    case AssetDiagnosticSeverity::RecoverableError:
        return "recoverable_error";
    case AssetDiagnosticSeverity::FatalError:
        return "fatal_error";
    }
    return "info";
}

bool ParseAssetImportState(std::string_view text, AssetImportState &state)
{
    if (text == "unknown")
        state = AssetImportState::Unknown;
    else if (text == "dirty")
        state = AssetImportState::Dirty;
    else if (text == "transforming")
        state = AssetImportState::Transforming;
    else if (text == "ready")
        state = AssetImportState::Ready;
    else if (text == "warning")
        state = AssetImportState::Warning;
    else if (text == "error")
        state = AssetImportState::Error;
    else
        return false;
    return true;
}

bool ParseAssetDiagnosticSeverity(std::string_view text, AssetDiagnosticSeverity &severity)
{
    if (text == "info")
        severity = AssetDiagnosticSeverity::Info;
    else if (text == "warning")
        severity = AssetDiagnosticSeverity::Warning;
    else if (text == "recoverable_error")
        severity = AssetDiagnosticSeverity::RecoverableError;
    else if (text == "fatal_error")
        severity = AssetDiagnosticSeverity::FatalError;
    else
        return false;
    return true;
}

bool SaveAssetDescriptor(const std::filesystem::path &path, const AssetDescriptor &descriptor, std::string *error)
{
    if (!descriptor.Guid.IsValid() || descriptor.Type.empty() || descriptor.DescriptorVersion == 0)
    {
        if (error)
            *error = "Cannot save an asset descriptor without a valid GUID, type and version";
        return false;
    }

    std::ostringstream stream;
    Put(stream, "format", "SLA_ASSET");
    PutNumber(stream, "envelope_version", kAssetDescriptorEnvelopeVersion);
    Put(stream, "guid", descriptor.Guid.ToString());
    Put(stream, "type", descriptor.Type);
    PutNumber(stream, "descriptor_version", descriptor.DescriptorVersion);
    PutNumber(stream, "importer_version", descriptor.ImporterVersion);
    PutNumber(stream, "transformer_version", descriptor.TransformerVersion);
    Put(stream, "name", descriptor.Name);
    Put(stream, "state", ToString(descriptor.State));
    Put(stream, "generated", BoolString(descriptor.Generated));
    Put(stream, "generated_by", descriptor.GeneratedBy.IsValid() ? descriptor.GeneratedBy.ToString() : "");
    Put(stream, "user_modified", BoolString(descriptor.UserModified));

    WriteArray(stream, "sources", descriptor.Sources, [](const auto &pathValue) { return pathValue.generic_string(); });
    WriteMap(stream, "settings", descriptor.Settings);

    std::vector<AssetGuid> assetDependencies = descriptor.AssetDependencies;
    std::sort(assetDependencies.begin(), assetDependencies.end());
    assetDependencies.erase(std::unique(assetDependencies.begin(), assetDependencies.end()), assetDependencies.end());
    WriteArray(stream, "asset_dependencies", assetDependencies, [](const AssetGuid &guid) { return guid.ToString(); });

    std::vector<std::filesystem::path> sourceDependencies = descriptor.SourceDependencies;
    std::sort(sourceDependencies.begin(), sourceDependencies.end());
    sourceDependencies.erase(std::unique(sourceDependencies.begin(), sourceDependencies.end()),
                             sourceDependencies.end());
    WriteArray(stream, "source_dependencies", sourceDependencies,
               [](const auto &pathValue) { return pathValue.generic_string(); });

    PutNumber(stream, "platform_overrides.count", descriptor.PlatformOverrides.size());
    for (size_t index = 0; index < descriptor.PlatformOverrides.size(); ++index)
    {
        const std::string prefix = "platform_overrides." + std::to_string(index);
        Put(stream, prefix + ".profile", descriptor.PlatformOverrides[index].Profile);
        WriteMap(stream, prefix + ".settings", descriptor.PlatformOverrides[index].Settings);
    }

    std::vector<std::string> tags = descriptor.Tags;
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    WriteArray(stream, "tags", tags, [](const std::string &value) { return value; });
    WriteMap(stream, "user_metadata", descriptor.UserMetadata);

    Put(stream, "fingerprint.source", descriptor.SourceFingerprint.ToString());
    Put(stream, "fingerprint.settings", descriptor.SettingsFingerprint.ToString());
    Put(stream, "fingerprint.dependencies", descriptor.DependencyFingerprint.ToString());
    Put(stream, "fingerprint.last_transform", descriptor.LastTransformFingerprint.ToString());

    Put(stream, "last_transform.platform", descriptor.LastTransform.Platform);
    Put(stream, "last_transform.profile", descriptor.LastTransform.Profile);
    Put(stream, "last_transform.cooked_resource", descriptor.LastTransform.CookedResource.generic_string());
    Put(stream, "last_transform.fingerprint", descriptor.LastTransform.Fingerprint.ToString());
    PutNumber(stream, "last_transform.timestamp", descriptor.LastTransform.UnixTimestampSeconds);
    Put(stream, "last_transform.succeeded", BoolString(descriptor.LastTransform.Succeeded));

    PutNumber(stream, "diagnostics.count", descriptor.Diagnostics.size());
    for (size_t index = 0; index < descriptor.Diagnostics.size(); ++index)
    {
        const AssetDiagnostic &diagnostic = descriptor.Diagnostics[index];
        const std::string prefix = "diagnostics." + std::to_string(index);
        Put(stream, prefix + ".severity", ToString(diagnostic.Severity));
        Put(stream, prefix + ".code", diagnostic.Code);
        Put(stream, prefix + ".message", diagnostic.Message);
        Put(stream, prefix + ".suggestion", diagnostic.Suggestion);
        Put(stream, prefix + ".source", diagnostic.Source.generic_string());
        Put(stream, prefix + ".step", diagnostic.Step);
    }
    return WriteTextFileAtomic(path, stream.str(), error);
}

bool LoadAssetDescriptor(const std::filesystem::path &path, AssetDescriptor &descriptor, std::string *error)
{
    std::string text;
    if (!ReadTextFile(path, text, error))
        return false;
    KeyValues values;
    if (!values.Parse(text, error))
        return false;
    if (values.Get("format") != "SLA_ASSET")
    {
        if (error)
            *error = "File is not an SLA asset descriptor: " + path.generic_string();
        return false;
    }
    uint32_t envelopeVersion = 0;
    if (!ReadVersion(values, "envelope_version", envelopeVersion, error) ||
        envelopeVersion != kAssetDescriptorEnvelopeVersion)
    {
        if (error && envelopeVersion != kAssetDescriptorEnvelopeVersion)
            *error = "Unsupported asset descriptor envelope version " + std::to_string(envelopeVersion);
        return false;
    }

    AssetDescriptor loaded;
    std::string guidText;
    if (!values.Required("guid", guidText, error))
        return false;
    const auto guid = AssetGuid::Parse(guidText);
    if (!guid)
    {
        if (error)
            *error = "Asset descriptor has an invalid GUID";
        return false;
    }
    loaded.Guid = *guid;
    if (!values.Required("type", loaded.Type, error) || loaded.Type.empty() ||
        !ReadVersion(values, "descriptor_version", loaded.DescriptorVersion, error) ||
        !ReadVersion(values, "importer_version", loaded.ImporterVersion, error) ||
        !ReadVersion(values, "transformer_version", loaded.TransformerVersion, error))
        return false;
    loaded.Name = values.Get("name");
    if (!ParseAssetImportState(values.Get("state", "unknown"), loaded.State))
    {
        if (error)
            *error = "Asset descriptor contains an invalid import state";
        return false;
    }
    if (!ParseBool(values.Get("generated", "false"), loaded.Generated) ||
        !ParseBool(values.Get("user_modified", "false"), loaded.UserModified))
    {
        if (error)
            *error = "Asset descriptor contains an invalid boolean";
        return false;
    }
    const std::string generatedBy = values.Get("generated_by");
    if (!generatedBy.empty())
    {
        const auto parsed = AssetGuid::Parse(generatedBy);
        if (!parsed)
        {
            if (error)
                *error = "Asset descriptor contains an invalid generator GUID";
            return false;
        }
        loaded.GeneratedBy = *parsed;
    }

    size_t count = 0;
    if (!values.Count("sources.count", count, error))
        return false;
    for (size_t index = 0; index < count; ++index)
        loaded.Sources.emplace_back(values.Get("sources." + std::to_string(index)));
    if (!ReadMap(values, "settings", loaded.Settings, error))
        return false;

    if (!values.Count("asset_dependencies.count", count, error))
        return false;
    for (size_t index = 0; index < count; ++index)
    {
        const auto parsed = AssetGuid::Parse(values.Get("asset_dependencies." + std::to_string(index)));
        if (!parsed)
        {
            if (error)
                *error = "Asset descriptor contains an invalid dependency GUID";
            return false;
        }
        loaded.AssetDependencies.push_back(*parsed);
    }
    if (!values.Count("source_dependencies.count", count, error))
        return false;
    for (size_t index = 0; index < count; ++index)
        loaded.SourceDependencies.emplace_back(values.Get("source_dependencies." + std::to_string(index)));

    if (!values.Count("platform_overrides.count", count, error))
        return false;
    for (size_t index = 0; index < count; ++index)
    {
        AssetPlatformOverride item;
        const std::string prefix = "platform_overrides." + std::to_string(index);
        if (!values.Required(prefix + ".profile", item.Profile, error) || item.Profile.empty() ||
            !ReadMap(values, prefix + ".settings", item.Settings, error))
            return false;
        loaded.PlatformOverrides.push_back(std::move(item));
    }

    if (!values.Count("tags.count", count, error))
        return false;
    for (size_t index = 0; index < count; ++index)
        loaded.Tags.push_back(values.Get("tags." + std::to_string(index)));
    if (!ReadMap(values, "user_metadata", loaded.UserMetadata, error))
        return false;

    auto parseFingerprint = [&](std::string_view key, AssetFingerprint &target) {
        const std::string value = values.Get(key);
        if (value.empty())
        {
            target = {};
            return true;
        }
        if (AssetFingerprint::Parse(value, target))
            return true;
        if (error)
            *error = "Invalid fingerprint in descriptor key '" + std::string(key) + "'";
        return false;
    };
    if (!parseFingerprint("fingerprint.source", loaded.SourceFingerprint) ||
        !parseFingerprint("fingerprint.settings", loaded.SettingsFingerprint) ||
        !parseFingerprint("fingerprint.dependencies", loaded.DependencyFingerprint) ||
        !parseFingerprint("fingerprint.last_transform", loaded.LastTransformFingerprint))
        return false;

    loaded.LastTransform.Platform = values.Get("last_transform.platform");
    loaded.LastTransform.Profile = values.Get("last_transform.profile");
    loaded.LastTransform.CookedResource = values.Get("last_transform.cooked_resource");
    if (!parseFingerprint("last_transform.fingerprint", loaded.LastTransform.Fingerprint))
        return false;
    uint64_t timestamp = 0;
    if (!ParseUnsigned(values.Get("last_transform.timestamp", "0"), timestamp) ||
        !ParseBool(values.Get("last_transform.succeeded", "false"), loaded.LastTransform.Succeeded))
    {
        if (error)
            *error = "Invalid last transform record";
        return false;
    }
    loaded.LastTransform.UnixTimestampSeconds = timestamp;

    if (!values.Count("diagnostics.count", count, error))
        return false;
    for (size_t index = 0; index < count; ++index)
    {
        AssetDiagnostic diagnostic;
        const std::string prefix = "diagnostics." + std::to_string(index);
        if (!ParseAssetDiagnosticSeverity(values.Get(prefix + ".severity", "info"), diagnostic.Severity))
        {
            if (error)
                *error = "Asset descriptor contains an invalid diagnostic severity";
            return false;
        }
        diagnostic.Code = values.Get(prefix + ".code");
        diagnostic.Message = values.Get(prefix + ".message");
        diagnostic.Suggestion = values.Get(prefix + ".suggestion");
        diagnostic.Source = values.Get(prefix + ".source");
        diagnostic.Step = values.Get(prefix + ".step");
        loaded.Diagnostics.push_back(std::move(diagnostic));
    }
    descriptor = std::move(loaded);
    return true;
}

std::string SerializeAssetSettings(const AssetDescriptor &descriptor, std::string_view profile)
{
    std::ostringstream stream;
    Put(stream, "type", descriptor.Type);
    PutNumber(stream, "descriptor_version", descriptor.DescriptorVersion);
    PutNumber(stream, "importer_version", descriptor.ImporterVersion);
    PutNumber(stream, "transformer_version", descriptor.TransformerVersion);
    Put(stream, "profile", profile);
    WriteMap(stream, "settings", descriptor.ResolveSettings(profile));
    return stream.str();
}

} // namespace engine::assets
