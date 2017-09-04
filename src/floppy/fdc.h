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
 * Version:	@(#)fdc.h	1.0.2	2017/09/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_FDC_H
# define EMU_FDC_H


extern void	fdc_init(void);
extern void	fdc_add(void);
extern void	fdc_add_for_superio(void);
extern void	fdc_add_pcjr(void);
extern void	fdc_add_tandy(void);
extern void	fdc_remove(void);
extern void	fdc_reset(void);
extern void	fdc_poll(void);
extern void	fdc_abort(void);
extern void	fdc_floppychange_clear(int drive);
extern void	fdc_set_dskchg_activelow(void);
extern void	fdc_3f1_enable(int enable);
extern void	fdc_set_ps1(void);
extern int	fdc_get_bit_rate(void);
extern int	fdc_get_bitcell_period(void);

/* A few functions to communicate between Super I/O chips and the FDC. */
extern void	fdc_update_is_nsc(int is_nsc);
extern void	fdc_update_max_track(int max_track);
extern void	fdc_update_enh_mode(int enh_mode);
extern int	fdc_get_rwc(int drive);
extern void	fdc_update_rwc(int drive, int rwc);
extern int	fdc_get_boot_drive(void);
extern void	fdc_update_boot_drive(int boot_drive);
extern void	fdc_update_densel_polarity(int densel_polarity);
extern uint8_t	fdc_get_densel_polarity(void);
extern void	fdc_update_densel_force(int densel_force);
extern void	fdc_update_drvrate(int drive, int drvrate);
extern void	fdc_update_drv2en(int drv2en);

extern void	fdc_noidam(void);
extern void	fdc_nosector(void);
extern void	fdc_nodataam(void);
extern void	fdc_cannotformat(void);
extern void	fdc_wrongcylinder(void);
extern void	fdc_badcylinder(void);

extern sector_id_t fdc_get_read_track_sector(void);
extern int	fdc_get_compare_condition(void);
extern int	fdc_is_deleted(void);
extern int	fdc_is_sk(void);
extern void	fdc_set_wrong_am(void);
extern int	fdc_get_drive(void);
extern int	fdc_get_perp(void);
extern int	fdc_get_format_n(void);
extern int	fdc_is_mfm(void);
extern double	fdc_get_hut(void);
extern double	fdc_get_hlt(void);
extern void	fdc_request_next_sector_id(void);
extern void	fdc_stop_id_request(void);
extern int	fdc_get_gap(void);
extern int	fdc_get_gap2(int drive);
extern int	fdc_get_dtl(void);
extern int	fdc_get_format_sectors(void);

extern void	fdc_finishcompare(int satisfying);
extern void	fdc_finishread(void);
extern void	fdc_sector_finishcompare(int satisfying);
extern void	fdc_sector_finishread(void);
extern void	fdc_track_finishread(int condition);
extern int	fdc_is_verify(void);

extern int	real_drive(int drive);
extern void	fdc_overrun(void);
extern void	fdc_set_base(int base, int super_io);
extern int	fdc_ps1_525(void);
extern void	fdc_hard_reset(void);


#endif	/*EMU_FDC_H*/
