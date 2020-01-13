/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ICS2595 clock chip emulation header.  Used by ATI Mach64.
 *
 * Version:	@(#)vid_ics2595.h	1.0.0	2018/10/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
typedef struct ics2595_t
{
    int oldfs3, oldfs2;
    int dat;
    int pos, state;

    double clocks[16];
    double output_clock;
} ics2595_t;

extern void	ics2595_write(ics2595_t *ics2595, int strobe, int dat);

extern const device_t ics2595_device;
