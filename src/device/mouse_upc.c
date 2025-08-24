/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Chips & Technologies F82C710 Universal
 *          Peripheral Controller (UPC) PS/2 mouse port.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include "x86seg.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>

#define STAT_DEV_IDLE      0x01
#define STAT_RX_FULL       0x02
#define STAT_TX_IDLE       0x04
#define STAT_RESET         0x08
#define STAT_INTS_ON       0x10
#define STAT_ERROR_FLAG    0x20
#define STAT_CLEAR         0x40
#define STAT_ENABLE        0x80

typedef struct mouse_upc_t {
    uint8_t           status;
    uint8_t           ib;
    uint8_t           ob;
    uint8_t           state;
    uint8_t           ctrl_queue_start;
    uint8_t           ctrl_queue_end;

    uint8_t           handler_enable[2];

    uint16_t          mdata_addr;
    uint16_t          mstat_addr;

    uint16_t          irq;

    uint16_t          base_addr[2];

    /* Local copies of the pointers to both ports for easier swapping (AMI '5' MegaKey). */
    kbc_at_port_t    *port;

    /* Main timers. */
    pc_timer_t        poll_timer;
    pc_timer_t        dev_poll_timer;

    struct {
        uint8_t (*read)(uint16_t port, void *priv);
        void    (*write)(uint16_t port, uint8_t val, void *priv);
    } handlers[2];

    void             *mouse_ps2;
} mouse_upc_t;

enum {
    STATE_MAIN_IBF,    /* UPC checking if the input buffer is full. */
    STATE_MAIN,        /* UPC checking if the auxiliary has anything to send. */
    STATE_OUT,         /* UPC is sending multiple bytes. */
    STATE_SEND,        /* UPC is sending command to the auxiliary device. */
    STATE_SCAN         /* UPC is waiting for the auxiliary command response. */
};

#ifdef ENABLE_MOUSE_UPC_LOG
int mouse_upc_do_log = ENABLE_MOUSE_UPC_LOG;

static void
mouse_upc_log(const char *fmt, ...)
{
    va_list ap;

    if (mouse_upc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mouse_upc_log(fmt, ...)
#endif

static void
mouse_upc_send_to_ob(mouse_upc_t *dev, uint8_t val)
{
    dev->status = (dev->status & ~STAT_DEV_IDLE) | STAT_RX_FULL;

    if (dev->status & STAT_INTS_ON) {
        picint_common(1 << dev->irq, 0, 0, NULL);
        picint_common(1 << dev->irq, 0, 1, NULL);
    }

    dev->ob = val;
}

static void
set_enable_aux(mouse_upc_t *dev, uint8_t enable)
{
    dev->status &= ~STAT_ENABLE;
    dev->status |= (enable ? STAT_ENABLE : 0x00);
}

static void
mouse_upc_ibf_process(mouse_upc_t *dev)
{
    /* IBF set, process both commands and data. */
    dev->status |= STAT_TX_IDLE;
    dev->state   = STATE_MAIN_IBF;

    set_enable_aux(dev, 1);

    if (dev->port != NULL) {
        dev->port->wantcmd = 1;
        dev->port->dat     = dev->ib;
        dev->state         = STATE_SEND;
    }
}

/*
    Correct Procedure:
        1. Controller asks the device (keyboard or auxiliary device) for a byte.
        2. The device, unless it's in the reset or command states, sees if there's anything to give it,
           and if yes, begins the transfer.
        3. The controller checks if there is a transfer, if yes, transfers the byte and sends it to the host,
           otherwise, checks the next device, or if there is no device left to check, checks if IBF is full
           and if yes, processes it.
 */
static int
mouse_upc_scan(mouse_upc_t *dev)
{
    if ((dev->port != NULL) && (dev->port->out_new != -1)) {
        mouse_upc_log("UPC Mouse: %02X coming\n", dev->port->out_new & 0xff);
        mouse_upc_send_to_ob(dev, dev->port->out_new);
        dev->port->out_new = -1;
        dev->state         = STATE_MAIN_IBF;
        return 1;
    }

    return 0;
}

static void
mouse_upc_poll(void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;

    timer_advance_u64(&dev->poll_timer, (100ULL * TIMER_USEC));

    switch (dev->state) {
        case STATE_MAIN_IBF:
        default:
            if (!(dev->status & STAT_TX_IDLE))
                mouse_upc_ibf_process(dev);
            else if (!(dev->status & STAT_RX_FULL)) {
                if (dev->status & STAT_ENABLE)
                    dev->state = STATE_MAIN;
            }
            break;
        case STATE_MAIN:
            if (!(dev->status & STAT_TX_IDLE))
                mouse_upc_ibf_process(dev);
            else {
                (void) mouse_upc_scan(dev);
                dev->state = STATE_MAIN_IBF;
            }
            break;
        case STATE_SEND:
            if (!dev->port->wantcmd)
                dev->state = STATE_SCAN;
            break;
        case STATE_SCAN:
            (void) mouse_upc_scan(dev);
            break;
    }
}

static void
mouse_upc_dev_poll(void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;

    timer_advance_u64(&dev->dev_poll_timer, (100ULL * TIMER_USEC));

    if ((dev->port != NULL) && (dev->port->priv != NULL))
        dev->port->poll(dev->port->priv);
}

static void
mouse_upc_port_1_write(uint16_t port, uint8_t val, void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;

    mouse_upc_log("UPC Mouse: [%04X:%08X] write(%04X) = %02X\n", CS, cpu_state.pc, port, val);

    if ((dev->status & STAT_TX_IDLE) && (dev->status & STAT_ENABLE)) {
        dev->ib      = val;
        dev->status &= ~STAT_TX_IDLE;
    }
}

static void
mouse_upc_port_2_write(uint16_t port, uint8_t val, void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;

    mouse_upc_log("UPC Mouse: [%04X:%08X] write(%04X) = %02X\n", CS, cpu_state.pc, port, val);

    dev->status = (dev->status & 0x27) | (val & 0xd8);

    if (dev->status & (STAT_CLEAR | STAT_RESET)) {
        /* TODO: Silently reset the mouse. */
        dev->status &= ~STAT_RX_FULL;
        dev->status |= (STAT_DEV_IDLE | STAT_TX_IDLE);
    }
}

static uint8_t
mouse_upc_port_1_read(uint16_t port, void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;
    uint8_t      ret = 0xff;

    cycles -= ISA_CYCLES(8);

    ret = dev->ob;
    dev->ob = 0xff;
    dev->status &= ~STAT_RX_FULL;
    dev->status |= STAT_DEV_IDLE;

    mouse_upc_log("UPC Mouse: [%04X:%08X] read (%04X) = %02X\n",  CS, cpu_state.pc, port, ret);

    return ret;
}

static uint8_t
mouse_upc_port_2_read(uint16_t port, void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;
    uint8_t      ret = 0xff;

    cycles -= ISA_CYCLES(8);

    ret = dev->status;

    mouse_upc_log("UPC Mouse: [%04X:%08X] read (%04X) = %02X\n",  CS, cpu_state.pc, port, ret);

    return ret;
}

static void
mouse_upc_reset(void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;

    dev->status      = STAT_DEV_IDLE | STAT_TX_IDLE;
    dev->ob          = 0xff;

    dev->state       = STATE_MAIN_IBF;
}

static void
mouse_upc_close(void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;

    /* Stop timers. */
    timer_disable(&dev->dev_poll_timer);
    timer_disable(&dev->poll_timer);

    if (kbc_at_ports[1] != NULL) {
        free(kbc_at_ports[1]);
        kbc_at_ports[1] = NULL;
    }

    free(dev);
}

void
mouse_upc_port_handler(int num, int set, uint16_t port, void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;

    if (dev->handler_enable[num] && (dev->base_addr[num] != 0x0000))
        io_removehandler(dev->base_addr[num], 1,
                         dev->handlers[num].read, NULL, NULL,
                         dev->handlers[num].write, NULL, NULL, priv);

    dev->handler_enable[num] = set;
    dev->base_addr[num]      = port;

    if (dev->handler_enable[num] && (dev->base_addr[num] != 0x0000))
        io_sethandler(dev->base_addr[num], 1,
                      dev->handlers[num].read, NULL, NULL,
                      dev->handlers[num].write, NULL, NULL, priv);
}

void
mouse_upc_handler(int set, uint16_t port, void *priv)
{
    mouse_upc_port_handler(0, set, port, priv);
    mouse_upc_port_handler(1, set, port + 0x0001, priv);
}

void
mouse_upc_set_irq(int num, uint16_t irq, void *priv)
{
    mouse_upc_t *dev = (mouse_upc_t *) priv;

    if (dev->irq != 0xffff)
        picintc(1 << dev->irq);

    dev->irq = irq;
}

static void *
mouse_upc_init_common(int standalone, int irq)
{
    mouse_upc_t *dev;

    dev = (mouse_upc_t *) calloc(1, sizeof(mouse_upc_t));

    mouse_upc_reset(dev);

    dev->handlers[0].read  = mouse_upc_port_1_read;
    dev->handlers[0].write = mouse_upc_port_1_write;
    dev->handlers[1].read  = mouse_upc_port_2_read;
    dev->handlers[1].write = mouse_upc_port_2_write;

    dev->irq = irq;

    if (kbc_at_ports[1] == NULL) {
        kbc_at_ports[1] = (kbc_at_port_t *) calloc(1, sizeof(kbc_at_port_t));
        kbc_at_ports[1]->out_new = -1;
    }

    dev->port = kbc_at_ports[1];

    timer_add(&dev->poll_timer, mouse_upc_poll, dev, 1);
    timer_add(&dev->dev_poll_timer, mouse_upc_dev_poll, dev, 1);

    if (standalone) {
        mouse_upc_handler(1, 0x02d4, dev);
        dev->mouse_ps2 = device_add_params(&mouse_ps2_device, (void *) (uintptr_t) MOUSE_TYPE_PS2_QPORT);
    }

    return dev;
}

static void *
mouse_upc_init(const device_t *info)
{
    void *dev = NULL;

    if (info->local == 1)
        dev = mouse_upc_init_common(1, device_get_config_int("irq"));
    else
        dev = mouse_upc_init_common(0, info->local);

    return dev;
}

static const device_config_t upc_config[] = {
  // clang-format off
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 12,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 12",  .value = 12 },
            { .description = "IRQ 2/9", .value =  2 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "buttons",
        .description    = "Buttons",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Two",             .value = 2 },
            { .description = "Three",           .value = 3 },
            { .description = "Wheel",           .value = 4 },
            { .description = "Five + Wheel",    .value = 5 },
            { .description = "Five + 2 Wheels", .value = 6 },
            { .description = ""                              }
        },
        .bios           = { { 0 } }
    },
    {
        .name = "", .description = "", .type = CONFIG_END
    }
  // clang-format on
};

const device_t mouse_upc_device = {
    .name          = "PS/2 QuickPort Mouse (F82C710)",
    .internal_name = "mouse_upc",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = mouse_upc_init,
    .close         = mouse_upc_close,
    .reset         = mouse_upc_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t mouse_upc_standalone_device = {
    .name          = "PS/2 QuickPort Mouse",
    .internal_name = "mouse_upc_standalone",
    .flags         = DEVICE_ISA,
    .local         = 1,
    .init          = mouse_upc_init,
    .close         = mouse_upc_close,
    .reset         = mouse_upc_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = upc_config
};
