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
 * Version:	@(#)vid_table.c	1.0.12	2018/01/25
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../machine/machine.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../timer.h"
#include "../plat.h"
#include "video.h"
#include "vid_svga.h"

#include "vid_ati18800.h"
#include "vid_ati28800.h"
#include "vid_ati_mach64.h"
#include "vid_cga.h"
#ifdef DEV_BRANCH
# ifdef USE_CIRRUS
#  include "vid_cl_ramdac.h" /* vid_cl_gd.c needs this */
#  include "vid_cl_gd.h"
# endif
#endif
#include "vid_compaq_cga.h"
#include "vid_ega.h"
#include "vid_et4000.h"
#include "vid_et4000w32.h"
#include "vid_genius.h"
#include "vid_hercules.h"
#include "vid_herculesplus.h"
#include "vid_incolor.h"
#include "vid_colorplus.h"
#include "vid_mda.h"
#ifdef DEV_BRANCH
# ifdef USE_RIVA
#  include "vid_nv_riva128.h"
# endif
#endif
#include "vid_oti067.h"
#include "vid_paradise.h"
#include "vid_s3.h"
#include "vid_s3_virge.h"
#include "vid_tgui9440.h"
#include "vid_ti_cf62011.h"
#include "vid_tvga.h"
#include "vid_vga.h"
#include "vid_voodoo.h"
#include "vid_wy700.h"


typedef struct {
    char	name[64];
    char	internal_name[24];
    device_t	*device;
    int		legacy_id;
} VIDEO_CARD;


static VIDEO_CARD
video_cards[] = {
    { "None",						"none",
      NULL,				GFX_NONE			},
    { "Internal",					"internal",
      NULL,				GFX_INTERNAL			},
    {"[ISA] ATI Graphics Pro Turbo (Mach64 GX)",	"mach64gx_isa",
      &mach64gx_isa_device,		GFX_MACH64GX_ISA		},
    { "[ISA] ATI VGA Charger (ATI-28800-5)",		"ati28800",
      &ati28800_device,			GFX_VGACHARGER			},
    { "[ISA] ATI VGA Wonder XL24 (ATI-28800-6)",	"ati28800w",
      &ati28800_wonderxl24_device,	GFX_VGAWONDERXL24		},
    { "[ISA] ATI VGA Edge-16 (ATI-18800)",		"ati18800",
      &ati18800_device,			GFX_VGAEDGE16			},
    { "[ISA] CGA",					"cga",
      &cga_device,			GFX_CGA				},
    { "[ISA] Chips & Technologies SuperEGA",		"superega",
      &sega_device,			GFX_SUPER_EGA			},
#if defined(DEV_BRANCH) && defined(USE_CIRRUS)
    { "[ISA] Cirrus Logic CL-GD5422",			"cl_gd5422",
      &gd5422_device,			GFX_CL_GD5422			},
    { "[ISA] Cirrus Logic CL-GD5430",			"cl_gd5430",
      &gd5430_device,			GFX_CL_GD5430			},
    { "[ISA] Cirrus Logic CL-GD5434",		    "cl_gd5434",		&gd5434_device,				GFX_CL_GD5434			},
    { "[ISA] Cirrus Logic CL-GD5436",		    "cl_gd5436",		&gd5436_device,				GFX_CL_GD5436			},
    { "[ISA] Cirrus Logic CL-GD5440",		    "cl_gd5440",		&gd5440_device,				GFX_CL_GD5440			},
#endif
    { "[ISA] Compaq ATI VGA Wonder XL (ATI-28800-5)","compaq_ati28800",		&compaq_ati28800_device,     		GFX_VGAWONDERXL			},
    { "[ISA] Compaq CGA",                            "compaq_cga",		&compaq_cga_device,              	GFX_COMPAQ_CGA			},
    { "[ISA] Compaq CGA 2",                          "compaq_cga_2",		&compaq_cga_2_device,              	GFX_COMPAQ_CGA_2		},
    { "[ISA] Compaq EGA",                            "compaq_ega",		&cpqega_device,              		GFX_COMPAQ_EGA			},
    { "[ISA] EGA",                                   "ega",			&ega_device,                 		GFX_EGA				},
    { "[ISA] Hercules",                              "hercules",			&hercules_device,            		GFX_HERCULES			},
    { "[ISA] Hercules Plus",                         "hercules_plus",		&herculesplus_device,        		GFX_HERCULESPLUS		},
    { "[ISA] Hercules InColor",                      "incolor",			&incolor_device,             		GFX_INCOLOR			},
    { "[ISA] MDA",                                   "mda",			&mda_device,                 		GFX_MDA				},
    { "[ISA] MDSI Genius",                           "genius",            	&genius_device,              		GFX_GENIUS			},
    { "[ISA] OAK OTI-067",                           "oti067",			&oti067_device,              		GFX_OTI067			},
    { "[ISA] OAK OTI-077",                           "oti077",			&oti077_device,              		GFX_OTI077			},
    { "[ISA] Paradise PVGA1A",             	     "pvga1a",			&paradise_pvga1a_device,    		GFX_PVGA1A			},
    { "[ISA] Paradise WD90C11-LR",                   "wd90c11",			&paradise_wd90c11_device,    		GFX_WD90C11			},
    { "[ISA] Paradise WD90C30-LR",                   "wd90c30",			&paradise_wd90c30_device,    		GFX_WD90C30			},
    { "[ISA] Plantronics ColorPlus",                 "plantronics",		&colorplus_device,           		GFX_COLORPLUS			},
#if defined(DEV_BRANCH) && defined(USE_TI)
    {"[ISA] TI CF62011 SVGA",                        "ti_cf62011",
     &ti_cf62011_device,                GFX_TICF62011			},
#endif
    { "[ISA] Trident TVGA8900D",                     "tvga8900d",		&tvga8900d_device,           		GFX_TVGA			},
    { "[ISA] Tseng ET4000AX",                        "et4000ax",			&et4000_device,              		GFX_ET4000			},
    {"[ISA] VGA",                                   "vga",			&vga_device,                 		GFX_VGA				},
    {"[ISA] Wyse 700",                              "wy700",			&wy700_device,               		GFX_WY700			},
    {"[PCI] ATI Graphics Pro Turbo (Mach64 GX)",    "mach64gx_pci",		&mach64gx_pci_device,        		GFX_MACH64GX_PCI		},
    {"[PCI] ATI Video Xpression (Mach64 VT2)",      "mach64vt2",		&mach64vt2_device,           		GFX_MACH64VT2			},
    {"[PCI] Cardex Tseng ET4000/w32p",		    "et4000w32p_pci",		&et4000w32p_cardex_pci_device,      	GFX_ET4000W32_CARDEX_PCI	},
#if defined(DEV_BRANCH) && defined(USE_STEALTH32)
    {"[PCI] Diamond Stealth 32 (Tseng ET4000/w32p)","stealth32_pci",		&et4000w32p_pci_device,      		GFX_ET4000W32_PCI		},
#endif
    {"[PCI] Diamond Stealth 3D 2000 (S3 ViRGE)",    "stealth3d_2000_pci",	&s3_virge_pci_device,            	GFX_VIRGE_PCI			},
    {"[PCI] Diamond Stealth 3D 3000 (S3 ViRGE/VX)", "stealth3d_3000_pci",	&s3_virge_988_pci_device,        	GFX_VIRGEVX_PCI			},
    {"[PCI] Diamond Stealth 64 DRAM (S3 Trio64)",   "stealth64d_pci",		&s3_diamond_stealth64_pci_device,	GFX_STEALTH64_PCI		},
#if defined(DEV_BRANCH) && defined(USE_RIVA)
    {"[PCI] nVidia RIVA 128",                       "riva128",			&riva128_device,             		GFX_RIVA128			},
    {"[PCI] nVidia RIVA TNT",                       "rivatnt",			&rivatnt_device,             		GFX_RIVATNT			},
    {"[PCI] nVidia RIVA TNT2",                      "rivatnt2",			&rivatnt2_device,            		GFX_RIVATNT2			},
#endif
    {"[PCI] Number Nine 9FX (S3 Trio64)",           "n9_9fx_pci",		&s3_9fx_pci_device,          		GFX_N9_9FX_PCI			},
    {"[PCI] Paradise Bahamas 64 (S3 Vision864)",    "bahamas64_pci",		&s3_bahamas64_pci_device,        	GFX_BAHAMAS64_PCI		},
    {"[PCI] Phoenix S3 Vision864",                  "px_vision864_pci",		&s3_phoenix_vision864_pci_device,	GFX_PHOENIX_VISION864_PCI	},
    {"[PCI] Phoenix S3 Trio32",                     "px_trio32_pci",		&s3_phoenix_trio32_pci_device,   	GFX_PHOENIX_TRIO32_PCI		},
    {"[PCI] Phoenix S3 Trio64",                     "px_trio64_pci",		&s3_phoenix_trio64_pci_device,   	GFX_PHOENIX_TRIO64_PCI		},
    {"[PCI] S3 ViRGE/DX",                           "virge375_pci",		&s3_virge_375_pci_device,        	GFX_VIRGEDX_PCI			},
    {"[PCI] S3 ViRGE/DX (VBE 2.0)",                 "virge375_vbe20_pci",	&s3_virge_375_4_pci_device,      	GFX_VIRGEDX4_PCI		},
    {"[PCI] Trident TGUI9440",                      "tgui9440_pci",		&tgui9440_pci_device,            	GFX_TGUI9440_PCI		},
    {"[VLB] ATI Graphics Pro Turbo (Mach64 GX)",    "mach64gx_vlb",		&mach64gx_vlb_device,        		GFX_MACH64GX_VLB		},
    {"[VLB] Cardex Tseng ET4000/w32p",		    "et4000w32p_vlb",		&et4000w32p_cardex_vlb_device,      	GFX_ET4000W32_CARDEX_VLB	},
#if defined(DEV_BRANCH) && defined(USE_CIRRUS)
    {"[VLB] Cirrus Logic CL-GD5429",		    "cl_gd5429",		&gd5429_device,				GFX_CL_GD5429			},
    {"[VLB] Cirrus Logic CL-GD5430",		    "cl_gd5430_vlb",		&dia5430_device,			GFX_CL_GD5430			},
    {"[VLB] Cirrus Logic CL-GD5446",		    "cl_gd5446",		&gd5446_device,				GFX_CL_GD5446			},
#endif
#if defined(DEV_BRANCH) && defined(USE_STEALTH32)
    {"[VLB] Diamond Stealth 32 (Tseng ET4000/w32p)","stealth32_vlb",		&et4000w32p_vlb_device,      		GFX_ET4000W32_VLB		},
#endif
    {"[VLB] Diamond Stealth 3D 2000 (S3 ViRGE)",    "stealth3d_2000_vlb",	&s3_virge_vlb_device,            	GFX_VIRGE_VLB			},
    {"[VLB] Diamond Stealth 3D 3000 (S3 ViRGE/VX)", "stealth3d_3000_vlb",	&s3_virge_988_vlb_device,        	GFX_VIRGEVX_VLB			},
    {"[VLB] Diamond Stealth 64 DRAM (S3 Trio64)",   "stealth64d_vlb",		&s3_diamond_stealth64_vlb_device,	GFX_STEALTH64_VLB		},
    {"[VLB] Number Nine 9FX (S3 Trio64)",           "n9_9fx_vlb",		&s3_9fx_vlb_device,          		GFX_N9_9FX_VLB			},
    {"[VLB] Paradise Bahamas 64 (S3 Vision864)",    "bahamas64_vlb",		&s3_bahamas64_vlb_device,        	GFX_BAHAMAS64_VLB		},
    {"[VLB] Phoenix S3 Vision864",                  "px_vision864_vlb",		&s3_phoenix_vision864_vlb_device,	GFX_PHOENIX_VISION864_VLB	},
    {"[VLB] Phoenix S3 Trio32",                     "px_trio32_vlb",		&s3_phoenix_trio32_vlb_device,   	GFX_PHOENIX_TRIO32_VLB		},
    {"[VLB] Phoenix S3 Trio64",                     "px_trio64_vlb",		&s3_phoenix_trio64_vlb_device,   	GFX_PHOENIX_TRIO64_VLB		},
    {"[VLB] S3 ViRGE/DX",                           "virge375_vlb",		&s3_virge_375_vlb_device,        	GFX_VIRGEDX_VLB			},
    {"[VLB] S3 ViRGE/DX (VBE 2.0)",                 "virge375_vbe20_vlb",	&s3_virge_375_4_vlb_device,      	GFX_VIRGEDX4_VLB		},
    {"[VLB] Trident TGUI9440",                      "tgui9440_vlb",		&tgui9440_vlb_device,            	GFX_TGUI9440_VLB		},
    {"",                                            "",				NULL,                        		-1				}
};


void
video_reset(int card)
{
    pclog("VIDEO: reset (romset=%d, gfxcard=%d, internal=%d)\n",
       	romset, card, (machines[machine].flags & MACHINE_VIDEO)?1:0);

    /* Reset the CGA palette. */
    cga_palette = 0;
    cgapal_rebuild();

    /* Do not initialize internal cards here. */
    if ((card == GFX_NONE) || \
	(card == GFX_INTERNAL) || machines[machine].fixed_gfxcard) return;

pclog("VIDEO: initializing '%s'\n", video_cards[video_old_to_new(card)].name);
    /* Initialize the video card. */
    device_add(video_cards[video_old_to_new(card)].device);

    /* Enable the Voodoo if configured. */
    if (voodoo_enabled)
       	device_add(&voodoo_device);
}


int
video_card_available(int card)
{
    if (video_cards[card].device)
	return(device_available(video_cards[card].device));

    return(1);
}


char *
video_card_getname(int card)
{
    return(video_cards[card].name);
}


device_t *
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


int
video_card_getid(char *s)
{
    int c = 0;

    while (video_cards[c].legacy_id != -1) {
	if (!strcmp(video_cards[c].name, s))
		return(c);
	c++;
    }

    return(0);
}


int
video_old_to_new(int card)
{
    int c = 0;

    while (video_cards[c].legacy_id != -1) {
	if (video_cards[c].legacy_id == card)
		return(c);
	c++;
    }

    return(0);
}


int
video_new_to_old(int card)
{
    return(video_cards[card].legacy_id);
}


char *
video_get_internal_name(int card)
{
    return(video_cards[card].internal_name);
}


int
video_get_video_from_internal_name(char *s)
{
    int c = 0;

    while (video_cards[c].legacy_id != -1) {
	if (!strcmp(video_cards[c].internal_name, s))
		return(video_cards[c].legacy_id);
	c++;
    }

    return(0);
}
