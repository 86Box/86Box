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

add_library(minivhd STATIC cwalk.c libxml2_encoding.c minivhd_convert.c
    minivhd_create.c minivhd_io.c minivhd_manage.c minivhd_struct_rw.c
    minivhd_util.c)
