#include "engine/renderer/Renderer.h"
#include "rendering/renderer_c.h"

#include <filesystem>
#include <iostream>

int main()
{
    const std::filesystem::path shaders(RENDERING_ENGINE_PACKAGE_SHADER_DIR);
    if (rendering::ApiVersion != RE_API_VERSION ||
        re_get_api_version() != RE_API_VERSION ||
        !std::filesystem::exists(shaders / "hlsl"))
        return 1;
    std::cout << "Found Rendering Engine ABI v" << RE_API_VERSION
              << " and shaders at " << shaders << '\n';
    return 0;
}
