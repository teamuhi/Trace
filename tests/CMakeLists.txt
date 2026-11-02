find_package(doctest CONFIG REQUIRED)

add_executable(trace_tests
    test_image.cpp
    test_document.cpp
    test_view_transform.cpp
    test_color.cpp
    test_raster.cpp
    test_tools.cpp
    test_history.cpp
    test_image_io.cpp
    test_image_extra.cpp
    test_file_write.cpp
    test_project_io.cpp
    test_selection.cpp
    test_ini.cpp
    test_keymap.cpp
    test_transform.cpp
    test_guides.cpp
    test_image_ops.cpp
    test_pattern.cpp
    test_font_check.cpp
    test_font_names.cpp
    test_i18n.cpp
    test_ui_strings.cpp
    test_mode.cpp
    test_layers.cpp
    test_endless_board.cpp
    test_board_export.cpp
    test_board_project.cpp
    test_pixel_project.cpp
    test_animation.cpp
    test_timeline_edit.cpp
    test_pixel_export.cpp
    test_palette.cpp
    test_palette_edit.cpp
    test_symmetry.cpp
    test_text_raster.cpp
    test_text_tool.cpp
    test_flow_document.cpp
    test_flow_history.cpp
    test_flow_geometry.cpp
    test_flow_layout.cpp
    test_flow_templates.cpp
    test_flow_serialize.cpp
    test_flow_svg.cpp
    test_dialog_logic.cpp
    test_svg_icon.cpp
    test_panel_layout.cpp
    test_tools_layout.cpp
)
target_link_libraries(trace_tests PRIVATE trace_options trace_core trace_tools trace_io trace_flow doctest::doctest)

target_include_directories(trace_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/msf_gif)
target_compile_definitions(trace_tests PRIVATE TRACE_README_PATH="${PROJECT_SOURCE_DIR}/README.md"
    TRACE_LANGUAGES_DIR="${PROJECT_SOURCE_DIR}/languages"
    TRACE_FONTS_DIR="${PROJECT_SOURCE_DIR}/third_party/fonts"
    TRACE_ICONS_DIR="${PROJECT_SOURCE_DIR}/src/assets/icons")

add_test(NAME trace_tests COMMAND trace_tests)
