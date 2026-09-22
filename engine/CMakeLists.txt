cmake_minimum_required(VERSION 3.20)
project(gitar_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GITAR_BUILD_TESTS "Build the gitar engine tests" ON)
option(GITAR_WERROR "Treat warnings as errors in gitar code" ON)

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
target_link_libraries(gitar_engine PRIVATE gitar_json Threads::Threads)
gitar_set_warnings(gitar_engine)

add_executable(gitar-engine src/main.cpp)
target_link_libraries(gitar-engine PRIVATE gitar_engine)
gitar_set_warnings(gitar-engine)

if(GITAR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
