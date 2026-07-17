#include "engine/asset/ModelAsset.h"

#include "engine/asset/AssetFileSystem.h"
#include "engine/asset/GltfLoader.h"
#include "engine/resource/BinaryIO.h"
#include "engine/scene/MeshCombiner.h"
#include "engine/scene/Scene.h"

#include <cgltf.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>

namespace engine::assets
{
namespace
{

AssetDiagnostic Diagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message,
                           const std::filesystem::path &source = {})
{
    AssetDiagnostic diagnostic;
    diagnostic.Severity = severity;
    diagnostic.Code = std::move(code);
    diagnostic.Message = std::move(message);
    diagnostic.Source = source;
    diagnostic.Step = "model import";
    return diagnostic;
}

// A generated descriptor is rebuilt from the source model on every import so
// changed glTF material/texture settings are detected. Preserve only its cook
// bookkeeping; otherwise a stable GUID would still lose its last fingerprint
// and force an expensive texture recook on every application launch.
void PreserveCookState(const AssetDescriptor &existing, AssetDescriptor &generated)
{
    generated.Guid = existing.Guid;
    generated.DescriptorVersion = existing.DescriptorVersion;
    generated.ImporterVersion = existing.ImporterVersion;
    generated.TransformerVersion = existing.TransformerVersion;
    generated.SourceFingerprint = existing.SourceFingerprint;
    generated.SettingsFingerprint = existing.SettingsFingerprint;
    generated.DependencyFingerprint = existing.DependencyFingerprint;
    generated.LastTransformFingerprint = existing.LastTransformFingerprint;
    generated.LastTransform = existing.LastTransform;
    generated.State = existing.State;
    generated.Diagnostics = existing.Diagnostics;
}

std::string Float(float value)
{
    std::ostringstream stream;
    stream.precision(9);
    stream << value;
    return stream.str();
}
std::string Bool(bool value)
{
    return value ? "true" : "false";
}

std::string CollisionTypeName(cook::CollisionType type)
{
    switch (type)
    {
    case cook::CollisionType::TriangleMesh:
        return "triangle";
    case cook::CollisionType::SimplifiedTriangleMesh:
        return "simplified";
    case cook::CollisionType::ConvexDecomposition:
        return "convex_decomposition";
    default:
        return "none";
    }
}

cook::CollisionType ParseCollisionType(std::string_view text)
{
    if (text == "triangle")
        return cook::CollisionType::TriangleMesh;
    if (text == "simplified")
        return cook::CollisionType::SimplifiedTriangleMesh;
    if (text == "convex_decomposition")
        return cook::CollisionType::ConvexDecomposition;
    return cook::CollisionType::None;
}

glm::vec3 AxisVector(MeshAxis axis)
{
    switch (axis)
    {
    case MeshAxis::PositiveX:
        return {1, 0, 0};
    case MeshAxis::NegativeX:
        return {-1, 0, 0};
    case MeshAxis::PositiveY:
        return {0, 1, 0};
    case MeshAxis::NegativeY:
        return {0, -1, 0};
    case MeshAxis::PositiveZ:
        return {0, 0, 1};
    default:
        return {0, 0, -1};
    }
}

std::string AxisName(MeshAxis axis)
{
    static constexpr const char *names[] = {"+x", "-x", "+y", "-y", "+z", "-z"};
    return names[static_cast<size_t>(axis)];
}

MeshAxis ParseAxis(std::string_view text, MeshAxis fallback)
{
    for (uint8_t index = 0; index < 6; ++index)
        if (text == AxisName(static_cast<MeshAxis>(index)))
            return static_cast<MeshAxis>(index);
    return fallback;
}

glm::vec3 ParseVec3(std::string_view text, glm::vec3 fallback)
{
    std::string copy(text);
    char *cursor = copy.data();
    for (int component = 0; component < 3; ++component)
    {
        char *end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(value))
            return fallback;
        fallback[component] = value;
        if (component < 2)
        {
            if (*end != ',')
                return fallback;
            cursor = end + 1;
        }
    }
    return fallback;
}

std::string Vec3(glm::vec3 value)
{
    return Float(value.x) + ',' + Float(value.y) + ',' + Float(value.z);
}

bool ParseBool(std::string_view text, bool fallback)
{
    return text == "true" || text == "1" ? true : text == "false" || text == "0" ? false : fallback;
}

float ParseFloat(std::string_view text, float fallback)
{
    std::string copy(text);
    char *end = nullptr;
    const float value = std::strtof(copy.c_str(), &end);
    return end == copy.c_str() + copy.size() && std::isfinite(value) ? value : fallback;
}

uint32_t ParseU32(std::string_view text, uint32_t fallback)
{
    try
    {
        return static_cast<uint32_t>(std::stoul(std::string(text)));
    }
    catch (...)
    {
        return fallback;
    }
}

std::vector<uint8_t> DecodeBase64(std::string_view encoded)
{
    static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> output;
    uint32_t accumulator = 0;
    int bits = 0;
    for (char character : encoded)
    {
        if (character == '=')
            break;
        const size_t position = alphabet.find(character);
        if (position == std::string_view::npos)
            continue;
        accumulator = (accumulator << 6u) | static_cast<uint32_t>(position);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            output.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xffu));
        }
    }
    return output;
}

std::string ImageExtension(const cgltf_image &image)
{
    if (image.mime_type)
    {
        const std::string_view mime(image.mime_type);
        if (mime == "image/jpeg")
            return ".jpg";
        if (mime == "image/webp")
            return ".webp";
        if (mime == "image/ktx2")
            return ".ktx2";
    }
    return ".png";
}

bool ExtractImage(const cgltf_image &image, const std::filesystem::path &modelPath,
                  const std::filesystem::path &destination, std::filesystem::path &source, std::string *error)
{
    if (image.uri && !std::string_view(image.uri).starts_with("data:"))
    {
        source = (modelPath.parent_path() / std::filesystem::path(image.uri)).lexically_normal();
        if (!std::filesystem::is_regular_file(source))
        {
            if (error)
                *error = "Missing model texture: " + source.generic_string();
            return false;
        }
        return true;
    }
    std::vector<std::byte> bytes;
    if (image.buffer_view && image.buffer_view->buffer && image.buffer_view->buffer->data)
    {
        const auto *begin = static_cast<const std::byte *>(image.buffer_view->buffer->data) + image.buffer_view->offset;
        bytes.assign(begin, begin + image.buffer_view->size);
    }
    else if (image.uri)
    {
        const std::string_view uri(image.uri);
        const size_t comma = uri.find(',');
        if (comma == std::string_view::npos)
        {
            if (error)
                *error = "Malformed embedded image URI";
            return false;
        }
        const auto decoded = DecodeBase64(uri.substr(comma + 1));
        bytes.resize(decoded.size());
        std::memcpy(bytes.data(), decoded.data(), decoded.size());
    }
    else
    {
        if (error)
            *error = "glTF image has no URI or buffer view";
        return false;
    }
    source = destination;
    return WriteFileAtomic(source, bytes, error);
}

TextureUsage UsageForImage(const cgltf_data &data, const cgltf_image *image)
{
    TextureUsage usage = TextureUsage::Color;
    auto uses = [&](const cgltf_texture_view &view) { return view.texture && view.texture->image == image; };
    for (cgltf_size index = 0; index < data.materials_count; ++index)
    {
        const cgltf_material &material = data.materials[index];
        if (uses(material.normal_texture))
            return TextureUsage::NormalMap;
        if (uses(material.occlusion_texture) ||
            (material.has_pbr_metallic_roughness && uses(material.pbr_metallic_roughness.metallic_roughness_texture)))
            usage = TextureUsage::LinearData;
    }
    return usage;
}

AssetGuid TextureGuid(const cgltf_data &data, const cgltf_texture_view &view, const std::vector<AssetGuid> &imageGuids)
{
    if (!view.texture || !view.texture->image)
        return {};
    const ptrdiff_t index = view.texture->image - data.images;
    return index >= 0 && static_cast<size_t>(index) < imageGuids.size() ? imageGuids[index] : AssetGuid{};
}

void WriteMaterialSettings(AssetDescriptor &descriptor, const cgltf_data &data, const cgltf_material &source,
                           const std::vector<AssetGuid> &images)
{
    const auto &pbr = source.pbr_metallic_roughness;
    auto &values = descriptor.Settings;
    values["shader"] = source.unlit ? "unlit" : "pbr";
    values["base_color"] =
        Float(pbr.base_color_factor[0]) + ',' + Float(pbr.base_color_factor[1]) + ',' + Float(pbr.base_color_factor[2]);
    values["base_alpha"] = Float(pbr.base_color_factor[3]);
    values["metallic"] = Float(pbr.metallic_factor);
    values["roughness"] = Float(pbr.roughness_factor);
    values["emissive"] = Float(source.emissive_factor[0]) + ',' + Float(source.emissive_factor[1]) + ',' +
                         Float(source.emissive_factor[2]);
    values["alpha_mode"] = source.alpha_mode == cgltf_alpha_mode_mask    ? "mask"
                           : source.alpha_mode == cgltf_alpha_mode_blend ? "blend"
                                                                         : "opaque";
    values["alpha_cutoff"] = Float(source.alpha_cutoff);
    values["double_sided"] = Bool(source.double_sided != 0);
    values["depth_test"] = "true";
    values["depth_write"] = source.alpha_mode == cgltf_alpha_mode_blend ? "false" : "true";
    const std::array<std::pair<const char *, AssetGuid>, 5> references = {
        {{"base_color_texture", TextureGuid(data, pbr.base_color_texture, images)},
         {"normal_texture", TextureGuid(data, source.normal_texture, images)},
         {"metallic_roughness_texture", TextureGuid(data, pbr.metallic_roughness_texture, images)},
         {"occlusion_texture", TextureGuid(data, source.occlusion_texture, images)},
         {"emissive_texture", TextureGuid(data, source.emissive_texture, images)}}};
    for (const auto &[key, guid] : references)
        if (guid.IsValid())
        {
            values[key] = guid.ToString();
            descriptor.AssetDependencies.push_back(guid);
        }
}

std::vector<AssetPropertySchema> MaterialProperties()
{
    return {
        {"shader", "Shader", "Shader", AssetPropertyType::String, "pbr"},
        {"base_color", "Base Color", "PBR", AssetPropertyType::Color, "0.8,0.8,0.8"},
        {"base_alpha", "Base Alpha", "PBR", AssetPropertyType::Number, "1", {}, 0.0, 1.0},
        {"metallic", "Metallic", "PBR", AssetPropertyType::Number, "0", {}, 0.0, 1.0},
        {"roughness", "Roughness", "PBR", AssetPropertyType::Number, "0.5", {}, 0.02, 1.0},
        {"emissive", "Emissive", "PBR", AssetPropertyType::Color, "0,0,0"},
        {"base_color_texture",
         "Base Color Texture",
         "Textures",
         AssetPropertyType::AssetReference,
         "",
         {},
         {},
         {},
         std::string(kTexture2DAssetType)},
        {"normal_texture",
         "Normal Texture",
         "Textures",
         AssetPropertyType::AssetReference,
         "",
         {},
         {},
         {},
         std::string(kTexture2DAssetType)},
        {"metallic_roughness_texture",
         "Metallic Roughness",
         "Textures",
         AssetPropertyType::AssetReference,
         "",
         {},
         {},
         {},
         std::string(kTexture2DAssetType)},
        {"occlusion_texture",
         "Occlusion",
         "Textures",
         AssetPropertyType::AssetReference,
         "",
         {},
         {},
         {},
         std::string(kTexture2DAssetType)},
        {"emissive_texture",
         "Emissive Texture",
         "Textures",
         AssetPropertyType::AssetReference,
         "",
         {},
         {},
         {},
         std::string(kTexture2DAssetType)},
        {"alpha_mode",
         "Alpha Mode",
         "Render State",
         AssetPropertyType::Enumeration,
         "opaque",
         {"opaque", "mask", "blend"}},
        {"alpha_cutoff",
         "Alpha Cutoff",
         "Render State",
         AssetPropertyType::Number,
         "0.5",
         {},
         0.0,
         1.0,
         {},
         "alpha_mode",
         "mask"},
        {"double_sided", "Double Sided", "Render State", AssetPropertyType::Boolean, "false"},
        {"depth_test", "Depth Test", "Render State", AssetPropertyType::Boolean, "true"},
        {"depth_write", "Depth Write", "Render State", AssetPropertyType::Boolean, "true"},
    };
}

std::vector<AssetPropertySchema> MeshProperties()
{
    return {
        {"source_up",
         "Source Up",
         "Coordinates",
         AssetPropertyType::Enumeration,
         "+y",
         {"+x", "-x", "+y", "-y", "+z", "-z"}},
        {"source_forward",
         "Source Forward",
         "Coordinates",
         AssetPropertyType::Enumeration,
         "+z",
         {"+x", "-x", "+y", "-y", "+z", "-z"}},
        {"handedness", "Handedness", "Coordinates", AssetPropertyType::Enumeration, "right", {"right", "left"}},
        {"translation", "Translation", "Transform", AssetPropertyType::String, "0,0,0"},
        {"rotation", "Rotation", "Transform", AssetPropertyType::String, "0,0,0"},
        {"scale", "Scale", "Transform", AssetPropertyType::String, "1,1,1"},
        {"bake_transform", "Bake Transform", "Transform", AssetPropertyType::Boolean, "false"},
        {"flatten_hierarchy", "Flatten Hierarchy", "Hierarchy", AssetPropertyType::Boolean, "false"},
        {"remove_empty_nodes", "Remove Empty Nodes", "Hierarchy", AssetPropertyType::Boolean, "true"},
        {"import_hidden_nodes", "Import Hidden Nodes", "Hierarchy", AssetPropertyType::Boolean, "false"},
        {"merge_meshes", "Merge Meshes", "Geometry", AssetPropertyType::Boolean, "false"},
        {"generate_normals", "Generate Missing Normals", "Geometry", AssetPropertyType::Boolean, "true"},
        {"generate_tangents", "Generate Missing Tangents", "Geometry", AssetPropertyType::Boolean, "true"},
        {"weld_vertices", "Weld Vertices", "Optimization", AssetPropertyType::Boolean, "false"},
        {"remove_degenerate", "Remove Degenerate Triangles", "Optimization", AssetPropertyType::Boolean, "true"},
        {"optimize_indices", "Optimize Indices", "Optimization", AssetPropertyType::Boolean, "true"},
        {"generate_lods", "Generate LODs", "LOD", AssetPropertyType::Boolean, "false"},
        {"lod_count", "LOD Count", "LOD", AssetPropertyType::Integer, "1", {}, 1.0, 8.0, {}, "generate_lods", "true"},
        {"lod_ratio",
         "Triangle Ratio",
         "LOD",
         AssetPropertyType::Number,
         "0.5",
         {},
         0.05,
         0.95,
         {},
         "generate_lods",
         "true"},
        {"lod_error", "Maximum Relative Error", "LOD", AssetPropertyType::Number, "0.02", {}, 0.0, 1.0, {},
         "generate_lods", "true"},
        {"lod_aggressive", "Aggressive Fallback", "LOD", AssetPropertyType::Boolean, "false", {}, {}, {}, {},
         "generate_lods", "true"},
        {"collision_mode",
         "Collision Mode",
         "Collision",
         AssetPropertyType::Enumeration,
         "none",
         {"none", "triangle", "simplified", "convex_decomposition"}},
        {"collision_ratio", "Triangle Ratio", "Collision", AssetPropertyType::Number, "0.25", {}, 0.01, 1.0},
        {"collision_error", "Simplification Error", "Collision", AssetPropertyType::Number, "0.02", {}, 0.0,
         1.0},
        {"collision_max_hulls", "Maximum Convex Hulls", "Collision", AssetPropertyType::Integer, "8", {}, 1.0,
         256.0},
        {"collision_hull_vertices", "Vertices per Hull", "Collision", AssetPropertyType::Integer, "64", {}, 4.0,
         1024.0},
        {"collision_resolution", "Voxel Resolution", "Collision", AssetPropertyType::Integer, "100000", {},
         10000.0, 10000000.0},
        {"collision_convex_error", "Convex Volume Error %", "Collision", AssetPropertyType::Number, "1", {},
         0.01, 100.0},
    };
}

MaterialAssetData ReadMaterial(const AssetDescriptor &descriptor)
{
    const auto &values = descriptor.Settings;
    auto get = [&](std::string_view key, std::string_view fallback) {
        const auto found = values.find(std::string(key));
        return found == values.end() ? std::string(fallback) : found->second;
    };
    MaterialAssetData data;
    data.Shader = get("shader", "pbr");
    data.Value.Albedo = ParseVec3(get("base_color", "0.8,0.8,0.8"), data.Value.Albedo);
    data.Value.BaseColorAlpha = ParseFloat(get("base_alpha", "1"), 1);
    data.Value.Metallic = ParseFloat(get("metallic", "0"), 0);
    data.Value.Roughness = ParseFloat(get("roughness", "0.5"), 0.5f);
    data.Value.Emissive = ParseVec3(get("emissive", "0,0,0"), {});
    data.Value.Alpha = get("alpha_mode", "opaque") == "mask" ? Material::AlphaMode::Mask : Material::AlphaMode::Opaque;
    data.Value.AlphaCutoff = ParseFloat(get("alpha_cutoff", "0.5"), 0.5f);
    data.DoubleSided = ParseBool(get("double_sided", "false"), false);
    data.DepthTest = ParseBool(get("depth_test", "true"), true);
    data.DepthWrite = ParseBool(get("depth_write", "true"), true);
    auto reference = [&](std::string_view key, AssetHandle<TextureData> &handle) {
        if (const auto guid = AssetGuid::Parse(get(key, "")))
            handle.Guid = *guid;
    };
    reference("base_color_texture", data.BaseColor);
    reference("normal_texture", data.Normal);
    reference("metallic_roughness_texture", data.MetallicRoughness);
    reference("occlusion_texture", data.Occlusion);
    reference("emissive_texture", data.Emissive);
    return data;
}

std::vector<std::byte> EncodeMaterial(const MaterialAssetData &data)
{
    resources::BinaryWriter writer;
    writer.WriteU32(1);
    writer.WriteString(data.Shader);
    for (float value :
         {data.Value.Albedo.x, data.Value.Albedo.y, data.Value.Albedo.z, data.Value.BaseColorAlpha, data.Value.Metallic,
          data.Value.Roughness, data.Value.Emissive.x, data.Value.Emissive.y, data.Value.Emissive.z,
          data.Value.AmbientOcclusion, data.Value.SpecularF0, data.Value.AlphaCutoff})
        writer.WriteF32(value);
    writer.WriteU8(static_cast<uint8_t>(data.Value.Alpha));
    writer.WriteU8(data.DoubleSided);
    writer.WriteU8(data.DepthTest);
    writer.WriteU8(data.DepthWrite);
    for (AssetGuid guid :
         {data.BaseColor.Guid, data.Normal.Guid, data.MetallicRoughness.Guid, data.Occlusion.Guid, data.Emissive.Guid})
    {
        writer.WriteU64(guid.High);
        writer.WriteU64(guid.Low);
    }
    writer.WriteU32(static_cast<uint32_t>(data.Features.size()));
    for (const auto &feature : data.Features)
        writer.WriteString(feature);
    return writer.TakeData();
}

std::shared_ptr<MaterialAssetData> LoadMaterial(const resources::ResourceLoadContext &context, std::string *error)
{
    resources::BinaryReader reader(context.Payload);
    uint32_t version = 0;
    auto data = std::make_shared<MaterialAssetData>();
    if (!reader.ReadU32(version) || version != 1 || !reader.ReadString(data->Shader, 4096))
    {
        if (error)
            *error = "Malformed material header";
        return {};
    }
    float *values[] = {&data->Value.Albedo.x,         &data->Value.Albedo.y,   &data->Value.Albedo.z,
                       &data->Value.BaseColorAlpha,   &data->Value.Metallic,   &data->Value.Roughness,
                       &data->Value.Emissive.x,       &data->Value.Emissive.y, &data->Value.Emissive.z,
                       &data->Value.AmbientOcclusion, &data->Value.SpecularF0, &data->Value.AlphaCutoff};
    for (float *value : values)
        if (!reader.ReadF32(*value))
        {
            if (error)
                *error = "Malformed material parameters";
            return {};
        }
    uint8_t alpha = 0, doubleSided = 0, depthTest = 0, depthWrite = 0;
    if (!reader.ReadU8(alpha) || !reader.ReadU8(doubleSided) || !reader.ReadU8(depthTest) ||
        !reader.ReadU8(depthWrite) || alpha > 1)
    {
        if (error)
            *error = "Malformed material state";
        return {};
    }
    data->Value.Alpha = static_cast<Material::AlphaMode>(alpha);
    data->DoubleSided = doubleSided != 0;
    data->DepthTest = depthTest != 0;
    data->DepthWrite = depthWrite != 0;
    AssetHandle<TextureData> *handles[] = {&data->BaseColor, &data->Normal, &data->MetallicRoughness, &data->Occlusion,
                                           &data->Emissive};
    for (auto *handle : handles)
        if (!reader.ReadU64(handle->Guid.High) || !reader.ReadU64(handle->Guid.Low))
        {
            if (error)
                *error = "Malformed material texture handle";
            return {};
        }
    uint32_t featureCount = 0;
    if (!reader.ReadU32(featureCount) || featureCount > 256)
    {
        if (error)
            *error = "Malformed material features";
        return {};
    }
    for (uint32_t i = 0; i < featureCount; ++i)
    {
        std::string feature;
        if (!reader.ReadString(feature, 256))
        {
            if (error)
                *error = reader.Error();
            return {};
        }
        data->Features.push_back(std::move(feature));
    }
    if (reader.Remaining() != 0)
    {
        if (error)
            *error = "Material resource contains trailing data";
        return {};
    }
    return data;
}

void WriteMatrix(resources::BinaryWriter &writer, const glm::mat4 &matrix)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            writer.WriteF32(matrix[column][row]);
}
bool ReadMatrix(resources::BinaryReader &reader, glm::mat4 &matrix)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!reader.ReadF32(matrix[column][row]))
                return false;
    return true;
}

void WriteMeshData(resources::BinaryWriter &writer, const MeshData &mesh)
{
    writer.WriteU64(mesh.Vertices.size());
    writer.WriteU64(mesh.Indices.size());
    for (const Vertex &vertex : mesh.Vertices)
        for (float value : {vertex.Position.x, vertex.Position.y, vertex.Position.z, vertex.Normal.x,
                            vertex.Normal.y, vertex.Normal.z, vertex.Tangent.x, vertex.Tangent.y, vertex.Tangent.z,
                            vertex.Tangent.w, vertex.UV.x, vertex.UV.y})
            writer.WriteF32(value);
    for (uint32_t index : mesh.Indices)
        writer.WriteU32(index);
}

bool ReadMeshData(resources::BinaryReader &reader, MeshData &mesh, std::string *error)
{
    uint64_t vertices = 0, indices = 0;
    if (!reader.ReadU64(vertices) || !reader.ReadU64(indices) || vertices > 100000000 || indices > 300000000 ||
        indices % 3u != 0)
    {
        if (error)
            *error = "Malformed static mesh geometry size";
        return false;
    }
    mesh.Vertices.resize(static_cast<size_t>(vertices));
    mesh.Indices.resize(static_cast<size_t>(indices));
    for (Vertex &vertex : mesh.Vertices)
    {
        float *values[] = {&vertex.Position.x, &vertex.Position.y, &vertex.Position.z, &vertex.Normal.x,
                           &vertex.Normal.y,   &vertex.Normal.z,   &vertex.Tangent.x,  &vertex.Tangent.y,
                           &vertex.Tangent.z,  &vertex.Tangent.w,  &vertex.UV.x,       &vertex.UV.y};
        for (float *value : values)
            if (!reader.ReadF32(*value) || !std::isfinite(*value))
            {
                if (error)
                    *error = "Truncated or non-finite mesh vertices";
                return false;
            }
    }
    for (uint32_t &index : mesh.Indices)
        if (!reader.ReadU32(index) || index >= vertices)
        {
            if (error)
                *error = "Invalid mesh index";
            return false;
        }
    return true;
}

std::vector<std::byte> EncodeMesh(const StaticMeshData &data)
{
    resources::BinaryWriter writer;
    writer.WriteU32(2);
    writer.WriteU32(static_cast<uint32_t>(data.Parts.size()));
    for (const auto &part : data.Parts)
    {
        WriteMatrix(writer, part.Transform);
        writer.WriteU64(part.Material.Guid.High);
        writer.WriteU64(part.Material.Guid.Low);
        writer.WriteString(part.MaterialSlot);
        WriteMeshData(writer, *part.Mesh);
        writer.WriteU32(static_cast<uint32_t>(part.Lods.size()));
        for (const cook::MeshLod &lod : part.Lods)
        {
            writer.WriteF32(lod.TriangleRatio);
            writer.WriteF32(lod.RelativeError);
            WriteMeshData(writer, lod.Mesh);
        }
    }
    for (float value : {data.BoundsMinimum.x, data.BoundsMinimum.y, data.BoundsMinimum.z, data.BoundsMaximum.x,
                        data.BoundsMaximum.y, data.BoundsMaximum.z})
        writer.WriteF32(value);
    writer.WriteU8(static_cast<uint8_t>(data.Collision.Type));
    writer.WriteU32(static_cast<uint32_t>(data.Collision.Meshes.size()));
    for (const cook::CollisionMesh &mesh : data.Collision.Meshes)
    {
        writer.WriteU8(mesh.Convex ? 1u : 0u);
        writer.WriteU64(mesh.Vertices.size());
        writer.WriteU64(mesh.Indices.size());
        writer.WriteU32(static_cast<uint32_t>(mesh.Bvh.size()));
        for (const glm::vec3 &vertex : mesh.Vertices)
            for (float value : {vertex.x, vertex.y, vertex.z})
                writer.WriteF32(value);
        for (uint32_t index : mesh.Indices)
            writer.WriteU32(index);
        for (const cook::CollisionBvhNode &node : mesh.Bvh)
        {
            for (float value : {node.BoundsMinimum.x, node.BoundsMinimum.y, node.BoundsMinimum.z,
                                node.BoundsMaximum.x, node.BoundsMaximum.y, node.BoundsMaximum.z})
                writer.WriteF32(value);
            writer.WriteU32(node.LeftChild);
            writer.WriteU32(node.RightChild);
            writer.WriteU32(node.FirstTriangle);
            writer.WriteU32(node.TriangleCount);
        }
    }
    return writer.TakeData();
}

std::shared_ptr<StaticMeshData> LoadMesh(const resources::ResourceLoadContext &context, std::string *error)
{
    resources::BinaryReader reader(context.Payload);
    uint32_t version = 0, count = 0;
    if (!reader.ReadU32(version) || (version != 1 && version != 2) || !reader.ReadU32(count) || count > 100000)
    {
        if (error)
            *error = "Malformed static mesh header";
        return {};
    }
    auto data = std::make_shared<StaticMeshData>();
    data->Parts.reserve(count);
    for (uint32_t partIndex = 0; partIndex < count; ++partIndex)
    {
        StaticMeshPart part;
        if (!ReadMatrix(reader, part.Transform) || !reader.ReadU64(part.Material.Guid.High) ||
            !reader.ReadU64(part.Material.Guid.Low) || !reader.ReadString(part.MaterialSlot, 4096))
        {
            if (error)
                *error = "Malformed static mesh part";
            return {};
        }
        part.Mesh = std::make_shared<MeshData>();
        if (!ReadMeshData(reader, *part.Mesh, error))
            return {};
        if (version >= 2)
        {
            uint32_t lodCount = 0;
            if (!reader.ReadU32(lodCount) || lodCount > 7u)
            {
                if (error)
                    *error = "Malformed mesh LOD count";
                return {};
            }
            part.Lods.resize(lodCount);
            size_t previousIndexCount = part.Mesh->Indices.size();
            float previousRatio = 1.0f;
            for (cook::MeshLod &lod : part.Lods)
            {
                if (!reader.ReadF32(lod.TriangleRatio) || !reader.ReadF32(lod.RelativeError) ||
                    !std::isfinite(lod.TriangleRatio) || !std::isfinite(lod.RelativeError) ||
                    lod.TriangleRatio <= 0.0f || lod.TriangleRatio >= previousRatio || lod.RelativeError < 0.0f ||
                    lod.RelativeError > 1.0f || !ReadMeshData(reader, lod.Mesh, error) ||
                    lod.Mesh.Indices.size() >= previousIndexCount)
                {
                    if (error && error->empty())
                        *error = "Malformed mesh LOD";
                    return {};
                }
                previousRatio = lod.TriangleRatio;
                previousIndexCount = lod.Mesh.Indices.size();
            }
        }
        data->Parts.push_back(std::move(part));
    }
    float *bounds[] = {&data->BoundsMinimum.x, &data->BoundsMinimum.y, &data->BoundsMinimum.z,
                       &data->BoundsMaximum.x, &data->BoundsMaximum.y, &data->BoundsMaximum.z};
    for (float *value : bounds)
        if (!reader.ReadF32(*value) || !std::isfinite(*value))
        {
            if (error)
                *error = "Missing mesh bounds";
            return {};
        }
    if (version >= 2)
    {
        uint8_t collisionType = 0;
        uint32_t collisionMeshCount = 0;
        if (!reader.ReadU8(collisionType) || collisionType > static_cast<uint8_t>(cook::CollisionType::ConvexDecomposition) ||
            !reader.ReadU32(collisionMeshCount) || collisionMeshCount > 256u)
        {
            if (error)
                *error = "Malformed collision resource header";
            return {};
        }
        data->Collision.Type = static_cast<cook::CollisionType>(collisionType);
        if ((data->Collision.Type == cook::CollisionType::None) != (collisionMeshCount == 0u))
        {
            if (error)
                *error = "Collision type and mesh count are inconsistent";
            return {};
        }
        data->Collision.Meshes.resize(collisionMeshCount);
        for (cook::CollisionMesh &mesh : data->Collision.Meshes)
        {
            uint8_t convex = 0;
            uint64_t vertexCount = 0, indexCount = 0;
            uint32_t nodeCount = 0;
            if (!reader.ReadU8(convex) || convex > 1u || !reader.ReadU64(vertexCount) ||
                !reader.ReadU64(indexCount) || !reader.ReadU32(nodeCount) || vertexCount > 100000000u ||
                indexCount > 300000000u || indexCount % 3u != 0 || nodeCount > indexCount || vertexCount == 0u ||
                indexCount == 0u || nodeCount == 0u)
            {
                if (error)
                    *error = "Malformed collision mesh size";
                return {};
            }
            mesh.Convex = convex != 0;
            mesh.Vertices.resize(static_cast<size_t>(vertexCount));
            mesh.Indices.resize(static_cast<size_t>(indexCount));
            mesh.Bvh.resize(nodeCount);
            for (glm::vec3 &vertex : mesh.Vertices)
                for (int component = 0; component < 3; ++component)
                    if (!reader.ReadF32(vertex[component]) || !std::isfinite(vertex[component]))
                    {
                        if (error)
                            *error = "Malformed collision vertices";
                        return {};
                    }
            for (uint32_t &index : mesh.Indices)
                if (!reader.ReadU32(index) || index >= vertexCount)
                {
                    if (error)
                        *error = "Malformed collision indices";
                    return {};
                }
            for (cook::CollisionBvhNode &node : mesh.Bvh)
            {
                float *nodeBounds[] = {&node.BoundsMinimum.x, &node.BoundsMinimum.y, &node.BoundsMinimum.z,
                                       &node.BoundsMaximum.x, &node.BoundsMaximum.y, &node.BoundsMaximum.z};
                for (float *value : nodeBounds)
                    if (!reader.ReadF32(*value) || !std::isfinite(*value))
                    {
                        if (error)
                            *error = "Malformed collision BVH bounds";
                        return {};
                    }
                if (!reader.ReadU32(node.LeftChild) || !reader.ReadU32(node.RightChild) ||
                    !reader.ReadU32(node.FirstTriangle) || !reader.ReadU32(node.TriangleCount))
                {
                    if (error)
                        *error = "Malformed collision BVH node";
                    return {};
                }
                const bool leaf = node.TriangleCount != 0;
                if ((leaf && static_cast<uint64_t>(node.FirstTriangle + node.TriangleCount) * 3u > indexCount) ||
                    (!leaf && (node.LeftChild >= nodeCount || node.RightChild >= nodeCount)))
                {
                    if (error)
                        *error = "Collision BVH references invalid children or triangles";
                    return {};
                }
            }
        }
    }
    if (reader.Remaining() != 0)
    {
        if (error)
            *error = "Static mesh resource contains trailing data";
        return {};
    }
    return data;
}

void RemoveDegenerates(MeshData &mesh)
{
    std::vector<uint32_t> indices;
    indices.reserve(mesh.Indices.size());
    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
    {
        const uint32_t a = mesh.Indices[i], b = mesh.Indices[i + 1], c = mesh.Indices[i + 2];
        if (a == b || b == c || a == c)
            continue;
        const glm::vec3 cross = glm::cross(mesh.Vertices[b].Position - mesh.Vertices[a].Position,
                                           mesh.Vertices[c].Position - mesh.Vertices[a].Position);
        if (glm::dot(cross, cross) < 1e-16f)
            continue;
        indices.insert(indices.end(), {a, b, c});
    }
    mesh.Indices = std::move(indices);
}

} // namespace

glm::mat4 ConvertCoordinateSystem(MeshAxis sourceUp, MeshAxis sourceForward, MeshHandedness handedness)
{
    const glm::vec3 up = AxisVector(sourceUp), forward = AxisVector(sourceForward);
    if (std::abs(glm::dot(up, forward)) > 0.001f)
        return glm::mat4(1.0f);
    glm::vec3 right = glm::normalize(glm::cross(up, forward));
    if (handedness == MeshHandedness::LeftHanded)
        right = -right;
    glm::mat3 sourceBasis(right, up, forward);
    return glm::mat4(glm::inverse(sourceBasis));
}

glm::mat4 BuildMeshImportTransform(const StaticMeshAssetSettings &settings)
{
    glm::mat4 result = glm::translate(glm::mat4(1), settings.Translation);
    result = glm::rotate(result, glm::radians(settings.RotationDegrees.z), {0, 0, 1});
    result = glm::rotate(result, glm::radians(settings.RotationDegrees.y), {0, 1, 0});
    result = glm::rotate(result, glm::radians(settings.RotationDegrees.x), {1, 0, 0});
    return result * glm::scale(glm::mat4(1), settings.Scale) *
           ConvertCoordinateSystem(settings.SourceUp, settings.SourceForward, settings.Handedness);
}

bool ValidateMeshImportSettings(const StaticMeshAssetSettings &settings, std::vector<AssetDiagnostic> &diagnostics)
{
    if (std::abs(glm::dot(AxisVector(settings.SourceUp), AxisVector(settings.SourceForward))) > 0.001f)
        diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "mesh_axis_conflict",
                                         "Source up and forward axes must be perpendicular."));
    if (!std::isfinite(settings.Scale.x) || !std::isfinite(settings.Scale.y) || !std::isfinite(settings.Scale.z) ||
        std::abs(settings.Scale.x) < 1e-6f || std::abs(settings.Scale.y) < 1e-6f || std::abs(settings.Scale.z) < 1e-6f)
        diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "mesh_scale_invalid",
                                         "Mesh scale must be finite and non-zero."));
    if (settings.LodCount < 1 || settings.LodCount > 8 || settings.LodTriangleRatio <= 0 ||
        settings.LodTriangleRatio >= 1 || settings.LodTargetError < 0.0f || settings.LodTargetError > 1.0f)
        diagnostics.push_back(
            Diagnostic(AssetDiagnosticSeverity::FatalError, "mesh_lod_invalid", "LOD count, ratio or error is invalid."));
    cook::CollisionCookSettings collision;
    collision.Type = settings.Collision;
    collision.TriangleRatio = settings.CollisionTriangleRatio;
    collision.SimplificationError = settings.CollisionSimplificationError;
    collision.MaxConvexHulls = settings.CollisionMaxConvexHulls;
    collision.MaxVerticesPerHull = settings.CollisionMaxVerticesPerHull;
    collision.VoxelResolution = settings.CollisionVoxelResolution;
    collision.ConvexErrorPercent = settings.CollisionConvexErrorPercent;
    std::string collisionError;
    if (!cook::ValidateCollisionCookSettings(collision, &collisionError))
        diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "collision_settings_invalid",
                                         std::move(collisionError)));
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const AssetDiagnostic &diagnostic) {
        return diagnostic.Severity == AssetDiagnosticSeverity::FatalError;
    });
}

StaticMeshAssetSettings ReadStaticMeshAssetSettings(const AssetDescriptor &descriptor, std::string_view profile)
{
    StaticMeshAssetSettings settings;
    const auto values = descriptor.ResolveSettings(profile);
    auto get = [&](std::string_view key, std::string_view fallback) {
        const auto found = values.find(std::string(key));
        return found == values.end() ? std::string(fallback) : found->second;
    };
    settings.SourceUp = ParseAxis(get("source_up", "+y"), settings.SourceUp);
    settings.SourceForward = ParseAxis(get("source_forward", "+z"), settings.SourceForward);
    settings.Handedness =
        get("handedness", "right") == "left" ? MeshHandedness::LeftHanded : MeshHandedness::RightHanded;
    settings.Translation = ParseVec3(get("translation", "0,0,0"), settings.Translation);
    settings.RotationDegrees = ParseVec3(get("rotation", "0,0,0"), settings.RotationDegrees);
    settings.Scale = ParseVec3(get("scale", "1,1,1"), settings.Scale);
    settings.BakeTransform = ParseBool(get("bake_transform", "false"), false);
    settings.FlattenHierarchy = ParseBool(get("flatten_hierarchy", "false"), false);
    settings.RemoveEmptyNodes = ParseBool(get("remove_empty_nodes", "true"), true);
    settings.ImportHiddenNodes = ParseBool(get("import_hidden_nodes", "false"), false);
    settings.MergeMeshes = ParseBool(get("merge_meshes", "false"), false);
    settings.GenerateNormals = ParseBool(get("generate_normals", "true"), true);
    settings.GenerateTangents = ParseBool(get("generate_tangents", "true"), true);
    settings.WeldVertices = ParseBool(get("weld_vertices", "false"), false);
    settings.RemoveDegenerateTriangles = ParseBool(get("remove_degenerate", "true"), true);
    settings.OptimizeIndices = ParseBool(get("optimize_indices", "true"), true);
    settings.GenerateLods = ParseBool(get("generate_lods", "false"), false);
    settings.LodCount = ParseU32(get("lod_count", "1"), 1);
    settings.LodTriangleRatio = ParseFloat(get("lod_ratio", "0.5"), 0.5f);
    settings.LodTargetError = ParseFloat(get("lod_error", "0.02"), 0.02f);
    settings.LodAggressive = ParseBool(get("lod_aggressive", "false"), false);
    settings.ImportCollision = ParseBool(get("import_collision", "false"), false);
    settings.Collision = ParseCollisionType(get("collision_mode", settings.ImportCollision ? "triangle" : "none"));
    settings.CollisionTriangleRatio = ParseFloat(get("collision_ratio", "0.25"), 0.25f);
    settings.CollisionSimplificationError = ParseFloat(get("collision_error", "0.02"), 0.02f);
    settings.CollisionMaxConvexHulls = ParseU32(get("collision_max_hulls", "8"), 8);
    settings.CollisionMaxVerticesPerHull = ParseU32(get("collision_hull_vertices", "64"), 64);
    settings.CollisionVoxelResolution = ParseU32(get("collision_resolution", "100000"), 100000);
    settings.CollisionConvexErrorPercent = ParseFloat(get("collision_convex_error", "1"), 1.0f);
    return settings;
}

void WriteStaticMeshAssetSettings(AssetDescriptor &descriptor, const StaticMeshAssetSettings &settings)
{
    auto &values = descriptor.Settings;
    values["source_up"] = AxisName(settings.SourceUp);
    values["source_forward"] = AxisName(settings.SourceForward);
    values["handedness"] = settings.Handedness == MeshHandedness::LeftHanded ? "left" : "right";
    values["translation"] = Vec3(settings.Translation);
    values["rotation"] = Vec3(settings.RotationDegrees);
    values["scale"] = Vec3(settings.Scale);
    values["bake_transform"] = Bool(settings.BakeTransform);
    values["flatten_hierarchy"] = Bool(settings.FlattenHierarchy);
    values["remove_empty_nodes"] = Bool(settings.RemoveEmptyNodes);
    values["import_hidden_nodes"] = Bool(settings.ImportHiddenNodes);
    values["merge_meshes"] = Bool(settings.MergeMeshes);
    values["generate_normals"] = Bool(settings.GenerateNormals);
    values["generate_tangents"] = Bool(settings.GenerateTangents);
    values["weld_vertices"] = Bool(settings.WeldVertices);
    values["remove_degenerate"] = Bool(settings.RemoveDegenerateTriangles);
    values["optimize_indices"] = Bool(settings.OptimizeIndices);
    values["generate_lods"] = Bool(settings.GenerateLods);
    values["lod_count"] = std::to_string(settings.LodCount);
    values["lod_ratio"] = Float(settings.LodTriangleRatio);
    values["lod_error"] = Float(settings.LodTargetError);
    values["lod_aggressive"] = Bool(settings.LodAggressive);
    values["collision_mode"] = CollisionTypeName(settings.Collision);
    values["collision_ratio"] = Float(settings.CollisionTriangleRatio);
    values["collision_error"] = Float(settings.CollisionSimplificationError);
    values["collision_max_hulls"] = std::to_string(settings.CollisionMaxConvexHulls);
    values["collision_hull_vertices"] = std::to_string(settings.CollisionMaxVerticesPerHull);
    values["collision_resolution"] = std::to_string(settings.CollisionVoxelResolution);
    values["collision_convex_error"] = Float(settings.CollisionConvexErrorPercent);
    values.erase("import_collision");
}

bool RegisterModelAssetTypes(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                             std::string *error)
{
    AssetTypeRegistration material;
    material.TypeId = std::string(kMaterialAssetType);
    material.DisplayName = "Material";
    material.DescriptorExtension = std::string(kMaterialDescriptorExtension);
    material.RuntimeType = std::string(kMaterialResourceType);
    material.Icon = "material";
    material.Properties = MaterialProperties();
    material.Validate = [](const AssetDescriptor &descriptor, const AssetValidationContext &) {
        std::vector<AssetDiagnostic> diagnostics;
        const auto data = ReadMaterial(descriptor);
        if (data.Value.Metallic < 0 || data.Value.Metallic > 1 || data.Value.Roughness < 0.02f ||
            data.Value.Roughness > 1)
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "material_pbr_range",
                                             "Material metallic/roughness values are outside their valid range."));
        return diagnostics;
    };
    material.Transform = [](const AssetDescriptor &descriptor, const AssetTransformContext &context,
                            AssetTransformOutput &output, std::string *) {
        AssetDescriptor resolved = descriptor;
        resolved.Settings = context.ResolvedSettings;
        resolved.PlatformOverrides.clear();
        const auto data = ReadMaterial(resolved);
        output.RuntimeType = std::string(kMaterialResourceType);
        output.ResourceVersion = 1;
        output.Payload = EncodeMaterial(data);
        for (AssetGuid guid : {data.BaseColor.Guid, data.Normal.Guid, data.MetallicRoughness.Guid, data.Occlusion.Guid,
                               data.Emissive.Guid})
            if (guid.IsValid())
                output.AssetDependencies.push_back(guid);
        return true;
    };
    material.Inspector = [](const AssetDescriptor &) { return MaterialProperties(); };
    if (!registry.Register(std::move(material), error) ||
        !resourceManager.RegisterLoader<MaterialAssetData>(std::string(kMaterialResourceType), LoadMaterial, error))
        return false;

    AssetTypeRegistration mesh;
    mesh.TypeId = std::string(kStaticMeshAssetType);
    mesh.DisplayName = "Static Mesh";
    mesh.DescriptorExtension = std::string(kStaticMeshDescriptorExtension);
    mesh.SourceExtensions = {"gltf", "glb"};
    mesh.ImportModes = {{"scene", "Static model and generated materials", 100}};
    mesh.RuntimeType = std::string(kStaticMeshResourceType);
    mesh.Icon = "static-mesh";
    mesh.TransformerVersion = 2;
    mesh.ResourceVersion = 2;
    mesh.Properties = MeshProperties();
    mesh.Import = [](const AssetImportRequest &request) {
        ImportResult result;
        cgltf_options options{};
        cgltf_data *raw = nullptr;
        const cgltf_result parsed = cgltf_parse_file(&options, request.SourcePath.string().c_str(), &raw);
        if (parsed != cgltf_result_success)
        {
            result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "gltf_parse_failed",
                                                    "Failed to parse glTF source.", request.SourcePath));
            return result;
        }
        const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
        if (cgltf_load_buffers(&options, data.get(), request.SourcePath.string().c_str()) != cgltf_result_success ||
            cgltf_validate(data.get()) != cgltf_result_success)
        {
            result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "gltf_validation_failed",
                                                    "glTF buffers or structure are invalid.", request.SourcePath));
            return result;
        }
        std::string relativeError;
        const auto modelRelative = NormalizeProjectRelative(request.ProjectRoot, request.SourcePath, &relativeError);
        if (modelRelative.empty())
        {
            result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "source_outside_project",
                                                    relativeError, request.SourcePath));
            return result;
        }
        ImportedAsset primary;
        primary.Descriptor.Name = request.SourcePath.stem().generic_string();
        primary.DescriptorPath = request.DestinationDirectory /
                                 (primary.Descriptor.Name + "." + std::string(kStaticMeshDescriptorExtension));
        AssetDescriptor existingPrimary;
        std::string existingError;
        if (LoadAssetDescriptor(primary.DescriptorPath, existingPrimary, &existingError) &&
            existingPrimary.Type == kStaticMeshAssetType)
        {
            primary.Descriptor = std::move(existingPrimary);
            primary.Reused = true;
            primary.Descriptor.Sources = {modelRelative};
            primary.Descriptor.SourceDependencies = {modelRelative};
            primary.Descriptor.AssetDependencies.clear();
            primary.Descriptor.State = AssetImportState::Dirty;
        }
        else
        {
            primary.Descriptor.Guid = AssetGuid::Generate();
            primary.Descriptor.Type = std::string(kStaticMeshAssetType);
            primary.Descriptor.Name = request.SourcePath.stem().generic_string();
            primary.Descriptor.Sources = {modelRelative};
            primary.Descriptor.SourceDependencies = {modelRelative};
            primary.Descriptor.State = AssetImportState::Dirty;
            WriteStaticMeshAssetSettings(primary.Descriptor, {});
        }
        for (cgltf_size index = 0; index < data->buffers_count; ++index)
            if (data->buffers[index].uri && !std::string_view(data->buffers[index].uri).starts_with("data:"))
            {
                std::string pathError;
                auto dependency = NormalizeProjectRelative(
                    request.ProjectRoot, request.SourcePath.parent_path() / data->buffers[index].uri, &pathError);
                if (!dependency.empty())
                    primary.Descriptor.SourceDependencies.push_back(dependency);
            }
        std::vector<AssetGuid> imageGuids(data->images_count);
        const auto generatedDir = request.DestinationDirectory / ".generated";
        for (cgltf_size index = 0; index < data->images_count; ++index)
        {
            std::filesystem::path source;
            std::string extractionError;
            if (!ExtractImage(data->images[index], request.SourcePath,
                              generatedDir / (primary.Descriptor.Name + "_image_" + std::to_string(index) +
                                              ImageExtension(data->images[index])),
                              source, &extractionError))
            {
                result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::RecoverableError,
                                                        "texture_extract_failed", extractionError, request.SourcePath));
                continue;
            }
            std::string pathError;
            const auto relative = NormalizeProjectRelative(request.ProjectRoot, source, &pathError);
            if (relative.empty())
                continue;
            ImportedAsset texture;
            texture.Descriptor.Guid = AssetGuid::Generate();
            texture.Descriptor.Type = std::string(kTexture2DAssetType);
            texture.Descriptor.Name = primary.Descriptor.Name + "_texture_" + std::to_string(index);
            texture.Descriptor.Sources = {relative};
            texture.Descriptor.SourceDependencies = {relative};
            texture.Descriptor.State = AssetImportState::Dirty;
            texture.Descriptor.Generated = true;
            texture.Descriptor.GeneratedBy = primary.Descriptor.Guid;
            TextureAssetSettings textureSettings;
            textureSettings.Usage = UsageForImage(*data, &data->images[index]);
            textureSettings.ColorSpace =
                (textureSettings.Usage == TextureUsage::Color) ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
            WriteTextureAssetSettings(texture.Descriptor, textureSettings);
            texture.DescriptorPath = request.DestinationDirectory /
                                     (texture.Descriptor.Name + "." + std::string(kTexture2DDescriptorExtension));
            AssetDescriptor existing;
            std::string ignored;
            if (LoadAssetDescriptor(texture.DescriptorPath, existing, &ignored) && existing.Type == kTexture2DAssetType)
            {
                if (existing.UserModified)
                    texture.Descriptor = existing;
                else
                    PreserveCookState(existing, texture.Descriptor);
                texture.Reused = true;
            }
            imageGuids[index] = texture.Descriptor.Guid;
            result.GeneratedAssets.push_back(std::move(texture));
        }
        std::vector<AssetGuid> materialGuids(data->materials_count);
        for (cgltf_size index = 0; index < data->materials_count; ++index)
        {
            ImportedAsset asset;
            asset.Descriptor.Guid = AssetGuid::Generate();
            asset.Descriptor.Type = std::string(kMaterialAssetType);
            asset.Descriptor.Name = data->materials[index].name
                                        ? data->materials[index].name
                                        : primary.Descriptor.Name + "_material_" + std::to_string(index);
            asset.Descriptor.State = AssetImportState::Dirty;
            asset.Descriptor.Generated = true;
            asset.Descriptor.GeneratedBy = primary.Descriptor.Guid;
            WriteMaterialSettings(asset.Descriptor, *data, data->materials[index], imageGuids);
            asset.DescriptorPath =
                request.DestinationDirectory / (primary.Descriptor.Name + "_material_" + std::to_string(index) + "." +
                                                std::string(kMaterialDescriptorExtension));
            AssetDescriptor existing;
            std::string ignored;
            if (LoadAssetDescriptor(asset.DescriptorPath, existing, &ignored) && existing.Type == kMaterialAssetType)
            {
                if (existing.UserModified)
                    asset.Descriptor = existing;
                else
                    PreserveCookState(existing, asset.Descriptor);
                asset.Reused = true;
            }
            materialGuids[index] = asset.Descriptor.Guid;
            primary.Descriptor.AssetDependencies.push_back(asset.Descriptor.Guid);
            primary.Descriptor.Settings["material_" + std::to_string(index)] = asset.Descriptor.Guid.ToString();
            result.GeneratedAssets.push_back(std::move(asset));
        }
        result.GeneratedAssets.push_back(std::move(primary));
        result.Statistics["images"] = data->images_count;
        result.Statistics["materials"] = data->materials_count;
        result.Statistics["meshes"] = data->meshes_count;
        result.Succeeded = true;
        return result;
    };
    mesh.Validate = [](const AssetDescriptor &descriptor, const AssetValidationContext &context) {
        std::vector<AssetDiagnostic> diagnostics;
        ValidateMeshImportSettings(ReadStaticMeshAssetSettings(descriptor, context.Profile), diagnostics);
        if (descriptor.Sources.size() != 1)
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "mesh_source_count",
                                             "Static mesh requires one source model."));
        return diagnostics;
    };
    mesh.Transform = [](const AssetDescriptor &descriptor, const AssetTransformContext &context,
                        AssetTransformOutput &output, std::string *transformError) {
        AssetDescriptor resolved = descriptor;
        resolved.Settings = context.ResolvedSettings;
        resolved.PlatformOverrides.clear();
        const auto settings = ReadStaticMeshAssetSettings(resolved);
        Scene scene;
        try
        {
            LoadGltfScene((context.ProjectRoot / descriptor.Sources[0]).string(), scene,
                          BuildMeshImportTransform(settings));
        }
        catch (const std::exception &exception)
        {
            if (transformError)
                *transformError = exception.what();
            return false;
        }
        StaticMeshData data;
        bool first = true;
        size_t partIndex = 0;
        double cacheAcmrBefore = 0.0;
        double cacheAcmrAfter = 0.0;
        uint64_t optimizedParts = 0;
        std::vector<Material> partMaterials;
        for (const MeshInstance &instance : scene.Instances())
        {
            if (!instance.Mesh || instance.Mesh->Vertices.empty() || instance.Mesh->Indices.empty())
                continue;
            StaticMeshPart part;
            part.Mesh = std::make_shared<MeshData>(*instance.Mesh);
            part.Transform = instance.Transform;
            part.MaterialSlot = "material_" + std::to_string(partIndex);
            if (const auto found = descriptor.Settings.find(part.MaterialSlot); found != descriptor.Settings.end())
                if (const auto guid = AssetGuid::Parse(found->second))
                    part.Material.Guid = *guid;
            if (settings.RemoveDegenerateTriangles)
                RemoveDegenerates(*part.Mesh);
            if (settings.BakeTransform)
            {
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(part.Transform)));
                for (Vertex &vertex : part.Mesh->Vertices)
                {
                    vertex.Position = glm::vec3(part.Transform * glm::vec4(vertex.Position, 1));
                    vertex.Normal = glm::normalize(normalMatrix * vertex.Normal);
                    vertex.Tangent =
                        glm::vec4(glm::normalize(normalMatrix * glm::vec3(vertex.Tangent)), vertex.Tangent.w);
                }
                part.Transform = glm::mat4(1);
            }
            if (!settings.MergeMeshes && (settings.OptimizeIndices || settings.WeldVertices))
            {
                cook::MeshOptimizationReport report;
                if (!cook::OptimizeMesh(*part.Mesh, settings.WeldVertices, &report, transformError))
                    return false;
                cacheAcmrBefore += report.VertexCacheAcmrBefore;
                cacheAcmrAfter += report.VertexCacheAcmrAfter;
                ++optimizedParts;
            }
            if (!settings.MergeMeshes && settings.GenerateLods && settings.LodCount > 1)
            {
                std::vector<cook::MeshLod> lods;
                if (!cook::GenerateMeshLods(*part.Mesh, settings.LodCount, settings.LodTriangleRatio,
                                            settings.LodTargetError, settings.LodAggressive, lods, transformError))
                    return false;
                if (lods.size() > 1)
                    part.Lods.assign(std::make_move_iterator(lods.begin() + 1),
                                     std::make_move_iterator(lods.end()));
                if (part.Lods.size() + 1u < settings.LodCount)
                    output.Diagnostics.push_back(Diagnostic(
                        AssetDiagnosticSeverity::Info, "mesh_lod_topology_limit",
                        "The simplifier stopped before the requested LOD count to preserve topology/error limits."));
            }
            for (const Vertex &vertex : part.Mesh->Vertices)
            {
                const glm::vec3 position = glm::vec3(part.Transform * glm::vec4(vertex.Position, 1));
                if (first)
                {
                    data.BoundsMinimum = data.BoundsMaximum = position;
                    first = false;
                }
                else
                {
                    data.BoundsMinimum = glm::min(data.BoundsMinimum, position);
                    data.BoundsMaximum = glm::max(data.BoundsMaximum, position);
                }
            }
            data.Parts.push_back(std::move(part));
            partMaterials.push_back(instance.Mat);
            ++partIndex;
        }
        if (data.Parts.empty())
        {
            if (transformError)
                *transformError = "Model contains no triangle primitives";
            return false;
        }
        const uint64_t sourcePartCount = data.Parts.size();
        if (settings.MergeMeshes)
        {
            std::vector<std::vector<size_t>> groups;
            std::unordered_map<uint64_t, std::vector<size_t>> groupBuckets;
            for (size_t index = 0; index < data.Parts.size(); ++index)
            {
                const uint64_t materialHash = MaterialRenderStateHash(partMaterials[index]);
                size_t groupIndex = groups.size();
                if (const auto found = groupBuckets.find(materialHash);
                    found != groupBuckets.end())
                {
                    for (size_t candidate : found->second)
                    {
                        if (MaterialRenderStatesEqual(
                                partMaterials[groups[candidate].front()], partMaterials[index]))
                        {
                            groupIndex = candidate;
                            break;
                        }
                    }
                }
                if (groupIndex == groups.size())
                {
                    groups.push_back({});
                    groupBuckets[materialHash].push_back(groupIndex);
                }
                groups[groupIndex].push_back(index);
            }

            std::vector<StaticMeshPart> mergedParts;
            mergedParts.reserve(groups.size());
            for (const std::vector<size_t> &group : groups)
            {
                StaticMeshPart merged;
                if (group.size() == 1)
                    merged = std::move(data.Parts[group.front()]);
                else
                {
                    merged.Material = data.Parts[group.front()].Material;
                    merged.MaterialSlot = data.Parts[group.front()].MaterialSlot;
                    std::vector<MeshCombineSource> sources;
                    sources.reserve(group.size());
                    for (size_t index : group)
                        sources.push_back({data.Parts[index].Mesh.get(),
                                           data.Parts[index].Transform});
                    MeshData combined;
                    if (!CombineMeshes(sources, combined, nullptr, transformError))
                        return false;
                    merged.Mesh = std::make_shared<MeshData>(std::move(combined));
                    merged.Transform = glm::mat4(1.0f);
                    merged.Lods.clear();
                }
                mergedParts.push_back(std::move(merged));
            }
            data.Parts = std::move(mergedParts);

            for (StaticMeshPart &part : data.Parts)
            {
                if (settings.OptimizeIndices || settings.WeldVertices)
                {
                    cook::MeshOptimizationReport report;
                    if (!cook::OptimizeMesh(*part.Mesh, settings.WeldVertices,
                                            &report, transformError))
                        return false;
                    cacheAcmrBefore += report.VertexCacheAcmrBefore;
                    cacheAcmrAfter += report.VertexCacheAcmrAfter;
                    ++optimizedParts;
                }
                if (settings.GenerateLods && settings.LodCount > 1)
                {
                    std::vector<cook::MeshLod> lods;
                    if (!cook::GenerateMeshLods(*part.Mesh, settings.LodCount,
                                                settings.LodTriangleRatio,
                                                settings.LodTargetError,
                                                settings.LodAggressive, lods,
                                                transformError))
                        return false;
                    if (lods.size() > 1)
                        part.Lods.assign(std::make_move_iterator(lods.begin() + 1),
                                         std::make_move_iterator(lods.end()));
                }
            }

            first = true;
            for (const StaticMeshPart &part : data.Parts)
                for (const Vertex &vertex : part.Mesh->Vertices)
                {
                    const glm::vec3 position = glm::vec3(
                        part.Transform * glm::vec4(vertex.Position, 1.0f));
                    if (first)
                    {
                        data.BoundsMinimum = data.BoundsMaximum = position;
                        first = false;
                    }
                    else
                    {
                        data.BoundsMinimum = glm::min(data.BoundsMinimum, position);
                        data.BoundsMaximum = glm::max(data.BoundsMaximum, position);
                    }
                }
        }
        if (settings.Collision != cook::CollisionType::None)
        {
            std::vector<cook::CollisionSourceMesh> sources;
            sources.reserve(data.Parts.size());
            for (const StaticMeshPart &part : data.Parts)
                sources.push_back({part.Mesh.get(), part.Transform});
            cook::CollisionCookSettings collision;
            collision.Type = settings.Collision;
            collision.TriangleRatio = settings.CollisionTriangleRatio;
            collision.SimplificationError = settings.CollisionSimplificationError;
            collision.MaxConvexHulls = settings.CollisionMaxConvexHulls;
            collision.MaxVerticesPerHull = settings.CollisionMaxVerticesPerHull;
            collision.VoxelResolution = settings.CollisionVoxelResolution;
            collision.ConvexErrorPercent = settings.CollisionConvexErrorPercent;
            if (!cook::CookCollision(sources, collision, data.Collision, transformError))
                return false;
        }
        output.RuntimeType = std::string(kStaticMeshResourceType);
        output.ResourceVersion = 2;
        output.Compression = "rle";
        output.Payload = EncodeMesh(data);
        output.AssetDependencies = descriptor.AssetDependencies;
        output.Statistics["parts"] = data.Parts.size();
        output.Statistics["source_parts"] = sourcePartCount;
        output.Statistics["merged_parts"] = sourcePartCount - data.Parts.size();
        uint64_t vertices = 0, triangles = 0;
        uint64_t lodVertices = 0, lodTriangles = 0;
        uint64_t maximumLodLevels = 1;
        for (const auto &part : data.Parts)
        {
            vertices += part.Mesh->Vertices.size();
            triangles += part.Mesh->Indices.size() / 3;
            for (const cook::MeshLod &lod : part.Lods)
            {
                lodVertices += lod.Mesh.Vertices.size();
                lodTriangles += lod.Mesh.Indices.size() / 3u;
            }
            maximumLodLevels = std::max<uint64_t>(maximumLodLevels, part.Lods.size() + 1u);
        }
        output.Statistics["vertices"] = vertices;
        output.Statistics["triangles"] = triangles;
        output.Statistics["lod_levels"] = maximumLodLevels;
        output.Statistics["lod_vertices"] = lodVertices;
        output.Statistics["lod_triangles"] = lodTriangles;
        uint64_t collisionVertices = 0, collisionTriangles = 0;
        for (const cook::CollisionMesh &collisionMesh : data.Collision.Meshes)
        {
            collisionVertices += collisionMesh.Vertices.size();
            collisionTriangles += collisionMesh.Indices.size() / 3u;
        }
        output.Statistics["collision_meshes"] = data.Collision.Meshes.size();
        output.Statistics["collision_vertices"] = collisionVertices;
        output.Statistics["collision_triangles"] = collisionTriangles;
        if (optimizedParts != 0)
        {
            output.Statistics["meshopt_acmr_before_x1000"] =
                static_cast<uint64_t>(std::round(cacheAcmrBefore / optimizedParts * 1000.0));
            output.Statistics["meshopt_acmr_after_x1000"] =
                static_cast<uint64_t>(std::round(cacheAcmrAfter / optimizedParts * 1000.0));
        }
        output.Statistics["payload_bytes"] = output.Payload.size();
        return true;
    };
    mesh.Preview = [](const AssetDescriptor &descriptor, const AssetPreviewRequest &, AssetPreview &preview,
                      std::string *) {
        preview.Kind = "mesh";
        preview.MimeType = "application/x-sla-mesh-preview-request";
        preview.Metadata["name"] = descriptor.Name;
        preview.Metadata["renderer_required"] = "true";
        preview.CacheKey = descriptor.LastTransformFingerprint;
        return true;
    };
    mesh.Inspector = [](const AssetDescriptor &) { return MeshProperties(); };
    mesh.ProfileDefaults = [](std::string_view profile) {
        std::map<std::string, std::string> values;
        if (profile == "mobile")
        {
            values["generate_lods"] = "true";
            values["lod_count"] = "3";
            values["lod_ratio"] = "0.4";
        }
        return values;
    };
    if (!registry.Register(std::move(mesh), error) ||
        !resourceManager.RegisterLoader<StaticMeshData>(std::string(kStaticMeshResourceType), LoadMesh, error))
        return false;
    return true;
}

} // namespace engine::assets
