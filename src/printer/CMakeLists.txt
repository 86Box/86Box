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
#
#          Copyright 2020-2021 David Hrdlička.
#

add_library(print OBJECT png.c prt_cpmap.c prt_escp.c prt_text.c prt_ps.c)

if(PCL)
    target_compile_definitions(print PRIVATE USE_PCL)
endif()

if(APPLE)
    find_library(GHOSTSCRIPT_LIB gs)
    if (NOT GHOSTSCRIPT_LIB)
        message(WARNING "Could not find ghostscript. The library will not be bundled and any related features will not work.")
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
target_link_libraries(86Box PkgConfig::FREETYPE)
if(STATIC_BUILD)
    # if(QT)
        # Qt provides its own version of harfbuzz which leads to duplicated symbols.
        target_link_options(86Box PRIVATE "LINKER:--allow-multiple-definition")
    # endif()
    target_link_libraries(86Box -static ${FREETYPE_STATIC_LIBRARIES})
endif()
