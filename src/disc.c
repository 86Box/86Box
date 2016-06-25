#include "ibm.h"

#include "config.h"
#include "disc.h"
#include "disc_fdi.h"
#include "disc_img.h"
#include "fdc.h"
#include "fdd.h"
#include "pit.h"
#include "timer.h"
#include "disc_sector.h"

int disc_drivesel = 0;
int disc_poll_time = 16;

int poll_time[2] = {16, 16};

int disc_track[2];
int writeprot[2], fwriteprot[2];

DRIVE drives[2];
int drive_type[2];

int curdrive = 0;

int swwp = 0;
int disable_write = 0;

//char discfns[2][260] = {"", ""};
int defaultwriteprot = 0;

int fdc_time;
int disc_time;

int fdc_ready;

int drive_empty[2] = {1, 1};
int disc_changed[2];

int bpulses[2] = {0, 0};

int motorspin;
int motoron;

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
        {"IMG", img_load,       img_close, -1},
        {"IMA", img_load,       img_close, -1},
        {"360", img_load,       img_close, -1},
	{"XDF", img_load,       img_close, -1},
        {"FDI", fdi_load,       fdi_close, -1},
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
	fdd_stepping_motor_on[drive] = fdd_track_diff[drive] = NULL;
        if (!fn) return;
        p = get_extension(fn);
        if (!p) return;
//        setejecttext(drive, fn);
        pclog("Loading :%i %s %s\n", drive, fn,p);
        f = fopen(fn, "rb");
        if (!f) return;
        fseek(f, -1, SEEK_END);
        size = ftell(f) + 1;
        fclose(f);        
        while (loaders[c].ext)
        {
                if (!strcasecmp(p, loaders[c].ext) && (size == loaders[c].size || loaders[c].size == -1))
                {
                        pclog("Loading as %s\n", p);
                        driveloaders[drive] = c;
                        loaders[c].load(drive, fn);
                        drive_empty[drive] = 0;
                        disc_changed[drive] = 1;
                        strcpy(discfns[drive], fn);
                        return;
                }
                c++;
        }
        pclog("Couldn't load %s %s\n",fn,p);
        drive_empty[drive] = 1;
        discfns[drive][0] = 0;
}

void disc_close(int drive)
{
//        pclog("disc_close %i\n", drive);
        if (loaders[driveloaders[drive]].close) loaders[driveloaders[drive]].close(drive);
        drive_empty[drive] = 1;
        discfns[drive][0] = 0;
        drives[drive].hole = NULL;
        drives[drive].poll = NULL;
        drives[drive].seek = NULL;
        drives[drive].readsector = NULL;
        drives[drive].writesector = NULL;
        drives[drive].readaddress = NULL;
        drives[drive].format = NULL;
        drives[drive].realtrack = NULL;
	drives[drive].stop = NULL;
	fdd_stepping_motor_on[drive] = fdd_track_diff[drive] = 0;
}

int disc_notfound=0;
int not_found[2] = {0, 0};
static int disc_period = 32;

int disc_hole(int drive)
{
	drive ^= fdd_swap;

	if (drives[drive].hole)
	{
		return drives[drive].hole(drive);
	}
	else
	{
		return 0;
	}
}

int disc_byteperiod(int drive)
{
	drive ^= fdd_swap;

	if (drives[drive].byteperiod)
	{
		return drives[drive].byteperiod(drive);
	}
	else
	{
		return 32;
	}
}

#define ACCURATE_TIMER_USEC ((((double) cpuclock) / 1000000.0) * (double)(1 << TIMER_SHIFT))

uint32_t byte_pulses = 0;

#if 0
void disc_time_adjust()
{
	if (disc_byteperiod(disc_drivesel ^ fdd_swap) == 26)
        	disc_poll_time -= ((160.0 / 6.0) * ACCURATE_TIMER_USEC);
	else
	        disc_poll_time -= 25.0 * ((double) disc_byteperiod(disc_drivesel ^ fdd_swap)) * ACCURATE_TIMER_USEC;
}

void disc_poll()
{
        // disc_poll_time += disc_period * TIMER_USEC;
	if (disc_byteperiod(disc_drivesel ^ fdd_swap) == 26)
		disc_poll_time += ((160.0 / 6.0) * ACCURATE_TIMER_USEC);
	else
	        disc_poll_time += ((double) disc_byteperiod(disc_drivesel ^ fdd_swap)) * ACCURATE_TIMER_USEC;

	byte_pulses++;
	if ((byte_pulses > 6250) && (byte_pulses <= 6275))  pclog("Byte pulses is now %i!\n", byte_pulses);

        if (drives[disc_drivesel].poll)
		drives[disc_drivesel].poll(disc_drivesel);

        if (disc_notfound)
        {
                disc_notfound--;
                if (!disc_notfound)
                        fdc_notfound();
        }
}
#endif

// #define TIMER_SUB 441 // 736
// #define TIMER_SUB 0
#define TIMER_SUB 0

double dt[2] = {0.0, 0.0};
double dt2[2] = {0.0, 0.0};

void disc_poll_ex(int poll_drive)
{
	int dp = 0;
	double pm = 1.0;

	int dbp = disc_byteperiod(poll_drive ^ fdd_swap);
	double ddbp = (double) dbp;

	double dtime = 0.0;

	double dusec = (double) TIMER_USEC;
	double dsub = (double) TIMER_SUB;

	if (dbp == 26)  ddbp = 160.0 / 6.0;

        if (not_found[poll_drive])
        {
                not_found[poll_drive]--;
                if (!not_found[poll_drive])
                        fdc_notfound();
        }
	else
	{
	        if (drives[poll_drive].poll)
			dp = drives[poll_drive].poll(poll_drive);
	}

#if 0
	if (dp == 2)
	{
		pm = 15.0 / 16.0;
		pclog("SYNC byte detected\n");
	}
#endif

	dtime = (ddbp * pm * dusec) - dsub;

	poll_time[poll_drive] += (int) dtime;

	dt[poll_drive] += dtime;
	if (dp)  dt2[poll_drive] += dtime;

	if (dp)  bpulses[poll_drive]++;

	// if (!dp && (byte_pulses == 10416))
	if (bpulses[poll_drive] == raw_tsize[poll_drive])
	{
		pclog("Sent %i byte pulses for drive %c (time: %lf | %lf)\n", raw_tsize[poll_drive], 0x41 + poll_drive, dt[poll_drive], dt2[poll_drive]);
	        // poll_time[poll_drive] += (dbp * (2.0 / 3.0) * pm * TIMER_USEC) - TIMER_SUB;
		// dt[poll_drive] = dt2[poll_drive] = bpulses[poll_drive] = motor_on[poll_drive] = 0;
		// disc_stop(poll_drive ^ fdd_swap);	/* Send drive to idle state after enough byte pulses have been sent. */
		/* Disc state is already set to idle on finish or error anyway. */
	}
}

void disc_poll_0()
{
	disc_poll_ex(0);
}

void disc_poll_1()
{
	disc_poll_ex(1);
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
	fdd_stepping_motor_on[0] = fdd_track_diff[0] = 0;
	fdd_stepping_motor_on[1] = fdd_track_diff[1] = 0;
	// timer_add(disc_poll, &disc_poll_time, &motoron, NULL);
	timer_add(disc_poll_0, &(poll_time[0]), &(motor_on[0]), NULL);
	timer_add(disc_poll_1, &(poll_time[1]), &(motor_on[1]), NULL);
}

void disc_init()
{
//        pclog("disc_init %p\n", drives);
        drives[0].poll = drives[1].poll = 0;
        drives[0].seek = drives[1].seek = 0;
        drives[0].readsector = drives[1].readsector = 0;
        disc_reset();
}

int oldtrack[2] = {0, 0};
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
        drive ^= fdd_swap;

        if (drives[drive].readsector)
	{
                drives[drive].readsector(drive, sector, track, side, density, sector_size);
		pclog("Byte pulses: %i\n", bpulses[drive]);
		bpulses[drive] = 0;
		dt[drive] = dt2[drive] = 0;
		// motor_on[drive] = 1;
		poll_time[drive] = 0;
	}
        else
		not_found[drive] = 1000;
#if 0
                disc_notfound = 1000;
#endif
}

void disc_writesector(int drive, int sector, int track, int side, int density, int sector_size)
{
        drive ^= fdd_swap;

        if (drives[drive].writesector)
	{
                drives[drive].writesector(drive, sector, track, side, density, sector_size);
		bpulses[drive] = 0;
		dt[drive] = dt2[drive] = 0;
		// motor_on[drive] = 1;
		poll_time[drive] = 0;
	}
        else
		not_found[drive] = 1000;
#if 0
                disc_notfound = 1000;
#endif
}

void disc_readaddress(int drive, int track, int side, int density)
{
        drive ^= fdd_swap;

        if (drives[drive].readaddress)
	{
                drives[drive].readaddress(drive, track, side, density);
		bpulses[drive] = 0;
		dt[drive] = dt2[drive] = 0;
		// motor_on[drive] = 1;
		poll_time[drive] = 0;
	}
}

void disc_format(int drive, int track, int side, int density, uint8_t fill)
{
        drive ^= fdd_swap;
        
        if (drives[drive].format)
	{
                drives[drive].format(drive, track, side, density, fill);
		bpulses[drive] = 0;
		dt[drive] = dt2[drive] = 0;
		// motor_on[drive] = 1;
		poll_time[drive] = 0;
	}
        else
		not_found[drive] = 1000;
#if 0
                disc_notfound = 1000;
#endif
}

int disc_realtrack(int drive, int track)
{
        drive ^= fdd_swap;
        
        if (drives[drive].realtrack)
                return drives[drive].realtrack(drive, track);
        else
                return track;
}

void disc_stop(int drive)
{
        drive ^= fdd_swap;
        
        if (drives[drive].stop)
                drives[drive].stop(drive);
}

void disc_set_drivesel(int drive)
{
        drive ^= fdd_swap;
        
        disc_drivesel = drive;
}
