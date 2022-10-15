#
# 86Box     A hypervisor and IBM PC system emulator that specializes in
#           running old operating systems and software designed for IBM
#           PC systems and compatibles from 1981 through fairly recent
#           system designs based on the PCI bus.
#
#           This file is part of the 86Box distribution.
#
#           CMake build script.
#
# Authors:  David Hrdlička, <hrdlickadavid@outlook.com>
#
#           Copyright 2020,2021 David Hrdlička.
#

add_library(sio OBJECT sio_acc3221.c sio_ali5123.c sio_f82c710.c sio_82091aa.c
    sio_fdc37c6xx.c sio_fdc37c67x.c sio_fdc37c669.c sio_fdc37c93x.c sio_fdc37m60x.c
    sio_it8661f.c
    sio_pc87306.c sio_pc87307.c sio_pc87309.c sio_pc87310.c sio_pc87311.c sio_pc87332.c
    sio_prime3b.c sio_prime3c.c
    sio_w83787f.c sio_w83877f.c sio_w83977f.c sio_um8669f.c
    sio_vt82c686.c)

if(SIO_DETECT)
    target_sources(sio PRIVATE sio_detect.c)
endif()
