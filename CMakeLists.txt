cmake_minimum_required(VERSION 3.20)

# ============================================================
#  UEOAL - Unreal Engine OpenAL Soft 3D Audio Spatializer
#  GPLv3 - https://github.com/yourusername/UEOAL
# ============================================================

project(UEOAL
    VERSION 1.0.9
    DESCRIPTION "OpenAL Soft-based 3D audio spatializer / XAudio2 proxy for UE4/5 games"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "UEOAL only supports Windows (32-bit and 64-bit).")
endif()

find_package(OpenAL CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/version.h"
    @ONLY
)

set(UEOAL_SOURCES
    src/dllmain.cpp
    src/logger.cpp
    src/audio/openal_backend.cpp
    src/hooks/x3daudio_hook.cpp
    src/proxy/xaudio2_proxy.cpp
    src/proxy/source_voice_proxy.cpp
    src/proxy/submix_voice_proxy.cpp
    src/proxy/mastering_voice_proxy.cpp
)

# ---------------------------------------------------------------------------
#  Helper macro to stamp shared properties on both DLL targets
# ---------------------------------------------------------------------------
macro(ueoal_configure_target TGT OUTPUT_DLL_NAME)
    target_sources(${TGT} PRIVATE ${UEOAL_SOURCES} src/exports.def)

    target_include_directories(${TGT}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_BINARY_DIR}/include"
    )

    target_link_libraries(${TGT}
        PRIVATE
            OpenAL::OpenAL
            minhook::minhook
            xaudio2
            ole32
            uuid
            winmm
    )

    target_compile_definitions(${TGT}
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            UEOAL_VER_MAJOR=${PROJECT_VERSION_MAJOR}
            UEOAL_VER_MINOR=${PROJECT_VERSION_MINOR}
            UEOAL_VER_PATCH=${PROJECT_VERSION_PATCH}
    )

    set_target_properties(${TGT} PROPERTIES
        OUTPUT_NAME "${OUTPUT_DLL_NAME}"
        PREFIX      ""
        SUFFIX      ".dll"
    )
endmacro()

# ---------------------------------------------------------------------------
#  Target 1: XAudio2_9.dll  (Mode A - system XAudio2 / UE5 / newer UE4)
#  Place next to the game .exe; Windows DLL search finds it before System32.
# ---------------------------------------------------------------------------
add_library(UEOAL_XAudio2_9 SHARED)
ueoal_configure_target(UEOAL_XAudio2_9 "XAudio2_9")

# ---------------------------------------------------------------------------
#  Target 2: xaudio2_9redist.dll  (Mode B - XAudio2 redistributable)
#  For UE4 games that ship Engine/Binaries/ThirdParty/.../xaudio2_9redist.dll.
#  Steps: rename the original to xaudio2_9redist_real.dll, then drop this DLL
#  and OpenAL32.dll in the same folder.
# ---------------------------------------------------------------------------
add_library(UEOAL_Redist SHARED)
ueoal_configure_target(UEOAL_Redist "xaudio2_9redist")

# ---------------------------------------------------------------------------
#  Install
# ---------------------------------------------------------------------------
install(TARGETS UEOAL_XAudio2_9 UEOAL_Redist RUNTIME DESTINATION bin)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION .
)

# ---------------------------------------------------------------------------
#  Packaging (CPack)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME              "UEOAL")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR            "UEOAL Project")
set(CPACK_PACKAGE_DESCRIPTION_SHORT "${PROJECT_DESCRIPTION}")
set(CPACK_GENERATOR                 "ZIP")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CPACK_PACKAGE_FILE_NAME "UEOAL-${PROJECT_VERSION}-win64")
else()
    set(CPACK_PACKAGE_FILE_NAME "UEOAL-${PROJECT_VERSION}-win32")
endif()

include(CPack)
