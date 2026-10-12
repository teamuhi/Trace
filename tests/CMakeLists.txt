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
    test_selection.cpp
    test_ini.cpp
    test_keymap.cpp
    test_transform.cpp
    test_guides.cpp
    test_image_ops.cpp
    test_pattern.cpp
    test_font_check.cpp
    test_i18n.cpp
    test_ui_strings.cpp
)
target_link_libraries(trace_tests PRIVATE trace_options trace_core trace_tools trace_io doctest::doctest)

target_include_directories(trace_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/msf_gif)
target_compile_definitions(trace_tests PRIVATE TRACE_README_PATH="${PROJECT_SOURCE_DIR}/README.md"
    TRACE_LANGUAGES_DIR="${PROJECT_SOURCE_DIR}/languages")

add_test(NAME trace_tests COMMAND trace_tests)
