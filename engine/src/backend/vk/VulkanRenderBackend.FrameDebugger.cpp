#include "engine/backend/vk/VulkanRenderBackend.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace engine {
namespace {

using debug::FrameDebugId;
using debug::FrameDebugResource;
using debug::FrameDebugResourceKind;
using debug::FrameDebugVisualization;

constexpr uint64_t kEnvironment = FrameDebugId("ibl.environment");
constexpr uint64_t kIrradiance = FrameDebugId("ibl.irradiance");
constexpr uint64_t kPrefilter = FrameDebugId("ibl.prefilter");
constexpr uint64_t kBrdf = FrameDebugId("ibl.brdf");
constexpr uint64_t kLocalShadow = FrameDebugId("shadow.local");
constexpr uint64_t kPointShadow = FrameDebugId("shadow.point");
constexpr uint64_t kCookies = FrameDebugId("light.cookies");
constexpr uint64_t kHdr = FrameDebugId("scene.hdr");
constexpr uint64_t kVelocity = FrameDebugId("scene.velocity");
constexpr uint64_t kDepth = FrameDebugId("scene.depth");
constexpr uint64_t kPostLdr = FrameDebugId("post.ldr");
constexpr std::array<uint64_t, 4> kDirectionalShadows{
    FrameDebugId("shadow.directional.0"), FrameDebugId("shadow.directional.1"),
    FrameDebugId("shadow.directional.2"), FrameDebugId("shadow.directional.3")};
constexpr std::array<uint64_t, 2> kTaaColor{
    FrameDebugId("taa.color.0"), FrameDebugId("taa.color.1")};
constexpr std::array<uint64_t, 2> kTaaDepth{
    FrameDebugId("taa.depth.0"), FrameDebugId("taa.depth.1")};
constexpr std::array<uint64_t, 6> kBloom{
    FrameDebugId("bloom.0"), FrameDebugId("bloom.1"), FrameDebugId("bloom.2"),
    FrameDebugId("bloom.3"), FrameDebugId("bloom.4"), FrameDebugId("bloom.5")};

const char* FormatName(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8_UNORM: return "R8 UNORM";
    case VK_FORMAT_R8G8B8A8_UNORM: return "RGBA8 UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "RGBA8 SRGB";
    case VK_FORMAT_R16G16_SFLOAT: return "RG16F";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "RGBA16F";
    case VK_FORMAT_R32_SFLOAT: return "R32F";
    case VK_FORMAT_D32_SFLOAT: return "D32F";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32F S8";
    case VK_FORMAT_D24_UNORM_S8_UINT: return "D24 S8";
    default: return "UNKNOWN";
    }
}

uint32_t SampleCount(VkSampleCountFlagBits samples)
{
    return static_cast<uint32_t>(samples);
}

FrameDebugResource Resource(uint64_t id, std::string name, const vulkan::Image& image,
                            FrameDebugVisualization visualization,
                            bool previewable = true,
                            FrameDebugResourceKind forcedKind = FrameDebugResourceKind::Texture2D)
{
    FrameDebugResource resource;
    resource.Id = id;
    resource.Name = std::move(name);
    resource.Kind = forcedKind;
    resource.Visualization = visualization;
    resource.Width = image.Extent.width;
    resource.Height = image.Extent.height;
    resource.Layers = image.ArrayLayers;
    resource.MipLevels = image.MipLevels;
    resource.Samples = SampleCount(image.Samples);
    resource.Format = FormatName(image.Format);
    resource.EstimatedBytes = image.AllocationSize;
    resource.Previewable = previewable;
    return resource;
}

uint32_t PixelBytes(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8_UNORM: return 1;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT: return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    default: return 0;
    }
}

float HalfToFloat(uint16_t half)
{
    const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16;
    uint32_t exponent = (half >> 10) & 0x1fu;
    uint32_t mantissa = half & 0x03ffu;
    uint32_t bits = 0;
    if (exponent == 0)
    {
        if (mantissa == 0)
            bits = sign;
        else
        {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x0400u) == 0)
            {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x03ffu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    }
    else if (exponent == 31)
        bits = sign | 0x7f800000u | (mantissa << 13);
    else
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    return std::bit_cast<float>(bits);
}

template <typename T>
T Read(const uint8_t* data)
{
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

float DisplayColor(float value)
{
    value = std::max(value, 0.0f);
    return std::pow(value / (1.0f + value), 1.0f / 2.2f);
}

uint8_t Byte(float value)
{
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct NativeResource
{
    const vulkan::Image* Image = nullptr;
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    FrameDebugVisualization Visualization = FrameDebugVisualization::Color;
};

VkImageMemoryBarrier2 ImageBarrier(const NativeResource& resource, VkImageLayout oldLayout,
                                   VkImageLayout newLayout, uint32_t mipLevel,
                                   uint32_t layer, VkPipelineStageFlags2 sourceStage,
                                   VkAccessFlags2 sourceAccess,
                                   VkPipelineStageFlags2 destinationStage,
                                   VkAccessFlags2 destinationAccess)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource.Image->Handle;
    barrier.subresourceRange = {resource.Aspect, mipLevel, 1, layer, 1};
    return barrier;
}

} // namespace

debug::FrameDebugSnapshot VulkanRenderBackend::GetFrameDebugSnapshot() const
{
    debug::FrameDebugSnapshot snapshot;
    snapshot.BackendName = Name();
    snapshot.FrameIndex = m_gpuProfileFrameIndex;
    if (m_hdrImages.empty() || m_depthImages.empty())
        return snapshot;
    const uint32_t imageIndex = std::min(
        m_hasFrameDebugFrame ? m_lastFrameDebugImageIndex : 0u,
        static_cast<uint32_t>(m_hdrImages.size() - 1));
    const uint32_t frameSlot = m_hasFrameDebugFrame ? m_lastFrameDebugFrameSlot : 0u;
    auto& resources = snapshot.Resources;
    resources.reserve(28);

    resources.push_back(Resource(kEnvironment, "IBL Environment Cubemap", m_environmentCube,
        FrameDebugVisualization::Color, m_hasFrameDebugFrame && m_environmentImagesInitialized,
        FrameDebugResourceKind::TextureCube));
    resources.push_back(Resource(kIrradiance, "IBL Irradiance Cubemap", m_irradianceCube,
        FrameDebugVisualization::Color, m_hasFrameDebugFrame && m_environmentImagesInitialized,
        FrameDebugResourceKind::TextureCube));
    resources.push_back(Resource(kPrefilter, "IBL GGX Prefilter Cubemap", m_prefilterCube,
        FrameDebugVisualization::Color, m_hasFrameDebugFrame && m_environmentImagesInitialized,
        FrameDebugResourceKind::TextureCube));
    resources.push_back(Resource(kBrdf, "IBL BRDF LUT", m_brdfLut,
        FrameDebugVisualization::Color, m_hasFrameDebugFrame));
    for (size_t cascade = 0; cascade < kDirectionalShadows.size(); ++cascade)
        resources.push_back(Resource(kDirectionalShadows[cascade],
            "Directional Shadow Cascade " + std::to_string(cascade),
            m_shadowMaps[frameSlot][cascade], FrameDebugVisualization::Depth,
            m_hasFrameDebugFrame));
    resources.push_back(Resource(kLocalShadow, "Spot + Area Shadow Atlas", m_localShadowAtlas,
        FrameDebugVisualization::Depth, m_hasFrameDebugFrame));
    resources.push_back(Resource(kPointShadow, "Point Shadow Cube Array", m_pointShadowArray,
        FrameDebugVisualization::Depth, m_hasFrameDebugFrame,
        FrameDebugResourceKind::TextureCubeArray));
    resources.push_back(Resource(kCookies, "Local Light Cookie Atlas", m_cookieAtlas,
        FrameDebugVisualization::SingleChannel, m_hasFrameDebugFrame));
    resources.push_back(Resource(FrameDebugId("scene.msaa.hdr"), "Main HDR MSAA Color",
        m_msaaHdrImages[frameSlot], FrameDebugVisualization::Color, false));
    resources.push_back(Resource(FrameDebugId("scene.msaa.velocity"), "Main HDR MSAA Velocity",
        m_msaaVelocityImages[frameSlot], FrameDebugVisualization::Velocity, false));
    resources.push_back(Resource(FrameDebugId("scene.msaa.depth"), "Main HDR MSAA Depth",
        m_msaaDepthImages[frameSlot], FrameDebugVisualization::Depth, false));
    resources.push_back(Resource(kHdr, "Main HDR Resolved Color", m_hdrImages[imageIndex],
        FrameDebugVisualization::Color, m_hasFrameDebugFrame));
    resources.push_back(Resource(kVelocity, "Main HDR Velocity", m_velocityImages[imageIndex],
        FrameDebugVisualization::Velocity, m_hasFrameDebugFrame));
    resources.push_back(Resource(kDepth, "Main HDR Depth", m_depthImages[imageIndex],
        FrameDebugVisualization::Depth, m_hasFrameDebugFrame));
    for (size_t history = 0; history < 2; ++history)
    {
        resources.push_back(Resource(kTaaColor[history],
            "TAA History Color " + std::to_string(history), m_taaHistoryColor[history],
            FrameDebugVisualization::Color, m_taaHistoryValid));
        resources.push_back(Resource(kTaaDepth[history],
            "TAA History Depth " + std::to_string(history), m_taaHistoryDepth[history],
            FrameDebugVisualization::Depth, m_taaHistoryValid));
    }
    for (size_t level = 0; level < m_bloomChains[imageIndex].Levels.size(); ++level)
        resources.push_back(Resource(kBloom[level], "Bloom Level " + std::to_string(level),
            m_bloomChains[imageIndex].Levels[level], FrameDebugVisualization::Color,
            m_hasFrameDebugFrame && m_lastFrameDebugPostEnabled));
    resources.push_back(Resource(kPostLdr, "Post Tonemap LDR", m_ldrImages[imageIndex],
        FrameDebugVisualization::Color, m_hasFrameDebugFrame && m_lastFrameDebugFxaaActive));

    snapshot.Passes.push_back({"Environment / IBL Update", {},
        {kEnvironment, kIrradiance, kPrefilter, kBrdf}});
    snapshot.Passes.push_back({"Shadows/Directional", {},
        std::vector<uint64_t>(kDirectionalShadows.begin(), kDirectionalShadows.end())});
    snapshot.Passes.push_back({"Shadows/Local lights", {kCookies},
        {kLocalShadow, kPointShadow}});
    std::vector<uint64_t> mainInputs(kDirectionalShadows.begin(), kDirectionalShadows.end());
    mainInputs.insert(mainInputs.end(), {kLocalShadow, kPointShadow, kCookies,
        kIrradiance, kPrefilter, kBrdf});
    snapshot.Passes.push_back({"Main HDR", std::move(mainInputs), {kHdr, kVelocity, kDepth}});
    std::vector<uint64_t> postInputs{kHdr, kVelocity, kDepth, kTaaColor[0], kTaaColor[1],
                                     kTaaDepth[0], kTaaDepth[1]};
    std::vector<uint64_t> postOutputs{kPostLdr, kTaaColor[0], kTaaColor[1],
                                      kTaaDepth[0], kTaaDepth[1]};
    postOutputs.insert(postOutputs.end(), kBloom.begin(), kBloom.end());
    snapshot.Passes.push_back({"Post process", std::move(postInputs), std::move(postOutputs)});
    snapshot.Passes.push_back({"Debug UI", {kPostLdr}, {}});
    return snapshot;
}

bool VulkanRenderBackend::CaptureFrameDebugResource(uint64_t resourceId, uint32_t mipLevel,
                                                    uint32_t layer,
                                                    debug::FrameDebugPreview& preview)
{
    if (!m_hasFrameDebugFrame || m_hdrImages.empty())
    {
        preview.Error = "Vulkan has no completed frame to inspect yet";
        return false;
    }
    const uint32_t imageIndex = std::min(m_lastFrameDebugImageIndex,
                                         static_cast<uint32_t>(m_hdrImages.size() - 1));
    NativeResource native;
    if (resourceId == kEnvironment)
        native = {&m_environmentCube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    else if (resourceId == kIrradiance)
        native = {&m_irradianceCube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    else if (resourceId == kPrefilter)
        native = {&m_prefilterCube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    else if (resourceId == kBrdf)
        native = {&m_brdfLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    else if (resourceId == kLocalShadow)
        native = {&m_localShadowAtlas, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                  VK_IMAGE_ASPECT_DEPTH_BIT, FrameDebugVisualization::Depth};
    else if (resourceId == kPointShadow)
        native = {&m_pointShadowArray, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                  VK_IMAGE_ASPECT_DEPTH_BIT, FrameDebugVisualization::Depth};
    else if (resourceId == kCookies)
        native = {&m_cookieAtlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_ASPECT_COLOR_BIT, FrameDebugVisualization::SingleChannel};
    else if (resourceId == kHdr)
        native = {&m_hdrImages[imageIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    else if (resourceId == kVelocity)
        native = {&m_velocityImages[imageIndex],
                  m_lastFrameDebugTaaActive ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_ASPECT_COLOR_BIT, FrameDebugVisualization::Velocity};
    else if (resourceId == kDepth)
        native = {&m_depthImages[imageIndex],
                  m_lastFrameDebugTaaActive ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_ASPECT_DEPTH_BIT, FrameDebugVisualization::Depth};
    else if (resourceId == kPostLdr && m_lastFrameDebugFxaaActive)
        native = {&m_ldrImages[imageIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    else
    {
        for (size_t index = 0; index < kDirectionalShadows.size(); ++index)
            if (resourceId == kDirectionalShadows[index])
                native = {&m_shadowMaps[m_lastFrameDebugFrameSlot][index],
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                          VK_IMAGE_ASPECT_DEPTH_BIT, FrameDebugVisualization::Depth};
        for (size_t index = 0; index < 2; ++index)
        {
            if (resourceId == kTaaColor[index] && m_taaHistoryValid)
                native = {&m_taaHistoryColor[index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            if (resourceId == kTaaDepth[index] && m_taaHistoryValid)
                native = {&m_taaHistoryDepth[index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_ASPECT_COLOR_BIT, FrameDebugVisualization::Depth};
        }
        if (m_lastFrameDebugPostEnabled)
            for (size_t index = 0; index < kBloom.size(); ++index)
                if (resourceId == kBloom[index])
                    native = {&m_bloomChains[imageIndex].Levels[index],
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    if (!native.Image || native.Image->Handle == VK_NULL_HANDLE ||
        mipLevel >= native.Image->MipLevels || layer >= native.Image->ArrayLayers)
    {
        preview.Error = "Vulkan resource or subresource is unavailable";
        return false;
    }
    const uint32_t pixelBytes = PixelBytes(native.Image->Format);
    if (pixelBytes == 0)
    {
        preview.Error = "Vulkan preview does not support format " +
                        std::string(FormatName(native.Image->Format));
        return false;
    }
    const uint32_t sourceWidth = std::max(native.Image->Extent.width >> mipLevel, 1u);
    const uint32_t sourceHeight = std::max(native.Image->Extent.height >> mipLevel, 1u);
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(sourceWidth) * sourceHeight * pixelBytes;
    auto readbackFound = std::find_if(m_frameDebugReadbackBuffers.begin(),
        m_frameDebugReadbackBuffers.end(), [byteCount](const vulkan::Buffer& buffer)
        { return buffer.Size >= byteCount; });
    if (readbackFound == m_frameDebugReadbackBuffers.end())
    {
        m_frameDebugReadbackBuffers.push_back(m_resources.CreateBuffer(
            byteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        readbackFound = std::prev(m_frameDebugReadbackBuffers.end());
        m_resources.SetDebugName(*readbackFound, "Frame Debugger Readback " +
            std::to_string(m_frameDebugReadbackBuffers.size() - 1));
    }
    vulkan::Buffer& readback = *readbackFound;

    vkDeviceWaitIdle(m_device);
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    VkImageMemoryBarrier2 toTransfer = ImageBarrier(native, native.Layout,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mipLevel, layer,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toTransfer;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {native.Aspect, mipLevel, layer, 1};
    copy.imageExtent = {sourceWidth, sourceHeight, 1};
    vkCmdCopyImageToBuffer(commandBuffer, native.Image->Handle,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.Handle, 1, &copy);
    VkImageMemoryBarrier2 restore = ImageBarrier(native, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        native.Layout, mipLevel, layer, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT);
    VkBufferMemoryBarrier2 hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = readback.Handle;
    hostBarrier.offset = 0;
    hostBarrier.size = byteCount;
    dependency.pImageMemoryBarriers = &restore;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &hostBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    EndImmediateCommands(commandBuffer);

    const uint8_t* source = nullptr;
    void* mapped = nullptr;
    if (vkMapMemory(m_device, readback.Memory, 0, byteCount, 0, &mapped) != VK_SUCCESS)
    {
        preview.Error = "Vulkan frame-debugger readback mapping failed";
        return false;
    }
    source = static_cast<const uint8_t*>(mapped);
    constexpr uint32_t maxWidth = 582;
    constexpr uint32_t maxHeight = 246;
    const float scale = std::min(1.0f, std::min(static_cast<float>(maxWidth) / sourceWidth,
                                               static_cast<float>(maxHeight) / sourceHeight));
    preview.ResourceId = resourceId;
    preview.SourceWidth = sourceWidth;
    preview.SourceHeight = sourceHeight;
    preview.Width = std::max(1u, static_cast<uint32_t>(sourceWidth * scale));
    preview.Height = std::max(1u, static_cast<uint32_t>(sourceHeight * scale));
    preview.MipLevel = mipLevel;
    preview.Layer = layer;
    preview.Pixels.resize(static_cast<size_t>(preview.Width) * preview.Height * 4);
    for (uint32_t y = 0; y < preview.Height; ++y)
    for (uint32_t x = 0; x < preview.Width; ++x)
    {
        const uint32_t sx = std::min(x * sourceWidth / preview.Width, sourceWidth - 1);
        const uint32_t sy = std::min(y * sourceHeight / preview.Height, sourceHeight - 1);
        const uint8_t* pixel = source +
            (static_cast<size_t>(sy) * sourceWidth + sx) * pixelBytes;
        float red = 0.0f, green = 0.0f, blue = 0.0f;
        switch (native.Image->Format)
        {
        case VK_FORMAT_R8_UNORM:
            red = green = blue = pixel[0] / 255.0f;
            break;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            red = pixel[0] / 255.0f; green = pixel[1] / 255.0f; blue = pixel[2] / 255.0f;
            break;
        case VK_FORMAT_R16G16_SFLOAT:
            red = HalfToFloat(Read<uint16_t>(pixel));
            green = HalfToFloat(Read<uint16_t>(pixel + 2));
            blue = 0.0f;
            break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            red = HalfToFloat(Read<uint16_t>(pixel));
            green = HalfToFloat(Read<uint16_t>(pixel + 2));
            blue = HalfToFloat(Read<uint16_t>(pixel + 4));
            break;
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            red = green = blue = Read<float>(pixel);
            break;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            red = green = blue = static_cast<float>(Read<uint32_t>(pixel) & 0x00ffffffu) /
                                 16777215.0f;
            break;
        default: break;
        }
        if (native.Visualization == FrameDebugVisualization::Color &&
            native.Image->Format != VK_FORMAT_R8G8B8A8_UNORM &&
            native.Image->Format != VK_FORMAT_R8G8B8A8_SRGB)
        {
            red = DisplayColor(red); green = DisplayColor(green); blue = DisplayColor(blue);
        }
        else if (native.Visualization == FrameDebugVisualization::Velocity)
        {
            red = 0.5f + red * 8.0f;
            green = 0.5f + green * 8.0f;
            blue = 0.5f;
        }
        else if (native.Visualization == FrameDebugVisualization::Depth)
            red = green = blue = std::pow(std::clamp(red, 0.0f, 1.0f), 24.0f);
        const size_t output = (static_cast<size_t>(y) * preview.Width + x) * 4;
        preview.Pixels[output] = Byte(red);
        preview.Pixels[output + 1] = Byte(green);
        preview.Pixels[output + 2] = Byte(blue);
        preview.Pixels[output + 3] = 255;
    }
    vkUnmapMemory(m_device, readback.Memory);
    return true;
}

} // namespace engine
