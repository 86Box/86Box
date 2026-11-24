/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Port 61h/speaker state, and the Intel 8255(A) Programmable
 *          Peripheral Interface.
 *
 *          The 8255 is a port of MAME's machine/i8255.cpp.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Curt Coder (MAME i8255),
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright Curt Coder; the 8255 code is distributed under the
 *          BSD-3-Clause license.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/plat_unused.h>

PPI ppi;
int ppispeakon;

void
ppi_reset(void)
{
    memset(&ppi, 0x00, sizeof(PPI));
}

/* Intel 8255(A) PPI. */

enum {
    PORT_A = 0,
    PORT_B,
    PORT_C,
    CONTROL
};

enum {
    GROUP_A = 0,
    GROUP_B
};

enum {
    MODE_0 = 0,
    MODE_1,
    MODE_2
};

enum {
    MODE_OUTPUT = 0,
    MODE_INPUT
};

#define CONTROL_PORT_C_LOWER_INPUT 0x01
#define CONTROL_PORT_B_INPUT       0x02
#define CONTROL_GROUP_B_MODE_1     0x04
#define CONTROL_PORT_C_UPPER_INPUT 0x08
#define CONTROL_PORT_A_INPUT       0x10
#define CONTROL_GROUP_A_MODE_MASK  0x60
#define CONTROL_MODE_SET           0x80

static void i8255_output_pc(i8255_t *dev);

static uint8_t
i8255_in_cb(const i8255_t *dev, int port)
{
    return dev->in[port] ? dev->in[port](dev->priv) : 0x00;
}

static uint8_t
i8255_tri_cb(const i8255_t *dev, int port)
{
    return dev->tri[port] ? dev->tri[port](dev->priv) : 0xff;
}

static void
i8255_out_cb(const i8255_t *dev, int port, uint8_t val)
{
    if (dev->out[port])
        dev->out[port](val, dev->priv);
}

static int
group_mode(const i8255_t *dev, int group)
{
    if (group == GROUP_A) {
        switch ((dev->control & CONTROL_GROUP_A_MODE_MASK) >> 5) {
            case 0:
                return MODE_0;
            case 1:
                return MODE_1;
            default:
                return MODE_2;
        }
    }

    return (dev->control & CONTROL_GROUP_B_MODE_1) ? MODE_1 : MODE_0;
}

static int
port_mode(const i8255_t *dev, int port)
{
    if (port == PORT_A)
        return (dev->control & CONTROL_PORT_A_INPUT) ? MODE_INPUT : MODE_OUTPUT;

    return (dev->control & CONTROL_PORT_B_INPUT) ? MODE_INPUT : MODE_OUTPUT;
}

static int
port_c_lower_mode(const i8255_t *dev)
{
    return (dev->control & CONTROL_PORT_C_LOWER_INPUT) ? MODE_INPUT : MODE_OUTPUT;
}

static int
port_c_upper_mode(const i8255_t *dev)
{
    return (dev->control & CONTROL_PORT_C_UPPER_INPUT) ? MODE_INPUT : MODE_OUTPUT;
}

static void
set_intr(i8255_t *dev, int port, int state)
{
    dev->intr[port] = state;
    i8255_output_pc(dev);
}

static void
check_interrupt(i8255_t *dev, int port)
{
    switch (group_mode(dev, port)) {
        case MODE_1:
            if (port_mode(dev, port) == MODE_INPUT)
                set_intr(dev, port, dev->inte[port] && dev->ibf[port]);
            else
                set_intr(dev, port, dev->inte[port] && dev->obf[port]);
            break;

        case MODE_2:
            set_intr(dev, port, (dev->inte1 && dev->obf[port]) || (dev->inte2 && dev->ibf[port]));
            break;

        default:
            break;
    }
}

static void
set_ibf(i8255_t *dev, int port, int state)
{
    dev->ibf[port] = state;
    check_interrupt(dev, port);
}

static void
set_obf(i8255_t *dev, int port, int state)
{
    dev->obf[port] = state;
    check_interrupt(dev, port);
}

static void
set_inte(i8255_t *dev, int port, int state)
{
    dev->inte[port] = state;
    check_interrupt(dev, port);
}

static void
set_inte1(i8255_t *dev, int state)
{
    dev->inte1 = state;
    check_interrupt(dev, PORT_A);
}

static void
set_inte2(i8255_t *dev, int state)
{
    dev->inte2 = state;
    check_interrupt(dev, PORT_A);
}

static uint8_t
read_mode0(i8255_t *dev, int port)
{
    if (port_mode(dev, port) == MODE_OUTPUT)
        return dev->output[port];

    return i8255_in_cb(dev, port);
}

static uint8_t
read_mode1(i8255_t *dev, int port)
{
    uint8_t data;

    if (port_mode(dev, port) == MODE_OUTPUT)
        return dev->output[port];

    /* Read the input latch, which clears IBF and INTR. */
    data = dev->input[port];
    set_ibf(dev, port, 0);
    set_intr(dev, port, 0);
    dev->input[port] = 0;

    return data;
}

static uint8_t
read_mode2(i8255_t *dev)
{
    const uint8_t data = dev->input[PORT_A];

    set_ibf(dev, PORT_A, 0);
    set_intr(dev, PORT_A, 0);
    dev->input[PORT_A] = 0;

    return data;
}

static uint8_t
read_pc(i8255_t *dev)
{
    uint8_t data   = 0;
    uint8_t mask   = 0;
    uint8_t b_mask = 0x0f;

    switch (group_mode(dev, GROUP_A)) {
        case MODE_0:
            if (port_c_upper_mode(dev) == MODE_OUTPUT)
                data |= dev->output[PORT_C] & 0xf0;
            else
                mask |= 0xf0;
            break;

        case MODE_1:
            data |= dev->intr[PORT_A] ? 0x08 : 0x00;

            if (port_mode(dev, PORT_A) == MODE_OUTPUT) {
                data |= dev->obf[PORT_A] ? 0x80 : 0x00;
                data |= dev->inte[PORT_A] ? 0x40 : 0x00;
                mask |= 0x30;
            } else {
                data |= dev->ibf[PORT_A] ? 0x20 : 0x00;
                data |= dev->inte[PORT_A] ? 0x10 : 0x00;
                mask |= 0xc0;
            }

            if (port_c_upper_mode(dev) == MODE_OUTPUT) {
                data |= dev->output[PORT_C] & mask;
                mask = 0;
            }
            break;

        case MODE_2:
            b_mask = 0x07;
            data |= dev->intr[PORT_A] ? 0x08 : 0x00;
            data |= dev->inte2 ? 0x10 : 0x00;
            data |= dev->ibf[PORT_A] ? 0x20 : 0x00;
            data |= dev->inte1 ? 0x40 : 0x00;
            data |= dev->obf[PORT_A] ? 0x80 : 0x00;
            break;

        default:
            break;
    }

    switch (group_mode(dev, GROUP_B)) {
        case MODE_0:
            if (port_c_lower_mode(dev) == MODE_OUTPUT)
                data |= dev->output[PORT_C] & b_mask;
            else
                mask |= b_mask;
            break;

        case MODE_1:
            data |= dev->inte[PORT_B] ? 0x04 : 0x00;
            data |= dev->intr[PORT_B] ? 0x01 : 0x00;

            if (port_mode(dev, PORT_B) == MODE_OUTPUT)
                data |= dev->obf[PORT_B] ? 0x02 : 0x00;
            else
                data |= dev->ibf[PORT_B] ? 0x02 : 0x00;
            break;

        default:
            break;
    }

    if (mask)
        data |= i8255_in_cb(dev, PORT_C) & mask;

    return data;
}

static void
write_mode0(i8255_t *dev, int port, uint8_t data)
{
    if (port_mode(dev, port) == MODE_OUTPUT) {
        dev->output[port] = data;
        i8255_out_cb(dev, port, data);
    }
}

static void
write_mode1(i8255_t *dev, int port, uint8_t data)
{
    if (port_mode(dev, port) == MODE_OUTPUT) {
        dev->output[port] = data;
        i8255_out_cb(dev, port, data);

        /* Writing the output latch drops OBF and clears INTR. */
        set_obf(dev, port, 0);
        set_intr(dev, port, 0);
    }
}

static void
write_mode2(i8255_t *dev, uint8_t data)
{
    dev->output[PORT_A] = data;
    i8255_out_cb(dev, PORT_A, data);

    set_obf(dev, PORT_A, 0);
    set_intr(dev, PORT_A, 0);
}

/* Drives port C: output-latch bits, handshake signals, and the floating
   level of bits configured as inputs. */
static void
i8255_output_pc(i8255_t *dev)
{
    uint8_t data   = 0;
    uint8_t mask   = 0;
    uint8_t b_mask = 0x0f;

    switch (group_mode(dev, GROUP_A)) {
        case MODE_0:
            if (port_c_upper_mode(dev) == MODE_OUTPUT)
                mask |= 0xf0;
            else
                data |= i8255_tri_cb(dev, PORT_C) & 0xf0;
            break;

        case MODE_1:
            data |= dev->intr[PORT_A] ? 0x08 : 0x00;

            if (port_mode(dev, PORT_A) == MODE_OUTPUT) {
                data |= dev->obf[PORT_A] ? 0x80 : 0x00;
                mask |= 0x30;
            } else {
                data |= dev->ibf[PORT_A] ? 0x20 : 0x00;
                mask |= 0xc0;
            }
            break;

        case MODE_2:
            b_mask = 0x07;
            data |= dev->intr[PORT_A] ? 0x08 : 0x00;
            data |= dev->ibf[PORT_A] ? 0x20 : 0x00;
            data |= dev->obf[PORT_A] ? 0x80 : 0x00;
            break;

        default:
            break;
    }

    switch (group_mode(dev, GROUP_B)) {
        case MODE_0:
            if (port_c_lower_mode(dev) == MODE_OUTPUT)
                mask |= b_mask;
            else
                data |= i8255_tri_cb(dev, PORT_C) & b_mask;
            break;

        case MODE_1:
            data |= dev->intr[PORT_B] ? 0x01 : 0x00;

            if (port_mode(dev, PORT_B) == MODE_OUTPUT)
                data |= dev->obf[PORT_B] ? 0x02 : 0x00;
            else
                data |= dev->ibf[PORT_B] ? 0x02 : 0x00;
            break;

        default:
            break;
    }

    data |= dev->output[PORT_C] & mask;
    i8255_out_cb(dev, PORT_C, data);
}

static void
set_mode(i8255_t *dev, uint8_t data)
{
    dev->control = data;

    if (dev->flags & I8255_FORCE_PORTB_IN)
        dev->control |= CONTROL_PORT_B_INPUT;

    if (dev->flags & I8255_FORCE_PORTC_OUT)
        dev->control &= ~(CONTROL_PORT_C_UPPER_INPUT | CONTROL_PORT_C_LOWER_INPUT);

    /* Group A */
    if (!(dev->flags & I8255_DONT_CLEAR_OUTPUT_LATCHES))
        dev->output[PORT_A] = 0;
    dev->input[PORT_A] = 0;
    dev->ibf[PORT_A]   = 0;
    dev->obf[PORT_A]   = 1;
    dev->inte[PORT_A]  = 0;
    dev->inte1         = 0;
    dev->inte2         = 0;

    if (port_mode(dev, PORT_A) == MODE_OUTPUT) {
        i8255_out_cb(dev, PORT_A, dev->output[PORT_A]);
        dev->ibf[PORT_A] = 1; /* MAME: needed by the SAM Coupe Blue Alpha sampler */
    } else
        i8255_out_cb(dev, PORT_A, i8255_tri_cb(dev, PORT_A));

    /* Group B */
    if (!(dev->flags & I8255_DONT_CLEAR_OUTPUT_LATCHES))
        dev->output[PORT_B] = 0;
    dev->input[PORT_B] = 0;
    dev->ibf[PORT_B]   = 0;
    dev->obf[PORT_B]   = 1;
    dev->inte[PORT_B]  = 0;

    if (port_mode(dev, PORT_B) == MODE_OUTPUT)
        i8255_out_cb(dev, PORT_B, dev->output[PORT_B]);
    else
        i8255_out_cb(dev, PORT_B, i8255_tri_cb(dev, PORT_B));

    if (!(dev->flags & I8255_DONT_CLEAR_OUTPUT_LATCHES))
        dev->output[PORT_C] = 0;
    dev->input[PORT_C] = 0;

    i8255_output_pc(dev);
}

static void
set_pc_bit(i8255_t *dev, int bit, int state)
{
    /* Set the output latch bit. */
    dev->output[PORT_C] &= ~(1 << bit);
    dev->output[PORT_C] |= state << bit;

    switch (group_mode(dev, GROUP_A)) {
        case MODE_1:
            if (port_mode(dev, PORT_A) == MODE_OUTPUT) {
                switch (bit) {
                    case 3:
                        set_intr(dev, PORT_A, state);
                        break;
                    case 6:
                        set_inte(dev, PORT_A, state);
                        break;
                    case 7:
                        set_obf(dev, PORT_A, state);
                        break;
                    default:
                        break;
                }
            } else {
                switch (bit) {
                    case 3:
                        set_intr(dev, PORT_A, state);
                        break;
                    case 4:
                        set_inte(dev, PORT_A, state);
                        break;
                    case 5:
                        set_ibf(dev, PORT_A, state);
                        break;
                    default:
                        break;
                }
            }
            break;

        case MODE_2:
            switch (bit) {
                case 3:
                    set_intr(dev, PORT_A, state);
                    break;
                case 4:
                    set_inte2(dev, state);
                    break;
                case 5:
                    set_ibf(dev, PORT_A, state);
                    break;
                case 6:
                    set_inte1(dev, state);
                    break;
                case 7:
                    set_obf(dev, PORT_A, state);
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }

    if (group_mode(dev, GROUP_B) == MODE_1) {
        switch (bit) {
            case 0:
                set_intr(dev, PORT_B, state);
                break;
            case 1:
                if (port_mode(dev, PORT_B) == MODE_OUTPUT)
                    set_obf(dev, PORT_B, state);
                else
                    set_ibf(dev, PORT_B, state);
                break;
            case 2:
                set_inte(dev, PORT_B, state);
                break;
            default:
                break;
        }
    }

    i8255_output_pc(dev);
}

uint8_t
i8255_read(i8255_t *dev, uint8_t reg)
{
    uint8_t data = 0;

    switch (reg & 0x03) {
        case PORT_A:
            switch (group_mode(dev, GROUP_A)) {
                case MODE_0:
                    data = read_mode0(dev, PORT_A);
                    break;
                case MODE_1:
                    data = read_mode1(dev, PORT_A);
                    break;
                case MODE_2:
                    data = read_mode2(dev);
                    break;
                default:
                    break;
            }
            break;

        case PORT_B:
            if (group_mode(dev, GROUP_B) == MODE_0)
                data = read_mode0(dev, PORT_B);
            else
                data = read_mode1(dev, PORT_B);
            break;

        case PORT_C:
            data = read_pc(dev);
            break;

        case CONTROL:
            data = dev->control;
            break;

        default:
            break;
    }

    return data;
}

void
i8255_write(i8255_t *dev, uint8_t reg, uint8_t val)
{
    switch (reg & 0x03) {
        case PORT_A:
            switch (group_mode(dev, GROUP_A)) {
                case MODE_0:
                    write_mode0(dev, PORT_A, val);
                    break;
                case MODE_1:
                    write_mode1(dev, PORT_A, val);
                    break;
                case MODE_2:
                    write_mode2(dev, val);
                    break;
                default:
                    break;
            }
            break;

        case PORT_B:
            if (group_mode(dev, GROUP_B) == MODE_0)
                write_mode0(dev, PORT_B, val);
            else
                write_mode1(dev, PORT_B, val);
            break;

        case PORT_C:
            dev->output[PORT_C] = val;
            i8255_output_pc(dev);
            break;

        case CONTROL:
            if (val & CONTROL_MODE_SET)
                set_mode(dev, val);
            else
                set_pc_bit(dev, (val >> 1) & 0x07, val & 0x01);
            break;

        default:
            break;
    }
}

/* Peripheral side. */
uint8_t
i8255_pa_r(i8255_t *dev)
{
    if (port_mode(dev, PORT_A) == MODE_OUTPUT)
        return dev->output[PORT_A];

    return 0xff;
}

uint8_t
i8255_acka_r(i8255_t *dev)
{
    uint8_t data;

    i8255_pc6_w(dev, 0);
    data = i8255_pa_r(dev);
    i8255_pc6_w(dev, 1);

    return data;
}

uint8_t
i8255_pb_r(i8255_t *dev)
{
    if (port_mode(dev, PORT_B) == MODE_OUTPUT)
        return dev->output[PORT_B];

    return 0xff;
}

uint8_t
i8255_ackb_r(i8255_t *dev)
{
    uint8_t data;

    i8255_pc2_w(dev, 0);
    data = i8255_pb_r(dev);
    i8255_pc2_w(dev, 1);

    return data;
}

/* PC2: port B strobe (input) or acknowledge (output) in mode 1. */
void
i8255_pc2_w(i8255_t *dev, int state)
{
    if (group_mode(dev, GROUP_B) != MODE_1)
        return;

    if (port_mode(dev, PORT_B) == MODE_OUTPUT) {
        /* Port B acknowledge. */
        if (!dev->obf[PORT_B] && !state)
            set_obf(dev, PORT_B, 1);
    } else if (!state) {
        /* Port B strobe. */
        dev->input[PORT_B] = i8255_in_cb(dev, PORT_B);
        set_ibf(dev, PORT_B, 1);
    }
}

/* PC4: port A strobe in mode 1 input or mode 2. */
void
i8255_pc4_w(i8255_t *dev, int state)
{
    if ((group_mode(dev, GROUP_A) == MODE_2) ||
        ((group_mode(dev, GROUP_A) == MODE_1) && (port_mode(dev, PORT_A) == MODE_INPUT))) {
        if (!state) {
            dev->input[PORT_A] = i8255_in_cb(dev, PORT_A);
            set_ibf(dev, PORT_A, 1);
        }
    }
}

/* PC6: port A acknowledge in mode 1 output or mode 2. */
void
i8255_pc6_w(i8255_t *dev, int state)
{
    if ((group_mode(dev, GROUP_A) == MODE_2) ||
        ((group_mode(dev, GROUP_A) == MODE_1) && (port_mode(dev, PORT_A) == MODE_OUTPUT))) {
        if (!dev->obf[PORT_A] && !state)
            set_obf(dev, PORT_A, 1);
    }
}

void
i8255_set_priv(i8255_t *dev, void *priv)
{
    dev->priv = priv;
}

void
i8255_set_flags(i8255_t *dev, uint8_t flags)
{
    dev->flags = flags;
}

void
i8255_set_in(i8255_t *dev, int port, uint8_t (*func)(void *priv))
{
    dev->in[port] = func;
}

void
i8255_set_out(i8255_t *dev, int port, void (*func)(uint8_t val, void *priv))
{
    dev->out[port] = func;
}

void
i8255_set_tri(i8255_t *dev, int port, uint8_t (*func)(void *priv))
{
    dev->tri[port] = func;
}

void
i8255_reset(i8255_t *dev)
{
    set_mode(dev, 0x9b);
}

static void
i8255_device_reset(void *priv)
{
    i8255_reset((i8255_t *) priv);
}

static void *
i8255_init(UNUSED(const device_t *info))
{
    i8255_t *dev = (i8255_t *) calloc(1, sizeof(i8255_t));

    /* No callbacks are attached yet; the owner sets them and resets the
       chip again. */
    i8255_reset(dev);

    return dev;
}

static void
i8255_close(void *priv)
{
    free(priv);
}

const device_t i8255_device = {
    .name          = "Intel 8255 Programmable Peripheral Interface",
    .internal_name = "i8255",
    .flags         = 0,
    .local         = 0,
    .init          = i8255_init,
    .close         = i8255_close,
    .reset         = i8255_device_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
