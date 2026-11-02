cmake_minimum_required(VERSION 3.25)

project(Trace VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRACE_BUILD_TESTS "Build unit tests" ON)
option(TRACE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(TRACE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer (non-MSVC)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT TRACE_IPO_SUPPORTED OUTPUT TRACE_IPO_OUTPUT LANGUAGES CXX)
if(TRACE_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(STATUS "IPO/LTO not supported: ${TRACE_IPO_OUTPUT}")
endif()

# Shared compile/link options applied to every Trace target.
add_library(trace_options INTERFACE)
if(MSVC)
    target_compile_options(trace_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    # Release: full optimization, one section per function so the linker can drop what is unused.
    target_compile_options(trace_options INTERFACE $<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Zc:inline>)
    target_link_options(trace_options INTERFACE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    if(TRACE_WARNINGS_AS_ERRORS)
        target_compile_options(trace_options INTERFACE /WX)
    endif()
else()
    target_compile_options(trace_options INTERFACE -Wall -Wextra -Wpedantic)
    # Release: hidden symbols and per-function sections, so the linker can drop unused code.
    target_compile_options(trace_options INTERFACE $<$<CONFIG:Release>:-fvisibility=hidden>
                           $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    if(APPLE)
        target_link_options(trace_options INTERFACE $<$<CONFIG:Release>:-Wl,-dead_strip>)
    else()
        target_link_options(trace_options INTERFACE $<$<CONFIG:Release>:-Wl,--gc-sections>)
    endif()
    if(TRACE_WARNINGS_AS_ERRORS)
        target_compile_options(trace_options INTERFACE -Werror)
    endif()
    if(TRACE_SANITIZERS)
        target_compile_options(trace_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(trace_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

find_path(STB_INCLUDE_DIRS "stb_image.h")

# --- trace_core: pure C++, no SDL/ImGui ------------------------------------------------------------
add_library(trace_core STATIC
    src/core/image.cpp
    src/core/document.cpp
    src/core/view_transform.cpp
    src/core/raster.cpp
    src/core/board_export.cpp
    src/core/history.cpp
    src/core/selection.cpp
    src/core/ini.cpp
    src/core/keymap.cpp
    src/core/transform.cpp
    src/core/color_text.cpp
    src/core/pattern.cpp
    src/core/font_check.cpp
    src/core/i18n.cpp
    src/core/guides.cpp
    src/core/palette.cpp
    src/core/animation.cpp
    src/core/text_raster.cpp
    src/core/font_names.cpp
    src/core/svg_icon.cpp
)
target_include_directories(trace_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(trace_core SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg)
target_link_libraries(trace_core PRIVATE trace_options)
if(STB_INCLUDE_DIRS)
    target_include_directories(trace_core SYSTEM PUBLIC ${STB_INCLUDE_DIRS})
endif()

# --- trace_tools: drawing tools and their shared settings (still no SDL/ImGui) ----------------------
add_library(trace_tools STATIC
    src/tools/toolbox.cpp
    src/tools/freehand_tools.cpp
    src/tools/shape_tools.cpp
    src/tools/click_tools.cpp
    src/tools/select_tool.cpp
    src/tools/text_tool.cpp
)
target_include_directories(trace_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(trace_tools PUBLIC trace_core PRIVATE trace_options)

# --- trace_flow: flowchart model, history and geometry (pure C++, no SDL/ImGui) ---------------------
add_library(trace_flow STATIC
    src/flow/flow_document.cpp
    src/flow/flow_history.cpp
    src/flow/flow_geometry.cpp
    src/flow/flow_layout.cpp
    src/flow/flow_templates.cpp
    src/flow/flow_serialize.cpp
    src/flow/flow_svg.cpp
)
target_include_directories(trace_flow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(trace_flow PUBLIC trace_core PRIVATE trace_options)

# --- trace_io: image file formats (stb_image / stb_image_write / vendored msf_gif), project files ------------------
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found; run the configure step through the vcpkg toolchain")
endif()
add_library(trace_io STATIC
    src/io/image_io.cpp
    src/io/image_extra.cpp
    src/io/file_write.cpp
    src/io/project_io.cpp
    src/io/pixel_project.cpp
    src/io/board_project.cpp
    src/io/pixel_export.cpp
    src/io/flow_project.cpp
    src/io/codec_impl.cpp
    src/io/system_fonts.cpp
)
target_include_directories(trace_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(trace_io SYSTEM PRIVATE ${STB_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/msf_gif)
target_link_libraries(trace_io PUBLIC trace_core trace_flow PRIVATE trace_options)

# --- trace_platform: OS-specific services ----------------------------------------------------------
if(WIN32)
    set(TRACE_SYSSTATS_SOURCE src/platform/sysstats_win.cpp)
elseif(APPLE)
    set(TRACE_SYSSTATS_SOURCE src/platform/sysstats_mac.cpp)
else()
    set(TRACE_SYSSTATS_SOURCE src/platform/sysstats_linux.cpp)
endif()

add_library(trace_platform STATIC src/platform/sysstats.cpp ${TRACE_SYSSTATS_SOURCE})
target_include_directories(trace_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(trace_platform PRIVATE trace_options)
if(WIN32)
    target_compile_definitions(trace_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(trace_platform PRIVATE psapi)
endif()

# --- Embedded fonts: third_party/fonts/*.ttf become C arrays in the build directory -----------------
function(trace_embed_font input symbol out_var)
    file(READ "${input}" hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR byte_count "${hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],){32}" "\\0\n" bytes "${bytes}")
    set(${out_var} "extern const unsigned char k${symbol}FontData[] = {\n${bytes}\n};\nextern const unsigned long long k${symbol}FontSize = ${byte_count}ULL;\n" PARENT_SCOPE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endfunction()

trace_embed_font("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/Inter.ttf" Inter inter_source)
trace_embed_font("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-Regular.ttf" Mono mono_source)
set(embedded_fonts_tmp "${CMAKE_BINARY_DIR}/generated/embedded_fonts.cpp.tmp")
set(embedded_fonts_cpp "${CMAKE_BINARY_DIR}/generated/embedded_fonts.cpp")
file(WRITE "${embedded_fonts_tmp}" "// Generated by CMake from third_party/fonts. Do not edit.\n${inter_source}${mono_source}")
configure_file("${embedded_fonts_tmp}" "${embedded_fonts_cpp}" COPYONLY) # only touched when the content changed

# --- Embedded icons: src/assets/icons/*.svg become byte arrays and an `Icon` enum in the build directory ------
# The enum names are the file names in CamelCase (pencil_tool.svg -> Icon::PencilTool). Adding, renaming or editing
# an SVG reconfigures the build, which regenerates both files.
file(GLOB trace_icon_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/icons/*.svg")
list(SORT trace_icon_files)
set(icon_enum "")
set(icon_data "")
set(icon_table "")
foreach(icon_file IN LISTS trace_icon_files)
    get_filename_component(icon_name "${icon_file}" NAME_WE)
    string(REGEX REPLACE "[^A-Za-z0-9]+" ";" icon_words "${icon_name}")
    set(icon_id "")
    foreach(word IN LISTS icon_words)
        string(SUBSTRING "${word}" 0 1 head)
        string(TOUPPER "${head}" head)
        string(SUBSTRING "${word}" 1 -1 tail)
        string(APPEND icon_id "${head}${tail}")
    endforeach()
    file(READ "${icon_file}" icon_hex HEX)
    string(LENGTH "${icon_hex}" icon_hex_length)
    math(EXPR icon_bytes "${icon_hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," icon_hex "${icon_hex}")
    string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],){32}" "\\0\n" icon_hex "${icon_hex}")
    string(APPEND icon_enum "    ${icon_id},\n")
    string(APPEND icon_data "static const unsigned char kBytes${icon_id}[] = {\n${icon_hex}\n};\n")
    string(APPEND icon_table "    {\"${icon_name}\", kBytes${icon_id}, ${icon_bytes}u},\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${icon_file}")
endforeach()
set(icon_ids_tmp "${CMAKE_BINARY_DIR}/generated/svg_icon_ids.hpp.tmp")
set(icon_ids_hpp "${CMAKE_BINARY_DIR}/generated/svg_icon_ids.hpp")
file(WRITE "${icon_ids_tmp}" "// Generated by CMake from src/assets/icons. Do not edit.\n#pragma once\n\nnamespace trace {\n\n// None draws nothing. The others are the SVG files, in file name order.\nenum class Icon : unsigned short {\n    None,\n${icon_enum}    Count\n};\n\n} // namespace trace\n")
configure_file("${icon_ids_tmp}" "${icon_ids_hpp}" COPYONLY)
set(icon_src_tmp "${CMAKE_BINARY_DIR}/generated/embedded_icons.cpp.tmp")
set(icon_src_cpp "${CMAKE_BINARY_DIR}/generated/embedded_icons.cpp")
file(WRITE "${icon_src_tmp}" "// Generated by CMake from src/assets/icons. Do not edit.\n#include \"app/svg_icons.hpp\"\n\nnamespace trace {\nnamespace {\n${icon_data}} // namespace\n\nconst SvgIconSource kSvgIconSources[] = {\n${icon_table}};\n\n} // namespace trace\n")
configure_file("${icon_src_tmp}" "${icon_src_cpp}" COPYONLY)

# --- Embedded app icon: src/assets/icons/app_icon.png, decoded at start-up for the window icon -----------------
set(app_icon_png "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/icons/app_icon.png")
file(READ "${app_icon_png}" app_icon_hex HEX)
string(LENGTH "${app_icon_hex}" app_icon_hex_length)
math(EXPR app_icon_bytes "${app_icon_hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," app_icon_hex "${app_icon_hex}")
string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],){32}" "\\0\n" app_icon_hex "${app_icon_hex}")
set(app_icon_tmp "${CMAKE_BINARY_DIR}/generated/embedded_app_icon.cpp.tmp")
set(app_icon_cpp "${CMAKE_BINARY_DIR}/generated/embedded_app_icon.cpp")
file(WRITE "${app_icon_tmp}" "// Generated by CMake from src/assets/icons/app_icon.png. Do not edit.\nextern const unsigned char kAppIconPngData[] = {\n${app_icon_hex}\n};\nextern const unsigned long long kAppIconPngSize = ${app_icon_bytes}ULL;\n")
configure_file("${app_icon_tmp}" "${app_icon_cpp}" COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${app_icon_png}")

# --- trace: the application ------------------------------------------------------------------------
find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(trace
    src/app/main.cpp
    src/app/app.cpp
    src/app/theme.cpp
    src/app/canvas_view.cpp
    src/app/texture_mirror.cpp
    src/app/file_session.cpp
    src/app/paint_file_adapter.cpp
    src/app/image_file_formats.cpp
    src/app/paint_workspace.cpp
    src/app/pixel_workspace.cpp
    src/app/pixel_file_adapter.cpp
    src/app/pixel_panels.cpp
    src/app/palette_panel.cpp
    src/app/font_library.cpp
    src/app/timeline_panel.cpp
    src/app/pixel_export_dialog.cpp
    src/app/board_export_dialog.cpp
    src/app/raster_common.cpp
    src/app/flow_view.cpp
    src/app/flow_panels.cpp
    src/app/flow_workspace.cpp
    src/app/flow_file_adapter.cpp
    src/app/flow_export.cpp
    src/app/image_dialogs.cpp
    src/app/modal_ui.cpp
    src/app/toasts.cpp
    src/app/pattern_draw.cpp
    src/app/tool_icons.cpp
    src/app/svg_icons.cpp
    src/app/icon_widgets.cpp
    src/app/panel_ui.cpp
    src/app/app_icon.cpp
    ${app_icon_cpp}
    ${icon_src_cpp}
    src/app/native_dialogs.cpp
    src/app/fonts.cpp
    src/app/languages.cpp
    src/app/theme_io.cpp
    ${embedded_fonts_cpp}
    src/app/clipboard.cpp
    src/app/tools_panel.cpp
    src/app/symmetry_panel.cpp
    src/app/keybinds.cpp
    src/app/settings.cpp
    src/app/settings_window.cpp
)
if(WIN32)
    # The exe icon (Explorer, taskbar, title bar): trace.ico is generated from app_icon.png by make_ico.py.
    enable_language(RC)
    target_sources(trace PRIVATE src/platform/windows/trace.rc)
    set_source_files_properties(src/platform/windows/trace.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/trace.ico")
endif()
set_target_properties(trace PROPERTIES
    OUTPUT_NAME Trace
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
target_compile_definitions(trace PRIVATE TRACE_VERSION="${PROJECT_VERSION}")
target_include_directories(trace PRIVATE ${CMAKE_BINARY_DIR}/generated)
# Language packs that ship with the program are looked up in a "languages" folder next to the executable.
add_custom_command(TARGET trace POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/languages" "$<TARGET_FILE_DIR:trace>/languages")
target_link_libraries(trace PRIVATE trace_options trace_core trace_tools trace_io trace_flow trace_platform SDL3::SDL3 imgui::imgui)

# --- Tests -----------------------------------------------------------------------------------------
enable_testing()
if(TRACE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
