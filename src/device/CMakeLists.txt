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

add_library(dev OBJECT
    access_bus.c
    ast_nvr.c
    ast_readout.c
    bugger.c
    cartridge.c
    cassette.c
    clock_ics9xxx.c
    dell_jumper.c
    hasp.c
    hwm.c
    hwm_gl518sm.c
    hwm_lm75.c
    hwm_lm78.c
    hwm_vt82c686.c
    i2c.c
    i2c_gpio.c
    ibm_5161.c
    inboard386.c
    ibm_xma.c
    mcamem.c
    isamem.c
    isarom.c
    isartc.c
    isapnp.c
    kbc_3270pc.c
    kbc_at.c
    kbc_at_dev.c
    kbc_ps2_m25.c
    kbc_xt.c
    keyboard.c
    keyboard_at.c
    keyboard_xt.c
    lpt.c
    lpt_bpck.c
    lpt_ditto.c
    lpt_epat.c
    mouse.c
    mouse_bus.c
    mouse_ibm7690_tablet.c
    mouse_microtouch_touchscreen.c
    mouse_ps2.c
    mouse_serial.c
    mouse_upc.c
    nec_mate_unk.c
    novell_cardkey.c
    pci_bridge.c
    phoenix_486_jumper.c
    postcard.c
    radisys_config.c
    serial_8251.c
    serial.c
    softpower.c
    smbus_ali7101.c
    smbus_piix4.c
    smbus_sis5595.c
    tandy_clkmouse.c
    tulip_jumper.c
    unittester.c
    zenith_scratchpad.c
    lightpen_pxl380.c
)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    target_link_libraries(86Box atomic)
endif()

if(ISAMEM_RAMPAGE)
    target_compile_definitions(dev PRIVATE USE_ISAMEM_RAMPAGE)
endif()

if(ISAMEM_IAB)
    target_compile_definitions(dev PRIVATE USE_ISAMEM_IAB)
endif()

if(ISAMEM_BRAT)
    target_compile_definitions(dev PRIVATE USE_ISAMEM_BRAT)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Linux" AND VFIO AND VFIO_AVAILABLE)
    add_compile_definitions(USE_VFIO)
    target_sources(dev PRIVATE vfio.c)

    # Embed the device preparation script, as not every supported compiler has #embed.
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vfio_setup_sh.h"
        COMMAND "${CMAKE_COMMAND}" "-DIN=${CMAKE_CURRENT_SOURCE_DIR}/vfio-setup.sh"
                "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vfio_setup_sh.h" -DNAME=vfio_setup_sh
                -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake"
        DEPENDS vfio-setup.sh "${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake"
        COMMENT "Embedding vfio-setup.sh")
    target_sources(dev PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/vfio_setup_sh.h")
    target_include_directories(dev PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

    # An installed copy is preferred, as it comes with a polkit policy.
    include(GNUInstallDirs)
    target_compile_definitions(dev PRIVATE "VFIO_SETUP_HELPER=\"${CMAKE_INSTALL_FULL_LIBEXECDIR}/86box/vfio-setup\"")
endif()

if(LASERXT)
    target_compile_definitions(dev PRIVATE USE_LASERXT)
endif()

if(PCL)
    target_compile_definitions(dev PRIVATE USE_PCL)
endif()

if(WACOM)
    target_compile_definitions(dev PRIVATE USE_WACOM)
    target_sources(dev PRIVATE mouse_wacom_tablet.c)
endif()
