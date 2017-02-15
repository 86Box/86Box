/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include "ibm.h"

#include "config.h"
#include "disc.h"
#include "disc_fdi.h"
#include "disc_img.h"
#include "disc_86f.h"
#include "disc_td0.h"
#include "disc_imd.h"
#include "fdc.h"
#include "fdd.h"
#include "timer.h"

int disc_poll_time[FDD_NUM] = { 16, 16, 16, 16 };

int disc_track[FDD_NUM];
int writeprot[FDD_NUM], fwriteprot[FDD_NUM];

DRIVE drives[FDD_NUM];
int drive_type[FDD_NUM];

int curdrive = 0;

int swwp = 0;
int disable_write = 0;

int defaultwriteprot = 0;

int fdc_time;
int disc_time;

int fdc_ready;

int drive_empty[FDD_NUM] = {1, 1, 1, 1};
int disc_changed[FDD_NUM];

int motorspin;
int motoron[FDD_NUM];

int fdc_indexcount = 52;

/*void (*fdc_callback)();
void (*fdc_data)(uint8_t dat);
void (*fdc_spindown)();
void (*fdc_finishread)();
void (*fdc_notfound)();
void (*fdc_datacrcerror)();
void (*fdc_headercrcerror)();
void (*fdc_writeprotect)();
int  (*fdc_getdata)(int last);
void (*fdc_sectorid)(uint8_t track, uint8_t side, uint8_t sector, uint8_t size, uint8_t crc1, uint8_t crc2);
void (*fdc_indexpulse)();*/

static struct
{
        char *ext;
        void (*load)(int drive, char *fn);
        void (*close)(int drive);
        int size;
}
loaders[]=
{
        {"001", img_load,       img_close, -1},
        {"002", img_load,       img_close, -1},
        {"003", img_load,       img_close, -1},
        {"004", img_load,       img_close, -1},
        {"005", img_load,       img_close, -1},
        {"006", img_load,       img_close, -1},
        {"007", img_load,       img_close, -1},
        {"008", img_load,       img_close, -1},
        {"009", img_load,       img_close, -1},
        {"010", img_load,       img_close, -1},
        {"12",  img_load,       img_close, -1},
        {"144", img_load,       img_close, -1},
        {"360", img_load,       img_close, -1},
        {"720", img_load,       img_close, -1},
        {"86F", d86f_load,     d86f_close, -1},
        {"BIN", img_load,       img_close, -1},
        {"CQ", img_load,        img_close, -1},
        {"CQM", img_load,       img_close, -1},
        {"DSK", img_load,       img_close, -1},
        {"FDI", fdi_load,       fdi_close, -1},
        {"FLP", img_load,       img_close, -1},
        {"HDM", img_load,       img_close, -1},
        {"IMA", img_load,       img_close, -1},
        {"IMD", imd_load,       imd_close, -1},
        {"IMG", img_load,       img_close, -1},
	{"TD0", td0_load,       td0_close, -1},
        {"VFD", img_load,       img_close, -1},
	{"XDF", img_load,       img_close, -1},
        {0,0,0}
};

static int driveloaders[4];

void disc_load(int drive, char *fn)
{
        int c = 0, size;
        char *p;
        FILE *f;
//        pclog("disc_load %i %s\n", drive, fn);
//        setejecttext(drive, "");
        if (!fn) return;
        p = get_extension(fn);
        if (!p) return;
//        setejecttext(drive, fn);
        // pclog("Loading :%i %s %s\n", drive, fn,p);
        f = fopen(fn, "rb");
        if (!f) return;
        fseek(f, -1, SEEK_END);
        size = ftell(f) + 1;
        fclose(f);        
        while (loaders[c].ext)
        {
                if (!strcasecmp(p, loaders[c].ext) && (size == loaders[c].size || loaders[c].size == -1))
                {
                        // pclog("Loading as %s (UI write protected = %s)\n", p, ui_writeprot[drive] ? "yes" : "no");
                        driveloaders[drive] = c;
                        loaders[c].load(drive, fn);
                        drive_empty[drive] = 0;
                        strcpy(discfns[drive], fn);
			// fdd_set_head(real_drive(drive), 0);
                        fdd_forced_seek(real_drive(drive), 0);
                        disc_changed[drive] = 1;
                        return;
                }
                c++;
        }
        pclog("Couldn't load %s %s\n",fn,p);
        drive_empty[drive] = 1;
	fdd_set_head(real_drive(drive), 0);
        discfns[drive][0] = 0;
}

void disc_close(int drive)
{
//        pclog("disc_close %i\n", drive);
        if (loaders[driveloaders[drive]].close) loaders[driveloaders[drive]].close(drive);
        drive_empty[drive] = 1;
	fdd_set_head(real_drive(drive), 0);
        discfns[drive][0] = 0;
        drives[drive].hole = NULL;
        drives[drive].poll = NULL;
        drives[drive].seek = NULL;
        drives[drive].readsector = NULL;
        drives[drive].writesector = NULL;
        drives[drive].comparesector = NULL;
        drives[drive].readaddress = NULL;
        drives[drive].format = NULL;
        drives[drive].byteperiod = NULL;
	drives[drive].stop = NULL;
}

int disc_notfound=0;
static int disc_period = 32;

int disc_hole(int drive)
{
	drive = real_drive(drive);

	if (drives[drive].hole)
	{
		return drives[drive].hole(drive);
	}
	else
	{
		return 0;
	}
}

double disc_byteperiod(int drive)
{
	drive = real_drive(drive);

	if (drives[drive].byteperiod)
	{
		return drives[drive].byteperiod(drive);
	}
	else
	{
		return 32.0;
	}
}

double disc_real_period(int drive)
{
	double ddbp;
	double dusec;

	ddbp = disc_byteperiod(real_drive(drive));

	dusec = (double) TIMER_USEC;

	/* This is a giant hack but until the timings become even more correct, this is needed to make floppies work right on that BIOS. */
	if (romset == ROM_MRTHOR)
	{
		return (ddbp * dusec) / 4.0;
	}
	else
	{
		return (ddbp * dusec);
	}
}

void disc_poll(int drive)
{
	if (drive >= FDD_NUM)
	{
		disc_poll_time[drive] += (int) (((romset == ROM_MRTHOR) ? 8.0 : 32.0) * TIMER_USEC);
		return;
	}

        disc_poll_time[drive] += (int) disc_real_period(drive);

        if (drives[drive].poll)
                drives[drive].poll(drive);

        if (disc_notfound)
        {
                disc_notfound--;
                if (!disc_notfound)
                        fdc_noidam();
        }
}

void disc_poll_0()
{
	disc_poll(0);
}

void disc_poll_1()
{
	disc_poll(1);
}

void disc_poll_2()
{
	disc_poll(2);
}

void disc_poll_3()
{
	disc_poll(3);
}

int disc_get_bitcell_period(int rate)
{
        int bit_rate;
        
        switch (rate)
        {
                case 0: /*High density*/
                bit_rate = 500;
                break;
                case 1: /*Double density (360 rpm)*/
                bit_rate = 300;
                break;
                case 2: /*Double density*/
                bit_rate = 250;
                break;
                case 3: /*Extended density*/
                bit_rate = 1000;
                break;
        }
        
        return 1000000 / bit_rate*2; /*Bitcell period in ns*/
}


void disc_set_rate(int drive, int drvden, int rate)
{
        switch (rate)
        {
                case 0: /*High density*/
                disc_period = 16;
                break;
                case 1:
		switch(drvden)
		{
			case 0: /*Double density (360 rpm)*/
		                disc_period = 26;
		                break;
			case 1: /*High density (360 rpm)*/
		                disc_period = 16;
		                break;
			case 2:
		                disc_period = 4;
		                break;
		}
                case 2: /*Double density*/
                disc_period = 32;
                break;
                case 3: /*Extended density*/
                disc_period = 8;
                break;
        }
}

void disc_reset()
{
        curdrive = 0;
        disc_period = 32;
	timer_add(disc_poll_0, &(disc_poll_time[0]), &(motoron[0]), NULL);
	timer_add(disc_poll_1, &(disc_poll_time[1]), &(motoron[1]), NULL);
	timer_add(disc_poll_2, &(disc_poll_time[2]), &(motoron[2]), NULL);
	timer_add(disc_poll_3, &(disc_poll_time[3]), &(motoron[3]), NULL);
}

void disc_init()
{
//        pclog("disc_init %p\n", drives);
        drives[0].poll = drives[1].poll = drives[2].poll = drives[3].poll = 0;
        drives[0].seek = drives[1].seek = drives[2].seek = drives[3].seek = 0;
        drives[0].readsector = drives[1].readsector = drives[2].readsector = drives[3].readsector = 0;
        disc_reset();
}

int oldtrack[FDD_NUM] = {0, 0, 0, 0};
void disc_seek(int drive, int track)
{
//        pclog("disc_seek: drive=%i track=%i\n", drive, track);
        if (drives[drive].seek)
                drives[drive].seek(drive, track);
//        if (track != oldtrack[drive])
//                fdc_discchange_clear(drive);
//        ddnoise_seek(track - oldtrack[drive]);
//        oldtrack[drive] = track;
}

void disc_readsector(int drive, int sector, int track, int side, int density, int sector_size)
{
        drive = real_drive(drive);

        if (drives[drive].readsector)
                drives[drive].readsector(drive, sector, track, side, density, sector_size);
        else
                disc_notfound = 1000;
}

void disc_writesector(int drive, int sector, int track, int side, int density, int sector_size)
{
        drive = real_drive(drive);

        if (drives[drive].writesector)
                drives[drive].writesector(drive, sector, track, side, density, sector_size);
        else
                disc_notfound = 1000;
}

void disc_comparesector(int drive, int sector, int track, int side, int density, int sector_size)
{
        drive = real_drive(drive);

        if (drives[drive].comparesector)
                drives[drive].comparesector(drive, sector, track, side, density, sector_size);
        else
                disc_notfound = 1000;
}

void disc_readaddress(int drive, int side, int density)
{
        drive = real_drive(drive);

        if (drives[drive].readaddress)
                drives[drive].readaddress(drive, side, density);
}

void disc_format(int drive, int side, int density, uint8_t fill)
{
        drive = real_drive(drive);
        
        if (drives[drive].format)
                drives[drive].format(drive, side, density, fill);
        else
                disc_notfound = 1000;
}

void disc_stop(int drive)
{
        drive = real_drive(drive);
        
        if (drives[drive].stop)
                drives[drive].stop(drive);
}
