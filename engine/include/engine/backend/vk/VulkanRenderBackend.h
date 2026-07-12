#pragma once

#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/backend/IRenderBackend.h"

namespace engine {

// Vulkan backend scaffold: brings up instance/debug messenger/physical+logical
// device/surface/swapchain and stops there. RenderFrame currently just clears
// the swapchain image - the PBR pipeline (shaders, descriptor layout,
// pipeline state, per-frame sync) is the next slice of work, mirroring what
// GLRenderBackend already does. Kept as a separate concrete class (not
// forced through a generic RHI) since Vulkan's resource/sync model is too
// different from GL's to share code productively at this stage; the shared
// surface is engine/scene (Mesh, Material, Scene) plus IRenderBackend.
class VulkanRenderBackend final : public IRenderBackend
{
public:
    void Init(Window& window) override;
    void Shutdown() override;
    void Resize(int width, int height) override;
    void RenderFrame(const Scene& scene, const Camera& camera) override;
    const char* Name() const override { return "Vulkan (WIP - swapchain clear only)"; }

private:
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> Graphics;
        std::optional<uint32_t> Present;
        bool IsComplete() const { return Graphics.has_value() && Present.has_value(); }
    };

    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface(Window& window);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain(int width, int height);
    void CreateImageViews();
    void CreateSyncObjects();
    void CreateCommandObjects();
    void RecreateSwapchain(int width, int height);
    void DestroySwapchain();

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    bool IsDeviceSuitable(VkPhysicalDevice device) const;

    Window* m_window = nullptr;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent{};
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    static constexpr int kFramesInFlight = 2;
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;
};

} // namespace engine
