/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel 8042 (AT keyboard controller) emulation.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          EngiNerd, <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2023 Miran Grca.
 *          Copyright 2023 EngiNerd.
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
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/ppi.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/m_at_t3100e.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/keyboard.h>

#include <86box/dma.h>
#include <86box/pci.h>

#define STAT_PARITY        0x80
#define STAT_RTIMEOUT      0x40
#define STAT_TTIMEOUT      0x20
#define STAT_MFULL         0x20
#define STAT_UNLOCKED      0x10
#define STAT_CD            0x08
#define STAT_SYSFLAG       0x04
#define STAT_IFULL         0x02
#define STAT_OFULL         0x01

#define CCB_UNUSED         0x80
#define CCB_TRANSLATE      0x40
#define CCB_PCMODE         0x20
#define CCB_ENABLEKBD      0x10
#define CCB_IGNORELOCK     0x08
#define CCB_SYSTEM         0x04
#define CCB_ENABLEMINT     0x02
#define CCB_ENABLEKINT     0x01

#define CCB_MASK           0x68
#define MODE_MASK          0x6c

#define KBC_TYPE_ISA       0x00 /* AT ISA-based chips */
#define KBC_TYPE_PS2_1     0x01 /* PS2 on PS/2, type 1 */
#define KBC_TYPE_PS2_2     0x02 /* PS2 on PS/2, type 2 */
#define KBC_TYPE_GREEN     0x03 /* PS2 green controller */
#define KBC_TYPE_MASK      0x03

#define KBC_VEN_GENERIC    0x00
#define KBC_VEN_IBM_PS1    0x04
#define KBC_VEN_TOSHIBA    0x08
#define KBC_VEN_OLIVETTI   0x0c
#define KBC_VEN_AMI        0x10
#define KBC_VEN_TRIGEM_AMI 0x14
#define KBC_VEN_QUADTEL    0x18
#define KBC_VEN_PHOENIX    0x1c
#define KBC_VEN_ACER       0x20
#define KBC_VEN_NCR        0x24
#define KBC_VEN_ALI        0x28
#define KBC_VEN_SIEMENS    0x2c
#define KBC_VEN_COMPAQ     0x30
#define KBC_VEN_IBM        0x34
#define KBC_VEN_MASK       0x7c

#define KBC_FLAG_IS_ASIC   0x80000000

#define FLAG_CLOCK         0x01
#define FLAG_CACHE         0x02
#define FLAG_PS2           0x04
#define FLAG_PCI           0x08

enum {
    STATE_RESET = 0,       /* KBC reset state, only accepts command AA. */
    STATE_KBC_DELAY_OUT,   /* KBC is sending one single byte. */
    STATE_KBC_AMI_OUT,     /* KBC waiting for OBF - needed for AMIKey commands that require clearing of the output byte. */
    STATE_MAIN_IBF,        /* KBC checking if the input buffer is full. */
    STATE_MAIN_KBD,        /* KBC checking if the keyboard has anything to send. */
    STATE_MAIN_AUX,        /* KBC checking if the auxiliary has anything to send. */
    STATE_MAIN_BOTH,       /* KBC checking if either device has anything to send. */
    STATE_KBC_OUT,         /* KBC is sending multiple bytes. */
    STATE_KBC_PARAM,       /* KBC wants a parameter. */
    STATE_SEND_KBD,        /* KBC is sending command to the keyboard. */
    STATE_SCAN_KBD,        /* KBC is waiting for the keyboard command response. */
    STATE_SEND_AUX,        /* KBC is sending command to the auxiliary device. */
    STATE_SCAN_AUX         /* KBC is waiting for the auxiliary command response. */
};

typedef struct atkbc_t {
    uint8_t state;
    uint8_t command;
    uint8_t command_phase;
    uint8_t status;
    uint8_t wantdata;
    uint8_t ib;
    uint8_t ob;
    uint8_t sc_or;
    uint8_t mem_addr;
    uint8_t p1;
    uint8_t p2;
    uint8_t old_p2;
    uint8_t misc_flags;
    uint8_t ami_flags;
    uint8_t key_ctrl_queue_start;
    uint8_t key_ctrl_queue_end;
    uint8_t val;
    uint8_t channel;
    uint8_t stat_hi;
    uint8_t pending;
    uint8_t irq_state;
    uint8_t do_irq;
    uint8_t is_asic;
    uint8_t pad;

    uint8_t mem[0x100];

    /* Internal FIFO for the purpose of commands with multi-byte output. */
    uint8_t key_ctrl_queue[64];

    uint32_t flags;

    /* Main timers. */
    pc_timer_t kbc_poll_timer;
    pc_timer_t kbc_dev_poll_timer;

    /* P2 pulse callback timer. */
    pc_timer_t pulse_cb;

    /* Local copies of the pointers to both ports for easier swapping (AMI '5' MegaKey). */
    kbc_at_port_t     *ports[2];

    uint8_t (*write60_ven)(void *priv, uint8_t val);
    uint8_t (*write64_ven)(void *priv, uint8_t val);
} atkbc_t;

/* Keyboard controller ports. */
kbc_at_port_t  *kbc_at_ports[2] = { NULL, NULL };

static uint8_t kbc_ami_revision   = '8';
static uint8_t kbc_award_revision = 0x42;

static uint8_t kbc_handler_set    = 0;

static void (*kbc_at_do_poll)(atkbc_t *dev);

/* Non-translated to translated scan codes. */
static const uint8_t nont_to_t[256] = {
    0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58,
    0x64, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59,
    0x65, 0x38, 0x2a, 0x70, 0x1d, 0x10, 0x02, 0x5a,
    0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
    0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
    0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
    0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e,
    0x6a, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
    0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x60,
    0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
    0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e,
    0x3a, 0x36, 0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76,
    0x55, 0x56, 0x77, 0x78, 0x79, 0x7a, 0x0e, 0x7b,
    0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e, 0x7f, 0x6f,
    0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
    0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
    0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

static const uint8_t multikey_vars[0x0b] = {
    0x0a,
    0x03, 0x1e, 0x27, 0x28, 0x29, 0x38, 0x39, 0x18, 0x19, 0x35
};

static uint8_t fast_reset = 0x00;

void
kbc_at_set_fast_reset(const uint8_t new_fast_reset)
{
    fast_reset = new_fast_reset;
}

#ifdef ENABLE_KBC_AT_LOG
int kbc_at_do_log = ENABLE_KBC_AT_LOG;

static void
kbc_at_log(const char *fmt, ...)
{
    va_list ap;

    if (kbc_at_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define kbc_at_log(fmt, ...)
#endif

static void
kbc_at_queue_reset(atkbc_t *dev)
{
    dev->key_ctrl_queue_start = dev->key_ctrl_queue_end = 0;
    memset(dev->key_ctrl_queue, 0x00, sizeof(dev->key_ctrl_queue));
}

static void
kbc_at_queue_add(atkbc_t *dev, uint8_t val)
{
    kbc_at_log("ATkbc: dev->key_ctrl_queue[%02X] = %02X;\n", dev->key_ctrl_queue_end, val);
    dev->key_ctrl_queue[dev->key_ctrl_queue_end] = val;
    dev->key_ctrl_queue_end                 = (dev->key_ctrl_queue_end + 1) & 0x3f;
    dev->state = STATE_KBC_OUT;
}

static int
kbc_translate(atkbc_t *dev, uint8_t val)
{
    int      xt_mode   = (dev->mem[0x20] & 0x20) && !(dev->misc_flags & FLAG_PS2);
    /* The IBM AT keyboard controller firmware does not apply translation in XT mode. */
    int      translate = !xt_mode && ((dev->mem[0x20] & 0x40) || ((dev->flags & KBC_TYPE_MASK) == KBC_TYPE_PS2_2));
    uint8_t  kbc_ven   = dev->flags & KBC_VEN_MASK;
    int      ret       = - 1;

    /* Allow for scan code translation. */
    if (translate && (val == 0xf0)) {
        kbc_at_log("ATkbc: translate is on, F0 prefix detected\n");
        dev->sc_or = 0x80;
        return ret;
    }

    /* Skip break code if translated make code has bit 7 set. */
    if (translate && (dev->sc_or == 0x80) && (nont_to_t[val] & 0x80)) {
        kbc_at_log("ATkbc: translate is on, skipping scan code: %02X (original: F0 %02X)\n", nont_to_t[val], val);
        dev->sc_or = 0;
        return ret;
    }

    kbc_at_log("ATkbc: translate is %s, ", translate ? "on" : "off");
#ifdef ENABLE_KEYBOARD_AT_LOG
    kbc_at_log("scan code: ");
    if (translate) {
        kbc_at_log("%02X (original: ", (nont_to_t[val] | dev->sc_or));
        if (dev->sc_or == 0x80)
            kbc_at_log("F0 ");
        kbc_at_log("%02X)\n", val);
    } else
        kbc_at_log("%02X\n", val);
#endif

    ret = translate ? (nont_to_t[val] | dev->sc_or) : val;

    if (dev->sc_or == 0x80)
        dev->sc_or = 0;

    /* Test for T3100E 'Fn' key (Right Alt / Right Ctrl) */
    if ((dev != NULL) && (kbc_ven == KBC_VEN_TOSHIBA) &&
        (keyboard_recv(0x138) || keyboard_recv(0x11d)))  switch (ret) {
        case 0x4f:
            t3100e_notify_set(0x01);
            break; /* End */
        case 0x50:
            t3100e_notify_set(0x02);
            break; /* Down */
        case 0x51:
            t3100e_notify_set(0x03);
            break; /* PgDn */
        case 0x52:
            t3100e_notify_set(0x04);
            break; /* Ins */
        case 0x53:
            t3100e_notify_set(0x05);
            break; /* Del */
        case 0x54:
            t3100e_notify_set(0x06);
            break; /* SysRQ */
        case 0x45:
            t3100e_notify_set(0x07);
            break; /* NumLock */
        case 0x46:
            t3100e_notify_set(0x08);
            break; /* ScrLock */
        case 0x47:
            t3100e_notify_set(0x09);
            break; /* Home */
        case 0x48:
            t3100e_notify_set(0x0a);
            break; /* Up */
        case 0x49:
            t3100e_notify_set(0x0b);
            break; /* PgUp */
        case 0x4a:
            t3100e_notify_set(0x0c);
            break; /* Keypad - */
        case 0x4b:
            t3100e_notify_set(0x0d);
            break; /* Left */
        case 0x4c:
            t3100e_notify_set(0x0e);
            break; /* KP 5 */
        case 0x4d:
            t3100e_notify_set(0x0f);
            break; /* Right */
        default:
            break;
    }

    return ret;
}

static void
kbc_set_do_irq(atkbc_t *dev, uint8_t channel)
{
    dev->channel = channel;
    dev->do_irq  = 1;
}

static void
kbc_do_irq(atkbc_t *dev)
{
    if (dev->do_irq) {
        /* WARNING: On PS/2, all IRQ's are level-triggered, but the IBM PS/2 KBC firmware is explicitly
                    written to pulse its P2 IRQ bits, so they should be kept as as edge-triggered here. */
        picint_common(1 << 1, 0, 0, NULL);
        picint_common(1 << 12, 0, 0, NULL);
        if (dev->channel >= 2)
            picint_common(1 << 12, 0, 1, NULL);
        else
            picint_common(1 << 1, 0, 1, NULL);

        dev->do_irq = 0;
    }
}

static void
kbc_send_to_ob(atkbc_t *dev, uint8_t val, uint8_t channel, uint8_t stat_hi)
{
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    int temp = (channel == 1) ? kbc_translate(dev, val) : ((int) val);

    if (temp == -1)
        return;

    if ((kbc_ven == KBC_VEN_AMI) || (kbc_ven == KBC_VEN_TRIGEM_AMI) ||
        (dev->misc_flags & FLAG_PS2))
        stat_hi |= ((dev->p1 & 0x80) ? 0x10 : 0x00);
    else
        stat_hi |= 0x10;

    kbc_at_log("ATkbc: Sending %02X to the output buffer on channel %i...\n", temp, channel);
    dev->status = (dev->status & ~0xf0) | STAT_OFULL | stat_hi;

    dev->do_irq = 0;

    /* WARNING: On PS/2, all IRQ's are level-triggered, but the IBM PS/2 KBC firmware is explicitly
                written to pulse its P2 IRQ bits, so they should be kept as as edge-triggered here. */
    if (dev->misc_flags & FLAG_PS2) {
        if (channel >= 2) {
            dev->status |= STAT_MFULL;

            if (dev->mem[0x20] & 0x02)
                kbc_set_do_irq(dev, channel);
        } else if (dev->mem[0x20] & 0x01)
            kbc_set_do_irq(dev, channel);
    } else if (dev->mem[0x20] & 0x01)
        picintlevel(1 << 1, &dev->irq_state); /* AT KBC: IRQ 1 is level-triggered because it is tied to OBF. */

#ifdef WRONG_CONDITION
    if ((dev->channel > 0) || dev->is_asic || (kbc_ven == KBC_VEN_IBM_PS1) || (kbc_ven == KBC_VEN_IBM))
#endif
        kbc_do_irq(dev);

    dev->ob = temp;
}

static void
kbc_delay_to_ob(atkbc_t *dev, uint8_t val, uint8_t channel, uint8_t stat_hi)
{
    dev->val = val;
    dev->channel = channel;
    dev->stat_hi = stat_hi;
    dev->pending = 1;
    dev->state = STATE_KBC_DELAY_OUT;

    if (dev->is_asic && (channel == 0) && (dev->status & STAT_OFULL)) {
        /* Expedite the sending to the output buffer to prevent the wrong
           data from being accidentally read. */
        kbc_send_to_ob(dev, dev->val, dev->channel, dev->stat_hi);
        dev->state = STATE_MAIN_IBF;
        dev->pending = 0;
    }
}

static void kbc_at_process_cmd(void *priv);

static void
set_enable_kbd(atkbc_t *dev, uint8_t enable)
{
    dev->mem[0x20] &= 0xef;
    dev->mem[0x20] |= (enable ? 0x00 : 0x10);
}

static void
set_enable_aux(atkbc_t *dev, uint8_t enable)
{
    dev->mem[0x20] &= 0xdf;
    dev->mem[0x20] |= (enable ? 0x00 : 0x20);
}

static void
kbc_ibf_process(atkbc_t *dev)
{
    /* IBF set, process both commands and data. */
    dev->status &= ~STAT_IFULL;
    dev->state   = STATE_MAIN_IBF;
    if (dev->status & STAT_CD)
        kbc_at_process_cmd(dev);
    else {
        set_enable_kbd(dev, 1);
        if ((dev->ports[0] != NULL) && (dev->ports[0]->priv != NULL)) {
            dev->ports[0]->wantcmd = 1;
            dev->ports[0]->dat = dev->ib;
            dev->state         = STATE_SEND_KBD;
        } else
            kbc_delay_to_ob(dev, 0xfe, 1, 0x40);
    }
}

static void
kbc_scan_kbd_at(atkbc_t *dev)
{
    if (!(dev->mem[0x20] & 0x10)) {
        /* Both OBF and IBF clear and keyboard is enabled. */
        /* XT mode. */
        if (dev->mem[0x20] & 0x20) {
            if ((dev->ports[0] != NULL) && (dev->ports[0]->out_new != -1)) {
                kbc_send_to_ob(dev, dev->ports[0]->out_new, 1, 0x00);
                dev->ports[0]->out_new = -1;
                dev->state             = STATE_MAIN_IBF;
            } else if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
        /* AT mode. */
        } else {
#if 0
            dev->t = dev->mem[0x28];
#endif
            if (dev->mem[0x2e] != 0x00) {
#if 0
                if (!(dev->t & 0x02))
                    return;
#endif
                dev->mem[0x2e] = 0x00;
            }
            dev->p2 &= 0xbf;
            if ((dev->ports[0] != NULL) && (dev->ports[0]->out_new != -1)) {
                /* In our case, we never have noise on the line, so we can simplify this. */
                /* Read data from the keyboard. */
                if (dev->mem[0x20] & 0x40) {
                    if ((dev->mem[0x20] & 0x08) || (dev->p1 & 0x80))
                        kbc_send_to_ob(dev, dev->ports[0]->out_new, 1, 0x00);
                    dev->mem[0x2d] = (dev->ports[0]->out_new == 0xf0) ? 0x80 : 0x00;
                } else
                    kbc_send_to_ob(dev, dev->ports[0]->out_new, 1, 0x00);
                dev->ports[0]->out_new = -1;
                dev->state             = STATE_MAIN_IBF;
            }
        }
    }
}

static void
write_p2(atkbc_t *dev, uint8_t val);

static void
kbc_at_poll_at(atkbc_t *dev)
{
    switch (dev->state) {
        case STATE_RESET:
            if (dev->status & STAT_IFULL) {
                dev->status = ((dev->status & 0x0f) | 0x10) & ~STAT_IFULL;
                if ((dev->status & STAT_CD) && (dev->ib == 0xaa))
                    kbc_at_process_cmd(dev);
            }
            break;
        case STATE_KBC_AMI_OUT:
            if (dev->status & STAT_OFULL)
                break;
            fallthrough;
        case STATE_MAIN_IBF:
        default:
at_main_ibf:
           if (dev->status & STAT_OFULL) {
                /* OBF set, wait until it is cleared but still process commands. */
                if ((dev->status & STAT_IFULL) && (dev->status & STAT_CD)) {
                    dev->status &= ~STAT_IFULL;
                    kbc_at_process_cmd(dev);
                }
            } else if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else if (!(dev->mem[0x20] & 0x10))
                dev->state = STATE_MAIN_KBD;
            break;
        case STATE_MAIN_KBD:
        case STATE_MAIN_BOTH:
            if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else {
                (void) kbc_scan_kbd_at(dev);
                dev->state = STATE_MAIN_IBF;
            }
            break;
        case STATE_KBC_DELAY_OUT:
            /* Keyboard controller command want to output a single byte. */
            kbc_at_log("ATkbc: %02X coming from channel %i with high status %02X\n", dev->val, dev->channel, dev->stat_hi);
            kbc_send_to_ob(dev, dev->val, dev->channel, dev->stat_hi);
#if 0
            dev->state = (dev->pending == 2) ? STATE_KBC_AMI_OUT : STATE_MAIN_IBF;
#endif
            dev->state = STATE_MAIN_IBF;
            dev->pending = 0;
            goto at_main_ibf;
        case STATE_KBC_OUT:
            /* Keyboard controller command want to output multiple bytes. */
            if (dev->status & STAT_IFULL) {
                /* Data from host aborts dumping. */
                dev->state = STATE_MAIN_IBF;
                kbc_ibf_process(dev);
            }
            /* Do not continue dumping until OBF is clear. */
            if (!(dev->status & STAT_OFULL)) {
                kbc_at_log("ATkbc: %02X coming from channel 0\n", dev->key_ctrl_queue[dev->key_ctrl_queue_start]);
                kbc_send_to_ob(dev, dev->key_ctrl_queue[dev->key_ctrl_queue_start], 0, 0x00);
                dev->key_ctrl_queue_start = (dev->key_ctrl_queue_start + 1) & 0x3f;
                if (dev->key_ctrl_queue_start == dev->key_ctrl_queue_end)
                    dev->state = STATE_MAIN_IBF;
            }
            break;
        case STATE_KBC_PARAM:
            /* Keyboard controller command wants data, wait for said data. */
            if (dev->status & STAT_IFULL) {
                /* Command written, abort current command. */
                if (dev->status & STAT_CD)
                    dev->state = STATE_MAIN_IBF;

                dev->status &= ~STAT_IFULL;
                kbc_at_process_cmd(dev);
            }
            break;
        case STATE_SEND_KBD:
            if (!dev->ports[0]->wantcmd)
                dev->state = STATE_SCAN_KBD;
            break;
        case STATE_SCAN_KBD:
            kbc_scan_kbd_at(dev);
            break;
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
kbc_scan_kbd_ps2(atkbc_t *dev)
{
    if ((dev->ports[0] != NULL) && (dev->ports[0]->out_new != -1)) {
        kbc_at_log("ATkbc: %02X coming from channel 1\n", dev->ports[0]->out_new & 0xff);
        kbc_send_to_ob(dev, dev->ports[0]->out_new, 1, 0x00);
        dev->ports[0]->out_new = -1;
        dev->state             = STATE_MAIN_IBF;
        return 1;
    }

    return 0;
}

static int
kbc_scan_aux_ps2(atkbc_t *dev)
{
    if ((dev->ports[1] != NULL) && (dev->ports[1]->out_new != -1)) {
        kbc_at_log("ATkbc: %02X coming from channel 2\n", dev->ports[1]->out_new & 0xff);
        kbc_send_to_ob(dev, dev->ports[1]->out_new, 2, 0x00);
        dev->ports[1]->out_new = -1;
        dev->state             = STATE_MAIN_IBF;
        return 1;
    }

    return 0;
}

static void
kbc_at_poll_ps2(atkbc_t *dev)
{
    kbc_do_irq(dev);

    switch (dev->state) {
        case STATE_RESET:
            if (dev->status & STAT_IFULL) {
                dev->status = ((dev->status & 0x0f) | 0x10) & ~STAT_IFULL;
                if ((dev->status & STAT_CD) && (dev->ib == 0xaa))
                    kbc_at_process_cmd(dev);
            }
            break;
        case STATE_KBC_AMI_OUT:
            if (dev->status & STAT_OFULL)
                break;
            fallthrough;
        case STATE_MAIN_IBF:
        default:
            if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else if (!(dev->status & STAT_OFULL)) {
                if (dev->mem[0x20] & 0x20) {
                    if (!(dev->mem[0x20] & 0x10)) {
                        dev->p2 &= 0xbf;
                        dev->state = STATE_MAIN_KBD;
                    }
                } else {
                    dev->p2 &= 0xf7;
                    if (dev->mem[0x20] & 0x10)
                        dev->state = STATE_MAIN_AUX;
                    else {
                        dev->p2 &= 0xbf;
                        dev->state = STATE_MAIN_BOTH;
                    }
                }
            }
            break;
        case STATE_MAIN_KBD:
            if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else {
                (void) kbc_scan_kbd_ps2(dev);
                dev->state = STATE_MAIN_IBF;
            }
            break;
        case STATE_MAIN_AUX:
            if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else {
                (void) kbc_scan_aux_ps2(dev);
                dev->state = STATE_MAIN_IBF;
            }
            break;
        case STATE_MAIN_BOTH:
            if (kbc_scan_kbd_ps2(dev))
                dev->state = STATE_MAIN_IBF;
            else
                dev->state = STATE_MAIN_AUX;
            break;
        case STATE_KBC_DELAY_OUT:
            /* Keyboard controller command want to output a single byte. */
            kbc_at_log("ATkbc: %02X coming from channel %i with high status %02X\n", dev->val, dev->channel, dev->stat_hi);
            kbc_send_to_ob(dev, dev->val, dev->channel, dev->stat_hi);
#if 0
            dev->state = (dev->pending == 2) ? STATE_KBC_AMI_OUT : STATE_MAIN_IBF;
#endif
            dev->state = STATE_MAIN_IBF;
            dev->pending = 0;
            // goto ps2_main_ibf;
            break;
        case STATE_KBC_OUT:
            /* Keyboard controller command want to output multiple bytes. */
            if (dev->status & STAT_IFULL) {
                /* Data from host aborts dumping. */
                dev->state = STATE_MAIN_IBF;
                kbc_ibf_process(dev);
            }
            /* Do not continue dumping until OBF is clear. */
            if (!(dev->status & STAT_OFULL)) {
                kbc_at_log("ATkbc: %02X coming from channel 0\n", dev->key_ctrl_queue[dev->key_ctrl_queue_start] & 0xff);
                kbc_send_to_ob(dev, dev->key_ctrl_queue[dev->key_ctrl_queue_start], 0, 0x00);
                dev->key_ctrl_queue_start = (dev->key_ctrl_queue_start + 1) & 0x3f;
                if (dev->key_ctrl_queue_start == dev->key_ctrl_queue_end)
                    dev->state = STATE_MAIN_IBF;
            }
            break;
        case STATE_KBC_PARAM:
            /* Keyboard controller command wants data, wait for said data. */
            if (dev->status & STAT_IFULL) {
                /* Command written, abort current command. */
                if (dev->status & STAT_CD)
                    dev->state = STATE_MAIN_IBF;

                dev->status &= ~STAT_IFULL;
                kbc_at_process_cmd(dev);
            }
            break;
        case STATE_SEND_KBD:
            if (!dev->ports[0]->wantcmd)
                dev->state = STATE_SCAN_KBD;
            break;
        case STATE_SCAN_KBD:
            (void) kbc_scan_kbd_ps2(dev);
            break;
        case STATE_SEND_AUX:
            if (!dev->ports[1]->wantcmd)
                dev->state = STATE_SCAN_AUX;
            break;
        case STATE_SCAN_AUX:
            (void) kbc_scan_aux_ps2(dev);
            break;
    }
}

static void
kbc_at_poll(void *priv)
{
    atkbc_t *dev = (atkbc_t *) priv;

    timer_advance_u64(&dev->kbc_poll_timer, (100ULL * TIMER_USEC));

    /* TODO: Implement the password security state. */
    kbc_at_do_poll(dev);
}

static void
kbc_at_dev_poll(void *priv)
{
    atkbc_t *dev = (atkbc_t *) priv;

    timer_advance_u64(&dev->kbc_dev_poll_timer, (100ULL * TIMER_USEC));

    if ((kbc_at_ports[0] != NULL) && (kbc_at_ports[0]->priv != NULL))
        kbc_at_ports[0]->poll(kbc_at_ports[0]->priv);

    if ((kbc_at_ports[1] != NULL) && (kbc_at_ports[1]->priv != NULL))
        kbc_at_ports[1]->poll(kbc_at_ports[1]->priv);
}

static void
write_p2(atkbc_t *dev, uint8_t val)
{
    uint8_t old = dev->p2;
    kbc_at_log("ATkbc: write P2: %02X (old: %02X)\n", val, dev->p2);

    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;

#if 0
    /* PS/2: Handle IRQ's. */
    if (dev->misc_flags & FLAG_PS2) {
        /* IRQ 12 */
        picint_common(1 << 12, 0, val & 0x20, NULL);

        /* IRQ 1 */
        picint_common(1 << 1, 0, val & 0x10, NULL);
    }
#endif

    /* AT, PS/2: Handle A20. */
    if ((mem_a20_key ^ val) & 0x02) { /* A20 enable change */
        mem_a20_key = val & 0x02;
        mem_a20_recalc();
        flushmmucache();
    }

    /* AT, PS/2: Handle reset. */
    /* 0 holds the CPU in the RESET state, 1 releases it. To simplify this,
       we just do everything on release. */
    /* TODO: The fast reset flag's condition should be reversed - the BCM SQ-588
             enables the flag and the CPURST on soft reset flag but expects this
             to still soft reset instead. */
    if ((fast_reset || !cpu_cpurst_on_sr) && ((old ^ val) & 0x01)) { /*Reset*/
        if (!(val & 0x01)) {  /* Pin 0 selected. */
            /* Pin 0 selected. */
            kbc_at_log("write_p2(): Pulse reset!\n");
            if (machines[machine].flags & MACHINE_COREBOOT) {
                /* The SeaBIOS hard reset code attempts a KBC reset if ACPI RESET_REG
                   is not available. However, the KBC reset is normally a soft reset, so
                   SeaBIOS gets caught in a soft reset loop as it tries to hard reset the
                   machine. Hack around this by making the KBC reset a hard reset only on
                   coreboot machines. */
                pc_reset_hard();
            } else {
                softresetx86(); /* Pulse reset! */
                cpu_set_edx();
                flushmmucache();
                if ((kbc_ven == KBC_VEN_ALI) || !strcmp(machine_get_internal_name(), "spc7700plw"))
                    smbase = 0x00030000;

                /* Yes, this is a hack, but until someone gets ahold of the real PCD-2L
                   and can find out what they actually did to make it boot from FFFFF0
                   correctly despite A20 being gated when the CPU is reset, this will
                   have to do. */
                if ((kbc_ven == KBC_VEN_SIEMENS) || !strcmp(machine_get_internal_name(), "acera1g"))
                    is486 ? loadcs(0xf000) : loadcs_2386(0xf000);
            }
        }
    }

    /* Do this here to avoid an infinite reset loop. */
    dev->p2 = val;

    if (!fast_reset && cpu_cpurst_on_sr && ((old ^ val) & 0x01)) { /*Reset*/
        if (!(val & 0x01)) {  /* Pin 0 selected. */
            /* Pin 0 selected. */
            kbc_at_log("write_p2(): Pulse reset!\n");
            dma_reset();
            dma_set_at(1);

            device_reset_all(DEVICE_ALL);

            cpu_alt_reset = 0;

            pci_reset();

            mem_a20_alt = 0;
            mem_a20_recalc();

            flushmmucache();

            resetx86();
        }
    }
}

static void
write_p2_fast_a20(atkbc_t *dev, uint8_t val)
{
    uint8_t old = dev->p2;
    kbc_at_log("ATkbc: write P2 in fast A20 mode: %02X (old: %02X)\n", val, dev->p2);

    /* AT, PS/2: Handle A20. */
    if ((old ^ val) & 0x02) { /* A20 enable change */
        mem_a20_key = val & 0x02;
        mem_a20_recalc();
        flushmmucache();
    }

    /* Do this here to avoid an infinite reset loop. */
    dev->p2 = val;
}

static void
write_cmd(atkbc_t *dev, uint8_t val)
{
    kbc_at_log("ATkbc: write command byte: %02X (old: %02X)\n", val, dev->mem[0x20]);

    /* PS/2 type 2 keyboard controllers always force the XLAT bit to 0. */
    if ((dev->flags & KBC_TYPE_MASK) == KBC_TYPE_PS2_2) {
        val &= ~CCB_TRANSLATE;
        dev->mem[0x20] &= ~CCB_TRANSLATE;
    } else if (!(dev->misc_flags & FLAG_PS2)) {
        if (val & 0x10)
            dev->mem[0x2e] = 0x01;
    }

    kbc_at_log("ATkbc: keyboard interrupt is now %s\n", (val & 0x01) ? "enabled" : "disabled");

    if (!(dev->misc_flags & FLAG_PS2)) {
        /* Update P2 to mirror the IBF and OBF bits, if active. */
        write_p2(dev, (dev->p2 & 0x0f) | ((val & 0x03) << 4) | ((val & 0x20) ? 0xc0 : 0x00));
    }

    kbc_at_log("ATkbc: Command byte now: %02X (%02X)\n", dev->mem[0x20], val);

    dev->status = (dev->status & ~STAT_SYSFLAG) | (val & STAT_SYSFLAG);
}

static void
pulse_output(atkbc_t *dev, uint8_t mask)
{
    if (mask != 0x0f) {
        dev->old_p2 = dev->p2 & ~(0xf0 | mask);
        kbc_at_log("ATkbc: pulse_output(): P2 now: %02X\n", dev->p2 & (0xf0 | mask));
        write_p2(dev, dev->p2 & (0xf0 | mask));
        timer_set_delay_u64(&dev->pulse_cb, 6ULL * TIMER_USEC);
    }
}

static void
pulse_poll(void *priv)
{
    atkbc_t *dev = (atkbc_t *) priv;

    kbc_at_log("ATkbc: pulse_poll(): P2 now: %02X\n", dev->p2 | dev->old_p2);
    write_p2(dev, dev->p2 | dev->old_p2);
}

static uint8_t
write64_generic(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *) priv;
    uint8_t  current_drive;
    uint8_t  fixed_bits;
    uint8_t  kbc_ven = 0x0;
    kbc_ven          = dev->flags & KBC_VEN_MASK;

    switch (val) {
        case 0xa4: /* check if password installed */
            if (dev->misc_flags & FLAG_PS2) {
                kbc_at_log("ATkbc: check if password installed\n");
                kbc_delay_to_ob(dev, 0xf1, 0, 0x00);
                return 0;
            }
            break;

        case 0xa5: /* load security */
            kbc_at_log("ATkbc: load security\n");
            dev->wantdata = 1;
            dev->state = STATE_KBC_PARAM;
            return 0;

        case 0xa7: /* disable auxiliary port */
            if (dev->misc_flags & FLAG_PS2) {
                kbc_at_log("ATkbc: disable auxiliary port\n");
                set_enable_aux(dev, 0);
                return 0;
            }
            break;

        case 0xa8: /* Enable auxiliary port */
            if (dev->misc_flags & FLAG_PS2) {
                kbc_at_log("ATkbc: enable auxiliary port\n");
                set_enable_aux(dev, 1);
                return 0;
            }
            break;

        case 0xa9: /* Test auxiliary port */
            kbc_at_log("ATkbc: test auxiliary port\n");
            if (dev->misc_flags & FLAG_PS2) {
                kbc_delay_to_ob(dev, 0x00, 0, 0x00); /* no error, this is testing the channel 2 interface */
                return 0;
            }
            break;

        /* TODO: Make this command do nothing on the Regional HT6542,
                 or else, Efflixi's Award OPTi 495 BIOS gets a stuck key
                 in Norton Commander 3.0. */
        case 0xaf: /* read keyboard version */
            kbc_at_log("ATkbc: read keyboard version\n");
            kbc_delay_to_ob(dev, kbc_award_revision, 0, 0x00);
            return 0;

        /*
                                                                                P1 bits: 76543210
                                                                                -----------------
           IBM PS/1:                                                                     xxxxxxxx
           IBM PS/2 MCA:                                                                 xxxxx1xx
           Intel AMI Pentium BIOS'es with AMI MegaKey KB-5 keyboard controller:          x1x1xxxx
           Acer:                                                                         xxxxx0xx
           Packard Bell PB450:                                                           xxxxx1xx
           P6RP4:                                                                        xx1xx1xx
           Epson Action Tower 2600:                                                      xxxx01xx
           TriGem Hawk:                                                                  xxxx11xx

           Machine input based on current code:                                          11111111
           Everything non-Green:    Pull down bit 7 if not PS/2 and keyboard is inhibited.
                                    Pull down bit 6 if primary display is CGA.
           Xi8088:                  Pull down bit 6 if primary display is MDA.
           Acer:                    Pull down bit 6 if primary display is MDA.
                                    Pull down bit 2 always (must be so to enable CMOS Setup).
           IBM PS/1:                Pull down bit 6 if current floppy drive is 3.5".
           Epson Action Tower 2600: Pull down bit 3 always (for Epson logo).
           NCR:                     Pull down bit 5 always (power-on default speed = high).
                                    Pull down bit 3 if there is no FPU.
                                    Pull down bits 1 and 0 always?
           Compaq:                  Pull down bit 6 if Compaq dual-scan display is in use.
                                    Pull down bit 5 if system board DIP switch is ON.
                                    Pull down bit 4 if CPU speed selected is auto.
                                    Pull down bit 3 if CPU speed selected is slow (4 MHz).
                                    Pull down bit 2 if FPU is present.
                                    Pull down bits 1 and 0 always?

           Bit 7: AT KBC only - keyboard inhibited (often physical lock): 0 = yes, 1 = no (also Compaq);
           Bit 6: Mostly, display: 0 = CGA, 1 = MDA, inverted on Xi8088 and Acer KBC's;
                  Intel AMI MegaKey KB-5: Used for green features, SMM handler expects it to be set;
                  IBM PS/1 Model 2011: 0 = current FDD is 3.5", 1 = current FDD is 5.25";
                  Compaq: 0 = Compaq dual-scan display, 1 = non-Compaq display.
           Bit 5: Mostly, manufacturing jumper: 0 = installed (infinite loop at POST), 1 = not installed;
                  NCR: power-on default speed: 0 = high, 1 = low;
                  Compaq: System board DIP switch 5: 0 = ON, 1 = OFF.
           Bit 4: (Which board?): RAM on motherboard: 0 = 512 kB, 1 = 256 kB;
                  NCR: RAM on motherboard: 0 = unsupported, 1 = 512 kB;
                  Intel AMI MegaKey KB-5: Must be 1;
                  IBM PS/1: Ignored;
                  Compaq: 0 = Auto speed selected, 1 = High speed selected.
           Bit 3: TriGem AMIKey: most significant bit of 2-bit OEM ID;
                  NCR: Coprocessor detect (1 = yes, 0 = no);
                  Compaq: 0 = Slow (4 MHz), 1 = Fast (8 MHz);
                  Sometimes configured for clock switching;
           Bit 2: TriGem AMIKey: least significant bit of 2-bit OEM ID;
                  Bit 3, 2:
                      1, 1: TriGem logo;
                      1, 0: Garbled logo;
                      0, 1: Epson logo;
                      0, 0: Generic AMI logo.
                  NCR: Unused;
                  IBM PS/2: Keyboard power: 0 = no power (fuse error), 1 = OK
                  (for some reason, www.win.tue.nl has this in reverse);
                  Compaq: FPU: 0 = 80287, 1 = none;
                  Sometimes configured for clock switching;
           Bit 1: PS/2: Auxiliary device data in;
                  Compaq: Reserved;
                  NCR: High/auto speed.
           Bit 0: PS/2: Keyboard device data in;
                  Compaq: Reserved;
                  NCR: DMA mode.
         */
        case 0xc0: /* read P1 */
            kbc_at_log("ATkbc: read P1\n");
            fixed_bits = 4;
            /* The SMM handlers of Intel AMI Pentium BIOS'es expect bit 6 to be set. */
            if ((kbc_ven == KBC_VEN_AMI) && ((dev->flags & KBC_TYPE_MASK) == KBC_TYPE_GREEN))
                fixed_bits |= 0x40;
            if (kbc_ven == KBC_VEN_IBM_PS1) {
                current_drive = fdc_get_current_drive();
                /* (B0 or F0) | (fdd_is_525(current_drive) on bit 6) */
                kbc_delay_to_ob(dev, dev->p1 | fixed_bits | (fdd_is_525(current_drive) ? 0x40 : 0x00),
                                0, 0x00);
            } else if (kbc_ven == KBC_VEN_NCR) {
                /* switch settings
                 * bit 7: keyboard disable
                 * bit 6: display type (0 color, 1 mono)
                 * bit 5: power-on default speed (0 high, 1 low)
                 * bit 4: sense RAM size (0 unsupported, 1 512k on system board)
                 * bit 3: coprocessor detect
                 * bit 2: unused
                 * bit 1: high/auto speed
                 * bit 0: dma mode
                 */
                /* (B0 or F0) | 0x04 | (display on bit 6) | (fpu on bit 3) */
                kbc_delay_to_ob(dev, (dev->p1 | fixed_bits | (video_is_mda() ? 0x40 : 0x00) | (hasfpu ? 0x08 : 0x00)) & 0xdf,
                                0, 0x00);
            } else if (kbc_ven == KBC_VEN_TRIGEM_AMI) {
                /* Bit 3, 2:
                       1, 1: TriGem logo;
                       1, 0: Garbled logo;
                       0, 1: Epson logo;
                       0, 0: Generic AMI logo. */
                if (dev->misc_flags & FLAG_PCI)
                    fixed_bits |= 8;
                /* (B0 or F0) | (0x04 or 0x0c) */
                kbc_delay_to_ob(dev, dev->p1 | fixed_bits, 0, 0x00);
            } else if (((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1) && ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_GREEN))
                /* (B0 or F0) | (0x08 or 0x0c) */
                kbc_delay_to_ob(dev, ((dev->p1 | fixed_bits) & 0xf0) | (((dev->flags & KBC_VEN_MASK) == KBC_VEN_ACER) ? 0x08 : 0x0c), 0, 0x00);
            else if (kbc_ven == KBC_VEN_COMPAQ)
                kbc_delay_to_ob(dev, dev->p1 | (hasfpu ? 0x00 : 0x04), 0, 0x00);
            else
                /* (B0 or F0) | (0x04 or 0x44) */
                kbc_delay_to_ob(dev, dev->p1 | fixed_bits, 0, 0x00);
            dev->p1 = ((dev->p1 + 1) & 3) | (dev->p1 & 0xfc);
            return 0;

        case 0xc1: /*Copy bits 0 to 3 of P1 to status bits 4 to 7*/
            if (dev->misc_flags & FLAG_PS2) {
                kbc_at_log("ATkbc: copy bits 0 to 3 of P1 to status bits 4 to 7\n");
                dev->status &= 0x0f;
                dev->status |= (dev->p1 << 4);
                return 0;
            }
            break;

        case 0xc2: /*Copy bits 4 to 7 of P1 to status bits 4 to 7*/
            if (dev->misc_flags & FLAG_PS2) {
                kbc_at_log("ATkbc: copy bits 4 to 7 of P1 to status bits 4 to 7\n");
                dev->status &= 0x0f;
                dev->status |= (dev->p1 & 0xf0);
                return 0;
            }
            break;

        case 0xd3: /* write auxiliary output buffer */
            if (dev->misc_flags & FLAG_PS2) {
                kbc_at_log("ATkbc: write auxiliary output buffer\n");
                dev->wantdata = 1;
                dev->state = STATE_KBC_PARAM;
                return 0;
            }
            break;

        case 0xd4: /* write to auxiliary port */
            kbc_at_log("ATkbc: write to auxiliary port\n");
            dev->wantdata = 1;
            dev->state = STATE_KBC_PARAM;
            return 0;

        case 0xf0 ... 0xff:
            kbc_at_log("ATkbc: pulse %01X\n", val & 0x0f);
            pulse_output(dev, val & 0x0f);
            return 0;

        default:
            break;
    }

    kbc_at_log("ATkbc: bad command %02X\n", val);
    return 1;
}

static uint8_t
write60_ami(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *) priv;

    switch (dev->command) {
        /* 0x40 - 0x5F are aliases for 0x60-0x7F */
        case 0x40 ... 0x5f:
            kbc_at_log("ATkbc: AMI - alias write to %02X\n", dev->command & 0x1f);
            dev->mem[(dev->command & 0x1f) + 0x20] = val;
            if (dev->command == 0x60)
                write_cmd(dev, val);
            return 0;

        case 0xaf: /* set extended controller RAM */
            kbc_at_log("ATkbc: AMI - set extended controller RAM\n");
            if (dev->command_phase == 1) {
                dev->mem_addr      = val;
                dev->wantdata      = 1;
                dev->state         = STATE_KBC_PARAM;
                dev->command_phase = 2;
            } else if (dev->command_phase == 2) {
                dev->mem[dev->mem_addr] = val;
                dev->command_phase      = 0;
            }
            return 0;

        case 0xc1:
            kbc_at_log("ATkbc: AMI MegaKey - write %02X to P1\n", val);
            dev->p1 = val;
            return 0;

        case 0xcb: /* set keyboard mode */
            kbc_at_log("ATkbc: AMI - set keyboard mode\n");
            dev->ami_flags = val;
            dev->misc_flags &= ~FLAG_PS2;
            if (val & 0x01) {
                kbc_at_log("ATkbc: AMI: Emulate PS/2 keyboard\n");
                dev->misc_flags |= FLAG_PS2;
                kbc_at_do_poll = kbc_at_poll_ps2;
            } else {
                kbc_at_log("ATkbc: AMI: Emulate AT keyboard\n");
                kbc_at_do_poll = kbc_at_poll_at;
            }
            return 0;

        default:
            break;
    }

    return 1;
}

void
kbc_at_set_ps2(void *priv, const uint8_t ps2)
{
    atkbc_t *dev     = (atkbc_t *) priv;

    dev->ami_flags = (dev->ami_flags & 0xfe) | (!!ps2);
    dev->misc_flags &= ~FLAG_PS2;
    if (ps2) {
        dev->misc_flags |= FLAG_PS2;
        kbc_at_do_poll = kbc_at_poll_ps2;
    } else
        kbc_at_do_poll = kbc_at_poll_at;

    write_cmd(dev, ~dev->mem[0x20]);
    write_cmd(dev, dev->mem[0x20]);
}

static uint8_t
write64_ami(void *priv, uint8_t val)
{
    atkbc_t *dev     = (atkbc_t *) priv;
    uint8_t  kbc_ven = dev->flags & KBC_VEN_MASK;

    switch (val) {
        case 0x00 ... 0x1f:
            kbc_at_log("ATkbc: AMI - alias read from %08X\n", val);
            kbc_delay_to_ob(dev, dev->mem[val + 0x20], 0, 0x00);
            return 0;

        case 0x40 ... 0x5f:
            kbc_at_log("ATkbc: AMI - alias write to %08X\n", dev->command);
            dev->wantdata = 1;
            dev->state    = STATE_KBC_PARAM;
            return 0;

        case 0xa0: /* copyright message */
            kbc_at_queue_add(dev, 0x28);
            kbc_at_queue_add(dev, 0x00);
            return 0;

        case 0xa1: /* get controller version */
            kbc_at_log("ATkbc: AMI - get controller version\n");
            kbc_delay_to_ob(dev, kbc_ami_revision, 0, 0x00);
            return 0;

        case 0xa2: /* clear keyboard controller lines P22/P23 */
            if (!(dev->misc_flags & FLAG_PS2)) {
                kbc_at_log("ATkbc: AMI - clear KBC lines P22 and P23\n");
                write_p2(dev, dev->p2 & 0xf3);
                kbc_delay_to_ob(dev, 0x00, 0, 0x00);
                return 0;
            }
            break;

        case 0xa3: /* set keyboard controller lines P22/P23 */
            if (!(dev->misc_flags & FLAG_PS2)) {
                kbc_at_log("ATkbc: AMI - set KBC lines P22 and P23\n");
                write_p2(dev, dev->p2 | 0x0c);
                kbc_delay_to_ob(dev, 0x00, 0, 0x00);
                return 0;
            }
            break;

        case 0xa4: /* write clock = low */
            if (!(dev->misc_flags & FLAG_PS2)) {
                kbc_at_log("ATkbc: AMI - write clock = low\n");
                dev->misc_flags &= ~FLAG_CLOCK;
                return 0;
            }
            break;

        case 0xa5: /* write clock = high */
            if (!(dev->misc_flags & FLAG_PS2)) {
                kbc_at_log("ATkbc: AMI - write clock = high\n");
                dev->misc_flags |= FLAG_CLOCK;
                return 0;
            }

        case 0xa6: /* read clock */
            if (!(dev->misc_flags & FLAG_PS2)) {
                kbc_at_log("ATkbc: AMI - read clock\n");
                kbc_delay_to_ob(dev, (dev->misc_flags & FLAG_CLOCK) ? 0xff : 0x00, 0, 0x00);
                return 0;
            }
            break;

        case 0xa7: /* write cache bad */
            if (!(dev->misc_flags & FLAG_PS2)) {
                kbc_at_log("ATkbc: AMI - write cache bad\n");
                dev->misc_flags &= FLAG_CACHE;
                return 0;
            }
            break;

        case 0xa8: /* write cache good */
            if (!(dev->misc_flags & FLAG_PS2)) {
                kbc_at_log("ATkbc: AMI - write cache good\n");
                dev->misc_flags |= FLAG_CACHE;
                return 0;
            }
            break;

        case 0xa9: /* read cache */
            if (!(dev->misc_flags & FLAG_PS2)) {
                kbc_at_log("ATkbc: AMI - read cache\n");
                kbc_delay_to_ob(dev, (dev->misc_flags & FLAG_CACHE) ? 0xff : 0x00, 0, 0x00);
                return 0;
            }
            break;

        case 0xaf: /* set extended controller RAM */
            if ((kbc_ven != KBC_VEN_SIEMENS) && (kbc_ven != KBC_VEN_ALI)) {
                kbc_at_log("ATkbc: set extended controller RAM\n");
                dev->wantdata      = 1;
                dev->state         = STATE_KBC_PARAM;
                dev->command_phase = 1;
                return 0;
            }
            break;

        case 0xb0 ... 0xb3:
            /* set KBC lines P10-P13 (P1 bits 0-3) low */
            kbc_at_log("ATkbc: set KBC lines P10-P13 (P1 bits 0-3) low\n");
            if (!(dev->flags & DEVICE_PCI) || (val > 0xb1))
                dev->p1 &= ~(1 << (val & 0x03));
            kbc_delay_to_ob(dev, dev->ob, 0, 0x00);
            dev->pending++;
            return 0;

        /* TODO: The ICS SB486PV sends command B4 but expects to read *TWO* bytes. */
        case 0xb4: case 0xb5:
            /* set KBC lines P22-P23 (P2 bits 2-3) low */
            kbc_at_log("ATkbc: set KBC lines P22-P23 (P2 bits 2-3) low\n");
            if (!(dev->flags & DEVICE_PCI))
                write_p2(dev, dev->p2 & ~(4 << (val & 0x01)));
            kbc_delay_to_ob(dev, dev->ob, 0, 0x00);
            dev->pending++;
            return 0;

        case 0xb8 ... 0xbb:
            /* set KBC lines P10-P13 (P1 bits 0-3) high */
            kbc_at_log("ATkbc: set KBC lines P10-P13 (P1 bits 0-3) high\n");
            if (!(dev->flags & DEVICE_PCI) || (val > 0xb9)) {
                dev->p1 |= (1 << (val & 0x03));
                kbc_delay_to_ob(dev, dev->ob, 0, 0x00);
                dev->pending++;
            }
            return 0;

        case 0xbc: case 0xbd:
            /* set KBC lines P22-P23 (P2 bits 2-3) high */
            kbc_at_log("ATkbc: set KBC lines P22-P23 (P2 bits 2-3) high\n");
            if (!(dev->flags & DEVICE_PCI))
                write_p2(dev, dev->p2 | (4 << (val & 0x01)));
            kbc_delay_to_ob(dev, dev->ob, 0, 0x00);
            dev->pending++;
            return 0;

        case 0xc1: /* write P1 */
            kbc_at_log("ATkbc: AMI MegaKey - write P1\n");
            dev->wantdata  = 1;
            dev->state     = STATE_KBC_PARAM;
            return 0;

        case 0xc4:
            /* set KBC line P14 low */
            kbc_at_log("ATkbc: set KBC line P14 (P1 bit 4) low\n");
            dev->p1 &= 0xef;
            kbc_delay_to_ob(dev, dev->ob, 0, 0x00);
            dev->pending++;
            return 0;
        case 0xc5:
            /* set KBC line P15 low */
            kbc_at_log("ATkbc: set KBC line P15 (P1 bit 5) low\n");
            dev->p1 &= 0xdf;
            kbc_delay_to_ob(dev, dev->ob, 0, 0x00);
            dev->pending++;
            return 0;

        case 0xc8:
            /*
             * unblock KBC lines P22/P23
             * (allow command D1 to change bits 2/3 of P2)
             */
            kbc_at_log("ATkbc: AMI - unblock KBC lines P22 and P23\n");
            dev->ami_flags &= 0xfb;
            return 0;

        case 0xc9:
            /*
             * block KBC lines P22/P23
             * (disallow command D1 from changing bits 2/3 of the port)
             */
            kbc_at_log("ATkbc: AMI - block KBC lines P22 and P23\n");
            dev->ami_flags |= 0x04;
            return 0;

        case 0xcc:
            /* set KBC line P14 high */
            kbc_at_log("ATkbc: set KBC line P14 (P1 bit 4) high\n");
            dev->p1 |= 0x10;
            kbc_delay_to_ob(dev, dev->ob, 0, 0x00);
            dev->pending++;
            return 0;
        case 0xcd:
            /* set KBC line P15 high */
            kbc_at_log("ATkbc: set KBC line P15 (P1 bit 5) high\n");
            dev->p1 |= 0x20;
            kbc_delay_to_ob(dev, dev->ob, 0, 0x00);
            dev->pending++;
            return 0;

        case 0xef: /* ??? - sent by AMI486 */
            kbc_at_log("ATkbc: ??? - sent by AMI486\n");
            return 0;

        default:
            break;
    }

    return write64_generic(dev, val);
}

static uint8_t
write60_phoenix(void *priv, uint8_t val)
{
    atkbc_t *dev     = (atkbc_t *) priv;

    switch (dev->command) {
        /* TODO: Make this actually load the password. */
        case 0xa3: /* Load Extended Password */
            kbc_at_log("ATkbc: Phoenix - Load Extended Password\n");
            if (val == 0x00)
                dev->command_phase = 0;
            else {
                dev->wantdata      = 1;
                dev->state         = STATE_KBC_PARAM;
            }
            return 0;

        case 0xaf: /* Set Inactivity Timer */
            kbc_at_log("ATkbc: Phoenix - Set Inactivity Timer\n");
            dev->mem[0x3a]    = val;
            dev->command_phase = 0;
            return 0;

        case 0xb8: /* Set Extended Memory Access Index */
            kbc_at_log("ATkbc: Phoenix - Set Extended Memory Access Index\n");
            dev->mem_addr      = val;
            dev->command_phase = 0;
            return 0;

        case 0xbb: /* Set Extended Memory */
            kbc_at_log("ATkbc: Phoenix - Set Extended Memory\n");
            dev->mem[dev->mem_addr] = val;
            dev->command_phase      = 0;
            return 0;

        case 0xbd: /* Set MultiKey Variable */
            kbc_at_log("ATkbc: Phoenix - Set MultiKey Variable\n");
            if ((dev->mem_addr > 0) && (dev->mem_addr <= multikey_vars[0x00]))
                dev->mem[multikey_vars[dev->mem_addr]] = val;
            dev->command_phase      = 0;
            return 0;

        case 0xc7: /* Set Port1 bits */
            kbc_at_log("ATkbc: Phoenix - Set Port1 bits\n");
            dev->p1           |= val;
            dev->command_phase = 0;
            return 0;

        case 0xc8: /* Clear Port1 bits */
            kbc_at_log("ATkbc: Phoenix - Clear Port1 bits\n");
            dev->p1           &= ~val;
            dev->command_phase = 0;
            return 0;

        case 0xc9: /* Set Port2 bits */
            kbc_at_log("ATkbc: Phoenix - Set Port2 bits\n");
            write_p2(dev, dev->p2 | val);
            dev->command_phase = 0;
            return 0;

        case 0xca: /* Clear Port2 bits */
            kbc_at_log("ATkbc: Phoenix - Clear Port2 bits\n");
            write_p2(dev, dev->p2 & ~val);
            dev->command_phase = 0;
            return 0;

        default:
            break;
    }

    return 1;
}

static uint8_t
write64_phoenix(void *priv, uint8_t val)
{
    atkbc_t *dev     = (atkbc_t *) priv;

    switch (val) {
        case 0x00 ... 0x1f:
            kbc_at_log("ATkbc: Phoenix - alias read from %08X\n", val);
            kbc_delay_to_ob(dev, dev->mem[val + 0x20], 0, 0x00);
            return 0;

        case 0x40 ... 0x5f:
            kbc_at_log("ATkbc: Phoenix - alias write to %08X\n", dev->command);
            dev->wantdata = 1;
            dev->state    = STATE_KBC_PARAM;
            return 0;

        case 0xa2: /* Test Extended Password */
            kbc_at_log("ATkbc: Phoenix - Test Extended Password\n");
            kbc_at_queue_add(dev, 0xf1); /* Extended Password not loaded */
            return 0;

        /* TODO: Make this actually load the password. */
        case 0xa3: /* Load Extended Password */
            kbc_at_log("ATkbc: Phoenix - Load Extended Password\n");
            dev->wantdata = 1;
            dev->state    = STATE_KBC_PARAM;
            return 0;

        case 0xaf: /* Set Inactivity Timer */
            kbc_at_log("ATkbc: Phoenix - Set Inactivity Timer\n");
            dev->wantdata = 1;
            dev->state    = STATE_KBC_PARAM;
            return 0;

        case 0xb8: /* Set Extended Memory Access Index */
            kbc_at_log("ATkbc: Phoenix - Set Extended Memory Access Index\n");
            dev->wantdata = 1;
            dev->state    = STATE_KBC_PARAM;
            return 0;

        case 0xb9: /* Get Extended Memory Access Index */
            kbc_at_log("ATkbc: Phoenix - Get Extended Memory Access Index\n");
            kbc_at_queue_add(dev, dev->mem_addr);
            return 0;

        case 0xba: /* Get Extended Memory */
            kbc_at_log("ATkbc: Phoenix - Get Extended Memory\n");
            kbc_at_queue_add(dev, dev->mem[dev->mem_addr]);
            return 0;

        case 0xbb: /* Set Extended Memory */
            kbc_at_log("ATkbc: Phoenix - Set Extended Memory\n");
            dev->wantdata = 1;
            dev->state    = STATE_KBC_PARAM;
            return 0;

        case 0xbc: /* Get MultiKey Variable */
            kbc_at_log("ATkbc: Phoenix - Get MultiKey Variable\n");
            if (dev->mem_addr == 0)
                kbc_at_queue_add(dev, multikey_vars[dev->mem_addr]);
            else if (dev->mem_addr <= multikey_vars[dev->mem_addr])
                kbc_at_queue_add(dev, dev->mem[multikey_vars[dev->mem_addr]]);
            else
                kbc_at_queue_add(dev, 0xff);
            return 0;

        case 0xbd: /* Set MultiKey Variable */
            kbc_at_log("ATkbc: Phoenix - Set MultiKey Variable\n");
            dev->wantdata = 1;
            dev->state    = STATE_KBC_PARAM;
            return 0;

        case 0xc7: /* Set Port1 bits */
            kbc_at_log("ATkbc: Phoenix - Set Port1 bits\n");
            dev->wantdata  = 1;
            dev->state     = STATE_KBC_PARAM;
            return 0;

        case 0xc8: /* Clear Port1 bits */
            kbc_at_log("ATkbc: Phoenix - Clear Port1 bits\n");
            dev->wantdata  = 1;
            dev->state     = STATE_KBC_PARAM;
            return 0;

        case 0xc9: /* Set Port2 bits */
            kbc_at_log("ATkbc: Phoenix - Set Port2 bits\n");
            dev->wantdata  = 1;
            dev->state     = STATE_KBC_PARAM;
            return 0;

        case 0xca: /* Clear Port2 bits */
            kbc_at_log("ATkbc: Phoenix - Clear Port2 bits\n");
            dev->wantdata  = 1;
            dev->state     = STATE_KBC_PARAM;
            return 0;

        /* TODO: Handle these three commands properly - configurable
                 revision level and proper CPU bits. */
        case 0xd5: /* Read MultiKey code revision level */
            kbc_at_log("ATkbc: Phoenix - Read MultiKey code revision level\n");
            kbc_at_queue_add(dev, 0x04);
            kbc_at_queue_add(dev, 0x16);
            return 0;

        case 0xd6: /* Read Version Information */
            kbc_at_log("ATkbc: Phoenix - Read Version Information\n");
            kbc_at_queue_add(dev, 0x81);
            kbc_at_queue_add(dev, 0xac);
            return 0;

        case 0xd7: /* Read MultiKey model numbers */
            kbc_at_log("ATkbc: Phoenix - Read MultiKey model numbers\n");
            kbc_at_queue_add(dev, 0x02);
            kbc_at_queue_add(dev, 0x87);
            kbc_at_queue_add(dev, 0x02);
            return 0;

        default:
            break;
    }

    return write64_generic(dev, val);
}

static uint8_t
write64_siemens(void *priv, uint8_t val)
{
    atkbc_t *dev     = (atkbc_t *) priv;

    switch (val) {
        case 0x92: /*Siemens Award - 92 sent by PCD-2L BIOS*/
            kbc_at_log("Siemens Award - 92 sent by PCD-2L BIOS\n");
            return 0;

        case 0x94: /*Siemens Award - 94 sent by PCD-2L BIOS*/
            kbc_at_log("Siemens Award - 94 sent by PCD-2L BIOS\n");
            return 0;

        case 0x9a: /*Siemens Award - 9A sent by PCD-2L BIOS*/
            kbc_at_log("Siemens Award - 9A sent by PCD-2L BIOS\n");
            return 0;

        case 0x9c: /*Siemens Award - 9C sent by PCD-2L BIOS*/
            kbc_at_log("Siemens Award - 9C sent by PCD-2L BIOS\n");
            return 0;

        case 0xa9: /*Siemens Award - A9 sent by PCD-2L BIOS*/
            kbc_at_log("Siemens Award - A9 sent by PCD-2L BIOS\n");
            return 0;

        default:
            break;
    }

    return write64_ami(dev, val);
}

static uint8_t
write60_quadtel(void *priv, UNUSED(uint8_t val))
{
    const atkbc_t *dev = (atkbc_t *) priv;

    switch (dev->command) {
        case 0xcf: /*??? - sent by MegaPC BIOS*/
            kbc_at_log("ATkbc: ??? - sent by MegaPC BIOS\n");
            return 0;

        default:
            break;
    }

    return 1;
}

static uint8_t
write64_olivetti(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *) priv;

    switch (val) {
        case 0x80: /* Olivetti-specific command */
            /*
             * bit 7: bus expansion board present (M300) / keyboard unlocked (M290)
             * bits 4-6: ???
             * bit 3: fast ram check (if inactive keyboard works erratically)
             * bit 2: keyboard fuse present
             * bits 0-1: ???
             */
            kbc_delay_to_ob(dev, (0x0c | (is386 ? 0x00 : 0x80)) & 0xdf, 0, 0x00);
            dev->p1 = ((dev->p1 + 1) & 3) | (dev->p1 & 0xfc);
            return 0;

        default:
            break;
    }

    return write64_generic(dev, val);
}

static uint8_t
write64_quadtel(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *) priv;

    switch (val) {
        case 0xaf:
            kbc_at_log("ATkbc: bad KBC command AF\n");
            return 1;

        case 0xcf: /*??? - sent by MegaPC BIOS*/
            kbc_at_log("ATkbc: ??? - sent by MegaPC BIOS\n");
            dev->wantdata  = 1;
            dev->state     = STATE_KBC_PARAM;
            return 0;

        default:
            break;
    }

    return write64_generic(dev, val);
}

static uint8_t
write60_toshiba(void *priv, uint8_t val)
{
    const atkbc_t *dev = (atkbc_t *) priv;

    switch (dev->command) {
        case 0xb6: /* T3100e - set color/mono switch */
            kbc_at_log("ATkbc: T3100e - set color/mono switch\n");
            t3100e_mono_set(val);
            return 0;

        default:
            break;
    }

    return 1;
}

static uint8_t
write64_toshiba(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *) priv;

    switch (val) {
        case 0xaf:
            kbc_at_log("ATkbc: bad KBC command AF\n");
            return 1;

        case 0xb0: /* T3100e: Turbo on */
            kbc_at_log("ATkbc: T3100e: Turbo on\n");
            t3100e_turbo_set(1);
            return 0;

        case 0xb1: /* T3100e: Turbo off */
            kbc_at_log("ATkbc: T3100e: Turbo off\n");
            t3100e_turbo_set(0);
            return 0;

        case 0xb2: /* T3100e: Select external display */
            kbc_at_log("ATkbc: T3100e: Select external display\n");
            t3100e_display_set(0x00);
            return 0;

        case 0xb3: /* T3100e: Select internal display */
            kbc_at_log("ATkbc: T3100e: Select internal display\n");
            t3100e_display_set(0x01);
            return 0;

        case 0xb4: /* T3100e: Get configuration / status */
            kbc_at_log("ATkbc: T3100e: Get configuration / status\n");
            kbc_delay_to_ob(dev, t3100e_config_get(), 0, 0x00);
            return 0;

        case 0xb5: /* T3100e: Get colour / mono byte */
            kbc_at_log("ATkbc: T3100e: Get colour / mono byte\n");
            kbc_delay_to_ob(dev, t3100e_mono_get(), 0, 0x00);
            return 0;

        case 0xb6: /* T3100e: Set colour / mono byte */
            kbc_at_log("ATkbc: T3100e: Set colour / mono byte\n");
            dev->wantdata  = 1;
            dev->state     = STATE_KBC_PARAM;
            return 0;

        /* TODO: Toshiba KBC mode switching. */
        case 0xb7: /* T3100e: Emulate PS/2 keyboard */
        case 0xb8: /* T3100e: Emulate AT keyboard */
            dev->misc_flags &= ~FLAG_PS2;
            if (val == 0xb7) {
                kbc_at_log("ATkbc: T3100e: Emulate PS/2 keyboard\n");
                dev->misc_flags |= FLAG_PS2;
                kbc_at_do_poll = kbc_at_poll_ps2;
            } else {
                kbc_at_log("ATkbc: T3100e: Emulate AT keyboard\n");
                kbc_at_do_poll = kbc_at_poll_at;
            }
            return 0;

        case 0xbb: /* T3100e: Read 'Fn' key.
                      Return it for right Ctrl and right Alt; on the real
                      T3100e, these keystrokes could only be generated
                      using 'Fn'. */
            kbc_at_log("ATkbc: T3100e: Read 'Fn' key\n");
            if (keyboard_recv(0xb8) || /* Right Alt */
                keyboard_recv(0x9d))   /* Right Ctrl */
                kbc_delay_to_ob(dev, 0x04, 0, 0x00);
            else
                kbc_delay_to_ob(dev, 0x00, 0, 0x00);
            return 0;

        case 0xbc: /* T3100e: Reset Fn+Key notification */
            kbc_at_log("ATkbc: T3100e: Reset Fn+Key notification\n");
            t3100e_notify_set(0x00);
            return 0;

        case 0xc0: /* Read P1 */
            kbc_at_log("ATkbc: read P1\n");

            /* The T3100e returns all bits set except bit 6 which
             * is set by t3100e_mono_set() */
            dev->p1 = (t3100e_mono_get() & 1) ? 0xff : 0xbf;
            kbc_delay_to_ob(dev, dev->p1, 0, 0x00);
            return 0;

        default:
            break;
    }

    return write64_generic(dev, val);
}

static void
kbc_at_process_cmd(void *priv)
{
    atkbc_t *dev = (atkbc_t *) priv;
    int      bad = 1;
    uint8_t  mask;
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    uint8_t  cmd_ac_conv[16] = { 0x0b, 2, 3, 4, 5, 6, 7, 8, 9, 0x0a, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21 };

    if (dev->status & STAT_CD) {
        /* Controller command. */
        dev->wantdata  = 0;
        dev->state     = STATE_MAIN_IBF;

        /* Clear the keyboard controller queue. */
        kbc_at_queue_reset(dev);

        switch (dev->ib) {
            /* Read data from KBC memory. */
            case 0x20 ... 0x3f:
                kbc_delay_to_ob(dev, dev->mem[dev->ib], 0, 0x00);
                if (dev->ib == 0x20)
                    dev->pending++;
                break;

            /* Write data to KBC memory. */
            case 0x60 ... 0x7f:
                dev->wantdata  = 1;
                dev->state     = STATE_KBC_PARAM;
                break;

            case 0xaa: /* self-test */
                kbc_at_log("ATkbc: self-test\n");

                if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1) {
                    if (dev->state != STATE_RESET) {
                        kbc_at_log("ATkbc: self-test reinitialization\n");
                        /* Yes, the firmware has an OR, but we need to make sure to keep any forcibly lowered bytes lowered. */
                        /* TODO: Proper P1 implementation, with OR and AND flags in the machine table. */
                        dev->p1 = dev->p1 & 0xff;
                        write_p2(dev, 0x4b);
                        picintc(0x1000);
                        picintc(0x0002);
                    }

                    dev->status = (dev->status & 0x0f) | 0x60;

                    dev->mem[0x20] = 0x30;
                    dev->mem[0x22] = 0x0b;
                    dev->mem[0x25] = 0x02;
                    dev->mem[0x27] = 0xf8;
                    dev->mem[0x28] = 0xce;
                    dev->mem[0x29] = 0x0b;
                    dev->mem[0x30] = 0x0b;
                } else {
                    if (dev->state != STATE_RESET) {
                        kbc_at_log("ATkbc: self-test reinitialization\n");
                        /* Yes, the firmware has an OR, but we need to make sure to keep any forcibly lowered bytes lowered. */
                        /* TODO: Proper P1 implementation, with OR and AND flags in the machine table. */
                        dev->p1 = dev->p1 & 0xff;
                        write_p2(dev, 0xcf);
                        picintclevel(0x0002, &dev->irq_state);
                        dev->irq_state = 0;
                    }

                    dev->status = (dev->status & 0x0f) | 0x60;

                    dev->mem[0x20] = 0x10;
                    dev->mem[0x22] = 0x06;
                    dev->mem[0x25] = 0x01;
                    dev->mem[0x27] = 0xfb;
                    dev->mem[0x28] = 0xe0;
                    dev->mem[0x29] = 0x06;
                }

                dev->mem[0x21] = 0x01;
                dev->mem[0x2a] = 0x10;
                dev->mem[0x2b] = 0x20;
                dev->mem[0x2c] = 0x15;

                if (dev->ports[0] != NULL)
                    dev->ports[0]->out_new = -1;
                if (dev->ports[1] != NULL)
                    dev->ports[1]->out_new = -1;
                kbc_at_queue_reset(dev);

                kbc_at_queue_add(dev, 0x55);
                break;

            case 0xab: /* interface test */
                kbc_at_log("ATkbc: interface test\n");
                kbc_delay_to_ob(dev, 0x00, 0, 0x00); /*no error*/
                break;

            case 0xac: /* diagnostic dump */
                if (dev->misc_flags & FLAG_PS2) {
                    kbc_at_log("ATkbc: diagnostic dump\n");
                    dev->mem[0x30] = (dev->p1 & 0xf0) | 0x80;
                    dev->mem[0x31] = dev->p2;
                    dev->mem[0x32] = 0x00;    /* T0 and T1. */
                    dev->mem[0x33] = 0x00;    /* PSW - Program Status Word - always return 0x00 because we do not emulate this byte. */
                    /* 20 bytes in high nibble in set 1, low nibble in set 1, set 1 space format = 60 bytes. */
                    for (uint8_t i = 0; i < 20; i++) {
                        kbc_at_queue_add(dev, cmd_ac_conv[dev->mem[i + 0x20] >> 4]);
                        kbc_at_queue_add(dev, cmd_ac_conv[dev->mem[i + 0x20] & 0x0f]);
                        kbc_at_queue_add(dev, 0x39);
                    }
                }
                break;

            case 0xad: /* disable keyboard */
                kbc_at_log("ATkbc: disable keyboard\n");
                set_enable_kbd(dev, 0);
                break;

            case 0xae: /* enable keyboard */
                kbc_at_log("ATkbc: enable keyboard\n");
                set_enable_kbd(dev, 1);
                break;

            case 0xc7: /* set port1 bits */
                kbc_at_log("ATkbc: Phoenix - set port1 bits\n");
                dev->wantdata  = 1;
                dev->state     = STATE_KBC_PARAM;
                break;

            case 0xca: /* read keyboard mode */
                kbc_at_log("ATkbc: AMI - read keyboard mode\n");
                kbc_delay_to_ob(dev, dev->ami_flags, 0, 0x00);
                break;

            case 0xcb: /* set keyboard mode */
                kbc_at_log("ATkbc: AMI - set keyboard mode\n");
                dev->wantdata  = 1;
                dev->state     = STATE_KBC_PARAM;
                break;

            case 0xd0: /* read P2 */
                kbc_at_log("ATkbc: read P2\n");
                mask = 0xff;
                if ((kbc_ven != KBC_VEN_OLIVETTI) && !(dev->misc_flags & FLAG_PS2) && (dev->mem[0x20] & 0x10))
                    mask &= 0xbf;
                kbc_delay_to_ob(dev, ((dev->p2 & 0xfd) | mem_a20_key) & mask, 0, 0x00);
                break;

            case 0xd1: /* write P2 */
                kbc_at_log("ATkbc: write P2\n");
                dev->wantdata  = 1;
                dev->state     = STATE_KBC_PARAM;
                break;

            case 0xd2: /* write keyboard output buffer */
                kbc_at_log("ATkbc: write keyboard output buffer\n");
                dev->wantdata  = 1;
                dev->state     = STATE_KBC_PARAM;
                break;

            case 0xdd: /* disable A20 address line */
            case 0xdf: /* enable A20 address line */
                kbc_at_log("ATkbc: %sable A20\n", (dev->ib == 0xdd) ? "dis" : "en");
                write_p2_fast_a20(dev, (dev->p2 & 0xfd) | (dev->ib & 0x02));
                break;

            case 0xe0: /* read test inputs */
                kbc_at_log("ATkbc: read test inputs\n");
                kbc_delay_to_ob(dev, 0x00, 0, 0x00);
                break;

            default:
                /*
                 * Unrecognized controller command.
                 *
                 * If we have a vendor-specific handler, run
                 * that. Otherwise, or if that handler fails,
                 * log a bad command.
                 */
                if (dev->write64_ven)
                    bad = dev->write64_ven(dev, dev->ib);

                kbc_at_log(bad ? "ATkbc: bad controller command %02X\n" : "", dev->ib);
        }

        /* If the command needs data, remember the command. */
        if (dev->wantdata)
            dev->command = dev->ib;
    } else if (dev->wantdata) {
        /* Write data to controller. */
        dev->wantdata = 0;
        dev->state    = STATE_MAIN_IBF;

        switch (dev->command) {
            case 0x60 ... 0x7f:
                dev->mem[(dev->command & 0x1f) + 0x20] = dev->ib;
                if (dev->command == 0x60)
                    write_cmd(dev, dev->ib);
                break;

            case 0xa5: /* load security */
                if (dev->misc_flags & FLAG_PS2) {
                    kbc_at_log("ATkbc: load security (%02X)\n", dev->ib);

                    if (dev->ib != 0x00) {
                        dev->wantdata = 1;
                        dev->state = STATE_KBC_PARAM;
                    }
                }
                break;

            case 0xc7: /* set port1 bits */
                kbc_at_log("ATkbc: Phoenix - set port1 bits\n");
                dev->p1 |= dev->ib;
                break;

            case 0xd1: /* write P2 */
                kbc_at_log("ATkbc: write P2\n");
                /* Bit 2 of AMI flags is P22-P23 blocked (1 = yes, 0 = no),
                   discovered by reverse-engineering the AOpen Vi15G BIOS. */
                if (dev->ami_flags & 0x04) {
                    /* If keyboard controller lines P22-P23 are blocked,
                       we force them to remain unchanged. */
                    dev->ib &= ~0x0c;
                    dev->ib |= (dev->p2 & 0x0c);
                }
                write_p2(dev, dev->ib | 0x01);
                break;

            case 0xd2: /* write to keyboard output buffer */
                kbc_at_log("ATkbc: write to keyboard output buffer\n");
                kbc_delay_to_ob(dev, dev->ib, 0, 0x00);
                break;

            case 0xd3: /* write to auxiliary output buffer */
                kbc_at_log("ATkbc: write to auxiliary output buffer\n");
                kbc_delay_to_ob(dev, dev->ib, 2, 0x00);
                break;

            case 0xd4: /* write to auxiliary port */
                kbc_at_log("ATkbc: write to auxiliary port (%02X)\n", dev->ib);

                if (dev->ib == 0xbb)
                    break;

                if (strstr(machine_get_internal_name(), "pb41") != NULL)
                    cpu_override_dynarec = 1;

                if (dev->misc_flags & FLAG_PS2) {
                    set_enable_aux(dev, 1);
                    if ((dev->ports[1] != NULL) && (dev->ports[1]->priv != NULL)) {
                        dev->ports[1]->wantcmd = 1;
                        dev->ports[1]->dat = dev->ib;
                        dev->state         = STATE_SEND_AUX;
                    } else
                        kbc_delay_to_ob(dev, 0xfe, 2, 0x40);
                }
                break;

            default:
                /*
                 * Run the vendor-specific handler
                 * if we have one. Otherwise, or if
                 * it returns an error, log a bad
                 * controller command.
                 */
                if (dev->write60_ven)
                    bad = dev->write60_ven(dev, dev->ib);

                if (bad) {
                    kbc_at_log("ATkbc: bad controller command %02x data %02x\n", dev->command, dev->ib);
                }
        }
    }
}

static void
kbc_at_write(uint16_t port, uint8_t val, void *priv)
{
    atkbc_t *dev = (atkbc_t *) priv;
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    uint8_t fast_a20 = (kbc_ven != KBC_VEN_SIEMENS);

    kbc_at_log("ATkbc: [%04X:%08X] write(%04X) = %02X\n", CS, cpu_state.pc, port, val);

    switch (port) {
        case 0x60:
            dev->status &= ~STAT_CD;
            if (fast_a20 && dev->wantdata && (dev->command == 0xd1)) {
                kbc_at_log("ATkbc: write P2\n");

                /* Fast A20 - ignore all other bits. */
                write_p2_fast_a20(dev, (dev->p2 & 0xfd) | (val & 0x02));

                dev->wantdata  = 0;
                dev->state     = STATE_MAIN_IBF;
                return;
            }
            break;

        case 0x64:
            dev->status |= STAT_CD;
            if (fast_a20 && (val == 0xd1)) {
                kbc_at_log("ATkbc: write P2\n");
                dev->wantdata  = 1;
                dev->state     = STATE_KBC_PARAM;
                dev->command = 0xd1;
                return;
            } else if (fast_reset && ((val & 0xf0) == 0xf0)) {
                pulse_output(dev, val & 0x0f);

                dev->state     = STATE_MAIN_IBF;
                return;
            } else if (val == 0xae) {
                /* Fast track it because of the LG MultiNet. */
                kbc_at_log("ATkbc: enable keyboard\n");
                set_enable_kbd(dev, 1);

                dev->state     = STATE_MAIN_IBF;
                return;
            }
            break;

        default:
            break;
    }

    dev->ib = val;
    dev->status |= STAT_IFULL;
}

static uint8_t
kbc_at_read(uint16_t port, void *priv)
{
    atkbc_t *dev     = (atkbc_t *) priv;
    uint8_t  ret     = 0xff;

    if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1)
        cycles -= ISA_CYCLES(8);

    switch (port) {
        case 0x60:
            ret = dev->ob;
            dev->status &= ~STAT_OFULL;
            /* TODO: IRQ is only tied to OBF on the AT KBC, on the PS/2 KBC, it is controlled by a P2 bit.
                     This also means that in AT mode, the IRQ is level-triggered. */
            if (!(dev->misc_flags & FLAG_PS2))
                picintclevel(1 << 1, &dev->irq_state);
            if ((strstr(machine_get_internal_name(), "pb41") != NULL) && (cpu_override_dynarec == 1))
                cpu_override_dynarec = 0;
            break;

        case 0x64:
            ret = dev->status;
            break;

        default:
            kbc_at_log("ATkbc: read(%04x) invalid!\n",port);
            break;
    }

    kbc_at_log("ATkbc: [%04X:%08X] read (%04X) = %02X\n",  CS, cpu_state.pc, port, ret);

    return ret;
}

static void
kbc_at_reset(void *priv)
{
    atkbc_t *dev = (atkbc_t *) priv;
    uint8_t  kbc_ven = dev->flags & KBC_VEN_MASK;

    dev->status        = STAT_UNLOCKED;
    dev->mem[0x20]     = 0x01;
    dev->mem[0x20]    |= CCB_TRANSLATE;
    dev->command_phase = 0;

    /* Set up the correct Video Type bits. */
    if (!is286 || (kbc_ven == KBC_VEN_ACER))
        dev->p1 = video_is_mda() ? 0xb0 : 0xf0;
    else
        dev->p1 = video_is_mda() ? 0xf0 : 0xb0;
    kbc_at_log("ATkbc: P1 = %02x\n", dev->p1);

    /* Disabled both the keyboard and auxiliary ports. */
    set_enable_kbd(dev, 0);
    set_enable_aux(dev, 0);

    kbc_at_queue_reset(dev);

    dev->sc_or = 0;

    dev->ami_flags = ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1) ? 0x01 : 0x00;
    dev->misc_flags &= FLAG_PCI;

    if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1) {
        dev->misc_flags |= FLAG_PS2;
        kbc_at_do_poll = kbc_at_poll_ps2;
        picintc(0x1000);
        picintc(0x0002);
    } else {
        kbc_at_do_poll = kbc_at_poll_at;
        picintclevel(0x0002, &dev->irq_state);
        dev->irq_state = 0;
    }

    dev->misc_flags |= FLAG_CACHE;

    dev->p2 = 0xcd;
    if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1) {
        write_p2(dev, 0x4b);
    } else {
        /* The real thing writes CF and then AND's it with BF. */
        write_p2(dev, 0x8f);
    }

    /* Stage 1. */
    dev->status = (dev->status & 0x0f) | (dev->p1 & 0xf0);
}

static void
kbc_at_close(void *priv)
{
    atkbc_t *dev = (atkbc_t *) priv;
#ifdef OLD_CODE
    int max_ports = ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1) ? 2 : 1;
#else
    int max_ports = 2;
#endif

    /* Stop timers. */
    timer_disable(&dev->kbc_dev_poll_timer);
    timer_disable(&dev->kbc_poll_timer);

    for (int i = 0; i < max_ports; i++) {
        if (kbc_at_ports[i] != NULL) {
            free(kbc_at_ports[i]);
            kbc_at_ports[i] = NULL;
        }
    }

    free(dev);
}

void
kbc_at_handler(int set, void *priv)
{
    if (kbc_handler_set) {
        io_removehandler(0x0060, 1, kbc_at_read, NULL, NULL, kbc_at_write, NULL, NULL, priv);
        io_removehandler(0x0064, 1, kbc_at_read, NULL, NULL, kbc_at_write, NULL, NULL, priv);
    }

    kbc_handler_set = set;

    if (kbc_handler_set) {
        io_sethandler(0x0060, 1, kbc_at_read, NULL, NULL, kbc_at_write, NULL, NULL, priv);
        io_sethandler(0x0064, 1, kbc_at_read, NULL, NULL, kbc_at_write, NULL, NULL, priv);
    }
}

static void *
kbc_at_init(const device_t *info)
{
    atkbc_t *dev;
    int max_ports;

    dev = (atkbc_t *) calloc(1, sizeof(atkbc_t));

    dev->flags = info->local;

    dev->is_asic = !!(info->local & KBC_FLAG_IS_ASIC);

    video_reset(gfxcard[0]);
    kbc_at_reset(dev);

    if (info->flags & DEVICE_PCI)
        dev->misc_flags |= FLAG_PCI;

    kbc_handler_set = 0;
    kbc_at_handler(1, dev);

    timer_add(&dev->kbc_poll_timer, kbc_at_poll, dev, 1);
    timer_add(&dev->pulse_cb, pulse_poll, dev, 0);

    timer_add(&dev->kbc_dev_poll_timer, kbc_at_dev_poll, dev, 1);

    dev->write60_ven = NULL;
    dev->write64_ven = NULL;

    kbc_ami_revision = '8';
    kbc_award_revision = 0x42;

    switch (dev->flags & KBC_VEN_MASK) {
        case KBC_VEN_SIEMENS:
            kbc_ami_revision = '8';
            kbc_award_revision = 0x42;
            dev->write60_ven = write60_ami;
            dev->write64_ven = write64_siemens;
            break;

        case KBC_VEN_ACER:
        case KBC_VEN_GENERIC:
        case KBC_VEN_NCR:
        case KBC_VEN_IBM_PS1:
        case KBC_VEN_IBM:
        case KBC_VEN_COMPAQ:
            dev->write64_ven = write64_generic;
            break;

        case KBC_VEN_OLIVETTI:
            dev->write64_ven = write64_olivetti;
            break;

        case KBC_VEN_ALI:
            kbc_ami_revision = 'F';
            kbc_award_revision = 0x43;
            dev->write60_ven = write60_ami;
            dev->write64_ven = write64_ami;
            break;

        case KBC_VEN_TRIGEM_AMI:
            kbc_ami_revision = 'Z';
            dev->write60_ven = write60_ami;
            dev->write64_ven = write64_ami;
            break;

        case KBC_VEN_AMI:
            if ((dev->flags & KBC_TYPE_MASK) == KBC_TYPE_GREEN)
                kbc_ami_revision = '5';
            else if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1) {
                if (cpu_64bitbus)
                    kbc_ami_revision = 'R';
                else if (is486)
                    kbc_ami_revision = 'P';
                else
                    kbc_ami_revision = 'H';
            } else if (is386 && !is486) {
                if (cpu_16bitbus)
                    kbc_ami_revision = 'D';
                else
                    kbc_ami_revision = 'B';
            } else if (!is386)
                kbc_ami_revision = '8';
            else
                kbc_ami_revision = 'F';

            dev->write60_ven = write60_ami;
            dev->write64_ven = write64_ami;
            break;

        case KBC_VEN_PHOENIX:
            dev->write60_ven = write60_phoenix;
            dev->write64_ven = write64_phoenix;
            break;

        case KBC_VEN_QUADTEL:
            dev->write60_ven = write60_quadtel;
            dev->write64_ven = write64_quadtel;
            break;

        case KBC_VEN_TOSHIBA:
            dev->write60_ven = write60_toshiba;
            dev->write64_ven = write64_toshiba;
            break;

        default:
            break;
    }

#ifdef OLD_CODE
    max_ports = ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_1) ? 2 : 1;
#else
    max_ports = 2;
#endif

    for (int i = 0; i < max_ports; i++) {
        kbc_at_ports[i] = (kbc_at_port_t *) calloc(1, sizeof(kbc_at_port_t));
        kbc_at_ports[i]->out_new = -1;
    }

    dev->ports[0] = kbc_at_ports[0];
    dev->ports[1] = kbc_at_ports[1];

    /* The actual keyboard. */
    device_add(&keyboard_at_generic_device);

    fast_reset = 0x00;

    return dev;
}

const device_t keyboard_at_device = {
    .name          = "PC/AT Keyboard",
    .internal_name = "keyboard_at",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_ISA | KBC_VEN_GENERIC,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_siemens_device = {
    .name          = "PC/AT Keyboard",
    .internal_name = "keyboard_at",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_ISA | KBC_VEN_SIEMENS,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_ami_device = {
    .name          = "PC/AT Keyboard (AMI)",
    .internal_name = "keyboard_at_ami",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_ISA | KBC_VEN_AMI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_tg_ami_device = {
    .name          = "PC/AT Keyboard (TriGem AMI)",
    .internal_name = "keyboard_at_tg_ami",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_ISA | KBC_VEN_TRIGEM_AMI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_toshiba_device = {
    .name          = "PC/AT Keyboard (Toshiba)",
    .internal_name = "keyboard_at_toshiba",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_ISA | KBC_VEN_TOSHIBA,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_olivetti_device = {
    .name          = "PC/AT Keyboard (Olivetti)",
    .internal_name = "keyboard_at_olivetti",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_ISA | KBC_VEN_OLIVETTI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_ncr_device = {
    .name          = "PC/AT Keyboard (NCR)",
    .internal_name = "keyboard_at_ncr",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_ISA | KBC_VEN_NCR,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_compaq_device = {
    .name          = "PC/AT Keyboard (Compaq)",
    .internal_name = "keyboard_at_compaq",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_ISA | KBC_VEN_COMPAQ,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_device = {
    .name          = "PS/2 Keyboard",
    .internal_name = "keyboard_ps2",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_GENERIC,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ps1_device = {
    .name          = "PS/2 Keyboard (IBM PS/1)",
    .internal_name = "keyboard_ps2_ps1",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_IBM_PS1,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ps1_pci_device = {
    .name          = "PS/2 Keyboard (IBM PS/1)",
    .internal_name = "keyboard_ps2_ps1_pci",
    .flags         = DEVICE_KBC | DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_IBM_PS1,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_xi8088_device = {
    .name          = "PS/2 Keyboard (Xi8088)",
    .internal_name = "keyboard_ps2_xi8088",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_GENERIC,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ami_device = {
    .name          = "PS/2 Keyboard (AMI)",
    .internal_name = "keyboard_ps2_ami",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_AMI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_holtek_device = {
    .name          = "PS/2 Keyboard (Holtek)",
    .internal_name = "keyboard_ps2_holtek",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_AMI | KBC_FLAG_IS_ASIC,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_phoenix_device = {
    .name          = "PS/2 Keyboard (Phoenix)",
    .internal_name = "keyboard_ps2_phoenix",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_PHOENIX,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_tg_ami_device = {
    .name          = "PS/2 Keyboard (TriGem AMI)",
    .internal_name = "keyboard_ps2_tg_ami",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_TRIGEM_AMI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_mca_1_device = {
    .name          = "PS/2 Keyboard (IBM PS/2 MCA Type 1)",
    .internal_name = "keyboard_ps2_mca_1",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_IBM,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_mca_2_device = {
    .name          = "PS/2 Keyboard (IBM PS/2 MCA Type 2)",
    .internal_name = "keyboard_ps2_mca_2",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_2 | KBC_VEN_IBM,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_quadtel_device = {
    .name          = "PS/2 Keyboard (Quadtel/MegaPC)",
    .internal_name = "keyboard_ps2_quadtel",
    .flags         = DEVICE_KBC,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_QUADTEL,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_pci_device = {
    .name          = "PS/2 Keyboard",
    .internal_name = "keyboard_ps2_pci",
    .flags         = DEVICE_KBC | DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_GENERIC,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ami_pci_device = {
    .name          = "PS/2 Keyboard (AMI)",
    .internal_name = "keyboard_ps2_ami_pci",
    .flags         = DEVICE_KBC | DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_AMI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ali_pci_device = {
    .name          = "PS/2 Keyboard (ALi M5123/M1543C)",
    .internal_name = "keyboard_ps2_ali_pci",
    .flags         = DEVICE_KBC | DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_ALI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_intel_ami_pci_device = {
    .name          = "PS/2 Keyboard (AMI)",
    .internal_name = "keyboard_ps2_intel_ami_pci",
    .flags         = DEVICE_KBC | DEVICE_PCI,
    .local         = KBC_TYPE_GREEN | KBC_VEN_AMI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_tg_ami_pci_device = {
    .name          = "PS/2 Keyboard (TriGem AMI)",
    .internal_name = "keyboard_ps2_tg_ami_pci",
    .flags         = DEVICE_KBC | DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_TRIGEM_AMI,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_acer_pci_device = {
    .name          = "PS/2 Keyboard (Acer 90M002A)",
    .internal_name = "keyboard_ps2_acer_pci",
    .flags         = DEVICE_KBC | DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_ACER,
    .init          = kbc_at_init,
    .close         = kbc_at_close,
    .reset         = kbc_at_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
