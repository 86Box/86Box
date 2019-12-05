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
 * Version:	@(#)fdc.h	1.0.8	2019/10/20
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#ifndef EMU_FDC_H
# define EMU_FDC_H


#define FDC_FLAG_PCJR		0x01	/* PCjr */
#define FDC_FLAG_DISKCHG_ACTLOW	0x02	/* Amstrad, PS/1, PS/2 ISA */
#define FDC_FLAG_AT		0x04	/* AT+, PS/x */
#define FDC_FLAG_PS1		0x08	/* PS/1, PS/2 ISA */
#define FDC_FLAG_SUPERIO	0x10	/* Super I/O chips */
#define FDC_FLAG_START_RWC_1	0x20	/* W83877F, W83977F */
#define FDC_FLAG_MORE_TRACKS	0x40	/* W83877F, W83977F, PC87306, PC87309 */
#define FDC_FLAG_NSC		0x80	/* PC87306, PC87309 */
#define FDC_FLAG_TOSHIBA	0x100	/* T1000, T1200 */
#define FDC_FLAG_AMSTRAD	0x200	/* Non-AT Amstrad machines */


typedef struct {
    uint8_t	dor, stat, command, dat, st0, swap;
    uint8_t	swwp, disable_write;
    uint8_t	params[256], res[256];
    uint8_t	specify[256], format_dat[256];
    uint8_t	config, pretrk;
    uint8_t	fifobuf[16];

    uint16_t	base_address;

    int		head, sector, drive, lastdrive;
    int		pcn[4], eot[256];
    int		rw_track, pos;
    int		pnum, ptot;
    int		rate, reset_stat;
    int		lock, perp;
    int		abort;
    int		format_state, format_n;
    int		tc, written;
    int		step, seek_dir;
    int		noprec;

    int		data_ready, inread;
    int		bitcell_period, enh_mode;
    int		rwc[4], drvrate[4];
    int		boot_drive, dma;
    int		densel_polarity, densel_force;
    int		fifo, tfifo;
    int		fifobufpos, drv2en;

    int		gap, dtl;
    int		enable_3f1, format_sectors;
    int		max_track, mfm;
    int		deleted, wrong_am;
    int		sc, satisfying_sectors;
    int		fintr, rw_drive;

    int		flags, interrupt;

    int		irq;		/* Should be 6 by default. */
    int		dma_ch;		/* Should be 2 by default. */

    int		bit_rate;	/* Should be 250 at start. */
    int		paramstogo;

    sector_id_t	read_track_sector;

	uint64_t watchdog_count;
	
	pc_timer_t	timer, watchdog_timer;
} fdc_t;


extern void	fdc_remove(fdc_t *fdc);
extern void	fdc_poll(fdc_t *fdc);
extern void	fdc_abort(fdc_t *fdc);
extern void	fdc_set_dskchg_activelow(fdc_t *fdc);
extern void	fdc_3f1_enable(fdc_t *fdc, int enable);
extern int	fdc_get_bit_rate(fdc_t *fdc);
extern int	fdc_get_bitcell_period(fdc_t *fdc);

/* A few functions to communicate between Super I/O chips and the FDC. */
extern void	fdc_update_enh_mode(fdc_t *fdc, int enh_mode);
extern int	fdc_get_rwc(fdc_t *fdc, int drive);
extern void	fdc_update_rwc(fdc_t *fdc, int drive, int rwc);
extern int	fdc_get_boot_drive(fdc_t *fdc);
extern void	fdc_update_boot_drive(fdc_t *fdc, int boot_drive);
extern void	fdc_update_densel_polarity(fdc_t *fdc, int densel_polarity);
extern uint8_t	fdc_get_densel_polarity(fdc_t *fdc);
extern void	fdc_update_densel_force(fdc_t *fdc, int densel_force);
extern void	fdc_update_drvrate(fdc_t *fdc, int drive, int drvrate);
extern void	fdc_update_drv2en(fdc_t *fdc, int drv2en);

extern void	fdc_noidam(fdc_t *fdc);
extern void	fdc_nosector(fdc_t *fdc);
extern void	fdc_nodataam(fdc_t *fdc);
extern void	fdc_cannotformat(fdc_t *fdc);
extern void	fdc_wrongcylinder(fdc_t *fdc);
extern void	fdc_badcylinder(fdc_t *fdc);
extern void	fdc_writeprotect(fdc_t *fdc);
extern void	fdc_datacrcerror(fdc_t *fdc);
extern void	fdc_headercrcerror(fdc_t *fdc);
extern void	fdc_nosector(fdc_t *fdc);

extern int	real_drive(fdc_t *fdc, int drive);

extern sector_id_t fdc_get_read_track_sector(fdc_t *fdc);
extern int	fdc_get_compare_condition(fdc_t *fdc);
extern int	fdc_is_deleted(fdc_t *fdc);
extern int	fdc_is_sk(fdc_t *fdc);
extern void	fdc_set_wrong_am(fdc_t *fdc);
extern int	fdc_get_drive(fdc_t *fdc);
extern int	fdc_get_perp(fdc_t *fdc);
extern int	fdc_get_format_n(fdc_t *fdc);
extern int	fdc_is_mfm(fdc_t *fdc);
extern double	fdc_get_hut(fdc_t *fdc);
extern double	fdc_get_hlt(fdc_t *fdc);
extern void	fdc_request_next_sector_id(fdc_t *fdc);
extern void	fdc_stop_id_request(fdc_t *fdc);
extern int	fdc_get_gap(fdc_t *fdc);
extern int	fdc_get_gap2(fdc_t *fdc, int drive);
extern int	fdc_get_dtl(fdc_t *fdc);
extern int	fdc_get_format_sectors(fdc_t *fdc);
extern uint8_t	fdc_get_swwp(fdc_t *fdc);
extern void	fdc_set_swwp(fdc_t *fdc, uint8_t swwp);
extern uint8_t	fdc_get_diswr(fdc_t *fdc);
extern void	fdc_set_diswr(fdc_t *fdc, uint8_t diswr);
extern uint8_t	fdc_get_swap(fdc_t *fdc);
extern void	fdc_set_swap(fdc_t *fdc, uint8_t swap);

extern void	fdc_finishcompare(fdc_t *fdc, int satisfying);
extern void	fdc_finishread(fdc_t *fdc);
extern void	fdc_sector_finishcompare(fdc_t *fdc, int satisfying);
extern void	fdc_sector_finishread(fdc_t *fdc);
extern void	fdc_track_finishread(fdc_t *fdc, int condition);
extern int	fdc_is_verify(fdc_t *fdc);

extern void	fdc_overrun(fdc_t *fdc);
extern void	fdc_set_base(fdc_t *fdc, int base);
extern int	fdc_getdata(fdc_t *fdc, int last);
extern int	fdc_data(fdc_t *fdc, uint8_t data);

extern void	fdc_sectorid(fdc_t *fdc, uint8_t track, uint8_t side,
			     uint8_t sector, uint8_t size, uint8_t crc1,
			     uint8_t crc2);

extern uint8_t	fdc_read(uint16_t addr, void *priv);
extern void	fdc_reset(void *priv);

extern uint8_t	fdc_get_current_drive(void);

#ifdef EMU_DEVICE_H
extern const device_t	fdc_xt_device;
extern const device_t	fdc_xt_t1x00_device;
extern const device_t	fdc_xt_amstrad_device;
extern const device_t	fdc_pcjr_device;
extern const device_t	fdc_at_device;
extern const device_t	fdc_at_actlow_device;
extern const device_t	fdc_at_ps1_device;
extern const device_t	fdc_at_smc_device;
extern const device_t	fdc_at_winbond_device;
extern const device_t	fdc_at_nsc_device;
#endif


#endif	/*EMU_FDC_H*/
