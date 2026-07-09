cmake_minimum_required(VERSION 3.20)

if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# Prebuilt Qt (fetched via aqtinstall, see docs/BUILD.md) — not vcpkg, building
# Qt from source would take hours.
if(EXISTS "${CMAKE_SOURCE_DIR}/qt/6.8.3/msvc2022_64")
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/qt/6.8.3/msvc2022_64")
endif()

project(StreamSoftNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(core)

if(EXISTS "${CMAKE_SOURCE_DIR}/qt/6.8.3/msvc2022_64")
    add_subdirectory(gui)
endif()
