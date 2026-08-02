#include "rendering/renderer_c.h"

#include "engine/renderer/Renderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

struct re_renderer
{
    rendering::RendererPtr Instance;
};

namespace {

thread_local std::string g_lastError;

void ClearError()
{
    g_lastError.clear();
}

void CaptureError()
{
    try
    {
        throw;
    }
    catch (const std::exception& exception)
    {
        g_lastError = exception.what();
    }
    catch (...)
    {
        g_lastError = "Unknown native renderer error";
    }
}

bool Valid(const re_renderer* renderer)
{
    if (renderer && renderer->Instance)
        return true;
    g_lastError = "Renderer handle is null";
    return false;
}

rendering::MaterialDesc Material(const re_material_desc& source)
{
    rendering::MaterialDesc result;
    std::copy_n(source.albedo, 3, result.Albedo);
    result.Alpha = source.alpha;
    result.Metallic = source.metallic;
    result.Roughness = source.roughness;
    std::copy_n(source.emissive, 3, result.Emissive);
    result.AmbientOcclusion = source.ambient_occlusion;
    result.SpecularF0 = source.specular_f0;
    return result;
}

rendering::CameraDesc Camera(const re_camera_desc& source)
{
    rendering::CameraDesc result;
    std::copy_n(source.position, 3, result.Position);
    result.YawDegrees = source.yaw_degrees;
    result.PitchDegrees = source.pitch_degrees;
    result.VerticalFovDegrees = source.vertical_fov_degrees;
    result.NearPlane = source.near_plane;
    result.FarPlane = source.far_plane;
    return result;
}

rendering::DirectionalLightDesc Sun(const re_directional_light_desc& source)
{
    rendering::DirectionalLightDesc result;
    std::copy_n(source.direction, 3, result.Direction);
    std::copy_n(source.color, 3, result.Color);
    result.Intensity = source.intensity;
    result.CastsShadows = source.casts_shadows != 0;
    return result;
}

rendering::PointLightDesc PointLight(const re_point_light_desc& source)
{
    rendering::PointLightDesc result;
    std::copy_n(source.position, 3, result.Position);
    std::copy_n(source.color, 3, result.Color);
    result.Intensity = source.intensity;
    result.Radius = source.radius;
    result.CastsShadows = source.casts_shadows != 0;
    return result;
}

std::array<float, 16> Transform(const float* values)
{
    if (!values)
        return rendering::IdentityTransform();
    std::array<float, 16> result;
    std::copy_n(values, 16, result.begin());
    return result;
}

std::filesystem::path Utf8Path(const char* value)
{
    const auto* first = reinterpret_cast<const char8_t*>(value);
    return std::filesystem::path(
        std::u8string(first, first + std::strlen(value)));
}

} // namespace

extern "C" {

uint32_t re_get_api_version(void) { return RE_API_VERSION; }
const char* re_get_last_error(void) { return g_lastError.c_str(); }

void re_renderer_desc_init(re_renderer_desc* desc)
{
    if (!desc) return;
    *desc = {};
    desc->struct_size = sizeof(*desc);
    desc->backend = RE_BACKEND_OPENGL;
    desc->window_title = "Rendering Engine C API";
    desc->width = 1280;
    desc->height = 720;
    desc->resizable = 1;
    desc->visible = 1;
    desc->vsync = 1;
    desc->msaa_samples = 4;
}

void re_material_desc_init(re_material_desc* desc)
{
    if (!desc) return;
    *desc = {};
    desc->albedo[0] = desc->albedo[1] = desc->albedo[2] = 0.8f;
    desc->alpha = 1.0f;
    desc->roughness = 0.5f;
    desc->ambient_occlusion = 1.0f;
    desc->specular_f0 = 0.04f;
}

void re_camera_desc_init(re_camera_desc* desc)
{
    if (!desc) return;
    *desc = {};
    desc->position[1] = 1.8f;
    desc->position[2] = 6.0f;
    desc->yaw_degrees = -90.0f;
    desc->pitch_degrees = -10.0f;
    desc->vertical_fov_degrees = 60.0f;
    desc->near_plane = 0.05f;
    desc->far_plane = 500.0f;
}

void re_directional_light_desc_init(re_directional_light_desc* desc)
{
    if (!desc) return;
    *desc = {};
    desc->direction[0] = -0.4f;
    desc->direction[1] = -0.85f;
    desc->direction[2] = -0.35f;
    desc->color[0] = 1.0f;
    desc->color[1] = 0.96f;
    desc->color[2] = 0.88f;
    desc->intensity = 3.0f;
    desc->casts_shadows = 1;
}

void re_point_light_desc_init(re_point_light_desc* desc)
{
    if (!desc) return;
    *desc = {};
    desc->color[0] = desc->color[1] = desc->color[2] = 1.0f;
    desc->intensity = 20.0f;
    desc->radius = 15.0f;
}

re_renderer* re_renderer_create(const re_renderer_desc* desc)
{
    ClearError();
    try
    {
        if (!desc || desc->struct_size < sizeof(re_renderer_desc))
            throw std::invalid_argument("Invalid or incompatible re_renderer_desc");
        if (desc->backend != RE_BACKEND_OPENGL && desc->backend != RE_BACKEND_VULKAN)
            throw std::invalid_argument("Unknown renderer backend value");
        rendering::RendererDesc native;
        native.GraphicsBackend = desc->backend == RE_BACKEND_VULKAN
            ? rendering::Backend::Vulkan : rendering::Backend::OpenGL;
        if (desc->window_title) native.WindowTitle = desc->window_title;
        native.Width = desc->width;
        native.Height = desc->height;
        native.Resizable = desc->resizable != 0;
        native.Visible = desc->visible != 0;
        native.VSync = desc->vsync != 0;
        native.Validation = desc->validation != 0;
        native.MsaaSamples = desc->msaa_samples;
        if (desc->shader_directory)
            native.ShaderDirectory = Utf8Path(desc->shader_directory);
        if (desc->pipeline_cache_directory)
            native.PipelineCacheDirectory =
                Utf8Path(desc->pipeline_cache_directory);
        auto result = std::make_unique<re_renderer>();
        result->Instance = rendering::Renderer::Create(native);
        return result.release();
    }
    catch (...)
    {
        CaptureError();
        return nullptr;
    }
}

void re_renderer_destroy(re_renderer* renderer)
{
    delete renderer;
}

int32_t re_renderer_pump_events(re_renderer* renderer)
{
    ClearError();
    try { return Valid(renderer) && renderer->Instance->PumpEvents() ? 1 : 0; }
    catch (...) { CaptureError(); return 0; }
}

int32_t re_renderer_tick(re_renderer* renderer, float delta_seconds)
{
    ClearError();
    try { return Valid(renderer) && renderer->Instance->Tick(delta_seconds) ? 1 : 0; }
    catch (...) { CaptureError(); return 0; }
}

int32_t re_renderer_render_frame(re_renderer* renderer, float delta_seconds)
{
    ClearError();
    try
    {
        if (!Valid(renderer)) return 0;
        renderer->Instance->RenderFrame(delta_seconds);
        return 1;
    }
    catch (...) { CaptureError(); return 0; }
}

void re_renderer_request_close(re_renderer* renderer)
{
    if (Valid(renderer)) renderer->Instance->RequestClose();
}

int32_t re_renderer_should_close(const re_renderer* renderer)
{
    return Valid(renderer) && renderer->Instance->ShouldClose() ? 1 : 0;
}

int32_t re_renderer_resize(re_renderer* renderer, uint32_t width, uint32_t height)
{
    ClearError();
    try
    {
        if (!Valid(renderer)) return 0;
        renderer->Instance->Resize(width, height);
        return 1;
    }
    catch (...) { CaptureError(); return 0; }
}

const char* re_renderer_backend_name(const re_renderer* renderer)
{
    return Valid(renderer) ? renderer->Instance->BackendName().data() : "";
}

int32_t re_renderer_get_frame_stats(const re_renderer* renderer, re_frame_stats* stats)
{
    if (!Valid(renderer) || !stats) return 0;
    const rendering::FrameStats native = renderer->Instance->GetFrameStats();
    stats->gpu_frame_milliseconds = native.GpuFrameMilliseconds;
    stats->draw_batch_count = native.DrawBatchCount;
    stats->instance_count = native.InstanceCount;
    stats->draw_calls_saved = native.DrawCallsSaved;
    return 1;
}

re_mesh re_renderer_create_mesh(re_renderer* renderer, const re_vertex* vertices,
    size_t vertex_count, const uint32_t* indices, size_t index_count)
{
    ClearError();
    try
    {
        if (!Valid(renderer) || !vertices || !indices) return 0;
        static_assert(sizeof(re_vertex) == sizeof(rendering::Vertex));
        return renderer->Instance->CreateMesh(
            std::span(reinterpret_cast<const rendering::Vertex*>(vertices), vertex_count),
            std::span(indices, index_count));
    }
    catch (...) { CaptureError(); return 0; }
}

re_mesh re_renderer_create_cube(re_renderer* renderer, float half_extent)
{
    ClearError();
    try { return Valid(renderer) ? renderer->Instance->CreateCube(half_extent) : 0; }
    catch (...) { CaptureError(); return 0; }
}

re_mesh re_renderer_create_sphere(re_renderer* renderer, float radius,
    uint32_t stacks, uint32_t slices)
{
    ClearError();
    try { return Valid(renderer) ? renderer->Instance->CreateSphere(radius, stacks, slices) : 0; }
    catch (...) { CaptureError(); return 0; }
}

void re_renderer_destroy_mesh(re_renderer* renderer, re_mesh mesh)
{
    if (Valid(renderer)) renderer->Instance->DestroyMesh(mesh);
}

re_material re_renderer_create_material(re_renderer* renderer,
    const re_material_desc* desc)
{
    ClearError();
    try { return Valid(renderer) && desc ? renderer->Instance->CreateMaterial(Material(*desc)) : 0; }
    catch (...) { CaptureError(); return 0; }
}

int32_t re_renderer_update_material(re_renderer* renderer, re_material material,
    const re_material_desc* desc)
{
    ClearError();
    try { return Valid(renderer) && desc && renderer->Instance->UpdateMaterial(material, Material(*desc)); }
    catch (...) { CaptureError(); return 0; }
}

void re_renderer_destroy_material(re_renderer* renderer, re_material material)
{
    if (Valid(renderer)) renderer->Instance->DestroyMaterial(material);
}

re_object re_renderer_add_object(re_renderer* renderer, re_mesh mesh,
    re_material material, const float* transform)
{
    ClearError();
    try { return Valid(renderer) ? renderer->Instance->AddObject(mesh, material, Transform(transform)) : 0; }
    catch (...) { CaptureError(); return 0; }
}

int32_t re_renderer_set_object_transform(re_renderer* renderer, re_object object,
    const float* transform)
{
    ClearError();
    try { return Valid(renderer) && renderer->Instance->SetObjectTransform(object, Transform(transform)); }
    catch (...) { CaptureError(); return 0; }
}

int32_t re_renderer_remove_object(re_renderer* renderer, re_object object)
{
    return Valid(renderer) && renderer->Instance->RemoveObject(object);
}

void re_renderer_clear_objects(re_renderer* renderer)
{
    if (Valid(renderer)) renderer->Instance->ClearObjects();
}

re_point_light re_renderer_add_point_light(re_renderer* renderer,
    const re_point_light_desc* desc)
{
    ClearError();
    try { return Valid(renderer) && desc ? renderer->Instance->AddPointLight(PointLight(*desc)) : 0; }
    catch (...) { CaptureError(); return 0; }
}

int32_t re_renderer_set_point_light(re_renderer* renderer, re_point_light light,
    const re_point_light_desc* desc)
{
    return Valid(renderer) && desc && renderer->Instance->SetPointLight(light, PointLight(*desc));
}

int32_t re_renderer_remove_point_light(re_renderer* renderer, re_point_light light)
{
    return Valid(renderer) && renderer->Instance->RemovePointLight(light);
}

void re_renderer_clear_point_lights(re_renderer* renderer)
{
    if (Valid(renderer)) renderer->Instance->ClearPointLights();
}

int32_t re_renderer_set_camera(re_renderer* renderer, const re_camera_desc* desc)
{
    if (!Valid(renderer) || !desc) return 0;
    renderer->Instance->SetCamera(Camera(*desc));
    return 1;
}

int32_t re_renderer_set_sun(re_renderer* renderer, const re_directional_light_desc* desc)
{
    if (!Valid(renderer) || !desc) return 0;
    renderer->Instance->SetSun(Sun(*desc));
    return 1;
}

int32_t re_renderer_set_exposure(re_renderer* renderer, float exposure)
{
    ClearError();
    try
    {
        if (!Valid(renderer)) return 0;
        renderer->Instance->SetExposure(exposure);
        return 1;
    }
    catch (...) { CaptureError(); return 0; }
}

int32_t re_renderer_set_background_color(re_renderer* renderer,
    const float* zenith, const float* horizon)
{
    ClearError();
    try
    {
        if (!Valid(renderer) || !zenith || !horizon) return 0;
        renderer->Instance->SetBackgroundColor(
            {zenith[0], zenith[1], zenith[2]},
            {horizon[0], horizon[1], horizon[2]});
        return 1;
    }
    catch (...) { CaptureError(); return 0; }
}

int32_t re_renderer_request_screenshot(re_renderer* renderer, const char* path)
{
    ClearError();
    try
    {
        if (!Valid(renderer) || !path) return 0;
        renderer->Instance->RequestScreenshot(Utf8Path(path));
        return 1;
    }
    catch (...) { CaptureError(); return 0; }
}

void re_identity_transform(float* transform)
{
    if (!transform) return;
    const auto result = rendering::IdentityTransform();
    std::copy(result.begin(), result.end(), transform);
}

void re_compose_transform(const float* translation, const float* rotation,
    const float* scale, float* transform)
{
    if (!translation || !rotation || !scale || !transform) return;
    const std::array<float, 3> t{translation[0], translation[1], translation[2]};
    const std::array<float, 3> r{rotation[0], rotation[1], rotation[2]};
    const std::array<float, 3> s{scale[0], scale[1], scale[2]};
    const auto result = rendering::ComposeTransform(t, r, s);
    std::copy(result.begin(), result.end(), transform);
}

} // extern "C"
