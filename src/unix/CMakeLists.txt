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
    if(WIN32)
        target_sources(plat PRIVATE ../qt/win_thread.c)
    else()
        set(THREADS_PREFER_PTHREAD_FLAG TRUE)
        find_package(Threads REQUIRED)
        target_link_libraries(86Box Threads::Threads)

        target_sources(plat PRIVATE unix_thread.c)
    endif()
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
if(STATIC_BUILD AND TARGET SDL2::SDL2-static)
    target_link_libraries(86Box SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    target_link_libraries(86Box SDL2::SDL2)
else()
    target_link_libraries(86Box ${SDL2_LIBRARIES})
endif()

option(USE_SDL_SHADER "Build SDL GLES2 shader pipeline" OFF)
option(USE_IMGUI      "Use ImGui-based OSD"             OFF)

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

if(USE_IMGUI)
    target_compile_definitions(ui PRIVATE USE_IMGUI)
    target_sources(ui PRIVATE sdl_osd.cpp sdl_osd_explorer.cpp)

    add_library(imgui OBJECT
        imgui/imgui.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/imgui_impl_sdl2.cpp
    )

    if(USE_SDL_SHADER)
        target_sources(imgui PRIVATE imgui/imgui_impl_opengl3.cpp)
        target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_ES2)
    else()
        target_sources(imgui PRIVATE imgui/imgui_impl_sdlrenderer2.cpp)
    endif()

    target_include_directories(ui PRIVATE imgui)
    target_link_libraries(86Box imgui)
else()
    target_sources(ui PRIVATE sdl_osd_old.c)
endif()

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
