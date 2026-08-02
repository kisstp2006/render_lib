#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(ENGINE_RENDERER_SDK_BUILD)
#    define RE_API __declspec(dllexport)
#  else
#    define RE_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define RE_API __attribute__((visibility("default")))
#else
#  define RE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RE_API_VERSION 1u

typedef struct re_renderer re_renderer;
typedef uint64_t re_mesh;
typedef uint64_t re_material;
typedef uint64_t re_object;
typedef uint64_t re_point_light;

typedef enum re_backend {
    RE_BACKEND_OPENGL = 0,
    RE_BACKEND_VULKAN = 1
} re_backend;

typedef struct re_renderer_desc {
    uint32_t struct_size;
    re_backend backend;
    const char* window_title;
    uint32_t width;
    uint32_t height;
    int32_t resizable;
    int32_t visible;
    int32_t vsync;
    int32_t validation;
    uint32_t msaa_samples;
    const char* shader_directory;
    const char* pipeline_cache_directory;
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

RE_API uint32_t re_get_api_version(void);
RE_API const char* re_get_last_error(void);
RE_API void re_renderer_desc_init(re_renderer_desc* desc);
RE_API void re_material_desc_init(re_material_desc* desc);
RE_API void re_camera_desc_init(re_camera_desc* desc);
RE_API void re_directional_light_desc_init(re_directional_light_desc* desc);
RE_API void re_point_light_desc_init(re_point_light_desc* desc);

RE_API re_renderer* re_renderer_create(const re_renderer_desc* desc);
RE_API void re_renderer_destroy(re_renderer* renderer);
RE_API int32_t re_renderer_pump_events(re_renderer* renderer);
RE_API int32_t re_renderer_tick(re_renderer* renderer, float delta_seconds);
RE_API int32_t re_renderer_render_frame(re_renderer* renderer, float delta_seconds);
RE_API void re_renderer_request_close(re_renderer* renderer);
RE_API int32_t re_renderer_should_close(const re_renderer* renderer);
RE_API int32_t re_renderer_resize(re_renderer* renderer, uint32_t width, uint32_t height);
RE_API const char* re_renderer_backend_name(const re_renderer* renderer);
RE_API int32_t re_renderer_get_frame_stats(const re_renderer* renderer, re_frame_stats* stats);

RE_API re_mesh re_renderer_create_mesh(re_renderer* renderer,
    const re_vertex* vertices, size_t vertex_count,
    const uint32_t* indices, size_t index_count);
RE_API re_mesh re_renderer_create_cube(re_renderer* renderer, float half_extent);
RE_API re_mesh re_renderer_create_sphere(re_renderer* renderer, float radius,
    uint32_t stacks, uint32_t slices);
RE_API void re_renderer_destroy_mesh(re_renderer* renderer, re_mesh mesh);

RE_API re_material re_renderer_create_material(re_renderer* renderer,
    const re_material_desc* desc);
RE_API int32_t re_renderer_update_material(re_renderer* renderer,
    re_material material, const re_material_desc* desc);
RE_API void re_renderer_destroy_material(re_renderer* renderer, re_material material);

RE_API re_object re_renderer_add_object(re_renderer* renderer, re_mesh mesh,
    re_material material, const float* column_major_transform_16);
RE_API int32_t re_renderer_set_object_transform(re_renderer* renderer,
    re_object object, const float* column_major_transform_16);
RE_API int32_t re_renderer_remove_object(re_renderer* renderer, re_object object);
RE_API void re_renderer_clear_objects(re_renderer* renderer);

RE_API re_point_light re_renderer_add_point_light(re_renderer* renderer,
    const re_point_light_desc* desc);
RE_API int32_t re_renderer_set_point_light(re_renderer* renderer,
    re_point_light light, const re_point_light_desc* desc);
RE_API int32_t re_renderer_remove_point_light(re_renderer* renderer, re_point_light light);
RE_API void re_renderer_clear_point_lights(re_renderer* renderer);

RE_API int32_t re_renderer_set_camera(re_renderer* renderer, const re_camera_desc* desc);
RE_API int32_t re_renderer_set_sun(re_renderer* renderer,
    const re_directional_light_desc* desc);
RE_API int32_t re_renderer_set_exposure(re_renderer* renderer, float exposure);
RE_API int32_t re_renderer_set_background_color(re_renderer* renderer,
    const float* zenith_3, const float* horizon_3);
RE_API int32_t re_renderer_request_screenshot(re_renderer* renderer, const char* path);
RE_API void re_identity_transform(float* column_major_transform_16);
RE_API void re_compose_transform(const float* translation_3,
    const float* rotation_degrees_3, const float* scale_3,
    float* column_major_transform_16);

#ifdef __cplusplus
}
#endif
