#include "engine/scene/Mesh.h"

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace engine::primitives {

MeshData MakeSphere(float radius, int stacks, int slices)
{
    MeshData mesh;

    for (int i = 0; i <= stacks; ++i)
    {
        const float v = static_cast<float>(i) / static_cast<float>(stacks);
        const float phi = v * static_cast<float>(M_PI);

        for (int j = 0; j <= slices; ++j)
        {
            const float u = static_cast<float>(j) / static_cast<float>(slices);
            const float theta = u * 2.0f * static_cast<float>(M_PI);

            const float x = std::sin(phi) * std::cos(theta);
            const float y = std::cos(phi);
            const float z = std::sin(phi) * std::sin(theta);

            glm::vec3 normal{x, y, z};
            glm::vec3 tangent{-std::sin(theta), 0.0f, std::cos(theta)};

            Vertex vert;
            vert.Position = normal * radius;
            vert.Normal = normal;
            vert.Tangent = glm::vec4(glm::normalize(tangent), 1.0f);
            vert.UV = {u, v};
            mesh.Vertices.push_back(vert);
        }
    }

    const int ringVerts = slices + 1;
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            const uint32_t a = static_cast<uint32_t>(i * ringVerts + j);
            const uint32_t b = static_cast<uint32_t>((i + 1) * ringVerts + j);
            const uint32_t c = static_cast<uint32_t>((i + 1) * ringVerts + j + 1);
            const uint32_t d = static_cast<uint32_t>(i * ringVerts + j + 1);

            mesh.Indices.insert(mesh.Indices.end(), {a, b, c, a, c, d});
        }
    }

    return mesh;
}

static void AppendFace(MeshData& mesh, glm::vec3 normal, glm::vec3 right, glm::vec3 up, float halfExtent)
{
    const uint32_t base = static_cast<uint32_t>(mesh.Vertices.size());
    const glm::vec3 center = normal * halfExtent;
    const glm::vec4 tangent(glm::normalize(right), 1.0f);

    mesh.Vertices.push_back({center - right * halfExtent - up * halfExtent, normal, tangent, {0.0f, 0.0f}});
    mesh.Vertices.push_back({center + right * halfExtent - up * halfExtent, normal, tangent, {1.0f, 0.0f}});
    mesh.Vertices.push_back({center + right * halfExtent + up * halfExtent, normal, tangent, {1.0f, 1.0f}});
    mesh.Vertices.push_back({center - right * halfExtent + up * halfExtent, normal, tangent, {0.0f, 1.0f}});

    mesh.Indices.insert(mesh.Indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

MeshData MakeCube(float halfExtent)
{
    MeshData mesh;
    AppendFace(mesh, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, halfExtent);  // +Z
    AppendFace(mesh, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, halfExtent); // -Z
    AppendFace(mesh, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}, halfExtent);  // +X
    AppendFace(mesh, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, halfExtent);  // -X
    AppendFace(mesh, {0, 1, 0}, {1, 0, 0}, {0, 0, -1}, halfExtent);  // +Y
    AppendFace(mesh, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}, halfExtent);  // -Y
    return mesh;
}

MeshData MakePlane(float size, int subdivisions)
{
    MeshData mesh;
    subdivisions = subdivisions < 1 ? 1 : subdivisions;
    const float half = size * 0.5f;
    const float step = size / static_cast<float>(subdivisions);

    for (int z = 0; z <= subdivisions; ++z)
    {
        for (int x = 0; x <= subdivisions; ++x)
        {
            Vertex v;
            v.Position = {-half + x * step, 0.0f, -half + z * step};
            v.Normal = {0.0f, 1.0f, 0.0f};
            v.Tangent = {1.0f, 0.0f, 0.0f, 1.0f};
            v.UV = {static_cast<float>(x) / subdivisions * size, static_cast<float>(z) / subdivisions * size};
            mesh.Vertices.push_back(v);
        }
    }

    const int rowVerts = subdivisions + 1;
    for (int z = 0; z < subdivisions; ++z)
    {
        for (int x = 0; x < subdivisions; ++x)
        {
            const uint32_t a = static_cast<uint32_t>(z * rowVerts + x);
            const uint32_t b = static_cast<uint32_t>(z * rowVerts + x + 1);
            const uint32_t c = static_cast<uint32_t>((z + 1) * rowVerts + x + 1);
            const uint32_t d = static_cast<uint32_t>((z + 1) * rowVerts + x);
            mesh.Indices.insert(mesh.Indices.end(), {a, b, c, a, c, d});
        }
    }

    return mesh;
}

} // namespace engine::primitives
