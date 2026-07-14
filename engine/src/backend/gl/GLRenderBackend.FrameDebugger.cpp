#include "engine/backend/gl/GLRenderBackend.h"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
constexpr uint64_t kHiZ = FrameDebugId("visibility.hiz");
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

uint64_t TextureBytes(uint32_t width, uint32_t height, uint32_t layers,
                      uint32_t mipLevels, uint32_t bytesPerPixel,
                      uint32_t samples = 1)
{
    uint64_t result = 0;
    for (uint32_t mip = 0; mip < mipLevels; ++mip)
    {
        result += static_cast<uint64_t>(std::max(width >> mip, 1u)) *
                  std::max(height >> mip, 1u) * layers * bytesPerPixel * samples;
    }
    return result;
}

FrameDebugResource Resource(uint64_t id, std::string name, FrameDebugResourceKind kind,
                            FrameDebugVisualization visualization, uint32_t width,
                            uint32_t height, uint32_t layers, uint32_t mips,
                            uint32_t samples, std::string format, uint32_t bytesPerPixel,
                            bool previewable = true)
{
    FrameDebugResource resource;
    resource.Id = id;
    resource.Name = std::move(name);
    resource.Kind = kind;
    resource.Visualization = visualization;
    resource.Width = width;
    resource.Height = height;
    resource.Layers = layers;
    resource.MipLevels = mips;
    resource.Samples = samples;
    resource.Format = std::move(format);
    resource.EstimatedBytes = TextureBytes(width, height, layers, mips,
                                           bytesPerPixel, samples);
    resource.Previewable = previewable;
    return resource;
}

struct NativeResource
{
    GLuint Texture = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Layers = 1;
    uint32_t Mips = 1;
    FrameDebugVisualization Visualization = FrameDebugVisualization::Color;
    bool Cubemap = false;
};

float DisplayColor(float value)
{
    value = std::max(value, 0.0f);
    return std::pow(value / (1.0f + value), 1.0f / 2.2f);
}

uint8_t Byte(float value)
{
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

} // namespace

debug::FrameDebugSnapshot GLRenderBackend::GetFrameDebugSnapshot() const
{
    debug::FrameDebugSnapshot snapshot;
    snapshot.BackendName = Name();
    snapshot.FrameIndex = m_gpuProfileFrameIndex;
    auto& resources = snapshot.Resources;
    resources.reserve(28);

    resources.push_back(Resource(kEnvironment, "IBL Environment Cubemap",
        FrameDebugResourceKind::TextureCube, FrameDebugVisualization::Color,
        256, 256, 6, 9, 1, "RGBA16F", 8));
    resources.push_back(Resource(kIrradiance, "IBL Irradiance Cubemap",
        FrameDebugResourceKind::TextureCube, FrameDebugVisualization::Color,
        32, 32, 6, 1, 1, "RGBA16F", 8));
    resources.push_back(Resource(kPrefilter, "IBL GGX Prefilter Cubemap",
        FrameDebugResourceKind::TextureCube, FrameDebugVisualization::Color,
        128, 128, 6, GLEnvironment::kPrefilterMips, 1, "RGBA16F", 8));
    resources.push_back(Resource(kBrdf, "IBL BRDF LUT", FrameDebugResourceKind::Texture2D,
        FrameDebugVisualization::Color, 512, 512, 1, 1, 1, "RG16F", 4));

    for (size_t cascade = 0; cascade < kDirectionalShadows.size(); ++cascade)
        resources.push_back(Resource(kDirectionalShadows[cascade],
            "Directional Shadow Cascade " + std::to_string(cascade),
            FrameDebugResourceKind::Texture2D, FrameDebugVisualization::Depth,
            static_cast<uint32_t>(m_shadowSizes[cascade]),
            static_cast<uint32_t>(m_shadowSizes[cascade]), 1, 1, 1, "D32F", 4));
    resources.push_back(Resource(kLocalShadow, "Spot + Area Shadow Atlas",
        FrameDebugResourceKind::Texture2D, FrameDebugVisualization::Depth,
        kLocalShadowAtlasSize, kLocalShadowAtlasSize, 1, 1, 1, "D32F", 4));
    resources.push_back(Resource(kPointShadow, "Point Shadow Cube Array",
        FrameDebugResourceKind::TextureCubeArray, FrameDebugVisualization::Depth,
        static_cast<uint32_t>(m_pointShadowSize), static_cast<uint32_t>(m_pointShadowSize),
        kMaxPointShadows * 6, 1, 1, "D32F", 4));
    resources.push_back(Resource(kCookies, "Local Light Cookie Atlas",
        FrameDebugResourceKind::Texture2D, FrameDebugVisualization::SingleChannel,
        kCookieAtlasSize, kCookieAtlasSize, 1, 1, 1, "R8", 1));

    resources.push_back(Resource(FrameDebugId("scene.msaa.hdr"), "Main HDR MSAA Color",
        FrameDebugResourceKind::Renderbuffer, FrameDebugVisualization::Color,
        m_width, m_height, 1, 1, m_msaaSamples, "RGBA16F", 8, false));
    resources.push_back(Resource(FrameDebugId("scene.msaa.velocity"), "Main HDR MSAA Velocity",
        FrameDebugResourceKind::Renderbuffer, FrameDebugVisualization::Velocity,
        m_width, m_height, 1, 1, m_msaaSamples, "RG16F", 4, false));
    resources.push_back(Resource(FrameDebugId("scene.msaa.depth"), "Main HDR MSAA Depth",
        FrameDebugResourceKind::Renderbuffer, FrameDebugVisualization::Depth,
        m_width, m_height, 1, 1, m_msaaSamples, "D32F", 4, false));
    resources.push_back(Resource(kHdr, "Main HDR Resolved Color",
        FrameDebugResourceKind::Texture2D, FrameDebugVisualization::Color,
        m_width, m_height, 1, 1, 1, "RGBA16F", 8));
    resources.push_back(Resource(kVelocity, "Main HDR Velocity",
        FrameDebugResourceKind::Texture2D, FrameDebugVisualization::Velocity,
        m_width, m_height, 1, 1, 1, "RG16F", 4));
    resources.push_back(Resource(kDepth, "Main HDR Depth",
        FrameDebugResourceKind::Texture2D, FrameDebugVisualization::Depth,
        m_width, m_height, 1, 1, 1, "D32F", 4));
    resources.push_back(Resource(kHiZ, "Visibility Hi-Z Maximum Depth",
        FrameDebugResourceKind::Texture2D, FrameDebugVisualization::SingleChannel,
        m_width, m_height, 1, static_cast<uint32_t>(m_hizMipLevels), 1,
        "R32F", 4, m_hizValid));
    for (size_t history = 0; history < 2; ++history)
    {
        resources.push_back(Resource(kTaaColor[history],
            "TAA History Color " + std::to_string(history), FrameDebugResourceKind::Texture2D,
            FrameDebugVisualization::Color, m_width, m_height, 1, 1, 1, "RGBA16F", 8));
        resources.push_back(Resource(kTaaDepth[history],
            "TAA History Depth " + std::to_string(history), FrameDebugResourceKind::Texture2D,
            FrameDebugVisualization::Depth, m_width, m_height, 1, 1, 1, "R32F", 4));
    }
    for (size_t level = 0; level < m_bloomChain.size(); ++level)
        resources.push_back(Resource(kBloom[level], "Bloom Level " + std::to_string(level),
            FrameDebugResourceKind::Texture2D, FrameDebugVisualization::Color,
            m_bloomChain[level].Width, m_bloomChain[level].Height,
            1, 1, 1, "RGBA16F", 8));
    resources.push_back(Resource(kPostLdr, "Post Tonemap LDR",
        FrameDebugResourceKind::Texture2D, FrameDebugVisualization::Color,
        m_width, m_height, 1, 1, 1, "RGBA16F", 8));
    if (!m_hasFrameDebugFrame)
        for (FrameDebugResource& resource : resources)
            resource.Previewable = false;

    m_renderGraph.PopulateFrameDebugSnapshot(snapshot);
    return snapshot;
}

bool GLRenderBackend::CaptureFrameDebugResource(uint64_t resourceId, uint32_t mipLevel,
                                                uint32_t layer,
                                                debug::FrameDebugPreview& preview)
{
    NativeResource native;
    if (resourceId == kEnvironment)
        native = {m_environment->EnvironmentMapId(), 256, 256, 6, 9,
                  FrameDebugVisualization::Color, true};
    else if (resourceId == kIrradiance)
        native = {m_environment->IrradianceMapId(), 32, 32, 6, 1,
                  FrameDebugVisualization::Color, true};
    else if (resourceId == kPrefilter)
        native = {m_environment->PrefilterMapId(), 128, 128, 6,
                  GLEnvironment::kPrefilterMips, FrameDebugVisualization::Color, true};
    else if (resourceId == kBrdf)
        native = {m_environment->BrdfLutId(), 512, 512, 1, 1};
    else if (resourceId == kLocalShadow)
        native = {m_localShadowAtlas, kLocalShadowAtlasSize, kLocalShadowAtlasSize, 1, 1,
                  FrameDebugVisualization::Depth};
    else if (resourceId == kPointShadow)
        native = {m_pointShadowArray, static_cast<uint32_t>(m_pointShadowSize),
                  static_cast<uint32_t>(m_pointShadowSize), kMaxPointShadows * 6, 1,
                  FrameDebugVisualization::Depth};
    else if (resourceId == kCookies)
        native = {m_cookieAtlas, kCookieAtlasSize, kCookieAtlasSize, 1, 1,
                  FrameDebugVisualization::SingleChannel};
    else if (resourceId == kHdr)
        native = {m_hdrColorTex, static_cast<uint32_t>(m_width),
                  static_cast<uint32_t>(m_height), 1, 1};
    else if (resourceId == kVelocity)
        native = {m_velocityTex, static_cast<uint32_t>(m_width),
                  static_cast<uint32_t>(m_height), 1, 1,
                  FrameDebugVisualization::Velocity};
    else if (resourceId == kDepth)
        native = {m_depthTex, static_cast<uint32_t>(m_width),
                  static_cast<uint32_t>(m_height), 1, 1,
                  FrameDebugVisualization::Depth};
    else if (resourceId == kHiZ)
        native = {m_hizTexture, static_cast<uint32_t>(m_width),
                  static_cast<uint32_t>(m_height), 1,
                  static_cast<uint32_t>(m_hizMipLevels),
                  FrameDebugVisualization::SingleChannel};
    else if (resourceId == kPostLdr)
        native = {m_postColorTex, static_cast<uint32_t>(m_width),
                  static_cast<uint32_t>(m_height), 1, 1};
    else
    {
        for (size_t index = 0; index < kDirectionalShadows.size(); ++index)
            if (resourceId == kDirectionalShadows[index])
                native = {m_shadowMaps[index], static_cast<uint32_t>(m_shadowSizes[index]),
                          static_cast<uint32_t>(m_shadowSizes[index]), 1, 1,
                          FrameDebugVisualization::Depth};
        for (size_t index = 0; index < 2; ++index)
        {
            if (resourceId == kTaaColor[index])
                native = {m_taaHistoryColor[index], static_cast<uint32_t>(m_width),
                          static_cast<uint32_t>(m_height), 1, 1};
            if (resourceId == kTaaDepth[index])
                native = {m_taaHistoryDepth[index], static_cast<uint32_t>(m_width),
                          static_cast<uint32_t>(m_height), 1, 1,
                          FrameDebugVisualization::Depth};
        }
        for (size_t index = 0; index < m_bloomChain.size(); ++index)
            if (resourceId == kBloom[index])
                native = {m_bloomChain[index].Texture,
                          static_cast<uint32_t>(m_bloomChain[index].Width),
                          static_cast<uint32_t>(m_bloomChain[index].Height), 1, 1};
    }

    if (native.Texture == 0 || mipLevel >= native.Mips || layer >= native.Layers)
    {
        preview.Error = "OpenGL resource or subresource is unavailable";
        return false;
    }
    const uint32_t sourceWidth = std::max(native.Width >> mipLevel, 1u);
    const uint32_t sourceHeight = std::max(native.Height >> mipLevel, 1u);
    const size_t componentCount = native.Visualization == FrameDebugVisualization::Depth ||
                                  native.Visualization == FrameDebugVisualization::SingleChannel
        ? 1 : (native.Visualization == FrameDebugVisualization::Velocity ? 2 : 4);
    const GLenum format = componentCount == 1
        ? (native.Visualization == FrameDebugVisualization::Depth ? GL_DEPTH_COMPONENT : GL_RED)
        : (componentCount == 2 ? GL_RG : GL_RGBA);
    std::vector<float> source(static_cast<size_t>(sourceWidth) * sourceHeight * componentCount);
    GLint previousPixelPackBuffer = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPixelPackBuffer);
    // Client pointers passed below are interpreted as byte offsets whenever a
    // pixel-pack buffer is bound. Isolate frame-debugger readback from async
    // exposure/screenshot state and restore the caller's binding afterwards.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (native.Cubemap)
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, native.Texture);
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer,
                      static_cast<GLint>(mipLevel), format, GL_FLOAT, source.data());
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    else
        glGetTextureSubImage(native.Texture, static_cast<GLint>(mipLevel), 0, 0,
                             static_cast<GLint>(layer), static_cast<GLsizei>(sourceWidth),
                             static_cast<GLsizei>(sourceHeight), 1, format, GL_FLOAT,
                             static_cast<GLsizei>(source.size() * sizeof(float)), source.data());
    // This diagnostic path is deliberately synchronous. In particular, keep
    // the client-memory destination alive until cubemap readback DMA has fully
    // retired on drivers that defer glGetTexImage work internally.
    glFinish();
    glBindBuffer(GL_PIXEL_PACK_BUFFER,
                 static_cast<GLuint>(previousPixelPackBuffer));
    if (glGetError() != GL_NO_ERROR)
    {
        preview.Error = "OpenGL texture readback failed";
        return false;
    }

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
        const uint32_t sy = sourceHeight - 1 -
            std::min(y * sourceHeight / preview.Height, sourceHeight - 1);
        const size_t input = (static_cast<size_t>(sy) * sourceWidth + sx) * componentCount;
        const size_t output = (static_cast<size_t>(y) * preview.Width + x) * 4;
        float red = source[input], green = red, blue = red;
        if (native.Visualization == FrameDebugVisualization::Color)
        {
            green = componentCount > 1 ? source[input + 1] : red;
            blue = componentCount > 2 ? source[input + 2] : red;
            red = DisplayColor(red); green = DisplayColor(green); blue = DisplayColor(blue);
        }
        else if (native.Visualization == FrameDebugVisualization::Velocity)
        {
            red = 0.5f + red * 8.0f;
            green = 0.5f + source[input + 1] * 8.0f;
            blue = 0.5f;
        }
        else if (native.Visualization == FrameDebugVisualization::Depth)
            red = green = blue = std::pow(std::clamp(red, 0.0f, 1.0f), 24.0f);
        preview.Pixels[output] = Byte(red);
        preview.Pixels[output + 1] = Byte(green);
        preview.Pixels[output + 2] = Byte(blue);
        preview.Pixels[output + 3] = 255;
    }
    return true;
}

} // namespace engine
