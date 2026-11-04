/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Tandy VIS PS/2 port.
 *
 *          The VIS has no 8042. Its single PS/2 port is a serial interface
 *          modelled on the one in the IBM PS/2 Model 25 gate array, and the
 *          BIOS drives it the same way:
 *
 *          0x66: control/status. Read bit 0 = parity error on the received
 *                byte. A low-to-high transition of bit 6 acknowledges the
 *                received byte and lets the device send the next one.
 *          0x67: data. Reads return the received byte, writes send a byte
 *                to the device.
 *          0x69: control latch, pulsed by BIOS along with 0x66 bit 6.
 *          0x6A: status. Bit 5 = received byte pending, bit 6 = ready to
 *                transmit.
 *
 *          0x60: reads return the received byte and acknowledge it, like
 *                an 8042's output buffer.
 *          0x64: read bits 0 and 5 = received byte pending.
 *
 *          A received byte requests master IRQ 1, which the VIS vectors to
 *          interrupt 71h. BIOS reads port 0xA0 there and only handles the
 *          interrupt if bit 2 is set (the pending byte shows up as IRQ 10 in
 *          the slave's IRR, and bit 2 of the slave's mask blocks the request),
 *          then dispatches it to INT 9 or, if BIOS found a mouse (device ID
 *          0), to the pointing device handler at INT 73h behind the INT 15h
 *          AH=C2h services. MOUSE.DRV in ROM is patched to hook interrupt 71h
 *          for a PS/2 mouse and, while Modular Windows runs in protected mode,
 *          takes the bytes itself through 0x64 and 0x60 instead.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2026 Cacodemon345.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>

typedef struct tvis_ps2_t {
    uint8_t port_66;
    uint8_t port_69;
    uint8_t rx_data;
    uint8_t irq;

    pc_timer_t poll_timer;
} tvis_ps2_t;

/* The device plugged into the port. Only a pointing device is supported, as
   the host keyboard drives the handheld controller instead. */
static kbc_at_port_t *
tvis_ps2_iface(void)
{
    kbc_at_port_t *iface = kbc_at_ports[DEV_AUX];

    return ((iface != NULL) && (iface->priv != NULL)) ? iface : NULL;
}

static int
tvis_ps2_rx_full(void)
{
    const kbc_at_port_t *iface = tvis_ps2_iface();

    return (iface != NULL) && (iface->out_new != -1);
}

static void
tvis_ps2_update_irq(tvis_ps2_t *dev)
{
    const int irq = tvis_ps2_rx_full() && !(pic2.imr & 0x04);

    if (irq && !dev->irq)
        picint(1 << 1);
    else if (!irq && dev->irq)
        picintc(1 << 1);

    dev->irq = irq;
}

static uint8_t
tvis_ps2_read(uint16_t addr, void *priv)
{
    tvis_ps2_t    *dev   = (tvis_ps2_t *) priv;
    kbc_at_port_t *iface = tvis_ps2_iface();
    uint8_t        ret   = 0xff;

    switch (addr) {
        case 0x60:
            if ((iface != NULL) && (iface->out_new != -1)) {
                dev->rx_data   = iface->out_new;
                iface->out_new = -1;
                tvis_ps2_update_irq(dev);
            }
            ret = dev->rx_data;
            break;

        case 0x64:
            ret = tvis_ps2_rx_full() ? 0x21 : 0x00;
            break;

        case 0x66:
            ret = dev->port_66 & ~0x01;
            break;

        case 0x67:
            if ((iface != NULL) && (iface->out_new != -1))
                dev->rx_data = iface->out_new;
            ret = dev->rx_data;
            break;

        case 0x69:
            ret = dev->port_69;
            break;

        case 0x6a:
            ret = (((iface == NULL) || !iface->wantcmd) ? 0x40 : 0x00) |
                  (tvis_ps2_rx_full() ? 0x20 : 0x00);
            break;

        default:
            break;
    }

    return ret;
}

static void
tvis_ps2_write(uint16_t addr, uint8_t val, void *priv)
{
    tvis_ps2_t    *dev   = (tvis_ps2_t *) priv;
    kbc_at_port_t *iface = tvis_ps2_iface();

    switch (addr) {
        case 0x66:
            if (!(dev->port_66 & 0x40) && (val & 0x40) && (iface != NULL)) {
                iface->out_new = -1;
                tvis_ps2_update_irq(dev);
            }
            dev->port_66 = val;
            break;

        case 0x67:
            /* Nothing plugged in: the byte goes nowhere. */
            if (iface != NULL) {
                iface->dat     = val;
                iface->wantcmd = 1;
            }
            break;

        case 0x69:
            dev->port_69 = val;
            break;

        default:
            break;
    }
}

/* Port 0xA0 reads return the slave PIC's IRR with the pending byte in
   bit 2. */
static uint8_t
tvis_ps2_pic2_read(uint16_t addr, UNUSED(void *priv))
{
    const int irr = !(pic2.ocw3 & 0x04) && ((pic2.ocw3 & 0x03) != 0x03);
    uint8_t   ret = pic_read(addr, &pic2);

    if (irr && tvis_ps2_rx_full())
        ret |= 0x04;

    return ret;
}

static void
tvis_ps2_pic2_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    pic_write(addr, val, &pic2);
}

static void
tvis_ps2_poll(void *priv)
{
    tvis_ps2_t    *dev   = (tvis_ps2_t *) priv;
    kbc_at_port_t *iface = tvis_ps2_iface();

    timer_advance_u64(&dev->poll_timer, 100ULL * TIMER_USEC);

    if (iface != NULL)
        iface->poll(iface->priv);

    tvis_ps2_update_irq(dev);
}

static void
tvis_ps2_reset(void *priv)
{
    tvis_ps2_t *dev = (tvis_ps2_t *) priv;

    dev->port_66 = 0x00;
    dev->port_69 = 0x00;
    dev->rx_data = 0x00;
    dev->irq     = 0;
    picintc(1 << 1);

    for (int i = 0; i < 2; i++) {
        kbc_at_ports[i]->wantcmd = 0;
        kbc_at_ports[i]->out_new = -1;
    }
}

static void *
tvis_ps2_init(UNUSED(const device_t *info))
{
    tvis_ps2_t *dev = (tvis_ps2_t *) calloc(1, sizeof(tvis_ps2_t));

    for (int i = 0; i < 2; i++) {
        kbc_at_ports[i]          = (kbc_at_port_t *) calloc(1, sizeof(kbc_at_port_t));
        kbc_at_ports[i]->out_new = -1;
    }

    keyboard_scan = 1;

    io_sethandler(0x0060, 1, tvis_ps2_read, NULL, NULL, NULL, NULL, NULL, dev);
    io_sethandler(0x0064, 1, tvis_ps2_read, NULL, NULL, NULL, NULL, NULL, dev);
    io_sethandler(0x0066, 2, tvis_ps2_read, NULL, NULL, tvis_ps2_write, NULL, NULL, dev);
    io_sethandler(0x0069, 2, tvis_ps2_read, NULL, NULL, tvis_ps2_write, NULL, NULL, dev);

    io_removehandler(0x00a0, 1, pic_read, NULL, NULL, pic_write, NULL, NULL, &pic2);
    io_sethandler(0x00a0, 1, tvis_ps2_pic2_read, NULL, NULL, tvis_ps2_pic2_write, NULL, NULL, dev);

    pic_set_vector_override(1, 0x71);

    timer_add(&dev->poll_timer, tvis_ps2_poll, dev, 1);
    tvis_ps2_reset(dev);

    return dev;
}

static void
tvis_ps2_close(void *priv)
{
    tvis_ps2_t *dev = (tvis_ps2_t *) priv;

    timer_disable(&dev->poll_timer);
    picintc(1 << 1);
    pic_set_vector_override(1, -1);

    io_removehandler(0x0060, 1, tvis_ps2_read, NULL, NULL, NULL, NULL, NULL, dev);
    io_removehandler(0x0064, 1, tvis_ps2_read, NULL, NULL, NULL, NULL, NULL, dev);
    io_removehandler(0x0066, 2, tvis_ps2_read, NULL, NULL, tvis_ps2_write, NULL, NULL, dev);
    io_removehandler(0x0069, 2, tvis_ps2_read, NULL, NULL, tvis_ps2_write, NULL, NULL, dev);

    io_removehandler(0x00a0, 1, tvis_ps2_pic2_read, NULL, NULL, tvis_ps2_pic2_write, NULL, NULL, dev);
    io_sethandler(0x00a0, 1, pic_read, NULL, NULL, pic_write, NULL, NULL, &pic2);

    for (int i = 0; i < 2; i++) {
        free(kbc_at_ports[i]);
        kbc_at_ports[i] = NULL;
    }

    free(dev);
}

const device_t kbc_tvis_device = {
    .name          = "Tandy VIS PS/2 Port",
    .internal_name = "kbc_tvis",
    .flags         = DEVICE_KBC,
    .local         = 0,
    .init          = tvis_ps2_init,
    .close         = tvis_ps2_close,
    .reset         = tvis_ps2_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
