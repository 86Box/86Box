/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header of the emulation of the Amstrad series of PC's:
 *		PC1512, PC1640 and PC200, including their keyboard, mouse and
 *		video devices, as well as the PC2086 and PC3086 systems.
 *
 * Version:	@(#)m_amstrad.h	1.0.0	2019/03/21
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2019 Sarah Walker.
 */
extern int amstrad_latch;

enum
{
    AMSTRAD_NOLATCH,
    AMSTRAD_SW9,
    AMSTRAD_SW10
};
