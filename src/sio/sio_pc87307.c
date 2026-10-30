/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NatSemi PC87307 Super I/O chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/sio.h>
#include <86box/random.h>
#include <86box/pic.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include "cpu.h"

/* RTC bank 2, the APC; indices into apc[], register - 40h. The PC87307/
   PC97307 have 40h-48h (PC87307/PC97307 data sheet, 4.5 and Table 4-9),
   the PC97317 all of them (PC87317VUL/PC97317VUL data sheet, 4.5 and
   Table 4-22). */
#define APC_APCR1      0x00
#define APC_APCR2      0x01
#define APC_APSR       0x02
#define APC_WDW        0x03
#define APC_WDM        0x04
#define APC_WM         0x05
#define APC_WY         0x06
#define APC_RLR        0x07
#define APC_WC         0x08
#define APC_APCR3      0x09
#define APC_APCR4      0x0a
#define APC_APCR5      0x0b
#define APC_APCR6      0x0c
#define APC_APCR7      0x0d
#define APC_APSR1      0x0e
#define APC_DADDR      0x0f
#define APC_MADDR      0x10
#define APC_CADDR      0x11

#define APCR1_FS_TRIG  0x01 /* Fail-safe Timer Trigger Command (PC97317) */
#define APCR1_FS_21S   0x02 /* Switch Off Delay Option: 21 s, else 5 s */
#define APCR1_SOC      0x20 /* Software Off Command */
#define APCR1_FS_RESET 0x40 /* Fail-safe Timer Reset Command */
#define APCR1_PF       0x80 /* Power Failure */
#define APCR2_SODE     0x40 /* Switch Off Delay Enable */

#define APSR_TMD       0x01 /* Timer Match Detect */
#define APSR_FTD       0x10 /* Fail-Safe Timer Detect */
#define APSR_SOED      0x20 /* Switch Off Event Detect */

#define RLR_UPPER      0x08 /* Upper RAM Block */
#define RLR_READ_LOW   0x10 /* RAM Block Read, upper RAM 00h-1Fh */
#define RLR_WRITE_LOW  0x20 /* RAM Block Write, upper RAM 00h-1Fh */
#define RLR_MASK_WRITE 0x40 /* RAM Mask Write, bank 0 and upper RAM */
#define RLR_LOCK       0x80 /* RAM Lock, 38h-3Fh */

/* RTC bank 1 (PC87307 Table 4-8, PC97317 Table 4-21); indices into
   rtc_bank1[], register - 40h. The date and month alarms are the
   PC97317's. */
#define RTC1_CENTURY   0x08
#define RTC1_DOM_ALARM 0x09
#define RTC1_MON_ALARM 0x0a
#define RTC1_URAM_ADDR 0x10
#define RTC1_URAM_DATA 0x13

/* The ACPI fixed registers (4.6, 4.7), all battery-backed: acpi[]. */
#define ACPI_STS_LO    0x00
#define ACPI_STS_HI    0x01
#define ACPI_EN_LO     0x02
#define ACPI_EN_HI     0x03
#define ACPI_CNT_LO    0x04
#define ACPI_CNT_HI    0x05
#define ACPI_GP1_STS0  0x06
#define ACPI_GP1_EN0   0x07
#define ACPI_GP2_EN0   0x08
#define ACPI_SMI_CMD   0x09

#define TMR_STS        0x01 /* PM1_STS_LOW */
#define GBL_STS        0x20
#define PWRBTN_STS     0x01 /* PM1_STS_HIGH */
#define RTC_STS        0x04
#define WAK_STS        0x80
#define SCI_EN         0x01 /* PM1_CNT_LOW */
#define GBL_RLS        0x04
#define SLP_EN         0x20 /* PM1_CNT_HIGH */

/* Power management registers 01h (FER2), 04h (PMC3) and 10h (ACPI Support,
   10.2). */
#define FER2_PM1_EVT   0x08
#define FER2_PM1_CNT   0x10
#define FER2_GPE       0x20
#define FER2_SMI_CMD   0x40
#define FER2_GPIO      0x80
#define PMC3_TMR_CLK   0x01
#define ACPI_SUP_STICKY 0x25 /* Sleep Enable, ACPI Global Lock, SMI Command Status */
#define ACPI_SUP_BGRLS 0x10 /* BIOS Global Lock Release */
#define ACPI_SUP_MASK  0x80 /* Mask PM1 Event Register Bits */

/* The battery-backed RTC bank, APC and ACPI state in <machine>_apc.nvr. */
#define APC_NVR_SIZE (64 + 64 + 128 + 16)

/* The PM timer runs at 3.579545 MHz (4.4, "Power Management Timer"). */
#define PM_TMR_FREQ    3579545

typedef struct pc87307_t {
    uint8_t   id;
    uint8_t   baddr;
    uint8_t   pm_idx;
    uint8_t   regs[48];
    uint8_t   ld_regs[256][256];
    uint8_t   pcregs[16];
    uint8_t   gpio[2][8];
    uint8_t   pm[0x20];
    uint16_t  superio_base;
    uint16_t  gpio_base;
    uint16_t  gpio_base2;
    uint16_t  pm_base;
    int       cur_reg;
    void     *kbc;
    fdc_t    *fdc;
    serial_t *uart[2];
    lpt_t    *lpt;

    /* The RTC, with its banks 1 and 2 and the APC, when the chip owns the
       board's NVR (always on the PC97317), and the PC97317's ACPI fixed
       registers. */
    int        is_97317;
    nvr_t     *nvr;
    uint16_t   rtc_base;
    uint8_t    rtc_bank1[64];
    uint8_t    apc[64];
    uint8_t    upper_ram[128];
    uint8_t    acpi[16];
    uint8_t    sleeping;
    uint8_t    switch_off;
    uint8_t    sci_state;
    int8_t     sci_irq;
    uint16_t   acpi_base[4];
    uint32_t   pm_tmr_latch;
    char      *apc_fn;
    pc_timer_t pm_tmr_timer;
    pc_timer_t failsafe_timer;
} pc87307_t;

enum {
    ACPI_BLK_EVT = 0,
    ACPI_BLK_CNT,
    ACPI_BLK_TMR,
    ACPI_BLK_GPE
};

enum {
    LD_KBD   = 0,
    LD_MOUSE,
    LD_RTC,
    LD_FDC,
    LD_LPT,
    LD_UART2,
    LD_UART1,
    LD_GPIO,
    LD_PM
} pc87307_ld_t;

#define LD_MIN LD_KBD
#define LD_MAX LD_PM

static void    fdc_handler(pc87307_t *dev);
static void    lpt_handler(pc87307_t *dev);
static void    serial_handler(pc87307_t *dev, int uart);
static void    kbc_handler(pc87307_t *dev);
static void    rtc_handler(pc87307_t *dev);
static void    gpio_handler(pc87307_t *dev);
static void    pc87307_write(uint16_t port, uint8_t val, void *priv);
static uint8_t pc87307_read(uint16_t port, void *priv);

#ifdef ENABLE_PC87307_LOG
int pc87307_do_log = ENABLE_PC87307_LOG;

static void
pc87307_log(const char *fmt, ...)
{
    va_list ap;

    if (pc87307_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pc87307_log(fmt, ...)
#endif

static void
pc87307_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev  = (pc87307_t *) priv;
    uint8_t    bank = !!(dev->regs[0x22] & 0x80);

    /* Bit 7 of SCNF2 = bank. */
    pc87307_log("[%04X:%08X] [W] (%04X) Bank %i = %02X\n",
                CS, cpu_state.pc, port, bank, val);

    dev->gpio[bank][port & 0x0007] = val;

    if (bank == 0) {
        machine_handle_gpio(1, ((dev->gpio[0][5] & dev->gpio[0][4]) << 8) | (dev->gpio[0][0] & dev->gpio[0][1]));
    }
}

/* The level on the pins of GPIO port 1 or 2: the data latch where the
   direction is output, the board's level (the pull-ups unless something
   drives them) where it is input (PC87317VUL datasheet, 9.2.2 and Table
   9-2). */
uint8_t
pc87307_gpio_pins(void *priv, int port)
{
    const pc87307_t *dev  = (pc87307_t *) priv;
    const int        reg  = (port == 2) ? 4 : 0;
    const uint8_t    dir  = dev->gpio[0][reg + 1];
    const uint8_t    pins = (machine_handle_gpio(0, 0xFFFF) >> ((port == 2) ? 8 : 0)) & 0xFF;

    return (dev->gpio[0][reg] & dir) | (pins & ~dir);
}

uint8_t
pc87307_gpio_read(uint16_t port, void *priv)
{
    const pc87307_t *dev  = (pc87307_t *) priv;
    uint8_t          pins = 0xff;
    uint8_t          bank = !!(dev->regs[0x22] & 0x80);
    uint8_t          ret  = dev->gpio[bank][port & 0x0007];

    switch (port & 0x0007) {
        case 0x0000:
            if (bank == 0) {
                uint8_t mask = dev->gpio[0][1];
                pins = machine_handle_gpio(0, 0xFFFF) & 0xFF;
                ret  = (ret & mask) | (pins & ~mask);
            }
            break;
        case 0x0004:
            if (bank == 0) {
                uint8_t mask = dev->gpio[0][5];
                pins = (machine_handle_gpio(0, 0xFFFF) >> 8) & 0xFF;
                ret  = (ret & mask) | (pins & ~mask);
            } else
                ret = 0xff;
            break;

        default:
            if (bank == 1)
                ret = 0xff;
            break;
    }

    /* Bit 7 of SCNF2 = bank. */
    pc87307_log("[%04X:%08X] [R] (%04X) Bank %i = %02X\n",
                CS, cpu_state.pc, port, bank, ret);

    return ret;
}

static void
pc87307_gpio_remove(pc87307_t *dev)
{
    if (dev->gpio_base != 0xffff) {
        io_removehandler(dev->gpio_base, 0x0008,
                         pc87307_gpio_read, NULL, NULL, pc87307_gpio_write, NULL, NULL, dev);
        dev->gpio_base = 0xffff;
    }

    if (dev->gpio_base2 != 0xffff) {
        io_removehandler(dev->gpio_base2, 0x0008,
                         pc87307_gpio_read, NULL, NULL, pc87307_gpio_write, NULL, NULL, dev);
        dev->gpio_base2 = 0xffff;
    }
}

static void
pc87307_gpio_init(pc87307_t *dev, int bank, uint16_t addr)
{
    uint16_t *bank_base = bank ? &(dev->gpio_base2) : &(dev->gpio_base);

    *bank_base = addr;

    io_sethandler(*bank_base, 0x0008,
                  pc87307_gpio_read, NULL, NULL, pc87307_gpio_write, NULL, NULL, dev);
}

/* The PC97317's SCI (4.4, "The SCI Signal"): a level interrupt on the IRQ
   picked by SIOC3[7:4] (Table 2-23). The RTC alarm, the PM timer carry and
   the BIOS Global Lock release always reach it; the power button and the
   general purpose events only with SCI_EN, POR otherwise. */
static void
pc97317_sci_update(pc87307_t *dev)
{
    static const int8_t sci_irqs[16] = { -1, 1, -1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, -1, 14, 15 };
    const uint8_t      *acpi         = dev->acpi;
    int8_t              irq          = sci_irqs[dev->regs[0x25] >> 4];
    int                 level;

    level = !!(acpi[ACPI_STS_HI] & acpi[ACPI_EN_HI] & RTC_STS) ||
            !!(acpi[ACPI_STS_LO] & acpi[ACPI_EN_LO] & GBL_STS);
    if (dev->pm[0x04] & PMC3_TMR_CLK)
        level |= !!(acpi[ACPI_STS_LO] & acpi[ACPI_EN_LO] & TMR_STS);
    if (acpi[ACPI_CNT_LO] & SCI_EN)
        level |= !!(acpi[ACPI_STS_HI] & acpi[ACPI_EN_HI] & PWRBTN_STS) ||
                 !!(acpi[ACPI_GP1_STS0] & acpi[ACPI_GP1_EN0]);

    if ((irq != dev->sci_irq) && (dev->sci_irq >= 0))
        picintclevel(1 << dev->sci_irq, &dev->sci_state);
    dev->sci_irq = irq;

    if (irq < 0)
        return;

    if (level)
        picintlevel(1 << irq, &dev->sci_state);
    else
        picintclevel(1 << irq, &dev->sci_state);
}

/* A power management event while the system sleeps (SLP_EN) sets WAK_STS. */
static void
pc97317_pm_event(pc87307_t *dev)
{
    if (dev->sleeping)
        dev->acpi[ACPI_STS_HI] |= WAK_STS;

    pc97317_sci_update(dev);
}

/* An access to an ACPI fixed register while WAK_STS is set ends the
   suspended state (4.6.2). */
static void
pc97317_acpi_access(pc87307_t *dev)
{
    if (dev->acpi[ACPI_STS_HI] & WAK_STS)
        dev->sleeping = 0;
}

/* The PM timer: 24 bits at 3.579545 MHz; TMR_STS when bit 23 toggles. */
static uint32_t
pc97317_pm_tmr_count(void)
{
    return (uint32_t) ((((double) tsc) * ((double) PM_TMR_FREQ)) / ((double) cpuclock)) & 0x00ffffff;
}

static void
pc97317_pm_tmr_schedule(pc87307_t *dev)
{
    uint32_t left = 0x00800000 - (pc97317_pm_tmr_count() & 0x007fffff);

    if (dev->pm[0x04] & PMC3_TMR_CLK)
        timer_on_auto(&dev->pm_tmr_timer, ((double) left * 1000000.0) / (double) PM_TMR_FREQ);
    else
        timer_stop(&dev->pm_tmr_timer);
}

static void
pc97317_pm_tmr_carry(void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    dev->acpi[ACPI_STS_LO] |= TMR_STS;
    pc97317_sci_update(dev);
    pc97317_pm_tmr_schedule(dev);
}

static void
pc87307_power_off(UNUSED(pc87307_t *dev))
{
    pc87307_log("APC: ONCTL deasserted, powering off\n");
    plat_power_off();
}

/* The Fail-safe Timer reaching terminal count turns the power off when the
   Switch Off delay is on and the events go to POR (4.4, ONCTL). */
static void
pc87307_failsafe_expire(void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    dev->apc[APC_APSR] |= APSR_FTD;

    if ((dev->apc[APC_APCR2] & APCR2_SODE) && !(dev->acpi[ACPI_CNT_LO] & SCI_EN))
        pc87307_power_off(dev);
}

static void
pc87307_failsafe_start(pc87307_t *dev)
{
    timer_on_auto(&dev->failsafe_timer, (dev->apc[APC_APCR1] & APCR1_FS_21S) ? 21000000.0 : 5000000.0);
}

/* The SWITCH input (the power button): a press while the system is on is a
   Switch Off event. With SCI_EN clear it turns ONCTL off, at once or after
   the Switch Off delay; with SCI_EN set it is left to the OS (PWRBTN_STS). */
static void
pc97317_switch_off_event(pc87307_t *dev)
{
    dev->acpi[ACPI_STS_HI] |= PWRBTN_STS;
    dev->apc[APC_APSR] |= APSR_SOED;

    if (!(dev->acpi[ACPI_CNT_LO] & SCI_EN)) {
        if (dev->apc[APC_APCR2] & APCR2_SODE) {
            if (!timer_is_on(&dev->failsafe_timer))
                pc87307_failsafe_start(dev);
        } else {
            pc87307_power_off(dev);
            return;
        }
    }

    pc97317_pm_event(dev);
}

static void
pc97317_power_button(void *priv)
{
    pc97317_switch_off_event((pc87307_t *) priv);
}

static uint8_t
pc97317_evt_read(uint16_t port, void *priv)
{
    pc87307_t *dev  = (pc87307_t *) priv;
    uint8_t    mask = 0xff;
    uint8_t    ret;

    pc97317_acpi_access(dev);

    /* Mask PM1 Event Register Bits: only RTC_STS, RTC_EN and WAK_STS are
       left; TMR_STS/TMR_EN follow PMC3 bit 0 alone. */
    switch (port & 0x03) {
        default:
        case 0x00:
            ret = dev->acpi[ACPI_STS_LO] & (TMR_STS | GBL_STS);
            break;
        case 0x01:
            ret = dev->acpi[ACPI_STS_HI] & 0x8d;
            mask = (dev->pm[0x10] & ACPI_SUP_MASK) ? (RTC_STS | WAK_STS) : 0xff;
            break;
        case 0x02:
            ret = dev->acpi[ACPI_EN_LO] & (TMR_STS | GBL_STS);
            break;
        case 0x03:
            ret = dev->acpi[ACPI_EN_HI] & (PWRBTN_STS | RTC_STS);
            mask = (dev->pm[0x10] & ACPI_SUP_MASK) ? RTC_STS : 0xff;
            break;
    }

    if (!(port & 0x01)) {
        if (dev->pm[0x10] & ACPI_SUP_MASK)
            mask = TMR_STS;
        if (!(dev->pm[0x04] & PMC3_TMR_CLK))
            mask &= ~TMR_STS;
    }

    return ret & mask;
}

static void
pc97317_evt_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    pc97317_acpi_access(dev);

    switch (port & 0x03) {
        default:
        case 0x00:
            dev->acpi[ACPI_STS_LO] &= ~(val & (TMR_STS | GBL_STS));
            break;
        case 0x01:
            dev->acpi[ACPI_STS_HI] &= ~(val & 0x8d);
            break;
        case 0x02:
            dev->acpi[ACPI_EN_LO] = val & (TMR_STS | GBL_STS);
            break;
        case 0x03:
            dev->acpi[ACPI_EN_HI] = val & (PWRBTN_STS | RTC_STS);
            break;
    }

    pc97317_sci_update(dev);
}

static uint8_t
pc97317_cnt_read(uint16_t port, void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    pc97317_acpi_access(dev);

    return (port & 0x01) ? (dev->acpi[ACPI_CNT_HI] & 0x1c) : (dev->acpi[ACPI_CNT_LO] & SCI_EN);
}

static void
pc97317_cnt_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    pc97317_acpi_access(dev);

    if (port & 0x01) {
        /* SLP_TYP is only stored; SLP_EN sets the ACPI Support register's
           Sleep Enable Status (and POR, which is not modelled). */
        dev->acpi[ACPI_CNT_HI] = val & 0x1c;
        if (val & SLP_EN) {
            dev->pm[0x10] |= 0x01;
            dev->sleeping = 1;
        }
    } else {
        dev->acpi[ACPI_CNT_LO] = val & SCI_EN;
        if (val & GBL_RLS)
            dev->pm[0x10] |= 0x04;
    }

    pc97317_sci_update(dev);
}

/* PM1_TMR_LOW latches the count for the other bytes; PM1_TMR_EXT reads 0. */
static uint8_t
pc97317_tmr_read(uint16_t port, void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    pc97317_acpi_access(dev);

    if (!(port & 0x03))
        dev->pm_tmr_latch = pc97317_pm_tmr_count();

    return (dev->pm_tmr_latch >> ((port & 0x03) << 3)) & 0xff;
}

static uint32_t
pc97317_tmr_readl(UNUSED(uint16_t port), void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    pc97317_acpi_access(dev);

    dev->pm_tmr_latch = pc97317_pm_tmr_count();

    return dev->pm_tmr_latch;
}

/* The general purpose event block: GP1_STS0 at 00h, GP1_EN0 at 04h,
   GP2_EN0 at 08h and SMI_CMD at 0Ch; FER2 enables SMI_CMD apart. */
static uint8_t
pc97317_gpe_read(uint16_t port, void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;
    uint8_t    ret = 0x00;

    if ((port & 0x0f) == 0x0c)
        return (dev->pm[0x01] & FER2_SMI_CMD) ? dev->acpi[ACPI_SMI_CMD] : 0xff;

    if (!(dev->pm[0x01] & FER2_GPE))
        return 0xff;

    pc97317_acpi_access(dev);

    switch (port & 0x0f) {
        case 0x00:
            ret = dev->acpi[ACPI_GP1_STS0];
            break;
        case 0x04:
            ret = dev->acpi[ACPI_GP1_EN0];
            break;
        case 0x08:
            ret = dev->acpi[ACPI_GP2_EN0];
            break;
        default:
            break;
    }

    return ret;
}

static void
pc97317_gpe_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    if ((port & 0x0f) == 0x0c) {
        /* Any write sets SMI Command Status (and POR, not modelled). */
        if (dev->pm[0x01] & FER2_SMI_CMD) {
            dev->acpi[ACPI_SMI_CMD] = val;
            dev->pm[0x10] |= 0x20;
        }
        return;
    }

    if (!(dev->pm[0x01] & FER2_GPE))
        return;

    pc97317_acpi_access(dev);

    switch (port & 0x0f) {
        case 0x00:
            dev->acpi[ACPI_GP1_STS0] &= ~val;
            break;
        case 0x04:
            dev->acpi[ACPI_GP1_EN0] = val;
            break;
        case 0x08:
            dev->acpi[ACPI_GP2_EN0] = val;
            break;
        default:
            break;
    }

    pc97317_sci_update(dev);
}

static void
pc97317_acpi_set_block(pc87307_t *dev, int blk, uint16_t base, int on)
{
    static const uint16_t sizes[4] = { 4, 2, 4, 16 };

    if (dev->acpi_base[blk] != 0x0000) {
        switch (blk) {
            case ACPI_BLK_EVT:
                io_removehandler(dev->acpi_base[blk], sizes[blk],
                                 pc97317_evt_read, NULL, NULL, pc97317_evt_write, NULL, NULL, dev);
                break;
            case ACPI_BLK_CNT:
                io_removehandler(dev->acpi_base[blk], sizes[blk],
                                 pc97317_cnt_read, NULL, NULL, pc97317_cnt_write, NULL, NULL, dev);
                break;
            case ACPI_BLK_TMR:
                io_removehandler(dev->acpi_base[blk], sizes[blk],
                                 pc97317_tmr_read, NULL, pc97317_tmr_readl, NULL, NULL, NULL, dev);
                break;
            case ACPI_BLK_GPE:
                io_removehandler(dev->acpi_base[blk], sizes[blk],
                                 pc97317_gpe_read, NULL, NULL, pc97317_gpe_write, NULL, NULL, dev);
                break;
        }
        dev->acpi_base[blk] = 0x0000;
    }

    if (!on || (base == 0x0000))
        return;

    dev->acpi_base[blk] = base;
    switch (blk) {
        case ACPI_BLK_EVT:
            io_sethandler(base, sizes[blk],
                          pc97317_evt_read, NULL, NULL, pc97317_evt_write, NULL, NULL, dev);
            break;
        case ACPI_BLK_CNT:
            io_sethandler(base, sizes[blk],
                          pc97317_cnt_read, NULL, NULL, pc97317_cnt_write, NULL, NULL, dev);
            break;
        case ACPI_BLK_TMR:
            io_sethandler(base, sizes[blk],
                          pc97317_tmr_read, NULL, pc97317_tmr_readl, NULL, NULL, NULL, dev);
            break;
        case ACPI_BLK_GPE:
            io_sethandler(base, sizes[blk],
                          pc97317_gpe_read, NULL, NULL, pc97317_gpe_write, NULL, NULL, dev);
            break;
    }
}

/* The ACPI fixed register blocks sit at the bases in power management
   registers 08h-0Fh, enabled by FER2 (the timer by PMC3 bit 0), whatever
   logical devices 2 and 8 are doing (4.6). */
static void
pc97317_acpi_handler(pc87307_t *dev)
{
    const uint8_t *pm = dev->pm;

    pc97317_acpi_set_block(dev, ACPI_BLK_EVT, (pm[0x09] << 8) | pm[0x08], pm[0x01] & FER2_PM1_EVT);
    pc97317_acpi_set_block(dev, ACPI_BLK_TMR, (pm[0x0b] << 8) | pm[0x0a], pm[0x04] & PMC3_TMR_CLK);
    pc97317_acpi_set_block(dev, ACPI_BLK_CNT, (pm[0x0d] << 8) | pm[0x0c], pm[0x01] & FER2_PM1_CNT);
    pc97317_acpi_set_block(dev, ACPI_BLK_GPE, (pm[0x0f] << 8) | pm[0x0e], pm[0x01] & (FER2_GPE | FER2_SMI_CMD));

    pc97317_pm_tmr_schedule(dev);
    pc97317_sci_update(dev);
}

static void
pc87307_pm_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    if (!(port & 1)) {
        /* The PC97317 has 17 power management registers (Table 10-1). */
        dev->pm_idx = val & (dev->is_97317 ? 0x1f : 0x07);
        return;
    }

    if (!dev->is_97317) {
        dev->pm[dev->pm_idx] = val;

        /* FER1 - Function Enable Register 1. */
        if (dev->pm_idx == 0x00) {
            rtc_handler(dev);
            fdc_handler(dev);
            lpt_handler(dev);
            serial_handler(dev, 1);
            serial_handler(dev, 0);
        }
        return;
    }

    switch (dev->pm_idx) {
        /* FER1 - Function Enable Register 1: KBC, RTC, FDC, LPT, UARTs. */
        case 0x00:
            dev->pm[0x00] = val;
            kbc_handler(dev);
            rtc_handler(dev);
            fdc_handler(dev);
            lpt_handler(dev);
            serial_handler(dev, 1);
            serial_handler(dev, 0);
            break;
        /* FER2 - Function Enable Register 2: the ACPI blocks and GPIO. */
        case 0x01:
            dev->pm[0x01] = val;
            pc97317_acpi_handler(dev);
            gpio_handler(dev);
            break;
        /* PMC2 - bit 7, the multiplier clock status, is read-only. */
        case 0x03:
            dev->pm[0x03] = val & 0x07;
            break;
        /* PMC3 - bits 7 and 6, the UART busy indicators, are read-only. */
        case 0x04:
            dev->pm[0x04] = val & 0x0f;
            pc97317_acpi_handler(dev);
            break;
        /* WDST is read-only. */
        case 0x07:
            break;
        /* The ACPI fixed register base addresses; the timer and event
           blocks are dword aligned, control word, GPE 16 bytes. */
        case 0x08: case 0x0a:
            dev->pm[dev->pm_idx] = val & 0xfc;
            pc97317_acpi_handler(dev);
            break;
        case 0x0c:
            dev->pm[dev->pm_idx] = val & 0xfe;
            pc97317_acpi_handler(dev);
            break;
        case 0x0e:
            dev->pm[dev->pm_idx] = val & 0xf0;
            pc97317_acpi_handler(dev);
            break;
        case 0x09: case 0x0b: case 0x0d: case 0x0f:
            dev->pm[dev->pm_idx] = val;
            pc97317_acpi_handler(dev);
            break;
        /* ACPI Support: Sleep Enable, ACPI Global Lock and SMI Command
           Status are write 1 to clear; BIOS Global Lock Release sets
           GBL_STS. */
        case 0x10:
            dev->pm[0x10] = ((dev->pm[0x10] & ~val) & ACPI_SUP_STICKY) | (val & ~(ACPI_SUP_STICKY | ACPI_SUP_BGRLS));
            if (val & ACPI_SUP_BGRLS)
                dev->acpi[ACPI_STS_LO] |= GBL_STS;
            pc97317_sci_update(dev);
            break;
        case 0x02: case 0x05: case 0x06:
            dev->pm[dev->pm_idx] = val;
            break;
        default:
            break;
    }
}

uint8_t
pc87307_pm_read(uint16_t port, void *priv)
{
    const pc87307_t *dev = (pc87307_t *) priv;

    if (!(port & 1))
        return dev->pm_idx;

    if (!dev->is_97317)
        return dev->pm[dev->pm_idx];

    switch (dev->pm_idx) {
        /* The multiplier clock is stable as soon as it is enabled. */
        case 0x03:
            return dev->pm[0x03] | ((dev->pm[0x03] & 0x04) ? 0x80 : 0x00);
        case 0x00 ... 0x02:
        case 0x04 ... 0x10:
            return dev->pm[dev->pm_idx];
        default:
            return 0x00;
    }
}

static void
pc87307_pm_remove(pc87307_t *dev)
{
    if (dev->pm_base != 0xffff) {
        io_removehandler(dev->pm_base, 0x0002,
                         pc87307_pm_read, NULL, NULL, pc87307_pm_write, NULL, NULL, dev);
        dev->pm_base = 0xffff;
    }
}

static void
pc87307_pm_init(pc87307_t *dev, uint16_t addr)
{
    dev->pm_base = addr;

    io_sethandler(dev->pm_base, 0x0002,
                  pc87307_pm_read, NULL, NULL, pc87307_pm_write, NULL, NULL, dev);
}

static void
kbc_handler(pc87307_t *dev)
{
    uint8_t  active   = (dev->ld_regs[LD_KBD][0x00] & 0x01) &&
                        (dev->pm[0x00] & 0x01);
    uint8_t  active_2 = dev->ld_regs[LD_MOUSE][0x00] & 0x01;
    uint8_t  irq      = (dev->ld_regs[LD_KBD][0x40] & 0x0f);
    uint8_t  irq_2    = (dev->ld_regs[LD_MOUSE][0x40] & 0x0f);
    uint16_t addr     = (dev->ld_regs[LD_KBD][0x30] << 8) |
                        dev->ld_regs[LD_KBD][0x31];
    uint16_t addr_2   = (dev->ld_regs[LD_KBD][0x32] << 8) |
                        dev->ld_regs[LD_KBD][0x33];

    pc87307_log("%02X, %02X, %02X, %02X, %04X, %04X\n",
                active, active_2, irq, irq_2, addr, addr_2);

    if (addr <= 0xfff8) {
        pc87307_log("Enabling KBC #1 on %04X...\n", addr);
        kbc_at_port_handler(0, active, addr,   dev->kbc);
    }

    if (addr_2 <= 0xfff8) {
        pc87307_log("Enabling KBC #2 on %04X...\n", addr_2);
        kbc_at_port_handler(1, active, addr_2, dev->kbc);
    }

    kbc_at_set_irq(0, active               ? irq   : 0xffff, dev->kbc);
    kbc_at_set_irq(1, (active && active_2) ? irq_2 : 0xffff, dev->kbc);
}

static void
fdc_handler(pc87307_t *dev)
{
    fdc_remove(dev->fdc);

    uint8_t  active = (dev->ld_regs[LD_FDC][0x00] & 0x01) &&
                      (dev->pm[0x00] & 0x08);
    uint8_t  irq    = (dev->ld_regs[LD_FDC][0x40] & 0x0f);
    uint8_t  dma    = (dev->ld_regs[LD_FDC][0x44] & 0x0f);
    uint16_t addr   = ((dev->ld_regs[LD_FDC][0x30] << 8) |
                      dev->ld_regs[LD_FDC][0x31]) & 0xfff8;

    if (active && (addr <= 0xfff8)) {
        pc87307_log("Enabling FDC on %04X, IRQ %i...\n", addr, irq);
        fdc_set_base(dev->fdc, addr);
        fdc_set_irq(dev->fdc, irq);
        fdc_set_dma_ch(dev->fdc, dma);
    }
}

static void
lpt_handler(pc87307_t *dev)
{
    uint8_t  active = (dev->ld_regs[LD_LPT][0x00] & 0x01) &&
                      (dev->pm[0x00] & 0x10);
    uint8_t  irq    = (dev->ld_regs[LD_LPT][0x40] & 0x0f);
    uint8_t  dma    = (dev->ld_regs[LD_LPT][0x44] & 0x0f);
    uint16_t addr   = (dev->ld_regs[LD_LPT][0x30] << 8) |
                      dev->ld_regs[LD_LPT][0x31];
    uint8_t  mode   = (dev->ld_regs[LD_LPT][0xf0] >> 7);
    uint16_t mask   = 0xfffc;

    if (irq > 15)
        irq = 0xff;

    if (dma >= 4)
        dma = 0xff;

    lpt_port_remove(dev->lpt);

    switch (mode) {
        default:
        case 0x00:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x01:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 1);
            break;
        case 0x02: case 0x03:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x04:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x07:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
    }

    lpt_set_cfg_regs_enabled(dev->lpt, !!(dev->ld_regs[LD_LPT][0xf0] & 0x10));

    if (active && (addr <= (0xfffc & mask))) {
        pc87307_log("Enabling LPT1 on %04X...\n", addr);
        lpt_port_setup(dev->lpt, addr & mask);
    } else
        lpt_port_setup(dev->lpt, 0xffff);

    lpt_port_irq(dev->lpt, irq);
    lpt_port_dma(dev->lpt, dma);
}

static void
serial_handler(pc87307_t *dev, int uart)
{
    serial_remove(dev->uart[uart]);

    uint8_t  active = (dev->ld_regs[LD_UART1 - uart][0x00] & 0x01) &&
                      (dev->pm[0x00] & (1 << (6 - uart)));
    uint8_t  irq    = (dev->ld_regs[LD_UART1 - uart][0x40] & 0x0f);
    uint16_t addr   = (dev->ld_regs[LD_UART1 - uart][0x30] << 8) |
                      dev->ld_regs[LD_UART1 - uart][0x31];

    if (active && (addr <= 0xfff8)) {
        pc87307_log("Enabling COM%i on %04X...\n", uart + 1, addr);
        serial_setup(dev->uart[uart], addr, irq);
    } else
        serial_setup(dev->uart[uart], 0x0000, irq);
}

static void
gpio_handler(pc87307_t *dev)
{
    pc87307_gpio_remove(dev);

    /* The PC97317's FER2 bit 7 gates the GPIO ports (10.2.4). */
    uint8_t  active = (dev->ld_regs[LD_GPIO][0x00] & 0x01) &&
                      (!dev->is_97317 || (dev->pm[0x01] & FER2_GPIO));
    uint16_t addr   = (dev->ld_regs[LD_GPIO][0x30] << 8) |
                      dev->ld_regs[LD_GPIO][0x31];
    uint16_t addr_2 = (dev->ld_regs[LD_GPIO][0x32] << 8) |
                      dev->ld_regs[LD_GPIO][0x33];

    if (active) {
        pc87307_log("Enabling GPIO #1 on %04X...\n", addr);
        pc87307_gpio_init(dev, 0, addr);
        pc87307_log("Enabling GPIO #2 on %04X...\n", addr_2);
        pc87307_gpio_init(dev, 1, addr_2);
    }
}

/* The RTC (logical device 2), when the chip owns the NVR: base address,
   IRQ, and Activate gated by FER1 bit 2. The APC keeps running either
   way. */
static void
rtc_handler(pc87307_t *dev)
{
    uint8_t  active;
    uint16_t addr;

    if (dev->nvr == NULL)
        return;

    active = (dev->ld_regs[LD_RTC][0x00] & 0x01) && (dev->pm[0x00] & 0x04);
    addr   = ((dev->ld_regs[LD_RTC][0x30] << 8) | dev->ld_regs[LD_RTC][0x31]) & 0xfffe;

    if (dev->rtc_base != 0x0000)
        nvr_at_handler(0, dev->rtc_base, dev->nvr);
    dev->rtc_base = 0x0000;

    nvr_irq_set(dev->ld_regs[LD_RTC][0x40] & 0x0f, dev->nvr);

    if (active && (addr != 0x0000)) {
        pc87307_log("Enabling RTC on %04X...\n", addr);
        dev->rtc_base = addr;
        nvr_at_handler(1, dev->rtc_base, dev->nvr);
    }
}

/* RAM Lock: 38h-3Fh read and write blocked, and RAM Mask Write blocks the
   writes to bank 0's RAM (0Eh-7Fh); only a reset clears them (PC87307
   4.5.4, PC97317 4.5.8). */
static void
pc87307_rlr_apply(pc87307_t *dev)
{
    const uint8_t rlr = dev->apc[APC_RLR];

    if (dev->nvr == NULL)
        return;

    for (int i = 0x0e; i < 0x80; i++) {
        int lock = (rlr & RLR_MASK_WRITE) ? 0x01 : 0x00;

        if ((rlr & RLR_LOCK) && (i >= 0x38) && (i <= 0x3f))
            lock = 0x03;

        nvr_lock_set(i, 1, lock, dev->nvr);
    }
}

/* Registers 40h-7Fh of RTC bank 1 (the century, the upper RAM ports, and
   the PC97317's date and month alarms) and bank 2 (the APC). The reserved
   ones read 00h and ignore writes. */
static uint8_t
pc87307_rtc_bank_read(int bank, uint8_t reg, void *priv)
{
    pc87307_t *dev  = (pc87307_t *) priv;
    uint8_t    idx  = (reg & 0x7f) - 0x40;
    uint8_t    addr = dev->rtc_bank1[RTC1_URAM_ADDR] & 0x7f;
    uint8_t    ret  = 0x00;

    if (bank == 1) {
        switch (idx) {
            case RTC1_DOM_ALARM:
            case RTC1_MON_ALARM:
                if (dev->is_97317)
                    ret = dev->rtc_bank1[idx];
                break;
            case RTC1_CENTURY:
            case RTC1_URAM_ADDR:
                ret = dev->rtc_bank1[idx];
                break;
            case RTC1_URAM_DATA:
                if ((dev->apc[APC_RLR] & RLR_UPPER) ||
                    ((dev->apc[APC_RLR] & RLR_READ_LOW) && (addr < 0x20)))
                    ret = 0xff;
                else
                    ret = dev->upper_ram[addr];
                break;
            default:
                break;
        }
    } else if (!dev->is_97317 && (idx > APC_WC)) {
        /* The PC87307's bank 2 ends at the Wake Up Century (Table 4-9). */
        ret = 0x00;
    } else switch (idx) {
        /* The command bits read back 0. */
        case APC_APCR1:
            ret = dev->apc[idx] & 0x96;
            break;
        case APC_APCR2:
            ret = dev->apc[idx] & 0x7f;
            break;
        /* Bits 5-0 clear on read. */
        case APC_APSR:
            ret = dev->apc[idx];
            dev->apc[idx] &= 0xc0;
            break;
        /* SWITCH is high (not pressed); the Switch On detect clears on read. */
        case APC_APSR1:
            ret = 0x01 | (dev->apc[idx] & 0x02);
            dev->apc[idx] &= ~0x02;
            break;
        case APC_WDW ... APC_RLR:
        case APC_WC ... APC_APCR7:
        case APC_DADDR ... APC_CADDR:
            ret = dev->apc[idx];
            break;
        default:
            break;
    }

    pc87307_log("RTC bank %i, %02X read  = %02X\n", bank, reg, ret);

    return ret;
}

static void
pc87307_rtc_bank_write(int bank, uint8_t reg, uint8_t val, void *priv)
{
    pc87307_t *dev  = (pc87307_t *) priv;
    uint8_t    idx  = (reg & 0x7f) - 0x40;
    uint8_t    addr = dev->rtc_bank1[RTC1_URAM_ADDR] & 0x7f;

    pc87307_log("RTC bank %i, %02X write = %02X\n", bank, reg, val);

    if (bank == 1) {
        switch (idx) {
            case RTC1_DOM_ALARM:
            case RTC1_MON_ALARM:
                if (dev->is_97317)
                    dev->rtc_bank1[idx] = val;
                break;
            case RTC1_CENTURY:
                dev->rtc_bank1[idx] = val;
                break;
            case RTC1_URAM_ADDR:
                dev->rtc_bank1[idx] = val & 0x7f;
                break;
            case RTC1_URAM_DATA:
                if (!(dev->apc[APC_RLR] & (RLR_UPPER | RLR_MASK_WRITE)) &&
                    !((dev->apc[APC_RLR] & RLR_WRITE_LOW) && (addr < 0x20)))
                    dev->upper_ram[addr] = val;
                break;
            default:
                break;
        }

        nvr_dosave = 1;
        return;
    }

    /* The PC87307's bank 2 ends at the Wake Up Century (Table 4-9). */
    if (!dev->is_97317 && (idx > APC_WC))
        return;

    switch (idx) {
        /* Bit 0 is reserved on the PC87307. */
        case APC_APCR1:
            dev->apc[idx] = ((dev->apc[idx] & ~val) & APCR1_PF) | (val & 0x16);
            if (val & APCR1_FS_RESET)
                timer_stop(&dev->failsafe_timer);
            else if ((val & APCR1_FS_TRIG) && dev->is_97317)
                pc87307_failsafe_start(dev);
            if (val & APCR1_SOC)
                pc87307_power_off(dev);
            break;
        /* Software On Command: ONCTL is already active. */
        case APC_APCR2:
            dev->apc[idx] = val & 0x7f;
            break;
        case APC_RLR:
            dev->apc[idx] |= val & 0xf8;
            pc87307_rlr_apply(dev);
            break;
        case APC_WDW ... APC_WY:
        case APC_WC ... APC_APCR7:
        case APC_DADDR ... APC_CADDR:
            dev->apc[idx] = val;
            break;
        default:
            break;
    }

    nvr_dosave = 1;
}

/* A relocatable RTC register (DADDR/MADDR/CADDR): bit 7 picks bank 1. */
static uint8_t
pc87307_rtc_reloc(const pc87307_t *dev, uint8_t addr)
{
    uint8_t reg = addr & 0x7f;

    if ((addr & 0x80) && (reg >= 0x40))
        return dev->rtc_bank1[reg - 0x40];

    return dev->nvr->regs[reg];
}

static int
pc87307_rtc_match(uint8_t alarm, uint8_t cur)
{
    return ((alarm & 0xc0) == 0xc0) || (alarm == cur);
}

/* The time alarm matched: on the PC97317 the date of month and month
   alarms decide the alarm (RTC_STS, as well as AF), while the PC87307 has
   only the time alarm; the wake-up date sets Timer Match Detect (4.4,
   "Predetermined Wake-Up"), against the century in bank 1, which the
   PC97317 can relocate. */
static int
pc87307_rtc_alarm(void *priv)
{
    pc87307_t     *dev  = (pc87307_t *) priv;
    const uint8_t *regs = dev->nvr->regs;
    uint8_t        cent = dev->rtc_bank1[RTC1_CENTURY];
    int            ret  = 1;

    if (dev->is_97317) {
        ret  = pc87307_rtc_match(pc87307_rtc_reloc(dev, dev->apc[APC_DADDR]), regs[0x07]) &&
               pc87307_rtc_match(pc87307_rtc_reloc(dev, dev->apc[APC_MADDR]), regs[0x08]);
        cent = pc87307_rtc_reloc(dev, dev->apc[APC_CADDR]);
    }

    if (pc87307_rtc_match(dev->apc[APC_WDW], regs[0x06]) &&
        pc87307_rtc_match(dev->apc[APC_WDM], regs[0x07]) &&
        pc87307_rtc_match(dev->apc[APC_WM], regs[0x08]) &&
        pc87307_rtc_match(dev->apc[APC_WY], regs[0x09]) &&
        pc87307_rtc_match(dev->apc[APC_WC], cent))
        dev->apc[APC_APSR] |= APSR_TMD;

    if (ret && dev->is_97317) {
        dev->acpi[ACPI_STS_HI] |= RTC_STS;
        pc97317_pm_event(dev);
    }

    return ret;
}

/* The values on the first power-up (PC87307 Tables 4-8 and 4-9; PC97317
   Tables 4-21 and 4-22, 4.6, 4.7). The PC87307's APC registers start at
   0, but for the status. */
static void
pc87307_nvr_defaults(pc87307_t *dev)
{
    memset(dev->rtc_bank1, 0x00, sizeof(dev->rtc_bank1));
    memset(dev->apc, 0x00, sizeof(dev->apc));
    memset(dev->upper_ram, 0x00, sizeof(dev->upper_ram));
    memset(dev->acpi, 0x00, sizeof(dev->acpi));

    dev->apc[APC_APSR] = 0x41;

    if (!dev->is_97317)
        return;

    dev->rtc_bank1[RTC1_DOM_ALARM] = 0xc0;
    dev->rtc_bank1[RTC1_MON_ALARM] = 0xc0;

    dev->apc[APC_WDW]   = 0xc0;
    dev->apc[APC_WDM]   = 0xc0;
    dev->apc[APC_WM]    = 0xc0;
    dev->apc[APC_WY]    = 0xc0;
    dev->apc[APC_WC]    = 0xc0;
    dev->apc[APC_APCR3] = 0xa0;
    dev->apc[APC_APCR4] = 0x2d;
    dev->apc[APC_APCR5] = 0x2d;
    dev->apc[APC_APCR6] = 0x2d;
    dev->apc[APC_APCR7] = 0x05;
    dev->apc[APC_DADDR] = 0xc9;
    dev->apc[APC_MADDR] = 0xca;
    dev->apc[APC_CADDR] = 0xc8;
}

/* The banks, the APC and the ACPI fixed registers share the CMOS battery:
   they are kept in <machine>_apc.nvr and start over with the CMOS. */
static pc87307_t *pc87307_nvr_dev = NULL;

static void
pc87307_nvr_save(void)
{
    pc87307_t *dev = pc87307_nvr_dev;
    FILE      *fp;

    if ((dev == NULL) || (dev->apc_fn == NULL))
        return;

    fp = nvr_fopen(dev->apc_fn, "wb");
    if (fp != NULL) {
        (void) fwrite(dev->rtc_bank1, 1, sizeof(dev->rtc_bank1), fp);
        (void) fwrite(dev->apc, 1, sizeof(dev->apc), fp);
        (void) fwrite(dev->upper_ram, 1, sizeof(dev->upper_ram), fp);
        (void) fwrite(dev->acpi, 1, sizeof(dev->acpi), fp);
        fclose(fp);
    }
}

static void
pc87307_nvr_load(pc87307_t *dev)
{
    uint8_t buf[APC_NVR_SIZE];
    FILE   *fp = NULL;

    dev->apc_fn = (char *) calloc(1, strlen(machine_get_nvr_name()) + 9 + 1);
    sprintf(dev->apc_fn, "%s_apc.nvr", machine_get_nvr_name());

    pc87307_nvr_defaults(dev);

    if (!dev->nvr->is_new)
        fp = nvr_fopen(dev->apc_fn, "rb");

    if (fp != NULL) {
        if (fread(buf, 1, sizeof(buf), fp) == sizeof(buf)) {
            memcpy(dev->rtc_bank1, buf, 64);
            memcpy(dev->apc, buf + 64, 64);
            memcpy(dev->upper_ram, buf + 128, 128);
            memcpy(dev->acpi, buf + 256, 16);
        }
        fclose(fp);
    }

    pc87307_nvr_dev = dev;
    nvr_set_ven_save(pc87307_nvr_save);
}

static void
pm_handler(pc87307_t *dev)
{
    pc87307_pm_remove(dev);

    uint8_t  active = (dev->ld_regs[LD_PM][0x00] & 0x01);
    uint16_t addr   = (dev->ld_regs[LD_PM][0x30] << 8) |
                      dev->ld_regs[LD_PM][0x31];

    if (active) {
        pc87307_log("Enabling power management on %04X...\n", addr);
        pc87307_pm_init(dev, addr);
    }
}

static void
superio_handler(pc87307_t *dev)
{
    if (dev->superio_base != 0x0000)
        io_removehandler(dev->superio_base, 0x0002,
                         pc87307_read, NULL, NULL,
                         pc87307_write, NULL, NULL, dev);

    switch (dev->regs[0x22] & 0x03) {
        default:
            dev->superio_base = 0x0000;
            break;        case 0x02:
            dev->superio_base = 0x015c;
            break;
        case 0x03:
            dev->superio_base = 0x002e;
            break;
    }

    if (dev->superio_base != 0x0000) {
        pc87307_log("Enabling Super I/O on %04X...\n", dev->superio_base);
        io_sethandler(dev->superio_base, 0x0002,
                      pc87307_read, NULL, NULL,
                      pc87307_write, NULL, NULL, dev);
    }
}

static void
pc87307_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev   = (pc87307_t *) priv;
    uint8_t    ld    = dev->regs[0x07];
    uint8_t    reg   = dev->cur_reg - 0x30;
    uint8_t    index = (port & 1) ? 0 : 1;
    uint8_t    old   = dev->regs[dev->cur_reg];

    if (index) {
        dev->cur_reg = val;
        return;
    } else {
#ifdef ENABLE_PC87307_LOG
        if (dev->cur_reg >= 0x30)
            pc87307_log("[%04X:%08X] [W] (%04X) %02X:%02X = %02X\n",
                        CS, cpu_state.pc, port, ld, dev->cur_reg, val);
        else
            pc87307_log("[%04X:%08X] [W] (%04X)    %02X = %02X\n",
                        CS, cpu_state.pc, port,     dev->cur_reg, val);
#endif
        switch (dev->cur_reg) {
            case 0x00:
            case 0x02: case 0x03:
            case 0x06: case 0x07:
                dev->regs[dev->cur_reg] = val;
                break;
            case 0x21:
                /* PC97317 SIOC1: bit 4 is the XDB strap; Lock Scratch Bit
                   (5) sticks and makes scratch bits 7-6 read-only. */
                if (dev->is_97317) {
                    val = (val & 0xef) | (old & 0x30);
                    if (old & 0x20)
                        val = (val & 0x3f) | (old & 0xc0);
                }
                dev->regs[dev->cur_reg] = val;
                fdc_toggle_flag(dev->fdc, FDC_FLAG_PS2_MCA, !(val & 0x04));
                break;
            case 0x22:
                dev->regs[dev->cur_reg] = val;
                superio_handler(dev);
                break;
            case 0x23:
                dev->regs[dev->cur_reg] = (old & 0xf0) | (val & 0x0f);
                break;
            case 0x24:
                dev->pcregs[dev->regs[0x23]] = val;
                break;
            /* PC97317 SIOC3: P16/GPIO25, P12/CS0, the SCI polarity and IRQ. */
            case 0x25:
                if (dev->is_97317) {
                    dev->regs[dev->cur_reg] = val & 0xfb;
                    pc97317_sci_update(dev);
                }
                break;
            default:
                if (dev->cur_reg >= 0x30)
                    old = dev->ld_regs[ld][reg];
                break;
        }
    }

    switch (dev->cur_reg) {
        case 0x30:
            switch (ld) {
                default:
                    break;
                case LD_KBD: case LD_MOUSE:
                    dev->ld_regs[ld][reg] = val;
                    kbc_handler(dev);
                    break;
                case LD_RTC:
                    dev->ld_regs[ld][reg] = val;
                    rtc_handler(dev);
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = val;
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 1);
                    break;
                case LD_UART1:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 0);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = val;
                    gpio_handler(dev);
                    break;
                case LD_PM:
                    dev->ld_regs[ld][reg] = val;
                    pm_handler(dev);
                    break;
            }
            break;
        /* I/O Range Check. */
        case 0x31:
            switch (ld) {
                default:
                    break;
                case LD_MIN ... LD_MAX:
                    if (ld != LD_MOUSE)
                        dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;
        /* Base Address 0 MSB. */
        case 0x60:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = val;
                    kbc_handler(dev);
                    break;
                case LD_RTC:
                    dev->ld_regs[ld][reg] = val;
                    rtc_handler(dev);
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = (old & 0xfc) | (val & 0x03);
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 1);
                    break;
                case LD_UART1:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 0);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = val;
                    gpio_handler(dev);
                    break;
                case LD_PM:
                    dev->ld_regs[ld][reg] = val;
                    pm_handler(dev);
                    break;
            }
            break;
        /* Base Address 0 LSB. */
        case 0x61:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = (old & 0x04) | (val & 0xfb);
                    kbc_handler(dev);
                    break;
                case LD_RTC:
                    dev->ld_regs[ld][reg] = (old & 0x01) | (val & 0xfe);
                    rtc_handler(dev);
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = (old & 0x07) | (val & 0xf8);
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = (old & 0x03) | (val & 0xfc);
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = (old & 0x07) | (val & 0xf8);
                    serial_handler(dev, 1);
                    break;
                case LD_UART1:
                    dev->ld_regs[ld][reg] = (old & 0x07) | (val & 0xf8);
                    serial_handler(dev, 0);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = (old & 0x07) | (val & 0xf8);
                    gpio_handler(dev);
                    break;
                case LD_PM:
                    dev->ld_regs[ld][reg] = (old & 0x01) | (val & 0xfe);
                    pm_handler(dev);
                    break;
            }
            break;
        /* Base Address 1 MSB (undocumented for Logical Device 7). */
        case 0x62:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = val;
                    kbc_handler(dev);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = val;
                    gpio_handler(dev);
                    break;
            }
            break;
        /* Base Address 1 LSB (undocumented for Logical Device 7). */
        case 0x63:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = (old & 0x04) | (val & 0xfb);
                    kbc_handler(dev);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = (old & 0x01) | (val & 0xfe);
                    gpio_handler(dev);
                    break;
            }
            break;
        /* Interrupt Select. */
        case 0x70:
            switch (ld) {
                default:
                    break;
                case LD_KBD: case LD_MOUSE:
                    dev->ld_regs[ld][reg] = val;
                    kbc_handler(dev);
                    break;
                case LD_RTC:
                    dev->ld_regs[ld][reg] = val;
                    rtc_handler(dev);
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = val;
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 1);
                    break;
                case LD_UART1:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 0);
                    break;
            }
            break;
        /* Interrupt Type. */
        case 0x71:
            switch (ld) {
                default:
                    break;
                case LD_MIN ... LD_MAX:
                    if ((ld == LD_KBD) || (ld == LD_MOUSE))
                        dev->ld_regs[ld][reg] = (old & 0xfc) | (val & 0x03);
                    else
                        dev->ld_regs[ld][reg] = (old & 0xfd) | (val & 0x02);
                    break;
            }
            break;
        /* DMA Channel Select 0. */
        case 0x74:
            switch (ld) {
                default:
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = val;
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;
        /* DMA Channel Select 1. */
        case 0x75:
            switch (ld) {
                default:
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;
        /* Configuration Register 0. */
        case 0xf0:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = val;
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_update_densel_polarity(dev->fdc, (val & 0x20) ? 1 : 0);
                    fdc_update_enh_mode(dev->fdc, (val & 0x40) ? 1 : 0);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = val;
                    lpt_handler(dev);
                    break;
                case LD_UART2: case LD_UART1:
                    dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;
        /* Configuration Register 1. */
        case 0xf1:
            switch (ld) {
                default:
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
pc87307_read(uint16_t port, void *priv)
{
    const pc87307_t *dev   = (pc87307_t *) priv;
    uint8_t          ld    = dev->regs[0x07];
    uint8_t          reg   = dev->cur_reg - 0x30;
    uint8_t          index = (port & 1) ? 0 : 1;
    uint8_t          ret   = 0xff;

    if (index)
        ret = dev->cur_reg;
    else {
        if (dev->cur_reg >= 0x30)
            ret = dev->ld_regs[ld][reg];
        else if (dev->cur_reg == 0x24)
            ret = dev->pcregs[dev->regs[0x23]];
        /* Write-only registers. */
        else if ((dev->cur_reg == 0x00) ||
                 (dev->cur_reg == 0x02) || (dev->cur_reg == 0x03))
            ret = 0x00;
        else
            ret = dev->regs[dev->cur_reg];
#ifdef EANBLE_PC87307_LOG
        if (dev->cur_reg >= 0x30)
            pc87307_log("[%04X:%08X] [R] (%04X) %02X:%02X = %02X\n",
                        CS, cpu_state.pc, port, ld, dev->cur_reg, ret);
        else
            pc87307_log("[%04X:%08X] [R] (%04X)    %02X = %02X\n",
                        CS, cpu_state.pc, port,     dev->cur_reg, ret);
#endif
    }

    return ret;
}

void
pc87307_reset(void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    memset(dev->regs, 0x00, 0x30);
    for (uint16_t i = 0; i < 256; i++)
        memset(dev->ld_regs[i], 0x00, 0xd0);
    memset(dev->pcregs, 0x00, 0x10);
    memset(dev->gpio, 0xff, 0x08);
    memset(dev->pm, 0x00, sizeof(dev->pm));

    dev->regs[0x20] = dev->id;
    dev->regs[0x21] = 0x04;
    dev->regs[0x22] = dev->baddr;

    dev->ld_regs[LD_KBD  ][0x00] = 0x01;
    dev->ld_regs[LD_KBD  ][0x31] = 0x60;
    dev->ld_regs[LD_KBD  ][0x33] = 0x64;
    dev->ld_regs[LD_KBD  ][0x40] = 0x01;
    dev->ld_regs[LD_KBD  ][0x41] = 0x02;
    dev->ld_regs[LD_KBD  ][0x44] = 0x04;
    dev->ld_regs[LD_KBD  ][0x45] = 0x04;
    dev->ld_regs[LD_KBD  ][0xc0] = 0x40;

    dev->ld_regs[LD_MOUSE][0x40] = 0x0c;
    dev->ld_regs[LD_MOUSE][0x41] = 0x02;
    dev->ld_regs[LD_MOUSE][0x44] = 0x04;
    dev->ld_regs[LD_MOUSE][0x45] = 0x04;

    dev->ld_regs[LD_RTC  ][0x00] = 0x01;
    dev->ld_regs[LD_RTC  ][0x31] = 0x70;
    dev->ld_regs[LD_RTC  ][0x40] = 0x08;
    dev->ld_regs[LD_RTC  ][0x44] = 0x04;
    dev->ld_regs[LD_RTC  ][0x45] = 0x04;

    dev->ld_regs[LD_FDC  ][0x01] = 0x01;
    dev->ld_regs[LD_FDC  ][0x30] = 0x03;
    dev->ld_regs[LD_FDC  ][0x31] = 0xf0;
    dev->ld_regs[LD_FDC  ][0x32] = 0x03;
    dev->ld_regs[LD_FDC  ][0x33] = 0xf7;
    dev->ld_regs[LD_FDC  ][0x40] = 0x06;
    dev->ld_regs[LD_FDC  ][0x41] = 0x03;
    dev->ld_regs[LD_FDC  ][0x44] = 0x02;
    dev->ld_regs[LD_FDC  ][0x45] = 0x04;
    dev->ld_regs[LD_FDC  ][0xc0] = 0x02;

    dev->ld_regs[LD_LPT  ][0x30] = 0x02;
    dev->ld_regs[LD_LPT  ][0x31] = 0x78;
    dev->ld_regs[LD_LPT  ][0x40] = 0x07;
    dev->ld_regs[LD_LPT  ][0x44] = 0x04;
    dev->ld_regs[LD_LPT  ][0x45] = 0x04;
    dev->ld_regs[LD_LPT  ][0xc0] = 0xf2;

    dev->ld_regs[LD_UART2][0x30] = 0x02;
    dev->ld_regs[LD_UART2][0x31] = 0xf8;
    dev->ld_regs[LD_UART2][0x40] = 0x03;
    dev->ld_regs[LD_UART2][0x41] = 0x03;
    dev->ld_regs[LD_UART2][0x44] = 0x04;
    dev->ld_regs[LD_UART2][0x45] = 0x04;
    dev->ld_regs[LD_UART2][0xc0] = 0x02;

    dev->ld_regs[LD_UART1][0x30] = 0x03;
    dev->ld_regs[LD_UART1][0x31] = 0xf8;
    dev->ld_regs[LD_UART1][0x40] = 0x04;
    dev->ld_regs[LD_UART1][0x41] = 0x03;
    dev->ld_regs[LD_UART1][0x44] = 0x04;
    dev->ld_regs[LD_UART1][0x45] = 0x04;
    dev->ld_regs[LD_UART1][0xc0] = 0x02;

    dev->ld_regs[LD_GPIO ][0x44] = 0x04;
    dev->ld_regs[LD_GPIO ][0x45] = 0x04;

    dev->ld_regs[LD_PM   ][0x44] = 0x04;
    dev->ld_regs[LD_PM   ][0x45] = 0x04;

    dev->gpio[0][0] = 0xff;
    dev->gpio[0][1] = 0x00;
    dev->gpio[0][2] = 0x00;
    dev->gpio[0][3] = 0xff;
    dev->gpio[0][4] = 0xff;
    dev->gpio[0][5] = 0x00;
    dev->gpio[0][6] = 0x00;
    dev->gpio[0][7] = 0xff;
    dev->gpio[1][1] = 0x00;

    dev->pm[0] = 0xff;
    dev->pm[1] = dev->is_97317 ? 0x87 : 0xff;
    dev->pm[4] = 0x0e;
    dev->pm[7] = 0x01;

    dev->gpio_base = dev->pm_base = 0xffff;

    /* Master Reset: the APC keeps its values, but for the RAM Lock. */
    if (dev->nvr != NULL) {
        dev->apc[APC_RLR] = 0x00;
        pc87307_rlr_apply(dev);
        timer_stop(&dev->failsafe_timer);
        rtc_handler(dev);
    }

    if (dev->is_97317) {
        /* Most of the ACPI fixed registers keep their values; the enables
           listed in 4.6/4.7 do not. */
        dev->acpi[ACPI_EN_LO]   = 0x00;
        dev->acpi[ACPI_EN_HI]  &= ~PWRBTN_STS;
        dev->acpi[ACPI_CNT_LO]  = 0x00;
        dev->acpi[ACPI_GP1_STS0] &= 0x0f;
        dev->acpi[ACPI_GP1_EN0]  &= 0x0f;
        dev->acpi[ACPI_GP2_EN0]   = 0x00;
        dev->sleeping = 0;

        for (int i = 0; i < 4; i++)
            pc97317_acpi_set_block(dev, i, 0x0000, 0);

        if (dev->sci_irq >= 0)
            picintclevel(1 << dev->sci_irq, &dev->sci_state);
        dev->sci_irq = -1;

        pc97317_acpi_handler(dev);
    }

    /*
        0 = 360 rpm @ 500 kbps for 3.5"
        1 = Default, 300 rpm @ 500, 300, 250, 1000 kbps for 3.5"
    */
    fdc_toggle_flag(dev->fdc, FDC_FLAG_PS2_MCA, 0);
    fdc_reset(dev->fdc);

    kbc_handler(dev);
    fdc_handler(dev);
    lpt_handler(dev);
    serial_handler(dev, 0);
    serial_handler(dev, 1);
    gpio_handler(dev);
    pm_handler(dev);
    superio_handler(dev);
}

static void
pc87307_close(void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    if (dev->is_97317)
        timer_disable(&dev->pm_tmr_timer);

    if (dev->nvr != NULL) {
        timer_disable(&dev->failsafe_timer);

        pc87307_nvr_save();
        pc87307_nvr_dev = NULL;

        free(dev->apc_fn);
    }

    free(dev);
}

static void *
pc87307_init(const device_t *info)
{
    pc87307_t *dev = (pc87307_t *) calloc(1, sizeof(pc87307_t));

    dev->id = info->local & 0xff;

    dev->fdc = device_add_params(&fdc_at_nsc_device, (void *) FDC_FLAG_PNP);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt     = device_add_inst(&lpt_port_device, 1);
    lpt_set_cnfga_readout(dev->lpt, 0x14);

    switch (info->local & PCX730X_KBC) {
        case PCX730X_AMI:
        default:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_AMI | 0x00003500));
            break;
        /* Optiplex! */
        case PCX730X_PHOENIX_42:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00013700));
            break;
        case PCX730X_PHOENIX_42I:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00041600));
            break;
    }

    /* SIOC2 BADDR1-0: 10b is 15Ch, 11b (and the unsupported 398h/26Eh
       straps) 2Eh. */
    if (((info->local & PCX730X_BADDR) >> PCX730X_BADDR_SHIFT) == 0x02)
        dev->baddr = 0x02;
    else
        dev->baddr = 0x03;

    /* The RTC is the board's NVR on the PC97317, and on PC87307/PC97307
       boards whose chipset has none (PCX7307_RTC): its banks 1 and 2 and
       the APC live here, as do the PC97317's ACPI fixed registers. */
    dev->is_97317 = (dev->id == (PCX7307_PC97317 & PCX730X_CHIP_ID));
    dev->sci_irq  = -1;
    if (dev->is_97317 || (info->local & PCX7307_RTC)) {
        dev->nvr      = device_add_params(&nvr_at_device,
                                          (void *) (uintptr_t) (dev->is_97317 ? NVR_PC873XX : NVR_PC87307));
        dev->rtc_base = 0x0070;
        nvr_at_set_pc873xx_banks(dev->nvr, pc87307_rtc_bank_read, pc87307_rtc_bank_write,
                                 pc87307_rtc_alarm, dev);
        pc87307_nvr_load(dev);

        timer_add(&dev->failsafe_timer, pc87307_failsafe_expire, dev, 0);
    }
    if (dev->is_97317)
        timer_add(&dev->pm_tmr_timer, pc97317_pm_tmr_carry, dev, 0);

    pc87307_reset(dev);

    return dev;
}

const device_t pc87307_device = {
    .name          = "National Semiconductor PC87307 Super I/O",
    .internal_name = "pc87307",
    .flags         = 0,
    .local         = 0x1c0,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87307_15c_device = {
    .name          = "National Semiconductor PC87307 Super I/O (Port 15Ch)",
    .internal_name = "pc87307_15c",
    .flags         = 0,
    .local         = 0x2c0,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87307_both_device = {
    .name          = "National Semiconductor PC87307 Super I/O (Ports 2Eh and 15Ch)",
    .internal_name = "pc87307_both",
    .flags         = 0,
    .local         = 0x3c0,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* The PC97317 drives the board's power (APC): it has the power button. */
const device_t pc97317_device = {
    .name          = "National Semiconductor PC97317 Super I/O",
    .internal_name = "pc97317",
    .flags         = 0,
    .local         = 0x1df,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL,
    .power_button  = pc97317_power_button
};

const device_t pc97307_device = {
    .name          = "National Semiconductor PC97307 Super I/O",
    .internal_name = "pc97307",
    .flags         = 0,
    .local         = 0x1cf,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
