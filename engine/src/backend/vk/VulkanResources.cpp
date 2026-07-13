#include "engine/backend/vk/VulkanResources.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine::vulkan {

void ResourceAllocator::Init(VkPhysicalDevice physicalDevice, VkDevice device)
{
    m_physicalDevice = physicalDevice;
    m_device = device;
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
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer.Handle) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, buffer.Handle, &requirements);

    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, memoryProperties);
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
    if (buffer.Handle != VK_NULL_HANDLE)
        vkDestroyBuffer(m_device, buffer.Handle, nullptr);
    if (buffer.Memory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, buffer.Memory, nullptr);
    buffer = {};
}

void ResourceAllocator::Destroy(Image& image) const
{
    if (image.View != VK_NULL_HANDLE)
        vkDestroyImageView(m_device, image.View, nullptr);
    if (image.Handle != VK_NULL_HANDLE)
        vkDestroyImage(m_device, image.Handle, nullptr);
    if (image.Memory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, image.Memory, nullptr);
    image = {};
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
