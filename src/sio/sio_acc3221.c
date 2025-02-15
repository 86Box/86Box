/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ACC 3221-SP Super I/O Chip.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2019 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>
#include <86box/plat_unused.h>

typedef struct acc3221_t {
    int       reg_idx;
    uint8_t   regs[256];
    fdc_t    *fdc;
    serial_t *uart[2];
} acc3221_t;

/* Configuration Register Index, BE (R/W):
    Bit     Function
    7       PIRQ 5 polarity.
                1 = active high, default
                0 = active low
    6       PIRQ 7 polarity.
                1 = active high, default
                0 = active low
    5       Primary Parallel Port Extended Mode
                0 = Compatible mode, default
                1 = Extended/Bidirectional mode.
    4       Primary Parallel Port Disable
                1 = Disable, 0 = Enable
                Power Up Default is set by pin 120
                (3221-DP)/pin 96 (3221-SP)
    3       Primary Parallel Port Power Down
                1 = Power Down, default = 0
    2**     Secondary Parallel Port Extended
                Mode
                0 = Compatible mode, default
                1 = Extended/Bidirectional mode.
    1**     Secondary Parallel Port Disable
                1 = Disable, 0 = Enable
                Power Up Default is set by pin 77
                (3221-DP)
    0**     Secondary Parallel Port Power Down
                1 = Power Down
                0 = Enable, default
    Note: Power Up not applicable to 3221-EP. */
#define REG_BE_LPT1_DISABLE (3 << 3)
#define REG_BE_LPT2_DISABLE (3 << 0) /* 3221-DP/EP only */

/* Configuration Register Index, BF (R/W):
    Bit     Function
    7-0     The 8 most significant address bits of
                the primary parallel port (A9-2)
                Default 9E (LPT2, at 278-27B) */

/* Configuration Register Index, DA (R/W)**:
    Bit     Function
    7-0     The 8 most significant address bits of
                the secondary parallel port (A9-2)
                Default DE (LPT1, at 378-37B) */

/* Configuration Register Index, DB (R/W):
    Bit     Function
    7       SIRQ4 polarity.
                1 = active high; default
                0 = active low
    6       SIRQ3 polarity.
                1 = active high; default
                0 = active low
    5       SXTAL clock off. 1 = SCLK off,
                0 = SCKL on, default
    4       Primary serial port disable
                1 = Disable, 0 = Enable
                Power Up default is set by pin 116
                (3221-DP)/pin 93 (3221-SP)
    3       Primary serial port power down
                1 = Power down, 0 = Enable
                Power Up default is set by pin 116
                (3221-DP)/pin 93 (3221-SP)
    2       Reserved
    1       Secondary serial port disable
                1 = Disable, 0 = Enable
                Power Up default is set by pin 121
                (3221-DP)/pin 97 (3221-SP)
    0       Secondary serial port power down
                1 = Power down, 0 = Enable
                Power Up default is set by pin 121
                (3221-DP)/pin 97 (3221-SP)
    Note: Power Up not applicable to 3221-EP. */
#define REG_DB_SERIAL1_DISABLE (3 << 3)
#define REG_DB_SERIAL2_DISABLE (3 << 0)

/* Configuration Register Index, DC (R/W):
    Bit     Function
    7-1     The MSB of the Primary Serial Port
                Address (bits A9-3).
                Default = 7F (COM1, at 3F8-3FF).
    0       When this bit is set to 1, bit A2 of
                primary parallel port is decoded.
                Default is 0. */

/* Configuration Register Index, DD (R/W):
    Bit     Function
    7-1     The MSB of the Secondary Serial Port
                Address (bits A9-3).
                Default = 5F (COM2, at 2F8-2FF).
    0**     When this bit is set to 1, bit A2 of
                secondary parallel port is decoded.
                Default is 0. */

/* Configuration Register Index, DE (R/W):
    Bit     Function
    7-6     SIRQ3 source
                b7  b6
                0   0   Disabled, tri-stated
                0   1   Disabled, tri-stated**
                1   0   Primary serial port
                1   1   Secondary serial port,
                                default
    5-4     SIRQ4 source
                b5  b4
                0   0   Disabled, tri-stated
                0   1   Disabled, tri-stated**
                1   0   Primary serial port,
                                default
                1   1   Secondary serial port

    3-2**   PIRQ7 source
                b3  b2
                0   0   Diabled, tri-stated,
                                default
                0   1   Primary serial port
                1   0   Primary parallel port
                1   1   Secondary parallel
                                port
    Note: Bits 3-2 are reserved in 3221-SP.

    1-0     PIRQ5 source
                b1  b0
                0   0   Disabled, tri-stated
                0   1   Secondary serial port
                1   0   Primary parallel port,
                                default
                1   1   Secondary parallel
                                port** */
#define REG_DE_SIRQ3_SOURCE  (3 << 6)
#define REG_DE_SIRQ3_SERIAL1 (1 << 6)
#define REG_DE_SIRQ3_SERIAL2 (3 << 6)
#define REG_DE_SIRQ4_SOURCE  (3 << 4)
#define REG_DE_SIRQ4_SERIAL1 (1 << 4)
#define REG_DE_SIRQ4_SERIAL2 (3 << 4)
#define REG_DE_PIRQ7_SOURCE  (3 << 2)
#define REG_DE_PIRQ7_SERIAL1 (1 << 2)
#define REG_DE_PIRQ7_LPT1    (2 << 2)
#define REG_DE_PIRQ7_LPT2    (3 << 2)
#define REG_DE_PIRQ5_SOURCE  (3 << 0)
#define REG_DE_PIRQ5_SERIAL2 (1 << 0)
#define REG_DE_PIRQ5_LPT1    (2 << 0)
#define REG_DE_PIRQ5_LPT2    (3 << 0)

/* Configuration Register Index, DF (R/W)**:
    Bit     Function
    7-6     Reserved
    5       RTC interface disable
                1 = /RTCCS disabled
                0 = /RTCCS enabled, default
    4       Disable Modem Select
                1 = Moden CS disabled, default
                0 = Modem CS enabled

    3-2
                b3  b2
                1   1   Reserved
                1   0   Modem port address
                                = 3E8-3EF (default)
                0   1   Modem port address:
                                2F8-2FF
                0   0   Modem port address:
                                3F8-3FF

    1-0
                b1  b0
                1   1   Reserved
                1   0   Mode 2, EISA Mode
                0   1   Mode 1, AT BUS,
                0   0   Mode 0, Two parallel
                                ports, default */

/* Configuration Register Index, FA (R/W)**:
    Bit     Function
    7       General purpose I/O register, Bit 7
    6       General purpose I/O register, Bit 6
    5       General purpose I/O register, Bit 5
    4       General purpose I/O register, Bit 4
    3       General purpose I/O register, Bit 3
    2       General purpose I/O register, Bit 2
    1       General purpose I/O register, Bit 1
    0       General purpose I/O register, Bit 0 */

/* Configuration Register Index, FB (R/W)**:
    Bit     Function
    7       Reserved
    6**     0/2 EXG (Read Only)
                In mode 1 and mode 2
                operation, when the third
                floppy drive is installed, pin
                EXTFDD should be pulled
                high to enable the third floppy
                drive or be pulled low to
                disable the third floppy drive.
                1 = Third floppy drive enabled
                0 = Third floppy drive disabled
    5**     EXTFDD (Read Only)
                In mode 1 and mode 2
                operation, when the third
                floppy drive is installed and
                pin 0/2 EXG is pulled high,
                the third floppy drive becomes
                the bootable drive (drive 0).
                When pi 0/2 EXG is pulled low,
                the third floppy drive acts as
                drive 2.
                1 = Third floppy drive as drive 0 (bootable)
                0 = Third floppy drive as drive 2
    4**     MS
                In mode 1 and mode 2, t his bit is to
                control the output pin MS to support a
                special 3 1/2", 1.2M drive. When this
                bit is set to high (1), the MS pin sends
                a low signal. When this bit is set to
                low (0), the MS pin sends a high
                signal to support a 3 1/2", 1.2M drive.
    3       FDC, Clock disable
                0 = enable, default
                1 = disable
    2       Reserved
    1       FDC disable
                0 = enable, 1= disable
                Power Upd efault set by pin 117 (3221-
                DP)/pin 94 (3221-SP)
    0       FDC address
                0 = Primary, default
                1 = Secondary
    Note: Bits 6-4 are reserved in 3221-SP. */
#define REG_FB_FDC_DISABLE (1 << 1)

/* Configuration Register Index, FB (R/W)**:
    Bit     Function
    7**     Disable general chip select 1
                1 = disable, default
                0 = enable
    6**     Disable general chip select 2
                1 = disable, default
                0 = enable
    5**     Enable SA2 decoding for general chip
                select 1
                1 = enable
                0 = disable, default
    4**     Enable SA2 decoding for general chip
                select 2
                1 = enable
                0 = disable, default
    3       Reserved
    2       IDE XT selected
                0 = IDE AT interface, default
                1 = IDE XT interface
    1       IDE disable, 1 = IDE disable
                0 = IDE enable
                Power Up default set by pin 13 (3221-
                DP)/pin 13 (3221-SP)
    0       Secondary IDE
                1 = secondary
                0 = primary, default
    Note: Bits 6-4 are reserved in 3221-SP. */
#define REG_FE_IDE_DISABLE (1 << 1)

static void
acc3221_lpt_handle(acc3221_t *dev)
{
    lpt1_remove();

    if (!(dev->regs[0xbe] & REG_BE_LPT1_DISABLE))
        lpt1_setup(dev->regs[0xbf] << 2);
}

static void
acc3221_serial1_handler(acc3221_t *dev)
{
    uint16_t com_addr = 0;

    serial_remove(dev->uart[0]);

    if (!(dev->regs[0xdb] & REG_DB_SERIAL1_DISABLE)) {
        com_addr = ((dev->regs[0xdc] & 0xfe) << 2);

        if ((dev->regs[0xde] & REG_DE_SIRQ3_SOURCE) == REG_DE_SIRQ3_SERIAL1)
            serial_setup(dev->uart[0], com_addr, 3);
        else if ((dev->regs[0xde] & REG_DE_SIRQ4_SOURCE) == REG_DE_SIRQ4_SERIAL1)
            serial_setup(dev->uart[0], com_addr, 4);
    }
}

static void
acc3221_serial2_handler(acc3221_t *dev)
{
    uint16_t com_addr = 0;

    serial_remove(dev->uart[1]);

    if (!(dev->regs[0xdb] & REG_DB_SERIAL2_DISABLE)) {
        com_addr = ((dev->regs[0xdd] & 0xfe) << 2);

        if ((dev->regs[0xde] & REG_DE_SIRQ3_SOURCE) == REG_DE_SIRQ3_SERIAL2)
            serial_setup(dev->uart[1], com_addr, 3);
        else if ((dev->regs[0xde] & REG_DE_SIRQ4_SOURCE) == REG_DE_SIRQ4_SERIAL2)
            serial_setup(dev->uart[1], com_addr, 4);
        else if ((dev->regs[0xde] & REG_DE_PIRQ5_SOURCE) == REG_DE_PIRQ5_SERIAL2)
            serial_setup(dev->uart[1], com_addr, 5);
    }
}

static void
acc3221_write(uint16_t addr, uint8_t val, void *priv)
{
    acc3221_t *dev = (acc3221_t *) priv;
    uint8_t    old;

    if (!(addr & 1))
        dev->reg_idx = val;
    else {
        old                     = dev->regs[dev->reg_idx];
        dev->regs[dev->reg_idx] = val;

        switch (dev->reg_idx) {
            case 0xbe:
                if ((old ^ val) & REG_BE_LPT1_DISABLE)
                    acc3221_lpt_handle(dev);
                break;

            case 0xbf:
                if (old != val)
                    acc3221_lpt_handle(dev);
                break;

            case 0xdb:
                if ((old ^ val) & REG_DB_SERIAL2_DISABLE)
                    acc3221_serial2_handler(dev);
                if ((old ^ val) & REG_DB_SERIAL1_DISABLE)
                    acc3221_serial1_handler(dev);
                break;

            case 0xdc:
                if (old != val)
                    acc3221_serial1_handler(dev);
                break;

            case 0xdd:
                if (old != val)
                    acc3221_serial2_handler(dev);
                break;

            case 0xde:
                if ((old ^ val) & (REG_DE_SIRQ3_SOURCE | REG_DE_SIRQ4_SOURCE)) {
                    acc3221_serial2_handler(dev);
                    acc3221_serial1_handler(dev);
                }
                break;

            case 0xfb:
                if ((old ^ val) & REG_FB_FDC_DISABLE) {
                    fdc_remove(dev->fdc);
                    if (!(dev->regs[0xfb] & REG_FB_FDC_DISABLE))
                        fdc_set_base(dev->fdc, FDC_PRIMARY_ADDR);
                }
                break;

            case 0xfe:
                if ((old ^ val) & REG_FE_IDE_DISABLE) {
                    ide_pri_disable();
                    if (!(dev->regs[0xfe] & REG_FE_IDE_DISABLE))
                        ide_pri_enable();
                }
                break;

            default:
                break;
        }
    }
}

static uint8_t
acc3221_read(uint16_t addr, void *priv)
{
    const acc3221_t *dev = (acc3221_t *) priv;

    if (!(addr & 1))
        return dev->reg_idx;

    if (dev->reg_idx < 0xbc)
        return 0xff;

    return dev->regs[dev->reg_idx];
}

static void
acc3221_reset(acc3221_t *dev)
{
    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    lpt1_remove();
    lpt1_setup(LPT1_ADDR);
    lpt1_irq(LPT1_IRQ);

    fdc_reset(dev->fdc);
}

static void
acc3221_close(void *priv)
{
    acc3221_t *dev = (acc3221_t *) priv;

    free(dev);
}

static void *
acc3221_init(UNUSED(const device_t *info))
{
    acc3221_t *dev = (acc3221_t *) calloc(1, sizeof(acc3221_t));

    dev->fdc = device_add(&fdc_at_device);

    dev->uart[0] = device_add_inst(&ns16450_device, 1);
    dev->uart[1] = device_add_inst(&ns16450_device, 2);

    io_sethandler(0x00f2, 0x0002, acc3221_read, NULL, NULL, acc3221_write, NULL, NULL, dev);

    acc3221_reset(dev);

    return dev;
}

const device_t acc3221_device = {
    .name          = "ACC 3221-SP Super I/O",
    .internal_name = "acc3221",
    .flags         = 0,
    .local         = 0,
    .init          = acc3221_init,
    .close         = acc3221_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
