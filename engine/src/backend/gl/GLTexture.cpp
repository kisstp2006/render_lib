#include "engine/backend/gl/GLTexture.h"
#include "engine/backend/gl/GLDebug.h"

#include <glad/gl.h>

#include <atomic>
#include <string>

namespace engine
{

GLTexture::GLTexture(const TextureData& data, float maxAnisotropy)
{
    glGenTextures(1, &m_texture);
    static std::atomic<uint64_t> nextTextureId{1};
    glBindTexture(GL_TEXTURE_2D, m_texture);
    gl_debug::LabelObject(GL_TEXTURE, m_texture,
        "Material Texture " + std::to_string(nextTextureId.fetch_add(1)) +
        (data.SRGB ? " (sRGB)" : " (Linear)"));

    const GLenum internalFormat = data.SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, data.Width, data.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 data.Pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
#ifdef GL_TEXTURE_MAX_ANISOTROPY
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, maxAnisotropy);
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
