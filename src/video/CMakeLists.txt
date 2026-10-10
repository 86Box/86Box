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
#          Copyright 2025 starfrost
#

add_library(vid OBJECT

    # Video Core
    agpgart.c
    video.c
    vid_table.c

    # RAMDAC (Should this be its own library?)
    ramdac/vid_ramdac_ati68860.c
    ramdac/vid_ramdac_ati68875.c 
    ramdac/vid_ramdac_att20c49x.c
    ramdac/vid_ramdac_att2xc498.c
    ramdac/vid_ramdac_bt48x.c 
    ramdac/vid_ramdac_bt481.c 
    ramdac/vid_ramdac_ibm_rgb528.c
    ramdac/vid_ramdac_sc1148x.c 
    ramdac/vid_ramdac_sc1502x.c 
    ramdac/vid_ramdac_sdac.c 
    ramdac/vid_ramdac_stg1702.c 
    ramdac/vid_ramdac_tkd8001.c 
    ramdac/vid_ramdac_tvp3026.c 

    # Clock generator chips 
    clockgen/vid_clockgen_av9194.c 
    clockgen/vid_clockgen_ch9201.c
    clockgen/vid_clockgen_ch9204.c 
    clockgen/vid_clockgen_icd2047.c
    clockgen/vid_clockgen_icd2061.c
    clockgen/vid_clockgen_ics1494.c 
    clockgen/vid_clockgen_ics2494.c 
    clockgen/vid_clockgen_ics2595.c
    clockgen/vid_clockgen_ics90c64a.c

    # DDC / monitor identification stuff
    vid_ddc.c
    vid_ddc_edid_custom.c

    # CARDS start here

    # CGA / Super CGA
    vid_cga.c
    vid_cga_comp.c
    vid_cga_compaq.c 
    vid_cga_compaq_plasma.c
    vid_cga_colorplus.c
    vid_cga_ncr.c
    vid_cga_olivetti.c
    vid_cga_quadcolor.c
    vid_cga_toshiba_t1000.c
    vid_cga_toshiba_t3100e.c
    vid_cga_v6355.c

    # IBM PC Convertible LCD display
    vid_ibm5140.c

    # PCjr/PC JX/Tandy
    vid_pcjr.c
    vid_pcjx.c
    vid_tandy.c

    # Hercules
    vid_mda.c
    vid_hercules.c
    vid_hercules_plus.c
    vid_hercules_incolor.c

    # Other early CGA-era cards
    vid_3270pc.c
    vid_genius.c
    vid_sigma.c

    # PGC / IM1024 / WY700 high-resolution
    vid_pgc.c
    vid_im1024.c
    vid_wy700.c

    # EGA
    vid_ega.c
    vid_ega_render.c
    vid_jega.c

    # (Real IBM) VGA 
    vid_vga.c
    vid_mcga.c

    # Super VGA core
    vid_svga.c
    vid_svga_render.c

    # IBM PS/2 plasma display
    vid_ibm_plasma.c

    # 8514/A, XGA and derivatives
    vid_8514a.c
    vid_xga.c
    vid_ps55da2.c

    # ATI Technologies
    vid_ati_eeprom.c
    vid_ati18800.c
    vid_ati28800.c
    vid_ati_mach8.c
    vid_ati_mach64.c
    vid_ati_mach64_accel.c
    vid_ati_mach64_3d.c
    vid_ati_rage128.c
    vid_ati_rage128_vga.c
    vid_ati_rage128_display.c
    vid_ati_rage128_ov0.c
    vid_ati_rage128_2d.c
    vid_ati_rage128_pm4.c
    vid_ati_rage128_3d.c
    vid_ati_rage128_mpeg.c
    vid_ati_rage128_raster.c
    vid_ati_rage128_mem.c
    vid_ati_rage128_jit.c
    vid_ati_rage128_synctel.c
    vid_ati_rage128_census.c
    vid_ati_rage128_gpu.c
    vid_ati_rage128_maxx.c

    # Chips & Technologies
    vid_chips_69000.c

    # Cirrus Logic
    vid_cl54xx.c

    # Tseng Labs
    vid_et3000.c
    vid_et4000.c
    vid_et4000w32.c

    # Headland
    vid_ht216.c
    vid_oak_oti.c

    # Paradise
    vid_paradise.c
    vid_wd90c31.c
    vid_rtg310x.c
    vid_f82c425.c
    vid_ti_cf62011.c

    # Trident
    vid_tvga.c 
    vid_tgui9440.c

    # S3 Graphics
    vid_s3.c 
    vid_s3_virge.c

    # Matrox
    vid_mga.c

    # NVidia (pending)
    nv/nv_rivatimer.c

    # Generic
    vid_bochs_vbe.c
    vid_hd44780.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_include_directories(vid PRIVATE /usr/local/include)
endif()

if(G100)
    target_compile_definitions(vid PRIVATE USE_G100)
endif()

if(XL24)
    target_compile_definitions(vid PRIVATE USE_XL24)
endif()

option(ENABLE_RAGE128_LOG "Enable ATI Rage 128 debug logging" OFF)
if(ENABLE_RAGE128_LOG)
    set_property(SOURCE
        vid_ati_rage128.c
        vid_ati_rage128_vga.c
        vid_ati_rage128_display.c
        vid_ati_rage128_ov0.c
        vid_ati_rage128_2d.c
        vid_ati_rage128_pm4.c
        vid_ati_rage128_3d.c
        vid_ati_rage128_mpeg.c
        vid_ati_rage128_raster.c
        vid_ati_rage128_mem.c
        vid_ati_rage128_jit.c
        vid_ati_rage128_synctel.c
        vid_ati_rage128_census.c
        vid_ati_rage128_gpu.c
        vid_ati_rage128_maxx.c
        APPEND PROPERTY COMPILE_DEFINITIONS ENABLE_RAGE128_LOG=1
    )
endif()

# ATI Rage 128: forbid FMA contraction so per-pixel coverage and texel/mip
# selection in the software rasterizer are bit-identical across arm64 (HW FMA,
# one rounding) and x86-64 SSE2 (mul+add, two roundings). MSVC does not
# contract by default and does not accept the flag.
if(NOT MSVC)
    set_source_files_properties(
        vid_ati_rage128.c
        vid_ati_rage128_vga.c
        vid_ati_rage128_display.c
        vid_ati_rage128_ov0.c
        vid_ati_rage128_2d.c
        vid_ati_rage128_pm4.c
        vid_ati_rage128_3d.c
        vid_ati_rage128_mpeg.c
        vid_ati_rage128_raster.c
        vid_ati_rage128_mem.c
        vid_ati_rage128_jit.c
        vid_ati_rage128_synctel.c
        vid_ati_rage128_census.c
        vid_ati_rage128_gpu.c
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off"
    )
endif()

# ATI Rage 128: optional GPU raster backend (vid_ati_rage128_gpu.c) needs
# Vulkan HEADERS only -- the library is dlopen'd at runtime (after the
# MoltenVK/loader environment is staged), so the binary carries no Vulkan
# link and runs unchanged on hosts without it. Without headers the
# backend compiles to stubs and the CPU renderer is untouched.
find_package(Vulkan QUIET)
# Gate on the header, not on Vulkan_FOUND: find_package() also demands the
# import library, which this backend never links. A static MinGW build narrows
# CMAKE_FIND_LIBRARY_SUFFIXES to ".a" (src/CMakeLists.txt), which cannot match
# MSYS2's libvulkan-1.dll.a -- so Vulkan_FOUND is false on every static Windows
# build and the backend would stub out despite perfectly usable headers.
# Vulkan_INCLUDE_DIR is still populated in that case, and is used as the hint.
find_path(R128_VULKAN_INCLUDE_DIR vulkan/vulkan.h HINTS ${Vulkan_INCLUDE_DIR})
if(R128_VULKAN_INCLUDE_DIR)
    target_compile_definitions(vid PRIVATE R128_GPU_HAVE_VULKAN)
    target_include_directories(vid PRIVATE ${R128_VULKAN_INCLUDE_DIR})
    message(STATUS "Rage 128 GPU raster backend: Vulkan headers found (${R128_VULKAN_INCLUDE_DIR}), enabled (runtime dlopen)")
else()
    message(STATUS "Rage 128 GPU raster backend: Vulkan headers not found, stubbed out")
endif()

# 3Dfx Voodoo
add_library(voodoo OBJECT
    vid_voodoo.c
    vid_voodoo_banshee.c
    vid_voodoo_banshee_blitter.c
    vid_voodoo_blitter.c
    vid_voodoo_display.c
    vid_voodoo_fb.c
    vid_voodoo_fifo.c
    vid_voodoo_reg.c
    vid_voodoo_render.c
    vid_voodoo_setup.c
    vid_voodoo_texture.c
)

if(NOT MSVC AND (ARCH STREQUAL "i386" OR ARCH STREQUAL "x86_64"))
    target_compile_options(voodoo PRIVATE "-msse2")
endif()
