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

find_package(PkgConfig REQUIRED)

pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)

add_library(cdrom OBJECT cdrom.c cdrom_image_backend.c cdrom_image_viso.c cdrom_image.c cdrom_ioctl.c)
target_link_libraries(86Box PkgConfig::SNDFILE)

if(CDROM_MITSUMI)
    target_compile_definitions(cdrom PRIVATE USE_CDROM_MITSUMI)
    target_sources(cdrom PRIVATE cdrom_mitsumi.c)
endif()

if (WIN32)
    # MSYS2
    target_link_libraries(86Box -static ${SNDFILE_STATIC_LIBRARIES})
endif()
