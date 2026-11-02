# Assembles a self-contained folder from a finished build:
#
#   cmake -DBUILD_DIR=build/windows-msvc-release -DOUT_DIR=Trace-portable -P cmake/PackagePortable.cmake
#
# The folder holds the program, the shared libraries it loads, the shipped language packs and a "portable.txt"
# marker; with the marker, Trace keeps its settings and window layout in a "data" folder beside the program
# instead of the user profile. A macOS app bundle is copied as it is (it has no portable mode).
cmake_minimum_required(VERSION 3.25)

if(NOT BUILD_DIR OR NOT OUT_DIR)
    message(FATAL_ERROR "Usage: cmake -DBUILD_DIR=<build folder> -DOUT_DIR=<folder to create> -P PackagePortable.cmake")
endif()
get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

if(EXISTS "${BUILD_DIR}/Trace.app")
    file(COPY "${BUILD_DIR}/Trace.app" DESTINATION "${OUT_DIR}" USE_SOURCE_PERMISSIONS)
    set(PORTABLE FALSE)
elseif(EXISTS "${BUILD_DIR}/Trace.exe")
    file(COPY "${BUILD_DIR}/Trace.exe" DESTINATION "${OUT_DIR}")
    set(PORTABLE TRUE)
elseif(EXISTS "${BUILD_DIR}/Trace")
    file(COPY "${BUILD_DIR}/Trace" DESTINATION "${OUT_DIR}" USE_SOURCE_PERMISSIONS)
    set(PORTABLE TRUE)
else()
    message(FATAL_ERROR "No Trace program in ${BUILD_DIR}; build it first.")
endif()

if(PORTABLE)
    # Libraries the program loads from its own folder (SDL3.dll on Windows, shared objects elsewhere).
    file(GLOB libraries "${BUILD_DIR}/*.dll" "${BUILD_DIR}/*.so" "${BUILD_DIR}/*.so.*" "${BUILD_DIR}/*.dylib")
    if(libraries)
        file(COPY ${libraries} DESTINATION "${OUT_DIR}" USE_SOURCE_PERMISSIONS)
    endif()
    file(COPY "${SOURCE_DIR}/languages" DESTINATION "${OUT_DIR}")
    file(WRITE "${OUT_DIR}/portable.txt"
        "Portable mode.\n"
        "\n"
        "While this file sits next to Trace, the settings, the window layout and imported languages are kept in the\n"
        "\"data\" folder beside it instead of your user profile. Move or copy this whole folder anywhere (a USB\n"
        "stick, for example) and your setup goes with it. Delete this file to use the normal user profile again.\n")
endif()

if(EXISTS "${SOURCE_DIR}/LICENSE")
    file(COPY "${SOURCE_DIR}/LICENSE" DESTINATION "${OUT_DIR}")
endif()
message(STATUS "Packaged ${OUT_DIR}")
