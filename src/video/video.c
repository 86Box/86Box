/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <time.h>
#include "../86box.h"
#include "../ibm.h"
#include "../cpu/cpu.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../config.h"
#include "../device.h"
#include "../timer.h"
#include "../win/plat_thread.h"
#include "../plat.h"
#include "video.h"
#include "vid_svga.h"
#ifndef __unix
# include "../win/win_cgapal.h"		/*YUCK*/
#endif


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


int vid_cga_contrast = 0;
int cga_palette = 0;

typedef struct
{
        char name[64];
	char internal_name[24];
        device_t *device;
        int legacy_id;
} VIDEO_CARD;

static VIDEO_CARD video_cards[] =
{
        {"[ISA] ATI VGA Charger (ATI-28800-5)",    	"ati28800",			&ati28800_device,            		GFX_VGACHARGER},
        {"[ISA] ATI VGA Wonder XL24 (ATI-28800-6)",	"ati28800w",			&ati28800_wonderxl24_device,		GFX_VGAWONDERXL24},
        {"[ISA] ATI VGA Edge-16 (ATI-18800)",      	"ati18800",			&ati18800_device,            		GFX_VGAEDGE16},
        {"[ISA] CGA",                              	"cga",				&cga_device,                 		GFX_CGA},
        {"[ISA] Chips & Technologies SuperEGA",    	"superega",			&sega_device,                		GFX_SUPER_EGA},
        {"[ISA] Compaq ATI VGA Wonder XL (ATI-28800-5)","compaq_ati28800",		&compaq_ati28800_device,     		GFX_VGAWONDERXL},
        {"[ISA] Compaq EGA",			   	"compaq_ega",			&cpqega_device,              		GFX_COMPAQ_EGA},
        {"[ISA] EGA",                              	"ega",				&ega_device,                 		GFX_EGA},
        {"[ISA] Hercules",                         	"hercules",			&hercules_device,            		GFX_HERCULES},
        {"[ISA] Hercules Plus",                    	"hercules_plus",		&herculesplus_device,        		GFX_HERCULESPLUS},
        {"[ISA] Hercules InColor",                 	"incolor",			&incolor_device,             		GFX_INCOLOR},
        {"[ISA] MDA",                              	"mda",				&mda_device,                 		GFX_MDA},
        {"[ISA] MDSI Genius",                      	"genius",            		&genius_device,              		GFX_GENIUS},
        {"[ISA] OAK OTI-067",                      	"oti067",			&oti067_device,              		GFX_OTI067},
        {"[ISA] OAK OTI-077",                      	"oti077",			&oti077_device,              		GFX_OTI077},
        {"[ISA] Paradise WD90C11",                 	"wd90c11",			&paradise_wd90c11_device,    		GFX_WD90C11},
        {"[ISA] Plantronics ColorPlus",            	"plantronics",			&colorplus_device,           		GFX_COLORPLUS},
        {"[ISA] Trident TVGA8900D",                	"tvga8900d",			&tvga8900d_device,           		GFX_TVGA},
        {"[ISA] Tseng ET4000AX",                   	"et4000ax",			&et4000_device,              		GFX_ET4000},
        {"[ISA] VGA",                              	"vga",				&vga_device,                 		GFX_VGA},
        {"[ISA] Wyse 700",                         	"wy700",			&wy700_device,               		GFX_WY700},
        {"[VLB] ATI Graphics Pro Turbo (Mach64 GX)",	"mach64x_vlb",			&mach64gx_vlb_device,        		GFX_MACH64GX_VLB},
        {"[VLB] Diamond Stealth 32 (Tseng ET4000/w32p)","stealth32_vlb",		&et4000w32p_vlb_device,      		GFX_ET4000W32_VLB},
        {"[VLB] Diamond Stealth 3D 2000 (S3 ViRGE)",    "stealth3d_2000_vlb",		&s3_virge_vlb_device,            	GFX_VIRGE_VLB},
        {"[VLB] Diamond Stealth 3D 3000 (S3 ViRGE/VX)", "stealth3d_3000_vlb",		&s3_virge_988_vlb_device,        	GFX_VIRGEVX_VLB},
        {"[VLB] Diamond Stealth 64 DRAM (S3 Trio64)",	"stealth64d_vlb",		&s3_diamond_stealth64_vlb_device,	GFX_STEALTH64_VLB},
        {"[VLB] Number Nine 9FX (S3 Trio64)",            "n9_9fx_vlb",			&s3_9fx_vlb_device,          		GFX_N9_9FX_VLB},
        {"[VLB] Paradise Bahamas 64 (S3 Vision864)",    "bahamas64_vlb",		&s3_bahamas64_vlb_device,        	GFX_BAHAMAS64_VLB},
        {"[VLB] Phoenix S3 Vision864",                  "px_vision864_vlb",		&s3_phoenix_vision864_vlb_device,	GFX_PHOENIX_VISION864_VLB},
        {"[VLB] Phoenix S3 Trio32",                     "px_trio32_vlb",		&s3_phoenix_trio32_vlb_device,   	GFX_PHOENIX_TRIO32_VLB},
        {"[VLB] Phoenix S3 Trio64",                     "px_trio64_vlb",		&s3_phoenix_trio64_vlb_device,   	GFX_PHOENIX_TRIO64_VLB},
        {"[VLB] S3 ViRGE/DX",                           "virge375_vlb",			&s3_virge_375_vlb_device,        	GFX_VIRGEDX_VLB},
        {"[VLB] S3 ViRGE/DX (VBE 2.0)",                 "virge375_vbe20_vlb",		&s3_virge_375_4_vlb_device,      	GFX_VIRGEDX4_VLB},
        {"[VLB] Trident TGUI9440",                      "tgui9440_vlb",			&tgui9440_vlb_device,            	GFX_TGUI9440_VLB},
        {"[PCI] ATI Graphics Pro Turbo (Mach64 GX)",	"mach64x_pci",			&mach64gx_pci_device,        		GFX_MACH64GX_PCI},
        {"[PCI] ATI Video Xpression (Mach64 VT2)",	"mach64vt2",			&mach64vt2_device,           		GFX_MACH64VT2},
        {"[PCI] Diamond Stealth 32 (Tseng ET4000/w32p)","stealth32_pci",		&et4000w32p_pci_device,      		GFX_ET4000W32_PCI},
        {"[PCI] Diamond Stealth 3D 2000 (S3 ViRGE)",    "stealth3d_2000_pci",		&s3_virge_pci_device,            	GFX_VIRGE_PCI},
        {"[PCI] Diamond Stealth 3D 3000 (S3 ViRGE/VX)", "stealth3d_3000_pci",		&s3_virge_988_pci_device,        	GFX_VIRGEVX_PCI},
        {"[PCI] Diamond Stealth 64 DRAM (S3 Trio64)",	"stealth64d_pci",		&s3_diamond_stealth64_pci_device,	GFX_STEALTH64_PCI},
#if defined(DEV_BRANCH) && defined(USE_RIVA)
        {"[PCI] nVidia RIVA 128",                  	"riva128",			&riva128_device,             		GFX_RIVA128},
        {"[PCI] nVidia RIVA TNT",                  	"rivatnt",			&rivatnt_device,             		GFX_RIVATNT},
        {"[PCI] nVidia RIVA TNT2",                 	"rivatnt2",			&rivatnt2_device,            		GFX_RIVATNT2},
#endif
        {"[PCI] Number Nine 9FX (S3 Trio64)",           "n9_9fx_pci",			&s3_9fx_pci_device,          		GFX_N9_9FX_PCI},
        {"[PCI] Paradise Bahamas 64 (S3 Vision864)",    "bahamas64_pci",		&s3_bahamas64_pci_device,        	GFX_BAHAMAS64_PCI},
        {"[PCI] Phoenix S3 Vision864",                  "px_vision864_pci",		&s3_phoenix_vision864_pci_device,	GFX_PHOENIX_VISION864_PCI},
        {"[PCI] Phoenix S3 Trio32",                     "px_trio32_pci",		&s3_phoenix_trio32_pci_device,   	GFX_PHOENIX_TRIO32_PCI},
        {"[PCI] Phoenix S3 Trio64",                     "px_trio64_pci",		&s3_phoenix_trio64_pci_device,   	GFX_PHOENIX_TRIO64_PCI},
        {"[PCI] S3 ViRGE/DX",                           "virge375_pci",			&s3_virge_375_pci_device,        	GFX_VIRGEDX_PCI},
        {"[PCI] S3 ViRGE/DX (VBE 2.0)",                 "virge375_vbe20_pci",		&s3_virge_375_4_pci_device,      	GFX_VIRGEDX4_PCI},
        {"[PCI] Trident TGUI9440",                      "tgui9440_pci",			&tgui9440_pci_device,            	GFX_TGUI9440_PCI},
        {"",                                       	"",				NULL,                        		0}
};

int video_card_available(int card)
{
        if (video_cards[card].device)
                return device_available(video_cards[card].device);

        return 1;
}

char *video_card_getname(int card)
{
        return video_cards[card].name;
}

device_t *video_card_getdevice(int card)
{
        return video_cards[card].device;
}

int video_card_has_config(int card)
{
        return video_cards[card].device->config ? 1 : 0;
}

int video_card_getid(char *s)
{
        int c = 0;

        while (video_cards[c].device)
        {
                if (!strcmp(video_cards[c].name, s))
                        return c;
                c++;
        }
        
        return 0;
}

int video_old_to_new(int card)
{
        int c = 0;
        
        while (video_cards[c].device)
        {
                if (video_cards[c].legacy_id == card)
                        return c;
                c++;
        }
        
        return 0;
}

int video_new_to_old(int card)
{
        return video_cards[card].legacy_id;
}

char *video_get_internal_name(int card)
{
        return video_cards[card].internal_name;
}

int video_get_video_from_internal_name(char *s)
{
	int c = 0;
	
	while (video_cards[c].legacy_id != -1)
	{
		if (!strcmp(video_cards[c].internal_name, s))
			return video_cards[c].legacy_id;
		c++;
	}
	
	return 0;
}

int video_fullscreen = 0, video_fullscreen_scale, video_fullscreen_first;
uint32_t *video_6to8, *video_15to32, *video_16to32;

int egareads=0,egawrites=0;
int changeframecount=2;

uint8_t rotatevga[8][256];

int frames = 0;

int fullchange;

uint8_t edatlookup[4][4];

int enable_overscan;
int overscan_x, overscan_y;

int force_43;

/*Video timing settings -

8-bit - 1mb/sec
        B = 8 ISA clocks
        W = 16 ISA clocks
        L = 32 ISA clocks
        
Slow 16-bit - 2mb/sec
        B = 6 ISA clocks
        W = 8 ISA clocks
        L = 16 ISA clocks

Fast 16-bit - 4mb/sec
        B = 3 ISA clocks
        W = 3 ISA clocks
        L = 6 ISA clocks
        
Slow VLB/PCI - 8mb/sec (ish)
        B = 4 bus clocks
        W = 8 bus clocks
        L = 16 bus clocks
        
Mid VLB/PCI -
        B = 4 bus clocks
        W = 5 bus clocks
        L = 10 bus clocks
        
Fast VLB/PCI -
        B = 3 bus clocks
        W = 3 bus clocks
        L = 4 bus clocks
*/

enum
{
        VIDEO_ISA = 0,
        VIDEO_BUS
};

int video_speed = 0;
int video_timing[6][4] =
{
        {VIDEO_ISA, 8, 16, 32},
        {VIDEO_ISA, 6,  8, 16},
        {VIDEO_ISA, 3,  3,  6},
        {VIDEO_BUS, 4,  8, 16},
        {VIDEO_BUS, 4,  5, 10},
        {VIDEO_BUS, 3,  3,  4}
};

void video_updatetiming(void)
{
        if (video_timing[video_speed][0] == VIDEO_ISA)
        {
                video_timing_b = (int)(isa_timing * video_timing[video_speed][1]);
                video_timing_w = (int)(isa_timing * video_timing[video_speed][2]);
                video_timing_l = (int)(isa_timing * video_timing[video_speed][3]);
        }
        else
        {
                video_timing_b = (int)(bus_timing * video_timing[video_speed][1]);
                video_timing_w = (int)(bus_timing * video_timing[video_speed][2]);
                video_timing_l = (int)(bus_timing * video_timing[video_speed][3]);
        }
        if (cpu_16bitbus)
           video_timing_l = video_timing_w * 2;
}

int video_timing_b, video_timing_w, video_timing_l;

int video_res_x, video_res_y, video_bpp;

void (*video_blit_memtoscreen_func)(int x, int y, int y1, int y2, int w, int h);
void (*video_blit_memtoscreen_8_func)(int x, int y, int w, int h);


void
video_reset(void)
{
        pclog("Video_reset(rom=%i, gfx=%i)\n", romset, gfxcard);

#ifndef __unix
	cga_palette = 0;
	cgapal_rebuild();
#endif

        switch (romset)
        {
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
        device_add(video_cards[video_old_to_new(gfxcard)].device);
}


BITMAP *buffer, *buffer32;

uint8_t fontdat[256][8];
uint8_t fontdatm[256][16];
uint8_t fontdatw[512][32];	/* Wyse700 font */
uint8_t fontdat8x12[256][16];	/* MDSI Genius font */

int xsize=1,ysize=1;

PALETTE cgapal;

void loadfont(wchar_t *s, int format)
{
        FILE *f=rom_fopen(s,L"rb");
        int c,d;
        if (!f)
	{
		return;
	}
	switch (format)
        {
		case 0:	/* MDA */
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f,4096+2048,SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                }
		break;
		case 1:	/* PC200 */
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                       	for (d=0;d<8;d++)
                        {
                                fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f, 4096, SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                        for (d=0;d<8;d++) getc(f);                
                }
		break;
		default:
		case 2:	/* CGA */
                for (c=0;c<256;c++)
                {
                       	for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                }
		break;
		case 3: /* Wyse 700 */
                for (c=0;c<512;c++)
                {
                        for (d=0;d<32;d++)
                        {
                                fontdatw[c][d]=getc(f);
                        }
                }
		break;
		case 4: /* MDSI Genius */
                for (c=0;c<256;c++)
                {
                        for (d=0;d<16;d++)
                        {
                                fontdat8x12[c][d]=getc(f);
                        }
                }
		break;

        }
        fclose(f);
}
 
static struct
{
        int x, y, y1, y2, w, h, blit8;
        int busy;
        int buffer_in_use;

        thread_t *blit_thread;
        event_t *wake_blit_thread;
        event_t *blit_complete;
        event_t *buffer_not_in_use;
} blit_data;

static void blit_thread(void *param);

int calc_6to8(int c)
{
	int ic, i8;
	double d8;
	ic = c;
	if (ic == 64)
	{
		ic = 63;
	}
	else
	{
		ic &= 0x3f;
	}
	d8 = (ic / 63.0) * 255.0;
	i8 = (int) d8;
	return i8 & 0xff;
}

int calc_15to32(int c)
{
	int b, g, r;
	double db, dg, dr;
	b = (c & 31);
	g = ((c >> 5) & 31);
	r = ((c >> 10) & 31);
	db = (((double) b) / 31.0) * 255.0;
	dg = (((double) g) / 31.0) * 255.0;
	dr = (((double) r) / 31.0) * 255.0;
	b = (int) db;
	g = ((int) dg) << 8;
	r = ((int) dr) << 16;
	return (b | g | r);
}

int calc_16to32(int c)
{
	int b, g, r;
	double db, dg, dr;
	b = (c & 31);
	g = ((c >> 5) & 63);
	r = ((c >> 11) & 31);
	db = (((double) b) / 31.0) * 255.0;
	dg = (((double) g) / 63.0) * 255.0;
	dr = (((double) r) / 31.0) * 255.0;
	b = (int) db;
	g = ((int) dg) << 8;
	r = ((int) dr) << 16;
	return (b | g | r);
}

void
video_init(void)
{
        int c, d, e;

	/* Account for overscan. */
        buffer32 = create_bitmap(2048, 2048);

        buffer = create_bitmap(2048, 2048);

        for (c = 0; c < 64; c++)
        {
                cgapal[c + 64].r = (((c & 4) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
                cgapal[c + 64].g = (((c & 2) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
                cgapal[c + 64].b = (((c & 1) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
                if ((c & 0x17) == 6) 
                        cgapal[c + 64].g >>= 1;
        }
        for (c = 0; c < 64; c++)
        {
                cgapal[c + 128].r = (((c & 4) ? 2 : 0) | ((c & 0x20) ? 1 : 0)) * 21;
                cgapal[c + 128].g = (((c & 2) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
                cgapal[c + 128].b = (((c & 1) ? 2 : 0) | ((c & 0x08) ? 1 : 0)) * 21;
        }

        for (c = 0; c < 256; c++)
        {
                e = c;
                for (d = 0; d < 8; d++)
                {
                        rotatevga[d][c] = e;
                        e = (e >> 1) | ((e & 1) ? 0x80 : 0);
                }
        }
        for (c = 0; c < 4; c++)
        {
                for (d = 0; d < 4; d++)
                {
                        edatlookup[c][d] = 0;
                        if (c & 1) edatlookup[c][d] |= 1;
                        if (d & 1) edatlookup[c][d] |= 2;
                        if (c & 2) edatlookup[c][d] |= 0x10;
                        if (d & 2) edatlookup[c][d] |= 0x20;
                }
        }

	video_6to8 = malloc(4 * 256);
	for (c = 0; c < 256; c++)
		video_6to8[c] = calc_6to8(c);

        video_15to32 = malloc(4 * 65536);
#if 0
        for (c = 0; c < 65536; c++)
                video_15to32[c] = ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19);
#endif
        for (c = 0; c < 65536; c++)
                video_15to32[c] = calc_15to32(c);

        video_16to32 = malloc(4 * 65536);
#if 0
        for (c = 0; c < 65536; c++)
                video_16to32[c] = ((c & 31) << 3) | (((c >> 5) & 63) << 10) | (((c >> 11) & 31) << 19);
#endif
        for (c = 0; c < 65536; c++)
                video_16to32[c] = calc_16to32(c);
 
        blit_data.wake_blit_thread = thread_create_event();
        blit_data.blit_complete = thread_create_event();
        blit_data.buffer_not_in_use = thread_create_event();
        blit_data.blit_thread = thread_create(blit_thread, NULL);
}

void
video_close(void)
{
        thread_kill(blit_data.blit_thread);
        thread_destroy_event(blit_data.buffer_not_in_use);
        thread_destroy_event(blit_data.blit_complete);
        thread_destroy_event(blit_data.wake_blit_thread);

        free(video_6to8);
        free(video_15to32);
        free(video_16to32);
        destroy_bitmap(buffer);
        destroy_bitmap(buffer32);
}


static void blit_thread(void *param)
{
        while (1)
        {
                thread_wait_event(blit_data.wake_blit_thread, -1);
                thread_reset_event(blit_data.wake_blit_thread);
                
                if (blit_data.blit8)
                        video_blit_memtoscreen_8_func(blit_data.x, blit_data.y, blit_data.w, blit_data.h);
                else
                        video_blit_memtoscreen_func(blit_data.x, blit_data.y, blit_data.y1, blit_data.y2, blit_data.w, blit_data.h);
                
                blit_data.busy = 0;
                thread_set_event(blit_data.blit_complete);
        }
}

void video_blit_complete(void)
{
        blit_data.buffer_in_use = 0;
        thread_set_event(blit_data.buffer_not_in_use);
}

void video_wait_for_blit(void)
{
        while (blit_data.busy)
                thread_wait_event(blit_data.blit_complete, -1);
        thread_reset_event(blit_data.blit_complete);
}
void video_wait_for_buffer(void)
{
        while (blit_data.buffer_in_use)
                thread_wait_event(blit_data.buffer_not_in_use, -1);
        thread_reset_event(blit_data.buffer_not_in_use);
}

void video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        if (h <= 0)
                return;
        video_wait_for_blit();
        blit_data.busy = 1;
        blit_data.buffer_in_use = 1;
        blit_data.x = x;
        blit_data.y = y;
        blit_data.y1 = y1;
        blit_data.y2 = y2;
        blit_data.w = w;
        blit_data.h = h;
        blit_data.blit8 = 0;
        thread_set_event(blit_data.wake_blit_thread);
}

void video_blit_memtoscreen_8(int x, int y, int w, int h)
{
        if (h <= 0)
                return;
        video_wait_for_blit();
        blit_data.busy = 1;
        blit_data.x = x;
        blit_data.y = y;
        blit_data.w = w;
        blit_data.h = h;
        blit_data.blit8 = 1;
        thread_set_event(blit_data.wake_blit_thread);
}


void
take_screenshot(void)
{
    wchar_t path[1024], fn[128];
    struct tm *info;
    time_t now;

    pclog("Screenshot: video API is: %i\n", vid_api);
    if ((vid_api < 0) || (vid_api > 1)) return;

    memset(fn, 0, sizeof(fn));
    memset(path, 0, sizeof(path));

    (void)time(&now);
    info = localtime(&now);

    append_filename_w(path, cfg_path, SCREENSHOT_PATH, sizeof(path)-2);

    if (! dir_check_exist(path))
	dir_create(path);

#ifdef WIN32
    wcscat(path, L"\\");
#else
    wcscat(path, L"/");
#endif

    if (vid_api == 1) {
	wcsftime(fn, 128, L"%Y%m%d_%H%M%S.png", info);
	append_filename_w(path, cfg_path, fn, 1024);
	if (video_fullscreen)
		d3d_fs_take_screenshot(path);
	  else
		d3d_take_screenshot(path);
    } else if (vid_api == 0) {
	wcsftime(path, 128, L"%Y%m%d_%H%M%S.bmp", info);
	append_filename_w(path, cfg_path, fn, 1024);
	if (video_fullscreen)
		ddraw_fs_take_screenshot(path);
	  else
		ddraw_take_screenshot(path);
    }

    pclog("Screenshot: fn='%ws'\n", path);
}
