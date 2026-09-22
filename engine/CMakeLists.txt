cmake_minimum_required(VERSION 3.24)
project(gitar_engine LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GITAR_BUILD_TESTS "Build the gitar engine tests" ON)
option(GITAR_WERROR "Treat warnings as errors in gitar code" ON)

if(WIN32)
    # miniaudio pulls in windows.h, whose min/max macros break std::min/std::max
    # and whose bloat we do not need.
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Pinned headers/sources for the neural amp modeler. Both projects are populated
# but their own CMake is never added: SOURCE_SUBDIR points at a directory that
# does not exist, so add_subdirectory() is skipped. Only headers (Eigen) and
# NAM's library sources are consumed.
FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake-here
)
FetchContent_Declare(
    nam
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG 0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842
    GIT_SHALLOW FALSE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR no-cmake-here
)

# Applied per-target so third-party headers never inherit our warning policy.
function(gitar_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(GITAR_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(GITAR_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Vendored headers are exposed as SYSTEM includes so their diagnostics are not
# promoted to errors by GITAR_WERROR.
add_library(gitar_miniaudio INTERFACE)
target_include_directories(gitar_miniaudio SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio")

# `third_party` (not `third_party/doctest`) is the include root so the tests can
# use the canonical `#include "doctest/doctest.h"` path.
add_library(gitar_doctest INTERFACE)
target_include_directories(gitar_doctest SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# `third_party` is the include root so the control server uses
# `#include "nlohmann/json.hpp"`.
add_library(gitar_json INTERFACE)
target_include_directories(gitar_json SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

find_package(Threads REQUIRED)

FetchContent_MakeAvailable(eigen nam)

# Third-party neural amp modeler core. Eigen and NAM are both header/source
# libraries here, so their warnings are disabled wholesale and they are never
# subject to GITAR_WERROR.
file(GLOB GITAR_NAM_SOURCES
    "${nam_SOURCE_DIR}/NAM/*.cpp"
    "${nam_SOURCE_DIR}/NAM/*/*.cpp")

add_library(gitar_nam STATIC ${GITAR_NAM_SOURCES})
set_target_properties(gitar_nam PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
# NAM defaults NAM_SAMPLE to double; the engine's public API is float, so force
# the float instantiation. NAM_ENABLE_A2_FAST mirrors the upstream build default.
target_compile_definitions(gitar_nam PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
target_include_directories(gitar_nam SYSTEM PUBLIC
    "${nam_SOURCE_DIR}"
    "${nam_SOURCE_DIR}/NAM"
    "${eigen_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann")
if(MSVC)
    target_compile_options(gitar_nam PRIVATE /w)
else()
    target_compile_options(gitar_nam PRIVATE -w)
endif()
target_link_libraries(gitar_nam PUBLIC Threads::Threads)

set(GITAR_AUDIO_BACKEND_LIBS "")

# miniaudio probes every backend it was built for at compile time. Disable the
# ones pkg-config cannot find so a missing dev package never breaks the build,
# and link the ones that are present.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ALSA IMPORTED_TARGET alsa)
        if(ALSA_FOUND)
            list(APPEND GITAR_AUDIO_BACKEND_LIBS PkgConfig::ALSA)
        else()
            target_compile_definitions(gitar_miniaudio INTERFACE MA_NO_ALSA)
        endif()

        pkg_check_modules(PULSEAUDIO IMPORTED_TARGET libpulse)
        if(PULSEAUDIO_FOUND)
            list(APPEND GITAR_AUDIO_BACKEND_LIBS PkgConfig::PULSEAUDIO)
        else()
            target_compile_definitions(gitar_miniaudio INTERFACE MA_NO_PULSEAUDIO)
        endif()

        pkg_check_modules(JACK IMPORTED_TARGET jack)
        if(JACK_FOUND)
            list(APPEND GITAR_AUDIO_BACKEND_LIBS PkgConfig::JACK)
        else()
            target_compile_definitions(gitar_miniaudio INTERFACE MA_NO_JACK)
        endif()
    else()
        target_compile_definitions(gitar_miniaudio INTERFACE
            MA_NO_ALSA MA_NO_PULSEAUDIO MA_NO_JACK)
    endif()
endif()

file(GLOB GITAR_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# main.cpp defines the executable entry point, so it must not be part of the
# static library (the test binary supplies its own main).
list(REMOVE_ITEM GITAR_ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(gitar_engine STATIC ${GITAR_ENGINE_SOURCES})
target_include_directories(gitar_engine PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
target_link_libraries(gitar_engine PUBLIC gitar_miniaudio ${GITAR_AUDIO_BACKEND_LIBS})
# NAM registers its architecture parsers from static initializers inside the
# archive; without whole-archive the linker drops those objects and model
# loading fails with "No config parser registered". WHOLE_ARCHIVE keeps them.
target_link_libraries(gitar_engine PRIVATE gitar_json Threads::Threads
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,gitar_nam>")
if(WIN32)
    # The control server uses WinSock.
    target_link_libraries(gitar_engine PRIVATE ws2_32)
endif()
gitar_set_warnings(gitar_engine)

add_executable(gitar-engine src/main.cpp)
target_link_libraries(gitar-engine PRIVATE gitar_engine)
gitar_set_warnings(gitar-engine)

if(GITAR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
