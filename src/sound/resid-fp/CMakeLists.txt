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

set(CMAKE_CXX_STANDARD 17)

add_library(resid-fp STATIC Dac.cpp EnvelopeGenerator.cpp ExternalFilter.cpp
    Filter.cpp Filter6581.cpp Filter8580.cpp FilterModelConfig.cpp
    FilterModelConfig6581.cpp FilterModelConfig8580.cpp
    Integrator6581.cpp Integrator8580.cpp OpAmp.cpp SID.cpp
    Spline.cpp WaveformCalculator.cpp WaveformGenerator.cpp resample/SincResampler.cpp)
