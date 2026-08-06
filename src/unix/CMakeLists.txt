#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: Cacodemon345
#          David Hrdlička, <hrdlickadavid@outlook.com>
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2021      Cacodemon345.
#          Copyright 2021      David Hrdlička.
#          Copyright 2021      Andreas J. Reichel.
#          Copyright 2021-2024 Jasmine Iwanek.
#

add_library(plat OBJECT
    sdl_main.c
    sdl_plat.c
    ../qt/sdl_joystick.c
)

if(WIN32)
    target_sources(plat PRIVATE
        ../qt/win_netsocket.c
        ../qt/win_dynld.c
        sdl_plat_win.c
    )
    target_link_libraries(86Box ws2_32)
else()
    target_sources(plat PRIVATE
        sdl_plat_unix.c
        unix_netsocket.c
        unix_dynld.c
    )
endif()

if (NOT CPPTHREADS)
    if (NOT WIN32)
        set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(86Box Threads::Threads)

    if (WIN32)
        target_sources(plat PRIVATE ../qt/win_thread.c)
    else()
        target_sources(plat PRIVATE unix_thread.c)
    endif()
endif()

if(SDL2)
    find_package(SDL2 REQUIRED)
    add_compile_definitions(USE_SDL2_LIB)
    include_directories(${SDL2_INCLUDE_DIRS})
    if(STATIC_BUILD AND TARGET SDL2::SDL2-static)
        target_link_libraries(86Box SDL2::SDL2-static)
    elseif(TARGET SDL3::SDL3)
        target_link_libraries(86Box SDL2::SDL2)
    else()
        target_link_libraries(86Box ${SDL2_LIBRARIES})
    endif()
else()
    find_package(SDL3 REQUIRED)
    include_directories(${SDL3_INCLUDE_DIRS})
    target_link_libraries(86Box SDL3::Headers)
    if(STATIC_BUILD AND TARGET SDL3::SDL3-static)
        target_link_libraries(86Box SDL3::SDL3-static)
    elseif(TARGET SDL3::SDL3)
        target_link_libraries(86Box SDL3::SDL3)
    else()
        target_link_libraries(86Box ${SDL3_LIBRARIES})
    endif()
endif()

option(USE_SDL_SHADER "Build SDL GLES2 shader pipeline" OFF)

add_library(ui OBJECT
    sdl_render.c
    sdl_cdrom.c
    sdl_monitor.c
    sdl_ui.c
)

if(USE_SDL_SHADER)
    target_compile_definitions(ui PRIVATE USE_SDL_SHADER_PIPELINE)

    find_library(GLESV2_LIBRARY GLESv2)
    find_path(GLESV2_INCLUDE_DIR GLES2/gl2.h)

    if(NOT GLESV2_LIBRARY OR NOT GLESV2_INCLUDE_DIR)
        message(FATAL_ERROR "USE_SDL_SHADER=ON requires GLESv2 and GLES2/gl2.h; use -DUSE_SDL_SHADER=OFF to build without the shader pipeline")
    endif()

    target_sources(ui PRIVATE sdl_shader.c)

    target_link_libraries(86Box ${GLESV2_LIBRARY})
    target_include_directories(ui PRIVATE ${GLESV2_INCLUDE_DIR})
endif()

target_sources(ui PRIVATE sdl_osd.cpp)

# Build the SDL ImGui backend separately so AUTOMOC skips it.
set(OSD_DIR ${CMAKE_SOURCE_DIR}/src/osd)
if(SDL2)
    add_library(osd_backend OBJECT
        ${OSD_DIR}/imgui/imgui_impl_sdl2.cpp
    )
else()
    add_library(osd_backend OBJECT
        ${OSD_DIR}/imgui/imgui_impl_sdl3.cpp
    )
endif()

if(USE_SDL_SHADER)
    target_sources(osd_backend PRIVATE ${OSD_DIR}/imgui/imgui_impl_opengl3.cpp)
    target_compile_definitions(osd_backend PRIVATE IMGUI_IMPL_OPENGL_ES2)
else()
    if(SDL2)
        target_sources(osd_backend PRIVATE ${OSD_DIR}/imgui/imgui_impl_sdlrenderer2.cpp)
    else()
        target_sources(osd_backend PRIVATE ${OSD_DIR}/imgui/imgui_impl_sdlrenderer3.cpp)
    endif()
endif()

target_include_directories(osd_backend PRIVATE ${OSD_DIR}/imgui)
target_link_libraries(86Box osd_backend)

target_include_directories(ui PRIVATE ${OSD_DIR} ${OSD_DIR}/imgui)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(plat PRIVATE linux_cdrom_ioctl.c)
    target_sources(plat PRIVATE dummy_floppy_ioctl.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(plat PRIVATE dummy_cdrom_ioctl.c)
    target_sources(plat PRIVATE unix_floppy_ioctl.c)
elseif(WIN32)
    target_sources(plat PRIVATE ../qt/win_cdrom_ioctl.c)
    target_sources(plat PRIVATE dummy_floppy_ioctl.c)
else()
    target_sources(plat PRIVATE dummy_cdrom_ioctl.c)
    target_sources(plat PRIVATE dummy_floppy_ioctl.c)
endif()

target_compile_definitions(plat PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(plat ${CMAKE_DL_LIBS})

if(APPLE)
    target_sources(plat PRIVATE macOSXGlue.m)
endif()

if(WIN32)
    enable_language(RC)
    target_sources(86Box PUBLIC ../qt/86Box-qt.rc)

    # CMake 3.22 messed this up for clang/clang++
    # See https://gitlab.kitware.com/cmake/cmake/-/issues/22611
    if(MSVC OR (NOT MINGW AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22))
        # MSVC linker adds its own manifest to the executable, which fails if
        # we include ours in 86Box.rc. We therefore need to pass the manifest
        # directly as as a source file, so the linker can use that instead.
        set_property(SOURCE ../qt/86Box-qt.rc DIRECTORY .. PROPERTY COMPILE_DEFINITIONS NO_INCLUDE_MANIFEST)
        target_sources(86Box PRIVATE ../qt/86Box.manifest)
    endif()

    if (MINGW)
        add_compile_definitions(NTDDI_VERSION=0x06010000)
    endif()
endif()
