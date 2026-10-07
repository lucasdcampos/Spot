include(FetchContent)

# Dear ImGui has no CMake project of its own, and ImGuizmo's would fetch another ImGui, so both are only downloaded
# (SOURCE_SUBDIR points nowhere) and built below into one library.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none
    SYSTEM
)
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 18cef5e031d8c6973d80284c67f60549fafd78c1
    SOURCE_SUBDIR none
    SYSTEM
)
# Names for the code points of Font Awesome 6, whose font is in resources/fonts.
FetchContent_Declare(icon_font_headers
    URL https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/210b5a399a64270674560d633638952d1e8d804d/IconsFontAwesome6.h
    URL_HASH SHA256=1986b023825b269fb8c60ccc2419c4594280249596e828e573d5c8ef309e265d
    DOWNLOAD_NO_EXTRACT TRUE
    SYSTEM
)
FetchContent_MakeAvailable(imgui imguizmo icon_font_headers)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/misc/cpp
    ${imguizmo_SOURCE_DIR}/src
    ${icon_font_headers_SOURCE_DIR}
)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
# Third-party code: its warnings are not ours to fix.
target_compile_options(imgui PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>)

# Everything but main(), so tests can link the editor.
add_library(spot_editor_core STATIC
    app/editor_application.cpp
    app/editor_application.h
    app/editor_context.cpp
    app/editor_context.h
    app/editor_layout.cpp
    app/editor_layout.h
    assets/asset_browser_model.cpp
    assets/asset_browser_model.h
    assets/asset_list.cpp
    assets/asset_list.h
    assets/thumbnail_cache.cpp
    assets/thumbnail_cache.h
    core/command.h
    core/command_history.cpp
    core/command_history.h
    core/console_log.cpp
    core/console_log.h
    core/editor_log.cpp
    core/editor_log.h
    core/editor_settings.cpp
    core/editor_settings.h
    core/text.cpp
    core/text.h
    imgui/drag_payloads.h
    imgui/editor_style.cpp
    imgui/editor_style.h
    imgui/icons.h
    imgui/imgui_layer.cpp
    imgui/imgui_layer.h
    imgui/imgui_platform.cpp
    imgui/imgui_platform.h
    imgui/imgui_renderer.cpp
    imgui/imgui_renderer.h
    imgui/widgets.cpp
    imgui/widgets.h
    inspector/asset_inspector.cpp
    inspector/asset_inspector.h
    inspector/entity_inspector.cpp
    inspector/entity_inspector.h
    inspector/inspector_backend.cpp
    inspector/inspector_backend.h
    inspector/inspector_clipboard.cpp
    inspector/inspector_clipboard.h
    inspector/multi_edit.cpp
    inspector/multi_edit.h
    inspector/property_widgets.cpp
    inspector/property_widgets.h
    panels/asset_browser_panel.cpp
    panels/asset_browser_panel.h
    panels/console_panel.cpp
    panels/console_panel.h
    panels/hierarchy_panel.cpp
    panels/hierarchy_panel.h
    panels/inspector_panel.cpp
    panels/inspector_panel.h
    panels/panel.cpp
    panels/panel.h
    panels/project_settings_panel.cpp
    panels/project_settings_panel.h
    project/editor_project.cpp
    project/editor_project.h
    project/new_project_dialog.cpp
    project/new_project_dialog.h
    project/project_hub.cpp
    project/project_hub.h
    project/project_templates.cpp
    project/project_templates.h
    scene/entity_actions.cpp
    scene/entity_actions.h
    scene/scene_commands.cpp
    scene/scene_commands.h
    scene/scene_document.cpp
    scene/scene_document.h
    scene/scene_workspace.cpp
    scene/scene_workspace.h
    scene/selection.cpp
    scene/selection.h
    viewport/editor_camera.cpp
    viewport/editor_camera.h
    viewport/scene_view.cpp
    viewport/scene_view.h
    viewport/transform_gizmo.cpp
    viewport/transform_gizmo.h
    viewport/viewport_renderer.cpp
    viewport/viewport_renderer.h
)
target_include_directories(spot_editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spot_editor_core PUBLIC
    spot::spot
    imgui
)
# The engine keeps its JSON library to itself; the editor uses it for its own settings files.
target_link_libraries(spot_editor_core PRIVATE nlohmann_json::nlohmann_json)
spot_add_shaders(spot_editor_core
    imgui/imgui.frag.hlsl
    imgui/imgui.vert.hlsl
    viewport/fullscreen.vert.hlsl
    viewport/grid.frag.hlsl
    viewport/outline.frag.hlsl
    viewport/silhouette.frag.hlsl
    viewport/silhouette.vert.hlsl
)
spot_embed_files(spot_editor_core
    resources/fonts/fa_solid_900.ttf
    resources/fonts/inter_regular.ttf
    resources/fonts/inter_semibold.ttf
)
spot_set_compile_options(spot_editor_core)

add_executable(spot_editor
    main.cpp
)
target_link_libraries(spot_editor PRIVATE spot_editor_core)
spot_set_compile_options(spot_editor)
