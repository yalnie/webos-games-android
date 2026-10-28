cmake_minimum_required(VERSION 3.22.1)
project(mainstream_loader C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

# SDL2
set(SDL_SHARED ON CACHE BOOL "Build shared SDL library" FORCE)
set(SDL_STATIC OFF CACHE BOOL "Build static SDL library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL tests" FORCE)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.28.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)

# SDL2_mixer
set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_VENDORED ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL2_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG release-2.8.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2_mixer)

# libpng
set(PNG_SHARED ON CACHE BOOL "" FORCE)
set(PNG_STATIC OFF CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libpng
    GIT_REPOSITORY https://github.com/glennrp/libpng.git
    GIT_TAG v1.6.40
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libpng)

# Loader library
add_library(mainstream_loader SHARED
    loader.c
    webos_bridge.c
    cJSON.c
)

target_include_directories(mainstream_loader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_SOURCE_DIR}/include
    ${SDL2_mixer_SOURCE_DIR}/include
    ${libpng_SOURCE_DIR}
    ${libpng_BINARY_DIR}
)

target_link_libraries(mainstream_loader
    SDL2
    SDL2_mixer
    png
    GLESv2
    EGL
    log
    android
    z
    dl
    m
)
