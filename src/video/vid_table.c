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
    const char		*name;
    const char		*internal_name;
    const device_t	*device;
} VIDEO_CARD;


static video_timings_t timing_default = {VIDEO_ISA, 8, 16, 32,   8, 16, 32};

static int was_reset = 0;


static const VIDEO_CARD
video_cards[] = {
    { "None",						"none",			NULL					},
    { "Internal",					"internal",		NULL					},
    { "[ISA] AMI S3 86c924",				"ami_s3_924",		&s3_ami_86c924_isa_device		},
    { "[ISA] ATI EGA Wonder 800+",			"egawonder800",		&atiega_device				},
    { "[ISA] ATI Graphics Pro Turbo (Mach64 GX)",	"mach64gx_isa",		&mach64gx_isa_device			},
    { "[ISA] ATI Korean VGA (ATI-28800-5)",		"ati28800k",		&ati28800k_device			},
    { "[ISA] ATI VGA-88 (ATI-18800-1)",			"ati18800v",		&ati18800_vga88_device			},
    { "[ISA] ATI VGA Charger (ATI-28800-5)",		"ati28800",		&ati28800_device			},
    { "[ISA] ATI VGA Edge-16 (ATI-18800-5)",		"ati18800",		&ati18800_device			},
#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
    { "[ISA] ATI VGA Wonder (ATI-18800)",		"ati18800w",		&ati18800_wonder_device			},
#endif
#if defined(DEV_BRANCH) && defined(USE_XL24)
    { "[ISA] ATI VGA Wonder XL24 (ATI-28800-6)",	"ati28800w",		&ati28800_wonderxl24_device		},
#endif
    { "[ISA] CGA",					"cga",			&cga_device				},
    { "[ISA] Chips & Technologies SuperEGA",		"superega",		&sega_device				},
    { "[ISA] Cirrus Logic CL-GD 5401",			"cl_gd5401_isa",	&gd5401_isa_device			},
    { "[ISA] Cirrus Logic CL-GD 5402",			"cl_gd5402_isa",	&gd5402_isa_device			},
    { "[ISA] Cirrus Logic CL-GD 5420",			"cl_gd5420_isa",	&gd5420_isa_device			},
#if defined(DEV_BRANCH) && defined(USE_CL5422)
    { "[ISA] Cirrus Logic CL-GD 5422",			"cl_gd5422_isa",	&gd5422_isa_device			},
#endif
    { "[ISA] Cirrus Logic CL-GD 5428",			"cl_gd5428_isa",	&gd5428_isa_device			},
    { "[ISA] Cirrus Logic CL-GD 5429",			"cl_gd5429_isa",	&gd5429_isa_device			},
    { "[ISA] Cirrus Logic CL-GD 5434",			"cl_gd5434_isa",	&gd5434_isa_device			},
    { "[ISA] Compaq ATI VGA Wonder XL (ATI-28800-5)",	"compaq_ati28800",	&compaq_ati28800_device			},
    { "[ISA] Compaq CGA",				"compaq_cga",		&compaq_cga_device			},
    { "[ISA] Compaq CGA 2",				"compaq_cga_2",		&compaq_cga_2_device			},
    { "[ISA] Compaq EGA",				"compaq_ega",		&cpqega_device				},
    { "[ISA] EGA",					"ega",			&ega_device				},
    { "[ISA] G2 GC205",					"g2_gc205",		&g2_gc205_device			},
    { "[ISA] Hercules",					"hercules",		&hercules_device			},
    { "[ISA] Hercules Plus",				"hercules_plus",	&herculesplus_device			},
    { "[ISA] Hercules InColor",				"incolor",		&incolor_device				},
    { "[ISA] Image Manager 1024",			"im1024",		&im1024_device				},
    { "[ISA] Kasan Hangulmadang-16 VGA (ET4000AX)",	"kasan16vga",		&et4000_kasan_isa_device		},
    { "[ISA] MDA",					"mda",			&mda_device				},
    { "[ISA] MDSI Genius",				"genius",		&genius_device				},
    { "[ISA] OAK OTI-037C",				"oti037c",		&oti037c_device				},
    { "[ISA] OAK OTI-067",				"oti067",		&oti067_device				},
    { "[ISA] OAK OTI-077",				"oti077",		&oti077_device				},
    { "[ISA] Orchid Fahrenheit 1280 (S3 86c911)",	"orchid_s3_911",	&s3_orchid_86c911_isa_device		},
    { "[ISA] Paradise PVGA1A",				"pvga1a",		&paradise_pvga1a_device			},
    { "[ISA] Paradise WD90C11-LR",			"wd90c11",		&paradise_wd90c11_device		},
    { "[ISA] Paradise WD90C30-LR",			"wd90c30",		&paradise_wd90c30_device		},
    { "[ISA] Plantronics ColorPlus",			"plantronics",		&colorplus_device			},
    { "[ISA] Professional Graphics Controller",		"pgc",			&pgc_device				},
    { "[ISA] Sigma Color 400",				"sigma400",		&sigma_device				},
    { "[ISA] SPEA V7 Mirage (S3 86c801)",		"px_s3_v7_801_isa",	&s3_v7mirage_86c801_isa_device		},
    { "[ISA] Trident TVGA8900B",			"tvga8900b",		&tvga8900b_device			},
    { "[ISA] Trident TVGA8900D",			"tvga8900d",		&tvga8900d_device			},
    { "[ISA] Trigem Korean VGA (ET4000AX)",		"tgkorvga",		&et4000k_isa_device			},
    { "[ISA] Tseng ET4000AX",				"et4000ax",		&et4000_isa_device			},
    { "[ISA] VGA",					"vga",			&vga_device				},
    { "[ISA] Video 7 VGA 1024i",			"v7_vga_1024i",		&v7_vga_1024i_device			},
    { "[ISA] Wyse 700",					"wy700",		&wy700_device				},
    { "[MCA] IBM 1MB SVGA Adapter/A (CL-GD 5428)",	"ibm1mbsvga",		&gd5428_mca_device			},
    { "[MCA] Tseng ET4000AX",				"et4000mca",		&et4000_mca_device			},
    { "[PCI] ATI Graphics Pro Turbo (Mach64 GX)",	"mach64gx_pci",		&mach64gx_pci_device			},
    { "[PCI] ATI Video Xpression (Mach64 VT2)",		"mach64vt2",		&mach64vt2_device			},
    { "[PCI] Cardex Tseng ET4000/w32p",			"et4000w32p_pci",	&et4000w32p_cardex_pci_device		},
    { "[PCI] Cirrus Logic CL-GD 5430",			"cl_gd5430_pci",	&gd5430_pci_device,			},
    { "[PCI] Cirrus Logic CL-GD 5434",			"cl_gd5434_pci",	&gd5434_pci_device			},
    { "[PCI] Cirrus Logic CL-GD 5436",			"cl_gd5436_pci",	&gd5436_pci_device			},
    { "[PCI] Cirrus Logic CL-GD 5440",			"cl_gd5440_pci",	&gd5440_pci_device			},
    { "[PCI] Cirrus Logic CL-GD 5446",			"cl_gd5446_pci",	&gd5446_pci_device			},
    { "[PCI] Cirrus Logic CL-GD 5480",			"cl_gd5480_pci",	&gd5480_pci_device			},
    { "[PCI] Diamond Stealth 32 (Tseng ET4000/w32p)",	"stealth32_pci",	&et4000w32p_pci_device			},
    { "[PCI] Diamond Stealth 3D 2000 (S3 ViRGE)",	"stealth3d_2000_pci",	&s3_virge_pci_device			},
    { "[PCI] Diamond Stealth 3D 3000 (S3 ViRGE/VX)",	"stealth3d_3000_pci",	&s3_virge_988_pci_device		},
    { "[PCI] Diamond Stealth 64 DRAM (S3 Trio64)",	"stealth64d_pci",	&s3_diamond_stealth64_pci_device	},
    { "[PCI] Diamond Stealth 64 VRAM (S3 Vision964)",	"stealth64v_pci",	&s3_diamond_stealth64_964_pci_device	},
#if defined(DEV_BRANCH) && defined(USE_MGA)
    { "[PCI] Matrox Mystique",				"mystique",		&mystique_device			},
    { "[PCI] Matrox Mystique 220",			"mystique_220",		&mystique_220_device			},
#endif
    { "[PCI] Number Nine 9FX (S3 Trio64)",		"n9_9fx_pci",		&s3_9fx_pci_device			},
    { "[PCI] Paradise Bahamas 64 (S3 Vision864)",	"bahamas64_pci",	&s3_bahamas64_pci_device		},
    { "[PCI] Phoenix S3 Vision864",			"px_vision864_pci",	&s3_phoenix_vision864_pci_device	},
    { "[PCI] Phoenix S3 Trio32",			"px_trio32_pci",	&s3_phoenix_trio32_pci_device		},
    { "[PCI] Phoenix S3 Trio64",			"px_trio64_pci",	&s3_phoenix_trio64_pci_device		},
    { "[PCI] S3 ViRGE/DX",				"virge375_pci",		&s3_virge_375_pci_device		},
    { "[PCI] S3 ViRGE/DX (VBE 2.0)",			"virge375_vbe20_pci",	&s3_virge_375_4_pci_device		},
    { "[PCI] STB Nitro 64V (CL-GD 5446)",		"cl_gd5446_stb_pci",	&gd5446_stb_pci_device			},
    { "[PCI] Trident TGUI9440",				"tgui9440_pci",		&tgui9440_pci_device			},
    { "[VLB] ATI Graphics Pro Turbo (Mach64 GX)",	"mach64gx_vlb",		&mach64gx_vlb_device			},
    { "[VLB] Cardex Tseng ET4000/w32p",			"et4000w32p_vlb",	&et4000w32p_cardex_vlb_device		},
#if defined(DEV_BRANCH) && defined(USE_CL5422)
    { "[VLB] Cirrus Logic CL-GD 5424",			"cl_gd5424_vlb",	&gd5424_vlb_device			},
#endif
    { "[VLB] Cirrus Logic CL-GD 5428",			"cl_gd5428_vlb",	&gd5428_vlb_device			},
    { "[VLB] Cirrus Logic CL-GD 5429",			"cl_gd5429_vlb",	&gd5429_vlb_device			},
    { "[VLB] Cirrus Logic CL-GD 5434",			"cl_gd5434_vlb",	&gd5434_vlb_device			},
    { "[VLB] Diamond Stealth 32 (Tseng ET4000/w32p)",	"stealth32_vlb",	&et4000w32p_vlb_device			},
    { "[VLB] Diamond SpeedStar PRO (CL-GD 5426)",	"cl_gd5426_vlb",	&gd5426_vlb_device			},
    { "[VLB] Diamond SpeedStar PRO SE (CL-GD 5430)",	"cl_gd5430_vlb",	&gd5430_vlb_device			},
    { "[VLB] Diamond Stealth 3D 2000 (S3 ViRGE)",	"stealth3d_2000_vlb",	&s3_virge_vlb_device			},
    { "[VLB] Diamond Stealth 3D 3000 (S3 ViRGE/VX)",	"stealth3d_3000_vlb",	&s3_virge_988_vlb_device		},
    { "[VLB] Diamond Stealth 64 DRAM (S3 Trio64)",	"stealth64d_vlb",	&s3_diamond_stealth64_vlb_device	},
    { "[VLB] Diamond Stealth 64 VRAM (S3 Vision964)",	"stealth64v_vlb",	&s3_diamond_stealth64_964_vlb_device	},
    { "[VLB] Number Nine 9FX (S3 Trio64)",		"n9_9fx_vlb",		&s3_9fx_vlb_device			},
    { "[VLB] Paradise Bahamas 64 (S3 Vision864)",	"bahamas64_vlb",	&s3_bahamas64_vlb_device		},
    { "[VLB] Phoenix S3 86c805",			"px_86c805_vlb",	&s3_phoenix_86c805_vlb_device		},
    { "[VLB] Phoenix S3 Vision864",			"px_vision864_vlb",	&s3_phoenix_vision864_vlb_device	},
    { "[VLB] Phoenix S3 Trio32",			"px_trio32_vlb",	&s3_phoenix_trio32_vlb_device		},
    { "[VLB] Phoenix S3 Trio64",			"px_trio64_vlb",	&s3_phoenix_trio64_vlb_device		},
    { "[VLB] S3 ViRGE/DX",				"virge375_vlb",		&s3_virge_375_vlb_device		},
    { "[VLB] S3 ViRGE/DX (VBE 2.0)",			"virge375_vbe20_vlb",	&s3_virge_375_4_vlb_device		},
    { "[VLB] Trident TGUI9400CXi",			"tgui9400cxi_vlb",	&tgui9400cxi_device			},
    { "[VLB] Trident TGUI9440",				"tgui9440_vlb",		&tgui9440_vlb_device			},
    { "",						"",			NULL                        		}
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

    loadfont(L"roms/video/mda/mda.rom", 0);

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
	!(card == VID_INTERNAL) && !(machines[machine].flags & MACHINE_VIDEO_FIXED)) {
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


char *
video_card_getname(int card)
{
    return((char *) video_cards[card].name);
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


int
video_card_getid(char *s)
{
    int c = 0;

    while (video_cards[c].name != NULL) {
	if (!strcmp((char *) video_cards[c].name, s))
		return(c);
	c++;
    }

    return(0);
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

    while (video_cards[c].name != NULL) {
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
