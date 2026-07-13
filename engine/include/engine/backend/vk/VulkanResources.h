#pragma once

#include <filesystem>
#include <initializer_list>

#include <vulkan/vulkan.h>

namespace engine::vulkan {

struct Buffer
{
    VkBuffer Handle = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
};

struct Image
{
    VkImage Handle = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkExtent3D Extent{};
    uint32_t MipLevels = 1;
    uint32_t ArrayLayers = 1;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
};

// Small Vulkan-only allocation helper. It deliberately does not leak into
// Scene/Mesh/Material: the backend remains responsible for GPU ownership and
// pointer-keyed resource caches, just like the OpenGL implementation.
class ResourceAllocator
{
public:
    void Init(VkPhysicalDevice physicalDevice, VkDevice device);

    Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags memoryProperties) const;
    Image CreateImage2D(uint32_t width, uint32_t height, VkFormat format,
                        VkImageUsageFlags usage, VkImageAspectFlags aspect,
                        uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                        VkImageCreateFlags flags = 0,
                        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) const;
    Image CreateImage3D(uint32_t width, uint32_t height, uint32_t depth,
                        VkFormat format, VkImageUsageFlags usage,
                        VkImageAspectFlags aspect) const;

    void Destroy(Buffer& buffer) const;
    void Destroy(Image& image) const;

    VkFormat FindSupportedFormat(std::initializer_list<VkFormat> candidates,
                                 VkImageTiling tiling,
                                 VkFormatFeatureFlags features) const;

private:
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
};

VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path);

} // namespace engine::vulkan
