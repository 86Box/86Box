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

add_library(softfloat OBJECT f2xm1.cc fpatan.cc fprem.cc fsincos.cc fyl2x.cc softfloat_poly.cc softfloat.cc softfloat16.cc
		softfloat-muladd.cc softfloat-round-pack.cc softfloat-specialize.cc softfloatx80.cc)
