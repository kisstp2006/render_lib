#include "engine/scene/GltfLoader.h"

#include "engine/core/Log.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Texture.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {

namespace {

const char* ResultName(cgltf_result result)
{
    switch (result)
    {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF 1.x is unsupported";
    default: return "unknown error";
    }
}

[[noreturn]] void Fail(const std::string& path, const std::string& message)
{
    throw GltfLoadError("glTF '" + path + "': " + message);
}

std::string DecodeUri(std::string uri)
{
    for (size_t i = 0; i + 2 < uri.size(); ++i)
    {
        if (uri[i] != '%')
            continue;
        const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = hex(uri[i + 1]);
        const int lo = hex(uri[i + 2]);
        if (hi >= 0 && lo >= 0)
        {
            uri.replace(i, 3, 1, static_cast<char>((hi << 4) | lo));
        }
    }
    return uri;
}

std::vector<uint8_t> DecodeBase64(const std::string& encoded)
{
    static constexpr std::array<int8_t, 256> table = [] {
        std::array<int8_t, 256> result{};
        result.fill(-1);
        const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; alphabet[i]; ++i)
            result[static_cast<uint8_t>(alphabet[i])] = static_cast<int8_t>(i);
        return result;
    }();

    std::vector<uint8_t> output;
    output.reserve(encoded.size() * 3 / 4);
    uint32_t accumulator = 0;
    int bits = 0;
    for (char c : encoded)
    {
        if (c == '=')
            break;
        const int value = table[static_cast<uint8_t>(c)];
        if (value < 0)
            continue;
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            output.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xff));
        }
    }
    return output;
}

glm::mat4 LocalTransform(const cgltf_node& node)
{
    float values[16]{};
    cgltf_node_transform_local(&node, values);
    return glm::make_mat4(values);
}

void GenerateNormals(MeshData& mesh)
{
    for (Vertex& vertex : mesh.Vertices)
        vertex.Normal = glm::vec3(0.0f);

    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
    {
        Vertex& a = mesh.Vertices[mesh.Indices[i]];
        Vertex& b = mesh.Vertices[mesh.Indices[i + 1]];
        Vertex& c = mesh.Vertices[mesh.Indices[i + 2]];
        const glm::vec3 face = glm::cross(b.Position - a.Position, c.Position - a.Position);
        a.Normal += face;
        b.Normal += face;
        c.Normal += face;
    }

    for (Vertex& vertex : mesh.Vertices)
        vertex.Normal = glm::dot(vertex.Normal, vertex.Normal) > 1e-12f ? glm::normalize(vertex.Normal) : glm::vec3(0.0f, 1.0f, 0.0f);
}

void GenerateTangents(MeshData& mesh)
{
    std::vector<glm::vec3> tangents(mesh.Vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(mesh.Vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
    {
        const uint32_t ia = mesh.Indices[i];
        const uint32_t ib = mesh.Indices[i + 1];
        const uint32_t ic = mesh.Indices[i + 2];
        const Vertex& a = mesh.Vertices[ia];
        const Vertex& b = mesh.Vertices[ib];
        const Vertex& c = mesh.Vertices[ic];
        const glm::vec3 e1 = b.Position - a.Position;
        const glm::vec3 e2 = c.Position - a.Position;
        const glm::vec2 d1 = b.UV - a.UV;
        const glm::vec2 d2 = c.UV - a.UV;
        const float det = d1.x * d2.y - d1.y * d2.x;
        if (std::abs(det) < 1e-8f)
            continue;
        const float inv = 1.0f / det;
        const glm::vec3 tangent = (e1 * d2.y - e2 * d1.y) * inv;
        const glm::vec3 bitangent = (e2 * d1.x - e1 * d2.x) * inv;
        for (uint32_t index : {ia, ib, ic})
        {
            tangents[index] += tangent;
            bitangents[index] += bitangent;
        }
    }

    for (size_t i = 0; i < mesh.Vertices.size(); ++i)
    {
        const glm::vec3 n = mesh.Vertices[i].Normal;
        glm::vec3 t = tangents[i] - n * glm::dot(n, tangents[i]);
        if (glm::dot(t, t) < 1e-12f)
        {
            const glm::vec3 axis = std::abs(n.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            t = glm::normalize(glm::cross(axis, n));
        }
        else
        {
            t = glm::normalize(t);
        }
        const float sign = glm::dot(glm::cross(n, t), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
        mesh.Vertices[i].Tangent = glm::vec4(t, sign);
    }
}

struct TextureKey
{
    const cgltf_image* Image = nullptr;
    bool Srgb = false;
    bool operator==(const TextureKey&) const = default;
};

struct TextureKeyHash
{
    size_t operator()(const TextureKey& key) const
    {
        return std::hash<const void*>{}(key.Image) ^ (key.Srgb ? size_t{0x9e3779b9} : 0);
    }
};

class Loader
{
public:
    Loader(std::string path, cgltf_data& data, Scene& scene, const glm::mat4& rootTransform)
        : Path(std::move(path)), BaseDirectory(std::filesystem::path(Path).parent_path()), Data(data), Target(scene), RootTransform(rootTransform)
    {
    }

    GltfLoadResult Run()
    {
        Result.Nodes = Data.nodes_count;
        Result.Meshes = Data.meshes_count;
        Result.Materials = Data.materials_count;

        const cgltf_scene* selected = Data.scene ? Data.scene : (Data.scenes_count > 0 ? &Data.scenes[0] : nullptr);
        if (selected)
        {
            for (cgltf_size i = 0; i < selected->nodes_count; ++i)
                VisitNode(*selected->nodes[i], RootTransform);
        }
        else
        {
            for (cgltf_size i = 0; i < Data.nodes_count; ++i)
            {
                if (!Data.nodes[i].parent)
                    VisitNode(Data.nodes[i], RootTransform);
            }
        }

        Result.Textures = TextureCache.size();
        return Result;
    }

private:
    std::shared_ptr<TextureData> LoadTexture(const cgltf_texture_view& view, bool srgb)
    {
        if (!view.texture || !view.texture->image)
            return nullptr;
        if (view.texcoord != 0)
            Fail(Path, "only TEXCOORD_0 material textures are supported");
        const cgltf_image* image = view.texture->image;
        const TextureKey key{image, srgb};
        if (const auto found = TextureCache.find(key); found != TextureCache.end())
            return found->second;

        std::shared_ptr<TextureData> texture;
        const std::string name = image->name ? image->name : (image->uri ? image->uri : "embedded image");
        if (image->buffer_view)
        {
            const cgltf_buffer_view& bufferView = *image->buffer_view;
            if (!bufferView.buffer || !bufferView.buffer->data)
                Fail(Path, "image buffer is not loaded: " + name);
            const auto* bytes = static_cast<const uint8_t*>(bufferView.buffer->data) + bufferView.offset;
            texture = textures::LoadFromMemory(bytes, bufferView.size, srgb, name);
        }
        else if (image->uri && std::string(image->uri).starts_with("data:"))
        {
            const std::string uri = image->uri;
            const size_t comma = uri.find(',');
            if (comma == std::string::npos || uri.find(";base64", 0) == std::string::npos)
                Fail(Path, "unsupported image data URI: " + name);
            const std::vector<uint8_t> bytes = DecodeBase64(uri.substr(comma + 1));
            texture = textures::LoadFromMemory(bytes.data(), bytes.size(), srgb, name);
        }
        else if (image->uri)
        {
            texture = textures::LoadFromFile((BaseDirectory / DecodeUri(image->uri)).string(), srgb);
        }

        if (!texture)
            Fail(Path, "failed to load image: " + name);
        TextureCache.emplace(key, texture);
        return texture;
    }

    Material LoadMaterial(const cgltf_material* source)
    {
        Material material;
        if (!source)
            return material;
        if (!source->has_pbr_metallic_roughness)
            Fail(Path, "material '" + std::string(source->name ? source->name : "unnamed") + "' does not use metallic-roughness PBR");

        const cgltf_pbr_metallic_roughness& pbr = source->pbr_metallic_roughness;
        material.Albedo = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
        material.BaseColorAlpha = pbr.base_color_factor[3];
        material.Metallic = pbr.metallic_factor;
        material.Roughness = pbr.roughness_factor;
        material.Emissive = {source->emissive_factor[0], source->emissive_factor[1], source->emissive_factor[2]};
        material.AlbedoMap = LoadTexture(pbr.base_color_texture, true);
        material.MetallicRoughnessMap = LoadTexture(pbr.metallic_roughness_texture, false);
        material.NormalMap = LoadTexture(source->normal_texture, false);
        material.OcclusionMap = LoadTexture(source->occlusion_texture, false);
        material.EmissiveMap = LoadTexture(source->emissive_texture, true);

        if (source->alpha_mode == cgltf_alpha_mode_mask)
        {
            material.Alpha = Material::AlphaMode::Mask;
            material.AlphaCutoff = source->alpha_cutoff;
        }
        else if (source->alpha_mode == cgltf_alpha_mode_blend)
        {
            Fail(Path, "material '" + std::string(source->name ? source->name : "unnamed") + "' uses alpha BLEND, which is not implemented yet");
        }
        return material;
    }

    std::shared_ptr<MeshData> LoadPrimitive(const cgltf_primitive& primitive)
    {
        if (const auto found = MeshCache.find(&primitive); found != MeshCache.end())
            return found->second;
        if (primitive.type != cgltf_primitive_type_triangles)
            Fail(Path, "only triangle-list primitives are supported");

        const cgltf_accessor* position = nullptr;
        const cgltf_accessor* normal = nullptr;
        const cgltf_accessor* tangent = nullptr;
        const cgltf_accessor* uv = nullptr;
        for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
        {
            const cgltf_attribute& attribute = primitive.attributes[i];
            if (attribute.type == cgltf_attribute_type_position) position = attribute.data;
            else if (attribute.type == cgltf_attribute_type_normal) normal = attribute.data;
            else if (attribute.type == cgltf_attribute_type_tangent) tangent = attribute.data;
            else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uv = attribute.data;
        }
        if (!position)
            Fail(Path, "primitive is missing POSITION");

        auto mesh = std::make_shared<MeshData>();
        mesh->Vertices.resize(position->count);
        for (cgltf_size i = 0; i < position->count; ++i)
        {
            float values[4]{};
            cgltf_accessor_read_float(position, i, values, 3);
            mesh->Vertices[i].Position = {values[0], values[1], values[2]};
            if (normal)
            {
                cgltf_accessor_read_float(normal, i, values, 3);
                mesh->Vertices[i].Normal = {values[0], values[1], values[2]};
            }
            if (tangent)
            {
                cgltf_accessor_read_float(tangent, i, values, 4);
                mesh->Vertices[i].Tangent = {values[0], values[1], values[2], values[3]};
            }
            if (uv)
            {
                cgltf_accessor_read_float(uv, i, values, 2);
                mesh->Vertices[i].UV = {values[0], values[1]};
            }
        }

        if (primitive.indices)
        {
            mesh->Indices.resize(primitive.indices->count);
            for (cgltf_size i = 0; i < primitive.indices->count; ++i)
                mesh->Indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
        }
        else
        {
            mesh->Indices.resize(position->count);
            for (cgltf_size i = 0; i < position->count; ++i)
                mesh->Indices[i] = static_cast<uint32_t>(i);
        }

        if (!normal)
            GenerateNormals(*mesh);
        if (!tangent)
            GenerateTangents(*mesh);
        MeshCache.emplace(&primitive, mesh);
        return mesh;
    }

    void VisitNode(const cgltf_node& node, const glm::mat4& parentTransform)
    {
        const glm::mat4 world = parentTransform * LocalTransform(node);
        if (node.mesh)
        {
            for (cgltf_size i = 0; i < node.mesh->primitives_count; ++i)
            {
                const cgltf_primitive& primitive = node.mesh->primitives[i];
                Target.AddInstance(LoadPrimitive(primitive), LoadMaterial(primitive.material), world);
                ++Result.Primitives;
            }
        }
        for (cgltf_size i = 0; i < node.children_count; ++i)
            VisitNode(*node.children[i], world);
    }

    std::string Path;
    std::filesystem::path BaseDirectory;
    cgltf_data& Data;
    Scene& Target;
    glm::mat4 RootTransform;
    GltfLoadResult Result;
    std::unordered_map<const cgltf_primitive*, std::shared_ptr<MeshData>> MeshCache;
    std::unordered_map<TextureKey, std::shared_ptr<TextureData>, TextureKeyHash> TextureCache;
};

} // namespace

GltfLoadResult LoadGltfScene(const std::string& path, Scene& scene, const glm::mat4& rootTransform)
{
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &raw);
    if (result != cgltf_result_success)
        Fail(path, std::string("parse failed: ") + ResultName(result));
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);

    result = cgltf_load_buffers(&options, data.get(), path.c_str());
    if (result != cgltf_result_success)
        Fail(path, std::string("buffer loading failed: ") + ResultName(result));
    result = cgltf_validate(data.get());
    if (result != cgltf_result_success)
        Fail(path, std::string("validation failed: ") + ResultName(result));

    GltfLoadResult loaded = Loader(path, *data, scene, rootTransform).Run();
    std::ostringstream message;
    message << "Loaded glTF: " << path << " (" << loaded.Nodes << " nodes, " << loaded.Meshes
            << " meshes, " << loaded.Primitives << " primitives, " << loaded.Textures << " textures)";
    log::Info(message.str());
    return loaded;
}

} // namespace engine
