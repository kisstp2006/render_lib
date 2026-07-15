add_library(engine_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
if(ENGINE_BUILD_VULKAN)
    target_sources(engine_imgui PRIVATE
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
    target_link_libraries(engine_imgui PUBLIC Vulkan::Vulkan)
endif()
target_include_directories(engine_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(engine_imgui PUBLIC glfw)
set_target_properties(engine_imgui PROPERTIES FOLDER "Dependencies")

set(editor_ui_dependencies
    engine_foundation
    engine_core
    engine_renderer_core
    engine_imgui)
if(ENGINE_BUILD_VULKAN)
    list(APPEND editor_ui_dependencies Vulkan::Vulkan)
endif()

engine_add_module(engine_editor_ui
    FOLDER "Editor/ImGui"
    ALIAS editor_ui
    SOURCES
        ${ENGINE_ROOT}/include/engine/editor/ImGuiLayer.h
        ${ENGINE_ROOT}/src/editor/ImGuiLayer.cpp
    PUBLIC_DEPS
        ${editor_ui_dependencies})
