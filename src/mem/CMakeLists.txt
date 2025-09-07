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
#          Copyright 2024      Jasmine Iwanek.
#

add_library(mem OBJECT
    catalyst_flash.c
    i2c_eeprom.c
    intel_flash.c
    mem.c
    mmu_2386.c
    nmc93cxx.c
    rom.c
    row.c
    smram.c
    spd.c
    sst_flash.c
)
