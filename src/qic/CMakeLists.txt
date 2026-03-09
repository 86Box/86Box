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
#          seal331,        <butilkama@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2025      Vladislav Dudar.
#

add_library(qic OBJECT
     wangtek_qic.c
)
