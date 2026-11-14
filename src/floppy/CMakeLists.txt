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
#          Jasmine Iwanek, <jriwanek@gmail.com>
#          Toni Riikonen, <riikonen.toni@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2024      Jasmine Iwanek.
#          Copyright 2025      Toni Riikonen.
#          All rights reserved.
#

# Build the FAT filesystem and block device support without the Arduino drivers.
set(sdfat_src "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sdfat/src")
add_library(sdfat STATIC
    fdd_folder.cpp
    "${sdfat_src}/FatLib/FatFile.cpp"
    "${sdfat_src}/FatLib/FatFileLFN.cpp"
    "${sdfat_src}/FatLib/FatName.cpp"
    "${sdfat_src}/FatLib/FatPartition.cpp"
    "${sdfat_src}/FatLib/FatVolume.cpp"
    "${sdfat_src}/common/FmtNumber.cpp"
    "${sdfat_src}/common/FsCache.cpp"
    "${sdfat_src}/common/FsDateTime.cpp"
    "${sdfat_src}/common/FsName.cpp"
    "${sdfat_src}/common/FsUtf.cpp"
    "${sdfat_src}/common/upcase.cpp"
)
target_include_directories(sdfat SYSTEM PRIVATE "${sdfat_src}")
target_compile_definitions(sdfat PRIVATE
    ENABLE_ARDUINO_FEATURES=0 ENABLE_ARDUINO_SERIAL=0 ENABLE_ARDUINO_STRING=0
    SPI_DRIVER_SELECT=3 USE_FCNTL_H=0
)
target_compile_options(sdfat PRIVATE
    "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/sdfat_compat.hpp\""
)

add_library(fdd OBJECT
    fdc_common_upd765.c
    fdc_common_ctrl.c
    fdc_compaticard.c
    fdc_magitronic.c
    fdc_monster.c
    fdc_qd6500.c
    fdc_pii15xb.c
    fdd.c
    fdi2raw.c
    fdd_common.c
    fdd_86f.c
    fdd_fdi.c
    fdd_imd.c
    fdd_img.c
    fdd_pcjs.c
    fdd_mfm.c
    fdd_td0.c
    fdd_tape.c
    fdd_audio.c
)

add_subdirectory(lzw)
target_link_libraries(86Box lzw sdfat)
