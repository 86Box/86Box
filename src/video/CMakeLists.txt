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
#          Copyright 2025 Connor Hyde / starfrost
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
    clockgen/vid_clockgen_icd2061.c
    clockgen/vid_clockgen_ics2494.c 
    clockgen/vid_clockgen_ics2595.c

    # DDC / monitor identification stuff
    vid_ddc.c

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

    # PCJr/Tandy
    vid_pcjr.c
    vid_tandy.c
    vid_mda.c

    # Hercules
    vid_hercules.c
    vid_hercules_plus.c
    vid_hercules_incolor.c

    # Other early CGA-era cards
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

    # Super VGA core
    vid_svga.c
    vid_svga_render.c

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

    # NVidia - Core
    nv/nv_base.c
    nv/nv_rivatimer.c

    # NVidia RIVA 128 - Subsystems
    nv/nv3/nv3_core.c 
    nv/nv3/nv3_core_config.c 
    nv/nv3/nv3_core_arbiter.c  

    nv/nv3/subsystems/nv3_pramdac.c 
    nv/nv3/subsystems/nv3_pfifo.c
    nv/nv3/subsystems/nv3_pgraph.c 
    nv/nv3/subsystems/nv3_pmc.c 
    nv/nv3/subsystems/nv3_pme.c
    nv/nv3/subsystems/nv3_pextdev.c
    nv/nv3/subsystems/nv3_pfb.c
    nv/nv3/subsystems/nv3_pbus.c 
    nv/nv3/subsystems/nv3_pbus_dma.c
    nv/nv3/subsystems/nv3_ptimer.c
    nv/nv3/subsystems/nv3_pramin.c 
    nv/nv3/subsystems/nv3_pramin_ramht.c 
    nv/nv3/subsystems/nv3_pramin_ramfc.c 
    nv/nv3/subsystems/nv3_pramin_ramro.c 
    nv/nv3/subsystems/nv3_pvideo.c
    nv/nv3/subsystems/nv3_user.c

    # NVidia RIVA 128 - Object Classes
    nv/nv3/classes/nv3_class_names.c
    nv/nv3/classes/nv3_class_shared_methods.c
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

    # NVidia RIVA 128 - Render
    nv/nv3/render/nv3_render_core.c
    nv/nv3/render/nv3_render_primitives.c   
    nv/nv3/render/nv3_render_blit.c   

    # Generic
    vid_bochs_vbe.c
)

if(G100)
    target_compile_definitions(vid PRIVATE USE_G100)
endif()

if(XL24)
    target_compile_definitions(vid PRIVATE USE_XL24)
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
