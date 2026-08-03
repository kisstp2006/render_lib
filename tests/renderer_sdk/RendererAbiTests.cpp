#include "engine/renderer/Renderer.h"
#include "rendering/renderer_c.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {

bool Near(float left, float right) { return std::abs(left - right) < 0.0001f; }

} // namespace

int main() {
  if (re_get_api_version() != RE_API_VERSION ||
      rendering::ApiVersion != RE_API_VERSION)
    return 1;

  re_renderer_desc renderer{};
  re_renderer_desc_init(&renderer);
  if (renderer.struct_size != sizeof(renderer) || renderer.width != 1280 ||
      renderer.height != 720 || renderer.backend != RE_BACKEND_OPENGL)
    return 2;

  re_material_desc material{};
  re_material_desc_init(&material);
  if (!Near(material.albedo[0], 0.8f) || !Near(material.roughness, 0.5f) ||
      !Near(material.specular_f0, 0.04f))
    return 3;

  re_shader_module_desc shader{};
  re_shader_module_desc_init(&shader);
  if (shader.struct_size != sizeof(shader) || shader.stage != RE_STAGE_VERTEX ||
      shader.language != RE_SHADER_HLSL || !shader.optimize ||
      !shader.enable_hot_reload || std::strcmp(shader.entry_point, "main") != 0)
    return 4;

  re_graphics_pipeline_desc pipeline{};
  re_graphics_pipeline_desc_init(&pipeline);
  if (pipeline.struct_size != sizeof(pipeline) ||
      pipeline.topology != RE_TOPOLOGY_TRIANGLE_LIST ||
      pipeline.cull != RE_CULL_BACK ||
      pipeline.depth_compare != RE_COMPARE_LESS)
    return 5;

  re_buffer_desc buffer{};
  re_texture_desc texture{};
  re_sampler_desc sampler{};
  re_render_pass_desc pass{};
  re_buffer_desc_init(&buffer);
  re_texture_desc_init(&texture);
  re_sampler_desc_init(&sampler);
  re_render_pass_desc_init(&pass);
  if (buffer.struct_size != sizeof(buffer) ||
      buffer.usage != RE_BUFFER_VERTEX ||
      texture.struct_size != sizeof(texture) || texture.width != 1 ||
      texture.height != 1 || sampler.struct_size != sizeof(sampler) ||
      sampler.max_anisotropy != 1.0f || pass.struct_size != sizeof(pass) ||
      pass.clear_depth != 1.0f)
    return 6;

  const float translation[3]{1.0f, 2.0f, 3.0f};
  const float rotation[3]{};
  const float scale[3]{1.0f, 1.0f, 1.0f};
  float transform[16]{};
  re_compose_transform(translation, rotation, scale, transform);
  if (!Near(transform[12], 1.0f) || !Near(transform[13], 2.0f) ||
      !Near(transform[14], 3.0f) || !Near(transform[15], 1.0f))
    return 7;

  if (re_renderer_resize(nullptr, 640, 480) != 0 ||
      std::strlen(re_get_last_error()) == 0)
    return 8;

  std::cout << "Rendering Engine C/C++ ABI v" << RE_API_VERSION << " passed\n";
  return 0;
}
