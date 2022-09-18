/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Bus Mouse devices.
 *
 *		These devices were made by both Microsoft and Logitech. At
 *		first, Microsoft used the same protocol as Logitech, but did
 *		switch to their new protocol for their InPort interface. So,
 *		although alike enough to be handled in the same driver, they
 *		are not the same.
 *
 * NOTES:	Ported from Bochs with extensive modifications per testing
 *		of the real hardware, testing of drivers, and the old code.
 *
 *		Logitech Bus Mouse verified with:
 *		  Linux Slackware 3.0
 *		  Logitech LMouse.com 3.12
 *		  Logitech LMouse.com 3.30
 *		  Logitech LMouse.com 3.41
 *		  Logitech LMouse.com 3.42
 *		  Logitech LMouse.com 4.00
 *		  Logitech LMouse.com 5.00
 *		  Logitech LMouse.com 6.00
 *		  Logitech LMouse.com 6.02 Beta
 *		  Logitech LMouse.com 6.02
 *		  Logitech LMouse.com 6.12
 *		  Logitech LMouse.com 6.20
 *		  Logitech LMouse.com 6.23
 *		  Logitech LMouse.com 6.30
 *		  Logitech LMouse.com 6.31E
 *		  Logitech LMouse.com 6.34
 *		  Logitech Mouse.exe 6.40
 *		  Logitech Mouse.exe 6.41
 *		  Logitech Mouse.exe 6.44
 *		  Logitech Mouse.exe 6.46
 *		  Logitech Mouse.exe 6.50
 *		  Microsoft Mouse.com 2.00
 *		  Microsoft Mouse.sys 3.00
 *		  Microsoft Mouse.com 7.04
 *		  Microsoft Mouse.com 8.21J
 *		  Microsoft Windows 1.00 DR5
 *		  Microsoft Windows 3.10.026
 *		  Microsoft Windows 3.10.068 both MOUSE.DRV and LMOUSE.DRV
 *		  Microsoft Windows NT 3.1
 *		  Microsoft Windows 95
 *
 *		InPort verified with:
 *		  Linux Slackware 3.0
 *		  Logitech LMouse.com 6.12
 *		  Logitech LMouse.com 6.41
 *		  Microsoft Windows 3.10.068 both MOUSE.DRV and LMOUSE.DRV
 *		  Microsoft Windows NT 3.1
 *		  Microsoft Windows 98 SE
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 200?-2019 Bochs.
 *		Copyright 2017-2019 Miran Grca.
 *		Copyright 1989-2019 Fred N. van Kempen.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/mouse.h>
#include <86box/random.h>

#define IRQ_MASK ((1 << 5) >> dev->irq)

/* MS Inport Bus Mouse Adapter */
#define INP_PORT_CONTROL      0x0000
#define INP_PORT_DATA         0x0001
#define INP_PORT_SIGNATURE    0x0002
#define INP_PORT_CONFIG       0x0003

#define INP_CTRL_READ_BUTTONS 0x00
#define INP_CTRL_READ_X       0x01
#define INP_CTRL_READ_Y       0x02
#define INP_CTRL_COMMAND      0x07
#define INP_CTRL_RAISE_IRQ    0x16
#define INP_CTRL_RESET        0x80

#define INP_HOLD_COUNTER      (1 << 5)
#define INP_ENABLE_TIMER_IRQ  (1 << 4)
#define INP_ENABLE_DATA_IRQ   (1 << 3)
#define INP_PERIOD_MASK       0x07

/* MS/Logictech Standard Bus Mouse Adapter */
#define BUSM_PORT_DATA      0x0000
#define BUSM_PORT_SIGNATURE 0x0001
#define BUSM_PORT_CONTROL   0x0002
#define BUSM_PORT_CONFIG    0x0003

#define HOLD_COUNTER        (1 << 7)
#define READ_X              (0 << 6)
#define READ_Y              (1 << 6)
#define READ_LOW            (0 << 5)
#define READ_HIGH           (1 << 5)
#define DISABLE_IRQ         (1 << 4)

#define DEVICE_ACTIVE       (1 << 7)

#define READ_X_LOW          (READ_X | READ_LOW)
#define READ_X_HIGH         (READ_X | READ_HIGH)
#define READ_Y_LOW          (READ_Y | READ_LOW)
#define READ_Y_HIGH         (READ_Y | READ_HIGH)

#define FLAG_INPORT         (1 << 0)
#define FLAG_ENABLED        (1 << 1)
#define FLAG_HOLD           (1 << 2)
#define FLAG_TIMER_INT      (1 << 3)
#define FLAG_DATA_INT       (1 << 4)

static const uint8_t periods[4] = { 30, 50, 100, 200 };

/* Our mouse device. */
typedef struct mouse {
    uint8_t current_b, control_val,
        config_val, sig_val,
        command_val, pad;

    int8_t current_x, current_y;

    int base, irq, bn, flags,
        mouse_delayed_dx, mouse_delayed_dy,
        mouse_buttons, mouse_buttons_last,
        toggle_counter, timer_enabled;

    double     period;
    pc_timer_t timer; /* mouse event timer */
} mouse_t;

#ifdef ENABLE_MOUSE_BUS_LOG
int bm_do_log = ENABLE_MOUSE_BUS_LOG;

static void
bm_log(const char *fmt, ...)
{
    va_list ap;

    if (bm_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define bm_log(fmt, ...)
#endif

/* Handle a READ operation from one of our registers. */
static uint8_t
lt_read(uint16_t port, void *priv)
{
    mouse_t *dev   = (mouse_t *) priv;
    uint8_t  value = 0xff;

    switch (port & 0x03) {
        case BUSM_PORT_DATA:
            /* Testing and another source confirm that the buttons are
             *ALWAYS* present, so I'm going to change this a bit. */
            switch (dev->control_val & 0x60) {
                case READ_X_LOW:
                    value = dev->current_x & 0x0F;
                    dev->current_x &= ~0x0F;
                    break;
                case READ_X_HIGH:
                    value = (dev->current_x >> 4) & 0x0F;
                    dev->current_x &= ~0xF0;
                    break;
                case READ_Y_LOW:
                    value = dev->current_y & 0x0F;
                    dev->current_y &= ~0x0F;
                    break;
                case READ_Y_HIGH:
                    value = (dev->current_y >> 4) & 0x0F;
                    dev->current_y &= ~0xF0;
                    break;
                default:
                    bm_log("ERROR: Reading data port in unsupported mode 0x%02x\n", dev->control_val);
            }
            value |= ((dev->current_b ^ 7) << 5);
            break;
        case BUSM_PORT_SIGNATURE:
            value = dev->sig_val;
            break;
        case BUSM_PORT_CONTROL:
            value = dev->control_val;
            dev->control_val |= 0x0F;
            /* If the conditions are right, simulate the flakiness of the correct IRQ bit. */
            if (dev->flags & FLAG_TIMER_INT)
                dev->control_val = (dev->control_val & ~IRQ_MASK) | (random_generate() & IRQ_MASK);
            break;
        case BUSM_PORT_CONFIG:
            /* Read from config port returns control_val in the upper 4 bits when enabled,
               possibly solid interrupt readout in the lower 4 bits, 0xff when not (at power-up). */
            if (dev->flags & FLAG_ENABLED)
                return (dev->control_val | 0x0F) & ~IRQ_MASK;
            else
                return 0xff;
            break;
    }

    bm_log("DEBUG: read from address 0x%04x, value = 0x%02x\n", port, value);

    return value;
}

static uint8_t
ms_read(uint16_t port, void *priv)
{
    mouse_t *dev   = (mouse_t *) priv;
    uint8_t  value = 0xff;

    switch (port & 0x03) {
        case INP_PORT_CONTROL:
            value = dev->control_val;
            break;
        case INP_PORT_DATA:
            switch (dev->command_val) {
                case INP_CTRL_READ_BUTTONS:
                    value = dev->current_b;
                    break;
                case INP_CTRL_READ_X:
                    value          = dev->current_x;
                    dev->current_x = 0;
                    break;
                case INP_CTRL_READ_Y:
                    value          = dev->current_y;
                    dev->current_y = 0;
                    break;
                case INP_CTRL_COMMAND:
                    value = dev->control_val;
                    break;
                default:
                    bm_log("ERROR: Reading data port in unsupported mode 0x%02x\n", dev->control_val);
            }
            break;
        case INP_PORT_SIGNATURE:
            if (dev->toggle_counter)
                value = 0x12;
            else
                value = 0xDE;
            dev->toggle_counter ^= 1;
            break;
        case INP_PORT_CONFIG:
            bm_log("ERROR: Unsupported read from port 0x%04x\n", port);
            break;
    }

    bm_log("DEBUG: read from address 0x%04x, value = 0x%02x\n", port, value);

    return value;
}

/* Handle a WRITE operation to one of our registers. */
static void
lt_write(uint16_t port, uint8_t val, void *priv)
{
    mouse_t *dev = (mouse_t *) priv;
    uint8_t  bit;

    bm_log("DEBUG: write  to address 0x%04x, value = 0x%02x\n", port, val);

    switch (port & 0x03) {
        case BUSM_PORT_DATA:
            bm_log("ERROR: Unsupported write to port 0x%04x (value = 0x%02x)\n", port, val);
            break;
        case BUSM_PORT_SIGNATURE:
            dev->sig_val = val;
            break;
        case BUSM_PORT_CONTROL:
            dev->control_val = val | 0x0F;

            if (!(val & DISABLE_IRQ))
                dev->flags |= FLAG_TIMER_INT;
            else
                dev->flags &= ~FLAG_TIMER_INT;

            if (val & HOLD_COUNTER)
                dev->flags |= FLAG_HOLD;
            else
                dev->flags &= ~FLAG_HOLD;

            if (dev->irq != -1)
                picintc(1 << dev->irq);

            break;
        case BUSM_PORT_CONFIG:
            /*
             * The original Logitech design was based on using a
             * 8255 parallel I/O chip. This chip has to be set up
             * for proper operation, and this configuration data
             * is what is programmed into this register.
             *
             * A snippet of code found in the FreeBSD kernel source
             * explains the value:
             *
             * D7    =  Mode set flag (1 = active)
             * This indicates the mode of operation of D7:
             * 1 = Mode set, 0 = Bit set/reset
             * D6,D5 =  Mode selection (port A)
             *		00 = Mode 0 = Basic I/O
             *		01 = Mode 1 = Strobed I/O
             * 		10 = Mode 2 = Bi-dir bus
             * D4    =  Port A direction (1 = input)
             * D3    =  Port C (upper 4 bits) direction. (1 = input)
             * D2    =  Mode selection (port B & C)
             *		0 = Mode 0 = Basic I/O
             *		1 = Mode 1 = Strobed I/O
             * D1    =  Port B direction (1 = input)
             * D0    =  Port C (lower 4 bits) direction. (1 = input)
             *
             * So 91 means Basic I/O on all 3 ports, Port A is an input
             * port, B is an output port, C is split with upper 4 bits
             * being an output port and lower 4 bits an input port, and
             * enable the sucker.  Courtesy Intel 8255 databook. Lars
             *
             * 1001 1011	9B	1111	Default state
             * 1001 0001	91	1001	Driver-initialized state
             * The only difference is - port C upper and port B go from
             * input to output.
             */
            if (val & DEVICE_ACTIVE) {
                /* Mode set/reset - enable this */
                dev->config_val = val;
                if (dev->timer_enabled)
                    dev->flags |= (FLAG_ENABLED | FLAG_TIMER_INT);
                else
                    dev->flags |= FLAG_ENABLED;
                dev->control_val = 0x0F & ~IRQ_MASK;
            } else {
                /* Single bit set/reset */
                bit = 1 << ((val >> 1) & 0x07); /* Bits 3-1 specify the target bit */
                if (val & 1)
                    dev->control_val |= bit; /* Set */
                else
                    dev->control_val &= ~bit; /* Reset */
            }
            break;
    }
}

/* Handle a WRITE operation to one of our registers. */
static void
ms_write(uint16_t port, uint8_t val, void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    bm_log("DEBUG: write  to address 0x%04x, value = 0x%02x\n", port, val);

    switch (port & 0x03) {
        case INP_PORT_CONTROL:
            /* Bit 7 is reset. */
            if (val & INP_CTRL_RESET)
                dev->control_val = 0;

            /* Bits 0-2 are the internal register index. */
            switch (val & 0x07) {
                case INP_CTRL_COMMAND:
                case INP_CTRL_READ_BUTTONS:
                case INP_CTRL_READ_X:
                case INP_CTRL_READ_Y:
                    dev->command_val = val & 0x07;
                    break;
                default:
                    bm_log("ERROR: Unsupported command written to port 0x%04x (value = 0x%02x)\n", port, val);
            }
            break;
        case INP_PORT_DATA:
            if (dev->irq != -1)
                picintc(1 << dev->irq);
            switch (dev->command_val) {
                case INP_CTRL_COMMAND:
                    if (val & INP_HOLD_COUNTER)
                        dev->flags |= FLAG_HOLD;
                    else
                        dev->flags &= ~FLAG_HOLD;

                    if (val & INP_ENABLE_TIMER_IRQ)
                        dev->flags |= FLAG_TIMER_INT;
                    else
                        dev->flags &= ~FLAG_TIMER_INT;

                    if (val & INP_ENABLE_DATA_IRQ)
                        dev->flags |= FLAG_DATA_INT;
                    else
                        dev->flags &= ~FLAG_DATA_INT;

                    switch (val & INP_PERIOD_MASK) {
                        case 0:
                            dev->period = 0.0;
                            timer_disable(&dev->timer);
                            dev->timer_enabled = 0;
                            break;

                        case 1:
                        case 2:
                        case 3:
                        case 4:
                            dev->period        = (1000000.0 / (double) periods[(val & INP_PERIOD_MASK) - 1]);
                            dev->timer_enabled = (val & INP_ENABLE_TIMER_IRQ) ? 1 : 0;
                            timer_disable(&dev->timer);
                            if (dev->timer_enabled)
                                timer_set_delay_u64(&dev->timer, (uint64_t) (dev->period * (double) TIMER_USEC));
                            bm_log("DEBUG: Timer is now %sabled at period %i\n", (val & INP_ENABLE_TIMER_IRQ) ? "en" : "dis", (int32_t) dev->period);
                            break;

                        case 6:
                            if ((val & INP_ENABLE_TIMER_IRQ) && (dev->irq != -1))
                                picint(1 << dev->irq);
                            dev->control_val &= INP_PERIOD_MASK;
                            dev->control_val |= (val & ~INP_PERIOD_MASK);
                            return;
                        default:
                            bm_log("ERROR: Unsupported period written to port 0x%04x (value = 0x%02x)\n", port, val);
                    }

                    dev->control_val = val;

                    break;
                default:
                    bm_log("ERROR: Unsupported write to port 0x%04x (value = 0x%02x)\n", port, val);
            }
            break;
        case INP_PORT_SIGNATURE:
        case INP_PORT_CONFIG:
            bm_log("ERROR: Unsupported write to port 0x%04x (value = 0x%02x)\n", port, val);
            break;
    }
}

/* The emulator calls us with an update on the host mouse device. */
static int
bm_poll(int x, int y, int z, int b, void *priv)
{
    mouse_t *dev = (mouse_t *) priv;
    int xor ;

    if (!(dev->flags & FLAG_ENABLED))
        return (1); /* Mouse is disabled, do nothing. */

    if (!x && !y && !((b ^ dev->mouse_buttons_last) & 0x07)) {
        dev->mouse_buttons_last = b;
        return (1); /* State has not changed, do nothing. */
    }

    /* Converts button states from MRL to LMR. */
    dev->mouse_buttons = (uint8_t) (((b & 1) << 2) | ((b & 2) >> 1));
    if (dev->bn == 3)
        dev->mouse_buttons |= ((b & 4) >> 1);

    if ((dev->flags & FLAG_INPORT) && !dev->timer_enabled) {
        /* This is an InPort mouse in data interrupt mode,
           so update bits 6-3 here. */

        /* If the mouse has moved, set bit 6. */
        if (x || y)
            dev->mouse_buttons |= 0x40;

        /* Set bits 3-5 according to button state changes. */
        xor = ((dev->current_b ^ dev->mouse_buttons) & 0x07) << 3;
        dev->mouse_buttons |= xor;
    }

    dev->mouse_buttons_last = b;

    /* Clamp x and y to between -128 and 127 (int8_t range). */
    if (x > 127)
        x = 127;
    if (x < -128)
        x = -128;

    if (y > 127)
        y = 127;
    if (y < -128)
        y = -128;

    if (dev->timer_enabled) {
        /* Update delayed coordinates. */
        dev->mouse_delayed_dx += x;
        dev->mouse_delayed_dy += y;
    } else {
        /* If the counters are not frozen, update them. */
        if (!(dev->flags & FLAG_HOLD)) {
            dev->current_x = (int8_t) x;
            dev->current_y = (int8_t) y;

            dev->current_b = dev->mouse_buttons;
        }

        /* Send interrupt. */
        if ((dev->flags & FLAG_DATA_INT) && (dev->irq != -1)) {
            picint(1 << dev->irq);
            bm_log("DEBUG: Data Interrupt Fired...\n");
        }
    }
    return (0);
}

/* The timer calls us on every tick if the mouse is in timer mode
   (InPort mouse is so configured, MS/Logitech Bus mouse always). */
static void
bm_update_data(mouse_t *dev)
{
    int delta_x, delta_y;
    int xor ;

    /* If the counters are not frozen, update them. */
    if (!(dev->flags & FLAG_HOLD)) {
        /* Update the deltas and the delays. */
        if (dev->mouse_delayed_dx > 127) {
            delta_x = 127;
            dev->mouse_delayed_dx -= 127;
        } else if (dev->mouse_delayed_dx < -128) {
            delta_x = -128;
            dev->mouse_delayed_dx += 128;
        } else {
            delta_x               = dev->mouse_delayed_dx;
            dev->mouse_delayed_dx = 0;
        }

        if (dev->mouse_delayed_dy > 127) {
            delta_y = 127;
            dev->mouse_delayed_dy -= 127;
        } else if (dev->mouse_delayed_dy < -128) {
            delta_y = -128;
            dev->mouse_delayed_dy += 128;
        } else {
            delta_y               = dev->mouse_delayed_dy;
            dev->mouse_delayed_dy = 0;
        }

        dev->current_x = (int8_t) delta_x;
        dev->current_y = (int8_t) delta_y;
    } else
        delta_x = delta_y = 0;

    if (dev->flags & FLAG_INPORT) {
        /* This is an InPort mouse in timer mode, so update current_b always,
           and update bits 6-3 (mouse moved and button state changed) here. */
        xor            = ((dev->current_b ^ dev->mouse_buttons) & 0x07) << 3;
        dev->current_b = (dev->mouse_buttons & 0x87) | xor;
        if (delta_x || delta_y)
            dev->current_b |= 0x40;
    } else if (!(dev->flags & FLAG_HOLD)) {
        /* This is a MS/Logitech Bus Mouse, so only update current_b if the
           counters are frozen. */
        dev->current_b = dev->mouse_buttons;
    }
}

/* Called at the configured period (InPort mouse) or 45 times per second (MS/Logitech Bus mouse). */
static void
bm_timer(void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    bm_log("DEBUG: Timer Tick (flags=%08X)...\n", dev->flags);

    /* The period is configured either via emulator settings (for MS/Logitech Bus mouse)
       or via software (for InPort mouse). */
    timer_advance_u64(&dev->timer, (uint64_t) (dev->period * (double) TIMER_USEC));

    if ((dev->flags & FLAG_TIMER_INT) && (dev->irq != -1)) {
        picint(1 << dev->irq);
        bm_log("DEBUG: Timer Interrupt Fired...\n");
    }

    bm_update_data(dev);
}

/* Release all resources held by the device. */
static void
bm_close(void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    if (dev)
        free(dev);
}

/* Set the mouse's IRQ. */
void
mouse_bus_set_irq(void *priv, int irq)
{
    mouse_t *dev = (mouse_t *) priv;

    dev->irq = irq;
}

/* Initialize the device for use by the user. */
static void *
bm_init(const device_t *info)
{
    mouse_t *dev;
    int      hz;

    dev = (mouse_t *) malloc(sizeof(mouse_t));
    memset(dev, 0x00, sizeof(mouse_t));

    if ((info->local & ~MOUSE_TYPE_ONBOARD) == MOUSE_TYPE_INPORT)
        dev->flags = FLAG_INPORT;
    else
        dev->flags = 0;

    if (info->local & MOUSE_TYPE_ONBOARD) {
        dev->base = 0x023c;
        dev->irq  = -1;
        dev->bn   = 2;
    } else {
        dev->base = device_get_config_hex16("base");
        dev->irq  = device_get_config_int("irq");
        dev->bn   = device_get_config_int("buttons");
    }
    mouse_set_buttons(dev->bn);

    dev->mouse_delayed_dx   = 0;
    dev->mouse_delayed_dy   = 0;
    dev->mouse_buttons      = 0;
    dev->mouse_buttons_last = 0;
    dev->sig_val            = 0; /* the signature port value */
    dev->current_x = dev->current_y = 0;
    dev->current_b                  = 0;
    dev->command_val                = 0; /* command byte */
    dev->toggle_counter             = 0; /* signature byte / IRQ bit toggle */
    dev->period                     = 0.0;

    timer_add(&dev->timer, bm_timer, dev, 0);

    if (dev->flags & FLAG_INPORT) {
        dev->control_val = 0; /* the control port value */
        dev->flags |= FLAG_ENABLED;

        io_sethandler(dev->base, 4,
                      ms_read, NULL, NULL, ms_write, NULL, NULL, dev);

        dev->timer_enabled = 0;
    } else {
        dev->control_val = 0x0f; /* the control port value */
        dev->config_val  = 0x9b; /* the config port value - 0x9b is the
                                    default state of the 8255: all ports
                                    are set to input */

        hz = device_get_config_int("hz");
        if (hz > 0)
            dev->period = (1000000.0 / (double) hz);

        io_sethandler(dev->base, 4,
                      lt_read, NULL, NULL, lt_write, NULL, NULL, dev);

        if (hz > 0) {
            timer_set_delay_u64(&dev->timer, (uint64_t) (dev->period * (double) TIMER_USEC));
            dev->timer_enabled = 1;
        } else {
            dev->flags |= FLAG_DATA_INT;
            dev->timer_enabled = 0;
        }
    }

    if (dev->flags & FLAG_INPORT)
        bm_log("MS Inport BusMouse initialized\n");
    else
        bm_log("Standard MS/Logitech BusMouse initialized\n");

    return dev;
}

static const device_config_t lt_config[] = {
  // clang-format off
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x23c,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "0x230", .value = 0x230 },
            { .description = "0x234", .value = 0x234 },
            { .description = "0x238", .value = 0x238 },
            { .description = "0x23C", .value = 0x23c },
            { .description = ""                      }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 3", .value = 3 },
            { .description = "IRQ 4", .value = 4 },
            { .description = "IRQ 5", .value = 5 },
            { .description = ""                  }
        }
    },
    {
        .name = "hz",
        .description = "Hz",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 45,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Non-timed (original)",       .value =  0 },
            { .description = "30 Hz (JMP2 = 1)",           .value = 30 },
            { .description = "45 Hz (JMP2 not populated)", .value = 45 },
            { .description = "60 Hz (JMP 2 = 2)",          .value = 60 },
            { .description = ""                                        }
        }
    },
    {
        .name = "buttons",
        .description = "Buttons",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 2,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Two",   .value = 2 },
            { .description = "Three", .value = 3 },
            { .description = ""                  }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t ms_config[] = {
  // clang-format off
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x23c,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "0x230", .value = 0x230 },
            { .description = "0x234", .value = 0x234 },
            { .description = "0x238", .value = 0x238 },
            { .description = "0x23C", .value = 0x23c },
            { .description = ""                      }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 3", .value = 3 },
            { .description = "IRQ 4", .value = 4 },
            { .description = "IRQ 5", .value = 5 },
            { .description = ""                  }
        }
    },
    {
        .name = "buttons",
        .description = "Buttons",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 2,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Two",   .value = 2 },
            { .description = "Three", .value = 3 },
            { .description = ""                  }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t mouse_logibus_device = {
    .name          = "Logitech/Microsoft Bus Mouse",
    .internal_name = "logibus",
    .flags         = DEVICE_ISA,
    .local         = MOUSE_TYPE_LOGIBUS,
    .init          = bm_init,
    .close         = bm_close,
    .reset         = NULL,
    { .poll = bm_poll },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lt_config
};

const device_t mouse_logibus_onboard_device = {
    .name          = "Logitech Bus Mouse (On-Board)",
    .internal_name = "logibus_onboard",
    .flags         = DEVICE_ISA,
    .local         = MOUSE_TYPE_LOGIBUS | MOUSE_TYPE_ONBOARD,
    .init          = bm_init,
    .close         = bm_close,
    .reset         = NULL,
    { .poll = bm_poll },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t mouse_msinport_device = {
    .name          = "Microsoft Bus Mouse (InPort)",
    .internal_name = "msbus",
    .flags         = DEVICE_ISA,
    .local         = MOUSE_TYPE_INPORT,
    .init          = bm_init,
    .close         = bm_close,
    .reset         = NULL,
    { .poll = bm_poll },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms_config
};
