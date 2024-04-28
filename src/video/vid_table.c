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
 *
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

static video_timings_t timing_default = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static int was_reset = 0;

static const device_t vid_none_device = {
    .name          = "None",
    .internal_name = "none",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
static const device_t vid_internal_device = {
    .name          = "Internal",
    .internal_name = "internal",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const VIDEO_CARD
video_cards[] = {
  // clang-format off
    { &vid_none_device                                 },
    { &vid_internal_device                             },
    { &atiega800p_device                               },
    { &mach8_vga_isa_device,      VIDEO_FLAG_TYPE_8514 },
    { &mach32_isa_device,         VIDEO_FLAG_TYPE_8514 },
    { &mach64gx_isa_device                             },
    { &ati28800k_device                                },
    { &ati18800_vga88_device                           },
    { &ati28800_device                                 },
    { &compaq_ati28800_device                          },
#if defined(DEV_BRANCH) && defined(USE_XL24)
    { &ati28800_wonderxl24_device                      },
#endif
    { &ati18800_device                                 },
    { &ati18800_wonder_device                          },
    { &cga_device                                      },
    { &sega_device                                     },
    { &gd5401_isa_device                               },
    { &gd5402_isa_device                               },
    { &gd5420_isa_device                               },
    { &gd5422_isa_device                               },
    { &gd5426_isa_device                               },
    { &gd5426_diamond_speedstar_pro_a1_isa_device      },
    { &gd5428_boca_isa_device                          },
    { &gd5428_isa_device                               },
    { &gd5429_isa_device                               },
    { &gd5434_isa_device                               },
    { &gd5434_diamond_speedstar_64_a3_isa_device       },
    { &compaq_cga_device                               },
    { &compaq_cga_2_device                             },
    { &cpqega_device                                   },
    { &ega_device                                      },
    { &g2_gc205_device                                 },
    { &hercules_device,            VIDEO_FLAG_TYPE_MDA },
    { &herculesplus_device,        VIDEO_FLAG_TYPE_MDA },
    { &incolor_device                                  },
    { &inmos_isa_device,           VIDEO_FLAG_TYPE_XGA },
    { &im1024_device                                   },
    { &iskra_ega_device                                },
    { &et4000_kasan_isa_device                         },
    { &mda_device,                 VIDEO_FLAG_TYPE_MDA },
    { &genius_device                                   },
    { &nga_device                                      },
    { &ogc_device                                      },
    { &oti037c_device                                  },
    { &oti067_device                                   },
    { &oti077_device                                   },
    { &paradise_pvga1a_device                          },
    { &paradise_wd90c11_device                         },
    { &paradise_wd90c30_device                         },
    { &colorplus_device                                },
    { &pgc_device                                      },
    { &cga_pravetz_device                              },
    { &radius_svga_multiview_isa_device                },
    { &realtek_rtg3105_device                          },
    { &realtek_rtg3106_device                          },
    { &s3_diamond_stealth_vram_isa_device              },
    { &s3_orchid_86c911_isa_device                     },
    { &s3_ami_86c924_isa_device                        },
    { &s3_metheus_86c928_isa_device                    },
    { &s3_phoenix_86c801_isa_device                    },
    { &s3_spea_mirage_86c801_isa_device                },
    { &sigma_device                                    },
    { &tvga8900b_device                                },
    { &tvga8900d_device                                },
    { &tvga8900dr_device                               },
    { &tvga9000b_device                                },
    { &nec_sv9000_device                               },
    { &et4000k_isa_device                              },
    { &et2000_device                                   },
    { &et3000_isa_device                               },
    { &et4000_tc6058af_isa_device                      },
    { &et4000_isa_device                               },
    { &et4000w32_device                                },
    { &et4000w32i_isa_device                           },
    { &vga_device                                      },
    { &v7_vga_1024i_device                             },
    { &wy700_device                                    },
    { &mach32_mca_device,         VIDEO_FLAG_TYPE_8514 },
    { &gd5426_mca_device                               },
    { &gd5428_mca_device                               },
    { &et4000_mca_device                               },
    { &radius_svga_multiview_mca_device                },
    { &mach32_pci_device,         VIDEO_FLAG_TYPE_8514 },
    { &mach64gx_pci_device                             },
    { &mach64vt2_device                                },
    { &bochs_svga_device                               },
    { &chips_69000_device                              },
    { &gd5430_pci_device,                              },
    { &gd5434_pci_device                               },
    { &gd5436_pci_device,      VIDEO_FLAG_TYPE_SPECIAL },
    { &gd5440_pci_device                               },
    { &gd5446_pci_device,      VIDEO_FLAG_TYPE_SPECIAL },
    { &gd5446_stb_pci_device,  VIDEO_FLAG_TYPE_SPECIAL },
    { &gd5480_pci_device                               },
    { &et4000w32p_videomagic_revb_pci_device           },
    { &et4000w32p_revc_pci_device                      },
    { &et4000w32p_cardex_pci_device                    },
    { &et4000w32p_noncardex_pci_device                 },
    { &et4000w32p_pci_device                           },
    { &s3_spea_mercury_lite_86c928_pci_device          },
    { &s3_diamond_stealth64_964_pci_device             },
    { &s3_elsa_winner2000_pro_x_964_pci_device         },
    { &s3_mirocrystal_20sv_964_pci_device              },
    { &s3_bahamas64_pci_device                         },
    { &s3_phoenix_vision864_pci_device                 },
    { &s3_diamond_stealth_se_pci_device                },
    { &s3_phoenix_trio32_pci_device                    },
    { &s3_diamond_stealth64_pci_device                 },
    { &s3_9fx_pci_device                               },
    { &s3_phoenix_trio64_pci_device                    },
    { &s3_elsa_winner2000_pro_x_pci_device             },
    { &s3_mirovideo_40sv_ergo_968_pci_device           },
    { &s3_9fx_771_pci_device                           },
    { &s3_phoenix_vision968_pci_device                 },
    { &s3_spea_mercury_p64v_pci_device                 },
    { &s3_9fx_531_pci_device                           },
    { &s3_phoenix_vision868_pci_device                 },
    { &s3_cardex_trio64vplus_pci_device                },
    { &s3_phoenix_trio64vplus_pci_device               },
    { &s3_trio64v2_dx_pci_device                       },
    { &s3_virge_325_pci_device                         },
    { &s3_diamond_stealth_2000_pci_device              },
    { &s3_diamond_stealth_3000_pci_device              },
    { &s3_stb_velocity_3d_pci_device                   },
    { &s3_virge_375_pci_device                         },
    { &s3_diamond_stealth_2000pro_pci_device           },
    { &s3_virge_385_pci_device                         },
    { &s3_virge_357_pci_device                         },
    { &s3_diamond_stealth_4000_pci_device              },
    { &s3_trio3d2x_pci_device                          },
    { &millennium_device                               },
    { &millennium_ii_device                            },
    { &mystique_device                                 },
    { &mystique_220_device                             },
    { &tgui9440_pci_device                             },
    { &tgui9660_pci_device                             },
    { &tgui9680_pci_device                             },
    { &voodoo_banshee_device                           },
    { &creative_voodoo_banshee_device                  },
    { &voodoo_3_1000_device                            },
    { &voodoo_3_2000_device                            },
    { &voodoo_3_3000_device                            },
    { &mach32_vlb_device,         VIDEO_FLAG_TYPE_8514 },
    { &mach64gx_vlb_device                             },
    { &et4000w32i_vlb_device                           },
    { &et4000w32p_videomagic_revb_vlb_device           },
    { &et4000w32p_revc_vlb_device                      },
    { &et4000w32p_cardex_vlb_device                    },
    { &et4000w32p_vlb_device                           },
    { &et4000w32p_noncardex_vlb_device                 },
    { &gd5424_vlb_device                               },
    { &gd5426_vlb_device                               },
    { &gd5428_vlb_device                               },
    { &gd5428_diamond_speedstar_pro_b1_vlb_device      },
    { &gd5429_vlb_device                               },
    { &gd5430_diamond_speedstar_pro_se_a8_vlb_device   },
    { &gd5430_vlb_device                               },
    { &gd5434_vlb_device                               },
    { &s3_metheus_86c928_vlb_device                    },
    { &s3_mirocrystal_8s_805_vlb_device                },
    { &s3_mirocrystal_10sd_805_vlb_device              },
    { &s3_phoenix_86c805_vlb_device                    },
    { &s3_spea_mirage_86c805_vlb_device                },
    { &s3_diamond_stealth64_964_vlb_device             },
    { &s3_mirocrystal_20sv_964_vlb_device              },
    { &s3_mirocrystal_20sd_864_vlb_device              },
    { &s3_bahamas64_vlb_device                         },
    { &s3_phoenix_vision864_vlb_device                 },
    { &s3_diamond_stealth_se_vlb_device                },
    { &s3_phoenix_trio32_vlb_device                    },
    { &s3_diamond_stealth64_vlb_device                 },
    { &s3_9fx_vlb_device                               },
    { &s3_phoenix_trio64_vlb_device                    },
    { &s3_spea_mirage_p64_vlb_device                   },
    { &s3_phoenix_vision968_vlb_device                 },
    { &s3_phoenix_vision868_vlb_device                 },
    { &s3_stb_powergraph_64_video_vlb_device           },
    { &ht216_32_standalone_device                      },
    { &tgui9400cxi_device                              },
    { &tgui9440_vlb_device                             },
    { &s3_virge_357_agp_device                         },
    { &s3_diamond_stealth_4000_agp_device              },
    { &s3_trio3d2x_agp_device                          },
    { &productiva_g100_device, VIDEO_FLAG_TYPE_SPECIAL },
    { &velocity_100_agp_device                         },
    { &velocity_200_agp_device                         },
    { &voodoo_3_1000_agp_device                        },
    { &voodoo_3_2000_agp_device                        },
    { &voodoo_3_3000_agp_device                        },
    { &voodoo_3_3500_agp_ntsc_device                   },
    { &voodoo_3_3500_agp_pal_device                    },
    { &compaq_voodoo_3_3500_agp_device                 },
    { &voodoo_3_3500_se_agp_device                     },
    { &voodoo_3_3500_si_agp_device                     },
    { NULL                                             }
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
        if (monitors[i].mon_cga_palette)
            *monitors[i].mon_cga_palette = 0;
        cgapal_rebuild_monitor(i);

        /* Do an inform on the default values, so that that there's some sane values initialized
           even if the device init function does not do an inform of its own. */
        video_inform_monitor(VIDEO_FLAG_TYPE_SPECIAL, &timing_default, i);
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
    loadfont("roms/video/mda/mda.rom", 0);

    if ((card != VID_NONE) && !machine_has_flags(machine, MACHINE_VIDEO_ONLY) &&
        (gfxcard[1] > VID_INTERNAL) && device_is_valid(video_card_getdevice(gfxcard[1]), machine)) {
        video_monitor_init(1);
        monitor_index_global = 1;
        device_add(video_cards[gfxcard[1]].device);
        monitor_index_global = 0;
    }

    /* Do not initialize internal cards here. */
    if ((card > VID_INTERNAL) && !machine_has_flags(machine, MACHINE_VIDEO_ONLY)) {
        vid_table_log("VIDEO: initializing '%s'\n", video_cards[card].device->name);

        video_prepare();

        /* Initialize the video card. */
        device_add(video_cards[card].device);
    }

    was_reset = 1;
}

void
video_post_reset(void)
{
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

    /* Reset the graphics card (or do nothing if it was already done
       by the machine's init function). */
    video_reset(gfxcard[0]);
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

int
video_is_ega_vga(void)
{
    return (video_get_type() == VIDEO_FLAG_TYPE_SPECIAL);
}

int
video_is_8514(void)
{
    return (video_get_type() == VIDEO_FLAG_TYPE_8514);
}

int
video_is_xga(void)
{
    return (video_get_type() == VIDEO_FLAG_TYPE_XGA);
}
