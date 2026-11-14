/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PC/AT keyboard controller backed by a real UPI-42 (8042) firmware
 *          image. The keyboard device is attached to the controller port and
 *          both directions of the emulated open-collector keyboard bus are
 *          implemented: 11-bit serial frames from the keyboard and clocked
 *          command transfers from the controller, including the data line
 *          acknowledge pulse.
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
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/upi42.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>

#define KBC_AT_UPI42_ROM_SIZE      2048
#define KBC_AT_UPI42_TICK_US       10 /* 100 kHz */
#define KBC_AT_UPI42_CYCLES        8  /* 12 MHz / 15 = 800 kHz, 10 us = 8 cycles */
#define KBC_AT_UPI42_KBD_POLL      10 /* keyboard port polled every 100 us */
#define KBC_AT_UPI42_KBD_BIT_TICKS 10 /* 100 us per bit, 10 kHz clock */
#define KBC_AT_UPI42_KBD_BITS      11 /* start, 8 data, parity, stop */
#define KBC_AT_UPI42_KBD_RX_BITS   10 /* 8 data, parity, stop */
#define KBC_AT_UPI42_KBD_ACK_TICKS 4  /* data line acknowledge hold time */

typedef struct {
    upi42_t   *mcu;
    pc_timer_t tick;
    uint8_t   *rom;
    uint8_t    p1;
    uint8_t    p2;
    int        irq;
    uint16_t   kbd_frame;
    uint8_t    kbd_bit;
    uint8_t    kbd_phase;
    uint8_t    kbd_sending;
    uint8_t    kbd_wait;
    uint8_t    kbd_poll;
    uint16_t   kbd_rx_frame;
    uint8_t    kbd_rx_state;
    uint8_t    kbd_rx_phase;
    uint8_t    kbd_rx_bit;
    kbc_upi42_bus_t bus;
} kbc_upi42_t;

kbc_upi42_bus_t *kbc_at_upi42_bus = NULL;

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
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    return dev->bus.kbc_clk && dev->bus.kbd_clk;
}

static int
kbc_upi42_t1_in(void *priv)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) priv;

    return dev->bus.kbc_data && dev->bus.kbd_data;
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
            /* P2.6 drives the keyboard clock (inverted), P2.7 the data line. */
            dev->bus.kbc_clk  = !(val & 0x40);
            dev->bus.kbc_data = !!(val & 0x80);
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

static uint8_t
kbc_upi42_parity(uint16_t val)
{
    uint8_t ret = 0;

    while (val) {
        ret ^= (val & 0x01);
        val >>= 1;
    }

    return ret;
}

static void
kbc_upi42_kbd_send(kbc_upi42_t *dev, uint8_t val)
{
    /* Odd parity: the data bits and the parity bit together contain an odd number of ones. */
    uint8_t parity = kbc_upi42_parity(val) ^ 0x01;

    /* Bit 0 is the start bit, followed by the data bits LSB first, the parity and the stop bit. */
    dev->kbd_frame   = ((uint16_t) val << 1) | ((uint16_t) parity << 9) | (1u << 10);
    dev->kbd_bit     = 0;
    dev->kbd_phase   = 0;
    dev->kbd_sending = 1;
    dev->kbd_wait    = 1;

    /* Pull the data line low to signal that the keyboard wants to send,
       the controller releases the clock in response. */
    dev->bus.kbd_clk  = 1;
    dev->bus.kbd_data = 0;

    kbc_at_upi42_log("KBC-UPI42: keyboard sends %02X\n", val);
}

static void
kbc_upi42_kbd_poll(kbc_upi42_t *dev)
{
    kbc_at_port_t *port = kbc_at_ports[DEV_KBD];
    uint8_t        val;

    if ((port == NULL) || (port->priv == NULL))
        return;

    port->poll(port->priv);

    if (dev->kbd_sending || (dev->kbd_rx_state != 0) || (port->out_new == -1))
        return;

    val           = (uint8_t) port->out_new;
    port->out_new = -1;

    kbc_upi42_kbd_send(dev, val);
}

static void
kbc_upi42_kbd_tick(kbc_upi42_t *dev)
{
    if (!dev->kbd_sending)
        return;

    /* The start bit is already on the line; wait for the controller to
       release the clock before clocking the frame out. */
    if (dev->kbd_wait) {
        if (!dev->bus.kbc_clk)
            return;

        dev->kbd_wait = 0;
    } else if (!dev->bus.kbc_clk) {
        /* The controller holds the clock line low to inhibit the keyboard. */
        return;
    }

    switch (dev->kbd_phase) {
        case 0:
            dev->bus.kbd_data = (dev->kbd_frame >> dev->kbd_bit) & 0x01;
            dev->bus.kbd_clk  = 1;
            break;

        case 2:
            dev->bus.kbd_clk = 0;
            break;

        case 7:
            dev->bus.kbd_clk = 1;
            break;

        case KBC_AT_UPI42_KBD_BIT_TICKS - 1:
            dev->kbd_bit++;
            if (dev->kbd_bit == KBC_AT_UPI42_KBD_BITS) {
                dev->kbd_sending  = 0;
                dev->bus.kbd_clk  = 1;
                dev->bus.kbd_data = 1;
            }
            break;

        default:
            break;
    }

    dev->kbd_phase = (dev->kbd_phase + 1) % KBC_AT_UPI42_KBD_BIT_TICKS;
}

/* Receives a byte from the controller. The controller pulls the clock line
   low and then the data line low (start), releases the clock and waits for
   the keyboard to clock the bits. The keyboard generates ten clock pulses
   (8 data bits, odd parity and stop), samples the data line after each
   rising edge and acknowledges by pulsing the data line low. */
static void
kbc_upi42_kbd_rx_tick(kbc_upi42_t *dev)
{
    kbc_at_port_t *port;
    atkbc_dev_t   *kbd;

    switch (dev->kbd_rx_state) {
        case 0:
            /* Do not take over the bus while a frame is being sent. */
            if (dev->kbd_sending)
                return;

            if (!dev->bus.kbc_clk && !dev->bus.kbc_data) {
                dev->kbd_rx_state = 1;
                dev->bus.kbd_clk  = 1;
                dev->bus.kbd_data = 1;
            }
            break;

        case 1:
            /* The controller releases the clock, the keyboard takes over. */
            if (dev->bus.kbc_clk) {
                dev->kbd_rx_bit   = 0;
                dev->kbd_rx_state = 2;
                dev->kbd_rx_phase = 0;
                dev->kbd_rx_frame = 0;
                kbc_at_upi42_log("KBC-UPI42: keyboard command transfer\n");
            }
            break;

        case 2:
            if (dev->kbd_rx_bit < KBC_AT_UPI42_KBD_RX_BITS) {
                switch (dev->kbd_rx_phase) {
                    case 0:
                        dev->bus.kbd_clk = 0;
                        break;

                    case KBC_AT_UPI42_KBD_BIT_TICKS / 2:
                        dev->bus.kbd_clk = 1;
                        break;

                    case (KBC_AT_UPI42_KBD_BIT_TICKS / 2) + 2:
                        if (dev->bus.kbc_data)
                            dev->kbd_rx_frame |= 1u << dev->kbd_rx_bit;
                        dev->kbd_rx_bit++;
                        break;

                    default:
                        break;
                }
            } else {
                /* Acknowledge the byte by holding the data line low. */
                if (dev->kbd_rx_phase == 0)
                    dev->bus.kbd_data = 0;
                else if (dev->kbd_rx_phase == KBC_AT_UPI42_KBD_ACK_TICKS)
                    dev->bus.kbd_data = 1;
                else if (dev->kbd_rx_phase == (KBC_AT_UPI42_KBD_BIT_TICKS - 1)) {
                    dev->bus.kbd_data = 1;
                    dev->kbd_rx_state = 0;

                    port = kbc_at_ports[DEV_KBD];
                    kbd  = (port != NULL) ? (atkbc_dev_t *) port->priv : NULL;

                    if (kbd != NULL) {
                        /* Odd parity and a high stop bit mean the whole
                           frame contains an even number of ones. */
                        if (((dev->kbd_rx_frame & 0x0200) != 0)
                         && (kbc_upi42_parity(dev->kbd_rx_frame & 0x03ff) == 0)) {
                            kbc_at_upi42_log("KBC-UPI42: keyboard command %02X\n",
                                             dev->kbd_rx_frame & 0xff);
                            port->dat     = (uint8_t) dev->kbd_rx_frame;
                            port->wantcmd = 1;
                        } else {
                            kbc_at_upi42_log("KBC-UPI42: bad keyboard frame %03X, resend\n",
                                             dev->kbd_rx_frame);
                            kbc_at_dev_queue_add(kbd, 0xfe, 0);
                        }
                    }
                }
            }

            dev->kbd_rx_phase = (dev->kbd_rx_phase + 1) % KBC_AT_UPI42_KBD_BIT_TICKS;
            break;

        default:
            break;
    }
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

    kbc_upi42_kbd_rx_tick(dev);
    kbc_upi42_kbd_tick(dev);

    if (++dev->kbd_poll >= KBC_AT_UPI42_KBD_POLL) {
        dev->kbd_poll = 0;
        kbc_upi42_kbd_poll(dev);
    }

    upi42_run(dev->mcu, KBC_AT_UPI42_CYCLES);

    timer_advance_u64(&dev->tick, KBC_AT_UPI42_TICK_US * TIMER_USEC);
}

static void
kbc_upi42_add_keyboard(void)
{
    const device_t *kbd;

    if (keyboard_type == KEYBOARD_TYPE_INTERNAL) {
        kbd = machine_get_kbd_device(machine);

        if (kbd != NULL)
            device_add(kbd);
        else
            device_add_params(&keyboard_at_generic_device, (void *) (uintptr_t) FLAG_AT);
    } else
        keyboard_add_device();
}

static void *
kbc_upi42_init(const device_t *info)
{
    kbc_upi42_t *dev = (kbc_upi42_t *) calloc(1, sizeof(kbc_upi42_t));

    if (dev == NULL)
        return NULL;

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

    dev->bus.kbc_clk  = 1;
    dev->bus.kbc_data = 1;
    dev->bus.kbd_clk  = 1;
    dev->bus.kbd_data = 1;

    kbc_at_upi42_bus = &dev->bus;

    /* The keyboard device attaches itself to this port. */
    kbc_at_ports[DEV_KBD] = (kbc_at_port_t *) calloc(1, sizeof(kbc_at_port_t));
    if (kbc_at_ports[DEV_KBD] == NULL) {
        upi42_close(dev->mcu);
        free(dev->rom);
        free(dev);
        return NULL;
    }
    kbc_at_ports[DEV_KBD]->out_new = -1;

    io_sethandler(0x0060, 1, kbc_upi42_data_r, NULL, NULL, kbc_upi42_data_w, NULL, NULL, dev);
    io_sethandler(0x0064, 1, kbc_upi42_status_r, NULL, NULL, kbc_upi42_cmd_w, NULL, NULL, dev);

    timer_add(&dev->tick, kbc_upi42_tick, dev, 0);
    timer_set_delay_u64(&dev->tick, KBC_AT_UPI42_TICK_US * TIMER_USEC);

    kbc_upi42_add_keyboard();

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

    if (kbc_at_ports[DEV_KBD] != NULL) {
        free(kbc_at_ports[DEV_KBD]);
        kbc_at_ports[DEV_KBD] = NULL;
    }

    if (kbc_at_upi42_bus == &dev->bus)
        kbc_at_upi42_bus = NULL;

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

    dev->bus.kbc_clk  = 1;
    dev->bus.kbc_data = 1;

    dev->kbd_sending  = 0;
    dev->kbd_wait     = 0;
    dev->kbd_bit      = 0;
    dev->kbd_poll     = 0;
    dev->kbd_phase    = 0;
    dev->kbd_rx_bit   = 0;
    dev->kbd_rx_phase = 0;
    dev->kbd_rx_state = 0;
    dev->bus.kbd_clk  = 1;
    dev->bus.kbd_data = 1;
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
