/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ibm.h"

#include "disc.h"
#include "dma.h"
#include "fdc.h"
#include "fdd.h"
#include "io.h"
#include "pic.h"
#include "timer.h"

extern int motoron[FDD_NUM];

int ui_writeprot[FDD_NUM] = {0, 0, 0, 0};

int command_has_drivesel[256] = {	0, 0,
					1,			/* READ TRACK */
					0,
					1,			/* SENSE DRIVE STATUS */
					1,			/* WRITE DATA */
					1,			/* READ DATA */
					1,			/* RECALIBRATE */
					0,
					1,			/* WRITE DELETED DATA */
					1,			/* READ ID */
					0,
					1,			/* READ DELETED DATA */
					1,			/* FORMAT TRACK */
					0,
					1,			/* SEEK, RELATIVE SEEK */
					0,
					1,			/* SCAN EQUAL */
					0, 0, 0, 0,
					1,			/* VERIFY */
					0, 0, 0,
					1,			/* SCAN LOW OR EQUAL */
					0, 0, 0,
					1,			/* SCAN HIGH OR EQUAL */
					0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

static int fdc_reset_stat = 0;
/*FDC*/
typedef struct FDC
{
        uint8_t dor,stat,command,dat,st0;
        int head,sector,drive,lastdrive;
	int pcn[4];
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
	int format_n;
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
	int rwc[4];
	int boot_drive;
	int densel_polarity;
	int densel_force;
	int drvrate[4];

	int dma;
	int fifo, tfifo;
	int fifobufpos;
	int drv2en;
	uint8_t fifobuf[16];

	int gap, dtl;
	int format_sectors;

	int max_track;
	int mfm;

	int deleted;
	int wrong_am;

	int sc;
	int satisfying_sectors;

	sector_id_t read_track_sector;
	int fintr;

	int rw_drive;

	uint16_t base_address;
} FDC;

static FDC fdc;

void fdc_callback();
int timetolive;
int lastbyte=0;
uint8_t disc_3f7;

int discmodified[4];
int discrate[4];

int discint;

int fdc_do_log = 1;

void fdc_log(const char *format, ...)
{
// #ifdef ENABLE_FDC_LOG
   if (fdc_do_log)
   {
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
   }
// #endif
}

void fdc_reset()
{
        fdc.stat=0x80;
        fdc.pnum=fdc.ptot=0;
        fdc.st0=0;
        fdc.lock = 0;
        fdc.head = 0;
        fdc.abort = 0;
        if (!AT)
        {
                fdc.rate = 2;
        }
}

sector_id_t fdc_get_read_track_sector()
{
	return fdc.read_track_sector;
}

int fdc_ps1_525()
{
	if ((romset == ROM_IBMPS1_2011) && fdd_is_525(real_drive(fdc.dor & 3)))
	{
		return 0x40;
	}
	else
	{
		return 0;
	}
}

int fdc_get_compare_condition()
{
	switch (discint)
	{
		case 0x11:
		default:
			return 0;
		case 0x19:
			return 1;
		case 0x1D:
			return 2;
	}
}

int fdc_is_deleted()
{
	return fdc.deleted & 1;
}

int fdc_is_sk()
{
	return (fdc.deleted & 0x20) ? 1 : 0;
}

void fdc_set_wrong_am()
{
	fdc.wrong_am = 1;
}

int fdc_get_drive()
{
	return fdc.drive;
}

int fdc_get_bitcell_period();

int fdc_get_perp()
{
	if (!AT || fdc.pcjr || fdc.ps1)  return 0;

	return fdc.perp;
}

int fdc_get_bit_rate();

int fdc_get_gap2(int drive)
{
	int auto_gap2 = 22;

	if (!AT || fdc.pcjr || fdc.ps1)  return 22;

	if (fdc.perp & 3)
	{
		return ((fdc.perp & 3) == 3) ? 41 : 22;
	}
	else
	{
		auto_gap2 = (fdc_get_bit_rate() >= 3) ? 41 : 22;
		return (fdc.perp & (4 << drive)) ? auto_gap2 : 22;
	}
}

int fdc_get_format_n()
{
	return fdc.format_n;
}

int fdc_is_mfm()
{
	return fdc.mfm ? 1 : 0;
}

#if 0
double fdc_get_hut()
{
	int hut = (fdc.specify[0] & 0xF);
	double dusec;
	double bcp = ((double) fdc_get_bitcell_period()) / 250.0;
	double dhut = (double) hut;
	if (fdc_get_bitcell_period() == 3333)  bcp = 160.0 / 6.0;
	if (hut == 0)  dhut = 16.0;
	dusec = (double) TIMER_USEC;
	return (bcp * dhut * dusec * 1000.0);
}

double fdc_get_hlt()
{
	int hlt = (fdc.specify[1] >> 1);
	double dusec;
	double bcp = ((double) fdc_get_bitcell_period()) / 2000.0;
	double dhlt = (double) hlt;
	if (fdc_get_bitcell_period() == 3333)  bcp = 20.0 / 6.0;
	if (hlt == 0)  dhlt = 256.0;
	dusec = (double) TIMER_USEC;
	return (bcp * dhlt * dusec * 1000.0);
}
#endif

void fdc_request_next_sector_id()
{
	if (fdc.pcjr || !fdc.dma)
	{
		fdc.stat = 0xf0;
	}
	else
	{
		fdc.stat = 0xd0;
	}
}

void fdc_stop_id_request()
{
	fdc.stat &= 0x7f;
}

int fdc_get_gap()
{
	return fdc.gap;
}

int fdc_get_dtl()
{
	return fdc.dtl;
}

int fdc_get_format_sectors()
{
	return fdc.format_sectors;
}

void fdc_reset_fifo_buf()
{
	memset(fdc.fifobuf, 0, 16);
	fdc.fifobufpos = 0;
}

void fdc_fifo_buf_advance()
{
	if (fdc.fifobufpos == fdc.tfifo)
	{
		fdc.fifobufpos = 0;
	}
	else
	{
		fdc.fifobufpos++;
	}
}

void fdc_fifo_buf_write(int val)
{
	fdc.fifobuf[fdc.fifobufpos] = val;
	fdc_fifo_buf_advance();
}

int fdc_fifo_buf_read()
{
	int temp = 0;
	temp = fdc.fifobuf[fdc.fifobufpos];
	fdc_fifo_buf_advance();
	return temp;
}

static void fdc_int()
{
        if (!fdc.pcjr)
	{
		if (fdc.dor & 8)
		{
        	        picint(1 << 6);
			fdc.fintr = 1;
		}
	}
}

static void fdc_watchdog_poll(void *p)
{
        FDC *fdc = (FDC *)p;

        fdc->watchdog_count--;
        if (fdc->watchdog_count)
                fdc->watchdog_timer += 1000 * TIMER_USEC;
        else
        {        
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

static void fdc_rate(int drive);

void fdc_update_rates()
{
	fdc_rate(0);
	fdc_rate(1);
	fdc_rate(2);
	fdc_rate(3);
}

void fdc_update_is_nsc(int is_nsc)
{
	int old_is_nsc = fdc.is_nsc;
	fdc.is_nsc = is_nsc;
	if (old_is_nsc != is_nsc)
	{
		fdc.densel_force = fdc.densel_force ^ 3;
	}
	fdc_update_rates();
}

void fdc_update_max_track(int max_track)
{
	fdc.max_track = max_track;
}

void fdc_update_enh_mode(int enh_mode)
{
	fdc.enh_mode = enh_mode;
	fdc_update_rates();
}

int fdc_get_rwc(int drive)
{
	return fdc.rwc[drive];
}

void fdc_update_rwc(int drive, int rwc)
{
	fdc.rwc[drive] = rwc;
	fdc_rate(drive);
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
	fdc_update_rates();
}

uint8_t fdc_get_densel_polarity()
{
	return fdc.densel_polarity;
}

void fdc_update_densel_force(int densel_force)
{
	fdc.densel_force = densel_force;
	fdc_update_rates();
}

void fdc_update_drvrate(int drive, int drvrate)
{
	fdc.drvrate[drive] = drvrate;
	fdc_rate(drive);
}

void fdc_update_drv2en(int drv2en)
{
	fdc.drv2en = drv2en;
}

void fdc_update_rate(int drive)
{
	if (((fdc.rwc[drive] == 1) || (fdc.rwc[drive] == 2)) && fdc.enh_mode)
	{
		bit_rate = 500;
	}
	else if ((fdc.rwc[drive] == 3) && fdc.enh_mode)
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
}

int fdc_get_bit_rate()
{
	switch(bit_rate)
	{
		case 500:
			return 0;
		case 300:
			return 1;
		case 2000:
			return 1 | 4;
		case 250:
			return 2;
		case 1000:
			return 3;
		default:
			return 2;
	}
	return 2;
}

int fdc_get_bitcell_period()
{
        return fdc.bitcell_period;
}

static int fdc_get_densel(int drive)
{
	if (fdc.enh_mode)
	{
		switch (fdc.rwc[drive])
		{
			case 1:
			case 3:
				return 0;
			case 2:
				return 1;
		}
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

	return 0;
}

static void fdc_rate(int drive)
{
	fdc_update_rate(drive);
	disc_set_rate(drive, fdc.drvrate[drive], fdc.rate);
	fdd_set_densel(fdc_get_densel(drive));
}

void fdc_seek(int drive, int params)
{
	fdd_seek(drive, params);
	fdc.stat |= (1 << fdc.drive);
}

int real_drive(int drive)
{
	if (drive < 2)
	{
		return drive ^ fdd_swap;
	}
	else
	{
		return drive;
	}
}

void fdc_implied_seek()
{
	if (fdc.config & 0x40)
	{
		if (fdc.params[1] != fdc.pcn[fdc.params[0] & 3])
		{
			fdc_seek(fdc.drive, ((int) fdc.params[1]) - ((int) fdc.pcn[fdc.params[0] & 3]));
			fdc.pcn[fdc.params[0] & 3] = fdc.params[1];
		}
	}
}

void fdc_write(uint16_t addr, uint8_t val, void *priv)
{
	int drive, i, drive_num;
	int seek_time, seek_time_base;

	fdc_log("Write FDC %04X %02X\n",addr,val);

        switch (addr&7)
        {
		case 0: return;
                case 1: return;
                case 2: /*DOR*/
                if (fdc.pcjr)
                {
                        if ((fdc.dor & 0x40) && !(val & 0x40))
                        {
                                fdc.watchdog_timer = 1000 * TIMER_USEC;
                                fdc.watchdog_count = 1000;
                                picintc(1 << 6);
                        }
                        if ((val & 0x80) && !(fdc.dor & 0x80))
                        {
        			timer_process();
                                disctime = 128 * (1 << TIMER_SHIFT);
                                timer_update_outstanding();
                                discint=-1;
                                fdc_reset();
                        }
			if (!fdd_get_flags(0))
			{
				val &= 0xfe;
			}
                        motoron[0 ^ fdd_swap] = val & 0x01;
                }
                else
                {
			if (!(val & 8) && (fdc.dor & 8))
			{
				fdc.tc = 1;
				fdc_int();
			}
			if (!(val&4))
			{
				disc_stop(real_drive(val & 3));
				fdc.stat=0x00;
                                fdc.pnum=fdc.ptot=0;
			}
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
				fdc.perp &= 0xfc;
                                fdc_reset();
                        }
			timer_process();
			timer_update_outstanding();
			/* We can now simplify this since each motor now spins separately. */
			for (i = 0; i < FDD_NUM; i++)
			{
				drive_num = real_drive(i);
				if ((!fdd_get_flags(drive_num)) || (drive_num >= FDD_NUM))
				{
					val &= ~(0x10 << drive_num);
				}
				else
				{
					motoron[i] = (val & (0x10 << drive_num));
				}
			}
			drive_num = real_drive(val & 3);
                }
                fdc.dor=val;
                return;
		case 3:
		/* TDR */
		if (fdc.enh_mode)
		{
			drive = real_drive(fdc.dor & 3);
			fdc_update_rwc(drive, (val & 0x30) >> 4);
		}
		return;
                case 4:
                if (val & 0x80)
                {
			timer_process();
                        disctime = 128 * (1 << TIMER_SHIFT);
                        timer_update_outstanding();
                        discint=-1;
			fdc.perp &= 0xfc;
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
                if (fdc.pnum==fdc.ptot)
                {
			if ((fdc.stat & 0xf0) != 0x80)
			{
				/* If bit 4 of the MSR is set, or the MSR is 0x00, the FDC is NOT in the command phase, therefore do NOT accept commands. */
				return;
			}

			fdc.stat &= 0xf;

                        fdc.tc = 0;
                        fdc.data_ready = 0;
                        
                        fdc.command=val;
			fdc.stat |= 0x10;
                        fdc_log("Starting FDC command %02X\n",fdc.command);
                        switch (fdc.command&0x1F)
                        {
				case 1: /*Mode*/
				if (!fdc.is_nsc)  goto bad_command;
                                fdc.pnum=0;
                                fdc.ptot=4;
                                fdc.stat |= 0x90;
                                fdc.pos=0;
                                fdc.format_state = 0;
                                break;

                                case 2: /*Read track*/
				fdc.satisfying_sectors=0;
				fdc.sc=0;
				fdc.wrong_am=0;
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat |= 0x90;
                                fdc.pos=0;
				fdc.mfm=(fdc.command&0x40)?1:0;
                                break;
                                case 3: /*Specify*/
                                fdc.pnum=0;
                                fdc.ptot=2;
                                fdc.stat |= 0x90;
                                break;
                                case 4: /*Sense drive status*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat |= 0x90;
                                break;
                                case 5: /*Write data*/
                                case 9: /*Write deleted data*/
				fdc.satisfying_sectors=0;
				fdc.sc=0;
				fdc.wrong_am=0;
				fdc.deleted = ((fdc.command&0x1F) == 9) ? 1 : 0;
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat |= 0x90;
                                fdc.pos=0;
				fdc.mfm=(fdc.command&0x40)?1:0;
                                break;
                                case 6: /*Read data*/
                                case 0xC: /*Read deleted data*/
                                case 0x11: /*Scan equal*/
                                case 0x19: /*Scan low or equal*/
                                case 0x16: /*Verify*/
                                case 0x1D: /*Scan high or equal*/
				fdc.satisfying_sectors=0;
				fdc.sc=0;
				fdc.wrong_am=0;
				fdc.deleted = ((fdc.command&0x1F) == 0xC) ? 1 : 0;
				if ((fdc.command&0x1F) == 0x16)  fdc.deleted = 2;
				fdc.deleted |= (fdc.command & 0x20);
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat |= 0x90;
                                fdc.pos=0;
				fdc.mfm=(fdc.command&0x40)?1:0;
                                break;
                                case 7: /*Recalibrate*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat |= 0x90;
                                break;
                                case 8: /*Sense interrupt status*/
				if (!fdc.fintr && !fdc_reset_stat)  goto bad_command;			
                                fdc.lastdrive = fdc.drive;
                                discint = 8;
                                fdc.pos = 0;
                                fdc_callback();
                                break;
                                case 10: /*Read sector ID*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat |= 0x90;
                                fdc.pos=0;
				fdc.mfm=(fdc.command&0x40)?1:0;
                                break;
                                case 0x0d: /*Format track*/
                                fdc.pnum=0;
                                fdc.ptot=5;
                                fdc.stat |= 0x90;
                                fdc.pos=0;
                                fdc.format_state = 0;
				fdc.mfm=(fdc.command&0x40)?1:0;
                                break;
                                case 15: /*Seek*/
                                fdc.pnum=0;
                                fdc.ptot=2;
                                fdc.stat |= 0x90;
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
				if (!AT || fdc.pcjr || fdc.ps1)  goto bad_command;
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat |= 0x90;
                                fdc.pos=0;
                                break;
                                case 0x13: /*Configure*/
                                fdc.pnum=0;
                                fdc.ptot=3;
                                fdc.stat |= 0x90;
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
                                fdc.stat |= 0x10;
                                discint=0xfc;
        			timer_process();
        			disctime = 200 * (1 << TIMER_SHIFT);
        			timer_update_outstanding();
                                break;
                        }
                }
                else
                {
			fdc.stat = 0x10 | (fdc.stat & 0xf);
                        fdc.params[fdc.pnum++]=val;
			if (fdc.pnum == 1)
			{
				if (command_has_drivesel[fdc.command & 0x1F])
				{
					fdc.drive = fdc.dor & 3;
	                                fdc.rw_drive = fdc.params[0] & 3;
					if (((fdc.command & 0x1F) == 7) || ((fdc.command & 0x1F) == 15))
					{
						fdc.stat |= (1 << real_drive(fdc.drive));
					}
				}
			}
                        if (fdc.pnum==fdc.ptot)
                        {
                                fdc_log("Got all params %02X\n", fdc.command);
                                discint=fdc.command&0x1F;
        			timer_process();
       				disctime = 1024 * (1 << TIMER_SHIFT);
        			timer_update_outstanding();
                                fdc_reset_stat = 0;
                                switch (discint & 0x1F)
                                {
                                        case 2: /*Read a track*/
					fdc_reset_fifo_buf();
					fdc_rate(fdc.drive);
                                        fdc.head=fdc.params[2];
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
                                        fdc.sector=fdc.params[3];
                                        fdc.eot[fdc.drive] = fdc.params[5];
					fdc.gap = fdc.params[6];
					fdc.dtl = fdc.params[7];
					fdc.read_track_sector.id.c = fdc.params[1];
					fdc.read_track_sector.id.h = fdc.params[2];
					fdc.read_track_sector.id.r = 1;
					fdc.read_track_sector.id.n = fdc.params[4];
					fdc_implied_seek();
                                        fdc.rw_track = fdc.params[1];
					disc_readsector(fdc.drive, SECTOR_FIRST, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);
					if (fdc.pcjr || !fdc.dma)
					{
						fdc.stat = 0x70;
					}
					else
					{
						fdc.stat = 0x50;
					}
					disctime = 0;
					update_status_bar_icon(fdc.drive, 1);
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

			                case 0x12:
                                        fdc.stat=0x80;
					if (fdc.params[0] & 0x80)
					{
						fdc.perp = fdc.params[0] & 0x3f;
					}
					else
					{
						fdc.perp &= 0xfc;
						fdc.perp |= (fdc.params[0] & 0x03);
					}
			                disctime = 0;
			                return;

					case 4:
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
					break;

                                        case 5: /*Write data*/
                                        case 9: /*Write deleted data*/
					fdc_reset_fifo_buf();
					fdc_rate(fdc.drive);
                                        fdc.head=fdc.params[2];
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
                                        fdc.sector=fdc.params[3];
                                        fdc.eot[fdc.drive] = fdc.params[5];
					fdc.gap = fdc.params[6];
					fdc.dtl = fdc.params[7];
					fdc_implied_seek();
                                        fdc.rw_track = fdc.params[1];                                        
	                                disc_writesector(fdc.drive, fdc.sector, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);
        	                        disctime = 0;
	                                fdc.written = 0;
        	                        update_status_bar_icon(fdc.drive, 1);
                	                fdc.pos = 0;
	                                if (fdc.pcjr)
	                                        fdc.stat = 0xb0;
					else
					{
						if (fdc.dma)
						{
		                                        fdc.stat = 0x90;
						}
						else
						{
		                                        fdc.stat = 0xb0;
						}
					}
                                        break;
                                        
                                        case 0x11: /*Scan equal*/
                                        case 0x19: /*Scan low or equal*/
                                        case 0x1D: /*Scan high or equal*/
					fdc_reset_fifo_buf();
					fdc_rate(fdc.drive);
                                        fdc.head=fdc.params[2];
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
                                        fdc.sector=fdc.params[3];
                                        fdc.eot[fdc.drive] = fdc.params[5];
					fdc.gap = fdc.params[6];
					fdc.dtl = fdc.params[7];
					fdc_implied_seek();
                                        fdc.rw_track = fdc.params[1];                                        
	                                disc_comparesector(fdc.drive, fdc.sector, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);
        	                        disctime = 0;
	                                fdc.written = 0;
        	                        update_status_bar_icon(fdc.drive, 1);
                	                fdc.pos = 0;
					if (fdc.pcjr || !fdc.dma)
					{
						fdc.stat = 0xb0;
					}
					else
					{
						fdc.stat = 0x90;
					}
                                        break;
                                        
                                        case 0x16: /*Verify*/
					if (fdc.params[0] & 0x80)  fdc.sc = fdc.params[7];
                                        case 6: /*Read data*/
                                        case 0xC: /*Read deleted data*/
					fdc_reset_fifo_buf();
					fdc_rate(fdc.drive);
                                        fdc.head=fdc.params[2];
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
                                        fdc.sector=fdc.params[3];
                                        fdc.eot[fdc.drive] = fdc.params[5];
					fdc.gap = fdc.params[6];
					fdc.dtl = fdc.params[7];
					fdc_implied_seek();
                                        fdc.rw_track = fdc.params[1];
					fdc_log("Reading sector (drive %i) (%i) (%i %i %i %i) (%i %i %i)\n", fdc.drive, fdc.params[0], fdc.params[1], fdc.params[2], fdc.params[3], fdc.params[4], fdc.params[5], fdc.params[6], fdc.params[7]);
					if (((dma_mode(2) & 0x0C) == 0x00) && !fdc.pcjr && fdc.dma)
					{
						/* DMA is in verify mode, treat this like a VERIFY command. */
						fdc_log("Verify-mode read!\n");
						fdc.tc = 1;
						fdc.deleted |= 2;
					}
                        	        disc_readsector(fdc.drive, fdc.sector, fdc.params[1], fdc.head, fdc.rate, fdc.params[4]);
					if (fdc.pcjr || !fdc.dma)
					{
						fdc.stat = 0x70;
					}
					else
					{
						fdc.stat = 0x50;
					}
                	                disctime = 0;
        	                        update_status_bar_icon(fdc.drive, 1);
	                                fdc.inread = 1;
                                        break;
                                        
                                        case 7: /*Recalibrate*/
					seek_time_base = fdd_doublestep_40(real_drive(fdc.drive)) ? 10 : 5;
                                        fdc.stat =  (1 << real_drive(fdc.drive)) | 0x80;
                                        disctime = 0;

					drive_num = real_drive(fdc.drive);

					/* Three conditions under which the command should fail. */
					if (!fdd_get_flags(drive_num) || (drive_num >= FDD_NUM) || !motoron[drive_num] || fdd_track0(real_drive(drive_num)))
					{
						if (!fdd_get_flags(drive_num) || (drive_num >= FDD_NUM) || !motoron[drive_num])
						{
                        				fdc.st0 = 0x70 | (fdc.params[0] & 3);
						}
						else
						{
                        				fdc.st0 = 0x20 | (fdc.params[0] & 3);
						}
				                fdc.pcn[fdc.params[0] & 3] = 0;
						disctime = 0;
				                discint=-3;
						timer_process();
						disctime = 2048 * (1 << TIMER_SHIFT);
						timer_update_outstanding();
						break;
					}

					if ((real_drive(fdc.drive) != 1) || fdc.drv2en)
					{
	                                        fdc_seek(fdc.drive, -fdc.max_track);
					}
                                        disctime = fdc.max_track * seek_time_base * TIMER_USEC;
                                        break;

                                        case 0x0d: /*Format*/
					fdc_rate(fdc.drive);
                                        fdc.head = (fdc.params[0] & 4) ? 1 : 0;
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
					fdc.gap = fdc.params[3];
					fdc.dtl = 4000000;
					fdc.format_sectors = fdc.params[2];
					fdc.format_n = fdc.params[1];
                                        fdc.format_state = 1;
                                        fdc.pos = 0;
                                        fdc.stat = 0x10;
                                        break;
                                        
                                        case 0xf: /*Seek*/
                                        fdc.stat =  (1 << fdc.drive) | 0x80;
                                        fdc.head = (fdc.params[0] & 4) ? 1 : 0;
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
                                        disctime = 0;

					drive_num = real_drive(fdc.drive);
					seek_time_base = fdd_doublestep_40(drive_num) ? 10 : 5;

					/* Three conditions under which the command should fail. */
					if (!fdd_get_flags(drive_num) || (drive_num >= FDD_NUM) || !motoron[drive_num])
					{
						/* Yes, failed SEEK's still report success, unlike failed RECALIBRATE's. */
                        			fdc.st0 = 0x20 | (fdc.params[0] & 7);
						if (fdc.command & 0x80)
						{
							if (fdc.command & 0x40)
							{
						                fdc.pcn[fdc.params[0] & 3] += fdc.params[1];
							}
							else
							{
						                fdc.pcn[fdc.params[0] & 3] -= fdc.params[1];
							}
						}
						else
						{
					                fdc.pcn[fdc.params[0] & 3] = fdc.params[1];
						}
						disctime = 0;
				                discint=-3;
						timer_process();
						disctime = 2048 * (1 << TIMER_SHIFT);
						timer_update_outstanding();
						break;
					}

					if (fdc.command & 0x80)
					{
						if (fdc.params[1])
						{
							if (fdc.command & 0x40)
							{
								/* Relative seek inwards. */
								fdc_seek(fdc.drive, fdc.params[1]);
						                fdc.pcn[fdc.params[0] & 3] += fdc.params[1];
							}
							else
							{
								/* Relative seek outwards. */
								fdc_seek(fdc.drive, -fdc.params[1]);
						                fdc.pcn[fdc.params[0] & 3] -= fdc.params[1];
							}
	        	                                disctime = ((int) fdc.params[1]) * seek_time_base * TIMER_USEC;
						}
						else
						{
							disctime = seek_time_base * TIMER_USEC;

                        				fdc.st0 = 0x20 | (fdc.params[0] & 7);
							disctime = 0;
					                discint=-3;
							timer_process();
							disctime = 2048 * (1 << TIMER_SHIFT);
							timer_update_outstanding();
							break;
						}
					}
					else
					{
						fdc_log("Seeking to track %i...\n", fdc.params[1]);
						seek_time = ((int) (fdc.params[1] - fdc.pcn[fdc.params[0] & 3])) * seek_time_base * TIMER_USEC;

						if ((fdc.params[1] - fdc.pcn[fdc.params[0] & 3]) == 0)
						{
                        				fdc.st0 = 0x20 | (fdc.params[0] & 7);
							disctime = 0;
					                discint=-3;
							timer_process();
							disctime = 2048 * (1 << TIMER_SHIFT);
							timer_update_outstanding();
							break;
						}

	                                        fdc_seek(fdc.drive, fdc.params[1] - fdc.pcn[fdc.params[0] & 3]);
				                fdc.pcn[fdc.params[0] & 3] = fdc.params[1];
						if (seek_time < 0)  seek_time = -seek_time;
	                                        disctime = seek_time;
					}
                                        break;
                                        
                                        case 10: /*Read sector ID*/
					fdc_rate(fdc.drive);
                                        disctime = 0;
                                        fdc.head = (fdc.params[0] & 4) ? 1 : 0;                                        
					fdd_set_head(fdc.drive, (fdc.params[0] & 4) ? 1 : 0);
					if ((real_drive(fdc.drive) != 1) || fdc.drv2en)
					{
						disc_readaddress(fdc.drive, fdc.head, fdc.rate);
						if (fdc.pcjr || !fdc.dma)
						{
							fdc.stat = 0x70;
						}
						else
						{
							fdc.stat = 0x50;
						}
					}
					else
						fdc_noidam();
                                        break;
                                }
                        }
			else
			{
				fdc.stat = 0x90 | (fdc.stat & 0xf);
			}
                }
                return;
                case 7:
                        if (!AT) return;
                fdc.rate=val&3;

                disc_3f7=val;
                return;
        }
}

int paramstogo=0;
uint8_t fdc_read(uint16_t addr, void *priv)
{
        uint8_t temp;
        int drive;
	fdc_log("Read FDC %04X\n",addr);
        switch (addr&7)
        {
		case 0:		/* STA */
		return 0xff;
		break;
                case 1:		/* STB */
		if (is486)
		{
			return 0xff;
		}
                drive = real_drive(fdc.dor & 3);
                if (!fdc.enable_3f1)
                        return 0xff;
                temp = 0x70;
                if (drive)
                        temp &= ~0x40;
                else
                        temp &= ~0x20;
                        
                if (fdc.dor & 0x10)
                        temp |= 1;
                if (fdc.dor & 0x20)
                        temp |= 2;
                break;
		case 2:
		temp = fdc.dor;
		break;
                case 3:
                drive = real_drive(fdc.dor & 3);
                if (fdc.ps1)
                {
                        /*PS/1 Model 2121 seems return drive type in port 0x3f3,
                          despite the 82077AA FDC not implementing this. This is
                          presumably implemented outside the FDC on one of the
                          motherboard's support chips.*/
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
		if (!(fdc.dor & 4) & !fdc.pcjr)
		{
			return 0;
		}
                temp=fdc.stat;
                break;
                case 5: /*Data*/
                fdc.stat&=~0x80;
                if ((fdc.stat & 0xf0) == 0xf0)
                {
			if (fdc.pcjr || !fdc.fifo)
	                        temp = fdc.dat;
			else
			{
				temp = fdc_fifo_buf_read();
			}
                        break;
                }
                if (paramstogo)
                {
                        paramstogo--;
                        temp=fdc.res[10 - paramstogo];
                        if (!paramstogo)
                        {
                                fdc.stat=0x80;
                        }
                        else
                        {
                                fdc.stat|=0xC0;
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
		/* What the heck is this even doing?! */
                /* if (discint==0xA) 
		{
			timer_process();
			disctime = 1024 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
		} */
                fdc.stat &= 0xf0;
                break;
                case 7: /*Disk change*/
                drive = real_drive(fdc.dor & 3);
                if (fdc.dor & (0x10 << drive))
                   temp = (disc_changed[drive] || drive_empty[drive])?0x80:0;
                else
                   temp = 0;
                if (fdc.dskchg_activelow)  /*PC2086/3086 seem to reverse this bit*/
                   temp ^= 0x80;
		temp |= 0x01;
                break;
                default:
                        temp=0xFF;
        }
        return temp;
}

void fdc_poll_common_finish(int compare, int st5)
{
        fdc_int();
	fdc.fintr = 0;
        fdc.stat=0xD0;
        fdc.res[4]=(fdd_get_head(real_drive(fdc.drive))?4:0)|fdc.rw_drive;
        fdc.res[5]=st5;
	fdc.res[6]=0;
	if (fdc.wrong_am)
	{
		fdc.res[6] |= 0x40;
		fdc.wrong_am = 0;
	}
	if (compare == 1)
	{
		if (!fdc.satisfying_sectors)
		{
			fdc.res[6] |= 4;
		}
		else if (fdc.satisfying_sectors == (fdc.params[5] << ((fdc.command & 80) ? 1 : 0)))
		{
			fdc.res[6] |= 8;
		}
	}
	else if (compare == 2)
	{
		if (fdc.satisfying_sectors & 1)
		{
			fdc.res[5] |= 0x20;
		}
		if (fdc.satisfying_sectors & 2)
		{
			fdc.res[5] |= 0x20;
			fdc.res[6] |= 0x20;
		}
		if (fdc.satisfying_sectors & 4)
		{
			fdc.res[5] |= 0x04;
		}
		if (fdc.satisfying_sectors & 8)
		{
			fdc.res[5] |= 0x04;
			fdc.res[6] |= 0x02;
		}
		if (fdc.satisfying_sectors & 0x10)
		{
			fdc.res[5] |= 0x04;
			fdc.res[6] |= 0x10;
		}
	}
        fdc.res[7]=fdc.rw_track;
        fdc.res[8]=fdc.head;
        fdc.res[9]=fdc.sector;
        fdc.res[10]=fdc.params[4];
	update_status_bar_icon(fdc.drive, 0);
        paramstogo=7;
}

void fdc_poll_readwrite_finish(int compare)
{
        fdc.inread = 0;
        discint=-2;

	fdc_poll_common_finish(compare, 0);
}

void fdc_no_dma_end(int compare)
{
        disctime = 0;

	fdc_poll_common_finish(compare, 0x80);
}

void fdc_callback()
{
	int compare = 0;
	int drive_num = 0;
	int old_sector = 0;
        disctime = 0;
        switch (discint)
        {
                case -3: /*End of command with interrupt*/
                fdc_int();
                fdc.stat = (fdc.stat & 0xf) | 0x80;
		return;
                case -2: /*End of command*/
                fdc.stat = (fdc.stat & 0xf) | 0x80;
                return;
                case -1: /*Reset*/
                fdc_int();
		fdc.fintr = 0;
		memset(fdc.pcn, 0, 4);
                fdc_reset_stat = 4;
                return;
		case 1: /*Mode*/
		fdc.stat=0x80;
		fdc.densel_force = (fdc.params[2] & 0xC0) >> 6;
		return;

                case 2: /*Read track*/
                update_status_bar_icon(fdc.drive, 1);
                fdc.eot[fdc.drive]--;
		fdc.read_track_sector.id.r++;
                if (!fdc.eot[fdc.drive] || fdc.tc)
                {
			fdc_poll_readwrite_finish(2);
                        return;
                }
                else
		{
                        disc_readsector(fdc.drive, SECTOR_NEXT, fdc.rw_track, fdc.head, fdc.rate, fdc.params[4]);
			if (fdc.pcjr || !fdc.dma)
			{
				fdc.stat = 0x70;
			}
			else
			{
				fdc.stat = 0x50;
			}
		}
                fdc.inread = 1;
                return;

                case 4: /*Sense drive status*/
                fdc.res[10] = (fdc.params[0] & 7) | 0x28;
		if ((real_drive(fdc.drive) != 1) || fdc.drv2en)
		{
	                if (fdd_track0(real_drive(fdc.drive)))
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
                case 9: /*Write deleted data*/
                case 6: /*Read data*/
                case 0xC: /*Read deleted data*/
		case 0x11: /*Scan equal*/
		case 0x19: /*Scan low or equal*/
                case 0x1C: /*Verify*/
		case 0x1D: /*Scan high or equal*/
		if ((discint == 0x11) || (discint == 0x19) || (discint == 0x1D))
		{
			compare = 1;
		}
		else
		{
			compare = 0;
		}
		if ((discint == 6) || (discint == 0xC))
		{
			if (fdc.wrong_am && !(fdc.deleted & 0x20))
			{
				/* Mismatching data address mark and no skip, set TC. */
				fdc.tc = 1;
			}
		}
		old_sector = fdc.sector;
		if (fdc.tc)
		{
			fdc.sector++;
			fdc_poll_readwrite_finish(compare);
			return;
		}
		if ((discint == 0x16) && (fdc.params[0] & 0x80))
		{
			/* VERIFY command, EC set */
			fdc.sc--;
			if (!fdc.sc)
			{
				fdc.sector++;
				fdc_poll_readwrite_finish(0);
				return;
			}
			/* The rest is processed normally per MT flag and EOT. */
		}
		else if ((discint == 0x16) && !(fdc.params[0] & 0x80))
		{
			/* VERIFY command, EC clear */
			if ((fdc.sector == old_sector) && (fdc.head == (fdc.command & 0x80) ? 1 : 0))
			{
				fdc.sector++;
				fdc_poll_readwrite_finish(0);
				return;
			}
		}
                if (fdc.sector == fdc.params[5])
                {
			/* Reached end of track, MT bit is clear */
			if (!(fdc.command & 0x80))
			{
				fdc.rw_track++;
				fdc.sector = 1;
				if (!fdc.pcjr && fdc.dma && (old_sector == 255))
				{
					fdc_no_dma_end(compare);
				}
				else
				{
					fdc_poll_readwrite_finish(compare);
				}
				return;
			}
			/* Reached end of track, MT bit is set, head is 1 */
			if (fdd_get_head(real_drive(fdc.drive)) == 1)
			{
				fdc.rw_track++;
				fdc.sector = 1;
                                fdc.head &= 0xFE;
				fdd_set_head(fdc.drive, 0);
				if (!fdc.pcjr && fdc.dma && (old_sector == 255))
				{
					fdc_no_dma_end(compare);
				}
				else
				{
					fdc_poll_readwrite_finish(compare);
				}
				return;
			}
                        if ((fdc.command & 0x80) && (fdd_get_head(real_drive(fdc.drive)) == 0))
			{
	                        fdc.sector = 1;
                                fdc.head |= 1;
				fdd_set_head(fdc.drive, 1);
			}
                }
		else if (fdc.sector < fdc.params[5])
		{
	                fdc.sector++;
		}
                update_status_bar_icon(fdc.drive, 1);
		switch (discint)
		{
			case 5:
			case 9:
		                disc_writesector(fdc.drive, fdc.sector, fdc.rw_track, fdc.head, fdc.rate, fdc.params[4]);
				if (fdc.pcjr || !fdc.dma)
				{
					fdc.stat = 0xb0;
				}
				else
				{
					fdc.stat = 0x90;
				}
				break;
			case 6:
			case 0xC:
			case 0x16:
		                disc_readsector(fdc.drive, fdc.sector, fdc.rw_track, fdc.head, fdc.rate, fdc.params[4]);
				if (fdc.pcjr || !fdc.dma)
				{
					fdc.stat = 0x70;
				}
				else
				{
					fdc.stat = 0x50;
				}
				break;
			case 0x11:
			case 0x19:
			case 0x1D:
		                disc_comparesector(fdc.drive, fdc.sector, fdc.rw_track, fdc.head, fdc.rate, fdc.params[4]);
				if (fdc.pcjr || !fdc.dma)
				{
					fdc.stat = 0xb0;
				}
				else
				{
					fdc.stat = 0x90;
				}
				break;
		}
                fdc.inread = 1;
                return;

                case 7: /*Recalibrate*/
                fdc.pcn[fdc.params[0] & 3] = 0;
		drive_num = real_drive(fdc.rw_drive);
		fdc.st0 = 0x20 | (fdc.params[0] & 3);
                if (!fdd_track0(drive_num))
		{
			fdc.st0 |= 0x50;
		}
                discint=-3;
		timer_process();
		disctime = 2048 * (1 << TIMER_SHIFT);
		timer_update_outstanding();
                fdc.stat = 0x80 | (1 << fdc.drive);
                return;

                case 8: /*Sense interrupt status*/

                fdc.stat    = (fdc.stat & 0xf) | 0xd0;

		if (fdc.fintr)
		{
			fdc.res[9] = fdc.st0;
			fdc.fintr = 0;
		}
		else
		{
			if (fdc_reset_stat)
			{
				drive_num = real_drive(4 - fdc_reset_stat);
				if ((!fdd_get_flags(drive_num)) || (drive_num >= FDD_NUM))
				{
					disc_stop(drive_num);
					fdd_set_head(drive_num, 0);
        	        	        fdc.res[9] = 0xc0 | (4 - fdc_reset_stat) | (fdd_get_head(drive_num) ? 4 : 0);
				}
				else
				{
        	        	        fdc.res[9] = 0xc0 | (4 - fdc_reset_stat);
				}

				fdc_reset_stat--;
			}
		}

		fdc.res[10] = fdc.pcn[fdc.res[9] & 3];

                paramstogo = 2;
                discint = 0;
		disctime = 0;
		return;
                
                case 0x0d: /*Format track*/
                if (fdc.format_state == 1)
                {
                        fdc.format_state = 2;
			timer_process();
			disctime = 128 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
                }                
                else if (fdc.format_state == 2)
                {
                        disc_format(fdc.drive, fdc.head, fdc.rate, fdc.params[4]);
                        fdc.format_state = 3;
                }
                else
                {
                        discint=-2;
                        fdc_int();
			fdc.fintr = 0;
                        fdc.stat=0xD0;
                        fdc.res[4] = (fdd_get_head(real_drive(fdc.drive))?4:0)|fdc.drive;
                        fdc.res[5] = fdc.res[6] = 0;
                        fdc.res[7] = fdc.pcn[fdc.params[0] & 3];
                        fdc.res[8] = fdd_get_head(real_drive(fdc.drive));
                        fdc.res[9] = fdc.format_dat[fdc.pos - 2] + 1;
                        fdc.res[10] = fdc.params[4];
                        paramstogo=7;
                        fdc.format_state = 0;
                        return;
                }
                return;
                
                case 15: /*Seek*/
		drive_num = real_drive(fdc.rw_drive);
		fdc.st0 = 0x20 | (fdc.params[0] & 7);
                discint=-3;
		timer_process();
		disctime = 2048 * (1 << TIMER_SHIFT);
		timer_update_outstanding();
                fdc.stat = 0x80 | (1 << fdc.drive);
                return;
                case 0x0e: /*Dump registers*/
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
       	        fdc.res[1] = fdc.pcn[0];
                fdc.res[2] = fdc.pcn[1];
               	fdc.res[3] = fdc.pcn[2];
       	        fdc.res[4] = fdc.pcn[3];
                fdc.res[5] = fdc.specify[0];
               	fdc.res[6] = fdc.specify[1];
       	        fdc.res[7] = fdc.eot[fdc.drive];
                fdc.res[8] = (fdc.perp & 0x7f) | ((fdc.lock) ? 0x80 : 0);
		fdc.res[9] = fdc.config;
		fdc.res[10] = fdc.pretrk;
		paramstogo = 10;
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
                
	        case 0x13: /*Configure*/
                fdc.config = fdc.params[1];
                fdc.pretrk = fdc.params[2];
		fdc.fifo = (fdc.params[1] & 0x20) ? 0 : 1;
		fdc.tfifo = (fdc.params[1] & 0xF) + 1;
                fdc.stat = 0x80;
                disctime = 0;
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
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                fdc.res[10] = fdc.st0;
                paramstogo=1;
                discint=0;
                disctime = 0;
                return;
        }
}

void fdc_error(int st5, int st6)
{
        disctime = 0;

        fdc_int();
	fdc.fintr = 0;
        fdc.stat=0xD0;
        fdc.res[4]=0x40|(fdd_get_head(real_drive(fdc.drive))?4:0)|fdc.rw_drive;
        fdc.res[5]=st5;
        fdc.res[6]=st6;
	switch(discint)
	{
		case 0x02:
		case 0x05:
		case 0x06:
		case 0x09:
		case 0x0C:
		case 0x11:
		case 0x16:
		case 0x19:
		case 0x1D:
		        fdc.res[7]=fdc.rw_track;
		        fdc.res[8]=fdc.head;
		        fdc.res[9]=fdc.sector;
		        fdc.res[10]=fdc.params[4];
			break;
		default:
		        fdc.res[7]=0;
		        fdc.res[8]=0;
		        fdc.res[9]=0;
		        fdc.res[10]=0;
			break;
	}
	update_status_bar_icon(fdc.drive, 0);
        paramstogo=7;
}


void fdc_overrun()
{
        disc_stop(fdc.drive);

	fdc_error(0x10, 0);
}

int fdc_is_verify()
{
	return (fdc.deleted & 2) ? 1 : 0;
}

int fdc_data(uint8_t data)
{
	int result = 0;

	if (fdc.deleted & 2)
	{
		/* We're in a VERIFY command, so return with 0. */
		return 0;
	}

        if (fdc.pcjr || !fdc.dma)
        {
        	if (fdc.tc)
                	return 0;

                if (fdc.data_ready)
                {
                        fdc_overrun();
                        return -1;
                }

		if (fdc.pcjr || !fdc.fifo || (fdc.tfifo <= 1))
		{
	                fdc.dat = data;
	                fdc.data_ready = 1;
	                fdc.stat = 0xf0;
		}
		else
		{
			/* FIFO enabled */
			fdc_fifo_buf_write(data);
			if (fdc.fifobufpos == 0)
			{
				/* We have wrapped around, means FIFO is over */
				fdc.data_ready = 1;
				fdc.stat = 0xf0;
			}
		}
        }
        else
        {
		result = dma_channel_write(2, data);

        	if (fdc.tc)
                	return -1;

                if (result & DMA_OVER)
		{
	                fdc.data_ready = 1;
	                fdc.stat = 0xd0;
                        fdc.tc = 1;
			return -1;
		}

		if (!fdc.fifo || (fdc.tfifo <= 1))
		{
	                fdc.data_ready = 1;
	                fdc.stat = 0xd0;
		}
		else
		{
			fdc_fifo_buf_advance();
			if (fdc.fifobufpos == 0)
			{
				/* We have wrapped around, means FIFO is over */
				fdc.data_ready = 1;
				fdc.stat = 0xd0;
			}
		}
        }
        
        return 0;
}

void fdc_finishread()
{
        fdc.inread = 0;
}

void fdc_track_finishread(int condition)
{
	fdc.stat = 0x10;
	fdc.satisfying_sectors |= condition;
        fdc.inread = 0;
	fdc_callback();
}

void fdc_sector_finishcompare(int satisfying)
{
	fdc.stat = 0x10;
	fdc.satisfying_sectors++;
        fdc.inread = 0;
	fdc_callback();
}

void fdc_sector_finishread()
{
	fdc.stat = 0x10;
        fdc.inread = 0;
	fdc_callback();
}

/* There is no sector ID. */
void fdc_noidam()
{
	fdc_error(1, 0);
}

/* Sector ID's are there, but there is no sector. */
void fdc_nosector()
{
	fdc_error(4, 0);
}

/* There is no sector data. */
void fdc_nodataam()
{
	fdc_error(1, 1);
}

/* Abnormal termination with both status 1 and 2 set to 0, used when abnormally
   terminating the FDC FORMAT TRACK command. */
void fdc_cannotformat()
{
	fdc_error(0, 0);
}

void fdc_datacrcerror()
{
	fdc_error(0x20, 0x20);
}

void fdc_headercrcerror()
{
	fdc_error(0x20, 0);
}

void fdc_wrongcylinder()
{
	fdc_error(4, 0x10);
}

void fdc_badcylinder()
{
	fdc_error(4, 0x02);
}

void fdc_writeprotect()
{
	fdc_error(0x02, 0);
}

int fdc_getdata(int last)
{
        int data;
        
        if (fdc.pcjr || !fdc.dma)
        {
                if (fdc.written)
                {
                        fdc_overrun();
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
			fdc_fifo_buf_advance();

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
        fdc_int();
        fdc.stat=0xD0;
        fdc.res[4]=(fdd_get_head(real_drive(fdc.drive))?4:0)|fdc.drive;
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
	return;
}

void fdc_hard_reset()
{
	int base_address = fdc.base_address;

	memset(&fdc, 0, sizeof(FDC));
	fdc.dskchg_activelow = 0;
	fdc.enable_3f1 = 1;

	fdc_update_is_nsc(0);
	fdc_update_enh_mode(0);
	fdc_update_densel_polarity(1);
	fdc_update_rwc(0, 0);
	fdc_update_rwc(1, 0);
	fdc_update_rwc(2, 0);
	fdc_update_rwc(3, 0);
	fdc_update_densel_force(0);
	fdc_update_drv2en(1);

	fdc.fifo = 0;
	fdc.tfifo = 1;

	fdd_init();

	if (fdc.pcjr)
	{
		fdc.dma = 0;
		fdc.specify[1] = 1;
	}
	else
	{
		fdc.dma = 1;
		fdc.specify[1] = 0;
	}
	fdc.config = 0x20;
	fdc.pretrk = 0;

	swwp = 0;
	disable_write = 0;

	fdc_reset();

	fdc.max_track = 79;
	fdc.base_address = base_address;
}

void fdc_init()
{
	fdc_hard_reset();

	timer_add(fdc_callback, &disctime, &disctime, NULL);
}

void fdc_add()
{
        io_sethandler(0x03f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        io_sethandler(0x03f7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        fdc.pcjr = 0;
        fdc.ps1 = 0;
	fdc.max_track = 79;
	fdc.perp = 0;
	fdc.base_address = 0x03f0;
	fdc_log("FDC Added (%04X)\n", fdc.base_address);
}

void fdc_set_base(int base, int super_io)
{
        io_sethandler(base + (super_io ? 2 : 0), super_io ? 0x0004 : 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
       	io_sethandler(base + 7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
	fdc.base_address = base;
	fdc_log("FDC Base address set%s (%04X)\n", super_io ? " for Super I/O" : "", fdc.base_address);
}

void fdc_add_for_superio()
{
        io_sethandler(0x03f2, 0x0004, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        io_sethandler(0x03f7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        fdc.pcjr = 0;
        fdc.ps1 = 0;
	fdc.base_address = 0x03f0;
	fdc_log("FDC Added for Super I/O (%04X)\n", fdc.base_address);
}

void fdc_add_pcjr()
{
        io_sethandler(0x00f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
	timer_add(fdc_watchdog_poll, &fdc.watchdog_timer, &fdc.watchdog_timer, &fdc);
        fdc.pcjr = 1;
        fdc.ps1 = 0;
	fdc.max_track = 79;
	fdc.perp = 0;
	fdc.base_address = 0x03f0;
	fdc_log("FDC Added for PCjr (%04X)\n", fdc.base_address);
}

void fdc_remove()
{
	fdc_log("FDC Removed (%04X)\n", fdc.base_address);
        io_removehandler(fdc.base_address, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        io_removehandler(fdc.base_address + 7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);        
}

void fdc_discchange_clear(int drive)
{
        if (drive < FDD_NUM)
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

void fdc_set_ps1()
{
        fdc.ps1 = 1;
}
