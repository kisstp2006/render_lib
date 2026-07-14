#include "engine/render/GpuCapabilities.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine
{
namespace
{

std::string Lower(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool Contains(std::string_view text, std::string_view needle)
{
    return Lower(text).find(Lower(needle)) != std::string::npos;
}

std::string Number(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(value == static_cast<float>(static_cast<int>(value)) ? 0 : 1)
           << value;
    return stream.str();
}

std::string Json(std::string_view value)
{
    std::string output = "\"";
    for (const char c : value)
    {
        switch (c)
        {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output += c; break;
        }
    }
    output += '"';
    return output;
}

void AddFeature(GpuCapabilityProfile& profile, GpuFeature feature, bool supported,
                bool enabled, std::string fallback = {})
{
    profile.Features.push_back({feature, supported, supported && enabled, std::move(fallback)});
}

void AddFallback(GpuCapabilityProfile& profile, std::string rule, std::string feature,
                 std::string requested, std::string selected, std::string reason)
{
    profile.Fallbacks.push_back({std::move(rule), std::move(feature), std::move(requested),
                                 std::move(selected), std::move(reason)});
}

struct DriverRule
{
    const char* Id;
    GpuApi Api;
    GpuVendor Vendor;
    const char* IdentityContains;
    uint64_t MinimumDriverVersion;
    uint64_t MaximumDriverVersion;
    bool SoftwareSafeMode;
};

// Deliberately conservative built-in database. It contains only rules that
// are safe across versions; vendor/version-specific workarounds should be
// added with a reproducible test case and a bounded driver version interval.
constexpr std::array<DriverRule, 5> kDriverRules{{
    {"mesa-llvmpipe-safe-mode", GpuApi::Unknown, GpuVendor::Unknown, "llvmpipe", 0, 0, true},
    {"mesa-lavapipe-safe-mode", GpuApi::Unknown, GpuVendor::Unknown, "lavapipe", 0, 0, true},
    {"google-swiftshader-safe-mode", GpuApi::Unknown, GpuVendor::Unknown, "swiftshader", 0, 0, true},
    {"windows-basic-render-safe-mode", GpuApi::Unknown, GpuVendor::Unknown, "microsoft basic render", 0, 0, true},
    {"software-pipe-safe-mode", GpuApi::Unknown, GpuVendor::Unknown, "softpipe", 0, 0, true},
}};

bool Matches(const DriverRule& rule, const GpuDeviceInfo& device)
{
    if (rule.Api != GpuApi::Unknown && rule.Api != device.Api)
        return false;
    if (rule.Vendor != GpuVendor::Unknown && rule.Vendor != device.Vendor)
        return false;
    if (rule.MinimumDriverVersion != 0 && device.DriverVersion < rule.MinimumDriverVersion)
        return false;
    if (rule.MaximumDriverVersion != 0 && device.DriverVersion > rule.MaximumDriverVersion)
        return false;
    const std::string identity = device.DeviceName + " " + device.VendorName + " " +
                                 device.DriverName + " " + device.DriverInfo;
    return Contains(identity, rule.IdentityContains);
}

} // namespace

const GpuFeatureState* GpuCapabilityProfile::Find(GpuFeature feature) const
{
    const auto found = std::find_if(Features.begin(), Features.end(),
                                    [feature](const GpuFeatureState& state)
                                    { return state.Feature == feature; });
    return found == Features.end() ? nullptr : &*found;
}

bool GpuCapabilityProfile::Supports(GpuFeature feature) const
{
    const GpuFeatureState* state = Find(feature);
    return state && state->Supported;
}

bool GpuCapabilityProfile::Uses(GpuFeature feature) const
{
    const GpuFeatureState* state = Find(feature);
    return state && state->Enabled;
}

GpuVendor IdentifyGpuVendor(uint32_t pciVendorId, std::string_view vendor,
                            std::string_view renderer)
{
    switch (pciVendorId)
    {
    case 0x10de: return GpuVendor::Nvidia;
    case 0x1002:
    case 0x1022: return GpuVendor::AMD;
    case 0x8086: return GpuVendor::Intel;
    case 0x106b: return GpuVendor::Apple;
    case 0x5143: return GpuVendor::Qualcomm;
    case 0x13b5: return GpuVendor::Arm;
    case 0x1010: return GpuVendor::Imagination;
    case 0x1414: return GpuVendor::Microsoft;
    default: break;
    }
    const std::string names = std::string(vendor) + " " + std::string(renderer);
    if (Contains(names, "nvidia")) return GpuVendor::Nvidia;
    if (Contains(names, "amd") || Contains(names, "ati") || Contains(names, "radeon")) return GpuVendor::AMD;
    if (Contains(names, "intel")) return GpuVendor::Intel;
    if (Contains(names, "apple")) return GpuVendor::Apple;
    if (Contains(names, "qualcomm") || Contains(names, "adreno")) return GpuVendor::Qualcomm;
    if (Contains(names, "arm") || Contains(names, "mali")) return GpuVendor::Arm;
    if (Contains(names, "imagination") || Contains(names, "powervr")) return GpuVendor::Imagination;
    if (Contains(names, "microsoft")) return GpuVendor::Microsoft;
    if (Contains(names, "mesa") || Contains(names, "llvmpipe") || Contains(names, "lavapipe")) return GpuVendor::Mesa;
    return GpuVendor::Unknown;
}

const char* GpuApiName(GpuApi api)
{
    switch (api) { case GpuApi::OpenGL: return "OpenGL"; case GpuApi::Vulkan: return "Vulkan"; default: return "Unknown"; }
}

const char* GpuVendorName(GpuVendor vendor)
{
    switch (vendor)
    {
    case GpuVendor::Nvidia: return "NVIDIA";
    case GpuVendor::AMD: return "AMD";
    case GpuVendor::Intel: return "Intel";
    case GpuVendor::Apple: return "Apple";
    case GpuVendor::Qualcomm: return "Qualcomm";
    case GpuVendor::Arm: return "Arm";
    case GpuVendor::Imagination: return "Imagination";
    case GpuVendor::Microsoft: return "Microsoft";
    case GpuVendor::Mesa: return "Mesa";
    default: return "Unknown";
    }
}

const char* GpuFeatureName(GpuFeature feature)
{
    switch (feature)
    {
    case GpuFeature::Msaa: return "MSAA";
    case GpuFeature::AnisotropicFiltering: return "anisotropic_filtering";
    case GpuFeature::TextureCompressionBc: return "texture_compression_bc";
    case GpuFeature::TextureCompressionBc7: return "texture_compression_bc7";
    case GpuFeature::TextureCompressionAstc: return "texture_compression_astc";
    case GpuFeature::GpuTimestamps: return "gpu_timestamps";
    case GpuFeature::PipelineStatistics: return "pipeline_statistics";
    case GpuFeature::MemoryBudget: return "memory_budget";
    case GpuFeature::ImmediatePresent: return "immediate_present";
    case GpuFeature::AdaptivePresent: return "adaptive_present";
    case GpuFeature::DynamicRendering: return "dynamic_rendering";
    case GpuFeature::Synchronization2: return "synchronization2";
    }
    return "unknown";
}

const char* GpuFeatureTierName(GpuFeatureTier tier)
{
    switch (tier) { case GpuFeatureTier::Advanced: return "advanced"; case GpuFeatureTier::Standard: return "standard"; default: return "compatibility"; }
}

const char* GpuCapabilityPolicyName(GpuCapabilityPolicy policy)
{
    return policy == GpuCapabilityPolicy::Conservative ? "conservative" : "default";
}

GpuCapabilityProfile EvaluateGpuCapabilities(const GpuRawCapabilities& raw,
                                             const GpuCapabilityRequest& request)
{
    GpuCapabilityProfile profile;
    profile.Device = raw.Device;
    if (request.EnableDriverWorkarounds && profile.Device.SoftwareRenderer)
        profile.AppliedRules.push_back("api-reported-software-device");
    if (request.EnableDriverWorkarounds)
    {
        for (const DriverRule& rule : kDriverRules)
        {
            if (!Matches(rule, profile.Device))
                continue;
            profile.AppliedRules.push_back(rule.Id);
            profile.Device.SoftwareRenderer = profile.Device.SoftwareRenderer || rule.SoftwareSafeMode;
        }
    }
    profile.Policy = request.Policy;

    uint32_t maximumMsaa = std::max(raw.MaxMsaaSamples, 1u);
    float maximumAnisotropy = std::max(raw.MaxAnisotropy, 1.0f);
    bool gpuTimingAllowed = raw.GpuTimestamps;

    if (request.EnableDriverWorkarounds && profile.Device.SoftwareRenderer)
    {
        maximumMsaa = 1;
        maximumAnisotropy = 1.0f;
        gpuTimingAllowed = false;
        profile.AppliedRules.push_back("software-renderer-safe-mode");
    }
    if (request.Policy == GpuCapabilityPolicy::Conservative)
    {
        maximumMsaa = std::min(maximumMsaa, 2u);
        maximumAnisotropy = std::min(maximumAnisotropy, 4.0f);
        profile.AppliedRules.push_back("user-conservative-policy");
    }

    profile.SelectedMsaaSamples = 1;
    const uint32_t requestedMsaa = std::max(request.MsaaSamples, 1u);
    for (const uint32_t sampleCount : {2u, 4u, 8u, 16u, 32u, 64u})
        if (sampleCount <= requestedMsaa && sampleCount <= maximumMsaa)
            profile.SelectedMsaaSamples = sampleCount;
    if (profile.SelectedMsaaSamples != requestedMsaa)
        AddFallback(profile, profile.Device.SoftwareRenderer ? "software-renderer-safe-mode" : "hardware-limit",
                    "MSAA", std::to_string(requestedMsaa) + "x",
                    std::to_string(profile.SelectedMsaaSamples) + "x",
                    "requested sample count is unavailable or disabled by the active safety policy");

    profile.SelectedAnisotropy = std::clamp(request.MaxAnisotropy, 1.0f, maximumAnisotropy);
    if (profile.SelectedAnisotropy != request.MaxAnisotropy)
        AddFallback(profile, profile.Device.SoftwareRenderer ? "software-renderer-safe-mode" : "hardware-limit",
                    "anisotropic_filtering", Number(request.MaxAnisotropy) + "x",
                    Number(profile.SelectedAnisotropy) + "x",
                    "requested anisotropy is unavailable or disabled by the active safety policy");

    const bool timingEnabled = request.EnableGpuTiming && gpuTimingAllowed;
    if (request.EnableGpuTiming && !timingEnabled)
        AddFallback(profile, profile.Device.SoftwareRenderer ? "software-renderer-safe-mode" : "unsupported-feature",
                    "gpu_timestamps", "enabled", "disabled",
                    "reliable GPU timestamp queries are unavailable");
    if (request.RequestImmediatePresent && !raw.ImmediatePresent)
        AddFallback(profile, "unsupported-present-mode", "present_mode", "immediate", "vsync",
                    "immediate presentation is unavailable");
    if (request.RequestAdaptivePresent && !raw.AdaptivePresent)
        AddFallback(profile, "unsupported-present-mode", "present_mode", "adaptive", "vsync",
                    "adaptive presentation is unavailable");
    AddFeature(profile, GpuFeature::Msaa, maximumMsaa > 1, profile.SelectedMsaaSamples > 1,
               "1x rendering");
    AddFeature(profile, GpuFeature::AnisotropicFiltering, raw.MaxAnisotropy > 1.0f,
               profile.SelectedAnisotropy > 1.0f, "trilinear filtering");
    AddFeature(profile, GpuFeature::TextureCompressionBc, raw.TextureCompressionBc,
               raw.TextureCompressionBc, "RGBA8 cooked fallback mip chain");
    AddFeature(profile, GpuFeature::TextureCompressionBc7, raw.TextureCompressionBc7,
               raw.TextureCompressionBc7, "RGBA8 cooked fallback mip chain");
    AddFeature(profile, GpuFeature::TextureCompressionAstc, raw.TextureCompressionAstc,
               raw.TextureCompressionAstc, "RGBA8 cooked fallback mip chain");
    AddFeature(profile, GpuFeature::GpuTimestamps, raw.GpuTimestamps, timingEnabled,
               "CPU frame timing");
    AddFeature(profile, GpuFeature::PipelineStatistics, raw.PipelineStatistics,
               raw.PipelineStatistics, "engine draw counters");
    AddFeature(profile, GpuFeature::MemoryBudget, raw.MemoryBudget, raw.MemoryBudget,
               "engine-owned allocation estimates");
    AddFeature(profile, GpuFeature::ImmediatePresent, raw.ImmediatePresent,
               request.RequestImmediatePresent && raw.ImmediatePresent, "FIFO vsync");
    AddFeature(profile, GpuFeature::AdaptivePresent, raw.AdaptivePresent,
               request.RequestAdaptivePresent && raw.AdaptivePresent, "FIFO vsync");
    AddFeature(profile, GpuFeature::DynamicRendering, raw.DynamicRendering,
               raw.DynamicRendering, "classic render passes");
    AddFeature(profile, GpuFeature::Synchronization2, raw.Synchronization2,
               raw.Synchronization2, "legacy barriers");

    if (profile.Device.SoftwareRenderer)
        profile.Tier = GpuFeatureTier::Compatibility;
    else if (raw.GpuTimestamps && raw.PipelineStatistics && raw.MemoryBudget &&
             (raw.TextureCompressionBc7 || raw.TextureCompressionAstc) && raw.MaxMsaaSamples >= 4)
        profile.Tier = GpuFeatureTier::Advanced;
    else
        profile.Tier = GpuFeatureTier::Standard;
    return profile;
}

bool WriteGpuCapabilityReport(const std::filesystem::path& path,
                              const GpuCapabilityProfile& profile, std::string* error)
{
    std::error_code directoryError;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError)
    {
        if (error) *error = "Cannot create capability report directory: " + directoryError.message();
        return false;
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file)
    {
        if (error) *error = "Cannot open GPU capability report: " + path.string();
        return false;
    }
    const GpuDeviceInfo& device = profile.Device;
    file << "{\n"
         << "  \"schema\": 1,\n"
         << "  \"api\": " << Json(GpuApiName(device.Api)) << ",\n"
         << "  \"api_version\": " << Json(device.ApiVersion) << ",\n"
         << "  \"vendor\": " << Json(GpuVendorName(device.Vendor)) << ",\n"
         << "  \"vendor_id\": " << device.VendorId << ",\n"
         << "  \"device_id\": " << device.DeviceId << ",\n"
         << "  \"device\": " << Json(device.DeviceName) << ",\n"
         << "  \"driver_name\": " << Json(device.DriverName) << ",\n"
         << "  \"driver_info\": " << Json(device.DriverInfo) << ",\n"
         << "  \"driver_id\": " << device.DriverId << ",\n"
         << "  \"driver_version\": " << device.DriverVersion << ",\n"
         << "  \"conformance_version\": " << Json(device.ConformanceVersion) << ",\n"
         << "  \"software_renderer\": " << (device.SoftwareRenderer ? "true" : "false") << ",\n"
         << "  \"tier\": " << Json(GpuFeatureTierName(profile.Tier)) << ",\n"
         << "  \"policy\": " << Json(GpuCapabilityPolicyName(profile.Policy)) << ",\n"
         << "  \"selected_msaa\": " << profile.SelectedMsaaSamples << ",\n"
         << "  \"selected_anisotropy\": " << profile.SelectedAnisotropy << ",\n"
         << "  \"features\": [\n";
    for (size_t index = 0; index < profile.Features.size(); ++index)
    {
        const GpuFeatureState& feature = profile.Features[index];
        file << "    {\"name\": " << Json(GpuFeatureName(feature.Feature))
             << ", \"supported\": " << (feature.Supported ? "true" : "false")
             << ", \"enabled\": " << (feature.Enabled ? "true" : "false")
             << ", \"fallback\": " << Json(feature.Fallback) << "}"
             << (index + 1 == profile.Features.size() ? "\n" : ",\n");
    }
    file << "  ],\n  \"applied_rules\": [";
    for (size_t index = 0; index < profile.AppliedRules.size(); ++index)
        file << (index ? ", " : "") << Json(profile.AppliedRules[index]);
    file << "],\n  \"fallbacks\": [\n";
    for (size_t index = 0; index < profile.Fallbacks.size(); ++index)
    {
        const GpuFallbackDecision& fallback = profile.Fallbacks[index];
        file << "    {\"rule\": " << Json(fallback.RuleId)
             << ", \"feature\": " << Json(fallback.Feature)
             << ", \"requested\": " << Json(fallback.Requested)
             << ", \"selected\": " << Json(fallback.Selected)
             << ", \"reason\": " << Json(fallback.Reason) << "}"
             << (index + 1 == profile.Fallbacks.size() ? "\n" : ",\n");
    }
    file << "  ]\n}\n";
    if (!file)
    {
        if (error) *error = "Failed while writing GPU capability report: " + path.string();
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace engine
