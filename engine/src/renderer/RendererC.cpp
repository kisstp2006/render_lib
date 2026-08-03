#include "rendering/renderer_c.h"

#include "engine/renderer/GraphicsDevice.h"
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
#include <vector>

struct re_renderer {
  rendering::RendererPtr Instance;
};

namespace {

thread_local std::string g_lastError;

void ClearError() { g_lastError.clear(); }

void CaptureError() {
  try {
    throw;
  } catch (const std::exception &exception) {
    g_lastError = exception.what();
  } catch (...) {
    g_lastError = "Unknown native renderer error";
  }
}

bool Valid(const re_renderer *renderer) {
  if (renderer && renderer->Instance)
    return true;
  g_lastError = "Renderer handle is null";
  return false;
}

rendering::MaterialDesc Material(const re_material_desc &source) {
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

rendering::CameraDesc Camera(const re_camera_desc &source) {
  rendering::CameraDesc result;
  std::copy_n(source.position, 3, result.Position);
  result.YawDegrees = source.yaw_degrees;
  result.PitchDegrees = source.pitch_degrees;
  result.VerticalFovDegrees = source.vertical_fov_degrees;
  result.NearPlane = source.near_plane;
  result.FarPlane = source.far_plane;
  return result;
}

rendering::DirectionalLightDesc Sun(const re_directional_light_desc &source) {
  rendering::DirectionalLightDesc result;
  std::copy_n(source.direction, 3, result.Direction);
  std::copy_n(source.color, 3, result.Color);
  result.Intensity = source.intensity;
  result.CastsShadows = source.casts_shadows != 0;
  return result;
}

rendering::PointLightDesc PointLight(const re_point_light_desc &source) {
  rendering::PointLightDesc result;
  std::copy_n(source.position, 3, result.Position);
  std::copy_n(source.color, 3, result.Color);
  result.Intensity = source.intensity;
  result.Radius = source.radius;
  result.CastsShadows = source.casts_shadows != 0;
  return result;
}

std::array<float, 16> Transform(const float *values) {
  if (!values)
    return rendering::IdentityTransform();
  std::array<float, 16> result;
  std::copy_n(values, 16, result.begin());
  return result;
}

std::filesystem::path Utf8Path(const char *value) {
  const auto *first = reinterpret_cast<const char8_t *>(value);
  return std::filesystem::path(
      std::u8string(first, first + std::strlen(value)));
}

rendering::GraphicsDevice &Device(re_renderer *renderer) {
  if (!Valid(renderer))
    throw std::invalid_argument("Renderer handle is null");
  return renderer->Instance->GetGraphicsDevice();
}

template <size_t N>
void FixedString(char (&destination)[N], std::string_view source) {
  const size_t count = std::min(source.size(), N - 1);
  std::memcpy(destination, source.data(), count);
  destination[count] = '\0';
}

std::span<const std::byte> ByteSpan(const void *data, size_t size) {
  if (size == 0)
    return {};
  if (!data)
    throw std::invalid_argument(
        "Data pointer is null for a non-empty byte span");
  return {static_cast<const std::byte *>(data), size};
}

rendering::ShaderModuleDesc ShaderDesc(const re_shader_module_desc &source) {
  if (source.struct_size < sizeof(source))
    throw std::invalid_argument("Invalid re_shader_module_desc");
  rendering::ShaderModuleDesc result;
  result.Stage = static_cast<rendering::ShaderStage>(source.stage);
  result.Language = static_cast<rendering::ShaderLanguage>(source.language);
  if (source.source_path)
    result.SourcePath = Utf8Path(source.source_path);
  if (source.source_data && source.source_size)
    result.Source.assign(static_cast<const char *>(source.source_data),
                         source.source_size);
  result.EntryPoint = source.entry_point ? source.entry_point : "main";
  result.Optimize = source.optimize != 0;
  result.GenerateDebugInfo = source.generate_debug_info != 0;
  result.EnableHotReload = source.enable_hot_reload != 0;
  if (source.debug_name)
    result.DebugName = source.debug_name;
  for (size_t i = 0; i < source.define_count; ++i) {
    if (!source.defines || !source.defines[i].name)
      continue;
    result.Defines.push_back(
        {source.defines[i].name,
         source.defines[i].value ? source.defines[i].value : "1"});
  }
  return result;
}

rendering::GraphicsPipelineDesc
GraphicsPipelineDesc(const re_graphics_pipeline_desc &s) {
  if (s.struct_size < sizeof(s))
    throw std::invalid_argument("Invalid re_graphics_pipeline_desc");
  if (s.vertex_binding_count && !s.vertex_bindings)
    throw std::invalid_argument(
        "Graphics pipeline vertex binding array is null");
  if (s.vertex_attribute_count && !s.vertex_attributes)
    throw std::invalid_argument(
        "Graphics pipeline vertex attribute array is null");
  if (s.color_format_count && !s.color_formats)
    throw std::invalid_argument("Graphics pipeline color format array is null");
  rendering::GraphicsPipelineDesc d;
  d.VertexShader = s.vertex_shader;
  d.FragmentShader = s.fragment_shader;
  d.Topology = static_cast<rendering::PrimitiveTopology>(s.topology);
  d.Cull = static_cast<rendering::CullMode>(s.cull);
  d.Winding = static_cast<rendering::FrontFace>(s.winding);
  d.DepthTest = s.depth_test != 0;
  d.DepthWrite = s.depth_write != 0;
  d.DepthCompare = static_cast<rendering::CompareOperation>(s.depth_compare);
  d.Blend.Enabled = s.blend_enabled != 0;
  for (size_t i = 0; i < s.vertex_binding_count; ++i) {
    const auto &b = s.vertex_bindings[i];
    d.VertexBindings.push_back(
        {b.binding, b.stride,
         static_cast<rendering::VertexInputRate>(b.input_rate)});
  }
  for (size_t i = 0; i < s.vertex_attribute_count; ++i) {
    const auto &a = s.vertex_attributes[i];
    d.VertexAttributes.push_back(
        {a.location, a.binding, static_cast<rendering::VertexFormat>(a.format),
         a.offset});
  }
  for (size_t i = 0; i < s.color_format_count; ++i)
    d.ColorFormats.push_back(
        static_cast<rendering::TextureFormat>(s.color_formats[i]));
  d.HasDepthFormat = s.has_depth_format != 0;
  d.DepthFormat = static_cast<rendering::TextureFormat>(s.depth_format);
  if (s.debug_name)
    d.DebugName = s.debug_name;
  return d;
}

rendering::BufferDesc BufferDesc(const re_buffer_desc &s) {
  if (s.struct_size < sizeof(s))
    throw std::invalid_argument("Invalid re_buffer_desc");
  rendering::BufferDesc d;
  d.Size = s.size;
  d.Usage = static_cast<rendering::BufferUsage>(s.usage);
  d.CpuWritable = s.cpu_writable != 0;
  if (s.debug_name)
    d.DebugName = s.debug_name;
  return d;
}
rendering::TextureDesc TextureDesc(const re_texture_desc &s) {
  if (s.struct_size < sizeof(s))
    throw std::invalid_argument("Invalid re_texture_desc");
  rendering::TextureDesc d;
  d.Width = s.width;
  d.Height = s.height;
  d.MipLevels = s.mip_levels;
  d.Format = static_cast<rendering::TextureFormat>(s.format);
  d.Usage = static_cast<rendering::TextureUsage>(s.usage);
  if (s.debug_name)
    d.DebugName = s.debug_name;
  return d;
}
rendering::SamplerDesc SamplerDesc(const re_sampler_desc &s) {
  if (s.struct_size < sizeof(s))
    throw std::invalid_argument("Invalid re_sampler_desc");
  rendering::SamplerDesc d;
  d.MinFilter = static_cast<rendering::Filter>(s.min_filter);
  d.MagFilter = static_cast<rendering::Filter>(s.mag_filter);
  d.AddressU = static_cast<rendering::AddressMode>(s.address_u);
  d.AddressV = static_cast<rendering::AddressMode>(s.address_v);
  d.AddressW = static_cast<rendering::AddressMode>(s.address_w);
  d.MaxAnisotropy = s.max_anisotropy;
  if (s.debug_name)
    d.DebugName = s.debug_name;
  return d;
}
rendering::RenderPassDesc RenderPassDesc(const re_render_pass_desc &s) {
  if (s.struct_size < sizeof(s))
    throw std::invalid_argument("Invalid re_render_pass_desc");
  rendering::RenderPassDesc d;
  d.Target = s.target;
  d.ColorLoad = static_cast<rendering::LoadAction>(s.color_load);
  d.ColorStore = static_cast<rendering::StoreAction>(s.color_store);
  std::copy_n(s.clear_color, 4, d.ClearColor.begin());
  d.DepthLoad = static_cast<rendering::LoadAction>(s.depth_load);
  d.DepthStore = static_cast<rendering::StoreAction>(s.depth_store);
  d.ClearDepth = s.clear_depth;
  if (s.debug_name)
    d.DebugName = s.debug_name;
  return d;
}

} // namespace

extern "C" {

uint32_t re_get_api_version(void) { return RE_API_VERSION; }
const char *re_get_last_error(void) { return g_lastError.c_str(); }

void re_renderer_desc_init(re_renderer_desc *desc) {
  if (!desc)
    return;
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

void re_material_desc_init(re_material_desc *desc) {
  if (!desc)
    return;
  *desc = {};
  desc->albedo[0] = desc->albedo[1] = desc->albedo[2] = 0.8f;
  desc->alpha = 1.0f;
  desc->roughness = 0.5f;
  desc->ambient_occlusion = 1.0f;
  desc->specular_f0 = 0.04f;
}

void re_camera_desc_init(re_camera_desc *desc) {
  if (!desc)
    return;
  *desc = {};
  desc->position[1] = 1.8f;
  desc->position[2] = 6.0f;
  desc->yaw_degrees = -90.0f;
  desc->pitch_degrees = -10.0f;
  desc->vertical_fov_degrees = 60.0f;
  desc->near_plane = 0.05f;
  desc->far_plane = 500.0f;
}

void re_directional_light_desc_init(re_directional_light_desc *desc) {
  if (!desc)
    return;
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

void re_point_light_desc_init(re_point_light_desc *desc) {
  if (!desc)
    return;
  *desc = {};
  desc->color[0] = desc->color[1] = desc->color[2] = 1.0f;
  desc->intensity = 20.0f;
  desc->radius = 15.0f;
}

void re_shader_module_desc_init(re_shader_module_desc *d) {
  if (!d)
    return;
  *d = {};
  d->struct_size = sizeof(*d);
  d->stage = RE_STAGE_VERTEX;
  d->language = RE_SHADER_HLSL;
  d->entry_point = "main";
  d->optimize = 1;
  d->enable_hot_reload = 1;
}
void re_graphics_pipeline_desc_init(re_graphics_pipeline_desc *d) {
  if (!d)
    return;
  *d = {};
  d->struct_size = sizeof(*d);
  d->topology = RE_TOPOLOGY_TRIANGLE_LIST;
  d->cull = RE_CULL_BACK;
  d->winding = RE_FRONT_COUNTER_CLOCKWISE;
  d->depth_compare = RE_COMPARE_LESS;
  d->depth_format = RE_FORMAT_DEPTH32_FLOAT;
}
void re_compute_pipeline_desc_init(re_compute_pipeline_desc *d) {
  if (!d)
    return;
  *d = {};
  d->struct_size = sizeof(*d);
}
void re_buffer_desc_init(re_buffer_desc *d) {
  if (!d)
    return;
  *d = {};
  d->struct_size = sizeof(*d);
  d->usage = RE_BUFFER_VERTEX;
}
void re_texture_desc_init(re_texture_desc *d) {
  if (!d)
    return;
  *d = {};
  d->struct_size = sizeof(*d);
  d->width = 1;
  d->height = 1;
  d->mip_levels = 1;
  d->format = RE_FORMAT_RGBA8_UNORM;
  d->usage = RE_TEXTURE_SAMPLED;
}
void re_sampler_desc_init(re_sampler_desc *d) {
  if (!d)
    return;
  *d = {};
  d->struct_size = sizeof(*d);
  d->min_filter = RE_FILTER_LINEAR;
  d->mag_filter = RE_FILTER_LINEAR;
  d->address_u = d->address_v = d->address_w = RE_ADDRESS_REPEAT;
  d->max_anisotropy = 1.0f;
}
void re_render_target_desc_init(re_render_target_desc *d) {
  if (!d)
    return;
  *d = {};
  d->struct_size = sizeof(*d);
}
void re_render_pass_desc_init(re_render_pass_desc *d) {
  if (!d)
    return;
  *d = {};
  d->struct_size = sizeof(*d);
  d->color_load = RE_LOAD;
  d->color_store = RE_STORE;
  d->clear_color[3] = 1.0f;
  d->depth_load = RE_CLEAR;
  d->depth_store = RE_STORE;
  d->clear_depth = 1.0f;
}

re_renderer *re_renderer_create(const re_renderer_desc *desc) {
  ClearError();
  try {
    if (!desc || desc->struct_size < sizeof(re_renderer_desc))
      throw std::invalid_argument("Invalid or incompatible re_renderer_desc");
    if (desc->backend != RE_BACKEND_OPENGL &&
        desc->backend != RE_BACKEND_VULKAN)
      throw std::invalid_argument("Unknown renderer backend value");
    rendering::RendererDesc native;
    native.GraphicsBackend = desc->backend == RE_BACKEND_VULKAN
                                 ? rendering::Backend::Vulkan
                                 : rendering::Backend::OpenGL;
    if (desc->window_title)
      native.WindowTitle = desc->window_title;
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
      native.PipelineCacheDirectory = Utf8Path(desc->pipeline_cache_directory);
    rendering::ExternalWindowDesc externalWindow;
    if (desc->external_window) {
      const re_external_window_desc &external = *desc->external_window;
      externalWindow.UserData = external.user_data;
      externalWindow.ShouldClose = external.should_close;
      externalWindow.RequestClose = external.request_close;
      externalWindow.PollEvents = external.poll_events;
      externalWindow.MakeContextCurrent = external.make_context_current;
      externalWindow.GetGlProcAddress = external.get_gl_proc_address;
      externalWindow.SwapBuffers = external.swap_buffers;
      externalWindow.SetSwapInterval = external.set_swap_interval;
      externalWindow.GetVulkanInstanceExtensions =
          external.get_vulkan_instance_extensions;
      externalWindow.CreateVulkanSurface = external.create_vulkan_surface;
      native.ExternalWindow = &externalWindow;
    }
    auto result = std::make_unique<re_renderer>();
    result->Instance = rendering::Renderer::Create(native);
    return result.release();
  } catch (...) {
    CaptureError();
    return nullptr;
  }
}

void re_renderer_destroy(re_renderer *renderer) { delete renderer; }

int32_t re_renderer_pump_events(re_renderer *renderer) {
  ClearError();
  try {
    return Valid(renderer) && renderer->Instance->PumpEvents() ? 1 : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

int32_t re_renderer_tick(re_renderer *renderer, float delta_seconds) {
  ClearError();
  try {
    return Valid(renderer) && renderer->Instance->Tick(delta_seconds) ? 1 : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

int32_t re_renderer_render_frame(re_renderer *renderer, float delta_seconds) {
  ClearError();
  try {
    if (!Valid(renderer))
      return 0;
    renderer->Instance->RenderFrame(delta_seconds);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

void re_renderer_request_close(re_renderer *renderer) {
  if (Valid(renderer))
    renderer->Instance->RequestClose();
}

int32_t re_renderer_should_close(const re_renderer *renderer) {
  return Valid(renderer) && renderer->Instance->ShouldClose() ? 1 : 0;
}

int32_t re_renderer_resize(re_renderer *renderer, uint32_t width,
                           uint32_t height) {
  ClearError();
  try {
    if (!Valid(renderer))
      return 0;
    renderer->Instance->Resize(width, height);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

const char *re_renderer_backend_name(const re_renderer *renderer) {
  return Valid(renderer) ? renderer->Instance->BackendName().data() : "";
}

int32_t re_renderer_get_frame_stats(const re_renderer *renderer,
                                    re_frame_stats *stats) {
  if (!Valid(renderer) || !stats)
    return 0;
  const rendering::FrameStats native = renderer->Instance->GetFrameStats();
  stats->gpu_frame_milliseconds = native.GpuFrameMilliseconds;
  stats->draw_batch_count = native.DrawBatchCount;
  stats->instance_count = native.InstanceCount;
  stats->draw_calls_saved = native.DrawCallsSaved;
  return 1;
}

re_mesh re_renderer_create_mesh(re_renderer *renderer,
                                const re_vertex *vertices, size_t vertex_count,
                                const uint32_t *indices, size_t index_count) {
  ClearError();
  try {
    if (!Valid(renderer) || !vertices || !indices)
      return 0;
    static_assert(sizeof(re_vertex) == sizeof(rendering::Vertex));
    return renderer->Instance->CreateMesh(
        std::span(reinterpret_cast<const rendering::Vertex *>(vertices),
                  vertex_count),
        std::span(indices, index_count));
  } catch (...) {
    CaptureError();
    return 0;
  }
}

re_mesh re_renderer_create_cube(re_renderer *renderer, float half_extent) {
  ClearError();
  try {
    return Valid(renderer) ? renderer->Instance->CreateCube(half_extent) : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

re_mesh re_renderer_create_sphere(re_renderer *renderer, float radius,
                                  uint32_t stacks, uint32_t slices) {
  ClearError();
  try {
    return Valid(renderer)
               ? renderer->Instance->CreateSphere(radius, stacks, slices)
               : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

void re_renderer_destroy_mesh(re_renderer *renderer, re_mesh mesh) {
  if (Valid(renderer))
    renderer->Instance->DestroyMesh(mesh);
}

re_material re_renderer_create_material(re_renderer *renderer,
                                        const re_material_desc *desc) {
  ClearError();
  try {
    return Valid(renderer) && desc
               ? renderer->Instance->CreateMaterial(Material(*desc))
               : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

int32_t re_renderer_update_material(re_renderer *renderer, re_material material,
                                    const re_material_desc *desc) {
  ClearError();
  try {
    return Valid(renderer) && desc &&
           renderer->Instance->UpdateMaterial(material, Material(*desc));
  } catch (...) {
    CaptureError();
    return 0;
  }
}

void re_renderer_destroy_material(re_renderer *renderer, re_material material) {
  if (Valid(renderer))
    renderer->Instance->DestroyMaterial(material);
}

re_object re_renderer_add_object(re_renderer *renderer, re_mesh mesh,
                                 re_material material, const float *transform) {
  ClearError();
  try {
    return Valid(renderer) ? renderer->Instance->AddObject(mesh, material,
                                                           Transform(transform))
                           : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

int32_t re_renderer_set_object_transform(re_renderer *renderer,
                                         re_object object,
                                         const float *transform) {
  ClearError();
  try {
    return Valid(renderer) &&
           renderer->Instance->SetObjectTransform(object, Transform(transform));
  } catch (...) {
    CaptureError();
    return 0;
  }
}

int32_t re_renderer_remove_object(re_renderer *renderer, re_object object) {
  return Valid(renderer) && renderer->Instance->RemoveObject(object);
}

void re_renderer_clear_objects(re_renderer *renderer) {
  if (Valid(renderer))
    renderer->Instance->ClearObjects();
}

re_point_light re_renderer_add_point_light(re_renderer *renderer,
                                           const re_point_light_desc *desc) {
  ClearError();
  try {
    return Valid(renderer) && desc
               ? renderer->Instance->AddPointLight(PointLight(*desc))
               : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

int32_t re_renderer_set_point_light(re_renderer *renderer, re_point_light light,
                                    const re_point_light_desc *desc) {
  return Valid(renderer) && desc &&
         renderer->Instance->SetPointLight(light, PointLight(*desc));
}

int32_t re_renderer_remove_point_light(re_renderer *renderer,
                                       re_point_light light) {
  return Valid(renderer) && renderer->Instance->RemovePointLight(light);
}

void re_renderer_clear_point_lights(re_renderer *renderer) {
  if (Valid(renderer))
    renderer->Instance->ClearPointLights();
}

int32_t re_renderer_set_camera(re_renderer *renderer,
                               const re_camera_desc *desc) {
  if (!Valid(renderer) || !desc)
    return 0;
  renderer->Instance->SetCamera(Camera(*desc));
  return 1;
}

int32_t re_renderer_set_sun(re_renderer *renderer,
                            const re_directional_light_desc *desc) {
  if (!Valid(renderer) || !desc)
    return 0;
  renderer->Instance->SetSun(Sun(*desc));
  return 1;
}

int32_t re_renderer_set_exposure(re_renderer *renderer, float exposure) {
  ClearError();
  try {
    if (!Valid(renderer))
      return 0;
    renderer->Instance->SetExposure(exposure);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

int32_t re_renderer_set_background_color(re_renderer *renderer,
                                         const float *zenith,
                                         const float *horizon) {
  ClearError();
  try {
    if (!Valid(renderer) || !zenith || !horizon)
      return 0;
    renderer->Instance->SetBackgroundColor(
        {zenith[0], zenith[1], zenith[2]},
        {horizon[0], horizon[1], horizon[2]});
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

int32_t re_renderer_request_screenshot(re_renderer *renderer,
                                       const char *path) {
  ClearError();
  try {
    if (!Valid(renderer) || !path)
      return 0;
    renderer->Instance->RequestScreenshot(Utf8Path(path));
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

re_shader_module
re_renderer_create_shader_module(re_renderer *r,
                                 const re_shader_module_desc *d) {
  ClearError();
  try {
    return d ? Device(r).CreateShaderModule(ShaderDesc(*d)) : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
re_shader_module
re_renderer_create_shader_permutation(re_renderer *r, re_shader_module base,
                                      const re_shader_define *defs,
                                      size_t count) {
  ClearError();
  try {
    std::vector<rendering::ShaderDefine> values;
    for (size_t i = 0; i < count; ++i)
      if (defs && defs[i].name)
        values.push_back({defs[i].name, defs[i].value ? defs[i].value : "1"});
    return Device(r).CreateShaderPermutation(base, values);
  } catch (...) {
    CaptureError();
    return 0;
  }
}
void re_renderer_destroy_shader_module(re_renderer *r, re_shader_module h) {
  try {
    Device(r).DestroyShaderModule(h);
  } catch (...) {
    CaptureError();
  }
}
int32_t re_renderer_get_shader_reflection(re_renderer *r, re_shader_module h,
                                          re_shader_reflection_info *out) {
  ClearError();
  try {
    if (!out)
      return 0;
    const auto v = Device(r).GetShaderReflection(h);
    *out = {};
    out->stage = static_cast<re_shader_stage>(v.Stage);
    FixedString(out->entry_point, v.EntryPoint);
    FixedString(out->permutation_key, v.PermutationKey);
    out->generation = v.Generation;
    out->resource_count = static_cast<uint32_t>(v.Resources.size());
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_get_shader_resource(re_renderer *r, re_shader_module h,
                                        uint32_t index,
                                        re_shader_resource *out) {
  ClearError();
  try {
    if (!out)
      return 0;
    const auto v = Device(r).GetShaderReflection(h);
    if (index >= v.Resources.size())
      return 0;
    const auto &s = v.Resources[index];
    *out = {};
    FixedString(out->name, s.Name);
    out->type = static_cast<re_shader_resource_type>(s.Type);
    out->set = s.Set;
    out->binding = s.Binding;
    out->location = s.Location;
    out->array_count = s.ArrayCount;
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
re_graphics_pipeline
re_renderer_create_graphics_pipeline(re_renderer *r,
                                     const re_graphics_pipeline_desc *d) {
  ClearError();
  try {
    return d ? Device(r).CreateGraphicsPipeline(GraphicsPipelineDesc(*d)) : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
re_compute_pipeline
re_renderer_create_compute_pipeline(re_renderer *r,
                                    const re_compute_pipeline_desc *d) {
  ClearError();
  try {
    if (!d || d->struct_size < sizeof(*d))
      return 0;
    rendering::ComputePipelineDesc n{d->compute_shader,
                                     d->debug_name ? d->debug_name : ""};
    return Device(r).CreateComputePipeline(n);
  } catch (...) {
    CaptureError();
    return 0;
  }
}
void re_renderer_destroy_graphics_pipeline(re_renderer *r,
                                           re_graphics_pipeline h) {
  try {
    Device(r).DestroyGraphicsPipeline(h);
  } catch (...) {
    CaptureError();
  }
}
void re_renderer_destroy_compute_pipeline(re_renderer *r,
                                          re_compute_pipeline h) {
  try {
    Device(r).DestroyComputePipeline(h);
  } catch (...) {
    CaptureError();
  }
}
re_buffer re_renderer_create_buffer(re_renderer *r, const re_buffer_desc *d,
                                    const void *data, size_t size) {
  ClearError();
  try {
    return d ? Device(r).CreateBuffer(BufferDesc(*d), ByteSpan(data, size)) : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_update_buffer(re_renderer *r, re_buffer h, uint64_t offset,
                                  const void *data, size_t size) {
  ClearError();
  try {
    return Device(r).UpdateBuffer(h, offset, ByteSpan(data, size));
  } catch (...) {
    CaptureError();
    return 0;
  }
}
void re_renderer_destroy_buffer(re_renderer *r, re_buffer h) {
  try {
    Device(r).DestroyBuffer(h);
  } catch (...) {
    CaptureError();
  }
}
re_texture re_renderer_create_texture(re_renderer *r, const re_texture_desc *d,
                                      const void *data, size_t size) {
  ClearError();
  try {
    return d ? Device(r).CreateTexture(TextureDesc(*d), ByteSpan(data, size))
             : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_update_texture(re_renderer *r, re_texture h, uint32_t mip,
                                   const void *data, size_t size) {
  ClearError();
  try {
    return Device(r).UpdateTexture(h, mip, ByteSpan(data, size));
  } catch (...) {
    CaptureError();
    return 0;
  }
}
void re_renderer_destroy_texture(re_renderer *r, re_texture h) {
  try {
    Device(r).DestroyTexture(h);
  } catch (...) {
    CaptureError();
  }
}
re_sampler re_renderer_create_sampler(re_renderer *r,
                                      const re_sampler_desc *d) {
  ClearError();
  try {
    return d ? Device(r).CreateSampler(SamplerDesc(*d)) : 0;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
void re_renderer_destroy_sampler(re_renderer *r, re_sampler h) {
  try {
    Device(r).DestroySampler(h);
  } catch (...) {
    CaptureError();
  }
}
re_render_target
re_renderer_create_render_target(re_renderer *r,
                                 const re_render_target_desc *d) {
  ClearError();
  try {
    if (!d || d->struct_size < sizeof(*d))
      return 0;
    rendering::RenderTargetDesc n;
    n.DepthAttachment = d->depth_attachment;
    if (d->debug_name)
      n.DebugName = d->debug_name;
    if (d->color_attachment_count && !d->color_attachments)
      throw std::invalid_argument(
          "Render target color attachment array is null");
    if (d->color_attachment_count)
      n.ColorAttachments.assign(d->color_attachments,
                                d->color_attachments +
                                    d->color_attachment_count);
    return Device(r).CreateRenderTarget(n);
  } catch (...) {
    CaptureError();
    return 0;
  }
}
void re_renderer_destroy_render_target(re_renderer *r, re_render_target h) {
  try {
    Device(r).DestroyRenderTarget(h);
  } catch (...) {
    CaptureError();
  }
}
int32_t re_renderer_begin_render_pass(re_renderer *r,
                                      const re_render_pass_desc *d) {
  ClearError();
  try {
    if (!d)
      return 0;
    Device(r).BeginRenderPass(RenderPassDesc(*d));
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_end_render_pass(re_renderer *r) {
  ClearError();
  try {
    Device(r).EndRenderPass();
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_graphics_pipeline(re_renderer *r,
                                           re_graphics_pipeline h) {
  ClearError();
  try {
    Device(r).BindGraphicsPipeline(h);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_compute_pipeline(re_renderer *r,
                                          re_compute_pipeline h) {
  ClearError();
  try {
    Device(r).BindComputePipeline(h);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_vertex_buffer(re_renderer *r, uint32_t b, re_buffer h,
                                       uint64_t o) {
  ClearError();
  try {
    Device(r).BindVertexBuffer(b, h, o);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_index_buffer(re_renderer *r, re_buffer h,
                                      re_index_type t, uint64_t o) {
  ClearError();
  try {
    Device(r).BindIndexBuffer(h, static_cast<rendering::IndexType>(t), o);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_uniform_buffer(re_renderer *r, uint32_t s, uint32_t b,
                                        re_buffer h, uint64_t o, uint64_t z) {
  ClearError();
  try {
    Device(r).BindUniformBuffer(s, b, h, o, z);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_storage_buffer(re_renderer *r, uint32_t s, uint32_t b,
                                        re_buffer h, uint64_t o, uint64_t z) {
  ClearError();
  try {
    Device(r).BindStorageBuffer(s, b, h, o, z);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_texture(re_renderer *r, uint32_t s, uint32_t b,
                                 re_texture h, re_sampler p) {
  ClearError();
  try {
    Device(r).BindTexture(s, b, h, p);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_sampler(re_renderer *r, uint32_t s, uint32_t b,
                                 re_sampler p) {
  ClearError();
  try {
    Device(r).BindSampler(s, b, p);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_bind_storage_texture(re_renderer *r, uint32_t s, uint32_t b,
                                         re_texture h) {
  ClearError();
  try {
    Device(r).BindStorageTexture(s, b, h);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_draw(re_renderer *r, uint32_t v, uint32_t i, uint32_t f,
                         uint32_t n) {
  ClearError();
  try {
    Device(r).Draw(v, i, f, n);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_draw_indexed(re_renderer *r, uint32_t c, uint32_t i,
                                 uint32_t f, int32_t v, uint32_t n) {
  ClearError();
  try {
    Device(r).DrawIndexed(c, i, f, v, n);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
int32_t re_renderer_dispatch(re_renderer *r, uint32_t x, uint32_t y,
                             uint32_t z) {
  ClearError();
  try {
    Device(r).Dispatch(x, y, z);
    return 1;
  } catch (...) {
    CaptureError();
    return 0;
  }
}
void re_renderer_reset_graphics_commands(re_renderer *r) {
  try {
    Device(r).ResetCommands();
  } catch (...) {
    CaptureError();
  }
}
uint32_t re_renderer_reload_changed_shaders(re_renderer *r) {
  ClearError();
  try {
    const uint32_t count = Device(r).ReloadChangedShaders();
    if (!Device(r).LastShaderError().empty())
      g_lastError = Device(r).LastShaderError();
    return count;
  } catch (...) {
    CaptureError();
    return 0;
  }
}

void re_identity_transform(float *transform) {
  if (!transform)
    return;
  const auto result = rendering::IdentityTransform();
  std::copy(result.begin(), result.end(), transform);
}

void re_compose_transform(const float *translation, const float *rotation,
                          const float *scale, float *transform) {
  if (!translation || !rotation || !scale || !transform)
    return;
  const std::array<float, 3> t{translation[0], translation[1], translation[2]};
  const std::array<float, 3> r{rotation[0], rotation[1], rotation[2]};
  const std::array<float, 3> s{scale[0], scale[1], scale[2]};
  const auto result = rendering::ComposeTransform(t, r, s);
  std::copy(result.begin(), result.end(), transform);
}

} // extern "C"
