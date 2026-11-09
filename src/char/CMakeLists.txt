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
#          Copyright 2021-2026 Jasmine Iwanek.
#

add_library(char OBJECT
    char.c
    char_file.c
    char_fujinet.c
    char_loopback.c
    char_modem.c
    char_pipe.c
    char_serial.c
    char_stdio.c
)

if(AUTOMATION)
    target_sources(char PRIVATE char_automation.c)
endif()

if(ENABLE_MODEM_ADVANCED_CONFIG)
    target_compile_definitions(char PRIVATE ENABLE_MODEM_ADVANCED_CONFIG)
endif()

if(ENABLE_MODEM_IPV6)
    target_compile_definitions(char PRIVATE ENABLE_MODEM_IPV6)
endif()

if(ENABLE_VSERVER_WINS OR ENABLE_VSERVER_SMB OR ENABLE_VSERVER_NETBEUI OR ENABLE_VSERVER_IPX OR ENABLE_VSERVER_PPTP
   OR ENABLE_VSERVER_FTP OR ENABLE_VSERVER_TFTP)
    target_compile_definitions(char PRIVATE ENABLE_VSERVER)
endif()

if(ENABLE_VSERVER_WINS OR ENABLE_VSERVER_SMB OR ENABLE_VSERVER_NETBEUI OR ENABLE_VSERVER_IPX OR ENABLE_VSERVER_PPTP)
    target_compile_definitions(char PRIVATE ENABLE_VSERVER_WINS)
endif()

if(ENABLE_VSERVER_SMB OR ENABLE_VSERVER_NETBEUI OR ENABLE_VSERVER_IPX OR ENABLE_VSERVER_PPTP)
    target_compile_definitions(char PRIVATE ENABLE_VSERVER_SMB)
endif()

if(ENABLE_VSERVER_NETBEUI)
    target_compile_definitions(char PRIVATE ENABLE_VSERVER_NETBEUI)
endif()

if(ENABLE_VSERVER_IPX)
    target_compile_definitions(char PRIVATE ENABLE_VSERVER_IPX)
endif()

if(ENABLE_VSERVER_PPTP)
    target_compile_definitions(char PRIVATE ENABLE_VSERVER_PPTP)
endif()

if(ENABLE_VSERVER_FTP)
    target_compile_definitions(char PRIVATE ENABLE_VSERVER_FTP)
endif()

if(ENABLE_VSERVER_TFTP)
    target_compile_definitions(char PRIVATE ENABLE_VSERVER_TFTP)
endif()

if(ENABLE_MODEM_RAW_CAPTURE)
    target_compile_definitions(char PRIVATE ENABLE_MODEM_RAW_CAPTURE)
endif()

if(ENABLE_MODEM_LOG)
    target_compile_definitions(char PRIVATE ENABLE_MODEM_LOG)
endif()

if(ENABLE_MODEM_DEBUG)
    target_compile_definitions(char PRIVATE ENABLE_MODEM_DEBUG)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_include_directories(char PRIVATE /usr/local/include)
endif()

if(CMAKE_WIN32_EXECUTABLE)
    target_compile_definitions(char PRIVATE USE_WIN32_GUI)
endif()

if(NOT WIN32)
    include(CheckIncludeFiles)
    check_include_files("asm/termbits.h;asm/ioctls.h" LINUX_TERMIOS)
    if(LINUX_TERMIOS)
        target_compile_definitions(char PRIVATE USE_LINUX_TERMIOS)
    endif()
endif()
