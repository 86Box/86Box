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
#          Copyright 2024-2026 Jasmine Iwanek.
#
set(net_sources)
list(APPEND net_sources
    network.c
    net_pcap.c
    net_slirp.c
    net_slirp_dhcp.c
    net_switch.c
    net_dp8390.c
    net_lance.c
    net_3c501.c
    net_3c503.c
    net_3c509b.c
    net_3c59x_eisa.c
    net_ne2000.c
    net_ne2100.c
    net_pcnet.c
    net_wd8003.c
    net_pe2.c
    net_pe3.c
    net_plip.c
    net_event.c
    net_null.c
    net_tulip.c
    net_smc_epic100.c
    net_rtl8139.c
    net_dp83815.c
    net_via_rhine.c
    net_i8255x.c
    net_l80225.c
    net_modem.c
    utils/getline.c
)

if(ENABLE_VSERVER_RARP)
    list(APPEND net_sources net_slirp_rarp.c)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SLIRP REQUIRED IMPORTED_TARGET slirp)
target_link_libraries(86Box PkgConfig::SLIRP)

if(WIN32)
    target_link_libraries(PkgConfig::SLIRP INTERFACE wsock32 ws2_32 iphlpapi)
    if (NOT MSVC)
        target_link_libraries(PkgConfig::SLIRP INTERFACE iconv)
    endif()
    if(STATIC_BUILD)
        add_compile_definitions(LIBSLIRP_STATIC)
    endif()
endif()

if (HAIKU)
    target_link_libraries(86Box network)
endif()

if(WIN32)
    target_link_libraries(86Box ws2_32)
    target_link_libraries(86Box bcrypt)
endif()

if (UNIX)
    if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	set_source_files_properties(net_slirp.c PROPERTIES COMPILE_FLAGS "-I/usr/local/include")
    endif()
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
if (UNIX AND NOT APPLE) # Support for TAP on Linux and BSD, supposedly.
    find_path(HAS_TAP "linux/if_tun.h" PATHS ${TAP_INCLUDE_DIR} "/usr/include /usr/local/include" "/opt/homebrew/include" )
    if(HAS_TAP)
        add_compile_definitions(HAS_TAP)
        list(APPEND net_sources net_tap.c)
    else()
        message(WARNING "TAP support not available. Are you on some BSD?")
    endif()
endif()

add_library(net OBJECT ${net_sources})
add_subdirectory(services)
find_package(ZLIB REQUIRED)
target_link_libraries(86Box ZLIB::ZLIB)

if(ENABLE_MODEM_ADVANCED_CONFIG)
    target_compile_definitions(net PRIVATE ENABLE_MODEM_ADVANCED_CONFIG)
endif()

if(ENABLE_MODEM_IPV6)
    target_compile_definitions(net PRIVATE ENABLE_MODEM_IPV6)
endif()

if(ENABLE_VSERVER_WINS OR ENABLE_VSERVER_SMB OR ENABLE_VSERVER_NETBEUI OR ENABLE_VSERVER_IPX OR ENABLE_VSERVER_PPTP
   OR ENABLE_VSERVER_FTP OR ENABLE_VSERVER_TFTP)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER)
endif()

if(ENABLE_VSERVER_WINS OR ENABLE_VSERVER_SMB OR ENABLE_VSERVER_NETBEUI OR ENABLE_VSERVER_IPX OR ENABLE_VSERVER_PPTP)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_WINS)
endif()

if(ENABLE_VSERVER_SMB OR ENABLE_VSERVER_NETBEUI OR ENABLE_VSERVER_IPX OR ENABLE_VSERVER_PPTP)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_SMB)
endif()

if(ENABLE_VSERVER_NETBEUI)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_NETBEUI)
endif()

if(ENABLE_VSERVER_IPX)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_IPX)
endif()

if(ENABLE_VSERVER_PPTP)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_PPTP)
endif()

if(ENABLE_VSERVER_FTP)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_FTP)
endif()

if(ENABLE_VSERVER_TFTP)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_TFTP)
endif()

if(ENABLE_VSERVER_RARP)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_RARP)
endif()

if(ENABLE_VSERVER_BOOTP)
    target_compile_definitions(net PRIVATE ENABLE_VSERVER_BOOTP)
endif()

if(ENABLE_MODEM_RAW_CAPTURE)
    target_compile_definitions(net PRIVATE ENABLE_MODEM_RAW_CAPTURE)
endif()

if(ENABLE_MODEM_LOG)
    target_compile_definitions(net PRIVATE ENABLE_MODEM_LOG)
endif()

if(ENABLE_MODEM_DEBUG)
    target_compile_definitions(net PRIVATE ENABLE_MODEM_DEBUG)
    target_compile_definitions(net PRIVATE ENABLE_SLIRP_LOG=1)
endif()
