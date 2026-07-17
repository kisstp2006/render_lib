#include "engine/backend/gl/GLMesh.h"
#include "engine/backend/gl/GLDebug.h"

#include <glad/gl.h>

#include <atomic>
#include <string>

namespace engine {

GLMesh::GLMesh(const MeshData& data)
{
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    static std::atomic<uint64_t> nextMeshId{1};
    const std::string baseName = "Scene Mesh " + std::to_string(nextMeshId.fetch_add(1));
    glBindVertexArray(m_vao);
    gl_debug::LabelObject(GL_VERTEX_ARRAY, m_vao, baseName + " VAO");

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl_debug::LabelObject(GL_BUFFER, m_vbo, baseName + " Vertex Buffer");

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    gl_debug::LabelObject(GL_BUFFER, m_ebo, baseName + " Index Buffer");

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, Position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, Normal)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, Tangent)));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, UV)));

    Upload(data);
    glBindVertexArray(0);
}

GLMesh::~GLMesh()
{
    glDeleteBuffers(1, &m_ebo);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

void GLMesh::Draw() const
{
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

void GLMesh::DrawInstanced(uint32_t instanceCount, uint32_t firstInstance) const
{
    glBindVertexArray(m_vao);
    // HLSL has no portable OpenGL BaseInstance semantic. The native SPIR-V
    // vertex shaders receive firstInstance through their small constant block.
    (void)firstInstance;
    glDrawElementsInstanced(GL_TRIANGLES, m_indexCount,
        GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(instanceCount));
}

void GLMesh::EnsureUpToDate(const MeshData& data)
{
    if (m_revision != data.Revision)
        Upload(data);
}

void GLMesh::Upload(const MeshData& data)
{
    const GLenum usage = data.RuntimeMutable ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(data.Vertices.size() * sizeof(Vertex)),
        data.Vertices.data(), usage);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(data.Indices.size() * sizeof(uint32_t)),
        data.Indices.data(), usage);
    m_indexCount = static_cast<int>(data.Indices.size());
    m_revision = data.Revision;
}

} // namespace engine
