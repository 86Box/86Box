#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include "ibm.h"
#include "device.h"
#include "ide.h"
#include "allegro-main.h"
#include "allegro-gui.h"

static char hd_path[4][260];
static char hd_sectors[4][10];
static char hd_heads[4][10];
static char hd_cylinders[4][10];
static char hd_size[4][20];

static char hd_path_new[260];
static char hd_sectors_new[10];
static char hd_heads_new[10];
static char hd_cylinders_new[10];
static char hd_size_new[20];

static int new_cdrom_channel;

static PcemHDC hdc_new[4];

static DIALOG hdparams_dialog[]=
{
        {d_shadow_box_proc, 0, 0, 194*2,86,0,0xffffff,0,0,     0,0,0,0,0}, // 0

        {d_button_proc, 126,  66, 50, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "OK",     0, 0}, // 1
        {d_button_proc, 196,  66, 50, 16, 0, 0xffffff, 0, D_EXIT, 0, 0, "Cancel", 0, 0}, // 2

        {d_text_proc,   7*2,   6,  170,   10, 0, 0xffffff, 0, 0, 0, 0, "Initial settings are based on file size", 0, 0},

        {d_text_proc,   7*2,  22,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "Sectors:", 0, 0},
        {d_text_proc,  63*2,  22,  29,  8, 0, 0xffffff, 0, 0, 0, 0, "Heads:", 0, 0},
        {d_text_proc, 120*2,  22,  28, 12, 0, 0xffffff, 0, 0, 0, 0, "Cylinders:", 0, 0},
        {d_edit_proc,  44*2,  22,  16*2, 12, 0, 0xffffff, 0, 0, 2, 0, hd_sectors_new, 0, 0},
        {d_edit_proc,  92*2,  22,  16*2, 12, 0, 0xffffff, 0, 0, 3, 0, hd_heads_new, 0, 0},
        {d_edit_proc, 168*2,  22,  24*2, 12, 0, 0xffffff, 0, 0, 5, 0, hd_cylinders_new, 0, 0},
        {d_text_proc,   7*2,  54, 136, 12, 0, 0xffffff, 0, 0, 0, 0, hd_size_new, 0, 0},

        {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

static int hdconf_open(int msg, DIALOG *d, int c)
{
        int drv = d->d2;
        int ret = d_button_proc(msg, d, c);
        
        if (ret == D_EXIT)
        {
                char fn[260];
                int xsize = SCREEN_W - 32, ysize = SCREEN_H - 64;
                
                strcpy(fn, hd_path[drv]);
                ret = file_select_ex("Please choose a disc image", fn, "IMG", 260, xsize, ysize);
                if (ret)
                {
                        uint64_t sz;
                        FILE *f = fopen64(fn, "rb");
                        if (!f)
                        {
                                return D_REDRAW;
                        }
                        fseeko64(f, -1, SEEK_END);
                        sz = ftello64(f) + 1;
                        fclose(f);
                        sprintf(hd_sectors_new, "63");
                        sprintf(hd_heads_new, "16");
                        sprintf(hd_cylinders_new, "%i", (int)((sz / 512) / 16) / 63);

                        while (1)
                        {
                                position_dialog(hdparams_dialog, SCREEN_W/2 - 186, SCREEN_H/2 - 86/2);
        
                                ret = popup_dialog(hdparams_dialog, 1);

                                position_dialog(hdparams_dialog, -(SCREEN_W/2 - 186), -(SCREEN_H/2 - 86/2));
                        
                                if (ret == 1)
                                {
                                        int spt, hpc, cyl;
                                        sscanf(hd_sectors_new, "%i", &spt);
                                        sscanf(hd_heads_new, "%i", &hpc);
                                        sscanf(hd_cylinders_new, "%i", &cyl);
                                        
                                        if (spt > 63)
                                        {
                                                alert("Drive has too many sectors (maximum is 63)", NULL, NULL, "OK", NULL, 0, 0);
                                                continue;
                                        }
                                        if (hpc > 128)
                                        {
                                                alert("Drive has too many heads (maximum is 128)", NULL, NULL, "OK", NULL, 0, 0);
                                                continue;
                                        }
                                        if (cyl > 16383)
                                        {
                                                alert("Drive has too many cylinders (maximum is 16383)", NULL, NULL, "OK", NULL, 0, 0);
                                                continue;
                                        }
                                        
                                        hdc_new[drv].spt = spt;
                                        hdc_new[drv].hpc = hpc;
                                        hdc_new[drv].tracks = cyl;

                                        strcpy(hd_path[drv], fn);
                                        sprintf(hd_sectors[drv], "%i", hdc_new[drv].spt);
                                        sprintf(hd_heads[drv], "%i", hdc_new[drv].hpc);
                                        sprintf(hd_cylinders[drv], "%i", hdc_new[drv].tracks);
                                        sprintf(hd_size[drv], "Size : %imb", (((((uint64_t)hdc_new[drv].tracks*(uint64_t)hdc_new[drv].hpc)*(uint64_t)hdc_new[drv].spt)*512)/1024)/1024);

                                        return D_REDRAW;
                                }

                                if (ret == 2)
                                        break;
                        }
                }

                return D_REDRAW;
        }

        return ret;
}

static int hdconf_new_file(int msg, DIALOG *d, int c)
{
        int ret = d_button_proc(msg, d, c);
        
        if (ret == D_EXIT)
        {        
                char fn[260];
                int xsize = SCREEN_W - 32, ysize = SCREEN_H - 64;
                
                strcpy(fn, hd_path_new);
                ret = file_select_ex("Please choose a disc image", fn, "IMG", 260, xsize, ysize);
                if (ret)
                        strcpy(hd_path_new, fn);
                
                return D_REDRAW;
        }
        
        return ret;
}

static DIALOG hdnew_dialog[]=
{
        {d_shadow_box_proc, 0, 0, 194*2,86,0,0xffffff,0,0,     0,0,0,0,0}, // 0

        {d_button_proc, 126,  66, 50, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "OK",     0, 0}, // 1
        {d_button_proc, 196,  66, 50, 16, 0, 0xffffff, 0, D_EXIT, 0, 0, "Cancel", 0, 0}, // 2

        {d_edit_proc,   7*2,   6,  136*2, 10, 0, 0xffffff, 0, 0, 0, 0, hd_path_new, 0, 0},
        {hdconf_new_file, 143*2, 6,   16*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "...", 0, 0},
        
        {d_text_proc,   7*2,  22,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "Sectors:", 0, 0},
        {d_text_proc,  63*2,  22,  29,  8, 0, 0xffffff, 0, 0, 0, 0, "Heads:", 0, 0},
        {d_text_proc, 120*2,  22,  28, 12, 0, 0xffffff, 0, 0, 0, 0, "Cylinders:", 0, 0},
        {d_edit_proc,  44*2,  22,  16*2, 12, 0, 0xffffff, 0, 0, 2, 0, hd_sectors_new, 0, 0},
        {d_edit_proc,  92*2,  22,  16*2, 12, 0, 0xffffff, 0, 0, 3, 0, hd_heads_new, 0, 0},
        {d_edit_proc, 168*2,  22,  24*2, 12, 0, 0xffffff, 0, 0, 5, 0, hd_cylinders_new, 0, 0},
//        {d_text_proc,   7*2,  54, 136, 12, 0, -1, 0, 0, 0, 0, hd_size_new, 0, 0},

        {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

static int create_hd(char *fn, int cyl, int hpc, int spt)
{
	int c;
	int e;
	uint8_t buf[512];
	FILE *f = fopen64(hd_path_new, "wb");
	e = errno;
	if (!f)
	{
		alert("Can't open file for write", NULL, NULL, "OK", NULL, 0, 0);
		return -1;
	}
	memset(buf, 0, 512);
	for (c = 0; c < (cyl * hpc * spt); c++)
	{
		fwrite(buf, 512, 1, f);
	}
	fclose(f);
}

static int hdconf_new(int msg, DIALOG *d, int c)
{
        int drv = d->d2;
        int ret = d_button_proc(msg, d, c);
        
        if (ret == D_EXIT)
        {
                sprintf(hd_sectors_new, "63");
                sprintf(hd_heads_new, "16");
                sprintf(hd_cylinders_new, "511");
                strcpy(hd_path_new, "");

                while (1)
                {
                        position_dialog(hdnew_dialog, SCREEN_W/2 - 186, SCREEN_H/2 - 86/2);
        
                        ret = popup_dialog(hdnew_dialog, 1);

                        position_dialog(hdnew_dialog, -(SCREEN_W/2 - 186), -(SCREEN_H/2 - 86/2));
                
                        if (ret == 1)
                        {
                                int spt, hpc, cyl;
                                int c, d;
                                FILE *f;
				uint8_t *buf;
                                
                                sscanf(hd_sectors_new, "%i", &spt);
                                sscanf(hd_heads_new, "%i", &hpc);
                                sscanf(hd_cylinders_new, "%i", &cyl);
                        
                                if (spt > 63)
                                {
                                        alert("Drive has too many sectors (maximum is 63)", NULL, NULL, "OK", NULL, 0, 0);
                                        continue;
                                }
                                if (hpc > 128)
                                {
                                        alert("Drive has too many heads (maximum is 128)", NULL, NULL, "OK", NULL, 0, 0);
                                        continue;
                                }
                                if (cyl > 16383)
                                {
                                        alert("Drive has too many cylinders (maximum is 16383)", NULL, NULL, "OK", NULL, 0, 0);
                                        continue;
                                }
				if (create_hd(hd_path_new, cyl, hpc, spt))
					return D_REDRAW;

                                alert("Remember to partition and format the new drive", NULL, NULL, "OK", NULL, 0, 0);
                                
                                hdc_new[drv].spt = spt;
                                hdc_new[drv].hpc = hpc;
                                hdc_new[drv].tracks = cyl;

                                strcpy(hd_path[drv], hd_path_new);
                                sprintf(hd_sectors[drv], "%i", hdc_new[drv].spt);
                                sprintf(hd_heads[drv], "%i", hdc_new[drv].hpc);
                                sprintf(hd_cylinders[drv], "%i", hdc_new[drv].tracks);
                                sprintf(hd_size[drv], "Size : %imb", (((((uint64_t)hdc_new[drv].tracks*(uint64_t)hdc_new[drv].hpc)*(uint64_t)hdc_new[drv].spt)*512)/1024)/1024);

                                return D_REDRAW;
                        }
                        
                        if (ret == 2)
                                break;
                }
                
                return D_REDRAW;
        }
        
        return ret;
}

static int hdconf_eject(int msg, DIALOG *d, int c)
{
        int drv = d->d2;
        int ret = d_button_proc(msg, d, c);
        
        if (ret == D_EXIT)
        {
                hdc_new[drv].spt = 0;
                hdc_new[drv].hpc = 0;
                hdc_new[drv].tracks = 0;                
                strcpy(hd_path[drv], "");
                sprintf(hd_sectors[drv], "%i", hdc_new[drv].spt);
                sprintf(hd_heads[drv], "%i", hdc_new[drv].hpc);
                sprintf(hd_cylinders[drv], "%i", hdc_new[drv].tracks);
                sprintf(hd_size[drv], "Size : %imb", (((((uint64_t)hdc_new[drv].tracks*(uint64_t)hdc_new[drv].hpc)*(uint64_t)hdc_new[drv].spt)*512)/1024)/1024);
                
                return D_REDRAW;
        }

        return ret;
}

static int hdconf_radio_hd(int msg, DIALOG *d, int c);
static int hdconf_radio_cd(int msg, DIALOG *d, int c);

static DIALOG hdconf_dialog[]=
{
        {d_shadow_box_proc, 0, 0, 210*2,354,0,0xffffff,0,0,     0,0,0,0,0}, // 0

        {d_button_proc, 150,  334, 50, 16, 0, 0xffffff, 0, D_EXIT, 0, 0, "OK",     0, 0}, // 1
        {d_button_proc, 220,  334, 50, 16, 0, 0xffffff, 0, D_EXIT, 0, 0, "Cancel", 0, 0}, // 2

        {d_text_proc,   7*2,   6,  27,   10, 0, 0xffffff, 0, 0, 0, 0, "C:", 0, 0},
        {hdconf_radio_hd, 7*2,   22, 96,   12, 0, 0xffffff, 0, D_EXIT, 0, 0, "Hard drive", 0, 0}, // 4
        {hdconf_radio_cd, 100*2, 22, 64,   12, 0, 0xffffff, 0, D_EXIT, 0, 0, "CD-ROM", 0, 0}, // 5
        {d_edit_proc,   7*2,   38, 136*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_path[0], 0, 0},
        {hdconf_open,   143*2, 38, 16*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "...", 0, 0},
        {hdconf_new,    159*2, 38, 24*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "New", 0, 0},
        {hdconf_eject,  183*2, 38, 24*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "Eject", 0, 0},

        {d_text_proc,   7*2,  54,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "Sectors:", 0, 0},
        {d_text_proc,  63*2,  54,  29,  8, 0, 0xffffff, 0, 0, 0, 0, "Heads:", 0, 0},
        {d_text_proc, 120*2,  54,  28, 12, 0, 0xffffff, 0, 0, 0, 0, "Cylinders:", 0, 0},
        {d_edit_proc,  44*2,  54,  16*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_sectors[0], 0, 0},
        {d_edit_proc,  92*2,  54,  16*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_heads[0], 0, 0},
        {d_edit_proc, 168*2,  54,  24*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_cylinders[0], 0, 0},
        {d_text_proc,   7*2,  54, 136, 12, 0, 0xffffff, 0, 0, 0, 0, hd_size[0], 0, 0},

        {d_text_proc,   7*2,  76,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "D:", 0, 0},
        {hdconf_radio_hd, 7*2,   92, 96,   12, 0, 0xffffff, 0, D_EXIT, 1, 0, "Hard drive", 0, 0}, // 18
        {hdconf_radio_cd, 100*2, 92, 64,   12, 0, 0xffffff, 0, D_EXIT, 1, 0, "CD-ROM", 0, 0}, // 19
        {d_edit_proc,   7*2,   108, 136*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_path[1], 0, 0},
        {hdconf_open,   143*2, 108, 16*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 1, "...", 0, 0},
        {hdconf_new,    159*2, 108, 24*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 1, "New", 0, 0},
        {hdconf_eject,  183*2, 108, 24*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 1, "Eject", 0, 0},

        {d_edit_proc,  44*2, 124,  16*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_sectors[1], 0, 0},
        {d_edit_proc,  92*2, 124,  16*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_heads[1], 0, 0},
        {d_edit_proc, 168*2, 124,  24*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_cylinders[1], 0, 0},
        {d_text_proc,   7*2, 124,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "Sectors:", 0, 0},
        {d_text_proc,  63*2, 124,  29,  8, 0, 0xffffff, 0, 0, 0, 0, "Heads:", 0, 0},
        {d_text_proc, 120*2, 124,  32, 12, 0, 0xffffff, 0, 0, 0, 0, "Cylinders:", 0, 0},
        {d_text_proc,   7*2, 140, 136, 12, 0, 0xffffff, 0, 0, 0, 0, hd_size[1], 0, 0},

        {d_text_proc,   7*2,  162,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "E:", 0, 0},
        {hdconf_radio_hd, 7*2,   178, 96,   12, 0, 0xffffff, 0, D_EXIT, 2, 0, "Hard drive", 0, 0}, // 32
        {hdconf_radio_cd, 100*2, 178, 64,   12, 0, 0xffffff, 0, D_EXIT, 2, 0, "CD-ROM", 0, 0}, // 33
        {d_edit_proc,   7*2,   194, 136*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_path[2], 0, 0},
        {hdconf_open,   143*2, 194, 16*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 2, "...", 0, 0},
        {hdconf_new,    159*2, 194, 24*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 2, "New", 0, 0},
        {hdconf_eject,  183*2, 194, 24*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 2, "Eject", 0, 0},

        {d_edit_proc,  44*2, 210,  16*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_sectors[2], 0, 0},
        {d_edit_proc,  92*2, 210,  16*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_heads[2], 0, 0},
        {d_edit_proc, 168*2, 210,  24*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_cylinders[2], 0, 0},
        {d_text_proc,   7*2, 210,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "Sectors:", 0, 0},
        {d_text_proc,  63*2, 210,  29,  8, 0, 0xffffff, 0, 0, 0, 0, "Heads:", 0, 0},
        {d_text_proc, 120*2, 210,  32, 12, 0, 0xffffff, 0, 0, 0, 0, "Cylinders:", 0, 0},
        {d_text_proc,   7*2, 226, 136, 12, 0, 0xffffff, 0, 0, 0, 0, hd_size[2], 0, 0},

        {d_text_proc,   7*2,  248,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "F:", 0, 0},
        {hdconf_radio_hd, 7*2,   264, 96,   12, 0, 0xffffff, 0, D_EXIT, 3, 0, "Hard drive", 0, 0}, // 46
        {hdconf_radio_cd, 100*2, 264, 64,   12, 0, 0xffffff, 0, D_EXIT, 3, 0, "CD-ROM", 0, 0}, // 47
        {d_edit_proc,   7*2,   280, 136*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_path[3], 0, 0},
        {hdconf_open,   143*2, 280, 16*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 3, "...", 0, 0},
        {hdconf_new,    159*2, 280, 24*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 3, "New", 0, 0},
        {hdconf_eject,  183*2, 280, 24*2, 14, 0, 0xffffff, 0, D_EXIT, 0, 3, "Eject", 0, 0},

        {d_edit_proc,  44*2, 296,  16*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_sectors[3], 0, 0},
        {d_edit_proc,  92*2, 296,  16*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_heads[3], 0, 0},
        {d_edit_proc, 168*2, 296,  24*2, 12, 0, 0xffffff, 0, D_DISABLED, 0, 0, hd_cylinders[3], 0, 0},
        {d_text_proc,   7*2, 296,  27, 10, 0, 0xffffff, 0, 0, 0, 0, "Sectors:", 0, 0},
        {d_text_proc,  63*2, 296,  29,  8, 0, 0xffffff, 0, 0, 0, 0, "Heads:", 0, 0},
        {d_text_proc, 120*2, 296,  32, 12, 0, 0xffffff, 0, 0, 0, 0, "Cylinders:", 0, 0},
        {d_text_proc,   7*2, 312, 136, 12, 0, 0xffffff, 0, 0, 0, 0, hd_size[3], 0, 0},

        {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

static void update_hdd_cdrom()
{
        if (new_cdrom_channel == 0)
        {
                hdconf_dialog[4].flags  &= ~D_SELECTED;
                hdconf_dialog[5].flags  |= D_SELECTED;
        }
        else
        {
                hdconf_dialog[4].flags  |= D_SELECTED;
                hdconf_dialog[5].flags  &= ~D_SELECTED;
        }
        if (new_cdrom_channel == 1)
        {
                hdconf_dialog[18].flags  &= ~D_SELECTED;
                hdconf_dialog[19].flags  |= D_SELECTED;
        }
        else
        {
                hdconf_dialog[18].flags  |= D_SELECTED;
                hdconf_dialog[19].flags  &= ~D_SELECTED;
        }
        if (new_cdrom_channel == 2)
        {
                hdconf_dialog[32].flags  &= ~D_SELECTED;
                hdconf_dialog[33].flags  |= D_SELECTED;
        }
        else
        {
                hdconf_dialog[32].flags  |= D_SELECTED;
                hdconf_dialog[33].flags  &= ~D_SELECTED;
        }
        if (new_cdrom_channel == 3)
        {
                hdconf_dialog[46].flags  &= ~D_SELECTED;
                hdconf_dialog[47].flags  |= D_SELECTED;
        }
        else
        {
                hdconf_dialog[46].flags  |= D_SELECTED;
                hdconf_dialog[47].flags  &= ~D_SELECTED;
        }
}

static int hdconf_radio_hd(int msg, DIALOG *d, int c)
{
        int ret = d_radio_proc(msg, d, c);
        
        if (ret == D_CLOSE)
        {
                if (new_cdrom_channel == d->d1)
                {
                        new_cdrom_channel = -1;
                        update_hdd_cdrom();
                }
                
                return D_REDRAW;
        }
        
        return ret;
}
static int hdconf_radio_cd(int msg, DIALOG *d, int c)
{
        int ret = d_radio_proc(msg, d, c);
        
        if (ret == D_CLOSE)
        {
                if (new_cdrom_channel != d->d1)
                {                
                        new_cdrom_channel = d->d1;
                        update_hdd_cdrom();
                }
                
                return D_REDRAW;
        }
        
        return ret;
}

int disc_hdconf()
{
        int c;
        int changed=0;

        hdc_new[0] = hdc[0];
        hdc_new[1] = hdc[1];
        hdc_new[2] = hdc[2];
        hdc_new[3] = hdc[3];
        strcpy(hd_path[0], ide_fn[0]);
        strcpy(hd_path[1], ide_fn[1]);
        strcpy(hd_path[2], ide_fn[2]);
        strcpy(hd_path[3], ide_fn[3]);
        sprintf(hd_sectors[0], "%i", hdc[0].spt);
        sprintf(hd_sectors[1], "%i", hdc[1].spt);
        sprintf(hd_sectors[2], "%i", hdc[2].spt);
        sprintf(hd_sectors[3], "%i", hdc[3].spt);
        sprintf(hd_heads[0], "%i", hdc[0].hpc);
        sprintf(hd_heads[1], "%i", hdc[1].hpc);
        sprintf(hd_heads[2], "%i", hdc[2].hpc);
        sprintf(hd_heads[3], "%i", hdc[3].hpc);
        sprintf(hd_cylinders[0], "%i", hdc[0].tracks);
        sprintf(hd_cylinders[1], "%i", hdc[1].tracks);
        sprintf(hd_cylinders[2], "%i", hdc[2].tracks);
        sprintf(hd_cylinders[3], "%i", hdc[3].tracks);
        sprintf(hd_size[0], "Size : %imb", (((((uint64_t)hdc[0].tracks*(uint64_t)hdc[0].hpc)*(uint64_t)hdc[0].spt)*512)/1024)/1024);
        sprintf(hd_size[1], "Size : %imb", (((((uint64_t)hdc[1].tracks*(uint64_t)hdc[1].hpc)*(uint64_t)hdc[1].spt)*512)/1024)/1024);
        sprintf(hd_size[2], "Size : %imb", (((((uint64_t)hdc[2].tracks*(uint64_t)hdc[2].hpc)*(uint64_t)hdc[2].spt)*512)/1024)/1024);
        sprintf(hd_size[3], "Size : %imb", (((((uint64_t)hdc[3].tracks*(uint64_t)hdc[3].hpc)*(uint64_t)hdc[3].spt)*512)/1024)/1024);

        new_cdrom_channel = cdrom_channel;

        update_hdd_cdrom();

        while (1)
        {
                position_dialog(hdconf_dialog, SCREEN_W/2 - hdconf_dialog[0].w/2, SCREEN_H/2 - hdconf_dialog[0].h/2);
        
                c = popup_dialog(hdconf_dialog, 1);

                position_dialog(hdconf_dialog, -(SCREEN_W/2 - hdconf_dialog[0].w/2), -(SCREEN_H/2 - hdconf_dialog[0].h/2));

                if (c == 1)
                {
                        if (alert("This will reset PCem!", "Okay to continue?", NULL, "OK", "Cancel", 0, 0) == 1)
                        {
                                hdc[0] = hdc_new[0];
                                hdc[1] = hdc_new[1];
                                hdc[2] = hdc_new[2];
                                hdc[3] = hdc_new[3];
                
                                strcpy(ide_fn[0], hd_path[0]);
                                strcpy(ide_fn[1], hd_path[1]);
                                strcpy(ide_fn[2], hd_path[2]);
                                strcpy(ide_fn[3], hd_path[3]);
                                
                                cdrom_channel = new_cdrom_channel;

                                saveconfig();
                                                                                
                                resetpchard();
                                
                                return D_O_K;
                        }
                }
                if (c == 2)
                        return D_O_K;
        }
        
        return D_O_K;
}
