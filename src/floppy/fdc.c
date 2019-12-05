/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the NEC uPD-765 and compatible floppy disk
 *		controller.
 *
 * Version:	@(#)fdc.c	1.0.21	2019/10/20
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../cpu/cpu.h"
#include "../machine/machine.h"
#include "../io.h"
#include "../dma.h"
#include "../pic.h"
#include "../timer.h"
#include "../ui.h"
#include "fdd.h"
#include "fdc.h"


extern uint64_t motoron[FDD_NUM];


const int command_has_drivesel[256] = {
	0, 0,
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


static uint8_t current_drive = 0;

static void fdc_callback(void *priv);
int lastbyte=0;

int floppymodified[4];
int floppyrate[4];

#ifdef ENABLE_FDC_LOG
int fdc_do_log = ENABLE_FDC_LOG;


static void
fdc_log(const char *fmt, ...)
{
   va_list ap;

   if (fdc_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define fdc_log(fmt, ...)
#endif


uint8_t
fdc_get_current_drive(void)
{
    return current_drive;
}


void
fdc_ctrl_reset(void *p)
{
    fdc_t *fdc = (fdc_t *) p;

    fdc->stat = 0x80;
    fdc->pnum = fdc->ptot=0;
    fdc->st0 = 0;
    fdc->lock = 0;
    fdc->head = 0;
    fdc->abort = 0;
    fdc->step = 0;
    if (!(fdc->flags & FDC_FLAG_AT))
	fdc->rate = 2;
}


sector_id_t
fdc_get_read_track_sector(fdc_t *fdc)
{
    return fdc->read_track_sector;
}


int
fdc_get_compare_condition(fdc_t *fdc)
{
    switch (fdc->interrupt) {
	case 0x11:
	default:
		return 0;
	case 0x19:
		return 1;
	case 0x1D:
		return 2;
    }
}


int
fdc_is_deleted(fdc_t *fdc)
{
    return fdc->deleted & 1;
}


int
fdc_is_sk(fdc_t *fdc)
{
    return (fdc->deleted & 0x20) ? 1 : 0;
}


void
fdc_set_wrong_am(fdc_t *fdc)
{
    fdc->wrong_am = 1;
}


int
fdc_get_drive(fdc_t *fdc)
{
    return fdc->drive;
}


int		fdc_get_bitcell_period(fdc_t *fdc);
int		fdc_get_bit_rate(fdc_t *fdc);
static void	fdc_rate(fdc_t *fdc, int drive);


int
fdc_get_perp(fdc_t *fdc)
{
    if (!(fdc->flags & FDC_FLAG_AT) || (fdc->flags & FDC_FLAG_PCJR))
	return 0;

    return fdc->perp;
}


int
fdc_get_gap2(fdc_t *fdc, int drive)
{
    int auto_gap2 = 22;

    if (!(fdc->flags & FDC_FLAG_AT) || (fdc->flags & FDC_FLAG_PCJR))
	return 22;

    if (fdc->perp & 3)
	return ((fdc->perp & 3) == 3) ? 41 : 22;
    else {
	auto_gap2 = (fdc_get_bit_rate(fdc) >= 3) ? 41 : 22;
	return (fdc->perp & (4 << drive)) ? auto_gap2 : 22;
    }
}


int
fdc_get_format_n(fdc_t *fdc)
{
    return fdc->format_n;
}


int
fdc_is_mfm(fdc_t *fdc)
{
    return fdc->mfm ? 1 : 0;
}


void
fdc_request_next_sector_id(fdc_t *fdc)
{
    if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma)
	fdc->stat = 0xf0;
    else
	fdc->stat = 0xd0;
}


void
fdc_stop_id_request(fdc_t *fdc)
{
    fdc->stat &= 0x7f;
}


int
fdc_get_gap(fdc_t *fdc)
{
    return fdc->gap;
}


int
fdc_get_dtl(fdc_t *fdc)
{
    return fdc->dtl;
}


int
fdc_get_format_sectors(fdc_t *fdc)
{
    return fdc->format_sectors;
}


static void
fdc_reset_fifo_buf(fdc_t *fdc)
{
    memset(fdc->fifobuf, 0, 16);
    fdc->fifobufpos = 0;
}


static void
fdc_fifo_buf_advance(fdc_t *fdc)
{
    if (fdc->fifobufpos == fdc->tfifo)
	fdc->fifobufpos = 0;
    else
	fdc->fifobufpos++;
}


static void
fdc_fifo_buf_write(fdc_t *fdc, uint8_t val)
{
    fdc->fifobuf[fdc->fifobufpos] = val;
    fdc_fifo_buf_advance(fdc);
}


static int
fdc_fifo_buf_read(fdc_t *fdc)
{
    int temp = fdc->fifobuf[fdc->fifobufpos];
    fdc_fifo_buf_advance(fdc);
    if (!fdc->fifobufpos)
	fdc->data_ready = 0;
    return temp;
}


static
void fdc_int(fdc_t *fdc, int set_fintr)
{
    int ienable = 0;

    if (!(fdc->flags & FDC_FLAG_PCJR))
	ienable = !!(fdc->dor & 8);

    if (ienable)
	picint(1 << fdc->irq);

    if (set_fintr)
	fdc->fintr = 1;
    fdc_log("fdc_int(%i): fdc->fintr = %i\n", set_fintr, fdc->fintr);
}


static void
fdc_watchdog_poll(void *priv)
{
    fdc_t *fdc = (fdc_t *) priv;

    fdc->watchdog_count--;
    if (fdc->watchdog_count)
	timer_advance_u64(&fdc->watchdog_timer, 1000 * TIMER_USEC);
    else {
	if (fdc->dor & 0x20)
		picint(1 << fdc->irq);
    }
}


/* fdc->rwc per Winbond W83877F datasheet:
    0 = normal;
    1 = 500 kbps, 360 rpm;
    2 = 500 kbps, 300 rpm;
    3 = 250 kbps

    Drive is only aware of selected rate and densel, so on real hardware, the rate expected by fdc_t and the rate actually being
    processed by drive can mismatch, in which case the fdc_t won't receive the correct data.
*/

void
fdc_update_rates(fdc_t *fdc)
{
    fdc_rate(fdc, 0);
    fdc_rate(fdc, 1);
    fdc_rate(fdc, 2);
    fdc_rate(fdc, 3);
}


void
fdc_update_max_track(fdc_t *fdc, int max_track)
{
    fdc->max_track = max_track;
}


void
fdc_update_enh_mode(fdc_t *fdc, int enh_mode)
{
    fdc->enh_mode = enh_mode;
    fdc_update_rates(fdc);
}


int
fdc_get_rwc(fdc_t *fdc, int drive)
{
    return fdc->rwc[drive];
}


void
fdc_update_rwc(fdc_t *fdc, int drive, int rwc)
{
    fdc_log("FDD %c: New RWC is %i\n", 0x41 + drive, rwc);
    fdc->rwc[drive] = rwc;
    fdc_rate(fdc, drive);
}


int
fdc_get_boot_drive(fdc_t *fdc)
{
    return fdc->boot_drive;
}


void
fdc_update_boot_drive(fdc_t *fdc, int boot_drive)
{
    fdc->boot_drive = boot_drive;
}


void
fdc_update_densel_polarity(fdc_t *fdc, int densel_polarity)
{
    fdc_log("FDC: New DENSEL polarity is %i\n", densel_polarity);
    fdc->densel_polarity = densel_polarity;
    fdc_update_rates(fdc);
}


uint8_t
fdc_get_densel_polarity(fdc_t *fdc)
{
    return fdc->densel_polarity;
}


void
fdc_update_densel_force(fdc_t *fdc, int densel_force)
{
    fdc_log("FDC: New DENSEL force is %i\n", densel_force);
    fdc->densel_force = densel_force;
    fdc_update_rates(fdc);
}


void
fdc_update_drvrate(fdc_t *fdc, int drive, int drvrate)
{
    fdc_log("FDD %c: New drive rate is %i\n", 0x41 + drive, drvrate);
    fdc->drvrate[drive] = drvrate;
    fdc_rate(fdc, drive);
}


void
fdc_update_drv2en(fdc_t *fdc, int drv2en)
{
    fdc->drv2en = drv2en;
}


void
fdc_update_rate(fdc_t *fdc, int drive)
{
    if (((fdc->rwc[drive] == 1) || (fdc->rwc[drive] == 2)) && fdc->enh_mode)
	fdc->bit_rate = 500;
    else if ((fdc->rwc[drive] == 3) && fdc->enh_mode)
	fdc->bit_rate = 250;
    else switch (fdc->rate) {
	case 0: /*High density*/
		fdc->bit_rate = 500;
		break;
	case 1: /*Double density (360 rpm)*/
		switch(fdc->drvrate[drive]) {
			case 0:
				fdc->bit_rate = 300;
				break;
			case 1:
				fdc->bit_rate = 500;
				break;
			case 2:
				fdc->bit_rate = 2000;
				break;
		}
		break;
	case 2: /*Double density*/
		fdc->bit_rate = 250;
		break;
	case 3: /*Extended density*/
		fdc->bit_rate = 1000;
		break;
    }

    fdc->bitcell_period = 1000000 / fdc->bit_rate * 2; /*Bitcell period in ns*/
}


int
fdc_get_bit_rate(fdc_t *fdc)
{
    switch(fdc->bit_rate) {
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


int
fdc_get_bitcell_period(fdc_t *fdc)
{
    return fdc->bitcell_period;
}


static int
fdc_get_densel(fdc_t *fdc, int drive)
{
    if (fdc->enh_mode) {
	switch (fdc->rwc[drive]) {
		case 1:
		case 3:
			return 0;
		case 2:
			return 1;
	}
    }

    if (!(fdc->flags & FDC_FLAG_NSC)) {
	switch (fdc->densel_force) {
		case 2:
			return 1;
		case 3:
			return 0;
	}
    } else {
	switch (fdc->densel_force) {
		case 0:
			return 0;
		case 1:
			return 1;
	}
    }

    switch (fdc->rate) {
	case 0:
	case 3:
		return fdc->densel_polarity ? 1 : 0;
	case 1:
	case 2:
		return fdc->densel_polarity ? 0 : 1;
    }

    return 0;
}


static void
fdc_rate(fdc_t *fdc, int drive)
{
    fdc_update_rate(fdc, drive);
    fdc_log("FDD %c: Setting rate: %i, %i, %i (%i, %i)\n", 0x41 + drive, fdc->drvrate[drive], fdc->rate, fdc_get_densel(fdc, drive), fdc->rwc[drive], fdc->densel_force);
    fdd_set_densel(fdc_get_densel(fdc, drive));
}


int
real_drive(fdc_t *fdc, int drive)
{
    if (drive < 2)
	return drive ^ fdc->swap;
    else
	return drive;
}


void
fdc_seek(fdc_t *fdc, int drive, int params)
{
    fdd_seek(real_drive(fdc, drive), params);
    fdc->stat |= (1 << fdc->drive);
}


void
fdc_implied_seek(fdc_t *fdc)
{
    if (fdc->config & 0x40) {
	if (fdc->params[1] != fdc->pcn[fdc->params[0] & 3]) {
		fdc_seek(fdc, fdc->drive, ((int) fdc->params[1]) - ((int) fdc->pcn[fdc->params[0] & 3]));
		fdc->pcn[fdc->params[0] & 3] = fdc->params[1];
	}
    }
}


static void
fdc_bad_command(fdc_t *fdc)
{
    fdc->stat = 0x10;
    fdc->interrupt = 0xfc;
    timer_set_delay_u64(&fdc->timer, 100 * TIMER_USEC);
}


static void
fdc_io_command_phase1(fdc_t *fdc, int out)
{
    fdc_reset_fifo_buf(fdc);
    fdc_rate(fdc, fdc->drive);
    fdc->head = fdc->params[2];
    fdd_set_head(real_drive(fdc, fdc->drive), (fdc->params[0] & 4) ? 1 : 0);
    fdc->sector=fdc->params[3];
    fdc->eot[fdc->drive] = fdc->params[5];
    fdc->gap = fdc->params[6];
    fdc->dtl = fdc->params[7];
    fdc_implied_seek(fdc);
    fdc->rw_track = fdc->params[1];
    ui_sb_update_icon(SB_FLOPPY | real_drive(fdc, fdc->drive), 1);
    fdc->stat = out ? 0x90 : 0x50;
    if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma)
	fdc->stat |= 0x20;
    if (out) {
	fdc->written = 0;
	fdc->pos = 0;
    } else
	fdc->inread = 1;
}


static void
fdc_sis(fdc_t *fdc)
{
    int drive_num;

    fdc->stat    = (fdc->stat & 0xf) | 0xd0;

    if (fdc->reset_stat) {
	drive_num = real_drive(fdc, 4 - fdc->reset_stat);
	if ((drive_num < FDD_NUM) && fdd_get_flags(drive_num)) {
		fdd_stop(drive_num);
		fdd_set_head(drive_num, 0);
		fdc->res[9] = 0xc0 | (4 - fdc->reset_stat) | (fdd_get_head(drive_num) ? 4 : 0);
	} else
		fdc->res[9] = 0xc0 | (4 - fdc->reset_stat);

	fdc->reset_stat--;
    } else {
	if (fdc->fintr) {
		fdc->res[9] = (fdc->st0 & ~0x04) | (fdd_get_head(fdc->drive & 0x03) ? 4 : 0);
		fdc->fintr = 0;
	} else {
		fdc->res[10] = 0x80;
		fdc->paramstogo = 1;
		return;
	}
    }

    fdc->res[10] = fdc->pcn[fdc->res[9] & 3];

    fdc_log("Sense interrupt status: 2 parameters to go\n");
    fdc->paramstogo = 2;
}


static void
fdc_write(uint16_t addr, uint8_t val, void *priv)
{
    fdc_t *fdc = (fdc_t *) priv;

    int drive, i, drive_num;

    fdc_log("Write FDC %04X %02X\n", addr, val);

    sub_cycles(ISA_CYCLES(8));

    switch (addr&7) {
	case 0:
		return;
	case 1:
		return;
	case 2:	/*DOR*/
		if (fdc->flags & FDC_FLAG_PCJR) {
			if ((fdc->dor & 0x40) && !(val & 0x40)) {
				timer_set_delay_u64(&fdc->watchdog_timer, 1000 * TIMER_USEC);
				fdc->watchdog_count = 1000;
				picintc(1 << fdc->irq);
			}
			if ((val & 0x80) && !(fdc->dor & 0x80)) {
				timer_set_delay_u64(&fdc->timer, 8 * TIMER_USEC);
				fdc->interrupt = -1;
				ui_sb_update_icon(SB_FLOPPY | 0, 0);
				fdc_ctrl_reset(fdc);
				fdd_changed[0] = 1;
			}
			if (!fdd_get_flags(0))
				val &= 0xfe;
			fdd_set_motor_enable(0, val & 0x01);
			fdc->st0 &= ~0x07;
			fdc->st0 |= (fdd_get_head(0) ? 4 : 0);
                } else {
			if (!(val & 8) && (fdc->dor & 8)) {
				fdc->tc = 1;
				fdc_int(fdc, 1);
			}
			if (!(val&4)) {
				fdd_stop(real_drive(fdc, val & 3));
				fdc->stat = 0x00;
				fdc->pnum = fdc->ptot = 0;
			}
                        if (val&4) {
				fdc->stat = 0x80;
				fdc->pnum = fdc->ptot = 0;
                        }
			if ((val&4) && !(fdc->dor&4)) {
				timer_set_delay_u64(&fdc->timer, 8 * TIMER_USEC);
				fdc->interrupt = -1;
				fdc->perp &= 0xfc;

				for (i = 0; i < FDD_NUM; i++)
					ui_sb_update_icon(SB_FLOPPY | i, 0);

				fdc_ctrl_reset(fdc);
			}
			/* We can now simplify this since each motor now spins separately. */
			for (i = 0; i < FDD_NUM; i++) {
				drive_num = real_drive(fdc, i);
				if ((!fdd_get_flags(drive_num)) || (drive_num >= FDD_NUM))
					val &= ~(0x10 << drive_num);
				else
					fdd_set_motor_enable(i, (val & (0x10 << drive_num)));
			}
			drive_num = real_drive(fdc, val & 0x03);
			current_drive = drive_num;
			fdc->st0 &= ~0x07;
			fdc->st0 |= real_drive(fdc, drive_num);
			fdc->st0 |= (fdd_get_head(drive_num) ? 4 : 0);
		}
		fdc->dor=val;
		return;
	case 3:	/* TDR */
		if (fdc->enh_mode) {
			drive = real_drive(fdc, fdc->dor & 3);
			fdc_update_rwc(fdc, drive, (val & 0x30) >> 4);
		}
		return;
	case 4:
		if (val & 0x80) {
			timer_set_delay_u64(&fdc->timer, 8 * TIMER_USEC);
			fdc->interrupt = -1;
			fdc->perp &= 0xfc;
			fdc_ctrl_reset(fdc);
		}
		return;
	case 5: /*Command register*/
		if ((fdc->stat & 0xf0) == 0xb0) {
			if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->fifo) {
				fdc->dat = val;
				fdc->stat &= ~0x80;
			} else {
				fdc_fifo_buf_write(fdc, val);
				if (fdc->fifobufpos == 0)
					fdc->stat &= ~0x80;
			}
			break;
		}
		if (fdc->pnum == fdc->ptot) {
			if ((fdc->stat & 0xf0) != 0x80) {
				/* If bit 4 of the MSR is set, or the MSR is 0x00,
				   the fdc_t is NOT in the command phase, therefore
				   do NOT accept commands. */
				return;
			}

			fdc->stat &= 0xf;

			fdc->tc = 0;
			fdc->data_ready = 0;

			fdc->command = val;
			fdc->stat |= 0x10;
			fdc_log("Starting FDC command %02X\n",fdc->command);

			switch (fdc->command & 0x1f) {
				case 0x01: /*Mode*/
					if (fdc->flags & FDC_FLAG_NSC) {
						fdc->pnum = 0;
						fdc->ptot = 4;
						fdc->stat |= 0x90;
						fdc->pos = 0;
						fdc->format_state = 0;
					} else
						fdc_bad_command(fdc);
					break;
				case 0x02: /*Read track*/
					fdc->satisfying_sectors = 0;
					fdc->sc = 0;
					fdc->wrong_am = 0;
					fdc->pnum = 0;
					fdc->ptot = 8;
					fdc->stat |= 0x90;
					fdc->pos = 0;
					fdc->mfm = (fdc->command & 0x40) ? 1:0;
					break;
				case 0x03: /*Specify*/
					fdc->pnum = 0;
					fdc->ptot = 2;
					fdc->stat |= 0x90;
					break;
				case 0x04: /*Sense drive status*/
					fdc->pnum = 0;
					fdc->ptot = 1;
					fdc->stat |= 0x90;
					break;
                                case 0x05: /*Write data*/
                                case 0x09: /*Write deleted data*/
					fdc->satisfying_sectors = 0;
					fdc->sc = 0;
					fdc->wrong_am = 0;
					fdc->deleted = ((fdc->command&0x1F) == 9) ? 1 : 0;
					fdc->pnum = 0;
					fdc->ptot = 8;
					fdc->stat |= 0x90;
					fdc->pos = 0;
					fdc->mfm = (fdc->command & 0x40) ? 1 : 0;
					break;
				case 0x06: /*Read data*/
				case 0x0c: /*Read deleted data*/
				case 0x11: /*Scan equal*/
				case 0x19: /*Scan low or equal*/
				case 0x16: /*Verify*/
				case 0x1d: /*Scan high or equal*/
					fdc->satisfying_sectors = 0;
					fdc->sc = 0;
					fdc->wrong_am = 0;
					fdc->deleted = ((fdc->command&0x1F) == 0xC) ? 1 : 0;
					if ((fdc->command&0x1F) == 0x16)  fdc->deleted = 2;
					fdc->deleted |= (fdc->command & 0x20);
					fdc->pnum = 0;
					fdc->ptot = 8;
					fdc->stat |= 0x90;
					fdc->pos = 0;
					fdc->mfm = (fdc->command&0x40)?1:0;
					break;
				case 0x07: /*Recalibrate*/
					fdc->pnum=0;
					fdc->ptot=1;
					fdc->stat |= 0x90;
					break;
				case 0x08: /*Sense interrupt status*/
					fdc_log("fdc->fintr = %i, fdc->reset_stat = %i\n", fdc->fintr, fdc->reset_stat);
					fdc->lastdrive = fdc->drive;
					fdc->pos = 0;
					fdc_sis(fdc);
					break;
				case 0x0a: /*Read sector ID*/
					fdc->pnum = 0;
					fdc->ptot = 1;
					fdc->stat |= 0x90;
					fdc->pos = 0;
					fdc->mfm = (fdc->command & 0x40) ? 1 : 0;
					break;
				case 0x0d: /*Format track*/
					fdc->pnum = 0;
					fdc->ptot = 5;
					fdc->stat |= 0x90;
					fdc->pos = 0;
					fdc->mfm = (fdc->command & 0x40) ? 1:0;
					fdc->format_state = 0;
					break;
				case 0x0e: /*Dump registers*/
					fdc->lastdrive = fdc->drive;
					fdc->interrupt = 0x0e;
					fdc->pos = 0;
					fdc_callback(fdc);
					break;
                                case 0x0f: /*Seek*/
                                	fdc->pnum = 0;
                                	fdc->ptot = 2;
                                	fdc->stat |= 0x90;
					break;
				case 0x18: /*NSC*/
					if (!(fdc->flags & FDC_FLAG_NSC)) {
						fdc_bad_command(fdc);
						break;
					}		
				case 0x10: /*Get version*/
				case 0x14: /*Unlock*/
				case 0x94: /*Lock*/
					fdc->lastdrive = fdc->drive;
					fdc->interrupt = fdc->command;
					fdc->pos = 0;
					fdc_callback(fdc);
					break;
				case 0x12: /*Set perpendicular mode*/
					if ((fdc->flags & FDC_FLAG_AT) && !(fdc->flags & FDC_FLAG_PCJR)) {
						fdc->pnum=0;
						fdc->ptot=1;
						fdc->stat |= 0x90;
						fdc->pos=0;
					} else
						fdc_bad_command(fdc);
					break;
				case 0x13: /*Configure*/
					fdc->pnum=0;
					fdc->ptot=3;
					fdc->stat |= 0x90;
					fdc->pos=0;
					break;
				default:
					fdc_bad_command(fdc);
					break;
			}
		} else {
			fdc->stat = 0x10 | (fdc->stat & 0xf);
			fdc->params[fdc->pnum++]=val;
			if (fdc->pnum == 1) {
				if (command_has_drivesel[fdc->command & 0x1F]) {
					if (fdc->flags & FDC_FLAG_PCJR)
						fdc->drive = 0;
					else
						fdc->drive = fdc->dor & 3;
					fdc->rw_drive = fdc->params[0] & 3;
					if (((fdc->command & 0x1F) == 7) || ((fdc->command & 0x1F) == 15))
						fdc->stat |= (1 << real_drive(fdc, fdc->drive));
				}
			}
                        if (fdc->pnum == fdc->ptot) {
				fdc_log("Got all params %02X\n", fdc->command);
				fdc->interrupt = fdc->command & 0x1F;
				fdc->reset_stat = 0;
				/* Disable timer if enabled. */
				timer_disable(&fdc->timer);
				/* Start timer if needed at this point. */
				switch (fdc->interrupt & 0x1f) {
					case 0x02:	/* Read a track */
					case 0x03:	/* Specify */
					case 0x0a:	/* Read sector ID */
					case 0x05:	/* Write data */
					case 0x06:	/* Read data */
					case 0x09:	/* Write deleted data */
					case 0x0c:	/* Read deleted data */
					case 0x11:	/* Scan equal */
					case 0x12:	/* Perpendicular mode */
					case 0x16:	/* Verify */
					case 0x19:	/* Scan low or equal */
					case 0x1d:	/* Scan high or equal */
						/* Do nothing. */
						break;
                                        case 0x07:	/* Recalibrate */
					case 0x0f:	/* Seek */
						timer_set_delay_u64(&fdc->timer, 1000 * TIMER_USEC);
						break;
					default:
						timer_set_delay_u64(&fdc->timer, 256 * TIMER_USEC);
						break;
				}
				/* Process the firt phase of the command. */
				switch (fdc->interrupt & 0x1F) {
					case 0x02:	/* Read a track */
						fdc_io_command_phase1(fdc, 0);
						fdc->read_track_sector.id.c = fdc->params[1];
						fdc->read_track_sector.id.h = fdc->params[2];
						fdc->read_track_sector.id.r = 1;
						fdc->read_track_sector.id.n = fdc->params[4];
						if ((fdc->head & 0x01) && !fdd_is_double_sided(real_drive(fdc, fdc->drive))) {
							fdc_noidam(fdc);
							return;
						}
						fdd_readsector(real_drive(fdc, fdc->drive), SECTOR_FIRST, fdc->params[1], fdc->head, fdc->rate, fdc->params[4]);
						break;
					case 0x03:	/* Specify */
						fdc->stat = 0x80;
						fdc->specify[0] = fdc->params[0];
						fdc->specify[1] = fdc->params[1];
						fdc->dma = (fdc->specify[1] & 1) ^ 1;
						break;
					case 0x04:	/*Sense drive status*/
						fdd_set_head(real_drive(fdc, fdc->drive), (fdc->params[0] & 4) ? 1 : 0);
						break;
					case 0x05:	/* Write data */
					case 0x09:	/* Write deleted data */
						fdc_io_command_phase1(fdc, 1);
						if ((fdc->head & 0x01) && !fdd_is_double_sided(real_drive(fdc, fdc->drive))) {
							fdc_noidam(fdc);
							return;
						}
	                                	fdd_writesector(real_drive(fdc, fdc->drive), fdc->sector, fdc->params[1], fdc->head, fdc->rate, fdc->params[4]);
						break;
					case 0x11:	/* Scan equal */
					case 0x19:	/* Scan low or equal */
					case 0x1d:	/* Scan high or equal */
						fdc_io_command_phase1(fdc, 1);
						if ((fdc->head & 0x01) && !fdd_is_double_sided(real_drive(fdc, fdc->drive))) {
							fdc_noidam(fdc);
							return;
						}
						fdd_comparesector(real_drive(fdc, fdc->drive), fdc->sector, fdc->params[1], fdc->head, fdc->rate, fdc->params[4]);
						break;
					case 0x16:	/* Verify */
						if (fdc->params[0] & 0x80)
							fdc->sc = fdc->params[7];
					case 0x06:	/* Read data */
					case 0x0c:	/* Read deleted data */
						fdc_io_command_phase1(fdc, 0);
						fdc_log("Reading sector (drive %i) (%i) (%i %i %i %i) (%i %i %i)\n", fdc->drive, fdc->params[0], fdc->params[1], fdc->params[2], fdc->params[3], fdc->params[4], fdc->params[5], fdc->params[6], fdc->params[7]);
						if ((fdc->head & 0x01) && !fdd_is_double_sided(real_drive(fdc, fdc->drive))) {
							fdc_noidam(fdc);
							return;
						}
						if (((dma_mode(2) & 0x0C) == 0x00) && !(fdc->flags & FDC_FLAG_PCJR) && fdc->dma) {
							/* DMA is in verify mode, treat this like a VERIFY command. */
							fdc_log("Verify-mode read!\n");
							fdc->tc = 1;
							fdc->deleted |= 2;
						}
						fdd_readsector(real_drive(fdc, fdc->drive), fdc->sector, fdc->params[1], fdc->head, fdc->rate, fdc->params[4]);
                                        	break;
                                        
                                        case 0x07:	/* Recalibrate */
						fdc->rw_drive = fdc->params[0] & 3;
						fdc->stat =  (1 << real_drive(fdc, fdc->drive));
						if (!(fdc->flags & FDC_FLAG_PCJR))
							fdc->stat |= 0x80;
						fdc->st0 = fdc->params[0] & 3;
						fdc->st0 |= fdd_get_head(real_drive(fdc, fdc->drive)) ? 0x04 : 0x00;
						fdc->st0 |= 0x80;
						drive_num = real_drive(fdc, fdc->drive);
						/* Three conditions under which the command should fail. */
						if (!fdd_get_flags(drive_num) || (drive_num >= FDD_NUM) || !motoron[drive_num] || fdd_track0(drive_num)) {
							fdc_log("Failed recalibrate\n");
							if (!fdd_get_flags(drive_num) || (drive_num >= FDD_NUM) || !motoron[drive_num])
								fdc->st0 = 0x70 | (fdc->params[0] & 3);
							else
								fdc->st0 = 0x20 | (fdc->params[0] & 3);
							fdc->pcn[fdc->params[0] & 3] = 0;
							if (fdc->flags & FDC_FLAG_PCJR) {
								fdc->fintr = 1;
								fdc->interrupt = -4;
							} else
								fdc->interrupt = -3;
							break;
						}
						if ((real_drive(fdc, fdc->drive) != 1) || fdc->drv2en)
							fdc_seek(fdc, fdc->drive, -fdc->max_track);
						fdc_log("Recalibrating...\n");
						fdc->seek_dir = fdc->step = 1;
						break;
					case 0x0a: /* Read sector ID */
						fdc_rate(fdc, fdc->drive);
						fdc->head = (fdc->params[0] & 4) ? 1 : 0;                                        
						fdd_set_head(real_drive(fdc, fdc->drive), (fdc->params[0] & 4) ? 1 : 0);
						if ((real_drive(fdc, fdc->drive) != 1) || fdc->drv2en) {
							fdd_readaddress(real_drive(fdc, fdc->drive), fdc->head, fdc->rate);
							if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma)
								fdc->stat = 0x70;
							else
								fdc->stat = 0x50;
						}
						else
							fdc_noidam(fdc);
						break;
					case 0x0d:	/* Format */
						fdc_rate(fdc, fdc->drive);
						fdc->head = (fdc->params[0] & 4) ? 1 : 0;
						fdd_set_head(real_drive(fdc, fdc->drive), (fdc->params[0] & 4) ? 1 : 0);
						fdc->gap = fdc->params[3];
						fdc->dtl = 4000000;
						fdc->format_sectors = fdc->params[2];
						fdc->format_n = fdc->params[1];
						fdc->format_state = 1;
						fdc->pos = 0;
						fdc->stat = 0x10;
						break;
					case 0x0f:	/* Seek */
						fdc->rw_drive = fdc->params[0] & 3;
						fdc->stat =  (1 << fdc->drive);
						if (!(fdc->flags & FDC_FLAG_PCJR))
							fdc->stat |= 0x80;
						fdc->head = (fdc->params[0] & 4) ? 1 : 0;
						fdc->st0 = fdc->params[0] & 0x03;
						fdc->st0 |= (fdc->params[0] & 4);
						fdc->st0 |= 0x80;
						fdd_set_head(real_drive(fdc, fdc->drive), (fdc->params[0] & 4) ? 1 : 0);
						drive_num = real_drive(fdc, fdc->drive);
						/* Three conditions under which the command should fail. */
						if (!fdd_get_flags(drive_num) || (drive_num >= FDD_NUM) || !motoron[drive_num]) {
							/* Yes, failed SEEK's still report success, unlike failed RECALIBRATE's. */
							fdc->st0 = 0x20 | (fdc->params[0] & 7);
							if (fdc->command & 0x80) {
								if (fdc->command & 0x40)
									fdc->pcn[fdc->params[0] & 3] += fdc->params[1];
								else
									fdc->pcn[fdc->params[0] & 3] -= fdc->params[1];
							} else
								fdc->pcn[fdc->params[0] & 3] = fdc->params[1];
							fdc->interrupt = -3;
							break;
						}
						if (fdc->command & 0x80) {
							if (fdc->params[1]) {
								if (fdc->command & 0x40) {
									/* Relative seek inwards. */
									fdc->seek_dir = 0;
									fdc_seek(fdc, fdc->drive, fdc->params[1]);
									fdc->pcn[fdc->params[0] & 3] += fdc->params[1];
								} else {
									/* Relative seek outwards. */
									fdc->seek_dir = 1;
									fdc_seek(fdc, fdc->drive, -fdc->params[1]);
									fdc->pcn[fdc->params[0] & 3] -= fdc->params[1];
								}
								fdc->step = 1;
							} else {
								fdc->st0 = 0x20 | (fdc->params[0] & 7);
								fdc->interrupt = -3;
								break;
							}
						} else {
							fdc_log("Seeking to track %i (PCN = %i)...\n", fdc->params[1], fdc->pcn[fdc->params[0] & 3]);
							if ((fdc->params[1] - fdc->pcn[fdc->params[0] & 3]) == 0) {
								fdc_log("Failed seek\n");
								fdc->st0 = 0x20 | (fdc->params[0] & 7);
								fdc->interrupt = -3;
								break;
							}
							if (fdc->params[1] > fdc->pcn[fdc->params[0] & 3])
								fdc->seek_dir = 0;
							else
								fdc->seek_dir = 1;
							fdc_seek(fdc, fdc->drive, fdc->params[1] - fdc->pcn[fdc->params[0] & 3]);
							fdc->pcn[fdc->params[0] & 3] = fdc->params[1];
							fdc->step = 1;
						}
						break;
					case 0x12:	/* Perpendicular mode */
						fdc->stat = 0x80;
						if (fdc->params[0] & 0x80)
							fdc->perp = fdc->params[0] & 0x3f;
						else {
							fdc->perp &= 0xfc;
							fdc->perp |= (fdc->params[0] & 0x03);
						}
						return;
				}
			} else
				fdc->stat = 0x90 | (fdc->stat & 0xf);
		}
		return;
	case 7:
		if (!(fdc->flags & FDC_FLAG_TOSHIBA) && !(fdc->flags & FDC_FLAG_AT))
			return;
		fdc->rate = val & 0x03;
		if (fdc->flags & FDC_FLAG_PS1)
			fdc->noprec = !!(val & 0x04);
		return;
    }
}


uint8_t
fdc_read(uint16_t addr, void *priv)
{
    fdc_t *fdc = (fdc_t *) priv;
    uint8_t ret;
    int drive;

    sub_cycles(ISA_CYCLES(8));

    switch (addr & 7) {
	case 0:		/* STA */
		if (fdc->flags & FDC_FLAG_PS1) {
			drive = real_drive(fdc, fdc->dor & 3);
			ret = 0x00;
			/* TODO:
				Bit 2: INDEX (best return always 0 as it goes by very fast)
				Bit 6: DRQ
			*/
			if (fdc->seek_dir)			/* nDIRECTION */
				ret |= 0x01;
			if (writeprot[drive])			/* WRITEPROT */
				ret |= 0x02;
			if (!fdd_get_head(drive))		/* nHDSEL */
				ret |= 0x08;
			if (fdd_track0(drive))			/* TRK0 */
				ret |= 0x10;
			if (fdc->step)				/* STEP */
				ret |= 0x20;
			if (fdc->fintr || fdc->reset_stat)	/* INTR */
				ret |= 0x80;
		} else
			ret = 0xff;
		break;
	case 1:		/* STB */
		if (fdc->flags & FDC_FLAG_PS1) {
			drive = real_drive(fdc, fdc->dor & 3);
			ret = 0x00;
			/* -Drive 2 Installed */
			if (!fdd_get_type(1))
				ret |= 80;
			/* -Drive Select 1,0 */
			switch (drive) {
				case 0:
					ret |= 0x43;
					break;
				case 1:
					ret |= 0x23;
					break;
				case 2:
					ret |= 0x62;
					break;
				case 3:
					ret |= 0x61;
					break;
			}
		} else {
			if (is486 || !fdc->enable_3f1)
				return 0xff;

			ret = 0x70;

			drive = real_drive(fdc, fdc->dor & 3);

			if (drive)
				ret &= ~0x40;
			else
				ret &= ~0x20;

			if (fdc->dor & 0x10)
				ret |= 1;
			if (fdc->dor & 0x20)
				ret |= 2;
		}
		break;
	case 2:
		ret = fdc->dor;
		break;
	case 3:
		drive = real_drive(fdc, fdc->dor & 3);
		if (fdc->flags & FDC_FLAG_PS1) {
			/* PS/1 Model 2121 seems return drive type in port
			 * 0x3f3, despite the 82077AA fdc_t not implementing
			 * this. This is presumably implemented outside the
			 * fdc_t on one of the motherboard's support chips.
			 *
			 * Confirmed: 00=1.44M 3.5
			 *	      10=2.88M 3.5
			 *	      20=1.2M 5.25
			 *	      30=1.2M 5.25
			 *
			 * as reported by Configur.exe.
			 */
			if (fdd_is_525(drive))
				ret = 0x20;
			else if (fdd_is_ed(drive))
				ret = 0x10;
			else
				ret = 0x00;
		} else if (!fdc->enh_mode)
			ret = 0x20;
		else
			ret = fdc->rwc[drive] << 4;
		break;
	case 4: /*Status*/
		ret = fdc->stat;
		break;
	case 5: /*Data*/
		if ((fdc->stat & 0xf0) == 0xf0) {
			fdc->stat &= ~0x80;
			if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->fifo) {
				fdc->data_ready = 0;
				ret = fdc->dat;
			} else
				ret = fdc_fifo_buf_read(fdc);
			break;
		}
		fdc->stat &= ~0x80;
		if (fdc->paramstogo) {
			fdc_log("%i parameters to go\n", fdc->paramstogo);
			fdc->paramstogo--;
			ret = fdc->res[10 - fdc->paramstogo];
			if (!fdc->paramstogo)
				fdc->stat = 0x80;
			else
				fdc->stat |= 0xC0;
		} else {
			if (lastbyte)
				fdc->stat = 0x80;
			lastbyte = 0;
			ret = fdc->dat;
			fdc->data_ready = 0;
		}
		fdc->stat &= 0xf0;
		break;
	case 7: /*Disk change*/
		drive = real_drive(fdc, fdc->dor & 3);

		if (fdc->flags & FDC_FLAG_PS1) {
			if (fdc->dor & (0x10 << drive)) {
				ret = (fdd_changed[drive] || drive_empty[drive]) ? 0x00 : 0x80;
				ret |= (fdc->dor & 0x08);
				ret |= (fdc->noprec << 2);
				ret |= (fdc->rate & 0x03);
			} else
				ret = 0x00;
		} else {
			if (fdc->dor & (0x10 << drive)) {
				if ((drive == 1) && (fdc->flags & FDC_FLAG_TOSHIBA))
					ret = 0x00;
				else
					ret = (fdd_changed[drive] || drive_empty[drive]) ? 0x80 : 0x00;
			} else
				ret = 0x00;
			if (fdc->flags & FDC_FLAG_DISKCHG_ACTLOW)  /*PC2086/3086 seem to reverse this bit*/
				ret ^= 0x80;

			/* 0 = ????, 1 = Ext. FDD off, 2 = Ext. FDD = FDD A, 3 = Ext. FDD = FDD B */
			if (fdc->flags & FDC_FLAG_TOSHIBA) {
				ret |= (3 << 5);
				ret |= 0x01;
			} else
				ret |= 0x7F;
		}

		fdc->step = 0;
		break;
	default:
		ret = 0xFF;
    }
    fdc_log("Read FDC %04X %02X\n", addr, ret);
    return ret;
}

static void
fdc_poll_common_finish(fdc_t *fdc, int compare, int st5)
{
    fdc_int(fdc, 1);
    if (!(fdc->flags & FDC_FLAG_PS1))
	fdc->fintr = 0;
    fdc->stat = 0xD0;
    fdc->st0 = fdc->res[4] = (fdd_get_head(real_drive(fdc, fdc->drive)) ? 4 : 0) | fdc->rw_drive;
    fdc->res[5] = st5;
    fdc->res[6] = 0;
    if (fdc->wrong_am) {
	fdc->res[6] |= 0x40;
	fdc->wrong_am = 0;
    }
    if (compare == 1) {
	if (!fdc->satisfying_sectors)
		fdc->res[6] |= 4;
	else if (fdc->satisfying_sectors == (fdc->params[5] << ((fdc->command & 80) ? 1 : 0)))
		fdc->res[6] |= 8;
    } else if (compare == 2) {
	if (fdc->satisfying_sectors & 1)
		fdc->res[5] |= 0x20;
	if (fdc->satisfying_sectors & 2) {
		fdc->res[5] |= 0x20;
		fdc->res[6] |= 0x20;
	}
	if (fdc->satisfying_sectors & 4)
		fdc->res[5] |= 0x04;
	if (fdc->satisfying_sectors & 8) {
		fdc->res[5] |= 0x04;
		fdc->res[6] |= 0x02;
	}
	if (fdc->satisfying_sectors & 0x10) {
		fdc->res[5] |= 0x04;
		fdc->res[6] |= 0x10;
	}
    }
    fdc->res[7]=fdc->rw_track;
    fdc->res[8]=fdc->head;
    fdc->res[9]=fdc->sector;
    fdc->res[10]=fdc->params[4];
    fdc_log("Read/write finish (%02X %02X %02X %02X %02X %02X %02X)\n" , fdc->res[4], fdc->res[5], fdc->res[6], fdc->res[7], fdc->res[8], fdc->res[9], fdc->res[10]);
    ui_sb_update_icon(SB_FLOPPY | real_drive(fdc, fdc->drive), 0);
    fdc->paramstogo = 7;
}


static void
fdc_poll_readwrite_finish(fdc_t *fdc, int compare)
{
    fdc->inread = 0;
    fdc->interrupt = -2;

    fdc_poll_common_finish(fdc, compare, 0);
}


static void
fdc_no_dma_end(fdc_t *fdc, int compare)
{
    timer_disable(&fdc->timer);

    fdc_poll_common_finish(fdc, compare, 0x80);
}


static void
fdc_callback(void *priv)
{
    fdc_t *fdc = (fdc_t *) priv;
    int compare = 0;
    int drive_num = 0;
    int old_sector = 0;
    fdc_log("fdc_callback(): %i\n", fdc->interrupt);
    switch (fdc->interrupt) {
	case -3: /*End of command with interrupt*/
	case -4: /*Recalibrate/seek interrupt (PCjr only)*/
		fdc_int(fdc, fdc->interrupt & 1);
		fdc->stat = (fdc->stat & 0xf) | 0x80;
		return;
	case -2: /*End of command*/
		fdc->stat = (fdc->stat & 0xf) | 0x80;
		return;
	case -1: /*Reset*/
		fdc_int(fdc, 1);
		fdc->fintr = 0;
		memset(fdc->pcn, 0, 4 * sizeof(int));
		fdc->reset_stat = 4;
		return;
	case 0x01:	/* Mode */
		fdc->stat=0x80;
		fdc->densel_force = (fdc->params[2] & 0xC0) >> 6;
		return;
	case 0x02:	/* Read track */
		ui_sb_update_icon(SB_FLOPPY | real_drive(fdc, fdc->drive), 1);
		fdc->eot[fdc->drive]--;
		fdc->read_track_sector.id.r++;
		if (!fdc->eot[fdc->drive] || fdc->tc) {
			fdc_poll_readwrite_finish(fdc, 2);
			return;
		} else {
			fdd_readsector(real_drive(fdc, fdc->drive), SECTOR_NEXT, fdc->rw_track, fdc->head, fdc->rate, fdc->params[4]);
			if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma)
				fdc->stat = 0x70;
			else
				fdc->stat = 0x50;
		}
		fdc->inread = 1;
		return;
	case 0x04:	/* Sense drive status */
		fdc->res[10] = (fdc->params[0] & 7) | 0x20;
		if (fdd_is_double_sided(real_drive(fdc, fdc->drive)))
			fdc->res[10] |= 0x08;
		if ((real_drive(fdc, fdc->drive) != 1) || fdc->drv2en) {
			if (fdd_track0(real_drive(fdc, fdc->drive)))
				fdc->res[10] |= 0x10;
		}
		if (writeprot[fdc->drive])
			fdc->res[10] |= 0x40;

		fdc->stat = (fdc->stat & 0xf) | 0xd0;
		fdc->paramstogo = 1;
		fdc->interrupt = 0;
		return;
	case 0x05:	/* Write data */
	case 0x09:	/* Write deleted data */
	case 0x06:	/* Read data */
	case 0x0c:	/* Read deleted data */
	case 0x11:	/* Scan equal */
	case 0x19:	/* Scan low or equal */
	case 0x1c:	/* Verify */
	case 0x1d:	/* Scan high or equal */
		if ((fdc->interrupt == 0x11) || (fdc->interrupt == 0x19) || (fdc->interrupt == 0x1D))
			compare = 1;
		else
			compare = 0;
		if ((fdc->interrupt == 6) || (fdc->interrupt == 0xC)) {
			if (fdc->wrong_am && !(fdc->deleted & 0x20)) {
				/* Mismatching data address mark and no skip, set TC. */
				fdc->tc = 1;
			}
		}
		old_sector = fdc->sector;
		if (fdc->tc) {
			/* This is needed so that the correct results are returned
			   in case of TC. */
			if (fdc->sector == fdc->params[5]) {
				if (!(fdc->command & 0x80)) {
					fdc->rw_track++;
					fdc->sector = 1;
				} else {
					if (fdc->head)
						fdc->rw_track++;

					fdc->head ^= 1;
					fdd_set_head(real_drive(fdc, fdc->drive), fdc->head);
					fdc->sector = 1;
				}
			} else
				fdc->sector++;
			fdc_poll_readwrite_finish(fdc, compare);
			return;
		}
		if ((fdc->interrupt == 0x16) && (fdc->params[0] & 0x80)) {
			/* VERIFY command, EC set */
			fdc->sc--;
			if (!fdc->sc) {
				fdc->sector++;
				fdc_poll_readwrite_finish(fdc, 0);
				return;
			}
			/* The rest is processed normally per MT flag and EOT. */
		} else if ((fdc->interrupt == 0x16) && !(fdc->params[0] & 0x80)) {
			/* VERIFY command, EC clear */
			if ((fdc->sector == old_sector) && (fdc->head == (fdc->command & 0x80) ? 1 : 0)) {
				fdc->sector++;
				fdc_poll_readwrite_finish(fdc, 0);
				return;
			}
		}
		if (fdc->sector == fdc->params[5]) {
			/* Reached end of track, MT bit is clear */
			if (!(fdc->command & 0x80)) {
				fdc->rw_track++;
				fdc->sector = 1;
				if (!(fdc->flags & FDC_FLAG_PCJR) && fdc->dma && (old_sector == 255))
					fdc_no_dma_end(fdc, compare);
				else
					fdc_poll_readwrite_finish(fdc, compare);
				return;
			}
			/* Reached end of track, MT bit is set, head is 1 */
			if (fdd_get_head(real_drive(fdc, fdc->drive)) == 1) {
				fdc->rw_track++;
				fdc->sector = 1;
				fdc->head &= 0xFE;
				fdd_set_head(real_drive(fdc, fdc->drive), 0);
				if (!(fdc->flags & FDC_FLAG_PCJR) && fdc->dma && (old_sector == 255))
					fdc_no_dma_end(fdc, compare);
				else
					fdc_poll_readwrite_finish(fdc, compare);
				return;
			}
			if ((fdd_get_head(real_drive(fdc, fdc->drive)) == 0)) {
				fdc->sector = 1;
				fdc->head |= 1;
				fdd_set_head(real_drive(fdc, fdc->drive), 1);
				if (!fdd_is_double_sided(real_drive(fdc, fdc->drive))) {
					fdc_noidam(fdc);
					return;
				}
			}
		} else if (fdc->sector < fdc->params[5])
			fdc->sector++;
		ui_sb_update_icon(SB_FLOPPY | real_drive(fdc, fdc->drive), 1);
		switch (fdc->interrupt) {
			case 5:
			case 9:
				fdd_writesector(real_drive(fdc, fdc->drive), fdc->sector, fdc->rw_track, fdc->head, fdc->rate, fdc->params[4]);
				if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma)
					fdc->stat = 0xb0;
				else
					fdc->stat = 0x90;
				break;
			case 6:
			case 0xC:
			case 0x16:
				fdd_readsector(real_drive(fdc, fdc->drive), fdc->sector, fdc->rw_track, fdc->head, fdc->rate, fdc->params[4]);
				if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma)
					fdc->stat = 0x70;
				else
					fdc->stat = 0x50;
				break;
			case 0x11:
			case 0x19:
			case 0x1D:
				fdd_comparesector(real_drive(fdc, fdc->drive), fdc->sector, fdc->rw_track, fdc->head, fdc->rate, fdc->params[4]);
				if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma)
					fdc->stat = 0xb0;
				else
					fdc->stat = 0x90;
				break;
		}
		fdc->inread = 1;
		return;
	case 0x07:	/* Recalibrate */
		fdc->pcn[fdc->params[0] & 3] = 0;
		drive_num = real_drive(fdc, fdc->rw_drive);
		fdc->st0 = 0x20 | (fdc->params[0] & 3);
		if (!fdd_track0(drive_num))
			fdc->st0 |= 0x50;
		if (fdc->flags & FDC_FLAG_PCJR) {
			fdc->fintr = 1;
			fdc->interrupt = -4;
		} else
			fdc->interrupt = -3;
		timer_set_delay_u64(&fdc->timer, 2048 * TIMER_USEC);
		fdc->stat = 0x80 | (1 << fdc->rw_drive);
		return;
	case 0x0d: /*Format track*/
		if (fdc->format_state == 1) {
			fdc->format_state = 2;
			timer_set_delay_u64(&fdc->timer, 8 * TIMER_USEC);
		} else if (fdc->format_state == 2) {
			fdd_format(real_drive(fdc, fdc->drive), fdc->head, fdc->rate, fdc->params[4]);
			fdc->format_state = 3;
		} else {
			fdc->interrupt = -2;
			fdc_int(fdc, 1);
			if (!(fdc->flags & FDC_FLAG_PS1))
				fdc->fintr = 0;
			fdc->stat = 0xD0;
			fdc->st0 = fdc->res[4] = (fdd_get_head(real_drive(fdc, fdc->drive)) ? 4 : 0) | fdc->drive;
			fdc->res[5] = fdc->res[6] = 0;
			fdc->res[7] = fdc->pcn[fdc->params[0] & 3];
			fdc->res[8] = fdd_get_head(real_drive(fdc, fdc->drive));
			fdc->res[9] = fdc->format_dat[fdc->pos - 2] + 1;
			fdc->res[10] = fdc->params[4];
			fdc->paramstogo = 7;
			fdc->format_state = 0;
			return;
		}
		return;
	case 0x0e: /*Dump registers*/
		fdc->stat = (fdc->stat & 0xf) | 0xd0;
		fdc->res[1] = fdc->pcn[0];
		fdc->res[2] = fdc->pcn[1];
		fdc->res[3] = fdc->pcn[2];
		fdc->res[4] = fdc->pcn[3];
		fdc->res[5] = fdc->specify[0];
		fdc->res[6] = fdc->specify[1];
		fdc->res[7] = fdc->eot[fdc->drive];
		fdc->res[8] = (fdc->perp & 0x7f) | ((fdc->lock) ? 0x80 : 0);
		fdc->res[9] = fdc->config;
		fdc->res[10] = fdc->pretrk;
		fdc->paramstogo = 10;
		fdc->interrupt = 0;
		return;
	case 0x0f: /*Seek*/
		fdc->st0 = 0x20 | (fdc->params[0] & 7);
		fdc->stat = 0x80 | (1 << fdc->rw_drive);
		if (fdc->flags & FDC_FLAG_PCJR) {
			fdc->fintr = 1;
			fdc->interrupt = -4;
			timer_set_delay_u64(&fdc->timer, 1024 * TIMER_USEC);
		} else {
			fdc->interrupt = -3;
			fdc_callback(fdc);
		}
		return;
	case 0x10: /*Version*/
	case 0x18: /*NSC*/
		fdc->stat = (fdc->stat & 0xf) | 0xd0;
		fdc->res[10] = (fdc->interrupt & 0x08) ? 0x73 : 0x90;
		fdc->paramstogo = 1;
		fdc->interrupt = 0;
		return;
	case 0x13: /*Configure*/
		fdc->config = fdc->params[1];
		fdc->pretrk = fdc->params[2];
		fdc->fifo = (fdc->params[1] & 0x20) ? 0 : 1;
		fdc->tfifo = (fdc->params[1] & 0xF);
		fdc->stat = 0x80;
		return;
	case 0x14: /*Unlock*/
	case 0x94: /*Lock*/
		fdc->lock = (fdc->interrupt & 0x80) ? 1 : 0;
		fdc->stat = (fdc->stat & 0xf) | 0xd0;
		fdc->res[10] = (fdc->interrupt & 0x80) ? 0x10 : 0x00;
		fdc->paramstogo = 1;
		fdc->interrupt = 0;
		return;
	case 0xfc: /*Invalid*/
		fdc->dat = fdc->st0 = 0x80;
		fdc->stat = (fdc->stat & 0xf) | 0xd0;
		fdc->res[10] = fdc->st0;
		fdc->paramstogo = 1;
		fdc->interrupt = 0;
		return;
    }
}


void
fdc_error(fdc_t *fdc, int st5, int st6)
{
    timer_disable(&fdc->timer);

    fdc_int(fdc, 1);
    if (!(fdc->flags & FDC_FLAG_PS1))
	fdc->fintr = 0;
    fdc->stat = 0xD0;
    fdc->st0 = fdc->res[4] = 0x40 | (fdd_get_head(real_drive(fdc, fdc->drive)) ? 4 : 0) | fdc->rw_drive;
    if (fdc->head && !fdd_is_double_sided(real_drive(fdc, fdc->drive)))
	fdc->st0 |= 0x08;
    fdc->res[5] = st5;
    fdc->res[6] = st6;
    fdc_log("FDC Error: %02X %02X %02X\n", fdc->res[4], fdc->res[5], fdc->res[6]);
    switch(fdc->interrupt) {
	case 0x02:
	case 0x05:
	case 0x06:
	case 0x09:
	case 0x0C:
	case 0x11:
	case 0x16:
	case 0x19:
	case 0x1D:
		fdc->res[7]=fdc->rw_track;
		fdc->res[8]=fdc->head;
		fdc->res[9]=fdc->sector;
		fdc->res[10]=fdc->params[4];
		break;
	default:
		fdc->res[7]=0;
		fdc->res[8]=0;
		fdc->res[9]=0;
		fdc->res[10]=0;
		break;
    }
    ui_sb_update_icon(SB_FLOPPY | real_drive(fdc, fdc->drive), 0);
    fdc->paramstogo = 7;
}


void
fdc_overrun(fdc_t *fdc)
{
    fdd_stop(fdc->drive);

    fdc_error(fdc, 0x10, 0);
}


int
fdc_is_verify(fdc_t *fdc)
{
    return (fdc->deleted & 2) ? 1 : 0;
}


int
fdc_data(fdc_t *fdc, uint8_t data)
{
    int result = 0;

    if (fdc->deleted & 2) {
	/* We're in a VERIFY command, so return with 0. */
	return 0;
    }

    if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma) {
	if (fdc->tc)
		return 0;

	if (fdc->data_ready) {
		fdc_overrun(fdc);
		return -1;
	}

	if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->fifo || (fdc->tfifo < 1)) {
		fdc->dat = data;
		fdc->data_ready = 1;
		fdc->stat = 0xf0;
	} else {
		/* FIFO enabled */
		fdc_fifo_buf_write(fdc, data);
		if (fdc->fifobufpos == 0) {
			/* We have wrapped around, means FIFO is over */
			fdc->data_ready = 1;
			fdc->stat = 0xf0;
		}
	}
    } else {
	result = dma_channel_write(fdc->dma_ch, data);

	if (fdc->tc)
		return -1;

	if (result & DMA_OVER) {
		fdc->data_ready = 1;
		fdc->stat = 0xd0;
		fdc->tc = 1;
		return -1;
	}

	if (!fdc->fifo || (fdc->tfifo < 1)) {
		fdc->data_ready = 1;
		fdc->stat = 0xd0;
	} else {
		fdc_fifo_buf_advance(fdc);
		if (fdc->fifobufpos == 0) {
			/* We have wrapped around, means FIFO is over */
			fdc->data_ready = 1;
			fdc->stat = 0xd0;
		}
	}
    }

    return 0;
}


void
fdc_finishread(fdc_t *fdc)
{
        fdc->inread = 0;
}


void
fdc_track_finishread(fdc_t *fdc, int condition)
{
    fdc->stat = 0x10;
    fdc->satisfying_sectors |= condition;
    fdc->inread = 0;
    fdc_callback(fdc);
}


void
fdc_sector_finishcompare(fdc_t *fdc, int satisfying)
{
    fdc->stat = 0x10;
    fdc->satisfying_sectors++;
    fdc->inread = 0;
    fdc_callback(fdc);
}


void
fdc_sector_finishread(fdc_t *fdc)
{
    fdc->stat = 0x10;
    fdc->inread = 0;
    fdc_callback(fdc);
}


/* There is no sector ID. */
void
fdc_noidam(fdc_t *fdc)
{
    fdc_error(fdc, 1, 0);
}


/* Sector ID's are there, but there is no sector. */
void fdc_nosector(fdc_t *fdc)
{
    fdc_error(fdc, 4, 0);
}


/* There is no sector data. */
void fdc_nodataam(fdc_t *fdc)
{
    fdc_error(fdc, 1, 1);
}


/* Abnormal termination with both status 1 and 2 set to 0, used when abnormally
   terminating the fdc_t FORMAT TRACK command. */
void fdc_cannotformat(fdc_t *fdc)
{
    fdc_error(fdc, 0, 0);
}


void
fdc_datacrcerror(fdc_t *fdc)
{
    fdc_error(fdc, 0x20, 0x20);
}


void
fdc_headercrcerror(fdc_t *fdc)
{
    fdc_error(fdc, 0x20, 0);
}


void
fdc_wrongcylinder(fdc_t *fdc)
{
    fdc_error(fdc, 4, 0x10);
}


void
fdc_badcylinder(fdc_t *fdc)
{
    fdc_error(fdc, 4, 0x02);
}


void
fdc_writeprotect(fdc_t *fdc)
{
    fdc_error(fdc, 0x02, 0);
}


int fdc_getdata(fdc_t *fdc, int last)
{
    int data;

    if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->dma) {
	if (fdc->written) {
		fdc_overrun(fdc);
		return -1;
	}
	if ((fdc->flags & FDC_FLAG_PCJR) || !fdc->fifo) {
		data = fdc->dat;

		if (!last)
			fdc->stat = 0xb0;
	} else {
		data = fdc_fifo_buf_read(fdc);

		if (!last && (fdc->fifobufpos == 0))
			fdc->stat = 0xb0;
	}
    } else {
	data = dma_channel_read(fdc->dma_ch);

	if (!fdc->fifo) {
		if (!last)
			fdc->stat = 0x90;
	} else {
		fdc_fifo_buf_advance(fdc);

		if (!last && (fdc->fifobufpos == 0))
			fdc->stat = 0x90;
	}

	if (data & DMA_OVER)
		fdc->tc = 1;
    }

    fdc->written = 0;
    return data & 0xff;
}


void
fdc_sectorid(fdc_t *fdc, uint8_t track, uint8_t side, uint8_t sector, uint8_t size, uint8_t crc1, uint8_t crc2)
{
    fdc_int(fdc, 1);
    fdc->stat = 0xD0;
    fdc->st0 = fdc->res[4] = (fdd_get_head(real_drive(fdc, fdc->drive)) ? 4 : 0) | fdc->drive;
    fdc->res[5] = 0;
    fdc->res[6] = 0;
    fdc->res[7] = track;
    fdc->res[8] = side;
    fdc->res[9] = sector;
    fdc->res[10] = size;
    ui_sb_update_icon(SB_FLOPPY | real_drive(fdc, fdc->drive), 0);
    fdc->paramstogo = 7;
}


uint8_t
fdc_get_swwp(fdc_t *fdc)
{
    return fdc->swwp;
}


void
fdc_set_swwp(fdc_t *fdc, uint8_t swwp)
{
    fdc->swwp = swwp;
}


uint8_t
fdc_get_diswr(fdc_t *fdc)
{
    if (!fdc)
	return 0;

    return fdc->disable_write;
}


void
fdc_set_diswr(fdc_t *fdc, uint8_t diswr)
{
    fdc->disable_write = diswr;
}


uint8_t
fdc_get_swap(fdc_t *fdc)
{
    return fdc->swap;
}


void
fdc_set_swap(fdc_t *fdc, uint8_t swap)
{
    fdc->swap = swap;
}


void
fdc_set_base(fdc_t *fdc, int base)
{
    int super_io = (fdc->flags & FDC_FLAG_SUPERIO);

    if ((fdc->flags & FDC_FLAG_AT) || (fdc->flags & FDC_FLAG_AMSTRAD)) {
	io_sethandler(base + (super_io ? 2 : 0), super_io ? 0x0004 : 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
	io_sethandler(base + 7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
    } else {
	if (fdc->flags & FDC_FLAG_PCJR)
		io_sethandler(base, 0x0010, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
	else {
		io_sethandler(base + 0x0002, 0x0001, NULL, NULL, NULL, fdc_write, NULL, NULL, fdc);
		io_sethandler(base + 0x0004, 0x0001, fdc_read, NULL, NULL, NULL, NULL, NULL, fdc);
		io_sethandler(base + 0x0005, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
		if (fdc->flags & FDC_FLAG_TOSHIBA)
			io_sethandler(base + 0x0007, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
	}
    }
    fdc->base_address = base;
    fdc_log("FDC Base address set%s (%04X)\n", super_io ? " for Super I/O" : "", fdc->base_address);
}


void
fdc_remove(fdc_t *fdc)
{
    int super_io = (fdc->flags & FDC_FLAG_SUPERIO);

    fdc_log("FDC Removed (%04X)\n", fdc->base_address);
    if ((fdc->flags & FDC_FLAG_AT) || (fdc->flags & FDC_FLAG_AMSTRAD)) {
	io_removehandler(fdc->base_address + (super_io ? 2 : 0), super_io ? 0x0004 : 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
	io_removehandler(fdc->base_address + 7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
    } else {
	if (fdc->flags & FDC_FLAG_PCJR)
		io_removehandler(fdc->base_address, 0x0010, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
	else {
		io_removehandler(fdc->base_address + 0x0002, 0x0001, NULL, NULL, NULL, fdc_write, NULL, NULL, fdc);
		io_removehandler(fdc->base_address + 0x0004, 0x0001, fdc_read, NULL, NULL, NULL, NULL, NULL, fdc);
		io_removehandler(fdc->base_address + 0x0005, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
		if (fdc->flags & FDC_FLAG_TOSHIBA)
			io_removehandler(fdc->base_address + 0x0007, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, fdc);
	}
    }
}


void
fdc_reset(void *priv)
{
    int i = 0;
    uint8_t default_rwc;

    fdc_t *fdc = (fdc_t *) priv;

    default_rwc = (fdc->flags & FDC_FLAG_START_RWC_1) ? 1 : 0;

    fdc->enable_3f1 = 1;

    fdc_update_enh_mode(fdc, 0);
    if (fdc->flags & FDC_FLAG_PS1)
	fdc_update_densel_polarity(fdc, 0);
    else
	fdc_update_densel_polarity(fdc, 1);
    if (fdc->flags & FDC_FLAG_NSC)
	fdc_update_densel_force(fdc, 3);
    else
	fdc_update_densel_force(fdc, 0);
    fdc_update_rwc(fdc, 0, default_rwc);
    fdc_update_rwc(fdc, 1, default_rwc);
    fdc_update_rwc(fdc, 2, default_rwc);
    fdc_update_rwc(fdc, 3, default_rwc);
    fdc_update_drvrate(fdc, 0, 0);
    fdc_update_drvrate(fdc, 1, 0);
    fdc_update_drvrate(fdc, 2, 0);
    fdc_update_drvrate(fdc, 3, 0);
    fdc_update_drv2en(fdc, 1);
    fdc_update_rates(fdc);

    fdc->fifo = 0;
    fdc->tfifo = 1;

    if (fdc->flags & FDC_FLAG_PCJR) {
	fdc->dma = 0;
	fdc->specify[1] = 1;
    } else {
	fdc->dma = 1;
	fdc->specify[1] = 0;
    }
    fdc->config = 0x20;
    fdc->pretrk = 0;

    fdc->swwp = 0;
    fdc->disable_write = 0;

    fdc_ctrl_reset(fdc);

    fdc->max_track = (fdc->flags & FDC_FLAG_MORE_TRACKS) ? 85 : 79;

    fdc_remove(fdc);
    fdc_set_base(fdc, (fdc->flags & FDC_FLAG_PCJR) ? 0x00f0 : 0x03f0);

    current_drive = 0;

    for (i = 0; i < FDD_NUM; i++)
	ui_sb_update_icon(SB_FLOPPY | i, 0);
}


static void
fdc_close(void *priv)
{
    fdc_t *fdc = (fdc_t *) priv;

    fdc_reset(fdc);

    /* Stop timers. */
    fdc->watchdog_count = 0;

    free(fdc);
}


static void *
fdc_init(const device_t *info)
{
    fdc_t *fdc = (fdc_t *) malloc(sizeof(fdc_t));
    memset(fdc, 0, sizeof(fdc_t));

    fdc->flags = info->local;
    fdc_reset(fdc);

    fdc->irq = 6;

    if (fdc->flags & FDC_FLAG_PCJR)
	timer_add(&fdc->watchdog_timer, fdc_watchdog_poll, fdc, 0);
    else
	fdc->dma_ch = 2;

    fdc_log("FDC added: %04X (flags: %08X)\n", fdc->base_address, fdc->flags);

    timer_add(&fdc->timer, fdc_callback, fdc, 0);

    d86f_set_fdc(fdc);
    fdi_set_fdc(fdc);
    fdd_set_fdc(fdc);
    imd_set_fdc(fdc);
    img_set_fdc(fdc);
    mfm_set_fdc(fdc);

    fdc_reset(fdc);

    return fdc;
}


void
fdc_3f1_enable(fdc_t *fdc, int enable)
{
    fdc->enable_3f1 = enable;
}


const device_t fdc_xt_device = {
    "PC/XT Floppy Drive Controller",
    0,
    0,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};

const device_t fdc_xt_t1x00_device = {
    "PC/XT Floppy Drive Controller (Toshiba)",
    0,
    FDC_FLAG_TOSHIBA,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};

const device_t fdc_xt_amstrad_device = {
    "PC/XT Floppy Drive Controller (Amstrad)",
    0,
    FDC_FLAG_DISKCHG_ACTLOW | FDC_FLAG_AMSTRAD,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};


const device_t fdc_pcjr_device = {
    "PCjr Floppy Drive Controller",
    0,
    FDC_FLAG_PCJR,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};

const device_t fdc_at_device = {
    "PC/AT Floppy Drive Controller",
    0,
    FDC_FLAG_AT,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};

const device_t fdc_at_actlow_device = {
    "PC/AT Floppy Drive Controller (Active low)",
    0,
    FDC_FLAG_DISKCHG_ACTLOW | FDC_FLAG_AT,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};

const device_t fdc_at_ps1_device = {
    "PC/AT Floppy Drive Controller (PS/1, PS/2 ISA)",
    0,
    FDC_FLAG_DISKCHG_ACTLOW | FDC_FLAG_AT | FDC_FLAG_PS1,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};

const device_t fdc_at_smc_device = {
    "PC/AT Floppy Drive Controller (SM(s)C FDC37Cxxx)",
    0,
    FDC_FLAG_AT | FDC_FLAG_SUPERIO,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};

const device_t fdc_at_winbond_device = {
    "PC/AT Floppy Drive Controller (Winbond W83x77F)",
    0,
    FDC_FLAG_AT | FDC_FLAG_SUPERIO | FDC_FLAG_START_RWC_1 | FDC_FLAG_MORE_TRACKS,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};

const device_t fdc_at_nsc_device = {
    "PC/AT Floppy Drive Controller (NSC PC8730x)",
    0,
    FDC_FLAG_AT | FDC_FLAG_MORE_TRACKS | FDC_FLAG_NSC,
    fdc_init,
    fdc_close,
    fdc_reset,
    NULL, NULL, NULL
};
