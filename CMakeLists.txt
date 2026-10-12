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
)
target_include_directories(trace_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
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
)
target_include_directories(trace_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(trace_tools PUBLIC trace_core PRIVATE trace_options)

# --- trace_io: image file formats (stb_image / stb_image_write / vendored msf_gif), project files ------------------
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found; run the configure step through the vcpkg toolchain")
endif()
add_library(trace_io STATIC
    src/io/image_io.cpp
    src/io/file_write.cpp
    src/io/project_io.cpp
    src/io/pixel_project.cpp
    src/io/codec_impl.cpp
)
target_include_directories(trace_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(trace_io SYSTEM PRIVATE ${STB_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/msf_gif)
target_link_libraries(trace_io PUBLIC trace_core PRIVATE trace_options)

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
    src/app/paint_workspace.cpp
    src/app/pixel_workspace.cpp
    src/app/pixel_file_adapter.cpp
    src/app/pixel_panels.cpp
    src/app/raster_common.cpp
    src/app/placeholder_workspace.cpp
    src/app/image_dialogs.cpp
    src/app/toasts.cpp
    src/app/pattern_draw.cpp
    src/app/tool_icons.cpp
    src/app/native_dialogs.cpp
    src/app/fonts.cpp
    src/app/languages.cpp
    src/app/theme_io.cpp
    ${embedded_fonts_cpp}
    src/app/clipboard.cpp
    src/app/tools_panel.cpp
    src/app/keybinds.cpp
    src/app/settings.cpp
    src/app/settings_window.cpp
)
set_target_properties(trace PROPERTIES
    OUTPUT_NAME Trace
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
target_compile_definitions(trace PRIVATE TRACE_VERSION="${PROJECT_VERSION}")
# Language packs that ship with the program are looked up in a "languages" folder next to the executable.
add_custom_command(TARGET trace POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/languages" "$<TARGET_FILE_DIR:trace>/languages")
target_link_libraries(trace PRIVATE trace_options trace_core trace_tools trace_io trace_platform SDL3::SDL3 imgui::imgui)

# --- Tests -----------------------------------------------------------------------------------------
enable_testing()
if(TRACE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
