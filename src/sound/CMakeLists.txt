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

add_library(snd OBJECT sound.c snd_opl.c snd_opl_nuked.c snd_resid.cc
    midi.c midi_rtmidi.cpp snd_speaker.c snd_pssj.c snd_lpt_dac.c snd_ac97_codec.c snd_ac97_via.c
    snd_lpt_dss.c snd_ps1.c snd_adlib.c snd_adlibgold.c snd_ad1848.c snd_audiopci.c
    snd_azt2316a.c snd_cms.c snd_cs423x.c snd_gus.c snd_sb.c snd_sb_dsp.c
    snd_emu8k.c snd_mpu401.c snd_sn76489.c snd_ssi2001.c snd_wss.c snd_ym7128.c)

if(OPENAL OR FAUDIO)
    target_compile_definitions(snd PRIVATE USE_OPENAL)
    if (FAUDIO)
        target_sources(snd PRIVATE faudio.cpp)
    else()
        target_sources(snd PRIVATE openal.c)
    endif()
endif()

if(FLUIDSYNTH)
    target_compile_definitions(snd PRIVATE USE_FLUIDSYNTH)
    target_sources(snd PRIVATE midi_fluidsynth.c)
endif()

if(MUNT)
    target_compile_definitions(snd PRIVATE USE_MUNT)
    target_sources(snd PRIVATE midi_mt32.c)

    option(MUNT_EXTERNAL "Link against the system-provided MUNT library" OFF)
    mark_as_advanced(MUNT_EXTERNAL)

    if(MUNT_EXTERNAL)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MT32EMU REQUIRED IMPORTED_TARGET mt32emu)
        target_link_libraries(86Box PkgConfig::MT32EMU)
    else()
        add_subdirectory(munt)
        target_link_libraries(86Box mt32emu)
    endif()
endif()

if(PAS16)
    target_compile_definitions(snd PRIVATE USE_PAS16)
    target_sources(snd PRIVATE snd_pas16.c)
endif()

if(GUSMAX)
    target_compile_definitions(snd PRIVATE USE_GUSMAX)
endif()

if(TANDY_ISA)
    target_compile_definitions(snd PRIVATE USE_TANDY_ISA)
endif()

add_subdirectory(resid-fp)
target_link_libraries(86Box resid-fp)
