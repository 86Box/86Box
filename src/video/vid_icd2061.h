/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ICD2061 clock generator emulation header.
 *		Also emulates the ICS9161 which is the same as the ICD2016,
 *		but without the need for tuning (which is irrelevant in
 *		emulation anyway).
 *
 *		Used by ET4000w32/p (Diamond Stealth 32) and the S3
 *		Vision964 family.
 *
 * Version:	@(#)vid_icd2061.h	1.0.3	2018/10/04
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
float icd2061_getclock(int clock, void *p);

extern const device_t icd2061_device;
extern const device_t ics9161_device;

/* The code is the same, the #define's are so that the correct name can be used. */
#define ics9161_write icd2061_write
#define ics9161_getclock icd2061_getclock
