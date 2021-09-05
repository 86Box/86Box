/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Define all known video cards.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
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


typedef struct {
    const char		*internal_name;
    const device_t	*device;
} VIDEO_CARD;


static video_timings_t timing_default = {VIDEO_ISA, 8, 16, 32,   8, 16, 32};

static int was_reset = 0;


static const VIDEO_CARD
video_cards[] = {
    { "none",			NULL					},
    { "internal",		NULL					},
    { "egawonder800",		&atiega_device				},
    { "mach64gx_isa",		&mach64gx_isa_device			},
    { "ati28800k",		&ati28800k_device			},
    { "ati18800v",		&ati18800_vga88_device			},
    { "ati28800",		&ati28800_device			},
    { "ati18800",		&ati18800_device			},
#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
    { "ati18800w",		&ati18800_wonder_device			},
#endif
#if defined(DEV_BRANCH) && defined(USE_XL24)
    { "ati28800w",		&ati28800_wonderxl24_device		},
#endif
    { "cga",			&cga_device				},
    { "superega",		&sega_device				},
    { "cl_gd5401_isa",		&gd5401_isa_device			},
    { "cl_gd5402_isa",		&gd5402_isa_device			},
    { "cl_gd5420_isa",		&gd5420_isa_device			},
    { "cl_gd5422_isa",		&gd5422_isa_device			},
    { "cl_gd5428_isa",		&gd5428_isa_device			},
    { "cl_gd5429_isa",		&gd5429_isa_device			},
    { "cl_gd5434_isa",		&gd5434_isa_device			},
    { "compaq_ati28800",	&compaq_ati28800_device			},
    { "compaq_cga",		&compaq_cga_device			},
    { "compaq_cga_2",		&compaq_cga_2_device			},
    { "compaq_ega",		&cpqega_device				},
    { "ega",			&ega_device				},
    { "g2_gc205",		&g2_gc205_device			},
    { "hercules",		&hercules_device			},
    { "hercules_plus",		&herculesplus_device			},
    { "incolor",		&incolor_device				},
    { "im1024",			&im1024_device				},
    { "iskra_ega",		&iskra_ega_device			},
    { "kasan16vga",		&et4000_kasan_isa_device		},
    { "mda",			&mda_device				},
    { "genius",			&genius_device				},
    { "nga",			&nga_device				},
    { "ogc",        		&ogc_device				},
    { "oti037c",		&oti037c_device				},
    { "oti067",			&oti067_device				},
    { "oti077",			&oti077_device				},
    { "pvga1a",			&paradise_pvga1a_device			},
    { "wd90c11",		&paradise_wd90c11_device		},
    { "wd90c30",		&paradise_wd90c30_device		},
    { "plantronics",		&colorplus_device			},
    { "pgc",			&pgc_device				},
	{ "radius_isa",		&radius_svga_multiview_isa_device			},
	{ "rtg3106",		&realtek_rtg3106_device			},
	{ "stealthvram_isa",	&s3_diamond_stealth_vram_isa_device	},
    { "orchid_s3_911",		&s3_orchid_86c911_isa_device		},
	{ "ami_s3_924",		&s3_ami_86c924_isa_device		},
    { "metheus928_isa",		&s3_metheus_86c928_isa_device		},
    { "px_86c801_isa",		&s3_phoenix_86c801_isa_device		},
    { "px_s3_v7_801_isa",	&s3_spea_mirage_86c801_isa_device		},
    { "sigma400",		&sigma_device				},
    { "tvga8900b",		&tvga8900b_device			},
    { "tvga8900d",		&tvga8900d_device			},
    { "tvga9000b",		&tvga9000b_device			},
    { "tgkorvga",		&et4000k_isa_device			},
    { "et2000",			&et2000_device				},
    { "et4000ax",		&et4000_isa_device			},
    { "et4000w32",		&et4000w32_device			},
    { "et4000w32i",		&et4000w32i_isa_device			},
    { "vga",			&vga_device				},
    { "v7_vga_1024i",		&v7_vga_1024i_device			},
    { "wy700",			&wy700_device				},
    { "ibm1mbsvga",		&gd5428_mca_device			},
    { "et4000mca",		&et4000_mca_device			},
    { "radius_mc",		&radius_svga_multiview_mca_device			},
    { "mach64gx_pci",		&mach64gx_pci_device			},
    { "mach64vt2",		&mach64vt2_device			},
    { "et4000w32p_pci",		&et4000w32p_cardex_pci_device		},
    { "et4000w32p_nc_pci",	&et4000w32p_noncardex_pci_device	},
    { "et4000w32p_revc_pci",	&et4000w32p_revc_pci_device		},
    { "cl_gd5430_pci",		&gd5430_pci_device,			},
    { "cl_gd5434_pci",		&gd5434_pci_device			},
    { "cl_gd5436_pci",		&gd5436_pci_device			},
    { "cl_gd5440_pci",		&gd5440_pci_device			},
    { "cl_gd5446_pci",		&gd5446_pci_device			},
    { "cl_gd5480_pci",		&gd5480_pci_device			},
    { "ctl3d_banshee_pci",	&creative_voodoo_banshee_device  	},
    { "stealth32_pci",		&et4000w32p_pci_device			},
    { "stealth64v_pci",		&s3_diamond_stealth64_964_pci_device	},
    { "elsawin2kprox_964_pci",	&s3_elsa_winner2000_pro_x_964_pci_device },
    { "mirocrystal20sv_pci", &s3_mirocrystal_20sv_964_pci_device	},
    { "bahamas64_pci",		&s3_bahamas64_pci_device		},
    { "px_vision864_pci",	&s3_phoenix_vision864_pci_device	},
    { "stealthse_pci",		&s3_diamond_stealth_se_pci_device	},
	{ "px_trio32_pci",		&s3_phoenix_trio32_pci_device		},
    { "stealth64d_pci",		&s3_diamond_stealth64_pci_device	},
    { "n9_9fx_pci",		&s3_9fx_pci_device			},
    { "px_trio64_pci",		&s3_phoenix_trio64_pci_device		},
    { "elsawin2kprox_pci",	&s3_elsa_winner2000_pro_x_pci_device	},
    { "mirovideo40sv_pci",		&s3_mirovideo_40sv_968_pci_device	},
    { "spea_mercury64p_pci",		&s3_spea_mercury_p64v_pci_device	},
    { "px_vision868_pci",	&s3_phoenix_vision868_pci_device	},
	{ "px_trio64vplus_pci",	&s3_phoenix_trio64vplus_pci_device	},
    { "trio64v2dx_pci",		&s3_trio64v2_dx_pci_device		},
    { "stealth3d_2000_pci",	&s3_virge_pci_device			},
    { "stealth3d_3000_pci",	&s3_virge_988_pci_device		},
#if defined(DEV_BRANCH) && defined(USE_MGA)
    { "mystique",		&mystique_device			},
    { "mystique_220",		&mystique_220_device			},
#endif
#if defined(DEV_BRANCH) && defined(USE_S3TRIO3D2X)    
    { "trio3d2x",		&s3_trio3d_2x_pci_device		},
#endif    
    { "virge325_pci",		&s3_virge_325_pci_device		},
    { "virge375_pci",		&s3_virge_375_pci_device		},
    { "virge375_vbe20_pci",	&s3_virge_375_4_pci_device		},
    { "cl_gd5446_stb_pci",	&gd5446_stb_pci_device			},
    { "tgui9440_pci",		&tgui9440_pci_device			},
    { "tgui9660_pci",		&tgui9660_pci_device			},
    { "tgui9680_pci",		&tgui9680_pci_device			},
    { "voodoo_banshee_pci",	&voodoo_banshee_device  		},
    { "voodoo3_2k_pci",		&voodoo_3_2000_device 			},
    { "voodoo3_3k_pci",		&voodoo_3_3000_device 			},
    { "mach64gx_vlb",		&mach64gx_vlb_device			},
    { "et4000w32i_vlb",		&et4000w32i_vlb_device			},
    { "et4000w32p_vlb",		&et4000w32p_cardex_vlb_device		},
    { "et4000w32p_nc_vlb",	&et4000w32p_noncardex_vlb_device	},
    { "et4000w32p_revc_vlb",	&et4000w32p_revc_vlb_device		},
    { "cl_gd5424_vlb",		&gd5424_vlb_device			},
    { "cl_gd5428_vlb",		&gd5428_vlb_device			},
    { "cl_gd5429_vlb",		&gd5429_vlb_device			},
    { "cl_gd5434_vlb",		&gd5434_vlb_device			},
    { "stealth32_vlb",		&et4000w32p_vlb_device			},
    { "cl_gd5426_vlb",		&gd5426_vlb_device			},
    { "cl_gd5430_vlb",		&gd5430_vlb_device			},
    { "stealth3d_2000_vlb",	&s3_virge_vlb_device			},
    { "stealth3d_3000_vlb",	&s3_virge_988_vlb_device		},
    { "metheus928_vlb",		&s3_metheus_86c928_vlb_device		},
	{ "mirocrystal10sd_vlb", &s3_mirocrystal_10sd_805_vlb_device		},
    { "px_86c805_vlb",		&s3_phoenix_86c805_vlb_device		},
    { "px_s3_v7_805_vlb",	&s3_spea_mirage_86c805_vlb_device		},
    { "stealth64v_vlb",		&s3_diamond_stealth64_964_vlb_device	},
    { "mirocrystal20sv_vlb", &s3_mirocrystal_20sv_964_vlb_device	},
    { "bahamas64_vlb",		&s3_bahamas64_vlb_device		},
    { "px_vision864_vlb",	&s3_phoenix_vision864_vlb_device	},
    { "stealthse_vlb",		&s3_diamond_stealth_se_vlb_device	},
    { "px_trio32_vlb",		&s3_phoenix_trio32_vlb_device		},
    { "stealth64d_vlb",		&s3_diamond_stealth64_vlb_device	},
    { "n9_9fx_vlb",		&s3_9fx_vlb_device			},
    { "px_trio64_vlb",		&s3_phoenix_trio64_vlb_device		},
	{ "spea_miragep64_vlb",		&s3_spea_mirage_p64_vlb_device		},
    { "px_vision868_vlb",	&s3_phoenix_vision868_vlb_device	},
    { "ht216_32",		&ht216_32_standalone_device		},
    { "virge325_vlb",		&s3_virge_325_vlb_device		},
    { "virge375_vlb",		&s3_virge_375_vlb_device		},
    { "virge375_vbe20_vlb",	&s3_virge_375_4_vlb_device		},
    { "tgui9400cxi_vlb",	&tgui9400cxi_device			},
    { "tgui9440_vlb",		&tgui9440_vlb_device			},
	{ "velocity100_agp",	&velocity_100_agp_device		},
    { "voodoo3_2k_agp",		&voodoo_3_2000_agp_device		},
    { "voodoo3_3k_agp",		&voodoo_3_3000_agp_device		},
    { "",			NULL                        		}
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
#define vid_table_log(fmt, ...)
#endif


void
video_reset_close(void)
{
    video_inform(VIDEO_FLAG_TYPE_NONE, &timing_default);
    was_reset = 0;
}


void
video_reset(int card)
{
    /* This is needed to avoid duplicate resets. */
    if ((video_get_type() != VIDEO_FLAG_TYPE_NONE) && was_reset)
	return;

    vid_table_log("VIDEO: reset (gfxcard=%d, internal=%d)\n",
		  card, (machines[machine].flags & MACHINE_VIDEO)?1:0);

    loadfont("roms/video/mda/mda.rom", 0);

    /* Reset (deallocate) the video font arrays. */
    if (fontdatksc5601) {
	free(fontdatksc5601);
	fontdatksc5601 = NULL;
    }

    /* Reset the CGA palette. */
    cga_palette = 0;
    cgapal_rebuild();

    /* Reset the blend. */
    herc_blend = 0;

    /* Do not initialize internal cards here. */
    if (!(card == VID_NONE) && \
	!(card == VID_INTERNAL) && !(machines[machine].flags & MACHINE_VIDEO_ONLY)) {
	vid_table_log("VIDEO: initializing '%s'\n", video_cards[card].name);

	/* Do an inform on the default values, so that that there's some sane values initialized
	   even if the device init function does not do an inform of its own. */
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_default);

	/* Initialize the video card. */
	device_add(video_cards[card].device);
    }

    /* Enable the Voodoo if configured. */
    if (voodoo_enabled)
       	device_add(&voodoo_device);

    was_reset = 1;
}


int
video_card_available(int card)
{
    if (video_cards[card].device)
	return(device_available(video_cards[card].device));

    return(1);
}


const device_t *
video_card_getdevice(int card)
{
    return(video_cards[card].device);
}


int
video_card_has_config(int card)
{
    if (video_cards[card].device == NULL) return(0);

    return(video_cards[card].device->config ? 1 : 0);
}


char *
video_get_internal_name(int card)
{
    return((char *) video_cards[card].internal_name);
}


int
video_get_video_from_internal_name(char *s)
{
    int c = 0;

    while (strcmp(video_cards[c].internal_name, "") != 0) {
	if (!strcmp((char *) video_cards[c].internal_name, s))
		return(c);
	c++;
    }

    return(0);
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
