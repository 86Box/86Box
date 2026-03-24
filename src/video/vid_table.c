/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Define all known video cards.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/vid_svga.h>

#include <86box/vid_cga.h>
#include <86box/vid_ega.h>
#include <86box/vid_colorplus.h>
#include <86box/vid_mda.h>
#include <86box/vid_xga_device.h>

typedef struct video_card_t {
    const device_t *device;
    int             flags;
} VIDEO_CARD;

typedef struct video_card_migrate_t {
    const device_t *device;
    const char     *old_internal_name;
} video_card_migrate_t;

static video_timings_t timing_default = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static int was_reset = 0;

static const VIDEO_CARD
video_cards[] = {
  // clang-format off
    { .device = &device_none,                                   .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &device_internal,                               .flags = VIDEO_FLAG_TYPE_NONE      },
    /* ISA */
    { .device = &ati18800_wonder_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &ati18800_vga88_device,                         .flags = VIDEO_FLAG_TYPE_NONE      },
#ifdef USE_XL24
    { .device = &ati28800_wonderxl24_device,                    .flags = VIDEO_FLAG_TYPE_NONE      },
#endif /* USE_XL24 */
    { .device = &ati28800_device,                               .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &compaq_ati28800_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &ati28800_wonder1024d_xl_plus_device,           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &atiega800p_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &mach8_vga_isa_device,                          .flags = VIDEO_FLAG_TYPE_8514      },
    { .device = &mach32_isa_device,                             .flags = VIDEO_FLAG_TYPE_8514      },
    { .device = &ati28800k_device,                              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &ati18800_device,                               .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &sega_device,                                   .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5401_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5402_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &colorplus_device,                              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &compaq_cga_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &compaq_cga_2_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &cpqega_device,                                 .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &g2_gc205_device,                               .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &genius_device,                                 .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &hercules_device,                               .flags = VIDEO_FLAG_TYPE_MDA       },
    { .device = &herculesplus_device,                           .flags = VIDEO_FLAG_TYPE_MDA       },
    { .device = &incolor_device,                                .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &cga_device,                                    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &ega_device,                                    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &mda_device,                                    .flags = VIDEO_FLAG_TYPE_MDA       },
    { .device = &pgc_device,                                    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &vga_device,                                    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &im1024_device,                                 .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &iskra_ega_device,                              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &jega_device,                                   .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000_kasan_isa_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &nga_device,                                    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &nec_sv9000_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &ogc_device,                                    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &oti037c_device,                                .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &oti067_device,                                 .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &oti077_device,                                 .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &paradise_pvga1a_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &paradise_wd90c11_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &paradise_wd90c30_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &cga_pravetz_device,                            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &quadcolor_device,                              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &realtek_rtg3105_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &realtek_rtg3106_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &sigma_device,                                  .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tvga8900b_device,                              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tvga8900d_device,                              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tvga8900dr_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tvga9000b_device,                              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000k_isa_device,                            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et2000_device,                                 .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et3000_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000_tc6058af_isa_device,                    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &v7_vga_1024i_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &wy700_device,                                  .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &v6355d_device,                                 .flags = VIDEO_FLAG_TYPE_NONE      },
    /* ISA16 */
    { .device = &mach64gx_isa_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5420_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5422_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5422_boca_isa_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5426_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5426_diamond_speedstar_pro_a1_isa_device,    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5428_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5429_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5434_isa_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5434_diamond_speedstar_64_a3_isa_device,     .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &inmos_isa_device,                              .flags = VIDEO_FLAG_TYPE_XGA       },
    { .device = &jvga_device,                                   .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &radius_svga_multiview_isa_device,              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_86c911_isa_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_86c924_isa_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_86c928_isa_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_86c801_isa_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_86c805_isa_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32_machspeed_vga_gui_2400s_isa_device,  .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32i_axis_microdevice_isa_device,        .flags = VIDEO_FLAG_TYPE_NONE      },
    /* MCA */
    { .device = &mach32_mca_device,                             .flags = VIDEO_FLAG_TYPE_8514      },
    { .device = &gd5426_mca_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5428_mca_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &radius_svga_multiview_mca_device,              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000_mca_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    /* VLB */
    { .device = &mach32_vlb_device,                             .flags = VIDEO_FLAG_TYPE_8514      },
    { .device = &mach64gx_vlb_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5424_vlb_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5426_vlb_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5428_vlb_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5428_diamond_speedstar_pro_b1_vlb_device,    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5429_vlb_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5430_diamond_speedstar_pro_se_a8_vlb_device, .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5430_vlb_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5434_vlb_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &ht216_32_standalone_device,                    .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_86c928_vlb_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_86c805_vlb_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_vision864_vlb_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio32_vlb_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_vision964_vlb_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio64_vlb_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_vision968_vlb_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio64vplus_vlb_device,                     .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tgui9400cxi_device,                            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tgui9440_vlb_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32_machspeed_vga_gui_2400s_vlb_device,  .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32i_hercules_dynamite_pro_vlb_device,   .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_videomagic_revb_vlb_device,         .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_cardex_revc_vlb_device,             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_generic_revd_vlb_device,            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_cardex_revd_vlb_device,             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_diamond_revd_vlb_device,            .flags = VIDEO_FLAG_TYPE_NONE      },
    /* PCI */
    { .device = &voodoo_banshee_device,                         .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_1000_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_2000_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_3000_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &mach32_pci_device,                             .flags = VIDEO_FLAG_TYPE_8514      },
    { .device = &mach64gx_pci_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &mach64ct_device,                               .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &mach64vt_device,                               .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &mach64vt2_device,                              .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &bochs_svga_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &chips_69000_device,                            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5430_pci_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5434_pci_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5436_pci_device,                             .flags = VIDEO_FLAG_TYPE_SECONDARY },
    { .device = &gd5440_pci_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5446_pci_device,                             .flags = VIDEO_FLAG_TYPE_SECONDARY },
    { .device = &gd5446_stb_pci_device,                         .flags = VIDEO_FLAG_TYPE_SECONDARY },
    { .device = &gd5480_pci_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &creative_voodoo_banshee_device,                .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &millennium_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &millennium_ii_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &mystique_device,                               .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &mystique_220_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &quantum3d_raven_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_86c928_pci_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_vision864_pci_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio32_pci_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_vision964_pci_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio64_pci_device,                          .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_vision868_pci_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_vision968_pci_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio64vplus_pci_device,                     .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio64v2dx_pci_device,                      .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_virge_325_pci_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_diamond_stealth_2000_pci_device,            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_mirocrystal_3d_pci_device,                  .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_diamond_stealth_3000_pci_device,            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_stb_velocity_3d_pci_device,                 .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_virge_375_pci_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_diamond_stealth_2000pro_pci_device,         .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_virge_385_pci_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_virge_357_pci_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio3d2x_pci_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tgui9440_pci_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tgui9660_pci_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &tgui9680_pci_device,                           .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_cardex_revc_pci_device,             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_generic_revd_pci_device,            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_cardex_revd_pci_device,             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &et4000w32p_diamond_revd_pci_device,            .flags = VIDEO_FLAG_TYPE_NONE      },
    /* AGP */
    { .device = &velocity_100_agp_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &velocity_200_agp_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_1000_agp_device,                      .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_2000_agp_device,                      .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_3000_agp_device,                      .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_3500_agp_ntsc_device,                 .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_3500_agp_pal_device,                  .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_3500_si_agp_device,                   .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &compaq_voodoo_3_3500_agp_device,               .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &voodoo_3_3500_se_agp_device,                   .flags = VIDEO_FLAG_TYPE_NONE      },
#ifdef USE_G100
    { .device = &productiva_g100_device,                        .flags = VIDEO_FLAG_TYPE_SPECIAL   },
#endif /*USE_G100 */
    { .device = &s3_virge_357_agp_device,                       .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_diamond_stealth_4000_agp_device,            .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &s3_trio3d2x_agp_device,                        .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = NULL,                                           .flags = VIDEO_FLAG_TYPE_NONE      }
  // clang-format on
};

static const video_card_migrate_t
video_cards_migrate[] = {
  // clang-format off
    /* 911 ISA */
    { .device = &s3_86c911_isa_device,                          .old_internal_name = "stealthvram_isa"        },
    { .device = &s3_86c911_isa_device,                          .old_internal_name = "orchid_s3_911"          },
    /* 924 ISA */
    { .device = &s3_86c924_isa_device,                          .old_internal_name = "ami_s3_924"             },
    /* 928 ISA */
    { .device = &s3_86c928_isa_device,                          .old_internal_name = "elsawin2k928_isa"       },
    { .device = &s3_86c928_isa_device,                          .old_internal_name = "metheus928_isa"         },
    /* 928 VLB */
    { .device = &s3_86c928_vlb_device,                          .old_internal_name = "elsawin1k928_vlb"       },
    { .device = &s3_86c928_vlb_device,                          .old_internal_name = "metheus928_vlb"         },
    /* 928 PCI */
    { .device = &s3_86c928_pci_device,                          .old_internal_name = "elsawin1k928_pci"       },
    { .device = &s3_86c928_pci_device,                          .old_internal_name = "spea_mercurylite_pci"   },
    /* 801 ISA */
    { .device = &s3_86c801_isa_device,                          .old_internal_name = "px_86c801_isa"          },
    { .device = &s3_86c801_isa_device,                          .old_internal_name = "px_s3_v7_801_isa"       },
    /* 805 ISA */
    { .device = &s3_86c805_isa_device,                          .old_internal_name = "winner1000_805_isa"     },
    /* 805 VLB */
    { .device = &s3_86c805_vlb_device,                          .old_internal_name = "mirocrystal8s_vlb"      },
    { .device = &s3_86c805_vlb_device,                          .old_internal_name = "mirocrystal10sd_vlb"    },
    { .device = &s3_86c805_vlb_device,                          .old_internal_name = "px_86c805_vlb"          },
    { .device = &s3_86c805_vlb_device,                          .old_internal_name = "px_s3_v7_805_vlb"       },
    /* Vision864 VLB */
    { .device = &s3_vision864_vlb_device,                       .old_internal_name = "mirocrystal20sd_vlb"    },
    { .device = &s3_vision864_vlb_device,                       .old_internal_name = "bahamas64_vlb"          },
    { .device = &s3_vision864_vlb_device,                       .old_internal_name = "px_vision864_vlb"       },
    /* Vision864 PCI */
    { .device = &s3_vision864_pci_device,                       .old_internal_name = "bahamas64_pci"          },
    { .device = &s3_vision864_pci_device,                       .old_internal_name = "px_vision864_pci"       },
    /* Trio32 VLB */
    { .device = &s3_trio32_vlb_device,                          .old_internal_name = "stealthse_vlb"          },
    { .device = &s3_trio32_vlb_device,                          .old_internal_name = "px_trio32_vlb"          },
    /* Trio32 PCI */
    { .device = &s3_trio32_pci_device,                          .old_internal_name = "stealthse_pci"          },
    { .device = &s3_trio32_pci_device,                          .old_internal_name = "px_trio32_pci"          },
    /* Vision964 VLB */
    { .device = &s3_vision964_vlb_device,                       .old_internal_name = "stealth64v_vlb"         },
    { .device = &s3_vision964_vlb_device,                       .old_internal_name = "mirocrystal20sv_vlb"    },
    /* Vision964 PCI */
    { .device = &s3_vision964_pci_device,                       .old_internal_name = "stealth64v_pci"         },
    { .device = &s3_vision964_pci_device,                       .old_internal_name = "elsawin2kprox_964_pci"  },
    { .device = &s3_vision964_pci_device,                       .old_internal_name = "mirocrystal20sv_pci"    },
    /* Trio64 VLB */
    { .device = &s3_trio64_vlb_device,                          .old_internal_name = "stealth64d_vlb"         },
    { .device = &s3_trio64_vlb_device,                          .old_internal_name = "n9_9fx_vlb"             },
    { .device = &s3_trio64_vlb_device,                          .old_internal_name = "px_trio64_vlb"          },
    { .device = &s3_trio64_vlb_device,                          .old_internal_name = "spea_miragep64_vlb"     },
    /* Trio64 PCI */
    { .device = &s3_trio64_pci_device,                          .old_internal_name = "stealth64d_pci"         },
    { .device = &s3_trio64_pci_device,                          .old_internal_name = "n9_9fx_pci"             },
    { .device = &s3_trio64_pci_device,                          .old_internal_name = "px_trio64_pci"          },
    /* Vision868 PCI */
    { .device = &s3_vision868_pci_device,                       .old_internal_name = "n9_9fx_531_pci"         },
    { .device = &s3_vision868_pci_device,                       .old_internal_name = "px_vision868_pci"       },
    /* Vision968 VLB */
    { .device = &s3_vision968_vlb_device,                       .old_internal_name = "stealth64vv_vlb"        },
    /* Vision968 PCI */
    { .device = &s3_vision968_pci_device,                       .old_internal_name = "stealth64vv_pci"        },
    { .device = &s3_vision968_pci_device,                       .old_internal_name = "elsawin2kprox_pci"      },
    { .device = &s3_vision968_pci_device,                       .old_internal_name = "mirovideo40sv_pci"      },
    { .device = &s3_vision968_pci_device,                       .old_internal_name = "n9_9fx_771_pci"         },
    { .device = &s3_vision968_pci_device,                       .old_internal_name = "px_vision968_pci"       },
    { .device = &s3_vision968_pci_device,                       .old_internal_name = "spea_mercury64p_pci"    },
    /* Trio64V+ VLB */
    { .device = &s3_trio64vplus_vlb_device,                     .old_internal_name = "stb_trio64vplus_vlb"    },
    /* Trio64V+ PCI */
    { .device = &s3_trio64vplus_pci_device,                     .old_internal_name = "cardex_trio64vplus_pci" },
    { .device = &s3_trio64vplus_pci_device,                     .old_internal_name = "px_trio64vplus_pci"     },
    /* Trio64V2/DX PCI */
    { .device = &s3_trio64v2dx_pci_device,                      .old_internal_name = "trio64v2dx_pci"         },
    { .device = NULL,                                           .old_internal_name = ""                       }
  // clang-format on
};

#ifdef ENABLE_VID_TABLE_LOG
int vid_table_do_log = ENABLE_VID_TABLE_LOG;

static void
vid_table_log(const char *fmt, ...)
{
    va_list ap;

    if (vid_table_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define vid_table_log(fmt, ...)
#endif

static pc_timer_t framerate_timer;

void
video_update_framerates(void* priv)
{
    (void)priv;
    int i = 0;

    for (i = 0; i < GFXCARD_MAX; i++) {
        monitors[i].mon_actualrenderedframes = monitors[i].mon_renderedframes;
        monitors[i].mon_renderedframes = 0;
    }

    timer_on_auto(&framerate_timer, 1000 * 1000);
}

void
video_reset_close(void)
{
    for (int i = 1; i < MONITORS_NUM; i++)
        video_monitor_close(i);

    monitor_index_global = 0;
    video_inform(VIDEO_FLAG_TYPE_NONE, &timing_default);
    was_reset = 0;
}

static void
video_prepare(void)
{
    /* Reset (deallocate) the video font arrays. */
    if (fontdatksc5601) {
        free(fontdatksc5601);
        fontdatksc5601 = NULL;
    }

    /* Reset the blend. */
    herc_blend = 0;

    for (int i = 0; i < MONITORS_NUM; i++) {
        /* Reset the CGA palette. */
#if 0
        if (monitors[i].mon_cga_palette)
            *monitors[i].mon_cga_palette = 0;
#endif
        cgapal_rebuild_monitor(i);

        /* Do an inform on the default values, so that that there's some sane values initialized
           even if the device init function does not do an inform of its own. */
        video_inform_monitor(VIDEO_FLAG_TYPE_SPECIAL, &timing_default, i);

        monitors[i].mon_interlace = 0;
        monitors[i].mon_composite = 0;
    }
}

void
video_pre_reset(int card)
{
    if ((card == VID_NONE) || (card == VID_INTERNAL) || machine_has_flags(machine, MACHINE_VIDEO_ONLY))
        video_prepare();
}

void
video_reset(int card)
{
    /* This is needed to avoid duplicate resets. */
    if ((video_get_type() != VIDEO_FLAG_TYPE_NONE) && was_reset)
        return;

    vid_table_log("VIDEO: reset (gfxcard[0]=%d, internal=%d)\n",
                  card, machine_has_flags(machine, MACHINE_VIDEO) ? 1 : 0);

    monitor_index_global = 0;
    video_load_font(FONT_IBM_MDA_437_PATH, FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);

    for (uint8_t i = 1; i < GFXCARD_MAX; i ++) {
        if ((card != VID_NONE) && !machine_has_flags(machine, MACHINE_VIDEO_ONLY) &&
            (gfxcard[i] > VID_INTERNAL) && device_is_valid(video_card_getdevice(gfxcard[i]), machine)) {
            video_monitor_init(i);
            monitor_index_global = 1;
            device_add(video_cards[gfxcard[i]].device);
            monitor_index_global = 0;
        }
    }

    /* Do not initialize internal cards here. */
    if ((card > VID_INTERNAL) && !machine_has_flags(machine, MACHINE_VIDEO_ONLY)) {
        vid_table_log("VIDEO: initializing '%s'\n", video_cards[card].device->name);

        video_prepare();

        /* Initialize the video card. */
        device_add(video_cards[card].device);
    }

    timer_add(&framerate_timer, video_update_framerates, NULL, 1);
    was_reset = 1;
}

void
video_post_reset(void)
{
    /* Reset the graphics card (or do nothing if it was already done
       by the machine's init function). */
    video_reset(gfxcard[0]);

    int ibm8514_has_vga = 0;
    if (gfxcard[0] == VID_INTERNAL)
        ibm8514_has_vga = (video_get_type_monitor(0) == VIDEO_FLAG_TYPE_8514);
    else if (gfxcard[0] != VID_NONE)
        ibm8514_has_vga = (video_card_get_flags(gfxcard[0]) == VIDEO_FLAG_TYPE_8514);
    else
        ibm8514_has_vga = 0;

    if (ibm8514_has_vga)
        ibm8514_active = 1;

    if (ibm8514_standalone_enabled)
        ibm8514_device_add();

    if (xga_standalone_enabled)
        xga_device_add();

    if (da2_standalone_enabled)
        da2_device_add();
}

void
video_voodoo_init(void)
{
    /* Enable the Voodoo if configured. */
    if (voodoo_enabled)
        device_add(&voodoo_device);
}

int
video_card_available(int card)
{
    if (video_cards[card].device)
        return (device_available(video_cards[card].device));

    return 1;
}

int
video_card_get_flags(int card)
{
    return video_cards[card].flags;
}

const device_t *
video_card_getdevice(int card)
{
    return (video_cards[card].device);
}

int
video_card_has_config(int card)
{
    if (video_cards[card].device == NULL)
        return 0;

    return (device_has_config(video_cards[card].device) ? 1 : 0);
}

const char *
video_get_internal_name(int card)
{
    return device_get_internal_name(video_cards[card].device);
}

int
video_get_video_from_internal_name(char *s)
{
    int c = 0;

    while (video_cards[c].device != NULL) {
        if (!strcmp(video_cards[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

const device_t *
video_get_video_from_old_internal_name(char *s)
{
    int c = 0;

    while (video_cards_migrate[c].device != NULL) {
        if (!strcmp(video_cards_migrate[c].old_internal_name, s))
            return video_cards_migrate[c].device;
        c++;
    }

    return NULL;
}

int
video_is_mda(void)
{
    return (video_get_type() == VIDEO_FLAG_TYPE_MDA);
}

int
video_is_cga(void)
{
    return (video_get_type() == VIDEO_FLAG_TYPE_CGA);
}
