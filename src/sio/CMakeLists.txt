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

add_library(sio OBJECT
    sio_82091aa.c
    sio_90c50.c
    sio_acc3221.c
    sio_ali5123.c
    sio_cbm_io.c
    sio_gm82c803ab.c
    sio_gm82c803c.c
    sio_f82c606.c
    sio_f82c710.c
    sio_fdc37c6xx.c
    sio_fdc37c67x.c
    sio_fdc37c669.c
    sio_fdc37c93x.c
    sio_fdc37m60x.c
    sio_it86x1f.c
    sio_p82c604.c
    sio_pc87310.c
    sio_pc873xx.c
    sio_pc87306.c
    sio_pc87307.c
    sio_pc87309.c
    sio_w837x7.c
    sio_w83877.c
    sio_w83977.c
    sio_um866x.c
    sio_um8669f.c
    sio_vl82c113.c
    sio_vt82c686.c
)

if(SIO_DETECT)
    target_sources(sio PRIVATE sio_detect.c)
endif()
