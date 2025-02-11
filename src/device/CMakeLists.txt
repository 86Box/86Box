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

add_library(dev OBJECT
    access_bus.c
    bugger.c
    cartridge.c
    cassette.c
    clock_ics9xxx.c
    hasp.c
    hwm.c
    hwm_gl518sm.c
    hwm_lm75.c
    hwm_lm78.c
    hwm_vt82c686.c
    i2c.c
    i2c_gpio.c
    ibm_5161.c
    isamem.c
    isartc.c
    isapnp.c
    kbc_at.c
    kbc_at_dev.c
    keyboard.c
    keyboard_at.c
    keyboard_xt.c
    ../lpt.c
    mouse.c
    mouse_bus.c
    mouse_microtouch_touchscreen.c
    mouse_ps2.c
    mouse_serial.c
    nec_mate_unk.c
    novell_cardkey.c
    pci_bridge.c
    phoenix_486_jumper.c
    postcard.c
    serial.c
    serial_passthrough.c
    smbus_ali7101.c
    smbus_piix4.c
    smbus_sis5595.c
    unittester.c
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
