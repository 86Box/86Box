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

add_library(mt32emu STATIC Analog.cpp BReverbModel.cpp File.cpp FileStream.cpp
    LA32Ramp.cpp LA32FloatWaveGenerator.cpp LA32WaveGenerator.cpp
    MidiStreamParser.cpp Part.cpp Partial.cpp PartialManager.cpp
    Poly.cpp ROMInfo.cpp SampleRateConverter.cpp
    srchelper/srctools/src/FIRResampler.cpp
    srchelper/srctools/src/IIR2xResampler.cpp
    srchelper/srctools/src/LinearResampler.cpp
    srchelper/srctools/src/ResamplerModel.cpp
    srchelper/srctools/src/SincResampler.cpp
    srchelper/InternalResampler.cpp	Synth.cpp Tables.cpp TVA.cpp TVF.cpp
    TVP.cpp sha1/sha1.cpp c_interface/c_interface.cpp)