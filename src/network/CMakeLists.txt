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
set(net_sources)
list(APPEND net_sources network.c net_pcap.c net_slirp.c net_dp8390.c net_3c501.c
    net_3c503.c net_ne2000.c net_pcnet.c net_wd8003.c net_plip.c net_event.c net_null.c
    net_eeprom_nmc93cxx.c net_tulip.c net_rtl8139.c net_l80225.c net_modem.c utils/getline.c)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SLIRP REQUIRED IMPORTED_TARGET slirp)
target_link_libraries(86Box PkgConfig::SLIRP)

if(WIN32)
    target_link_libraries(PkgConfig::SLIRP INTERFACE wsock32 ws2_32 iphlpapi iconv)
    if(STATIC_BUILD)
        add_compile_definitions(LIBSLIRP_STATIC)
    endif()
endif()

if (HAIKU)
    target_link_libraries(86Box network)
endif()

if(WIN32)
    target_link_libraries(86Box ws2_32)
endif()

if (UNIX)
    find_path(HAS_VDE "libvdeplug.h" PATHS ${VDE_INCLUDE_DIR} "/usr/include /usr/local/include" "/opt/homebrew/include" )
    if(HAS_VDE)
        find_library(VDE_LIB vdeplug)
        if (NOT VDE_LIB)
            message(WARNING "Could not find VDE. The library will not be bundled and any related features will be disabled.")
        else()
            add_compile_definitions(HAS_VDE)
            list(APPEND net_sources net_vde.c)
        endif()
    endif()
endif()

add_library(net OBJECT ${net_sources})
