/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the multifunction QDI Vision QD6500 VLB IDE controller.
 *
 * Authors: mw308
 *
 *          Copyright 2026 mw308
 *
 * The QD6500 is a single-channel VLB IDE controller. ATA task-file access
 * is provided by the common 86Box VLB IDE core; the QD6500-specific state is
 * exposed at either 30h-31h or B0h-B1h:
 *
 *   base+0  Timer 1 (read/write)
 *   base+1  Configuration (read-only)
 *
 * Configuration register bits documented by the historical Linux qd65xx
 * driver and confirmed against a QD6500A CARD V3.011/1993 are:
 *   bit 0   1 = IDE at 1F0h, 0 = IDE at 170h       (JP4)
 *   bit 1   1 = QD registers at B0h, 0 = at 30h   (JP3)
 *   bit 2   ID3 / bus-speed state
 *   bit 3   1 = controller disabled, 0 = enabled
 *   bits 7:4 = 1100b for QD6500
 *
 * ID3 is derived from the emulated VLB/front-side-bus clock rather than from
 * a user-visible speed option.  The tested V3.011 card on the AH4T reports
 * ID3=0 and Timer1=08h at its normal bus speed, matching the QDI notes.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/io.h>

#include "cpu.h"

typedef struct qd6500_t {
    uint16_t config_base;
    uint8_t  timer;
    uint8_t  config;
    uint8_t  timer_programmed;
    int      ide_board;
} qd6500_t;


static int
qd6500_timer_clocks(const qd6500_t *dev)
{
    const int active_field   = dev->timer & 0x07;
    const int recovery_field = (dev->timer >> 4) & 0x0f;

    /*
     * ID3=0 (>33 MHz): active = 8..1 clocks, recovery = 18..3 clocks
     * ID3=1 (<=33 MHz): active = 9..2 clocks, recovery = 15..0 clocks
     */
    if (dev->config & 0x04)
        return (9 - active_field) + (15 - recovery_field);

    return (8 - active_field) + (18 - recovery_field);
}

static void
qd6500_apply_pio_timing(qd6500_t *dev)
{
    double wait_ns = 0.0;

    /*
     * Driverless BIOS path, calibrated against a real V3.011 QD6500A with
     * a WDC AC35100L.
     */
    if (!dev->timer_programmed) {
        ide_set_pio_wait_ns(dev->ide_board, 314.0);
        ide_set_single_sector_gap_ns(dev->ide_board, 0.0);
        return;
    }

    /*
     * QD6500.SYS 2.4 programming points measured on the real card.  These
     * values represent controller data-port overhead, not absolute disk
     * speed caps, so slower drives can still remain the bottleneck.
     *
     *   08h  power-on/driverless -> 314 ns/word
     *   EAh  Auto                -> 250 ns/word
     *   18h  /S                  -> 184 ns/word
     *   38h  /N1                 -> 121 ns/word
     *   4Ah  /N2                 ->  54 ns/word
     *   7Ah  /F                  ->   0 ns/word
     *
     * The explicit profiles are empirical calibration points.  Unknown
     * Timer1 values still use the documented active/recovery clock decoder.
     */
    switch (dev->timer) {
        case 0x08:
            /* Some BIOSes rewrite the power-on value; keep driverless timing. */
            wait_ns = 314.0;
            break;
        case 0xea:
            wait_ns = 250.0;
            break;
        case 0x18:
            wait_ns = 184.0;
            break;
        case 0x38:
            wait_ns = 121.0;
            break;
        case 0x4a:
            wait_ns = 54.0;
            break;
        case 0x7a:
            wait_ns = 0.0;
            break;
        default: {
            const int clocks       = qd6500_timer_clocks(dev);
            const int extra_clocks = clocks - 18;

            if ((extra_clocks > 0) && (cpu_busspeed > 0.0))
                wait_ns = ((double) extra_clocks * 1000000000.0) / cpu_busspeed;
            break;
        }
    }

    ide_set_pio_wait_ns(dev->ide_board, wait_ns);

    /*
     * /O uses ordinary READ/WRITE SECTORS rather than READ/WRITE MULTIPLE.
     * The real QD6500A shows substantial additional per-sector controller
     * handshake/setup cost in this path.  The WD AC35100L /F /O measurement
     * calibrates this at about 97 us per sector.  Multiple-sector commands
     * bypass it.
     */
    ide_set_single_sector_gap_ns(dev->ide_board, 97000.0);
}

static void
qd6500_update_bus_speed(qd6500_t *dev, int reset_timer)
{
    /*
     * ID3 is a hardware status bit, not a user-configurable speed setting:
     *   0 = VLB clock > 33 MHz
     *   1 = VLB clock <= 33 MHz
     *
     * The QDI programming notes give the corresponding power-on Timer1
     * values as 08h and 0Ch respectively.  The AH4T's VLB clock follows
     * the CPU/front-side bus, which 86Box exposes as cpu_busspeed.
     */
    if (cpu_busspeed <= 33000000.0) {
        dev->config |= 0x04;
        if (reset_timer)
            dev->timer = 0x0c;
    } else {
        dev->config &= ~0x04;
        if (reset_timer)
            dev->timer = 0x08;
    }
}

static uint8_t
qd6500_read(uint16_t addr, void *priv)
{
    const qd6500_t *dev = (qd6500_t *) priv;

    switch (addr - dev->config_base) {
        case 0:
            return dev->timer;
        case 1:
            return dev->config;
        default:
            return 0xff;
    }
}

static void
qd6500_write(uint16_t addr, uint8_t val, void *priv)
{
    qd6500_t *dev = (qd6500_t *) priv;

    /*
     * Timer1 is writable except for bit 3, which is hard-wired/read-only 1.
     * The configuration register is entirely read-only.
     */
    if ((addr - dev->config_base) == 0) {
        dev->timer = val | 0x08;
        dev->timer_programmed = 1;
        qd6500_apply_pio_timing(dev);
    }
}


static void
qd6500_reset(void *priv)
{
    qd6500_t *dev = (qd6500_t *) priv;

    /* Hardware reset restores the speed-dependent Timer1 power-on value. */
    qd6500_update_bus_speed(dev, 1);
    dev->timer_programmed = 0;
    qd6500_apply_pio_timing(dev);
}

static void
qd6500_speed_changed(void *priv)
{
    qd6500_t *dev = (qd6500_t *) priv;

    /*
     * Keep the read-only ID3 status bit in sync with the emulated VLB/FSB
     * clock, but do not overwrite software-programmed Timer1 while running.
     */
    qd6500_update_bus_speed(dev, 0);
    qd6500_apply_pio_timing(dev);
}

static void
qd6500_close(void *priv)
{
    qd6500_t *dev = (qd6500_t *) priv;

    io_removehandler(dev->config_base, 2,
                     qd6500_read, NULL, NULL,
                     qd6500_write, NULL, NULL,
                     dev);

    free(dev);
}

static void *
qd6500_init(const device_t *info)
{
    qd6500_t *dev = (qd6500_t *) calloc(1, sizeof(qd6500_t));

    const int ide_primary = device_get_config_int("ide_channel") == 0;
    const int regs_b0     = device_get_config_int("config_base") == 0xb0;
    const int channel_irq = device_get_config_int("channel_irq");
    const int ide_board   = ide_primary ? 0 : 1;

    dev->config_base = regs_b0 ? 0x00b0 : 0x0030;
    dev->ide_board   = ide_board;

    /* QD6500 signature (Cxh), plus the hardware states currently modelled. */
    dev->config = 0xc0;
    if (ide_primary)
        dev->config |= 0x01;
    if (regs_b0)
        dev->config |= 0x02;

    /* Derive ID3 and the documented Timer1 power-on value from VLB/FSB speed. */
    qd6500_update_bus_speed(dev, 1);

    /* Set primary or secondary IDE. */
    if (ide_primary)
        device_add(&ide_vlb_device);
    else
        device_add(&ide_vlb_sec_device);

    /* JP15 is independent of JP4 on the real card. */
    ide_set_irq(ide_board, channel_irq);

    /* Start in the measured driverless/power-on timing state. */
    dev->timer_programmed = 0;
    qd6500_apply_pio_timing(dev);

    io_sethandler(dev->config_base, 2,
                  qd6500_read, NULL, NULL,
                  qd6500_write, NULL, NULL,
                  dev);

    return dev;
}

// clang-format off
static const device_config_t qd6500_config[] = {
    {
        .name           = "config_base",
        .description    = "Chipset Register Address (JP3)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0xb0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "30h", .value = 0x30 },
            { .description = "B0h", .value = 0xb0 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ide_channel",
        .description    = "IDE Channel (JP4)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Primary",   .value = 0 },
            { .description = "Secondary", .value = 1 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "channel_irq",
        .description    = "Channel IRQ (JP15)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 14,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 14", .value = 14 },
            { .description = "IRQ 15", .value = 15 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

/* Report the IDE channel selected by JP4 to the current board ownership planner.
 * This is evaluated with the instance's configuration context by ide_plan(). */
static uint32_t
qd6500_ide_boards(const device_t *info)
{
    (void) info;
    return (device_get_config_int("ide_channel") == 0) ? (1u << 0) : (1u << 1);
}

const device_t ide_qd6500_vlb_device = {
    .name          = "QDI QD6500A",
    .internal_name = "ide_qd6500_vlb",
    .flags         = DEVICE_VLB,
    .local         = 0,
    .init          = qd6500_init,
    .close         = qd6500_close,
    .reset         = qd6500_reset,
    .available     = NULL,
    .speed_changed = qd6500_speed_changed,
    .force_redraw  = NULL,
    .config        = qd6500_config,
    .ide_boards    = qd6500_ide_boards
};
