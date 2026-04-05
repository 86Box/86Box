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
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2021      Andreas J. Reichel.
#          Copyright 2021-2025 Jasmine Iwanek.
#

add_library(char OBJECT
    char.c
    char_loopback.c
    char_file.c
    char_serial.c
    char_stdio.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_include_directories(char PRIVATE /usr/local/include)
endif()

if(CMAKE_WIN32_EXECUTABLE)
    target_compile_definitions(char PRIVATE USE_WIN32_GUI)
endif()

include(CheckIncludeFiles)
check_include_files("asm/termbits.h;asm/ioctls.h" LINUX_TERMIOS)
if(LINUX_TERMIOS)
    target_compile_definitions(char PRIVATE USE_LINUX_TERMIOS)
endif()
