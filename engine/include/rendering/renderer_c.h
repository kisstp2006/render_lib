#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(ENGINE_RENDERER_SDK_BUILD)
#define RE_API __declspec(dllexport)
#else
#define RE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define RE_API __attribute__((visibility("default")))
#else
#define RE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RE_API_VERSION 2u

typedef struct re_renderer re_renderer;
typedef uint64_t re_mesh;
typedef uint64_t re_material;
typedef uint64_t re_object;
typedef uint64_t re_point_light;
typedef uint64_t re_shader_module;
typedef uint64_t re_graphics_pipeline;
typedef uint64_t re_compute_pipeline;
typedef uint64_t re_buffer;
typedef uint64_t re_texture;
typedef uint64_t re_sampler;
typedef uint64_t re_render_target;

typedef enum re_backend {
  RE_BACKEND_OPENGL = 0,
  RE_BACKEND_VULKAN = 1
} re_backend;

typedef struct re_renderer_desc {
  uint32_t struct_size;
  re_backend backend;
  const char *window_title;
  uint32_t width;
  uint32_t height;
  int32_t resizable;
  int32_t visible;
  int32_t vsync;
  int32_t validation;
  uint32_t msaa_samples;
  const char *shader_directory;
  const char *pipeline_cache_directory;
} re_renderer_desc;

typedef struct re_vertex {
  float position[3];
  float normal[3];
  float tangent[4];
  float uv[2];
} re_vertex;

typedef struct re_material_desc {
  float albedo[3];
  float alpha;
  float metallic;
  float roughness;
  float emissive[3];
  float ambient_occlusion;
  float specular_f0;
} re_material_desc;

typedef struct re_camera_desc {
  float position[3];
  float yaw_degrees;
  float pitch_degrees;
  float vertical_fov_degrees;
  float near_plane;
  float far_plane;
} re_camera_desc;

typedef struct re_directional_light_desc {
  float direction[3];
  float color[3];
  float intensity;
  int32_t casts_shadows;
} re_directional_light_desc;

typedef struct re_point_light_desc {
  float position[3];
  float color[3];
  float intensity;
  float radius;
  int32_t casts_shadows;
} re_point_light_desc;

typedef struct re_frame_stats {
  float gpu_frame_milliseconds;
  uint32_t draw_batch_count;
  uint32_t instance_count;
  uint32_t draw_calls_saved;
} re_frame_stats;

typedef enum re_shader_language {
  RE_SHADER_HLSL,
  RE_SHADER_GLSL,
  RE_SHADER_SPIRV
} re_shader_language;
typedef enum re_shader_stage {
  RE_STAGE_VERTEX,
  RE_STAGE_FRAGMENT,
  RE_STAGE_COMPUTE
} re_shader_stage;
typedef enum re_shader_resource_type {
  RE_RESOURCE_UNKNOWN,
  RE_RESOURCE_VERTEX_INPUT,
  RE_RESOURCE_UNIFORM_BUFFER,
  RE_RESOURCE_STORAGE_BUFFER,
  RE_RESOURCE_SAMPLED_TEXTURE,
  RE_RESOURCE_COMBINED_TEXTURE_SAMPLER,
  RE_RESOURCE_STORAGE_TEXTURE,
  RE_RESOURCE_SAMPLER,
  RE_RESOURCE_PUSH_CONSTANTS
} re_shader_resource_type;

typedef struct re_shader_define {
  const char *name;
  const char *value;
} re_shader_define;
typedef struct re_shader_module_desc {
  uint32_t struct_size;
  re_shader_stage stage;
  re_shader_language language;
  const char *source_path;
  const void *source_data;
  size_t source_size;
  const char *entry_point;
  const re_shader_define *defines;
  size_t define_count;
  int32_t optimize;
  int32_t generate_debug_info;
  int32_t enable_hot_reload;
  const char *debug_name;
} re_shader_module_desc;

typedef struct re_shader_reflection_info {
  re_shader_stage stage;
  char entry_point[64];
  char permutation_key[65];
  uint64_t generation;
  uint32_t resource_count;
} re_shader_reflection_info;
typedef struct re_shader_resource {
  char name[128];
  re_shader_resource_type type;
  uint32_t set;
  uint32_t binding;
  uint32_t location;
  uint32_t array_count;
} re_shader_resource;

typedef enum re_vertex_format {
  RE_VERTEX_FLOAT,
  RE_VERTEX_FLOAT2,
  RE_VERTEX_FLOAT3,
  RE_VERTEX_FLOAT4,
  RE_VERTEX_UINT,
  RE_VERTEX_UINT2,
  RE_VERTEX_UINT4,
  RE_VERTEX_UBYTE4_NORMALIZED
} re_vertex_format;
typedef enum re_vertex_input_rate {
  RE_INPUT_PER_VERTEX,
  RE_INPUT_PER_INSTANCE
} re_vertex_input_rate;
typedef struct re_vertex_binding_desc {
  uint32_t binding;
  uint32_t stride;
  re_vertex_input_rate input_rate;
} re_vertex_binding_desc;
typedef struct re_vertex_attribute_desc {
  uint32_t location;
  uint32_t binding;
  re_vertex_format format;
  uint32_t offset;
} re_vertex_attribute_desc;
typedef enum re_primitive_topology {
  RE_TOPOLOGY_TRIANGLE_LIST,
  RE_TOPOLOGY_TRIANGLE_STRIP,
  RE_TOPOLOGY_LINE_LIST,
  RE_TOPOLOGY_POINT_LIST
} re_primitive_topology;
typedef enum re_cull_mode {
  RE_CULL_NONE,
  RE_CULL_FRONT,
  RE_CULL_BACK
} re_cull_mode;
typedef enum re_front_face {
  RE_FRONT_COUNTER_CLOCKWISE,
  RE_FRONT_CLOCKWISE
} re_front_face;
typedef enum re_compare_operation {
  RE_COMPARE_NEVER,
  RE_COMPARE_LESS,
  RE_COMPARE_LESS_EQUAL,
  RE_COMPARE_EQUAL,
  RE_COMPARE_GREATER_EQUAL,
  RE_COMPARE_GREATER,
  RE_COMPARE_ALWAYS
} re_compare_operation;
typedef enum re_texture_format {
  RE_FORMAT_R8_UNORM,
  RE_FORMAT_RG8_UNORM,
  RE_FORMAT_RGBA8_UNORM,
  RE_FORMAT_RGBA8_SRGB,
  RE_FORMAT_RGBA16_FLOAT,
  RE_FORMAT_R32_FLOAT,
  RE_FORMAT_DEPTH32_FLOAT
} re_texture_format;

typedef struct re_graphics_pipeline_desc {
  uint32_t struct_size;
  re_shader_module vertex_shader;
  re_shader_module fragment_shader;
  const re_vertex_binding_desc *vertex_bindings;
  size_t vertex_binding_count;
  const re_vertex_attribute_desc *vertex_attributes;
  size_t vertex_attribute_count;
  re_primitive_topology topology;
  re_cull_mode cull;
  re_front_face winding;
  int32_t depth_test;
  int32_t depth_write;
  re_compare_operation depth_compare;
  int32_t blend_enabled;
  const re_texture_format *color_formats;
  size_t color_format_count;
  int32_t has_depth_format;
  re_texture_format depth_format;
  const char *debug_name;
} re_graphics_pipeline_desc;
typedef struct re_compute_pipeline_desc {
  uint32_t struct_size;
  re_shader_module compute_shader;
  const char *debug_name;
} re_compute_pipeline_desc;

enum {
  RE_BUFFER_VERTEX = 1u << 0u,
  RE_BUFFER_INDEX = 1u << 1u,
  RE_BUFFER_UNIFORM = 1u << 2u,
  RE_BUFFER_STORAGE = 1u << 3u,
  RE_BUFFER_TRANSFER_SOURCE = 1u << 4u,
  RE_BUFFER_TRANSFER_DESTINATION = 1u << 5u
};
typedef struct re_buffer_desc {
  uint32_t struct_size;
  uint64_t size;
  uint32_t usage;
  int32_t cpu_writable;
  const char *debug_name;
} re_buffer_desc;
enum {
  RE_TEXTURE_SAMPLED = 1u << 0u,
  RE_TEXTURE_STORAGE = 1u << 1u,
  RE_TEXTURE_COLOR_ATTACHMENT = 1u << 2u,
  RE_TEXTURE_DEPTH_ATTACHMENT = 1u << 3u,
  RE_TEXTURE_TRANSFER_SOURCE = 1u << 4u,
  RE_TEXTURE_TRANSFER_DESTINATION = 1u << 5u
};
typedef struct re_texture_desc {
  uint32_t struct_size;
  uint32_t width;
  uint32_t height;
  uint32_t mip_levels;
  re_texture_format format;
  uint32_t usage;
  const char *debug_name;
} re_texture_desc;
typedef enum re_filter { RE_FILTER_NEAREST, RE_FILTER_LINEAR } re_filter;
typedef enum re_address_mode {
  RE_ADDRESS_REPEAT,
  RE_ADDRESS_MIRRORED_REPEAT,
  RE_ADDRESS_CLAMP_EDGE,
  RE_ADDRESS_CLAMP_BORDER
} re_address_mode;
typedef struct re_sampler_desc {
  uint32_t struct_size;
  re_filter min_filter;
  re_filter mag_filter;
  re_address_mode address_u;
  re_address_mode address_v;
  re_address_mode address_w;
  float max_anisotropy;
  const char *debug_name;
} re_sampler_desc;
typedef struct re_render_target_desc {
  uint32_t struct_size;
  const re_texture *color_attachments;
  size_t color_attachment_count;
  re_texture depth_attachment;
  const char *debug_name;
} re_render_target_desc;
typedef enum re_load_action { RE_LOAD, RE_CLEAR, RE_DONT_CARE } re_load_action;
typedef enum re_store_action { RE_STORE, RE_STORE_DONT_CARE } re_store_action;
typedef struct re_render_pass_desc {
  uint32_t struct_size;
  re_render_target target;
  re_load_action color_load;
  re_store_action color_store;
  float clear_color[4];
  re_load_action depth_load;
  re_store_action depth_store;
  float clear_depth;
  const char *debug_name;
} re_render_pass_desc;
typedef enum re_index_type { RE_INDEX_UINT16, RE_INDEX_UINT32 } re_index_type;

RE_API uint32_t re_get_api_version(void);
RE_API const char *re_get_last_error(void);
RE_API void re_renderer_desc_init(re_renderer_desc *desc);
RE_API void re_material_desc_init(re_material_desc *desc);
RE_API void re_camera_desc_init(re_camera_desc *desc);
RE_API void re_directional_light_desc_init(re_directional_light_desc *desc);
RE_API void re_point_light_desc_init(re_point_light_desc *desc);
RE_API void re_shader_module_desc_init(re_shader_module_desc *desc);
RE_API void re_graphics_pipeline_desc_init(re_graphics_pipeline_desc *desc);
RE_API void re_compute_pipeline_desc_init(re_compute_pipeline_desc *desc);
RE_API void re_buffer_desc_init(re_buffer_desc *desc);
RE_API void re_texture_desc_init(re_texture_desc *desc);
RE_API void re_sampler_desc_init(re_sampler_desc *desc);
RE_API void re_render_target_desc_init(re_render_target_desc *desc);
RE_API void re_render_pass_desc_init(re_render_pass_desc *desc);

RE_API re_renderer *re_renderer_create(const re_renderer_desc *desc);
RE_API void re_renderer_destroy(re_renderer *renderer);
RE_API int32_t re_renderer_pump_events(re_renderer *renderer);
RE_API int32_t re_renderer_tick(re_renderer *renderer, float delta_seconds);
RE_API int32_t re_renderer_render_frame(re_renderer *renderer,
                                        float delta_seconds);
RE_API void re_renderer_request_close(re_renderer *renderer);
RE_API int32_t re_renderer_should_close(const re_renderer *renderer);
RE_API int32_t re_renderer_resize(re_renderer *renderer, uint32_t width,
                                  uint32_t height);
RE_API const char *re_renderer_backend_name(const re_renderer *renderer);
RE_API int32_t re_renderer_get_frame_stats(const re_renderer *renderer,
                                           re_frame_stats *stats);

RE_API re_mesh re_renderer_create_mesh(re_renderer *renderer,
                                       const re_vertex *vertices,
                                       size_t vertex_count,
                                       const uint32_t *indices,
                                       size_t index_count);
RE_API re_mesh re_renderer_create_cube(re_renderer *renderer,
                                       float half_extent);
RE_API re_mesh re_renderer_create_sphere(re_renderer *renderer, float radius,
                                         uint32_t stacks, uint32_t slices);
RE_API void re_renderer_destroy_mesh(re_renderer *renderer, re_mesh mesh);

RE_API re_material re_renderer_create_material(re_renderer *renderer,
                                               const re_material_desc *desc);
RE_API int32_t re_renderer_update_material(re_renderer *renderer,
                                           re_material material,
                                           const re_material_desc *desc);
RE_API void re_renderer_destroy_material(re_renderer *renderer,
                                         re_material material);

RE_API re_object re_renderer_add_object(re_renderer *renderer, re_mesh mesh,
                                        re_material material,
                                        const float *column_major_transform_16);
RE_API int32_t
re_renderer_set_object_transform(re_renderer *renderer, re_object object,
                                 const float *column_major_transform_16);
RE_API int32_t re_renderer_remove_object(re_renderer *renderer,
                                         re_object object);
RE_API void re_renderer_clear_objects(re_renderer *renderer);

RE_API re_point_light re_renderer_add_point_light(
    re_renderer *renderer, const re_point_light_desc *desc);
RE_API int32_t re_renderer_set_point_light(re_renderer *renderer,
                                           re_point_light light,
                                           const re_point_light_desc *desc);
RE_API int32_t re_renderer_remove_point_light(re_renderer *renderer,
                                              re_point_light light);
RE_API void re_renderer_clear_point_lights(re_renderer *renderer);

RE_API int32_t re_renderer_set_camera(re_renderer *renderer,
                                      const re_camera_desc *desc);
RE_API int32_t re_renderer_set_sun(re_renderer *renderer,
                                   const re_directional_light_desc *desc);
RE_API int32_t re_renderer_set_exposure(re_renderer *renderer, float exposure);
RE_API int32_t re_renderer_set_background_color(re_renderer *renderer,
                                                const float *zenith_3,
                                                const float *horizon_3);
RE_API int32_t re_renderer_request_screenshot(re_renderer *renderer,
                                              const char *path);

RE_API re_shader_module
re_renderer_create_shader_module(re_renderer *, const re_shader_module_desc *);
RE_API re_shader_module re_renderer_create_shader_permutation(
    re_renderer *, re_shader_module, const re_shader_define *, size_t);
RE_API void re_renderer_destroy_shader_module(re_renderer *, re_shader_module);
RE_API int32_t re_renderer_get_shader_reflection(re_renderer *,
                                                 re_shader_module,
                                                 re_shader_reflection_info *);
RE_API int32_t re_renderer_get_shader_resource(re_renderer *, re_shader_module,
                                               uint32_t index,
                                               re_shader_resource *);
RE_API re_graphics_pipeline re_renderer_create_graphics_pipeline(
    re_renderer *, const re_graphics_pipeline_desc *);
RE_API re_compute_pipeline re_renderer_create_compute_pipeline(
    re_renderer *, const re_compute_pipeline_desc *);
RE_API void re_renderer_destroy_graphics_pipeline(re_renderer *,
                                                  re_graphics_pipeline);
RE_API void re_renderer_destroy_compute_pipeline(re_renderer *,
                                                 re_compute_pipeline);
RE_API re_buffer re_renderer_create_buffer(re_renderer *,
                                           const re_buffer_desc *,
                                           const void *initial_data,
                                           size_t initial_size);
RE_API int32_t re_renderer_update_buffer(re_renderer *, re_buffer,
                                         uint64_t offset, const void *data,
                                         size_t size);
RE_API void re_renderer_destroy_buffer(re_renderer *, re_buffer);
RE_API re_texture re_renderer_create_texture(re_renderer *,
                                             const re_texture_desc *,
                                             const void *initial_data,
                                             size_t initial_size);
RE_API int32_t re_renderer_update_texture(re_renderer *, re_texture,
                                          uint32_t mip_level, const void *data,
                                          size_t size);
RE_API void re_renderer_destroy_texture(re_renderer *, re_texture);
RE_API re_sampler re_renderer_create_sampler(re_renderer *,
                                             const re_sampler_desc *);
RE_API void re_renderer_destroy_sampler(re_renderer *, re_sampler);
RE_API re_render_target
re_renderer_create_render_target(re_renderer *, const re_render_target_desc *);
RE_API void re_renderer_destroy_render_target(re_renderer *, re_render_target);
RE_API int32_t re_renderer_begin_render_pass(re_renderer *,
                                             const re_render_pass_desc *);
RE_API int32_t re_renderer_end_render_pass(re_renderer *);
RE_API int32_t re_renderer_bind_graphics_pipeline(re_renderer *,
                                                  re_graphics_pipeline);
RE_API int32_t re_renderer_bind_compute_pipeline(re_renderer *,
                                                 re_compute_pipeline);
RE_API int32_t re_renderer_bind_vertex_buffer(re_renderer *, uint32_t binding,
                                              re_buffer, uint64_t offset);
RE_API int32_t re_renderer_bind_index_buffer(re_renderer *, re_buffer,
                                             re_index_type, uint64_t offset);
RE_API int32_t re_renderer_bind_uniform_buffer(re_renderer *, uint32_t set,
                                               uint32_t binding, re_buffer,
                                               uint64_t offset, uint64_t size);
RE_API int32_t re_renderer_bind_storage_buffer(re_renderer *, uint32_t set,
                                               uint32_t binding, re_buffer,
                                               uint64_t offset, uint64_t size);
RE_API int32_t re_renderer_bind_texture(re_renderer *, uint32_t set,
                                        uint32_t binding, re_texture,
                                        re_sampler);
RE_API int32_t re_renderer_bind_sampler(re_renderer *, uint32_t set,
                                        uint32_t binding, re_sampler);
RE_API int32_t re_renderer_bind_storage_texture(re_renderer *, uint32_t set,
                                                uint32_t binding, re_texture);
RE_API int32_t re_renderer_draw(re_renderer *, uint32_t vertex_count,
                                uint32_t instance_count, uint32_t first_vertex,
                                uint32_t first_instance);
RE_API int32_t re_renderer_draw_indexed(re_renderer *, uint32_t index_count,
                                        uint32_t instance_count,
                                        uint32_t first_index,
                                        int32_t vertex_offset,
                                        uint32_t first_instance);
RE_API int32_t re_renderer_dispatch(re_renderer *, uint32_t x, uint32_t y,
                                    uint32_t z);
RE_API void re_renderer_reset_graphics_commands(re_renderer *);
RE_API uint32_t re_renderer_reload_changed_shaders(re_renderer *);
RE_API void re_identity_transform(float *column_major_transform_16);
RE_API void re_compose_transform(const float *translation_3,
                                 const float *rotation_degrees_3,
                                 const float *scale_3,
                                 float *column_major_transform_16);

#ifdef __cplusplus
}
#endif
