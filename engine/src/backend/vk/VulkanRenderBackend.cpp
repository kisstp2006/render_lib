#include "engine/backend/vk/VulkanRenderBackend.h"
#include "engine/core/Log.h"
#include "engine/core/Window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace engine {

namespace {

const std::vector<const char*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> kDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif

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

} // namespace

void VulkanRenderBackend::Init(Window& window)
{
    m_window = &window;

    CreateInstance();
    SetupDebugMessenger();
    CreateSurface(window);
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapchain(window.Width(), window.Height());
    CreateImageViews();
    CreateCommandObjects();
    CreateSyncObjects();

    log::Info("Vulkan backend initialized (swapchain-clear only; PBR pipeline is TODO)");
}

void VulkanRenderBackend::CreateInstance()
{
    if (kEnableValidation && !CheckValidationLayerSupport())
        log::Warn("Validation layers requested but not available - continuing without them");

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

    const bool useValidation = kEnableValidation && CheckValidationLayerSupport();
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
    if (!kEnableValidation || !CheckValidationLayerSupport())
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

    for (VkPhysicalDevice device : devices)
    {
        if (IsDeviceSuitable(device))
        {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("No suitable Vulkan GPU found");

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
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

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
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

    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR; // always available, vsync
    for (const auto& mode : presentModes)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            chosenPresentMode = mode;
            break;
        }
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
    DestroySwapchain();
    CreateSwapchain(width, height);
    CreateImageViews();
}

void VulkanRenderBackend::Resize(int width, int height)
{
    RecreateSwapchain(width, height);
}

void VulkanRenderBackend::RenderFrame(const Scene& /*scene*/, const Camera& /*camera*/)
{
    // TODO: this only proves the swapchain round-trip works (acquire -> clear
    // -> present). The PBR pipeline (matching GLRenderBackend/pbr.frag) needs
    // pipeline/descriptor set layout, SPIR-V shader modules and a depth
    // buffer + shadow pass before scene geometry can be drawn here.
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain(m_window->Width(), m_window->Height());
        return;
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toClear{};
    toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.image = m_swapchainImages[imageIndex];
    toClear.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toClear);

    VkClearColorValue clearColor{{0.05f, 0.06f, 0.08f, 1.0f}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, m_swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

    VkImageMemoryBarrier toPresent = toClear;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailable[m_currentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinished[m_currentFrame];

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);

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

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}

void VulkanRenderBackend::Shutdown()
{
    if (m_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_device);

    for (int i = 0; i < kFramesInFlight; ++i)
    {
        if (i < static_cast<int>(m_imageAvailable.size())) vkDestroySemaphore(m_device, m_imageAvailable[i], nullptr);
        if (i < static_cast<int>(m_renderFinished.size())) vkDestroySemaphore(m_device, m_renderFinished[i], nullptr);
        if (i < static_cast<int>(m_inFlightFences.size())) vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    if (m_commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);

    DestroySwapchain();

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
