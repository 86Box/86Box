/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ICD2061 clock generator emulation header.
 *
 *		Used by ET4000w32/p (Diamond Stealth 32)
 *
 * Version:	@(#)vid_icd2061.h	1.0.1	2018/10/02
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018 Miran Grca.
 */
typedef struct icd2061_t
{
        float freq[3];

	int count, bit_count;
	int unlocked, state;
        uint32_t data, ctrl;
} icd2061_t;

void icd2061_write(icd2061_t *icd2061, int val);
void icd2061_init(icd2061_t *icd2061);
float icd2061_getclock(int clock, void *p);
