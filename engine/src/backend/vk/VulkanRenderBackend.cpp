#include "engine/backend/vk/VulkanRenderBackend.h"
#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/core/Window.h"
#include "engine/render/SceneRenderer.h"
#include "engine/scene/Scene.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
#include <stdexcept>

namespace engine {

namespace {

const std::vector<const char*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> kDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

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

} // namespace

void VulkanRenderBackend::Init(Window& window, const RenderBackendConfig& config)
{
    m_window = &window;
    m_config = config;
    m_presentMode = config.Presentation;
    m_validationEnabled = config.EnableValidation;

    CreateInstance();
    SetupDebugMessenger();
    CreateSurface(window);
    PickPhysicalDevice();
    CreateLogicalDevice();
    m_resources.Init(m_physicalDevice, m_device);
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
    if (useValidation)
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
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.Graphics = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport)
            indices.Present = i;

        if (indices.IsComplete())
            break;
    }

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
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &vulkan13Features;
    vkGetPhysicalDeviceFeatures2(device, &features);
    if (!vulkan13Features.dynamicRendering || !vulkan13Features.synchronization2
        || !features.features.imageCubeArray)
        return false;

    QueueFamilyIndices indices = FindQueueFamilies(device);
    if (!indices.IsComplete())
        return false;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

    std::set<std::string> required(kDeviceExtensions.begin(), kDeviceExtensions.end());
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

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    const VkSampleCountFlags commonSamples = props.limits.framebufferColorSampleCounts
                                           & props.limits.framebufferDepthSampleCounts;
    m_msaaSamples = SelectSampleCount(commonSamples, std::max(m_config.MsaaSamples, 1u));
    m_capabilities.AdapterName = props.deviceName;
    m_capabilities.MaxMsaaSamples = SampleCountValue(SelectSampleCount(commonSamples, 64));
    m_capabilities.ActiveMsaaSamples = SampleCountValue(m_msaaSamples);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i)
        if ((memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            m_capabilities.DedicatedVideoMemoryBytes += memoryProperties.memoryHeaps[i].size;
    log::Info(std::string("Vulkan GPU: ") + props.deviceName);
}

void VulkanRenderBackend::CreateLogicalDevice()
{
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    std::set<uint32_t> uniqueFamilies = {*indices.Graphics, *indices.Present};

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
    m_samplerAnisotropySupported = supportedFeatures.samplerAnisotropy == VK_TRUE;

    VkPhysicalDeviceProperties physicalDeviceProperties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &physicalDeviceProperties);
    m_maxSamplerAnisotropy = m_samplerAnisotropySupported
        ? std::clamp(m_config.MaxAnisotropy, 1.0f,
                     physicalDeviceProperties.limits.maxSamplerAnisotropy)
        : 1.0f;
    m_capabilities.MaxAnisotropy = m_samplerAnisotropySupported
        ? physicalDeviceProperties.limits.maxSamplerAnisotropy : 1.0f;
    m_capabilities.ActiveAnisotropy = m_maxSamplerAnisotropy;

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &vulkan13Features;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan logical device");

    vkGetDeviceQueue(m_device, *indices.Graphics, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, *indices.Present, 0, &m_presentQueue);
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

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

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
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, aspect));
        }
        if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
        {
            for (vulkan::Image& image : m_msaaDepthImages)
                image = m_resources.CreateImage2D(
                    m_swapchainExtent.width, m_swapchainExtent.height, m_depthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, aspect,
                    1, 1, 0, m_msaaSamples);
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

    m_commandBuffers.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate Vulkan command buffers");
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
    {
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("WaitFrameFence", "Renderer/Vulkan/Sync");
        vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    }
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
    std::vector<PreparedDraw> draws;
    draws.reserve(scene.Instances().size());
    for (const MeshInstance& instance : scene.Instances())
    {
        if (!instance.Mesh)
            continue;
        PreparedDraw draw;
        draw.Mesh = &GetOrCreateMesh(instance.Mesh);
        draw.Material = &GetOrCreateMaterial(instance.Mat);
        draw.Object.Model = instance.Transform;
        const auto previous = instance.TemporalId != 0
            ? m_previousTransforms.find(instance.TemporalId) : m_previousTransforms.end();
        draw.Object.PreviousModel = m_taaActive && m_taaHistoryValid
            && previous != m_previousTransforms.end() ? previous->second : instance.Transform;
        draw.CastsShadows = instance.CastsShadows;
        draws.push_back(draw);
    }
    drawPreparationScope.End();

    vulkan::Buffer screenshotBuffer;
    const bool takeScreenshot = !m_screenshotPath.empty();
    if (takeScreenshot)
    {
        screenshotBuffer = m_resources.CreateBuffer(
            static_cast<VkDeviceSize>(m_swapchainExtent.width) * m_swapchainExtent.height * 4,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    profiling::CpuProfileScope commandRecordingScope("RecordCommands", "Renderer/Vulkan");
    vkBeginCommandBuffer(cmd, &beginInfo);

    const bool recordGpuTiming = m_gpuTimingSupported
        && (scene.Shadows.LogPerformance || scene.PostProcess.LogPerformance
            || frame.DebugOverlay != nullptr);
    const uint32_t timestampBase = m_currentFrame * kTimestampCountPerFrame;
    if (recordGpuTiming)
    {
        vkCmdResetQueryPool(cmd, m_timestampQueryPool, timestampBase,
                            kTimestampCountPerFrame);
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 0);
    }

    if (frame.SunShadowsActive)
    {
        std::array<VkImageMemoryBarrier2, 4> toDepth{};
        for (int cascade = 0; cascade < 4; ++cascade)
        {
            toDepth[cascade].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            toDepth[cascade].srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            toDepth[cascade].srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            toDepth[cascade].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                          | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            toDepth[cascade].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                           | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            toDepth[cascade].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            toDepth[cascade].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            toDepth[cascade].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toDepth[cascade].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toDepth[cascade].image = m_shadowMaps[m_currentFrame][cascade].Handle;
            toDepth[cascade].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        }
        VkDependencyInfo shadowDependency{};
        shadowDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        shadowDependency.imageMemoryBarrierCount = static_cast<uint32_t>(toDepth.size());
        shadowDependency.pImageMemoryBarriers = toDepth.data();
        vkCmdPipelineBarrier2(cmd, &shadowDependency);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
        for (int cascade = 0; cascade < 4; ++cascade)
        {
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
            for (const PreparedDraw& draw : draws)
            {
                if (!draw.CastsShadows)
                    continue;
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &draw.Mesh->VertexBuffer.Handle, &offset);
                vkCmdBindIndexBuffer(cmd, draw.Mesh->IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipelineLayout,
                                        1, 1, &draw.Material->DescriptorSets[m_currentFrame], 0, nullptr);
                vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(draw.Object), &draw.Object);
                vkCmdDrawIndexed(cmd, draw.Mesh->IndexCount, 1, 0, 0, 0);
            }
            vkCmdEndRendering(cmd);
        }

        std::array<VkImageMemoryBarrier2, 4> toSample{};
        for (int cascade = 0; cascade < 4; ++cascade)
        {
            toSample[cascade] = toDepth[cascade];
            toSample[cascade].srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            toSample[cascade].srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            toSample[cascade].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            toSample[cascade].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            toSample[cascade].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            toSample[cascade].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        shadowDependency.pImageMemoryBarriers = toSample.data();
        vkCmdPipelineBarrier2(cmd, &shadowDependency);
    }

    RecordLocalLightShadows(cmd, scene);
    if (recordGpuTiming)
    {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 1);
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 2);
    }

    VkImageMemoryBarrier2 beginBarriers[6]{};
    beginBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    beginBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    beginBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    beginBarriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    beginBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    beginBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    beginBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarriers[0].image = m_hdrImages[imageIndex].Handle;
    beginBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    beginBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    beginBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    beginBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                  | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    beginBarriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                   | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    beginBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    beginBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    beginBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarriers[1].image = m_depthImages[imageIndex].Handle;
    beginBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    beginBarriers[2] = beginBarriers[0];
    beginBarriers[2].image = m_velocityImages[imageIndex].Handle;
    uint32_t beginBarrierCount = 3;
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        beginBarriers[3] = beginBarriers[0];
        beginBarriers[3].image = m_msaaHdrImages[m_currentFrame].Handle;
        beginBarriers[4] = beginBarriers[0];
        beginBarriers[4].image = m_msaaVelocityImages[m_currentFrame].Handle;
        beginBarriers[5] = beginBarriers[1];
        beginBarriers[5].image = m_msaaDepthImages[m_currentFrame].Handle;
        beginBarrierCount = 6;
    }
    VkDependencyInfo beginDependency{};
    beginDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    beginDependency.imageMemoryBarrierCount = beginBarrierCount;
    beginDependency.pImageMemoryBarriers = beginBarriers;
    vkCmdPipelineBarrier2(cmd, &beginDependency);

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbrPipeline);
    for (const PreparedDraw& draw : draws)
    {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &draw.Mesh->VertexBuffer.Handle, &offset);
        vkCmdBindIndexBuffer(cmd, draw.Mesh->IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbrPipelineLayout,
                                1, 1, &draw.Material->DescriptorSets[m_currentFrame], 0, nullptr);
        vkCmdPushConstants(cmd, m_pbrPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(draw.Object), &draw.Object);
        vkCmdDrawIndexed(cmd, draw.Mesh->IndexCount, 1, 0, 0, 0);
    }
    vkCmdEndRendering(cmd);

    if (recordGpuTiming)
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 3);

    VkImageMemoryBarrier2 hdrToSample{};
    hdrToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    hdrToSample.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    hdrToSample.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    hdrToSample.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                             | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    hdrToSample.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    hdrToSample.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hdrToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrToSample.image = m_hdrImages[imageIndex].Handle;
    hdrToSample.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo hdrDependency{};
    hdrDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    hdrDependency.imageMemoryBarrierCount = 1;
    hdrDependency.pImageMemoryBarriers = &hdrToSample;
    vkCmdPipelineBarrier2(cmd, &hdrDependency);

    if (recordGpuTiming)
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 4);
    RecordAutoExposure(cmd, frame, imageIndex);
    if (m_taaActive)
        RecordTemporalAA(cmd, frame, imageIndex);
    RecordPost(cmd, frame, imageIndex);
    RecordDebugOverlay(cmd, frame, imageIndex);
    if (recordGpuTiming)
    {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                             m_timestampQueryPool, timestampBase + 5);
        m_timestampFrameWritten[m_currentFrame] = true;
        m_timestampLogShadows[m_currentFrame] = scene.Shadows.LogPerformance;
        m_timestampLogPost[m_currentFrame] = scene.PostProcess.LogPerformance;
        m_timestampAaMode[m_currentFrame] = scene.PostProcess.AntiAliasing;
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

    vkEndCommandBuffer(cmd);
    commandRecordingScope.End();

    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("SubmitAndPresent", "Renderer/Vulkan/Sync");
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailable[m_currentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinished[m_currentFrame];

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to submit rendered frame");

    if (takeScreenshot)
    {
        vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
        SaveScreenshot(screenshotBuffer, m_screenshotPath);
        m_resources.Destroy(screenshotBuffer);
        m_screenshotPath.clear();
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
    for (const MeshInstance& instance : scene.Instances())
        if (instance.TemporalId != 0)
            m_previousTransforms[instance.TemporalId] = instance.Transform;

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}

void VulkanRenderBackend::Shutdown()
{
    if (m_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_device);

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
