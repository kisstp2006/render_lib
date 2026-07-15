#pragma once

#include "engine/scene/Mesh.h"

namespace engine {

// GPU-side representation of a MeshData (VAO + VBO + EBO). One per unique
// MeshData; the GL backend caches these keyed by MeshData pointer.
class GLMesh
{
public:
    explicit GLMesh(const MeshData& data);
    ~GLMesh();

    GLMesh(const GLMesh&) = delete;
    GLMesh& operator=(const GLMesh&) = delete;

    void Draw() const;
    void DrawInstanced(uint32_t instanceCount, uint32_t firstInstance) const;

private:
    unsigned int m_vao = 0, m_vbo = 0, m_ebo = 0;
    int m_indexCount = 0;
};

} // namespace engine
