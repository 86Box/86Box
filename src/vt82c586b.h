/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the VIA Apollo MVP3 southbridge
 *
 * Version:	@(#)via_mvp3_sb.c	1.0.22	2018/10/31
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *      Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *      Copyright 2020 Melissa Goad.
 */

#if defined(DEV_BRANCH) && defined(USE_SS7)
extern const device_t vt82c586b_device;
#endif