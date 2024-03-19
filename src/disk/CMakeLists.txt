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

add_library(hdd OBJECT hdd.c hdd_image.c hdd_table.c hdc.c hdc_st506_xt.c
    hdc_st506_at.c hdc_xta.c hdc_esdi_at.c hdc_esdi_mca.c hdc_xtide.c
    hdc_ide.c hdc_ide_ali5213.c hdc_ide_opti611.c hdc_ide_cmd640.c hdc_ide_cmd646.c
    hdc_ide_sff8038i.c hdc_ide_um8673f.c hdc_ide_w83769f.c lba_enhancer.c)

add_library(zip OBJECT zip.c)

add_library(mo OBJECT mo.c)

add_subdirectory(minivhd)
target_link_libraries(86Box minivhd)
