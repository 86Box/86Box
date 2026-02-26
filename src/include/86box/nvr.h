/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Definitions for the generic NVRAM/CMOS driver.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>,
 *          David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2018-2020 David Hrdlička.
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
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
#define EMU_NVR_H

#define NVR_MAXSIZE 512 /* max size of NVR data */

/* Conversion from BCD to Binary and vice versa. */
#define RTC_BCD(x)       (((x) % 10) | (((x) / 10) << 4))
#define RTC_DCB(x)       ((((x) &0xf0) >> 4) * 10 + ((x) &0x0f))
#define RTC_BCDINC(x, y) RTC_BCD(RTC_DCB(x) + y)

/* Time sync options */
#define TIME_SYNC_DISABLED 0
#define TIME_SYNC_ENABLED  1
#define TIME_SYNC_UTC      2

/* Century register for the AT, etc. */
#define RTC_CENTURY_AT            0x32
/* Century register for the PS/1 and the PS/2. */
#define RTC_CENTURY_PS            0x37
/* Century register for the Epson Equity LT. */
#define RTC_CENTURY_ELT           0x1a
/* century register for VIA VT82C586B. */
#define RTC_CENTURY_VIA           0x7f
/* No century register. */
#define RTC_CENTURY_NONE          0xff

#define FLAG_ZERO_DEFAULT       0x0001
#define FLAG_NO_NMI             0x0002
#define FLAG_MULTI_ADDRESS      0x0004
#define FLAG_MULTI_BANK         0x0008
#define FLAG_PIIX4              0x0010
#define FLAG_FIXED_SIZE         0x0020
#define FLAG_MARTIN_HACK        0x0040
#define FLAG_AMI_1992_HACK      0x0080
#define FLAG_AMI_1994_HACK      0x0100
#define FLAG_P6RP4_HACK         0x0200
#define FLAG_AMI_1995_HACK      0x0400
#define FLAG_AMI_1999_HACK      0x0800
#define FLAG_AMI_1999J_HACK     0x1000
#define FLAG_SPITFIRE_HACK      0x2000
#define FLAG_BX6_HACK           0x4000

#define NVR_CENTURY_AT          (RTC_CENTURY_AT << 16)
#define NVR_CENTURY_PS          (RTC_CENTURY_PS << 16)
#define NVR_CENTURY_VIA         (RTC_CENTURY_VIA << 16)
#define NVR_CENTURY_ELT         (RTC_CENTURY_ELT << 16)
#define NVR_CENTURY_NONE        (0xff << 16)

#define NVR_ADDR_STANDARD       (0x0070ULL << 24)
#define NVR_ADDR_ELT            (0x11b4ULL << 24)
#define NVR_ADDR_CONFIG         (0xfffeULL << 24)
#define NVR_ADDR_NONE           (0xffffULL << 24)

#define NVR_IRQ_STANDARD        (0x0008ULL << 40)
#define NVR_IRQ_AMSTRAD         (0x0001ULL << 40)
#define NVR_IRQ_CONFIG          (0xfffeULL << 40)
#define NVR_IRQ_NONE            (0xffffULL << 40)

#define NVR_AT_STANDARD         NVR_IRQ_STANDARD | NVR_ADDR_STANDARD
#define NVR_AT_STANDARD_MA      NVR_AT_STANDARD | FLAG_MULTI_ADDRESS
#define NVR_AT_STANDARD_ZERO    NVR_AT_STANDARD | FLAG_ZERO_DEFAULT

#define NVR_AT_OLD              (NVR_AT_STANDARD | NVR_CENTURY_NONE)
#define NVR_AT                  (NVR_AT_STANDARD | NVR_CENTURY_AT)
#define NVR_AT_ZERO_DEFAULT     (NVR_AT_STANDARD_ZERO | NVR_CENTURY_AT)
#define NVR_AT_MA               (NVR_AT | FLAG_MULTI_ADDRESS)
#define NVR_AT_MA_ZERO          (NVR_AT_ZERO_DEFAULT | FLAG_MULTI_ADDRESS)
#define NVR_AT_MB               (NVR_AT_ZERO_DEFAULT | FLAG_MULTI_BANK)
#define NVR_PS                  (NVR_AT_STANDARD_ZERO | NVR_CENTURY_PS)
#define NVR_PS_NO_NMI           (NVR_PS | FLAG_NO_NMI)
#define NVR_AMSTRAD             (NVR_IRQ_AMSTRAD | NVR_ADDR_STANDARD | NVR_CENTURY_AT)
#define NVR_AMSTRAD_NO_NMI      (NVR_AMSTRAD | FLAG_NO_NMI)
#define NVR_PIIX4               (NVR_AT_MA_ZERO | FLAG_PIIX4)
#define NVR_AMI_1992            (NVR_AT_ZERO_DEFAULT | FLAG_AMI_1992_HACK)
#define NVR_AMI_1994            (NVR_AT_ZERO_DEFAULT | FLAG_AMI_1994_HACK)
#define NVR_AMI_1995            (NVR_AT_ZERO_DEFAULT | FLAG_AMI_1995_HACK)
#define NVR_P6RP4               (NVR_AT_MA_ZERO | FLAG_P6RP4_HACK)
#define NVR_PIIX4_AMI_1995      (NVR_PIIX4 | FLAG_AMI_1999_HACK)
#define NVR_PIIX4_AMI_1995J     (NVR_PIIX4 | FLAG_AMI_1999J_HACK)
#define NVR_VIA                 (NVR_AT_STANDARD_MA | NVR_CENTURY_VIA)
#define NVR_MARTIN              (NVR_AT_MB | FLAG_MARTIN_HACK)
#define NVR_ELT                 (NVR_IRQ_NONE | NVR_ADDR_ELT | NVR_CENTURY_ELT)
#define NVR_RTC8088             (NVR_IRQ_CONFIG | NVR_ADDR_CONFIG | \
                                 NVR_CENTURY_ALT | FLAG_NO_NMI | FLAG_FIXED_SIZE)

#ifdef _TIMER_H_
/* Define a generic RTC/NVRAM device. */
typedef struct _nvr_ {
    char    *fn;   /* pathname of image file */
    uint16_t size; /* device configuration */
    int8_t   irq;
    int8_t   is_new;

    uint8_t    onesec_cnt;
    pc_timer_t onesec_time;

    void *data; /* local data */

    /* Hooks to device functions. */
    void (*reset)(struct _nvr_ *);
    void (*start)(struct _nvr_ *);
    void (*tick)(struct _nvr_ *);
    void (*ven_save)(void);

    uint8_t regs[NVR_MAXSIZE]; /* these are the registers */
} nvr_t;

extern int nvr_dosave;

#ifdef EMU_DEVICE_H
extern const device_t nvr_at_device;
#endif

extern void rtc_tick(void);

extern void  nvr_init(nvr_t *);
extern int   nvr_load(void);
extern void  nvr_close(void);
extern void  nvr_set_ven_save(void (*ven_save)(void));
extern int   nvr_save(void);

extern int  nvr_is_leap(int year);
extern int  nvr_get_days(int month, int year);
extern void nvr_time_sync(void);
extern void nvr_time_get(void *priv);
extern void nvr_time_set(void *priv);

extern void nvr_reg_write(uint16_t reg, uint8_t val, void *priv);
extern void nvr_at_handler(int set, uint16_t base, nvr_t *nvr);
extern void nvr_at_sec_handler(int set, uint16_t base, nvr_t *nvr);
extern void nvr_at_index_read_handler(int set, uint16_t base, nvr_t *nvr);
extern void nvr_read_addr_set(int set, nvr_t *nvr);
extern uint8_t nvr_get_index(void *priv, uint8_t addr_id);
extern void nvr_at_data_port(int set, nvr_t *nvr);
extern void nvr_wp_set(int set, int h, nvr_t *nvr);
extern void nvr_via_wp_set(int set, int reg, nvr_t *nvr);
extern void nvr_bank_set(int base, uint8_t bank, nvr_t *nvr);
extern void nvr_lock_set(int base, int size, int lock, nvr_t *nvr);
extern void nvr_irq_set(int irq, nvr_t *nvr);
extern void nvr_smi_enable(int enable, nvr_t *nvr);
extern uint8_t nvr_smi_status(nvr_t *nvr);
extern void nvr_smi_status_clear(nvr_t *nvr);
#endif

extern char *nvr_path(char *str);
extern FILE *nvr_fopen(char *str, char *mode);

#endif /*EMU_NVR_H*/
