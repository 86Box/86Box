/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of the emulated chipsets.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019,2020 Miran Grca.
 */
#ifndef EMU_CHIPSET_H
# define EMU_CHIPSET_H


/* ACC */
extern const device_t	acc2168_device;

/* Acer M3A and V35N */
extern const device_t	acerm3a_device;

/* ALi */
extern const device_t	ali1429_device;

/* Headland */
extern const device_t	headland_device;
extern const device_t	headland_386_device;

/* Intel 4x0xX */
extern const device_t	i420tx_device;
extern const device_t	i420zx_device;
extern const device_t	i430lx_device;
extern const device_t	i430nx_device;
extern const device_t	i430fx_device;
extern const device_t	i430fx_pb640_device;
extern const device_t	i430hx_device;
extern const device_t	i430vx_device;
extern const device_t	i430tx_device;
extern const device_t	i440fx_device;
extern const device_t   i440lx_device;
extern const device_t   i440ex_device;
extern const device_t	i440bx_device;
extern const device_t	i440gx_device;
extern const device_t	i440zx_device;

extern const device_t	ioapic_device;

/* OPTi */
extern const device_t	opti495_device;
extern const device_t	opti5x7_device;

/* C&T */
extern const device_t	neat_device;
extern const device_t	scat_device;
extern const device_t	scat_4_device;
extern const device_t	scat_sx_device;
extern const device_t	cs8230_device;

/* SiS */
extern const device_t   rabbit_device;
extern const device_t	sis_85c471_device;
extern const device_t	sis_85c496_device;
#if defined(DEV_BRANCH) && defined(USE_SIS_85C50X)
extern const device_t	sis_85c50x_device;
#endif

/* VIA */
extern const device_t	via_vpx_device;
extern const device_t   via_vp3_device;
extern const device_t   via_mvp3_device;
extern const device_t   via_apro_device;

/* VLSI */
extern const device_t   vlsi_scamp_device;

/* WD */
extern const device_t	wd76c10_device;


#endif	/*EMU_CHIPSET_H*/
