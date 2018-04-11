/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the generic NVRAM/CMOS driver.
 *
 * Version:	@(#)nvr.h	1.0.2	2018/03/11
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EMU_NVR_H
# define EMU_NVR_H


#define NVR_MAXSIZE	128		/* max size of NVR data */

/* Conversion from BCD to Binary and vice versa. */
#define RTC_BCD(x)      (((x) % 10) | (((x) / 10) << 4))
#define RTC_DCB(x)      ((((x) & 0xf0) >> 4) * 10 + ((x) & 0x0f))
#define RTC_BCDINC(x,y)	RTC_BCD(RTC_DCB(x) + y)


/* Define a generic RTC/NVRAM device. */
typedef struct _nvr_ {
    uint8_t	regs[NVR_MAXSIZE];	/* these are the registers */
    wchar_t	*fn;			/* pathname of image file */
    uint16_t	size;			/* device configuration */
    int8_t	irq;

    int8_t	upd_stat,		/* FIXME: move to private struct */
		addr;
    int64_t	upd_ecount,		/* FIXME: move to private struct */
		onesec_time,
		onesec_cnt,
    		rtctime;

    /* Hooks to device functions. */
    void	(*reset)(struct _nvr_ *);
    void	(*start)(struct _nvr_ *);
    void	(*tick)(struct _nvr_ *);
} nvr_t;


extern int	nvr_dosave;


extern void	nvr_init(nvr_t *);
extern int	nvr_load(void);
extern int	nvr_save(void);

extern int	nvr_is_leap(int year);
extern int	nvr_get_days(int month, int year);
extern void	nvr_time_get(struct tm *);
extern void	nvr_time_set(struct tm *);

extern wchar_t	*nvr_path(wchar_t *str);
extern FILE	*nvr_fopen(wchar_t *str, wchar_t *mode);

extern void	nvr_at_init(int irq);
extern void	nvr_at_close(void);


#endif	/*EMU_NVR_H*/
