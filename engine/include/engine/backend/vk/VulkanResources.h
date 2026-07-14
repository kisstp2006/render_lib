#pragma once

#include <filesystem>
#include <initializer_list>
#include <atomic>
#include <cstdint>
#include <string_view>

#include <vulkan/vulkan.h>

namespace engine::vulkan {

struct Buffer
{
    VkBuffer Handle = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    VkDeviceSize AllocationSize = 0;
    bool DeviceLocal = false;
    bool AllocationTracked = false;
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
    VkDeviceSize AllocationSize = 0;
    bool DeviceLocal = true;
    bool AllocationTracked = false;
};

struct ResourceMemoryStats
{
    uint64_t DeviceLocalBytes = 0;
    uint64_t PeakDeviceLocalBytes = 0;
    uint64_t HostVisibleBytes = 0;
    uint64_t PeakHostVisibleBytes = 0;
    uint64_t AllocationCount = 0;
};

// Small Vulkan-only allocation helper. It deliberately does not leak into
// Scene/Mesh/Material: the backend remains responsible for GPU ownership and
// pointer-keyed resource caches, just like the OpenGL implementation.
class ResourceAllocator
{
public:
    void Init(VkPhysicalDevice physicalDevice, VkDevice device,
              uint32_t graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED,
              uint32_t transferQueueFamily = VK_QUEUE_FAMILY_IGNORED);

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

    void SetDebugName(const Buffer& buffer, std::string_view name) const;
    void SetDebugName(const Image& image, std::string_view name) const;

    VkFormat FindSupportedFormat(std::initializer_list<VkFormat> candidates,
                                 VkImageTiling tiling,
                                 VkFormatFeatureFlags features) const;
    ResourceMemoryStats MemoryStats() const;

private:
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void TrackAllocation(uint64_t bytes, bool deviceLocal) const;
    void TrackFree(uint64_t bytes, bool deviceLocal) const;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT m_setDebugObjectName = nullptr;
    uint32_t m_graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t m_transferQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    mutable std::atomic<uint64_t> m_deviceLocalBytes{0};
    mutable std::atomic<uint64_t> m_peakDeviceLocalBytes{0};
    mutable std::atomic<uint64_t> m_hostVisibleBytes{0};
    mutable std::atomic<uint64_t> m_peakHostVisibleBytes{0};
    mutable std::atomic<uint64_t> m_allocationCount{0};
};

VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path);

} // namespace engine::vulkan
