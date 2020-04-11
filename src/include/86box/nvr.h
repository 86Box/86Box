﻿/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the generic NVRAM/CMOS driver.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>,
 * 		David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2018-2020 David Hrdlička.
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


#define NVR_MAXSIZE	512		/* max size of NVR data */

/* Conversion from BCD to Binary and vice versa. */
#define RTC_BCD(x)      (((x) % 10) | (((x) / 10) << 4))
#define RTC_DCB(x)      ((((x) & 0xf0) >> 4) * 10 + ((x) & 0x0f))
#define RTC_BCDINC(x,y)	RTC_BCD(RTC_DCB(x) + y)

/* Time sync options */
#define TIME_SYNC_DISABLED	0
#define TIME_SYNC_ENABLED	1
#define TIME_SYNC_UTC		2


/* Define a generic RTC/NVRAM device. */
typedef struct _nvr_ {
    wchar_t	*fn;			/* pathname of image file */
    uint16_t	size;			/* device configuration */
    int8_t	irq;

    uint8_t	onesec_cnt;
    pc_timer_t	onesec_time;

    void	*data;			/* local data */

    /* Hooks to device functions. */
    void	(*reset)(struct _nvr_ *);
    void	(*start)(struct _nvr_ *);
    void	(*tick)(struct _nvr_ *);
    void	(*ven_save)(void);

    uint8_t	regs[NVR_MAXSIZE];	/* these are the registers */
} nvr_t;


extern int	nvr_dosave;
#ifdef EMU_DEVICE_H
extern const device_t at_nvr_old_device;
extern const device_t at_nvr_device;
extern const device_t ps_nvr_device;
extern const device_t amstrad_nvr_device;
extern const device_t ibmat_nvr_device;
extern const device_t piix4_nvr_device;
extern const device_t ls486e_nvr_device;
extern const device_t via_nvr_device;
#endif


extern void	rtc_tick(void);

extern void	nvr_init(nvr_t *);
extern wchar_t	*nvr_path(wchar_t *str);
extern FILE	*nvr_fopen(wchar_t *str, wchar_t *mode);
extern int	nvr_load(void);
extern void	nvr_close(void);
extern void	nvr_set_ven_save(void (*ven_save)(void));
extern int	nvr_save(void);

extern int	nvr_is_leap(int year);
extern int	nvr_get_days(int month, int year);
extern void	nvr_time_get(struct tm *);
extern void	nvr_time_set(struct tm *);

extern void	nvr_at_handler(int set, uint16_t base, nvr_t *nvr);
extern void	nvr_at_sec_handler(int set, uint16_t base, nvr_t *nvr);
extern void	nvr_wp_set(int set, int h, nvr_t *nvr);
extern void	nvr_bank_set(int base, uint8_t bank, nvr_t *nvr);
extern void	nvr_lock_set(int base, int size, int lock, nvr_t *nvr);


#endif	/*EMU_NVR_H*/
