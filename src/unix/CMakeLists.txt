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
    unix.c
    unix_netsocket.c
    ../qt/sdl_joystick.c
)

if (NOT CPPTHREADS)
    target_sources(plat PRIVATE unix_thread.c)
endif()

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(86Box Threads::Threads)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
if(STATIC_BUILD AND TARGET SDL2::SDL2-static)
    target_link_libraries(86Box SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    target_link_libraries(86Box SDL2::SDL2)
else()
    target_link_libraries(86Box ${SDL2_LIBRARIES})
endif()

add_library(ui OBJECT
    unix_sdl.c
    unix_sdl_shader.c
    unix_cdrom.c
)

option(USE_IMGUI "Use ImGui-based OSD (requires GLES2)" OFF)

if(USE_IMGUI)
    target_sources(ui PRIVATE unix_osd_new.cpp)
    target_sources(ui PRIVATE
        ${CMAKE_SOURCE_DIR}/src/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/src/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/src/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/src/imgui/imgui_widgets.cpp
        ${CMAKE_SOURCE_DIR}/src/imgui/imgui_impl_sdl2.cpp
        ${CMAKE_SOURCE_DIR}/src/imgui/imgui_impl_opengl3.cpp
    )
    target_include_directories(ui PRIVATE ${CMAKE_SOURCE_DIR}/src/imgui)
    target_compile_definitions(ui PUBLIC USE_IMGUI)
    target_compile_definitions(ui PRIVATE IMGUI_IMPL_OPENGL_ES2)
    target_compile_definitions(plat PUBLIC USE_IMGUI)
else()
    target_sources(ui PRIVATE unix_osd.c)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(ui PRIVATE linux_cdrom_ioctl.c)
else()
    target_sources(ui PRIVATE dummy_cdrom_ioctl.c)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(ui PRIVATE unix_floppy_ioctl.c)
else()
    target_sources(ui PRIVATE dummy_floppy_ioctl.c)
endif()
target_compile_definitions(ui PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(ui ${CMAKE_DL_LIBS})

find_library(GLESV2_LIBRARY GLESv2)
if(GLESV2_LIBRARY)
    target_link_libraries(86Box ${GLESV2_LIBRARY})
    include(FindPkgConfig)
    pkg_check_modules(GLESV2_PKG glesv2)
    if(GLESV2_PKG_FOUND)
        target_include_directories(ui PRIVATE ${GLESV2_PKG_INCLUDE_DIRS})
    endif()
endif()

if(APPLE)
    target_sources(plat PRIVATE macOSXGlue.m)
endif()
