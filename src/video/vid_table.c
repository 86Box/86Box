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
 * Version:	@(#)vid_table.c	1.0.1	2017/10/10
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
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
#include "vid_olivetti_m24.h"
#include "vid_oti067.h"
#include "vid_paradise.h"
#include "vid_pc1512.h"
#include "vid_pc1640.h"
#include "vid_pc200.h"
#include "vid_pcjr.h"
#include "vid_ps1_svga.h"
#include "vid_s3.h"
#include "vid_s3_virge.h"
#include "vid_tandy.h"
#include "vid_tandysl.h"
#include "vid_tgui9440.h"
#include "vid_tvga.h"
#include "vid_vga.h"
#include "vid_wy700.h"


typedef struct {
    char	name[64];
    char	internal_name[24];
    device_t	*device;
    int		legacy_id;
} VIDEO_CARD;


static VIDEO_CARD
video_cards[] = {
    {"[ISA] ATI VGA Charger (ATI-28800-5)",         "ati28800",			&ati28800_device,            		GFX_VGACHARGER			},
    {"[ISA] ATI VGA Wonder XL24 (ATI-28800-6)",     "ati28800w",		&ati28800_wonderxl24_device,		GFX_VGAWONDERXL24		},
    {"[ISA] ATI VGA Edge-16 (ATI-18800)",           "ati18800",			&ati18800_device,            		GFX_VGAEDGE16			},
    {"[ISA] CGA",                                   "cga",			&cga_device,                 		GFX_CGA				},
    {"[ISA] Chips & Technologies SuperEGA",         "superega",			&sega_device,                		GFX_SUPER_EGA			},
    {"[ISA] Compaq ATI VGA Wonder XL (ATI-28800-5)","compaq_ati28800",		&compaq_ati28800_device,     		GFX_VGAWONDERXL			},
    {"[ISA] Compaq EGA",                            "compaq_ega",		&cpqega_device,              		GFX_COMPAQ_EGA			},
    {"[ISA] EGA",                                   "ega",			&ega_device,                 		GFX_EGA				},
    {"[ISA] Hercules",                              "hercules",			&hercules_device,            		GFX_HERCULES			},
    {"[ISA] Hercules Plus",                         "hercules_plus",		&herculesplus_device,        		GFX_HERCULESPLUS		},
    {"[ISA] Hercules InColor",                      "incolor",			&incolor_device,             		GFX_INCOLOR			},
    {"[ISA] MDA",                                   "mda",			&mda_device,                 		GFX_MDA				},
    {"[ISA] MDSI Genius",                           "genius",            	&genius_device,              		GFX_GENIUS			},
    {"[ISA] OAK OTI-067",                           "oti067",			&oti067_device,              		GFX_OTI067			},
    {"[ISA] OAK OTI-077",                           "oti077",			&oti077_device,              		GFX_OTI077			},
    {"[ISA] Paradise WD90C11",                      "wd90c11",			&paradise_wd90c11_device,    		GFX_WD90C11			},
    {"[ISA] Plantronics ColorPlus",                 "plantronics",		&colorplus_device,           		GFX_COLORPLUS			},
    {"[ISA] Trident TVGA8900D",                     "tvga8900d",		&tvga8900d_device,           		GFX_TVGA			},
    {"[ISA] Tseng ET4000AX",                        "et4000ax",			&et4000_device,              		GFX_ET4000			},
    {"[ISA] VGA",                                   "vga",			&vga_device,                 		GFX_VGA				},
    {"[ISA] Wyse 700",                              "wy700",			&wy700_device,               		GFX_WY700			},
    {"[VLB] ATI Graphics Pro Turbo (Mach64 GX)",    "mach64x_vlb",		&mach64gx_vlb_device,        		GFX_MACH64GX_VLB		},
    {"[VLB] Diamond Stealth 32 (Tseng ET4000/w32p)","stealth32_vlb",		&et4000w32p_vlb_device,      		GFX_ET4000W32_VLB		},
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
    {"[PCI] ATI Graphics Pro Turbo (Mach64 GX)",    "mach64x_pci",		&mach64gx_pci_device,        		GFX_MACH64GX_PCI		},
    {"[PCI] ATI Video Xpression (Mach64 VT2)",      "mach64vt2",		&mach64vt2_device,           		GFX_MACH64VT2			},
    {"[PCI] Diamond Stealth 32 (Tseng ET4000/w32p)","stealth32_pci",		&et4000w32p_pci_device,      		GFX_ET4000W32_PCI		},
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
    {"",                                            "",				NULL,                        		0				}
};


/* This will be merged into machine.c soon. --FvK */
void
video_reset_device(int rs, int gc)
{
    pclog("Video_reset_device(rom=%i, gfx=%i)\n", rs, gc);

    switch (rs) {
	case ROM_IBMPCJR:
		device_add(&pcjr_video_device);
		return;

	case ROM_TANDY:
	case ROM_TANDY1000HX:
		device_add(&tandy_device);
		return;

	case ROM_TANDY1000SL2:
		device_add(&tandysl_device);
		return;

	case ROM_PC1512:
		device_add(&pc1512_device);
		return;
		
	case ROM_PC1640:
		device_add(&pc1640_device);
		return;
		
	case ROM_PC200:
		device_add(&pc200_device);
		return;
		
	case ROM_OLIM24:
		device_add(&m24_device);
		return;

	case ROM_PC2086:
		device_add(&paradise_pvga1a_pc2086_device);
		return;

	case ROM_PC3086:
		device_add(&paradise_pvga1a_pc3086_device);
		return;

	case ROM_MEGAPC:
		device_add(&paradise_wd90c11_megapc_device);
		return;
			
	case ROM_ACER386:
		device_add(&oti067_device);
		return;
		
	case ROM_IBMPS1_2011:
	case ROM_IBMPS2_M30_286:
	case ROM_IBMPS2_M50:
	case ROM_IBMPS2_M55SX:
	case ROM_IBMPS2_M80:
		device_add(&ps1vga_device);
		return;

	case ROM_IBMPS1_2121:
		device_add(&ps1_m2121_svga_device);
		return;
    }

    device_add(video_cards[video_old_to_new(gc)].device);
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
    return(video_cards[card].device->config ? 1 : 0);
}


int
video_card_getid(char *s)
{
    int c = 0;

    while (video_cards[c].device) {
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

    while (video_cards[c].device) {
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
