#include "engine/backend/gl/GLTexture.h"
#include "engine/backend/gl/GLDebug.h"

#include <glad/gl.h>

#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <string>

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR 0x93D0
#endif

namespace engine
{
namespace
{

GLenum ToWrap(TextureAddressMode mode)
{
    switch (mode)
    {
    case TextureAddressMode::Clamp: return GL_CLAMP_TO_EDGE;
    case TextureAddressMode::Mirror: return GL_MIRRORED_REPEAT;
    case TextureAddressMode::Border: return GL_CLAMP_TO_BORDER;
    default: return GL_REPEAT;
    }
}

void ApplyFiltering(TextureFilterMode mode, bool hasMips)
{
    GLenum minimum = GL_LINEAR;
    GLenum magnification = mode == TextureFilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
    if (hasMips)
    {
        if (mode == TextureFilterMode::Nearest) minimum = GL_NEAREST_MIPMAP_NEAREST;
        else if (mode == TextureFilterMode::Bilinear) minimum = GL_LINEAR_MIPMAP_NEAREST;
        else minimum = GL_LINEAR_MIPMAP_LINEAR;
    }
    else if (mode == TextureFilterMode::Nearest) minimum = GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minimum);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magnification);
}

GLenum CompressedInternalFormat(TexturePixelStorage storage, bool srgb)
{
    switch (storage)
    {
    case TexturePixelStorage::Bc1Rgb:
        return srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    case TexturePixelStorage::Bc3Rgba:
        return srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    case TexturePixelStorage::Bc5Rg:
        return GL_COMPRESSED_RG_RGTC2;
    case TexturePixelStorage::Bc7Rgba:
        return srgb ? GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM : GL_COMPRESSED_RGBA_BPTC_UNORM;
    case TexturePixelStorage::Astc4x4Rgba:
        return srgb ? GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR : GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
    default:
        return 0;
    }
}

void ValidateCompressionSupport(TexturePixelStorage storage)
{
    if ((storage == TexturePixelStorage::Bc1Rgb || storage == TexturePixelStorage::Bc3Rgba) &&
        !GLAD_GL_EXT_texture_compression_s3tc)
        throw std::runtime_error("OpenGL: BC1/BC3 texture requires EXT_texture_compression_s3tc");
    if (storage == TexturePixelStorage::Bc7Rgba && !GLAD_GL_ARB_texture_compression_bptc && !GLAD_GL_VERSION_4_2)
        throw std::runtime_error("OpenGL: BC7 texture requires ARB_texture_compression_bptc or OpenGL 4.2");
    if (storage == TexturePixelStorage::Astc4x4Rgba && !GLAD_GL_KHR_texture_compression_astc_ldr)
        throw std::runtime_error("OpenGL: ASTC texture requires KHR_texture_compression_astc_ldr");
}

} // namespace

GLTexture::GLTexture(const TextureData& data, float maxAnisotropy)
{
    glGenTextures(1, &m_texture);
    static std::atomic<uint64_t> nextTextureId{1};
    glBindTexture(GL_TEXTURE_2D, m_texture);
    gl_debug::LabelObject(GL_TEXTURE, m_texture,
        "Material Texture " + std::to_string(nextTextureId.fetch_add(1)) +
        (data.SRGB ? " (sRGB)" : " (Linear)"));

    const bool floatingPoint = data.Storage == TexturePixelStorage::Rgba32Float;
    const bool blockCompressed = IsBlockCompressed(data.Storage);
    if (blockCompressed)
        ValidateCompressionSupport(data.Storage);
    const GLenum internalFormat = floatingPoint ? GL_RGBA32F :
                                  (blockCompressed ? CompressedInternalFormat(data.Storage, data.SRGB) :
                                   (data.SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8));
    const GLenum pixelType = floatingPoint ? GL_FLOAT : GL_UNSIGNED_BYTE;
    if (!data.MipLevels.empty())
    {
        for (size_t level = 0; level < data.MipLevels.size(); ++level)
        {
            const TextureMipData& mip = data.MipLevels[level];
            if (blockCompressed)
            {
                const size_t expected = TextureMipByteSize(data.Storage, static_cast<uint32_t>(mip.Width),
                                                           static_cast<uint32_t>(mip.Height));
                if (mip.Pixels.size() != expected)
                    throw std::runtime_error("OpenGL: invalid block-compressed texture mip data");
                glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), internalFormat, mip.Width,
                                       mip.Height, 0, static_cast<GLsizei>(mip.Pixels.size()), mip.Pixels.data());
                continue;
            }
            const void* pixels = floatingPoint ? static_cast<const void*>(mip.FloatPixels.data()) :
                                                 static_cast<const void*>(mip.Pixels.data());
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), internalFormat,
                         mip.Width, mip.Height, 0, GL_RGBA, pixelType, pixels);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                        static_cast<GLint>(data.MipLevels.size() - 1));
    }
    else
    {
        if (blockCompressed)
            throw std::runtime_error("OpenGL: block-compressed textures require a complete cooked mip chain");
        const void* pixels = floatingPoint ? static_cast<const void*>(data.FloatPixels.data()) :
                                             static_cast<const void*>(data.Pixels.data());
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, data.Width, data.Height, 0, GL_RGBA,
                     pixelType, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    ApplyFiltering(data.Filter, true);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToWrap(data.AddressU));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToWrap(data.AddressV));
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, data.MipBias);
#ifdef GL_TEXTURE_MAX_ANISOTROPY
    const float requestedAnisotropy = data.Filter == TextureFilterMode::Anisotropic ?
        std::min(data.MaxAnisotropy, maxAnisotropy) : 1.0f;
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY,
                    std::max(requestedAnisotropy, 1.0f));
#else
    (void)maxAnisotropy;
#endif
}

GLTexture::~GLTexture() { glDeleteTextures(1, &m_texture); }

void GLTexture::Bind(int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_texture);
}

} // namespace engine
