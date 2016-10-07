/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "ibm.h"
#include "config.h"
#include "device.h"
#include "mem.h"
#include "video.h"
#include "vid_svga.h"
#include "io.h"
#include "cpu.h"
#include "rom.h"
#include "thread.h"
#include "timer.h"

#include "vid_ati18800.h"
#include "vid_ati28800.h"
#include "vid_ati_mach64.h"
#include "vid_cga.h"
#include "vid_cl_ramdac.h" //vid_cl_gd.c needs this
#include "vid_cl_gd.h"
#include "vid_ega.h"
#include "vid_et4000.h"
#include "vid_et4000w32.h"
#include "vid_hercules.h"
#include "vid_incolor.h"
#include "vid_mda.h"
#include "vid_nv_riva128.h"
#include "vid_nv_rivatnt.h"
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

typedef struct
{
        char name[64];
        device_t *device;
        int legacy_id;
} VIDEO_CARD;

static VIDEO_CARD video_cards[] =
{
        {"ATI Graphics Pro Turbo (Mach64 GX)",     &mach64gx_device,            GFX_MACH64GX},
        {"ATI VGA Charger (ATI-28800-5)",          &ati28800_device,            GFX_VGACHARGER},
        {"ATI VGA Wonder XL24 (ATI-28800-6)",      &ati28800_wonderxl24_device, GFX_VGAWONDERXL24},
        {"ATI VGA Edge-16 (ATI-18800)",            &ati18800_device,            GFX_VGAEDGE16},
        {"Cardex 1703-DDC (ET4000/W32P)",          &et4000w32pc_device,         GFX_ET4000W32C},
        {"Cardex ICS5341 (ET4000/W32P)",           &et4000w32pcs_device,        GFX_ET4000W32CS},
        {"CGA",                                    &cga_device,                 GFX_CGA},
        {"Cirrus Logic CL-GD5429",                 &gd5429_device,              GFX_CL_GD5429},
        {"Diamond Stealth 32 (Tseng ET4000/w32p)", &et4000w32p_device,          GFX_ET4000W32},
        {"Diamond Stealth 64 DRAM (S3 Vision864)", &s3_diamond_stealth64_device,GFX_STEALTH64},
        {"Diamond Stealth 3D 2000 (S3 ViRGE)",     &s3_virge_device,            GFX_VIRGE},
        {"EGA",                                    &ega_device,                 GFX_EGA},
        {"Chips & Technologies SuperEGA",          &sega_device,           	GFX_SUPER_EGA},
        {"Compaq ATI VGA Wonder XL (ATI-28800-5)", &compaq_ati28800_device,     GFX_VGAWONDERXL},
        {"Compaq EGA",				   &cpqega_device,           	GFX_COMPAQ_EGA},
        {"Compaq/Paradise VGA",			   &cpqvga_device,           	GFX_COMPAQ_VGA},
        {"Hercules",                               &hercules_device,            GFX_HERCULES},
        {"Hercules InColor",                       &incolor_device,             GFX_INCOLOR},
        {"MDA",                                    &mda_device,                 GFX_MDA},
        {"Miro Crystal S3 Vision964",              &s3_miro_vision964_device,   GFX_MIRO_VISION964},
        {"Number Nine 9FX (S3 Trio64)",            &s3_9fx_device,              GFX_N9_9FX},
        {"nVidia RIVA 128 (Experimental)",         &riva128_device,             GFX_RIVA128},
        {"nVidia RIVA TNT (Experimental)",         &rivatnt_device,             GFX_RIVATNT},
        {"OAK OTI-067",                            &oti067_device,              GFX_OTI067},
        {"OAK OTI-077",                            &oti077_device,              GFX_OTI077},
        {"Paradise Bahamas 64 (S3 Vision864)",     &s3_bahamas64_device,        GFX_BAHAMAS64},
        {"Paradise WD90C11",                       &paradise_wd90c11_device,    GFX_WD90C11},
        {"Phoenix S3 Vision864",                   &s3_phoenix_vision864_device,GFX_PHOENIX_VISION864},
        {"Phoenix S3 Trio32",                      &s3_phoenix_trio32_device,   GFX_PHOENIX_TRIO32},
        {"Phoenix S3 Trio64",                      &s3_phoenix_trio64_device,   GFX_PHOENIX_TRIO64},
        {"S3 ViRGE/DX",                            &s3_virge_375_device,        GFX_VIRGEDX},
        {"Trident TVGA8900D",                      &tvga8900d_device,           GFX_TVGA},
        {"Tseng ET4000AX",                         &et4000_device,              GFX_ET4000},
        {"Trident TGUI9440",                       &tgui9440_device,            GFX_TGUI9440},
        {"VGA",                                    &vga_device,                 GFX_VGA},
        {"",                                       NULL,                        0}
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

int video_fullscreen = 0, video_fullscreen_scale, video_fullscreen_first;
uint32_t *video_15to32, *video_16to32;

int egareads=0,egawrites=0;
int changeframecount=2;

uint8_t rotatevga[8][256];

int frames = 0;

int fullchange;

uint8_t edatlookup[4][4];

int enable_overscan;
int overscan_x, overscan_y;
int force_43;
int enable_flash;

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

void video_updatetiming()
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

void video_init()
{
        pclog("Video_init %i %i\n",romset,gfxcard);

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

int xsize=1,ysize=1;

PALETTE cgapal;

void loadfont(char *s, int format)
{
        FILE *f=romfopen(s,"rb");
        int c,d;
        if (!f)
           return;

        if (!format)
        {
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
        }
        else if (format == 1)
        {
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
        }
        else
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                }
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

void initvideo()
{
        int c, d, e;

	/* Account for overscan. */
        buffer32 = create_bitmap(2064, 2056);

        buffer = create_bitmap(2064, 2056);

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
//                        printf("Edat %i,%i now %02X\n",c,d,edatlookup[c][d]);
                }
        }

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

void closevideo()
{
        thread_kill(blit_data.blit_thread);
        thread_destroy_event(blit_data.buffer_not_in_use);
        thread_destroy_event(blit_data.blit_complete);
        thread_destroy_event(blit_data.wake_blit_thread);

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

void video_blit_complete()
{
        blit_data.buffer_in_use = 0;
        thread_set_event(blit_data.buffer_not_in_use);
}

void video_wait_for_blit()
{
        while (blit_data.busy)
                thread_wait_event(blit_data.blit_complete, -1);
        thread_reset_event(blit_data.blit_complete);
}
void video_wait_for_buffer()
{
        while (blit_data.buffer_in_use)
                thread_wait_event(blit_data.buffer_not_in_use, -1);
        thread_reset_event(blit_data.buffer_not_in_use);
}

void video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
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
        video_wait_for_blit();
        blit_data.busy = 1;
        blit_data.x = x;
        blit_data.y = y;
        blit_data.w = w;
        blit_data.h = h;
        blit_data.blit8 = 1;
        thread_set_event(blit_data.wake_blit_thread);
}

#ifdef __unix
void d3d_fs_take_screenshot(char *fn)
{
}

void d3d_take_screenshot(char *fn)
{
}

void ddraw_fs_take_screenshot(char *fn)
{
}

void ddraw_take_screenshot(char *fn)
{
}

void take_screenshot()
{
}
#else
time_t now;
struct tm *info;
char screenshot_fn_partial[2048];
char screenshot_fn[4096];

void take_screenshot()
{
	if ((vid_api < 0) || (vid_api > 1))  return;
	time(&now);
	info = localtime(&now);
	memset(screenshot_fn, 0, 4096);
	memset(screenshot_fn_partial, 0, 2048);
	pclog("Video API is: %i\n", vid_api);
	if (vid_api == 1)
	{
		strftime(screenshot_fn_partial, 2048, "screenshots\\%Y%m%d_%H%M%S.png", info);
		append_filename(screenshot_fn, pcempath, screenshot_fn_partial, 4095);
		if (video_fullscreen)
		{
			d3d_fs_take_screenshot(screenshot_fn);
		}
		else
		{
			pclog("Direct 3D...\n");
			d3d_take_screenshot(screenshot_fn);
		}
	}
	else if (vid_api == 0)
	{
		strftime(screenshot_fn_partial, 1024, "screenshots\\%Y%m%d_%H%M%S.bmp", info);
		append_filename(screenshot_fn, pcempath, screenshot_fn_partial, 4095);
		if (video_fullscreen)
		{
			ddraw_fs_take_screenshot(screenshot_fn);
		}
		else
		{
			ddraw_take_screenshot(screenshot_fn);
		}
	}
}
#endif
