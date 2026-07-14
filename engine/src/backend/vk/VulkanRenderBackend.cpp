#include "engine/backend/vk/VulkanRenderBackend.h"
#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/core/Window.h"
#include "engine/render/SceneRenderer.h"
#include "engine/render/RendererFrameGraph.h"
#include "engine/scene/Scene.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <set>
#include <stdexcept>

namespace engine {

namespace {

const std::vector<const char*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> kRequiredDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

bool DeviceHasExtension(VkPhysicalDevice device, const char* requested)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());
    return std::any_of(available.begin(), available.end(), [&](const VkExtensionProperties& extension)
    {
        return std::strcmp(extension.extensionName, requested) == 0;
    });
}

bool InstanceHasExtension(const char* requested)
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
    return std::any_of(available.begin(), available.end(), [&](const VkExtensionProperties& extension)
    {
        return std::strcmp(extension.extensionName, requested) == 0;
    });
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* /*userData*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        log::Warn(std::string("[VK] ") + data->pMessage);
    return VK_FALSE;
}

bool CheckValidationLayerSupport()
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    for (const char* layer : kValidationLayers)
    {
        bool found = std::any_of(available.begin(), available.end(), [&](const VkLayerProperties& p) {
            return std::strcmp(p.layerName, layer) == 0;
        });
        if (!found)
            return false;
    }
    return true;
}

bool ContainsInsensitive(const std::string& text, const std::string& requested)
{
    if (requested.empty())
        return true;
    std::string haystack = text;
    std::string needle = requested;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(needle) != std::string::npos;
}

uint32_t SampleCountValue(VkSampleCountFlagBits samples)
{
    switch (samples)
    {
    case VK_SAMPLE_COUNT_64_BIT: return 64;
    case VK_SAMPLE_COUNT_32_BIT: return 32;
    case VK_SAMPLE_COUNT_16_BIT: return 16;
    case VK_SAMPLE_COUNT_8_BIT: return 8;
    case VK_SAMPLE_COUNT_4_BIT: return 4;
    case VK_SAMPLE_COUNT_2_BIT: return 2;
    default: return 1;
    }
}

VkSampleCountFlagBits SelectSampleCount(VkSampleCountFlags supported, uint32_t requested)
{
    for (const VkSampleCountFlagBits samples : {
            VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT})
    {
        if ((supported & samples) != 0 && SampleCountValue(samples) <= requested)
            return samples;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

std::string VulkanVersion(uint32_t version)
{
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
           std::to_string(VK_API_VERSION_MINOR(version)) + "." +
           std::to_string(VK_API_VERSION_PATCH(version));
}

bool SupportsSampledFormat(VkPhysicalDevice device, VkFormat format)
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
}

VkPipelineStageFlags2 GraphStage(rendergraph::PipelineStage stage)
{
    using rendergraph::PipelineStage;
    switch (stage)
    {
    case PipelineStage::Vertex: return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    case PipelineStage::Fragment: return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    case PipelineStage::EarlyDepth:
    case PipelineStage::LateDepth: return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    case PipelineStage::ColorOutput: return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    case PipelineStage::Compute: return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    case PipelineStage::AllShaders: return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    case PipelineStage::Transfer: return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    case PipelineStage::Present: return VK_PIPELINE_STAGE_2_NONE;
    default: return VK_PIPELINE_STAGE_2_NONE;
    }
}

VkAccessFlags2 GraphAccess(rendergraph::ResourceState state)
{
    using rendergraph::ResourceState;
    switch (state)
    {
    case ResourceState::ShaderRead: return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    case ResourceState::ShaderWrite: return VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    case ResourceState::ColorAttachment: return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    case ResourceState::DepthWrite: return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case ResourceState::DepthRead: return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    case ResourceState::TransferSource: return VK_ACCESS_2_TRANSFER_READ_BIT;
    case ResourceState::TransferDestination: return VK_ACCESS_2_TRANSFER_WRITE_BIT;
    default: return 0;
    }
}

VkImageLayout GraphLayout(rendergraph::ResourceState state)
{
    using rendergraph::ResourceState;
    switch (state)
    {
    case ResourceState::ShaderRead: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::ShaderWrite: return VK_IMAGE_LAYOUT_GENERAL;
    case ResourceState::ColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthWrite: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthRead: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case ResourceState::TransferSource: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::TransferDestination: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default: return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

} // namespace

void VulkanRenderBackend::Init(Window& window, const RenderBackendConfig& config)
{
    m_window = &window;
    m_config = config;
    m_presentMode = config.Presentation;
    m_validationEnabled = config.EnableValidation;
    m_renderGraphConfig = {};
    m_renderGraphConfig.Enabled = config.EnableRenderGraph;
    m_renderGraphConfig.Validation = config.ValidateRenderGraph;
    m_renderGraphConfig.TransientAliasing = config.EnableTransientAliasing;
    if (!config.RenderGraphConfigPath.empty())
    {
        std::string graphError;
        if (!rendergraph::LoadConfig(config.RenderGraphConfigPath,
                                     m_renderGraphConfig, &graphError))
            throw std::runtime_error(graphError);
        m_renderGraphConfig.Enabled = m_renderGraphConfig.Enabled && config.EnableRenderGraph;
        m_renderGraphConfig.Validation = m_renderGraphConfig.Validation && config.ValidateRenderGraph;
        m_renderGraphConfig.TransientAliasing = m_renderGraphConfig.TransientAliasing &&
                                                config.EnableTransientAliasing;
    }

    CreateInstance();
    SetupDebugMessenger();
    CreateSurface(window);
    PickPhysicalDevice();
    CreateLogicalDevice();
    LoadDebugUtils();
    m_pipelineCacheStats = {};
    const GpuDeviceInfo& gpu = m_capabilities.Gpu.Device;
    const std::filesystem::path cacheRoot = config.PipelineCacheDirectory.empty()
        ? std::filesystem::path(ENGINE_RENDERER_CACHE_DIR)
        : std::filesystem::path(config.PipelineCacheDirectory);
    const std::string cacheIdentity = gpu.VendorName + "|" + gpu.DeviceName + "|" +
        gpu.ApiVersion + "|" + gpu.DriverName + "|" +
        std::to_string(gpu.DriverVersion);
    const std::filesystem::path cacheDirectory =
        RendererCacheDirectory(cacheRoot, "vulkan", cacheIdentity);
    if (config.ClearPipelineCache)
        ClearRendererCacheFiles(cacheDirectory);
    m_shaderCacheDirectory = cacheDirectory / "shaders";
    m_pipelineCache.Init(m_physicalDevice, m_device,
                         cacheDirectory / "pipelines.vkc",
                         config.EnablePipelineCache,
                         config.ClearPipelineCache,
                         m_pipelineCreationFeedbackSupported,
                         m_pipelineCacheStats);
    if (m_pipelineCache.Handle() != VK_NULL_HANDLE)
        SetDebugName(VK_OBJECT_TYPE_PIPELINE_CACHE,
                     reinterpret_cast<uint64_t>(m_pipelineCache.Handle()),
                     "Renderer Persistent Pipeline Cache");
    const QueueFamilyIndices resourceQueues = FindQueueFamilies(m_physicalDevice);
    m_resources.Init(m_physicalDevice, m_device, *resourceQueues.Graphics, *resourceQueues.Transfer);
    CreateAsyncResourceInfrastructure();
    m_pipelineTasks = std::make_unique<concurrency::TaskSystem>(4);
    CreateShaderInfrastructure();
    CreatePostInfrastructure();
    CreateEnvironmentInfrastructure();
    CreateSwapchain(window.Width(), window.Height());
    CreateImageViews();
    CreateDepthResources();
    CreateGraphicsPipeline();
    CreateCommandObjects();
    CreateDefaultResources();
    CreatePostTargets();
    CreateEnvironmentResources();
    CreateShadowResources();
    CreateLocalLightResources();
    CreatePerformanceQueries();
    CreateSyncObjects();

    log::Info("Vulkan cache: " +
              std::to_string(m_pipelineCacheStats.ShaderPermutationHits) + " shader hit(s), " +
              std::to_string(m_pipelineCacheStats.ShaderPermutationMisses) + " shader miss(es), " +
              std::to_string(m_pipelineCacheStats.PipelineCreateCalls) + " pipeline create(s), " +
              std::to_string(m_pipelineCacheStats.PipelineCreateMilliseconds) + " ms");
    log::Info("Vulkan PBR renderer ready (indexed meshes + material textures + dynamic rendering)");
}

void VulkanRenderBackend::CreateInstance()
{
    if (m_validationEnabled && !CheckValidationLayerSupport())
    {
        log::Warn("Validation layers requested but not available - continuing without them");
        m_validationEnabled = false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SourceLikeEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "SourceLikeEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    const bool useValidation = m_validationEnabled;
    m_debugUtilsEnabled = InstanceHasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (m_debugUtilsEnabled)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (useValidation)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan instance");
}

void VulkanRenderBackend::SetupDebugMessenger()
{
    if (!m_validationEnabled)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (func)
        func(m_instance, &createInfo, nullptr, &m_debugMessenger);
}

void VulkanRenderBackend::CreateSurface(Window& window)
{
    if (glfwCreateWindowSurface(m_instance, window.Handle(), nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan window surface");
}

VulkanRenderBackend::QueueFamilyIndices VulkanRenderBackend::FindQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i)
    {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !indices.Graphics)
            indices.Graphics = i;

        if (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            const bool dedicated = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0;
            if (!indices.Transfer || dedicated)
                indices.Transfer = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport)
            indices.Present = i;

    }

    if (!indices.Transfer)
        indices.Transfer = indices.Graphics;
    return indices;
}

bool VulkanRenderBackend::IsDeviceSuitable(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3)
        return false;

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.pNext = &vulkan13Features;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &vulkan12Features;
    vkGetPhysicalDeviceFeatures2(device, &features);
    if (!vulkan13Features.dynamicRendering || !vulkan13Features.synchronization2
        || !vulkan12Features.timelineSemaphore || !features.features.imageCubeArray)
        return false;

    QueueFamilyIndices indices = FindQueueFamilies(device);
    if (!indices.IsComplete())
        return false;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

    std::set<std::string> required(kRequiredDeviceExtensions.begin(),
                                   kRequiredDeviceExtensions.end());
    for (const auto& ext : available)
        required.erase(ext.extensionName);

    if (!required.empty())
        return false;

    uint32_t formatCount = 0, presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    return formatCount > 0 && presentModeCount > 0;
}

void VulkanRenderBackend::PickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0)
        throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice device : devices)
    {
        if (!IsDeviceSuitable(device))
            continue;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (!ContainsInsensitive(properties.deviceName, m_config.PreferredAdapter))
            continue;
        int score = 1;
        if (m_config.PreferDiscreteGpu
            && properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 1000;
        score += static_cast<int>(properties.limits.maxImageDimension2D / 4096);
        if (score > bestScore)
        {
            bestScore = score;
            m_physicalDevice = device;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        if (!m_config.PreferredAdapter.empty())
            throw std::runtime_error("No suitable Vulkan GPU matches adapter filter: "
                                     + m_config.PreferredAdapter);
        throw std::runtime_error("No suitable Vulkan GPU found");
    }

    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties2);
    const VkPhysicalDeviceProperties& props = properties2.properties;
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &features);
    const VkSampleCountFlags commonSamples = props.limits.framebufferColorSampleCounts
                                           & props.limits.framebufferDepthSampleCounts;

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount,
                                               presentModes.data());
    const auto hasPresentMode = [&presentModes](VkPresentModeKHR mode)
    {
        return std::find(presentModes.begin(), presentModes.end(), mode) != presentModes.end();
    };

    const QueueFamilyIndices queueIndices = FindQueueFamilies(m_physicalDevice);
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, families.data());
    const bool timestamps = queueIndices.Graphics && families[*queueIndices.Graphics].timestampValidBits > 0 &&
                            props.limits.timestampComputeAndGraphics == VK_TRUE;

    GpuRawCapabilities raw;
    raw.Device.Api = GpuApi::Vulkan;
    raw.Device.VendorId = props.vendorID;
    raw.Device.DeviceId = props.deviceID;
    raw.Device.DriverId = static_cast<uint32_t>(driver.driverID);
    raw.Device.DriverVersion = props.driverVersion;
    raw.Device.DeviceName = props.deviceName;
    raw.Device.Vendor = IdentifyGpuVendor(props.vendorID, driver.driverName, props.deviceName);
    raw.Device.VendorName = GpuVendorName(raw.Device.Vendor);
    raw.Device.ApiVersion = VulkanVersion(props.apiVersion);
    raw.Device.DriverName = driver.driverName;
    raw.Device.DriverInfo = driver.driverInfo;
    raw.Device.ConformanceVersion = std::to_string(driver.conformanceVersion.major) + "." +
        std::to_string(driver.conformanceVersion.minor) + "." +
        std::to_string(driver.conformanceVersion.subminor) + "." +
        std::to_string(driver.conformanceVersion.patch);
    raw.Device.SoftwareRenderer = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    raw.MaxMsaaSamples = SampleCountValue(SelectSampleCount(commonSamples, 64));
    raw.MaxAnisotropy = features.samplerAnisotropy ? props.limits.maxSamplerAnisotropy : 1.0f;
    raw.TextureCompressionBc = features.textureCompressionBC &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_BC1_RGBA_UNORM_BLOCK) &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_BC1_RGBA_SRGB_BLOCK) &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_BC3_UNORM_BLOCK) &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_BC3_SRGB_BLOCK) &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_BC5_UNORM_BLOCK);
    raw.TextureCompressionBc7 = features.textureCompressionBC &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_BC7_UNORM_BLOCK) &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_BC7_SRGB_BLOCK);
    raw.TextureCompressionAstc = features.textureCompressionASTC_LDR &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_ASTC_4x4_UNORM_BLOCK) &&
        SupportsSampledFormat(m_physicalDevice, VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
    raw.GpuTimestamps = timestamps;
    raw.PipelineStatistics = features.pipelineStatisticsQuery;
    raw.MemoryBudget = DeviceHasExtension(m_physicalDevice, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    raw.ImmediatePresent = hasPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR);
    raw.AdaptivePresent = hasPresentMode(VK_PRESENT_MODE_FIFO_RELAXED_KHR);
    raw.DynamicRendering = true;
    raw.Synchronization2 = true;

    GpuCapabilityRequest request;
    request.MsaaSamples = m_config.MsaaSamples;
    request.MaxAnisotropy = m_config.MaxAnisotropy;
    request.EnableGpuTiming = m_config.EnableGpuTiming;
    request.RequestImmediatePresent = m_config.Presentation == PresentMode::Immediate;
    request.RequestAdaptivePresent = m_config.Presentation == PresentMode::Adaptive;
    request.Policy = m_config.CapabilityPolicy;
    request.EnableDriverWorkarounds = m_config.EnableDriverWorkarounds;
    m_capabilities.Gpu = EvaluateGpuCapabilities(raw, request);
    m_msaaSamples = SelectSampleCount(commonSamples, m_capabilities.Gpu.SelectedMsaaSamples);
    m_capabilities.AdapterName = props.deviceName;
    m_capabilities.MaxMsaaSamples = raw.MaxMsaaSamples;
    m_capabilities.ActiveMsaaSamples = SampleCountValue(m_msaaSamples);
    m_capabilities.MaxAnisotropy = raw.MaxAnisotropy;
    m_capabilities.ActiveAnisotropy = m_capabilities.Gpu.SelectedAnisotropy;
    m_capabilities.GpuTiming = raw.GpuTimestamps;
    m_capabilities.GpuPipelineStatistics = raw.PipelineStatistics;
    m_capabilities.ImmediatePresent = raw.ImmediatePresent;
    m_capabilities.AdaptivePresent = raw.AdaptivePresent;
    m_capabilities.HardwareAccelerated = !raw.Device.SoftwareRenderer;
    m_memoryBudgetSupported = raw.MemoryBudget;
    m_pipelineCreationFeedbackSupported =
        DeviceHasExtension(m_physicalDevice, VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
    m_capabilities.GpuMemoryBudget = m_memoryBudgetSupported;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i)
        if ((memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            m_capabilities.DedicatedVideoMemoryBytes += memoryProperties.memoryHeaps[i].size;
    log::Info(std::string("Vulkan GPU: ") + props.deviceName + " (" + driver.driverName +
              ", API " + raw.Device.ApiVersion + ", tier " +
              GpuFeatureTierName(m_capabilities.Gpu.Tier) + ")");
    for (const GpuFallbackDecision& fallback : m_capabilities.Gpu.Fallbacks)
        log::Warn("GPU fallback [" + fallback.RuleId + "] " + fallback.Feature + ": " +
                  fallback.Requested + " -> " + fallback.Selected + " (" + fallback.Reason + ")");
}

void VulkanRenderBackend::CreateLogicalDevice()
{
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    std::set<uint32_t> uniqueFamilies = {*indices.Graphics, *indices.Present, *indices.Transfer};

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueCreateInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
    deviceFeatures.imageCubeArray = supportedFeatures.imageCubeArray;
    deviceFeatures.pipelineStatisticsQuery = supportedFeatures.pipelineStatisticsQuery;
    // Cooked assets may contain native desktop BC or mobile ASTC blocks. The
    // corresponding device feature must be enabled before those formats can
    // legally be sampled, even when format properties advertise support.
    deviceFeatures.textureCompressionBC = supportedFeatures.textureCompressionBC;
    deviceFeatures.textureCompressionASTC_LDR = supportedFeatures.textureCompressionASTC_LDR;
    m_samplerAnisotropySupported = supportedFeatures.samplerAnisotropy == VK_TRUE &&
        m_capabilities.Gpu.Supports(GpuFeature::AnisotropicFiltering);
    m_pipelineStatisticsSupported = supportedFeatures.pipelineStatisticsQuery == VK_TRUE;
    m_capabilities.GpuPipelineStatistics = m_pipelineStatisticsSupported;

    VkPhysicalDeviceProperties physicalDeviceProperties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &physicalDeviceProperties);
    m_maxSamplerAnisotropy = m_samplerAnisotropySupported
        ? m_capabilities.Gpu.SelectedAnisotropy : 1.0f;
    m_capabilities.MaxAnisotropy = m_samplerAnisotropySupported
        ? physicalDeviceProperties.limits.maxSamplerAnisotropy : 1.0f;
    m_capabilities.ActiveAnisotropy = m_maxSamplerAnisotropy;

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.timelineSemaphore = VK_TRUE;
    vulkan12Features.pNext = &vulkan13Features;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &vulkan12Features;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    std::vector<const char*> enabledExtensions = kRequiredDeviceExtensions;
    if (m_memoryBudgetSupported)
        enabledExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (m_pipelineCreationFeedbackSupported)
        enabledExtensions.push_back(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan logical device");

    vkGetDeviceQueue(m_device, *indices.Graphics, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, *indices.Present, 0, &m_presentQueue);
    vkGetDeviceQueue(m_device, *indices.Transfer, 0, &m_transferQueue);
}

void VulkanRenderBackend::CreateSwapchain(int width, int height)
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosenFormat = f;
            break;
        }
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    const auto supportsPresentMode = [&](VkPresentModeKHR requested) {
        return std::find(presentModes.begin(), presentModes.end(), requested)
            != presentModes.end();
    };
    m_capabilities.ImmediatePresent = supportsPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR);
    m_capabilities.AdaptivePresent = supportsPresentMode(VK_PRESENT_MODE_FIFO_RELAXED_KHR);
    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    m_presentModeExact = true;
    if (m_presentMode == PresentMode::Immediate)
    {
        if (m_capabilities.ImmediatePresent)
            chosenPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else
        {
            chosenPresentMode = supportsPresentMode(VK_PRESENT_MODE_MAILBOX_KHR)
                ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
            m_presentModeExact = false;
        }
    }
    else if (m_presentMode == PresentMode::Adaptive)
    {
        if (m_capabilities.AdaptivePresent)
            chosenPresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        else
            m_presentModeExact = false;
    }

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX)
    {
        extent = caps.currentExtent;
    }
    else
    {
        extent.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = chosenFormat.format;
    createInfo.imageColorSpace = chosenFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0)
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = {*indices.Graphics, *indices.Present};
    if (indices.Graphics != indices.Present)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = chosenPresentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan swapchain");
    SetDebugName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<uint64_t>(m_swapchain),
                 "Main Window Swapchain");

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());
    for (size_t index = 0; index < m_swapchainImages.size(); ++index)
        SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_swapchainImages[index]),
                     "Swapchain Image " + std::to_string(index));

    m_swapchainFormat = chosenFormat.format;
    m_swapchainExtent = extent;
}

void VulkanRenderBackend::CreateImageViews()
{
    m_swapchainImageViews.resize(m_swapchainImages.size());
    for (size_t i = 0; i < m_swapchainImages.size(); ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create swapchain image view");
        SetDebugName(VK_OBJECT_TYPE_IMAGE_VIEW,
                     reinterpret_cast<uint64_t>(m_swapchainImageViews[i]),
                     "Swapchain Image " + std::to_string(i) + " View");
    }
}

void VulkanRenderBackend::CreateDepthResources()
{
    m_depthFormat = m_resources.FindSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (m_depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || m_depthFormat == VK_FORMAT_D24_UNORM_S8_UINT)
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

    try
    {
        m_depthImages.reserve(m_swapchainImages.size());
        for (size_t i = 0; i < m_swapchainImages.size(); ++i)
        {
            m_depthImages.push_back(m_resources.CreateImage2D(
                m_swapchainExtent.width, m_swapchainExtent.height, m_depthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT, aspect));
            m_resources.SetDebugName(m_depthImages.back(),
                "Main HDR Depth " + std::to_string(i));
        }
        if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
        {
            for (size_t i = 0; i < m_msaaDepthImages.size(); ++i)
            {
                vulkan::Image& image = m_msaaDepthImages[i];
                image = m_resources.CreateImage2D(
                    m_swapchainExtent.width, m_swapchainExtent.height, m_depthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, aspect,
                    1, 1, 0, m_msaaSamples);
                m_resources.SetDebugName(image, "Main HDR MSAA Depth " + std::to_string(i));
            }
        }
    }
    catch (...)
    {
        for (vulkan::Image& image : m_depthImages)
            m_resources.Destroy(image);
        m_depthImages.clear();
        for (vulkan::Image& image : m_msaaDepthImages)
            m_resources.Destroy(image);
        throw;
    }
}

void VulkanRenderBackend::CreateCommandObjects()
{
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = *indices.Graphics;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan command pool");
    SetDebugName(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(m_commandPool),
                 "Graphics Command Pool");

    m_commandBuffers.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate Vulkan command buffers");
    for (size_t index = 0; index < m_commandBuffers.size(); ++index)
        SetDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER,
                     reinterpret_cast<uint64_t>(m_commandBuffers[index]),
                     "Frame Command Buffer " + std::to_string(index));
}

void VulkanRenderBackend::CreateSyncObjects()
{
    m_imageAvailable.resize(kFramesInFlight);
    m_renderFinished.resize(kFramesInFlight);
    m_inFlightFences.resize(kFramesInFlight);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kFramesInFlight; ++i)
    {
        if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinished[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan sync objects");
        }
        SetDebugName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(m_imageAvailable[i]),
                     "Frame " + std::to_string(i) + " Image Available");
        SetDebugName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(m_renderFinished[i]),
                     "Frame " + std::to_string(i) + " Render Finished");
        SetDebugName(VK_OBJECT_TYPE_FENCE, reinterpret_cast<uint64_t>(m_inFlightFences[i]),
                     "Frame " + std::to_string(i) + " In Flight Fence");
    }
}

void VulkanRenderBackend::DestroySwapchain()
{
    for (vulkan::Image& image : m_depthImages)
        m_resources.Destroy(image);
    m_depthImages.clear();
    for (vulkan::Image& image : m_msaaDepthImages)
        m_resources.Destroy(image);

    for (VkImageView view : m_swapchainImageViews)
        vkDestroyImageView(m_device, view, nullptr);
    m_swapchainImageViews.clear();

    if (m_swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

void VulkanRenderBackend::RecreateSwapchain(int width, int height)
{
    if (width == 0 || height == 0)
        return;

    vkDeviceWaitIdle(m_device);
    m_hasFrameDebugFrame = false;
    DestroyGraphicsPipeline();
    DestroyPostTargets();
    DestroySwapchain();
    CreateSwapchain(width, height);
    CreateImageViews();
    CreateDepthResources();
    CreateGraphicsPipeline();
    CreatePostTargets();
}

void VulkanRenderBackend::Resize(int width, int height)
{
    RecreateSwapchain(width, height);
}

bool VulkanRenderBackend::SetPresentMode(PresentMode mode)
{
    if (m_presentMode == mode)
        return m_presentModeExact;
    m_presentMode = mode;
    if (m_device != VK_NULL_HANDLE && m_window && m_window->Width() > 0 && m_window->Height() > 0)
        RecreateSwapchain(m_window->Width(), m_window->Height());
    return m_presentModeExact;
}

void VulkanRenderBackend::RenderFrame(const RenderFrameData& frame)
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Vulkan.RenderFrame", "Renderer/Vulkan");
    const Scene& scene = *frame.SceneData;
    ++m_gpuProfileFrameIndex;
    {
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("WaitFrameFence", "Renderer/Vulkan/Sync");
        vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    }
    ReclaimTransferUploads();
    m_frameGpuArena->Reset(m_currentFrame);
    m_deferredRelease.ReleaseCompleted(frame.FrameIndex);
    ReadPerformanceQueries(m_currentFrame);
    UpdateAutoExposure(frame);
    {
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Environment", "Renderer/Vulkan");
        EnsureEnvironmentBaked(frame);
    }
    {
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("FramePreparation", "Renderer/Vulkan");
        PrepareLocalLights(frame);
        UpdateFrameUniforms(frame);
    }

    uint32_t imageIndex = 0;
    VkResult acquireResult;
    {
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("AcquireSwapchain", "Renderer/Vulkan/Sync");
        acquireResult = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
            m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    }
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain(m_window->Width(), m_window->Height());
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Vulkan: failed to acquire swapchain image");
    PreparePost(frame, imageIndex);
    PrepareDebugOverlay(frame);

    struct PreparedDraw
    {
        const GpuMesh* Mesh = nullptr;
        GpuMaterial* Material = nullptr;
        vulkan::ObjectConstants Object;
        bool CastsShadows = true;
    };
    profiling::CpuProfileScope drawPreparationScope("PrepareDraws", "Renderer/Vulkan");
    std::vector<PreparedDraw> preparedDraws(frame.SceneData->Instances().size());
    std::vector<uint8_t> preparedFlags(preparedDraws.size(), 0);
    const auto prepareDraw = [&](const PreparedRenderCommand& command) -> PreparedDraw* {
        PreparedDraw& draw = preparedDraws[command.InstanceIndex];
        if (preparedFlags[command.InstanceIndex] != 0)
            return &draw;
        preparedFlags[command.InstanceIndex] = 1;
        const MeshInstance& instance = *command.Source;
        draw.Mesh = &GetOrCreateMesh(instance.Mesh);
        draw.Material = &GetOrCreateMaterial(instance.Mat);
        draw.Object.Model = instance.Transform;
        const auto previous = instance.TemporalId != 0
            ? m_previousTransforms.find(instance.TemporalId) : m_previousTransforms.end();
        draw.Object.PreviousModel = m_taaActive && m_taaHistoryValid
            && previous != m_previousTransforms.end() ? previous->second : instance.Transform;
        draw.CastsShadows = instance.CastsShadows;
        const render::ArenaAllocation objectAllocation = m_frameGpuArena->Allocate(
            m_currentFrame, sizeof(draw.Object), 16);
        if (!objectAllocation)
            throw std::runtime_error("Vulkan: per-frame GPU arena exhausted by object commands");
        std::memcpy(static_cast<std::byte*>(m_frameArenaMapped) + objectAllocation.Offset,
                    &draw.Object, sizeof(draw.Object));
        return &draw;
    };
    std::vector<PreparedDraw*> draws;
    draws.reserve(frame.RenderCommands.size());
    for (const PreparedRenderCommand& command : frame.RenderCommands)
        draws.push_back(prepareDraw(command));
    std::vector<PreparedDraw*> shadowDraws;
    shadowDraws.reserve(frame.ShadowCommands.size());
    for (const PreparedRenderCommand& command : frame.ShadowCommands)
        shadowDraws.push_back(prepareDraw(command));
    drawPreparationScope.End();

    vulkan::Buffer screenshotBuffer;
    const bool takeScreenshot = !m_screenshotPath.empty();
    if (takeScreenshot)
    {
        screenshotBuffer = m_resources.CreateBuffer(
            static_cast<VkDeviceSize>(m_swapchainExtent.width) * m_swapchainExtent.height * 4,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_resources.SetDebugName(screenshotBuffer, "Screenshot Readback Buffer");
    }
    vulkan::Buffer hdrScreenshotBuffer;
    const bool takeHdrScreenshot = !m_hdrScreenshotPath.empty();
    if (takeHdrScreenshot)
    {
        hdrScreenshotBuffer = m_resources.CreateBuffer(
            static_cast<VkDeviceSize>(m_swapchainExtent.width) * m_swapchainExtent.height * 8,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_resources.SetDebugName(hdrScreenshotBuffer, "Linear HDR Screenshot Readback Buffer");
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    profiling::CpuProfileScope commandRecordingScope("RecordCommands", "Renderer/Vulkan");
    vkBeginCommandBuffer(cmd, &beginInfo);
    BeginDebugLabel(cmd, "Frame", {0.20f, 0.45f, 0.95f, 1.0f});

    const bool gpuProfilerEnabled = profiling::GpuProfiler::Get().IsEnabled();
    const bool recordGpuTiming = gpuProfilerEnabled && m_gpuTimingSupported;
    const bool recordPipelineStatistics = gpuProfilerEnabled &&
        m_pipelineStatisticsSupported && m_pipelineStatisticsQueryPool != VK_NULL_HANDLE;
    const uint32_t timestampBase = m_currentFrame * kTimestampCountPerFrame;
    m_gpuDrawCallsThisFrame = 0;
    m_gpuDispatchesThisFrame = 0;
    if (recordGpuTiming)
    {
        vkCmdResetQueryPool(cmd, m_timestampQueryPool, timestampBase,
                            kTimestampCountPerFrame);
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 0);
    }
    if (recordPipelineStatistics)
    {
        vkCmdResetQueryPool(cmd, m_pipelineStatisticsQueryPool, m_currentFrame, 1);
        vkCmdBeginQuery(cmd, m_pipelineStatisticsQueryPool, m_currentFrame, 0);
    }

    const auto directionalShadowPass = [&]() {
    BeginDebugLabel(cmd, "Shadows / Directional Cascades", {0.55f, 0.35f, 0.85f, 1.0f});
    if (frame.SunShadowsActive)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
        for (int cascade = 0; cascade < 4; ++cascade)
        {
            BeginDebugLabel(cmd, "Directional Cascade " + std::to_string(cascade),
                            {0.45f, 0.25f, 0.75f, 1.0f});
            VkRenderingAttachmentInfo shadowDepth{};
            shadowDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            shadowDepth.imageView = m_shadowMaps[m_currentFrame][cascade].View;
            shadowDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            shadowDepth.clearValue.depthStencil = {1.0f, 0};
            VkRenderingInfo shadowRendering{};
            shadowRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            shadowRendering.renderArea.extent = {m_shadowSizes[cascade], m_shadowSizes[cascade]};
            shadowRendering.layerCount = 1;
            shadowRendering.pDepthAttachment = &shadowDepth;
            vkCmdBeginRendering(cmd, &shadowRendering);

            VkViewport shadowViewport{};
            shadowViewport.width = static_cast<float>(m_shadowSizes[cascade]);
            shadowViewport.height = static_cast<float>(m_shadowSizes[cascade]);
            shadowViewport.minDepth = 0.0f;
            shadowViewport.maxDepth = 1.0f;
            VkRect2D shadowScissor{{0, 0}, {m_shadowSizes[cascade], m_shadowSizes[cascade]}};
            vkCmdSetViewport(cmd, 0, 1, &shadowViewport);
            vkCmdSetScissor(cmd, 0, 1, &shadowScissor);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipelineLayout,
                                    0, 1, &m_shadowDescriptorSets[m_currentFrame][cascade], 0, nullptr);
            for (const PreparedDraw* draw : shadowDraws)
            {
                if (!draw->CastsShadows)
                    continue;
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &draw->Mesh->VertexBuffer.Handle, &offset);
                vkCmdBindIndexBuffer(cmd, draw->Mesh->IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipelineLayout,
                                        1, 1, &draw->Material->DescriptorSets[m_currentFrame], 0, nullptr);
                vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(draw->Object), &draw->Object);
                vkCmdDrawIndexed(cmd, draw->Mesh->IndexCount, 1, 0, 0, 0);
                ++m_gpuDrawCallsThisFrame;
            }
            vkCmdEndRendering(cmd);
            EndDebugLabel(cmd);
        }

    }
    EndDebugLabel(cmd);

    if (recordGpuTiming)
    {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                              m_timestampQueryPool, timestampBase + 1);
    }
    };

    const auto localShadowPass = [&]() {
    if (recordGpuTiming)
    {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                              m_timestampQueryPool, timestampBase + 2);
    }
    BeginDebugLabel(cmd, "Shadows / Local Lights", {0.75f, 0.35f, 0.75f, 1.0f});
    RecordLocalLightShadows(cmd, scene);
    EndDebugLabel(cmd);
    if (recordGpuTiming)
    {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 3);
    }
    };

    const auto mainHdrPass = [&]() {
    if (recordGpuTiming)
    {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 4);
    }

    BeginDebugLabel(cmd, "Main HDR / Geometry + Sky + Resolve",
                    {0.20f, 0.70f, 0.35f, 1.0f});
    const glm::vec3 background = scene.Sky.HorizonColor * scene.Sky.SkyIntensity * 0.35f;
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_msaaSamples != VK_SAMPLE_COUNT_1_BIT
        ? m_msaaHdrImages[m_currentFrame].View : m_hdrImages[imageIndex].View;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{background.r, background.g, background.b, 1.0f}};
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = m_hdrImages[imageIndex].View;
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo velocityAttachment{};
    velocityAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    velocityAttachment.imageView = m_msaaSamples != VK_SAMPLE_COUNT_1_BIT
        ? m_msaaVelocityImages[m_currentFrame].View : m_velocityImages[imageIndex].View;
    velocityAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    velocityAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    velocityAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    velocityAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        velocityAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        velocityAttachment.resolveImageView = m_velocityImages[imageIndex].View;
        velocityAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    const VkRenderingAttachmentInfo colorAttachments[] = {colorAttachment, velocityAttachment};
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_msaaSamples != VK_SAMPLE_COUNT_1_BIT
        ? m_msaaDepthImages[m_currentFrame].View : m_depthImages[imageIndex].View;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        depthAttachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
        depthAttachment.resolveImageView = m_depthImages[imageIndex].View;
        depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = m_swapchainExtent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<uint32_t>(std::size(colorAttachments));
    rendering.pColorAttachments = colorAttachments;
    rendering.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(cmd, &rendering);

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_swapchainExtent.width);
    viewport.height = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, m_swapchainExtent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbrPipelineLayout,
                            0, 1, &m_frameDescriptorSets[m_currentFrame], 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ++m_gpuDrawCallsThisFrame;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbrPipeline);
    for (const PreparedDraw* draw : draws)
    {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &draw->Mesh->VertexBuffer.Handle, &offset);
        vkCmdBindIndexBuffer(cmd, draw->Mesh->IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbrPipelineLayout,
                                1, 1, &draw->Material->DescriptorSets[m_currentFrame], 0, nullptr);
        vkCmdPushConstants(cmd, m_pbrPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(draw->Object), &draw->Object);
        vkCmdDrawIndexed(cmd, draw->Mesh->IndexCount, 1, 0, 0, 0);
        ++m_gpuDrawCallsThisFrame;
    }
    if (!frame.VisibilityDebug.empty())
    {
        BeginDebugLabel(cmd, "Visibility / Bounds Debug", {0.20f, 0.90f, 0.30f, 1.0f});
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boundsDebugPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_boundsDebugPipelineLayout, 0, 1,
                                &m_frameDescriptorSets[m_currentFrame], 0, nullptr);
        for (const VisibilityDebugBounds& debugBounds : frame.VisibilityDebug)
        {
            vulkan::BoundsDebugConstants constants;
            constants.Minimum = glm::vec4(debugBounds.Bounds.Minimum, 0.0f);
            constants.Maximum = glm::vec4(debugBounds.Bounds.Maximum, 0.0f);
            if (debugBounds.Classification == VisibilityClassification::FrustumCulled)
                constants.Color = {3.0f, 0.12f, 0.08f, 1.0f};
            else if (debugBounds.Classification == VisibilityClassification::DistanceCulled)
                constants.Color = {3.0f, 1.4f, 0.05f, 1.0f};
            vkCmdPushConstants(cmd, m_boundsDebugPipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(constants), &constants);
            vkCmdDraw(cmd, 24, 1, 0, 0);
            ++m_gpuDrawCallsThisFrame;
        }
        EndDebugLabel(cmd);
    }
    vkCmdEndRendering(cmd);
    EndDebugLabel(cmd);

    if (recordGpuTiming)
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                              m_timestampQueryPool, timestampBase + 5);

    };

    const auto postProcessPass = [&]() {
    if (recordGpuTiming)
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                              m_timestampQueryPool, timestampBase + 6);
    BeginDebugLabel(cmd, "Post Process", {0.90f, 0.55f, 0.15f, 1.0f});
    RecordAutoExposure(cmd, frame, imageIndex);
    if (m_taaActive)
        RecordTemporalAA(cmd, frame, imageIndex);
    RecordPost(cmd, frame, imageIndex);
    EndDebugLabel(cmd);
    if (recordGpuTiming)
    {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 7);
    }
    };

    const auto debugUiPass = [&]() {
    if (recordGpuTiming)
    {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 8);
    }
    BeginDebugLabel(cmd, "Debug UI", {0.20f, 0.75f, 0.85f, 1.0f});
    RecordDebugOverlay(cmd, frame, imageIndex);
    EndDebugLabel(cmd);
    if (recordGpuTiming)
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 9);
    };

    rendergraph::RendererFrameGraphFeatures graphFeatures;
    graphFeatures.Width = m_swapchainExtent.width;
    graphFeatures.Height = m_swapchainExtent.height;
    graphFeatures.MsaaSamples = SampleCountValue(m_msaaSamples);
    graphFeatures.BloomLevels = 6;
    graphFeatures.SunShadows = frame.SunShadowsActive;
    graphFeatures.LocalShadows = !scene.PointLights().empty() || !scene.SpotLights().empty() ||
                                 !scene.AreaLights().empty();
    graphFeatures.TemporalAA = m_taaActive;
    graphFeatures.TaaReadIndex = static_cast<uint32_t>(m_taaHistoryIndex);
    graphFeatures.TaaWriteIndex = static_cast<uint32_t>((m_taaHistoryIndex + 1) % 2);
    graphFeatures.Bloom = scene.PostProcess.Enabled;
    graphFeatures.Fxaa = scene.PostProcess.Enabled &&
                         scene.PostProcess.AntiAliasing == AntiAliasingMode::Fxaa;
    graphFeatures.DebugUi = true;
    rendergraph::RendererFrameGraphCallbacks graphCallbacks;
    graphCallbacks[rendergraph::RendererPass::Environment] = [] {};
    graphCallbacks[rendergraph::RendererPass::DirectionalShadows] = directionalShadowPass;
    graphCallbacks[rendergraph::RendererPass::LocalShadows] = localShadowPass;
    graphCallbacks[rendergraph::RendererPass::MainHdr] = mainHdrPass;
    graphCallbacks[rendergraph::RendererPass::PostProcess] = postProcessPass;
    graphCallbacks[rendergraph::RendererPass::DebugUi] = debugUiPass;
    std::string graphError;
    if (!rendergraph::BuildRendererFrameGraph(m_renderGraph, m_renderGraphConfig,
                                               graphFeatures, std::move(graphCallbacks),
                                               &graphError))
        throw std::runtime_error("Vulkan render graph: " + graphError);
    const auto applyGraphBarriers = [&](const rendergraph::CompiledPass& pass) {
        std::vector<VkImageMemoryBarrier2> nativeBarriers;
        nativeBarriers.reserve(pass.Barriers.size());
        for (const rendergraph::Barrier& barrier : pass.Barriers)
        {
            const rendergraph::CompiledResource& resource =
                m_renderGraph.Resources()[barrier.Resource.Index];
            const std::string& name = resource.Description.Name;
            VkImage image = VK_NULL_HANDLE;
            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            if (name.rfind("shadow.directional.", 0) == 0)
            {
                const uint32_t cascade = static_cast<uint32_t>(name.back() - '0');
                if (cascade < m_shadowMaps[m_currentFrame].size())
                    image = m_shadowMaps[m_currentFrame][cascade].Handle;
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            else if (name == "scene.hdr") image = m_hdrImages[imageIndex].Handle;
            else if (name == "scene.velocity") image = m_velocityImages[imageIndex].Handle;
            else if (name == "scene.depth")
            {
                image = m_depthImages[imageIndex].Handle;
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            else if (name == "scene.msaa.hdr") image = m_msaaHdrImages[m_currentFrame].Handle;
            else if (name == "scene.msaa.velocity") image = m_msaaVelocityImages[m_currentFrame].Handle;
            else if (name == "scene.msaa.depth")
            {
                image = m_msaaDepthImages[m_currentFrame].Handle;
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            else if (name.rfind("taa.color.", 0) == 0)
            {
                const uint32_t history = static_cast<uint32_t>(name.back() - '0');
                if (history < m_taaHistoryColor.size()) image = m_taaHistoryColor[history].Handle;
            }
            else if (name.rfind("taa.depth.", 0) == 0)
            {
                const uint32_t history = static_cast<uint32_t>(name.back() - '0');
                if (history < m_taaHistoryDepth.size()) image = m_taaHistoryDepth[history].Handle;
            }
            else if (name == "output.backbuffer") image = m_swapchainImages[imageIndex];

            // Local-light shadows, the IBL bake and bloom pyramid contain
            // multiple native subpasses. Their internal transitions remain
            // local until those operations are split into graph nodes.
            if (image == VK_NULL_HANDLE)
                continue;
            VkImageMemoryBarrier2 native{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            native.srcStageMask = GraphStage(barrier.SourceStage);
            native.srcAccessMask = GraphAccess(barrier.Before);
            native.dstStageMask = GraphStage(barrier.DestinationStage);
            native.dstAccessMask = GraphAccess(barrier.After);
            native.oldLayout = GraphLayout(barrier.Before);
            native.newLayout = GraphLayout(barrier.After);
            native.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            native.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            native.image = image;
            native.subresourceRange = {aspect, 0, 1, 0, 1};
            nativeBarriers.push_back(native);
        }
        if (!nativeBarriers.empty())
        {
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = static_cast<uint32_t>(nativeBarriers.size());
            dependency.pImageMemoryBarriers = nativeBarriers.data();
            vkCmdPipelineBarrier2(cmd, &dependency);
        }
    };
    m_renderGraph.ExecutePhase(rendergraph::PassPhase::Prepare);
    m_renderGraph.ExecutePhase(rendergraph::PassPhase::Render, applyGraphBarriers);

    if (recordPipelineStatistics)
        vkCmdEndQuery(cmd, m_pipelineStatisticsQueryPool, m_currentFrame);
    if (recordGpuTiming || recordPipelineStatistics)
    {
        m_timestampFrameWritten[m_currentFrame] = recordGpuTiming;
        m_pipelineStatisticsFrameWritten[m_currentFrame] = recordPipelineStatistics;
        m_gpuProfileFrameIds[m_currentFrame] = m_gpuProfileFrameIndex;
        m_gpuProfileDrawCalls[m_currentFrame] = m_gpuDrawCallsThisFrame;
        m_gpuProfileDispatches[m_currentFrame] = m_gpuDispatchesThisFrame;
        m_timestampLogShadows[m_currentFrame] = scene.Shadows.LogPerformance;
        m_timestampLogPost[m_currentFrame] = scene.PostProcess.LogPerformance;
        m_timestampAaMode[m_currentFrame] = scene.PostProcess.AntiAliasing;
    }

    if (takeHdrScreenshot)
    {
        const vulkan::Image& hdrSource = m_taaActive
            ? m_taaHistoryColor[m_taaHistoryIndex]
            : m_hdrImages[imageIndex];
        VkImageMemoryBarrier2 toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                                | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toTransfer.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = hdrSource.Handle;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo hdrCopyDependency{};
        hdrCopyDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        hdrCopyDependency.imageMemoryBarrierCount = 1;
        hdrCopyDependency.pImageMemoryBarriers = &toTransfer;
        vkCmdPipelineBarrier2(cmd, &hdrCopyDependency);

        VkBufferImageCopy hdrCopy{};
        hdrCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        hdrCopy.imageExtent = {m_swapchainExtent.width, m_swapchainExtent.height, 1};
        vkCmdCopyImageToBuffer(cmd, hdrSource.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               hdrScreenshotBuffer.Handle, 1, &hdrCopy);

        VkImageMemoryBarrier2 toSample = toTransfer;
        toSample.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toSample.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toSample.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                              | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toSample.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hdrCopyDependency.pImageMemoryBarriers = &toSample;
        vkCmdPipelineBarrier2(cmd, &hdrCopyDependency);
    }

    VkImageMemoryBarrier2 finishBarrier{};
    finishBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    finishBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    finishBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    finishBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finishBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finishBarrier.image = m_swapchainImages[imageIndex];
    finishBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    finishBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    finishBarrier.newLayout = takeScreenshot ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                             : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    finishBarrier.dstStageMask = takeScreenshot ? VK_PIPELINE_STAGE_2_TRANSFER_BIT
                                                : VK_PIPELINE_STAGE_2_NONE;
    finishBarrier.dstAccessMask = takeScreenshot ? VK_ACCESS_2_TRANSFER_READ_BIT : 0;
    VkDependencyInfo finishDependency{};
    finishDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    finishDependency.imageMemoryBarrierCount = 1;
    finishDependency.pImageMemoryBarriers = &finishBarrier;
    vkCmdPipelineBarrier2(cmd, &finishDependency);

    if (takeScreenshot)
    {
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {m_swapchainExtent.width, m_swapchainExtent.height, 1};
        vkCmdCopyImageToBuffer(cmd, m_swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               screenshotBuffer.Handle, 1, &copy);

        VkImageMemoryBarrier2 toPresent = finishBarrier;
        toPresent.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toPresent.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        toPresent.dstAccessMask = 0;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        finishDependency.pImageMemoryBarriers = &toPresent;
        vkCmdPipelineBarrier2(cmd, &finishDependency);
    }

    EndDebugLabel(cmd);
    vkEndCommandBuffer(cmd);
    commandRecordingScope.End();

    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("SubmitAndPresent", "Renderer/Vulkan/Sync");
    std::array<VkSemaphoreSubmitInfo, 2> waitInfos{};
    waitInfos[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfos[0].semaphore = m_imageAvailable[m_currentFrame];
    waitInfos[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    uint32_t waitCount = 1;
    if (m_pendingTransferWaitValue != 0)
    {
        waitInfos[1] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        waitInfos[1].semaphore = m_transferTimeline;
        waitInfos[1].value = m_pendingTransferWaitValue;
        waitInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        waitCount = 2;
    }
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = cmd;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = m_renderFinished[m_currentFrame];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = waitCount;
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    if (vkQueueSubmit2(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to submit rendered frame");
    m_pendingTransferWaitValue = 0;

    if (takeScreenshot || takeHdrScreenshot)
        vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    if (takeScreenshot)
    {
        SaveScreenshot(screenshotBuffer, m_screenshotPath);
        m_resources.Destroy(screenshotBuffer);
        m_screenshotPath.clear();
    }
    if (takeHdrScreenshot)
    {
        SaveHdrScreenshot(hdrScreenshotBuffer, m_hdrScreenshotPath);
        m_resources.Destroy(hdrScreenshotBuffer);
        m_hdrScreenshotPath.clear();
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinished[m_currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        RecreateSwapchain(m_window->Width(), m_window->Height());

    const Camera& camera = *frame.CameraData;
    m_previousViewProjection = m_currentViewProjection;
    m_previousCameraPosition = camera.Position;
    m_previousCameraForward = camera.Forward();
    m_previousCameraFov = camera.FovDegrees;
    m_previousScene = &scene;
    m_previousTransforms.clear();
    for (const PreparedRenderCommand& command : frame.RenderCommands)
    {
        const MeshInstance& instance = *command.Source;
        if (instance.TemporalId != 0)
            m_previousTransforms[instance.TemporalId] = instance.Transform;
    }

    m_lastFrameDebugFrameSlot = m_currentFrame;
    m_lastFrameDebugImageIndex = imageIndex;
    m_lastFrameDebugTaaActive = m_taaActive;
    m_lastFrameDebugFxaaActive = scene.PostProcess.Enabled &&
        scene.PostProcess.AntiAliasing == AntiAliasingMode::Fxaa;
    m_lastFrameDebugPostEnabled = scene.PostProcess.Enabled;
    m_hasFrameDebugFrame = true;
    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}

void VulkanRenderBackend::Shutdown()
{
    if (m_pipelineTasks)
    {
        m_pipelineTasks->WaitIdle();
        m_pipelineTasks.reset();
    }
    if (m_device != VK_NULL_HANDLE && !m_inFlightFences.empty())
        vkWaitForFences(m_device, static_cast<uint32_t>(m_inFlightFences.size()),
                        m_inFlightFences.data(), VK_TRUE, UINT64_MAX);
    if (m_device != VK_NULL_HANDLE && m_transferTimeline != VK_NULL_HANDLE &&
        m_transferTimelineValue != 0)
    {
        VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_transferTimeline;
        waitInfo.pValues = &m_transferTimelineValue;
        vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);
    }
    if (m_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_device);

    for (vulkan::Buffer& buffer : m_frameDebugReadbackBuffers)
        m_resources.Destroy(buffer);
    m_frameDebugReadbackBuffers.clear();

    DestroySceneResources();
    DestroyEnvironmentResources();
    DestroyLocalLightResources();
    DestroyShadowResources();
    DestroyPerformanceQueries();
    DestroyGraphicsPipeline();
    DestroyPostTargets();

    for (int i = 0; i < kFramesInFlight; ++i)
    {
        if (i < static_cast<int>(m_imageAvailable.size())) vkDestroySemaphore(m_device, m_imageAvailable[i], nullptr);
        if (i < static_cast<int>(m_renderFinished.size())) vkDestroySemaphore(m_device, m_renderFinished[i], nullptr);
        if (i < static_cast<int>(m_inFlightFences.size())) vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    if (m_commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);

    DestroySwapchain();
    DestroyEnvironmentInfrastructure();
    DestroyPostInfrastructure();
    DestroyShaderInfrastructure();
    DestroyAsyncResourceInfrastructure();
    m_pipelineCache.Shutdown();

    if (m_device != VK_NULL_HANDLE)
        vkDestroyDevice(m_device, nullptr);

    if (m_debugMessenger != VK_NULL_HANDLE)
    {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func)
            func(m_instance, m_debugMessenger, nullptr);
    }

    if (m_surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    if (m_instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_instance, nullptr);
}

} // namespace engine
