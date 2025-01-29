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
#          Copyright 2025 Connor Hyde
#

# todo: Split nv stuff into its own file...

add_library(vid OBJECT agpgart.c video.c vid_table.c vid_cga.c vid_cga_comp.c
    vid_compaq_cga.c vid_mda.c vid_hercules.c vid_herculesplus.c
    vid_incolor.c vid_colorplus.c vid_genius.c vid_pgc.c vid_im1024.c
    vid_sigma.c vid_wy700.c vid_ega.c vid_ega_render.c vid_svga.c vid_8514a.c
    vid_svga_render.c vid_ddc.c vid_vga.c vid_ati_eeprom.c vid_ati18800.c
    vid_ati28800.c vid_ati_mach8.c vid_ati_mach64.c vid_ati68875_ramdac.c
    vid_ati68860_ramdac.c vid_bt481_ramdac.c vid_bt48x_ramdac.c vid_chips_69000.c
    vid_av9194.c vid_icd2061.c vid_ics2494.c vid_ics2595.c vid_cl54xx.c
    vid_et3000.c vid_et4000.c vid_sc1148x_ramdac.c vid_sc1502x_ramdac.c
    vid_et4000w32.c vid_stg_ramdac.c vid_ht216.c vid_oak_oti.c vid_paradise.c
    vid_rtg310x.c vid_f82c425.c vid_ti_cf62011.c vid_tvga.c vid_tgui9440.c
    vid_tkd8001_ramdac.c vid_att20c49x_ramdac.c vid_s3.c vid_s3_virge.c
    vid_ibm_rgb528_ramdac.c vid_sdac_ramdac.c vid_ogc.c vid_mga.c vid_nga.c
    vid_tvp3026_ramdac.c vid_att2xc498_ramdac.c vid_xga.c
    vid_bochs_vbe.c
    nv/nv_base.c nv/nv_rivatimer.c
    nv/nv3/nv3_core.c nv/nv3/nv3_core_config.c nv/nv3/nv3_core_arbiter.c  
    nv/nv3/subsystems/nv3_pramdac.c 
    nv/nv3/subsystems/nv3_pfifo.c
    nv/nv3/subsystems/nv3_pgraph.c 
    nv/nv3/subsystems/nv3_pmc.c 
    nv/nv3/subsystems/nv3_pme.c
    nv/nv3/subsystems/nv3_pextdev.c
    nv/nv3/subsystems/nv3_pfb.c
    nv/nv3/subsystems/nv3_pbus.c nv/nv3/subsystems/nv3_pbus_dma.c
    nv/nv3/subsystems/nv3_ptimer.c
    nv/nv3/subsystems/nv3_pramin.c nv/nv3/subsystems/nv3_pramin_ramht.c nv/nv3/subsystems/nv3_pramin_ramfc.c nv/nv3/subsystems/nv3_pramin_ramro.c 
    nv/nv3/subsystems/nv3_pvideo.c

    nv/nv3/classes/nv3_class_names.c
    nv/nv3/classes/nv3_class_001_beta_factor.c
    nv/nv3/classes/nv3_class_002_rop.c
    nv/nv3/classes/nv3_class_003_chroma_key.c
    nv/nv3/classes/nv3_class_004_plane_mask.c
    nv/nv3/classes/nv3_class_005_clipping_rectangle.c
    nv/nv3/classes/nv3_class_006_pattern.c
    nv/nv3/classes/nv3_class_007_rectangle.c
    nv/nv3/classes/nv3_class_008_point.c
    nv/nv3/classes/nv3_class_009_line.c
    nv/nv3/classes/nv3_class_00a_lin.c
    nv/nv3/classes/nv3_class_00b_triangle.c
    nv/nv3/classes/nv3_class_00c_win95_gdi_text.c
    nv/nv3/classes/nv3_class_00d_m2mf.c
    nv/nv3/classes/nv3_class_00e_scaled_image_from_mem.c
    nv/nv3/classes/nv3_class_010_blit.c
    nv/nv3/classes/nv3_class_011_image.c
    nv/nv3/classes/nv3_class_012_bitmap.c
    nv/nv3/classes/nv3_class_014_transfer2memory.c
    nv/nv3/classes/nv3_class_015_stretched_image_from_cpu.c
    nv/nv3/classes/nv3_class_017_d3d5_tri_zeta_buffer.c
    nv/nv3/classes/nv3_class_018_point_zeta_buffer.c
    nv/nv3/classes/nv3_class_01c_image_in_memory.c
    
    )

if(G100)
    target_compile_definitions(vid PRIVATE USE_G100)
endif()

if(XL24)
    target_compile_definitions(vid PRIVATE USE_XL24)
endif()

add_library(voodoo OBJECT vid_voodoo.c vid_voodoo_banshee.c
    vid_voodoo_banshee_blitter.c vid_voodoo_blitter.c vid_voodoo_display.c
    vid_voodoo_fb.c vid_voodoo_fifo.c vid_voodoo_reg.c vid_voodoo_render.c
    vid_voodoo_setup.c vid_voodoo_texture.c)

if(NOT MSVC AND (ARCH STREQUAL "i386" OR ARCH STREQUAL "x86_64"))
    target_compile_options(voodoo PRIVATE "-msse2")
endif()
