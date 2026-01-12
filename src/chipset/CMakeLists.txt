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

add_library(chipset OBJECT
    82c100.c
    acc2036.c
    acc2168.c
    cs8220.c
    cs8230.c
    ali1429.c
    ali1435.c
    ali1489.c
    ali1531.c
    ali1541.c
    ali1543.c
    ali1621.c
    ali6117.c
    ali1409.c
    compaq.c
    compaq_386.c
    contaq_82c59x.c
    cs4031.c
    grid1520.c
    gc100.c
    headland.c
    ims8848.c
    intel_82335.c
    intel_420ex.c
    intel_4x0.c
    intel_i450kx.c
    intel_sio.c
    intel_piix.c
    isa486c.c
    ../ioapic.c
    laserxt.c
    neat.c
    olivetti_eva.c
    opti283.c
    opti291.c
    opti391.c
    opti495.c
    opti498.c
    opti499.c
    opti602.c
    opti822.c
    opti895.c
    opti5x7.c
    philips.c
    sanyo.c
    scamp.c
    scat.c
    sis_85c310.c
    sis_85c4xx.c
    sis_85c496.c
    sis_85c50x.c
    sis_5511.c
    sis_5571.c
    sis_5581.c
    sis_5591.c
    sis_5600.c
    sis_5511_h2p.c
    sis_5571_h2p.c
    sis_5581_h2p.c
    sis_5591_h2p.c
    sis_5600_h2p.c
    sis_5513_p2i.c
    sis_5513_ide.c
    sis_5572_usb.c
    sis_5595_pmu.c
    sis_55xx.c
    sl82c461.c
    stpc.c
    via_vt82c49x.c
    via_vt82c505.c
    umc_8886.c
    umc_hb4.c
    umc_8890.c
    via_apollo.c
    via_pipc.c
    vl82c480.c
    vl82c59x.c
    wd76c10.c
)
