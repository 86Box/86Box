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

add_library(fdd OBJECT fdd.c fdc.c fdc_magitronic.c fdc_monster.c fdc_pii15xb.c
    fdi2raw.c fdd_common.c fdd_86f.c fdd_fdi.c fdd_imd.c fdd_img.c fdd_pcjs.c
    fdd_mfm.c fdd_td0.c)

add_subdirectory(lzw)
target_link_libraries(86Box lzw)
