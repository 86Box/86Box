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
#
#          Copyright 2021      Cacodemon345.
#          Copyright 2021      David Hrdlička.
#          Copyright 2021      Andreas J. Reichel.
#          Copyright 2021-2022 Jasmine Iwanek.
#

add_library(plat OBJECT unix.c unix_serial_passthrough.c unix_netsocket.c)

if (NOT CPPTHREADS)
    target_sources(plat PRIVATE unix_thread.c)
endif()

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(86Box Threads::Threads)

add_library(ui OBJECT unix_sdl.c unix_cdrom.c dummy_cdrom_ioctl.c)
target_compile_definitions(ui PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(ui ${CMAKE_DL_LIBS})

if(APPLE)
    target_sources(plat PRIVATE macOSXGlue.m)
endif()
