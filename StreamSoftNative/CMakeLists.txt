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

# Packages build/gui/Release into installer/Output/StreamSoftSetup.exe (see
# installer/streamsoft.iss) — a separate opt-in target, not part of ALL,
# since it needs Inno Setup installed (https://jrsoftware.org/isinfo.php)
# and only makes sense after a Release build of streamsoft_gui.
if(WIN32 AND TARGET streamsoft_gui)
    find_program(ISCC_EXE ISCC
        HINTS
            "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
            "C:/Program Files (x86)/Inno Setup 6"
            "C:/Program Files/Inno Setup 6"
    )
    if(ISCC_EXE)
        add_custom_target(installer
            COMMAND "${ISCC_EXE}" "${CMAKE_SOURCE_DIR}/installer/streamsoft.iss"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/installer"
            DEPENDS streamsoft_gui
            COMMENT "Building StreamSoft installer (Inno Setup)"
        )
    else()
        message(STATUS "Inno Setup (ISCC.exe) not found — 'installer' target unavailable. Install from https://jrsoftware.org/isinfo.php")
    endif()
endif()
