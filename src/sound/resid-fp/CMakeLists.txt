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

add_library(resid-fp STATIC convolve-sse.cc convolve.cc envelope.cc extfilt.cc
    filter.cc pot.cc sid.cc voice.cc wave.cc wave6581_PST.cc
    wave6581_PS_.cc wave6581_P_T.cc wave6581__ST.cc wave8580_PST.cc
    wave8580_PS_.cc wave8580_P_T.cc wave8580__ST.cc)