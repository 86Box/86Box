#include <stdio.h>
#include <string.h>
#include "ibm.h"

#include "disc.h"
#include "dma.h"
#include "fdd.h"
#include "io.h"
#include "pic.h"
#include "timer.h"

extern int motoron;

extern int motor_on[2] = {0, 0};

static int fdc_reset_stat = 0;
/*FDC*/
typedef struct FDC
{
        uint8_t dor,stat,command,dat,st0;
        int head,track[256],sector,drive,lastdrive;
        int rw_track;
        int pos;
        uint8_t params[256];
        uint8_t res[256];
        int pnum,ptot;
        int rate;
        uint8_t specify[256];
        int eot[256];
        int lock;
        int perp;
        uint8_t config, pretrk;
        int abort;
        uint8_t format_dat[256];
        int format_state;
        int tc;
        int written;
        
        int pcjr, ps1;
        
        int watchdog_timer;
        int watchdog_count;

        int data_ready;
        int inread;
        
        int dskchg_activelow;
        int enable_3f1;
        
        int bitcell_period;

	int is_nsc;			/* 1 = FDC is on a National Semiconductor Super I/O chip, 0 = other FDC. This is needed,
					   because the National Semiconductor Super I/O chips add some FDC commands. */
	int enh_mode;
	int rwc[2];
	int boot_drive;
	int densel_polarity;
	int densel_force;
	int drvrate[2];

	int dma;
	int fifo, tfifo;
	int fifobufpos;
	int drv2en;
	uint8_t fifobuf[16];

	int seek_params;		/* Needed for relative seek. */
} FDC;

int skip_pulses[2] = {0, 0};

static FDC fdc;

void fdc_callback();
int timetolive;
//#define SECTORS 9
int lastbyte=0;
uint8_t disc_3f7;

int discmodified[2];
int discrate[2];

int discint;

#define FDC_STATE_NORMAL	0
#define FDC_STATE_SEEK		1

int fdc_state = 0;

void fdc_reset()
{
        fdc.stat=0x80;
        fdc.pnum=fdc.ptot=0;
        fdc.st0=0;
        fdc.lock = 0;
        fdc.head = 0;
        fdc.abort = 0;
	fdc_set_skip_pulses(0, 0);
	fdc_set_skip_pulses(1, 0);
	fdd_stepping_motor_on[0] = fdd_stepping_motor_on[1] = 0;
	fdd_track_diff[0] = fdd_track_diff[1] = 0;
#if 0
        if (!AT)
        {
                fdc.rate = 2;
                // fdc_update_rate();
        }
#endif
	fdc_state = FDC_STATE_NORMAL;
//        pclog("Reset FDC\n");
}
int ins;

void fdc_set_skip_pulses(int drive, int val)
{
	skip_pulses[drive] = val;
	pclog("Skip pulses for drive %c: is now %i\n", 0x41 + drive, val);
}

int fdc_skip_pulses(int drive)
{
	return (skip_pulses[drive] ? 1 : 0) || (discint < 0);
}

void fdc_reset_fifo_buf()
{
	int i = 0;
	memset(fdc.fifobuf, 0, 16);
	fdc.fifobufpos = 0;
}

void fdc_fifo_buf_write(int val)
{
	if (fdc.fifobufpos < fdc.tfifo)
	{
		fdc.fifobuf[fdc.fifobufpos] = val;
		fdc.fifobufpos++;
		fdc.fifobufpos %= fdc.tfifo;
		// pclog("FIFO buffer position = %02X\n", fdc.fifobufpos);
		if (fdc.fifobufpos == fdc.tfifo)  fdc.fifobufpos = 0;
	}
}

int fdc_fifo_buf_read()
{
	int temp = 0;
	if (fdc.fifobufpos < fdc.tfifo)
	{
		temp = fdc.fifobuf[fdc.fifobufpos];
		fdc.fifobufpos++;
		fdc.fifobufpos %= fdc.tfifo;
		// pclog("FIFO buffer position = %02X\n", fdc.fifobufpos);
		if (fdc.fifobufpos == fdc.tfifo)  fdc.fifobufpos = 0;
	}
	return temp;
}

/* For DMA mode, just goes ahead in FIFO buffer but doesn't actually read or write anything. */
void fdc_fifo_buf_dummy()
{
	if (fdc.fifobufpos < fdc.tfifo)
	{
		fdc.fifobufpos++;
		fdc.fifobufpos %= fdc.tfifo;
		// pclog("FIFO buffer position = %02X\n", fdc.fifobufpos);
		if (fdc.fifobufpos == fdc.tfifo)  fdc.fifobufpos = 0;
	}
}

static void fdc_int()
{
	pclog("FDC interrupt issued\n");
        if (!fdc.pcjr)
                picint(1 << 6);
}

static void fdc_watchdog_poll(void *p)
{
        FDC *fdc = (FDC *)p;

        fdc->watchdog_count--;
        if (fdc->watchdog_count)
                fdc->watchdog_timer += 1000 * TIMER_USEC;
        else
        {
//                pclog("Watchdog timed out\n");
        
                fdc->watchdog_timer = 0;
                if (fdc->dor & 0x20)
                        picint(1 << 6);
        }
}

/* fdc.rwc per Winbond W83877F datasheet:
	0 = normal;
	1 = 500 kbps, 360 rpm;
	2 = 500 kbps, 300 rpm;
	3 = 250 kbps

	Drive is only aware of selected rate and densel, so on real hardware, the rate expected by FDC and the rate actually being
	processed by drive can mismatch, in which case the FDC won't receive the correct data.
*/

int bit_rate = 250;

void fdc_update_is_nsc(int is_nsc)
{
	fdc.is_nsc = is_nsc;
}

void fdc_update_enh_mode(int enh_mode)
{
	fdc.enh_mode = enh_mode;
}

int fdc_get_rwc(int drive)
{
	return fdc.rwc[drive];
}

void fdc_update_rwc(int drive, int rwc)
{
	fdc.rwc[drive] = rwc;
}

int fdc_get_boot_drive()
{
	return fdc.boot_drive;
}

void fdc_update_boot_drive(int boot_drive)
{
	fdc.boot_drive = boot_drive;
}

void fdc_update_densel_polarity(int densel_polarity)
{
	fdc.densel_polarity = densel_polarity;
}

void fdc_update_densel_force(int densel_force)
{
	fdc.densel_force = densel_force;
}

void fdc_update_drvrate(int drive, int drvrate)
{
	fdc.drvrate[drive] = drvrate;
}

void fdc_update_drv2en(int drv2en)
{
	fdc.drv2en = drv2en;
}

void fdc_update_rate(int drive)
{
	if ((fdc.rwc[drive] == 1) || (fdc.rwc[drive] == 2))
	{
		bit_rate = 500;
	}
	else if (fdc.rwc[drive] == 3)
	{
		bit_rate = 250;
	}
        else switch (fdc.rate)
        {
                case 0: /*High density*/
                bit_rate = 500;
                break;
                case 1: /*Double density (360 rpm)*/
		switch(fdc.drvrate[drive])
		{
			case 0:
		                bit_rate = 300;
				break;
			case 1:
		                bit_rate = 500;
				break;
			case 2:
		                bit_rate = 2000;
				break;
		}
                break;
                case 2: /*Double density*/
                bit_rate = 250;
                break;
                case 3: /*Extended density*/
                bit_rate = 1000;
                break;
        }

        fdc.bitcell_period = 1000000 / bit_rate*2; /*Bitcell period in ns*/
        pclog("fdc_update_rate: rate=%i bit_rate=%i bitcell_period=%i\n", fdc.rate, bit_rate, fdc.bitcell_period);
}

int fdc_get_bitcell_period()
{
        return fdc.bitcell_period;
}

static int fdc_get_densel(int drive)
{
	switch (fdc.rwc[drive])
	{
		case 1:
		case 3:
			return 0;
		case 2:
			return 1;
	}

	if (!fdc.is_nsc)
	{
		switch (fdc.densel_force)
		{
			case 2:
				return 1;
			case 3:
				return 0;
		}
	}
	else
	{
		switch (fdc.densel_force)
		{
			case 0:
				return 0;
			case 1:
				return 1;
		}
	}

	switch (fdc.rate)
	{
		case 0:
		case 3:
			return fdc.densel_polarity ? 1 : 0;
		case 1:
		case 2:
			return fdc.densel_polarity ? 0 : 1;
	}
}

static void fdc_rate(int drive)
{
	fdc_update_rate(drive);
	disc_set_rate(drive, fdc.drvrate[drive], fdc.rate);
	fdd_set_densel(fdc_get_densel(drive));
	// pclog("Drive %i: enh. %i, rate %i, DENSEL %i, RWC %i, drvrate %i, densel polarity %i, NSC %i, densel force %i\n", drive, fdc.enh_mode, fdc.rate, fdc_get_densel(drive), fdc_get_rwc(drive), fdc.drvrate[drive], fdc.densel_polarity, fdc.is_nsc, fdc.densel_force);
}

void fdc_seek(int drive, int params)
{
	fdd_seek(drive, params);
	fdc.stat |= (1 << fdc.drive);
	// fdc_state = FDC_STATE_SEEK;
	pclog("FDC seek issued\n");
}

void fdc_write(uint16_t addr, uint8_t val, void *priv)
{
//        pclog("Write FDC %04X %02X %04X:%04X %i %02X %i rate=%i  %i\n",addr,val,cs>>4,pc,ins,fdc.st0,ins,fdc.rate, fdc.data_ready);
	int drive;

	// pclog("fdc_write: %04X: %02X\n", addr, val);
        switch (addr&7)
        {
                case 1: return;
                case 2: /*DOR*/
//                if (val == 0xD && (cs >> 4) == 0xFC81600 && ins > 769619936) output = 3;
//                printf("DOR was %02X\n",fdc.dor);
                if (fdc.pcjr)
                {
                        if ((fdc.dor & 0x40) && !(val & 0x40))
                        {
                                fdc.watchdog_timer = 1000 * TIMER_USEC;
                                fdc.watchdog_count = 1000;
                                picintc(1 << 6);
//                                pclog("watchdog set %i %i\n", fdc.watchdog_timer, TIMER_USEC);
                        }
                        if ((val & 0x80) && !(fdc.dor & 0x80))
                        {
        			timer_process();
                                disctime = 128 * (1 << TIMER_SHIFT);
                                timer_update_outstanding();
                                discint=-1;
                                fdc_reset();
                        }
                        motor_on[0] = val & 0x01;
                        fdc.drive = 0;
/*                        if (motor_on[0])
                                output = 3;
                        else
                                output = 0;*/
                }
                else
                {
                        if (val&4)
                        {
                                fdc.stat=0x80;
                                fdc.pnum=fdc.ptot=0;
                        }
                        if ((val&4) && !(fdc.dor&4))
                        {
        			timer_process();
                                disctime = 128 * (1 << TIMER_SHIFT);
                                timer_update_outstanding();
                                discint=-1;
                                fdc_reset();
                        }
			timer_process();
                        fdc.drive = val & 3;
			val &= 0x3F;
			/* if (fdc.drive > 2)
			{
				motor_on = 0;
			} */
			if (fdc.drive == 1)
			{
				if ((!fdc.drv2en) || (fdd_get_type(1) == 0))
				{
					motor_on[1] = 0;
					val &= 0xDF;
				}
				else
				{
					motor_on[1] = val & 0x20;
				}
			}
			else if (fdc.drive == 0)
			{
				if (fdd_get_type(0) == 0)
				{
					motor_on[0] = 0;
					val &= 0xEF;
				}
				else
				{
					motor_on[0] = val & 0x10;
				}
			}
			fdc_set_skip_pulses(fdc.drive, 0);
			timer_update_outstanding();
                }
                fdc.dor=val;
//                printf("DOR now %02X\n",val);
                return;
		case 3:
		/* TDR */
		if (fdc.enh_mode)
		{
			drive = (fdc.dor & 1) ^ fdd_swap;
			fdc.rwc[drive] = (val & 0x30) >> 4;
		}
		return;
                case 4:
                if (val & 0x80)
                {
			timer_process();
                        disctime = 128 * (1 << TIMER_SHIFT);
                        timer_update_outstanding();
                        discint=-1;
                        fdc_reset();
                }
                return;
                case 5: /*Command register*/
                if ((fdc.stat & 0xf0) == 0xb0)
                {
			if (fdc.pcjr || !fdc.fifo)
			{
	                        fdc.dat = val;
        	                fdc.stat &= ~0x80;
			}
			else
			{
				fdc_fifo_buf_write(val);
				if (fdc.fifobufpos == 0)  fdc.stat &= ~0x80;
			}
                        break;
                }
//                if (fdc.inread)
//                        rpclog("c82c711_fdc_write : writing while inread! %02X\n", val);
//                rpclog("Write command reg %i %i\n",fdc.pnum, fdc.ptot);
                if (fdc.pnum==fdc.ptot)
                {
                        fdc.tc = 0;
                        fdc.data_ready = 0;
                       
                        fdc.command=val;
//                        pclog("Starting FDC command %02X\n",fdc.command);
                        switch (fdc.command&0x1F)
                        {
				case 1: /*Mode*/
				if (!fdc.is_nsc)  goto bad_command;
                                fdc.pnum=0;
                                fdc.ptot=4;
                                fdc.stat=0x90;
                                fdc.pos=0;
                                fdc.format_state = 0;
                                break;

                                case 2: /*Read track*/
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat=0x90;
                                fdc.pos=0;
                                break;
                                case 3: /*Specify*/
                                fdc.pnum=0;
                                fdc.ptot=2;
                                fdc.stat=0x90;
                                break;
                                case 4: /*Sense drive status*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat=0x90;
                                break;
                                case 5: /*Write data*/
//                                printf("Write data!\n");
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat=0x90;
                                fdc.pos=0;
//                                readflash=1;
                                break;
                                case 6: /*Read data*/
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat=0x90;
                                fdc.pos=0;
                                break;
                                case 7: /*Recalibrate*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat=0x90;
                                break;
                                case 8: /*Sense interrupt status*/
//                                printf("Sense interrupt status %i\n",curdrive);
                                fdc.lastdrive = fdc.drive;
//                                fdc.stat = 0x10 | (fdc.stat & 0xf);
//                                fdc_time=1024;
                                discint = 8;
                                fdc.pos = 0;
                                fdc_callback();
                                break;
                                case 10: /*Read sector ID*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat=0x90;
                                fdc.pos=0;
                                break;
                                case 0x0d: /*Format track*/
                                fdc.pnum=0;
                                fdc.ptot=5;
                                fdc.stat=0x90;
                                fdc.pos=0;
                                fdc.format_state = 0;
                                break;
                                case 15: /*Seek*/
                                fdc.pnum=0;
                                fdc.ptot=2;
                                fdc.stat=0x90;
				fdc.seek_params = val & 0xC0;
                                break;
                                case 0x0e: /*Dump registers*/
                                fdc.lastdrive = fdc.drive;
                                discint = 0x0e;
                                fdc.pos = 0;
                                fdc_callback();
                                break;
                                case 0x10: /*Get version*/
                                fdc.lastdrive = fdc.drive;
                                discint = 0x10;
                                fdc.pos = 0;
                                fdc_callback();
                                break;
                                case 0x12: /*Set perpendicular mode*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat=0x90;
                                fdc.pos=0;
                                break;
                                case 0x13: /*Configure*/
                                fdc.pnum=0;
                                fdc.ptot=3;
                                fdc.stat=0x90;
                                fdc.pos=0;
                                break;
                                case 0x14: /*Unlock*/
                                case 0x94: /*Lock*/
                                fdc.lastdrive = fdc.drive;
                                discint = fdc.command;
                                fdc.pos = 0;
                                fdc_callback();
                                break;

                                case 0x18:
				if (!fdc.is_nsc)  goto bad_command;			
                                fdc.lastdrive = fdc.drive;
                                discint = 0x10;
                                fdc.pos = 0;
                                fdc_callback();
                                /* fdc.stat = 0x10;
                                discint  = 0xfc;
                                fdc_callback(); */
                                break;

                                default:
bad_command:
                                // fatal("Bad FDC command %02X\n",val);
//                                dumpregs();
//                                exit(-1);
                                fdc.stat=0x10;
                                discint=0xfc;
        			timer_process();
        			disctime = 200 * (1 << TIMER_SHIFT);
        			timer_update_outstanding();
                                break;
                        }
                }
                else
                {
                        fdc.params[fdc.pnum++]=val;
                        if (fdc.pnum==fdc.ptot)
                        {
//                                pclog("Got all params %02X\n", fdc.command);
                                fdc.stat=0x30;
                                discint=fdc.command&0x1F;
        			timer_process();
        			disctime = 1024 * (1 << TIMER_SHIFT);
        			timer_update_outstanding();
//                                fdc.drive = fdc.params[0] & 3;
                                disc_drivesel = fdc.drive & 1;
                                fdc_reset_stat = 0;
                                disc_set_drivesel(fdc.drive & 1);
                                switch (discint)
                                {
                                        case 2: /*Read a track*/
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 1);
					fdc_rate(fdc.drive);
                                        fdc.head=fdc.params[2];
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
                                        fdc.sector=fdc.params[3];
                                        fdc.eot[fdc.drive] = fdc.params[5];
					if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
						fdc_notfound();
                                        if (fdc.config & 0x40)
					{
						if (fdc.params[1] != fdc.track[fdc.drive])
						{
                                                	fdc_seek(fdc.drive, fdc.params[1] - fdc.track[fdc.drive]);
							fdc.track[fdc.drive] = fdc.params[1];
						}
					}
                                        // fdc.track[fdc.drive]=fdc.params[1];
                                        fdc.rw_track = fdc.params[1];
//                                        pclog("Read a track track=%i head=%i sector=%i eot=%i\n", fdc.track[fdc.drive], fdc.head, fdc.sector, fdc.eot[fdc.drive]);
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);

					if (fdc_state != FDC_STATE_SEEK)
	                                        disc_readsector(fdc.drive, SECTOR_FIRST, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);

                                        disctime = (fdc_state == FDC_STATE_SEEK) ? (790 * TIMER_USEC) : 0;
                                        readflash = 1;
                                        fdc.inread = 1;
                                        break;

                                        case 3: /*Specify*/
                                        fdc.stat=0x80;
                                        fdc.specify[0] = fdc.params[0];
                                        fdc.specify[1] = fdc.params[1];
                                        fdc.dma = (fdc.specify[1] & 1) ^ 1;
                                        disctime = 0;
                                        break;

                                         case 5: /*Write data*/
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 1);
					fdc_rate(fdc.drive);
                                        fdc.head=fdc.params[2];
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
                                        fdc.sector=fdc.params[3];
                                        fdc.eot[fdc.drive] = fdc.params[5];
					if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
						fdc_notfound();
                                        if (fdc.config & 0x40)
					{
						if (fdc.params[1] != fdc.track[fdc.drive])
						{
                                                	fdc_seek(fdc.drive, fdc.params[1] - fdc.track[fdc.drive]);
							fdc.track[fdc.drive] = fdc.params[1];
						}
					}
                                        // fdc.track[fdc.drive]=fdc.params[1];
                                        fdc.rw_track = fdc.params[1];
                                        
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);

					if (fdc_state != FDC_STATE_SEEK)
	                                        disc_writesector(fdc.drive, fdc.sector, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);

                                        disctime = (fdc_state == FDC_STATE_SEEK) ? (790 * TIMER_USEC) : 0;
                                        fdc.written = 0;
                                        readflash = 1;
                                        fdc.pos = 0;
					if (fdc_state != FDC_STATE_SEEK)
					{
	                                        if (fdc.pcjr)
	                                                fdc.stat = 0xb0;
					}
                                        break;
                                        
                                        case 6: /*Read data*/
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 1);
					fdc_rate(fdc.drive);
                                        fdc.head=fdc.params[2];
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
                                        fdc.sector=fdc.params[3];
                                        fdc.eot[fdc.drive] = fdc.params[5];
					// pclog("FDC track is %i, requested track is %i\n", fdc.track[fdc.drive], fdc.params[1]);
					if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
						fdc_notfound();

                                        if (fdc.config & 0x40)
					{
						if (fdc.params[1] != fdc.track[fdc.drive])
						{
                                                	fdc_seek(fdc.drive, fdc.params[1] - fdc.track[fdc.drive]);
							fdc.track[fdc.drive] = fdc.params[1];
						}
					}
                                        // fdc.track[fdc.drive]=fdc.params[1];
                                        fdc.rw_track = fdc.params[1];

					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);

					if (fdc_state != FDC_STATE_SEEK)
	                                        disc_readsector(fdc.drive, fdc.sector, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);

                                        disctime = (fdc_state == FDC_STATE_SEEK) ? (790 * TIMER_USEC) : 0;
                                        readflash = 1;
                                        fdc.inread = 1;
                                        break;
                                        
                                        case 7: /*Recalibrate*/
                                        fdc.stat =  1 << fdc.drive;
                                        disctime = 0;
					if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
						fdc_notfound();
					else
					{
	                                        // fdc_seek(fdc.drive, SEEK_RECALIBRATE);
	                                        fdc_seek(fdc.drive, -79);
					}
                                        disctime = 790 * TIMER_USEC;
                                        break;

                                        case 0x0d: /*Format*/
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 1);
					fdc_rate(fdc.drive);
                                        fdc.head = (fdc.params[0] & 4) ? 1 : 0;
                                        fdc.format_state = 1;
                                        fdc.pos = 0;
                                        fdc.stat = 0x30;
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);
                                        break;
                                        
                                        case 0xf: /*Seek*/
                                        fdc.stat =  1 << fdc.drive;
                                        fdc.head = (fdc.params[0] & 4) ? 1 : 0;
                                        disctime = 0;
					if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
						fdc_notfound();
					else
					{
						// pclog("Seeking (params=%02X, params[1]=%i)...\n", fdc.seek_params, fdc.params[1]);
						if (fdc.seek_params & 0x80)
						{
							if (fdc.seek_params & 0x40)
							{
								/* Relative seek inwards. */
								fdc_seek(fdc.drive, fdc.params[1]);
							}
							else
							{
								/* Relative seek outwards. */
								fdc_seek(fdc.drive, -fdc.params[1]);
							}
						}
						else
						{
		                                        fdc_seek(fdc.drive, fdc.params[1] - fdc.track[fdc.drive]);
						}
					}
                                        disctime = 790 * TIMER_USEC;
//                                        pclog("Seek to %i\n", fdc.params[1]);
                                        break;
                                        
                                        case 10: /*Read sector ID*/
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 1);
					fdc_rate(fdc.drive);
                                        disctime = 0;
                                        fdc.head = (fdc.params[0] & 4) ? 1 : 0;                                        
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
//                                        pclog("Read sector ID %i %i\n", fdc.rate, fdc.drive);
					fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);
					if (((fdc.drive ^ fdd_swap) != 1) || fdc.drv2en)
	                                        disc_readaddress(fdc.drive, fdc.track[fdc.drive], fdc.head, fdc.rate);
					else
						fdc_notfound();
                                        break;
                                }
                        }
                }
                return;
                case 7:
                        // if (!AT) return;
                fdc.rate=val&3;
		// pclog("Rate is now: %i\n", fdc.rate);

                disc_3f7=val;
                return;
        }
//        printf("Write FDC %04X %02X\n",addr,val);
//        dumpregs();
//        exit(-1);
}

int paramstogo=0;
uint8_t fdc_read(uint16_t addr, void *priv)
{
        uint8_t temp;
        int drive;
//        /*if (addr!=0x3f4) */printf("Read FDC %04X %04X:%04X %04X %i %02X %02x %i ",addr,cs>>4,pc,BX,fdc.pos,fdc.st0,fdc.stat,ins);
        switch (addr&7)
        {
                case 1: /*???*/
                drive = (fdc.dor & 1) ^ fdd_swap;
                if (!fdc.enable_3f1)
                        return 0xff;
//                temp=0x50;
                temp = 0x70;
                if (drive)
                        temp &= ~0x40;
                else
                        temp &= ~0x20;
                break;
                case 3:
		drive = (fdc.dor & 1) ^ fdd_swap;
                if (fdc.ps1)
                {
                        /*PS/1 Model 2121 seems return drive type in port 0x3f3,
                          despite the 82077AA FDC not implementing this. This is
                          presumably implemented outside the FDC on one of the
                          motherboard's support chips.*/
			/* NOTE by OBattler: This has to be implemented for
			   super I/O chips too. */
                        if (fdd_is_525(drive))
                                temp = 0x20;
                        else if (fdd_is_ed(drive))
                                temp = 0x10;
                        else
                                temp = 0x00;
                }
                else if (!fdc.enh_mode)
	                temp = 0x20;
		else
		{
			temp = fdc.rwc[drive] << 4;
		}
                break;
                case 4: /*Status*/
                temp=fdc.stat;
                break;
                case 5: /*Data*/
                if ((fdc.stat & 0xf0) == 0xf0)
                {
	                fdc.stat&=~0x80;
			if (fdc.pcjr || !fdc.fifo)
	                        temp = fdc.dat;
			else
			{
				temp = fdc_fifo_buf_read();
			}
                        break;
                }
		fdc.stat&=~0x80;
		if (paramstogo)
                {
                        paramstogo--;
                        temp=fdc.res[10 - paramstogo];
//                        pclog("Read param %i %02X\n",10-paramstogo,temp);
                        if (!paramstogo)
                        {
                                fdc.stat=0x80;
//                                fdc.st0=0;
                        }
                        else
                        {
                                fdc.stat|=0xC0;
//                                fdc_poll();
                        }
                }
                else
                {
                        if (lastbyte)
                                fdc.stat = 0x80;
                        lastbyte=0;
                        temp=fdc.dat;
                        fdc.data_ready = 0;
                }
                if (discint==0xA) 
		{
			timer_process();
			disctime = 1024 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
		}
                fdc.stat &= 0xf0;
                break;
                case 7: /*Disk change*/
                drive = (fdc.dor & 1) ^ fdd_swap;
                if (fdc.dor & (0x10 << drive))
                   temp = (disc_changed[drive] || drive_empty[drive])?0x80:0;
                else
                   temp = 0;
                if (fdc.dskchg_activelow)  /*PC2086/3086 seem to reverse this bit*/
                   temp ^= 0x80;
//                printf("- DC %i %02X %02X %i %i - ",fdc.dor & 1, fdc.dor, 0x10 << (fdc.dor & 1), discchanged[fdc.dor & 1], driveempty[fdc.dor & 1]);
//                discchanged[fdc.dor&1]=0;
                break;
                default:
                        temp=0xFF;
//                printf("Bad read FDC %04X\n",addr);
//                dumpregs();
//                exit(-1);
        }
//        /*if (addr!=0x3f4) */printf("%02X rate=%i  %i\n",temp,fdc.rate, fdc.data_ready);
	// pclog("fdc_read: %04X: %02X\n", addr, temp);
        return temp;
}

void fdc_callback()
{
        int temp;
        disctime = 0;
//        pclog("fdc_callback %i %i\n", discint, disctime);
//        if (fdc.inread)
//                rpclog("c82c711_fdc_callback : while inread! %08X %i %02X  %i\n", discint, fdc.drive, fdc.st0, ins);
	if (fdc_state == FDC_STATE_SEEK)
	{
		if (!fdd_stepping_motor_on[fdc.drive ^ fdd_swap])
		{
			pclog("FDC has stopped seeking\n");
			fdc_state = FDC_STATE_NORMAL;

			fdc.stat &= ~(1 << fdc.drive);

			switch (discint)
			{
				case 2:
					disc_readsector(fdc.drive, SECTOR_FIRST, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);
                                        disctime = 0;
                                        readflash = 1;
                                        fdc.inread = 1;
					return;
				case 5:
					disc_writesector(fdc.drive, fdc.sector, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);
                                        disctime = 0;
                                        fdc.written = 0;
                                        readflash = 1;
                                        fdc.pos = 0;
                                        if (fdc.pcjr)
                                                fdc.stat = 0xb0;
					return;
				case 6:
                                        disc_readsector(fdc.drive, fdc.sector, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);
                                        disctime = 0;
                                        readflash = 1;
                                        fdc.inread = 1;
					return;
				case 7:
				case 0xf:
					break;
				default:
					return;
			}
		}
		else
		{
			pclog("FDC is seeking\n");
			return;
		}
	}
	else
	{
		if (fdd_stepping_motor_on[fdc.drive ^ fdd_swap])
			pclog("FDD is seeking but FDC not\n");
		else
			pclog("Neither the FDD nor the FDC is seeking\n");
	}

        switch (discint)
        {
                case -3: /*End of command with interrupt*/
//                if (output) printf("EOC - interrupt!\n");
//rpclog("EOC\n");
                fdc_int();
                case -2: /*End of command*/
                fdc.stat = (fdc.stat & 0xf) | 0x80;
                return;
                case -1: /*Reset*/
//rpclog("Reset\n");
                fdc_int();
                fdc_reset_stat = 4;
                return;
		case 1: /*Mode*/
		fdc.stat=0x80;
		fdc.densel_force = (fdc.params[2] & 0xC0) >> 6;
		return;

                case 2: /*Read track*/
                readflash = 1;
                fdc.eot[fdc.drive]--;
//                pclog("Read a track callback, eot=%i\n", fdc.eot[fdc.drive]);
                if (!fdc.eot[fdc.drive] || fdc.tc)
                {
//                        pclog("Complete\n");
                        fdc.inread = 0;
                        discint=-2;
                        fdc_int();
			fdc_set_skip_pulses(fdc.drive, 0);
                        fdc.stat=0xD0;
                        fdc.res[4]=(fdc.head?4:0)|fdc.drive;
                        fdc.res[5]=fdc.res[6]=0;
                        fdc.res[7]=fdc.rw_track;
                        fdc.res[8]=fdc.head;
                        fdc.res[9]=fdc.sector;
                        fdc.res[10]=fdc.params[4];
                        paramstogo=7;
                        return;
                }
                else
		{
			if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
				not_found[fdc.drive ^ fdd_swap] = 1000;
#if 0
				disc_notfound = 1000;
#endif
			else
	                        disc_readsector(fdc.drive, SECTOR_NEXT, fdc.rw_track, fdc.head, fdc.rate, fdc.params[4]);
		}
                fdc.inread = 1;
                return;
                case 4: /*Sense drive status*/
                fdc.res[10] = (fdc.params[0] & 7) | 0x28;
		if (((fdc.drive ^ fdd_swap) != 1) || fdc.drv2en)
		{
	                if (fdd_track0(fdc.drive ^ fdd_swap))
        	                fdc.res[10] |= 0x10;
		}
                if (writeprot[fdc.drive])
                        fdc.res[10] |= 0x40;

                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                paramstogo = 1;
                discint = 0;
                disctime = 0;
                return;
                case 5: /*Write data*/
                readflash = 1;
                fdc.sector++;
                if (fdc.sector > fdc.params[5])
                {
                        fdc.sector = 1;
                        if (fdc.command & 0x80)
                        {
                                fdc.head ^= 1;
                                if (!fdc.head)
                                {
                                        fdc.rw_track++;
//                                        fdc.track[fdc.drive]++;
/*                                        if (fdc.track[fdc.drive] >= 79)
                                        {
                                                fdc.track[fdc.drive] = 79;
                                                fdc.tc = 1;
                                        }*/
                                }
                        }
                        else
                        {
                                fdc.rw_track++;
//                                fdc.track[fdc.drive]++;
                                fdc.tc = 1;
                        }
                }
                if (fdc.tc)
                {
                        discint=-2;
                        fdc_int();
			fdc_set_skip_pulses(fdc.drive, 0);
                        fdc.stat=0xD0;
                        fdc.res[4]=(fdc.head?4:0)|fdc.drive;
                        fdc.res[5]=fdc.res[6]=0;
                        fdc.res[7]=fdc.rw_track;
                        fdc.res[8]=fdc.head;
                        fdc.res[9]=fdc.sector;
                        fdc.res[10]=fdc.params[4];
                        paramstogo=7;
                        return;
                }
		if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
				not_found[fdc.drive ^ fdd_swap] = 1000;
#if 0
				disc_notfound = 1000;
#endif
		else
	                disc_writesector(fdc.drive, fdc.sector, fdc.rw_track, fdc.head, fdc.rate, fdc.params[4]);
//                ioc_fiq(IOC_FIQ_DISC_DATA);
                return;
                case 6: /*Read data*/
//                rpclog("Read data %i\n", fdc.tc);
                readflash = 1;
		fdc_set_skip_pulses(fdc.drive, 1);
                fdc.sector++;
                if (fdc.sector > fdc.params[5])
                {
                        fdc.sector = 1;
                        if (fdc.command & 0x80)
                        {
                                fdc.head ^= 1;
                                if (!fdc.head)
                                {
                                        fdc.rw_track++;
//                                        fdc.track[fdc.drive]++;
/*                                        if (fdc.track[fdc.drive] >= 79)
                                        {
                                                fdc.track[fdc.drive] = 79;
                                                fdc.tc = 1;
                                        }*/
                                }
                        }
                        else
                        {
                                fdc.rw_track++;
//                                fdc.track[fdc.drive]++;
                                fdc.tc = 1;
                        }
                }
                if (fdc.tc)
                {
                        fdc.inread = 0;
                        discint=-2;
                        fdc_int();
			fdc_set_skip_pulses(fdc.drive, 0);
                        fdc.stat=0xD0;
                        fdc.res[4]=(fdc.head?4:0)|fdc.drive;
                        fdc.res[5]=fdc.res[6]=0;
                        fdc.res[7]=fdc.rw_track;
                        fdc.res[8]=fdc.head;
                        fdc.res[9]=fdc.sector;
                        fdc.res[10]=fdc.params[4];
                        paramstogo=7;
                        return;
                }                        
		fdc_set_skip_pulses(fdc.drive, 0);
		if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
				not_found[fdc.drive ^ fdd_swap] = 1000;
#if 0
				disc_notfound = 1000;
#endif
		else
	                disc_readsector(fdc.drive, fdc.sector, fdc.rw_track, fdc.head, fdc.rate, fdc.params[4]);
                fdc.inread = 1;
                return;

                case 7: /*Recalibrate*/
                fdc.track[fdc.drive]=0;
//                if (!driveempty[fdc.dor & 1]) discchanged[fdc.dor & 1] = 0;
                if (fdc.drive <= 1)
                        fdc.st0 = 0x20 | (fdc.params[0] & 3) | (fdc.head?4:0);
                else
                        fdc.st0 = 0x68 | (fdc.params[0] & 3) | (fdc.head?4:0);
                discint=-3;
		timer_process();
		disctime = 2048 * (1 << TIMER_SHIFT);
		timer_update_outstanding();
//                printf("Recalibrate complete!\n");
                fdc.stat = 0x80 | (1 << fdc.drive);
                return;

                case 8: /*Sense interrupt status*/
//                pclog("Sense interrupt status %i\n", fdc_reset_stat);

                fdc.stat    = (fdc.stat & 0xf) | 0xd0;                
                if (fdc_reset_stat)
                        fdc.res[9] = 0xc0 | (4 - fdc_reset_stat) | (fdc.head ? 4 : 0);
                else
                        fdc.res[9] = fdc.st0;
                fdc.res[10] = fdc.track[fdc.drive];
                if (!fdc_reset_stat)
                        fdc.st0 = 0x80;
                else
                        fdc_reset_stat--;

                paramstogo = 2;
                discint = 0;
		disctime = 0;
                return;
                
                case 0x0d: /*Format track*/
//                rpclog("Format\n");
                if (fdc.format_state == 1)
                {
//                        ioc_fiq(IOC_FIQ_DISC_DATA);
                        fdc.format_state = 2;
			timer_process();
			disctime = 128 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
                }                
                else if (fdc.format_state == 2)
                {
                        temp = fdc_getdata(fdc.pos == ((fdc.params[2] * 4) - 1));
                        if (temp == -1)
                        {
        			timer_process();
        			disctime = 128 * (1 << TIMER_SHIFT);
        			timer_update_outstanding();
                                return;
                        }
                        fdc.format_dat[fdc.pos++] = temp;
                        if (fdc.pos == (fdc.params[2] * 4))
                                fdc.format_state = 3;
			timer_process();
			disctime = 128 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
                }
                else if (fdc.format_state == 3)
                {
//                        pclog("Format next stage track %i head %i\n", fdc.track[fdc.drive], fdc.head);
			if (((fdc.drive ^ fdd_swap) == 1) && !fdc.drv2en)
				not_found[fdc.drive ^ fdd_swap] = 1000;
#if 0
				disc_notfound = 1000;
#endif
			else
	                        disc_format(fdc.drive, disc_realtrack(fdc.drive, fdc.track[fdc.drive]), fdc.head, fdc.rate, fdc.params[4]);
                        fdc.format_state = 4;
                }
                else
                {
                        discint=-2;
                        fdc_int();
			fdc_set_skip_pulses(fdc.drive, 0);
                        fdc.stat=0xD0;
                        fdc.res[4] = (fdc.head?4:0)|fdc.drive;
                        fdc.res[5] = fdc.res[6] = 0;
                        fdc.res[7] = fdc.track[fdc.drive];
                        fdc.res[8] = fdc.head;
                        fdc.res[9] = fdc.format_dat[fdc.pos - 2] + 1;
                        fdc.res[10] = fdc.params[4];
                        paramstogo=7;
                        fdc.format_state = 0;
                        return;
                }
                return;
                
                case 15: /*Seek*/
                fdc.track[fdc.drive]=fdc.params[1];
//                if (!driveempty[fdc.dor & 1]) discchanged[fdc.dor & 1] = 0;
//                printf("Seeked to track %i %i\n",fdc.track[fdc.drive], fdc.drive);
                if (fdc.drive <= 1)
                        fdc.st0 = 0x20 | (fdc.params[0] & 3) | (fdc.head?4:0);
                else
                        fdc.st0 = 0x68 | (fdc.params[0] & 3) | (fdc.head?4:0);
                discint=-3;
		timer_process();
		disctime = 2048 * (1 << TIMER_SHIFT);
		timer_update_outstanding();
                fdc.stat = 0x80 | (1 << fdc.drive);
//                pclog("Stat %02X ST0 %02X\n", fdc.stat, fdc.st0);
                return;
                case 0x0e: /*Dump registers*/
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                fdc.res[3] = fdc.track[0];
                fdc.res[4] = fdc.track[1];
                fdc.res[5] = 0;
                fdc.res[6] = 0;
                fdc.res[7] = fdc.specify[0];
                fdc.res[8] = fdc.specify[1];
                fdc.res[9] = fdc.eot[fdc.drive];
                fdc.res[10] = (fdc.perp & 0x7f) | ((fdc.lock) ? 0x80 : 0);
                paramstogo=10;
                discint=0;
		disctime = 0;
                return;

                case 0x10: /*Version*/
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                fdc.res[10] = 0x90;
                paramstogo=1;
                discint=0;
                disctime = 0;
                return;
                
                case 0x12:
                fdc.perp = fdc.params[0];
                fdc.stat = 0x80;
                disctime = 0;
//                picint(0x40);
                return;
                case 0x13: /*Configure*/
                fdc.config = fdc.params[1];
                fdc.pretrk = fdc.params[2];
		fdc.fifo = (fdc.params[1] & 0x20) ? 0 : 1;
		fdc.tfifo = (fdc.params[1] & 0xF) + 1;
		// pclog("FIFO is now %02X, threshold is %02X\n", fdc.fifo, fdc.tfifo);
                fdc.stat = 0x80;
                disctime = 0;
//                picint(0x40);
                return;
                case 0x14: /*Unlock*/
                fdc.lock = 0;
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                fdc.res[10] = 0;
                paramstogo=1;
                discint=0;
                disctime = 0;
                return;
                case 0x94: /*Lock*/
                fdc.lock = 1;
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                fdc.res[10] = 0x10;
                paramstogo=1;
                discint=0;
                disctime = 0;
                return;

                case 0x18: /*NSC*/
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                fdc.res[10] = 0x73;
                paramstogo=1;
                discint=0;
                disctime = 0;
                return;

                case 0xfc: /*Invalid*/
                fdc.dat = fdc.st0 = 0x80;
//                pclog("Inv!\n");
                //picint(0x40);
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
//                fdc.stat|=0xC0;
                fdc.res[10] = fdc.st0;
                paramstogo=1;
                discint=0;
                disctime = 0;
                return;
        }
//        printf("Bad FDC disc int %i\n",discint);
//        dumpregs();
//        exit(-1);
}

void fdc_overrun()
{
        disc_sector_stop(fdc.drive ^ fdd_swap);
        disctime = 0;

        fdc_int();
        fdc.stat=0xD0;
	fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);
        fdc.res[4]=0x40|(fdc.head?4:0)|fdc.drive;
        fdc.res[5]=0x10; /*Overrun*/
        fdc.res[6]=0;
        fdc.res[7]=0;
        fdc.res[8]=0;
        fdc.res[9]=0;
        fdc.res[10]=0;
        paramstogo=7;
}

int fdc_data(uint8_t data)
{
        if (fdc.tc)
                return 0;

        if (fdc.pcjr || !fdc.dma)
        {
                if (fdc.data_ready)
                {
                        fdc_overrun();
//                        pclog("Overrun\n");
                        return -1;
                }

		if (fdc.pcjr || !fdc.fifo)
		{
	                fdc.dat = data;
	                fdc.data_ready = 1;
	                fdc.stat = 0xf0;
		}
		else
		{
			// FIFO enabled
			fdc_fifo_buf_write(data);
			if (fdc.fifobufpos == 0)
			{
				// We have wrapped around, means FIFO is over
				fdc.data_ready = 1;
				fdc.stat = 0xf0;
			}
		}
        }
        else
        {
                if (dma_channel_write(2, data) & DMA_OVER)
                        fdc.tc = 1;

		if (!fdc.fifo)
		{
	                fdc.data_ready = 1;
	                fdc.stat = 0xd0;
		}
		else
		{
			fdc_fifo_buf_dummy();
			if (fdc.fifobufpos == 0)
			{
				// We have wrapped around, means FIFO is over
				fdc.data_ready = 1;
				fdc.stat = 0xd0;
			}
		}
        }
        
        return 0;
}

void fdc_finishread(int drive)
{
	pclog("Read finished\n");
	fdc_set_skip_pulses(drive, 1);
        disctime = 200 * TIMER_USEC;
//        rpclog("fdc_finishread\n");
}

void fdc_notfound()
{
        disctime = 0;

        fdc_int();
        fdc.stat=0xD0;
	fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);
        fdc.res[4]=0x40|(fdc.head?4:0)|fdc.drive;
        fdc.res[5]=5;
        fdc.res[6]=0;
        fdc.res[7]=0;
        fdc.res[8]=0;
        fdc.res[9]=0;
        fdc.res[10]=0;
        paramstogo=7;
//        rpclog("c82c711_fdc_notfound\n");
}

void fdc_datacrcerror()
{
        disctime = 0;

        fdc_int();
        fdc.stat=0xD0;
	fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);
        fdc.res[4]=0x40|(fdc.head?4:0)|fdc.drive;
        fdc.res[5]=0x20; /*Data error*/
        fdc.res[6]=0x20; /*Data error in data field*/
        fdc.res[7]=0;
        fdc.res[8]=0;
        fdc.res[9]=0;
        fdc.res[10]=0;
        paramstogo=7;
//        rpclog("c82c711_fdc_datacrcerror\n");
}

void fdc_headercrcerror()
{
        disctime = 0;

        fdc_int();
        fdc.stat=0xD0;
	fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);
        fdc.res[4]=0x40|(fdc.head?4:0)|fdc.drive;
        fdc.res[5]=0x20; /*Data error*/
        fdc.res[6]=0;
        fdc.res[7]=0;
        fdc.res[8]=0;
        fdc.res[9]=0;
        fdc.res[10]=0;
        paramstogo=7;
//        rpclog("c82c711_fdc_headercrcerror\n");
}

void fdc_writeprotect()
{
        disctime = 0;

        fdc_int();
        fdc.stat=0xD0;
	fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);
        fdc.res[4]=0x40|(fdc.head?4:0)|fdc.drive;
        fdc.res[5]=0x02; /*Not writeable*/
        fdc.res[6]=0;
        fdc.res[7]=0;
        fdc.res[8]=0;
        fdc.res[9]=0;
        fdc.res[10]=0;
        paramstogo=7;
}

int fdc_getdata(int last)
{
        int data;
        
        if (fdc.pcjr || !fdc.dma)
        {
                if (fdc.written)
                {
                        fdc_overrun();
//                        pclog("Overrun\n");
                        return -1;
                }
		if (fdc.pcjr || !fdc.fifo)
		{
	                data = fdc.dat;

	                if (!last)
        	                fdc.stat = 0xb0;
		}
		else
		{
			data = fdc_fifo_buf_read();

	                if (!last && (fdc.fifobufpos == 0))
        	                fdc.stat = 0xb0;
		}
        }
        else
        {
                data = dma_channel_read(2);

		if (!fdc.fifo)
		{
			if (!last)
				fdc.stat = 0x90;
		}
		else
		{
			fdc_fifo_buf_dummy();

	                if (!last && (fdc.fifobufpos == 0))
        	                fdc.stat = 0x90;
		}
        
                if (data & DMA_OVER)
                        fdc.tc = 1;
        }
        
        fdc.written = 0;
        return data & 0xff;
}

void fdc_sectorid(uint8_t track, uint8_t side, uint8_t sector, uint8_t size, uint8_t crc1, uint8_t crc2)
{
//        pclog("SectorID %i %i %i %i\n", track, side, sector, size);
        fdc_int();
	fdc_set_skip_pulses(fdc.drive ^ fdd_swap, 0);
        fdc.stat=0xD0;
        fdc.res[4]=(fdc.head?4:0)|fdc.drive;
        fdc.res[5]=0;
        fdc.res[6]=0;
        fdc.res[7]=track;
        fdc.res[8]=side;
        fdc.res[9]=sector;
        fdc.res[10]=size;
        paramstogo=7;
}

void fdc_indexpulse()
{
//        ioc_irqa(IOC_IRQA_DISC_INDEX);
//        rpclog("c82c711_fdc_indexpulse\n");
}

void fdc_init()
{
	timer_add(fdc_callback, &disctime, &disctime, NULL);
	fdc.dskchg_activelow = 0;
	fdc.enable_3f1 = 1;

	fdc_update_enh_mode(0);
	fdc_update_densel_polarity(1);
	fdc_update_rwc(0, 0);
	fdc_update_rwc(1, 0);
	fdc_update_densel_force(0);
	fdc_update_drv2en(1);

	fdc_set_skip_pulses(0, 0);
	fdc_set_skip_pulses(1, 0);

	fdd_init();

	swwp = 0;
	disable_write = 0;

	fdd_stepping_motor_on[0] = fdd_track_diff[0] = 0;
	fdd_stepping_motor_on[1] = fdd_track_diff[1] = 0;
	fdc_state = FDC_STATE_NORMAL;

	fdc.fifo = fdc.tfifo = 0;
}

void fdc_add()
{
        io_sethandler(0x03f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        io_sethandler(0x03f7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        fdc.pcjr = 0;
        fdc.ps1 = 0;
}

void fdc_add_for_superio()
{
        io_sethandler(0x03f2, 0x0004, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        io_sethandler(0x03f7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        fdc.pcjr = 0;
        fdc.ps1 = 0;
}

void fdc_add_pcjr()
{
        io_sethandler(0x00f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
	timer_add(fdc_watchdog_poll, &fdc.watchdog_timer, &fdc.watchdog_timer, &fdc);
        fdc.pcjr = 1;
        fdc.ps1 = 0;
}

void fdc_remove()
{
        io_removehandler(0x03f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        io_removehandler(0x03f7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);        
}

void fdc_set_ps1()
{
        fdc.ps1 = 1;
}

void fdc_discchange_clear(int drive)
{
        if (drive < 2)
                disc_changed[drive] = 0;
}

void fdc_set_dskchg_activelow()
{
        fdc.dskchg_activelow = 1;
}

void fdc_3f1_enable(int enable)
{
        fdc.enable_3f1 = enable;
}
