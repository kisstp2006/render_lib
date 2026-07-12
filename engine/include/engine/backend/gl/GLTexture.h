#pragma once

#include "engine/scene/Texture.h"

namespace engine {

// GPU-side 2D texture uploaded from a TextureData, mipmapped, with sRGB
// internal format when the source data is color. Cached by the backend
// keyed by TextureData pointer.
class GLTexture
{
public:
    explicit GLTexture(const TextureData& data);
    ~GLTexture();

    GLTexture(const GLTexture&) = delete;
    GLTexture& operator=(const GLTexture&) = delete;

    void Bind(int unit) const;
    unsigned int Id() const { return m_texture; }

private:
    unsigned int m_texture = 0;
};

} // namespace engine
