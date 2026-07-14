#include "engine/backend/vk/VulkanResources.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine::vulkan {

namespace {

void RaisePeak(std::atomic<uint64_t>& peak, uint64_t value)
{
    uint64_t previous = peak.load(std::memory_order_relaxed);
    while (previous < value &&
           !peak.compare_exchange_weak(previous, value, std::memory_order_relaxed))
    {
    }
}

} // namespace

void ResourceAllocator::Init(VkPhysicalDevice physicalDevice, VkDevice device,
                             uint32_t graphicsQueueFamily, uint32_t transferQueueFamily)
{
    m_physicalDevice = physicalDevice;
    m_device = device;
    m_graphicsQueueFamily = graphicsQueueFamily;
    m_transferQueueFamily = transferQueueFamily;
    m_setDebugObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
}

namespace
{

void SetObjectName(VkDevice device, PFN_vkSetDebugUtilsObjectNameEXT setName,
                   VkObjectType type, uint64_t handle, std::string_view name)
{
    if (setName == nullptr || handle == 0 || name.empty())
        return;
    const std::string ownedName(name);
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = ownedName.c_str();
    setName(device, &info);
}

} // namespace

void ResourceAllocator::SetDebugName(const Buffer& buffer, std::string_view name) const
{
    SetObjectName(m_device, m_setDebugObjectName, VK_OBJECT_TYPE_BUFFER,
                  reinterpret_cast<uint64_t>(buffer.Handle), name);
    SetObjectName(m_device, m_setDebugObjectName, VK_OBJECT_TYPE_DEVICE_MEMORY,
                  reinterpret_cast<uint64_t>(buffer.Memory), std::string(name) + " Memory");
}

void ResourceAllocator::SetDebugName(const Image& image, std::string_view name) const
{
    SetObjectName(m_device, m_setDebugObjectName, VK_OBJECT_TYPE_IMAGE,
                  reinterpret_cast<uint64_t>(image.Handle), name);
    SetObjectName(m_device, m_setDebugObjectName, VK_OBJECT_TYPE_IMAGE_VIEW,
                  reinterpret_cast<uint64_t>(image.View), std::string(name) + " View");
    SetObjectName(m_device, m_setDebugObjectName, VK_OBJECT_TYPE_DEVICE_MEMORY,
                  reinterpret_cast<uint64_t>(image.Memory), std::string(name) + " Memory");
}

uint32_t ResourceAllocator::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        const bool acceptedType = (typeFilter & (1u << i)) != 0;
        const bool acceptedProperties =
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (acceptedType && acceptedProperties)
            return i;
    }
    throw std::runtime_error("Vulkan: no compatible GPU memory type found");
}

Buffer ResourceAllocator::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                       VkMemoryPropertyFlags memoryProperties) const
{
    if (m_device == VK_NULL_HANDLE || size == 0)
        throw std::runtime_error("Vulkan: invalid buffer allocation request");

    Buffer buffer;
    buffer.Size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    const uint32_t queueFamilies[] = {m_graphicsQueueFamily, m_transferQueueFamily};
    if (m_graphicsQueueFamily != VK_QUEUE_FAMILY_IGNORED &&
        m_transferQueueFamily != VK_QUEUE_FAMILY_IGNORED &&
        m_graphicsQueueFamily != m_transferQueueFamily &&
        (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) != 0)
    {
        bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bufferInfo.queueFamilyIndexCount = 2;
        bufferInfo.pQueueFamilyIndices = queueFamilies;
    }
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer.Handle) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, buffer.Handle, &requirements);

    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, memoryProperties);
    buffer.AllocationSize = requirements.size;
    buffer.DeviceLocal = (memoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
    if (vkAllocateMemory(m_device, &allocationInfo, nullptr, &buffer.Memory) != VK_SUCCESS)
    {
        vkDestroyBuffer(m_device, buffer.Handle, nullptr);
        throw std::runtime_error("Vulkan: failed to allocate buffer memory");
    }

    if (vkBindBufferMemory(m_device, buffer.Handle, buffer.Memory, 0) != VK_SUCCESS)
    {
        Destroy(buffer);
        throw std::runtime_error("Vulkan: failed to bind buffer memory");
    }
    TrackAllocation(static_cast<uint64_t>(buffer.AllocationSize), buffer.DeviceLocal);
    buffer.AllocationTracked = true;
    return buffer;
}

Image ResourceAllocator::CreateImage2D(uint32_t width, uint32_t height, VkFormat format,
                                       VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                       uint32_t mipLevels, uint32_t arrayLayers,
                                       VkImageCreateFlags flags,
                                       VkSampleCountFlagBits samples) const
{
    if (m_device == VK_NULL_HANDLE || width == 0 || height == 0 || mipLevels == 0 || arrayLayers == 0)
        throw std::runtime_error("Vulkan: invalid image allocation request");

    Image image;
    image.Format = format;
    image.Extent = {width, height, 1};
    image.MipLevels = mipLevels;
    image.ArrayLayers = arrayLayers;
    image.Samples = samples;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = flags;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = image.Extent;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.samples = samples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    const uint32_t queueFamilies[] = {m_graphicsQueueFamily, m_transferQueueFamily};
    if (m_graphicsQueueFamily != VK_QUEUE_FAMILY_IGNORED &&
        m_transferQueueFamily != VK_QUEUE_FAMILY_IGNORED &&
        m_graphicsQueueFamily != m_transferQueueFamily &&
        (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) != 0)
    {
        imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        imageInfo.queueFamilyIndexCount = 2;
        imageInfo.pQueueFamilyIndices = queueFamilies;
    }
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &image.Handle) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create image");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, image.Handle, &requirements);

    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &allocationInfo, nullptr, &image.Memory) != VK_SUCCESS)
    {
        vkDestroyImage(m_device, image.Handle, nullptr);
        throw std::runtime_error("Vulkan: failed to allocate image memory");
    }
    if (vkBindImageMemory(m_device, image.Handle, image.Memory, 0) != VK_SUCCESS)
    {
        Destroy(image);
        throw std::runtime_error("Vulkan: failed to bind image memory");
    }
    image.AllocationSize = requirements.size;
    TrackAllocation(static_cast<uint64_t>(image.AllocationSize), true);
    image.AllocationTracked = true;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.Handle;
    viewInfo.viewType = arrayLayers == 6 && (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
        ? VK_IMAGE_VIEW_TYPE_CUBE
        : (arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = arrayLayers;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &image.View) != VK_SUCCESS)
    {
        Destroy(image);
        throw std::runtime_error("Vulkan: failed to create image view");
    }
    return image;
}

Image ResourceAllocator::CreateImage3D(uint32_t width, uint32_t height, uint32_t depth,
                                       VkFormat format, VkImageUsageFlags usage,
                                       VkImageAspectFlags aspect) const
{
    if (m_device == VK_NULL_HANDLE || width == 0 || height == 0 || depth == 0)
        throw std::runtime_error("Vulkan: invalid 3D image allocation request");

    Image image;
    image.Format = format;
    image.Extent = {width, height, depth};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = format;
    imageInfo.extent = image.Extent;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &image.Handle) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create 3D image");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, image.Handle, &requirements);
    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &allocationInfo, nullptr, &image.Memory) != VK_SUCCESS)
    {
        vkDestroyImage(m_device, image.Handle, nullptr);
        throw std::runtime_error("Vulkan: failed to allocate 3D image memory");
    }
    if (vkBindImageMemory(m_device, image.Handle, image.Memory, 0) != VK_SUCCESS)
    {
        Destroy(image);
        throw std::runtime_error("Vulkan: failed to bind 3D image memory");
    }
    image.AllocationSize = requirements.size;
    TrackAllocation(static_cast<uint64_t>(image.AllocationSize), true);
    image.AllocationTracked = true;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.Handle;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &image.View) != VK_SUCCESS)
    {
        Destroy(image);
        throw std::runtime_error("Vulkan: failed to create 3D image view");
    }
    return image;
}

void ResourceAllocator::Destroy(Buffer& buffer) const
{
    if (buffer.AllocationTracked)
        TrackFree(static_cast<uint64_t>(buffer.AllocationSize), buffer.DeviceLocal);
    if (buffer.Handle != VK_NULL_HANDLE)
        vkDestroyBuffer(m_device, buffer.Handle, nullptr);
    if (buffer.Memory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, buffer.Memory, nullptr);
    buffer = {};
}

void ResourceAllocator::Destroy(Image& image) const
{
    if (image.AllocationTracked)
        TrackFree(static_cast<uint64_t>(image.AllocationSize), image.DeviceLocal);
    if (image.View != VK_NULL_HANDLE)
        vkDestroyImageView(m_device, image.View, nullptr);
    if (image.Handle != VK_NULL_HANDLE)
        vkDestroyImage(m_device, image.Handle, nullptr);
    if (image.Memory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, image.Memory, nullptr);
    image = {};
}

void ResourceAllocator::TrackAllocation(uint64_t bytes, bool deviceLocal) const
{
    std::atomic<uint64_t>& current = deviceLocal ? m_deviceLocalBytes : m_hostVisibleBytes;
    std::atomic<uint64_t>& peak = deviceLocal ? m_peakDeviceLocalBytes : m_peakHostVisibleBytes;
    const uint64_t value = current.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    RaisePeak(peak, value);
    m_allocationCount.fetch_add(1, std::memory_order_relaxed);
}

void ResourceAllocator::TrackFree(uint64_t bytes, bool deviceLocal) const
{
    std::atomic<uint64_t>& current = deviceLocal ? m_deviceLocalBytes : m_hostVisibleBytes;
    current.fetch_sub(bytes, std::memory_order_relaxed);
    m_allocationCount.fetch_sub(1, std::memory_order_relaxed);
}

ResourceMemoryStats ResourceAllocator::MemoryStats() const
{
    ResourceMemoryStats result;
    result.DeviceLocalBytes = m_deviceLocalBytes.load(std::memory_order_relaxed);
    result.PeakDeviceLocalBytes = m_peakDeviceLocalBytes.load(std::memory_order_relaxed);
    result.HostVisibleBytes = m_hostVisibleBytes.load(std::memory_order_relaxed);
    result.PeakHostVisibleBytes = m_peakHostVisibleBytes.load(std::memory_order_relaxed);
    result.AllocationCount = m_allocationCount.load(std::memory_order_relaxed);
    return result;
}

VkFormat ResourceAllocator::FindSupportedFormat(std::initializer_list<VkFormat> candidates,
                                                VkImageTiling tiling,
                                                VkFormatFeatureFlags features) const
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
        const VkFormatFeatureFlags supported = tiling == VK_IMAGE_TILING_LINEAR
            ? properties.linearTilingFeatures
            : properties.optimalTilingFeatures;
        if ((supported & features) == features)
            return format;
    }
    throw std::runtime_error("Vulkan: no supported image format found");
}

VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file)
        throw std::runtime_error("Vulkan: cannot open SPIR-V shader: " + path.string());

    const std::streamsize byteCount = file.tellg();
    if (byteCount <= 0 || byteCount % 4 != 0)
        throw std::runtime_error("Vulkan: invalid SPIR-V byte size: " + path.string());

    std::vector<uint32_t> words(static_cast<size_t>(byteCount) / sizeof(uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), byteCount))
        throw std::runtime_error("Vulkan: failed to read SPIR-V shader: " + path.string());
    if (words.empty() || words[0] != 0x07230203u)
        throw std::runtime_error("Vulkan: invalid SPIR-V magic number: " + path.string());

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = static_cast<size_t>(byteCount);
    createInfo.pCode = words.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create shader module: " + path.string());
    return module;
}

} // namespace engine::vulkan
