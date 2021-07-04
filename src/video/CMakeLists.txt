#
# 86Box		A hypervisor and IBM PC system emulator that specializes in
#		running old operating systems and software designed for IBM
#		PC systems and compatibles from 1981 through fairly recent
#		system designs based on the PCI bus.
#
#		This file is part of the 86Box distribution.
#
#		CMake build script.
#
# Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
#
#		Copyright 2020,2021 David Hrdlička.
#

add_library(vid OBJECT video.c vid_table.c vid_cga.c vid_cga_comp.c
	vid_compaq_cga.c vid_mda.c vid_hercules.c vid_herculesplus.c
	vid_incolor.c vid_colorplus.c vid_genius.c vid_pgc.c vid_im1024.c
	vid_sigma.c vid_wy700.c vid_ega.c vid_ega_render.c vid_svga.c
	vid_svga_render.c vid_ddc.c vid_vga.c vid_ati_eeprom.c vid_ati18800.c
	vid_ati28800.c vid_ati_mach64.c vid_ati68860_ramdac.c vid_bt48x_ramdac.c
	vid_av9194.c vid_icd2061.c vid_ics2494.c vid_ics2595.c vid_cl54xx.c
	vid_et4000.c vid_sc1148x_ramdac.c vid_sc1502x_ramdac.c vid_et4000w32.c
	vid_stg_ramdac.c vid_ht216.c vid_oak_oti.c vid_paradise.c vid_rtg310x.c
	vid_ti_cf62011.c vid_tvga.c vid_tgui9440.c vid_tkd8001_ramdac.c
	vid_att20c49x_ramdac.c vid_s3.c vid_s3_virge.c vid_ibm_rgb528_ramdac.c
	vid_sdac_ramdac.c vid_ogc.c vid_nga.c)

if(MGA)
	target_compile_definitions(vid PRIVATE USE_MGA)
	target_sources(vid PRIVATE vid_mga.c)
endif()

if(S3TRIO3D2X)
	target_compile_definitions(vid PRIVATE USE_S3TRIO3D2X)
endif()

if(VGAWONDER)
	target_compile_definitions(vid PRIVATE USE_VGAWONDER)
endif()

if(XL24)
	target_compile_definitions(vid PRIVATE USE_XL24)
endif()

add_library(voodoo OBJECT vid_voodoo.c vid_voodoo_banshee.c
	vid_voodoo_banshee_blitter.c vid_voodoo_blitter.c vid_voodoo_display.c
	vid_voodoo_fb.c vid_voodoo_fifo.c vid_voodoo_reg.c vid_voodoo_render.c
	vid_voodoo_setup.c vid_voodoo_texture.c)

if(NOT MSVC)
	target_compile_options(voodoo PRIVATE "-msse2")
endif()

# Suppress GCC false positive warnings in vid_voodoo_codegen_x86[-64].h
# that cause ~3000 lines to be output into the logs each time
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
	target_compile_options(voodoo PRIVATE "-Wstringop-overflow=0")
endif()