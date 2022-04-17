#
# 86Box     A hypervisor and IBM PC system emulator that specializes in
#           running old operating systems and software designed for IBM
#           PC systems and compatibles from 1981 through fairly recent
#           system designs based on the PCI bus.
#
#           This file is part of the 86Box distribution.
#
#           CMake build script.
#
# Authors:  David Hrdlička, <hrdlickadavid@outlook.com>
#
#           Copyright 2020,2021 David Hrdlička.
#

enable_language(RC)

add_library(plat OBJECT win.c win_dynld.c win_cdrom.c win_keyboard.c
    win_mouse.c)

add_library(ui OBJECT win_ui.c win_icon.c win_stbar.c win_sdl.c win_dialog.c win_about.c
    win_settings.c win_devconf.c win_snd_gain.c win_specify_dim.c win_new_floppy.c
    win_jsconf.c win_media_menu.c win_preferences.c glad.c win_opengl.c
    win_opengl_glslp.c win_toolbar.c 86Box.rc)

if(NOT CPPTHREADS)
    target_sources(plat PRIVATE win_thread.c)
endif()

if(RTMIDI)
    target_compile_definitions(ui PRIVATE USE_RTMIDI)
endif()

# CMake 3.22 messed this up for clang/clang++
# See https://gitlab.kitware.com/cmake/cmake/-/issues/23066
if(MSVC OR (NOT MINGW AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22))
    # MSVC linker adds its own manifest to the executable, which fails if
    # we include ours in 86Box.rc. We therefore need to pass the manifest
    # directly as as a source file, so the linker can use that instead.
    set_property(SOURCE 86Box.rc PROPERTY COMPILE_DEFINITIONS NO_INCLUDE_MANIFEST)
    target_sources(86Box PRIVATE 86Box.manifest)
endif()

if(NOT MINGW)
    # Append null to resource strings (fixes file dialogs)
    set_property(SOURCE 86Box.rc PROPERTY COMPILE_FLAGS -n)

    # `opendir` is only included in MinGW, so include an implementation
    # for other builds.
    target_sources(plat PRIVATE win_opendir.c)
endif()

if(DINPUT)
    target_sources(plat PRIVATE win_joystick.cpp)
    target_link_libraries(86Box dinput8)
else()
    target_sources(plat PRIVATE win_joystick_rawinput.c)
endif()

target_link_libraries(86Box advapi32 comctl32 comdlg32 gdi32 shell32 iphlpapi
    dxguid imm32 hid setupapi uxtheme version winmm psapi ws2_32)
