/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PC/AT keyboard controller backed by a real UPI-42 (8042)
 *          firmware image. M1: host-side handshake validation only, the
 *          keyboard serial bus and the P2 control lines are not wired yet.
 *
 * Authors: WNT50, <wnt2195@gmail.com>
 *
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/upi42.h>

#define KBC_AT_UPI42_ROM_SIZE  2048
#define KBC_AT_UPI42_TICK_US   50 /* 20 kHz */
#define KBC_AT_UPI42_CYCLES    40 /* 12 MHz / 15 = 800 kHz, 50 us = 40 cycles */

typedef struct {
    upi42_t   *mcu;
    pc_timer_t tick;
    uint8_t   *rom;
    uint8_t    p1;
    uint8_t    p2;
    int        irq;
} kbc_upi42_t;

#ifdef ENABLE_KBC_AT_UPI42_LOG
int kbc_at_upi42_do_log = ENABLE_KBC_AT_UPI42_LOG;

static void
kbc_at_upi42_log(const char *fmt, ...)
{
    va_list ap;

    if (kbc_at_upi42_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define kbc_at_upi42_log(fmt, ...)
#endif

static FILE *
kbc_upi42_rom_open(const char *rom_path)
{
    if (rom_path == NULL)
        return NULL;

    return rom_fopen(rom_path, "rb");
}

int
kbc_at_upi42_available(const char *rom_path)
{
    FILE *fp = kbc_upi42_rom_open(rom_path);

    if (fp == NULL)
        return 0;

    fclose(fp);

    return 1;
}

static uint8_t
kbc_upi42_load_rom(uint8_t *rom, const char *rom_path)
{
    FILE *fp = kbc_upi42_rom_open(rom_path);

    memset(rom, 0x00, KBC_AT_UPI42_ROM_SIZE);

    if (fp == NULL) {
        kbc_at_upi42_log("KBC-UPI42: firmware image not found\n");
        return 0;
    }

    if (fread(rom, 1, KBC_AT_UPI42_ROM_SIZE, fp) != KBC_AT_UPI42_ROM_SIZE)
        kbc_at_upi42_log("KBC-UPI42: firmware image is shorter than %i bytes\n", KBC_AT_UPI42_ROM_SIZE);

    fclose(fp);

    kbc_at_upi42_log("KBC-UPI42: loaded firmware from %s\n", rom_path);

    return 1;
}

static int
kbc_upi42_t0_in(void *priv)
{
    (void) priv;

    return 1;
}

static int
kbc_upi42_t1_in(void *priv)
{
    (void) priv;

    return 1;
}

static uint8_t
kbc_upi42_p1_in(void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    return dev->p1;
}

static uint8_t
kbc_upi42_p2_in(void *priv)
{
    (void) priv;

    return 0xff;
}

static void
kbc_upi42_port_out(void *priv, int port, uint8_t val)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    switch (port) {
        case 1:
            dev->p1 = val;
            break;

        case 2:
            dev->p2 = val;
            break;

        default:
            break;
    }

    kbc_at_upi42_log("KBC-UPI42: port %i output %02X\n", port, val);
}

static void
kbc_upi42_irq_out(void *priv, int level)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    dev->irq = level;

    /* M1: not wired to the PIC yet. */
    kbc_at_upi42_log("KBC-UPI42: INT %i\n", level);
}

static const upi42_hooks kbc_upi42_hooks = {
    .t0_in    = kbc_upi42_t0_in,
    .t1_in    = kbc_upi42_t1_in,
    .p1_in    = kbc_upi42_p1_in,
    .p2_in    = kbc_upi42_p2_in,
    .port_out = kbc_upi42_port_out,
    .irq_out  = kbc_upi42_irq_out,
};

static uint8_t
kbc_upi42_data_r(uint16_t port, void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    (void) port;

    return upi42_master_read_data(dev->mcu);
}

static void
kbc_upi42_data_w(uint16_t port, uint8_t val, void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    (void) port;

    kbc_at_upi42_log("KBC-UPI42: host data write %02X\n", val);

    upi42_master_write_data(dev->mcu, val);
}

static uint8_t
kbc_upi42_status_r(uint16_t port, void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    (void) port;

    return upi42_master_read_status(dev->mcu);
}

static void
kbc_upi42_cmd_w(uint16_t port, uint8_t val, void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    (void) port;

    kbc_at_upi42_log("KBC-UPI42: host command write %02X\n", val);

    upi42_master_write_cmd(dev->mcu, val);
}

static void
kbc_upi42_tick(void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    upi42_run(dev->mcu, KBC_AT_UPI42_CYCLES);

    timer_advance_u64(&dev->tick, KBC_AT_UPI42_TICK_US * TIMER_USEC);
}

static void *
kbc_upi42_init(const device_t *info)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) calloc(1, sizeof(kbc_upi42_t));

    dev->rom = (uint8_t *) malloc(KBC_AT_UPI42_ROM_SIZE);
    if (dev->rom == NULL) {
        free(dev);
        return NULL;
    }

    if (!kbc_upi42_load_rom(dev->rom, (const char *) info->local)) {
        free(dev->rom);
        free(dev);
        return NULL;
    }

    dev->mcu = upi42_create(UPI42_8042, dev->rom);
    if (dev->mcu == NULL) {
        free(dev->rom);
        free(dev);
        return NULL;
    }

    upi42_attach(dev->mcu, &kbc_upi42_hooks, dev);

    dev->p1 = 0xff;
    dev->p2 = 0xff;

    io_sethandler(0x0060, 1, kbc_upi42_data_r, NULL, NULL, kbc_upi42_data_w, NULL, NULL, dev);
    io_sethandler(0x0064, 1, kbc_upi42_status_r, NULL, NULL, kbc_upi42_cmd_w, NULL, NULL, dev);

    timer_add(&dev->tick, kbc_upi42_tick, dev, 0);
    timer_set_delay_u64(&dev->tick, KBC_AT_UPI42_TICK_US * TIMER_USEC);

    return dev;
}

static void
kbc_upi42_close(void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    if (dev == NULL)
        return;

    io_removehandler(0x0060, 1, kbc_upi42_data_r, NULL, NULL, kbc_upi42_data_w, NULL, NULL, dev);
    io_removehandler(0x0064, 1, kbc_upi42_status_r, NULL, NULL, kbc_upi42_cmd_w, NULL, NULL, dev);

    timer_disable(&dev->tick);

    upi42_close(dev->mcu);
    free(dev->rom);
    free(dev);
}

static void
kbc_upi42_reset(void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    upi42_reset(dev->mcu);

    dev->p1 = 0xff;
    dev->p2 = 0xff;
}

const device_t kbc_at_upi42_device = {
    .name          = "PC/AT Keyboard Controller (UPI-42 Firmware)",
    .internal_name = "kbc_at_upi42",
    .flags         = DEVICE_KBC,
    .local         = 0,
    .init          = kbc_upi42_init,
    .close         = kbc_upi42_close,
    .reset         = kbc_upi42_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
