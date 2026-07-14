#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

enum class GpuApi : uint8_t
{
    Unknown,
    OpenGL,
    Vulkan
};

enum class GpuVendor : uint8_t
{
    Unknown,
    Nvidia,
    AMD,
    Intel,
    Apple,
    Qualcomm,
    Arm,
    Imagination,
    Microsoft,
    Mesa
};

enum class GpuCapabilityPolicy : uint8_t
{
    Default,
    Conservative
};

enum class GpuFeatureTier : uint8_t
{
    Compatibility,
    Standard,
    Advanced
};

enum class GpuFeature : uint8_t
{
    Msaa,
    AnisotropicFiltering,
    TextureCompressionBc,
    TextureCompressionBc7,
    TextureCompressionAstc,
    GpuTimestamps,
    PipelineStatistics,
    MemoryBudget,
    ImmediatePresent,
    AdaptivePresent,
    DynamicRendering,
    Synchronization2
};

struct GpuDeviceInfo
{
    GpuApi Api = GpuApi::Unknown;
    GpuVendor Vendor = GpuVendor::Unknown;
    uint32_t VendorId = 0;
    uint32_t DeviceId = 0;
    uint32_t DriverId = 0;
    uint64_t DriverVersion = 0;
    std::string DeviceName = "Unknown";
    std::string VendorName = "Unknown";
    std::string ApiVersion;
    std::string DriverName;
    std::string DriverInfo;
    std::string ConformanceVersion;
    bool SoftwareRenderer = false;
};

struct GpuFeatureState
{
    GpuFeature Feature = GpuFeature::Msaa;
    bool Supported = false;
    bool Enabled = false;
    std::string Fallback;
};

struct GpuFallbackDecision
{
    std::string RuleId;
    std::string Feature;
    std::string Requested;
    std::string Selected;
    std::string Reason;
};

// API-specific discovery is converted to this stable, testable input. No GL
// or Vulkan type escapes into the shared policy/database implementation.
struct GpuRawCapabilities
{
    GpuDeviceInfo Device;
    uint32_t MaxMsaaSamples = 1;
    float MaxAnisotropy = 1.0f;
    bool TextureCompressionBc = false;
    bool TextureCompressionBc7 = false;
    bool TextureCompressionAstc = false;
    bool GpuTimestamps = false;
    bool PipelineStatistics = false;
    bool MemoryBudget = false;
    bool ImmediatePresent = false;
    bool AdaptivePresent = false;
    bool DynamicRendering = false;
    bool Synchronization2 = false;
};

struct GpuCapabilityRequest
{
    uint32_t MsaaSamples = 1;
    float MaxAnisotropy = 1.0f;
    bool EnableGpuTiming = true;
    bool RequestImmediatePresent = false;
    bool RequestAdaptivePresent = false;
    GpuCapabilityPolicy Policy = GpuCapabilityPolicy::Default;
    bool EnableDriverWorkarounds = true;
};

struct GpuCapabilityProfile
{
    GpuDeviceInfo Device;
    GpuFeatureTier Tier = GpuFeatureTier::Compatibility;
    GpuCapabilityPolicy Policy = GpuCapabilityPolicy::Default;
    uint32_t SelectedMsaaSamples = 1;
    float SelectedAnisotropy = 1.0f;
    std::vector<GpuFeatureState> Features;
    std::vector<GpuFallbackDecision> Fallbacks;
    std::vector<std::string> AppliedRules;

    const GpuFeatureState* Find(GpuFeature feature) const;
    bool Supports(GpuFeature feature) const;
    bool Uses(GpuFeature feature) const;
};

GpuVendor IdentifyGpuVendor(uint32_t pciVendorId, std::string_view vendor,
                            std::string_view renderer = {});
const char* GpuApiName(GpuApi api);
const char* GpuVendorName(GpuVendor vendor);
const char* GpuFeatureName(GpuFeature feature);
const char* GpuFeatureTierName(GpuFeatureTier tier);
const char* GpuCapabilityPolicyName(GpuCapabilityPolicy policy);

GpuCapabilityProfile EvaluateGpuCapabilities(const GpuRawCapabilities& raw,
                                             const GpuCapabilityRequest& request);
bool WriteGpuCapabilityReport(const std::filesystem::path& path,
                              const GpuCapabilityProfile& profile,
                              std::string* error = nullptr);

} // namespace engine
