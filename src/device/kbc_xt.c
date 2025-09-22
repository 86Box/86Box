/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the XT-style keyboard.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          EngiNerd, <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van kempen.
 *          Copyright 2020 EngiNerd.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/machine.h>
#include <86box/m_xt_t1000.h>
#include <86box/cassette.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/keyboard.h>

#define STAT_PARITY   0x80
#define STAT_RTIMEOUT 0x40
#define STAT_TTIMEOUT 0x20
#define STAT_LOCK     0x10
#define STAT_CD       0x08
#define STAT_SYSFLAG  0x04
#define STAT_IFULL    0x02
#define STAT_OFULL    0x01

/* Keyboard Types */
enum {
    KBD_TYPE_PC81 = 0,
    KBD_TYPE_PC82,
    KBD_TYPE_XT82,
    KBD_TYPE_XT86,
    KBD_TYPE_COMPAQ,
    KBD_TYPE_TANDY,
    KBD_TYPE_TOSHIBA,
    KBD_TYPE_VTECH,
    KBD_TYPE_OLIVETTI,
    KBD_TYPE_ZENITH,
    KBD_TYPE_PRAVETZ,
    KBD_TYPE_HYUNDAI,
    KBD_TYPE_FE2010,
    KBD_TYPE_XTCLONE
};

typedef struct xtkbd_t {
    int want_irq;
    int blocked;
    int tandy;

    uint8_t pa;
    uint8_t pb;
    uint8_t pd;
    uint8_t cfg;
    uint8_t clock;
    uint8_t key_waiting;
    uint8_t type;
    uint8_t pravetz_flags;
    uint8_t cpu_speed;

    pc_timer_t send_delay_timer;
} xtkbd_t;

static uint8_t key_queue[16];
static int     key_queue_start = 0;
static int     key_queue_end   = 0;
static int     is_tandy = 0;
static int     is_t1x00 = 0;
static int     is_amstrad = 0;

#ifdef ENABLE_KEYBOARD_XT_LOG
int keyboard_xt_do_log = ENABLE_KEYBOARD_XT_LOG;

static void
kbd_log(const char *fmt, ...)
{
    va_list ap;

    if (keyboard_xt_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define kbd_log(fmt, ...)
#endif

static uint8_t
get_fdd_switch_settings(void)
{

    uint8_t fdd_count = 0;

    for (uint8_t i = 0; i < FDD_NUM; i++) {
        if (fdd_get_flags(i))
            fdd_count++;
    }

    if (!fdd_count)
        return 0x00;
    else
        return ((fdd_count - 1) << 6) | 0x01;
}

static uint8_t
get_videomode_switch_settings(void)
{

    if (video_is_mda())
        return 0x30;
    else if (video_is_cga())
        return 0x20; /* 0x10 would be 40x25 */
    else
        return 0x00;
}

static void
kbd_poll(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *) priv;

    timer_advance_u64(&kbd->send_delay_timer, 1000 * TIMER_USEC);

    if (!(kbd->pb & 0x40) && (kbd->type != KBD_TYPE_TANDY))
        return;

    if (kbd->want_irq) {
        kbd->want_irq = 0;
        kbd->pa       = kbd->key_waiting;
        kbd->blocked  = 1;
        picint(2);
#ifdef ENABLE_KEYBOARD_XT_LOG
        kbd_log("XTkbd: kbd_poll(): keyboard_xt : take IRQ\n");
#endif
    }

    if ((key_queue_start != key_queue_end) && !kbd->blocked) {
        kbd->key_waiting = key_queue[key_queue_start];
        kbd_log("XTkbd: reading %02X from the key queue at %i\n",
                kbd->key_waiting, key_queue_start);
        key_queue_start = (key_queue_start + 1) & 0x0f;
        kbd->want_irq   = 1;
    }
}

static void
kbd_adddata(uint16_t val)
{
    /* Test for T1000 'Fn' key (Right Alt / Right Ctrl) */
    if (is_t1x00) {
        if (keyboard_recv(0x138) || keyboard_recv(0x11d)) { /* 'Fn' pressed */
            t1000_syskey(0x00, 0x04, 0x00);                 /* Set 'Fn' indicator */
            switch (val) {
                case 0x45: /* Num Lock => toggle numpad */
                    t1000_syskey(0x00, 0x00, 0x10);
                    break;
                case 0x47: /* Home => internal display */
                    t1000_syskey(0x40, 0x00, 0x00);
                    break;
                case 0x49: /* PgDn => turbo on */
                    t1000_syskey(0x80, 0x00, 0x00);
                    break;
                case 0x4D: /* Right => toggle LCD font */
                    t1000_syskey(0x00, 0x00, 0x20);
                    break;
                case 0x4F: /* End => external display */
                    t1000_syskey(0x00, 0x40, 0x00);
                    break;
                case 0x51: /* PgDn => turbo off */
                    t1000_syskey(0x00, 0x80, 0x00);
                    break;
                case 0x54: /* SysRQ => toggle window */
                    t1000_syskey(0x00, 0x00, 0x08);
                    break;

                default:
                    break;
            }
        } else
            t1000_syskey(0x04, 0x00, 0x00); /* Reset 'Fn' indicator */
    }

    key_queue[key_queue_end] = val;
    kbd_log("XTkbd: %02X added to key queue at %i\n",
            val, key_queue_end);
    key_queue_end = (key_queue_end + 1) & 0x0f;
}

void
kbd_adddata_process(uint16_t val, void (*adddata)(uint16_t val))
{
    uint8_t num_lock = 0;
    uint8_t shift_states = 0;

    if (!adddata)
        return;

    keyboard_get_states(NULL, &num_lock, NULL, NULL);
    shift_states = keyboard_get_shift() & STATE_LSHIFT;

    if (is_amstrad)
        num_lock = !num_lock;

    /* If NumLock is on, invert the left shift state so we can always check for
       the the same way flag being set (and with NumLock on that then means it
       is actually *NOT* set). */
    if (num_lock)
        shift_states ^= STATE_LSHIFT;

    switch (val) {
        case FAKE_LSHIFT_ON:
            /* If NumLock is on, fake shifts are sent when shift is *NOT* presed,
               if NumLock is off, fake shifts are sent when shift is pressed. */
            if (shift_states) {
                /* Send fake shift. */
                adddata(num_lock ? 0x2a : 0xaa);
            }
            break;
        case FAKE_LSHIFT_OFF:
            if (shift_states) {
                /* Send fake shift. */
                adddata(num_lock ? 0xaa : 0x2a);
            }
            break;
        default:
            adddata(val);
            break;
    }
}

static void
kbd_adddata_ex(uint16_t val)
{
    kbd_adddata_process(val, kbd_adddata);
}

static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *) priv;
    uint8_t  bit;
    uint8_t  set;
    uint8_t  new_clock;

    switch (port) {
        case 0x61: /* Keyboard Control Register (aka Port B) */
            if (!(val & 0x80) || (kbd->type == KBD_TYPE_HYUNDAI)) {
                new_clock = !!(val & 0x40);
                if (!kbd->clock && new_clock) {
                    key_queue_start = key_queue_end = 0;
                    kbd->want_irq                   = 0;
                    kbd->blocked                    = 0;
                    kbd_adddata(0xaa);
                }
            }

            kbd->pb = val;
            if (!(kbd->pb & 0x80) || (kbd->type == KBD_TYPE_HYUNDAI))
                kbd->clock = !!(kbd->pb & 0x40);
            ppi.pb = val;

            timer_process();

            if (((kbd->type == KBD_TYPE_PC81) || (kbd->type == KBD_TYPE_PC82) ||
                (kbd->type == KBD_TYPE_PRAVETZ)) && (cassette != NULL))
                pc_cas_set_motor(cassette, (kbd->pb & 0x08) == 0);

            speaker_update();

            speaker_gated  = val & 1;
            speaker_enable = val & 2;

            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, 2, val & 1);

            if (val & 0x80) {
                kbd->pa      = 0;
                kbd->blocked = 0;
                picintc(2);
            }

#ifdef ENABLE_KEYBOARD_XT_LOG
            if ((kbd->type == KBD_TYPE_PC81) || (kbd->type == KBD_TYPE_PC82) || (kbd->type == KBD_TYPE_PRAVETZ))
                kbd_log("XTkbd: Cassette motor is %s\n", !(val & 0x08) ? "ON" : "OFF");
#endif
            break;

        case 0x62: /* Switch Register (aka Port C) */
#ifdef ENABLE_KEYBOARD_XT_LOG
            if ((kbd->type == KBD_TYPE_PC81) || (kbd->type == KBD_TYPE_PC82) || (kbd->type == KBD_TYPE_PRAVETZ))
                kbd_log("XTkbd: Cassette IN is %i\n", !!(val & 0x10));
#endif
            if (kbd->type == KBD_TYPE_FE2010) {
                kbd_log("XTkbd: Switch register in is %02X\n", val);
                if (!(kbd->cfg & 0x08))
                    kbd->pd = (kbd->pd & 0x30) | (val & 0xcf);
            }
            break;

        case 0x63:
            if (kbd->type == KBD_TYPE_FE2010) {
                kbd_log("XTkbd: Configuration register in is %02X\n", val);
                if (!(kbd->cfg & 0x08))
                    kbd->cfg = val;
            }
            break;

        case 0xc0 ... 0xcf: /* Pravetz Flags */
            kbd_log("XTkbd: Port %02X out: %02X\n", port, val);
            if (kbd->type == KBD_TYPE_PRAVETZ) {
                bit                = (port >> 1) & 0x07;
                set                = (port & 0x01) << bit;
                kbd->pravetz_flags = (kbd->pravetz_flags & ~(1 << bit)) | set;
            }
            break;

        case 0x1f0:
            kbd_log("XTkbd: Port %04X out: %02X\n", port, val);
            if (kbd->type == KBD_TYPE_VTECH) {
                kbd->cpu_speed     = val;
                cpu_dynamic_switch(kbd->cpu_speed >> 7);
            }
            break;

        default:
            break;
    }
}

static uint8_t
kbd_read(uint16_t port, void *priv)
{
    const xtkbd_t *kbd = (xtkbd_t *) priv;
    uint8_t        ret = 0xff;

    switch (port) {
        case 0x60: /* Keyboard Data Register  (aka Port A) */
            if ((kbd->pb & 0x80) && ((kbd->type == KBD_TYPE_PC81) ||
                (kbd->type == KBD_TYPE_PC82) || (kbd->type == KBD_TYPE_PRAVETZ) ||
                (kbd->type == KBD_TYPE_XT82) || (kbd->type == KBD_TYPE_XT86) ||
                (kbd->type == KBD_TYPE_XTCLONE) || (kbd->type == KBD_TYPE_COMPAQ) ||
                (kbd->type == KBD_TYPE_ZENITH) || (kbd->type == KBD_TYPE_HYUNDAI) ||
                (kbd->type == KBD_TYPE_VTECH))) {
                if ((kbd->type == KBD_TYPE_PC81) || (kbd->type == KBD_TYPE_PC82) ||
                    (kbd->type == KBD_TYPE_XTCLONE) || (kbd->type == KBD_TYPE_COMPAQ) ||
                    (kbd->type == KBD_TYPE_PRAVETZ) || (kbd->type == KBD_TYPE_HYUNDAI))
                    ret = (kbd->pd & ~0x02) | (hasfpu ? 0x02 : 0x00);
                else if ((kbd->type == KBD_TYPE_XT82) || (kbd->type == KBD_TYPE_XT86) ||
                    (kbd->type == KBD_TYPE_VTECH))
                    /* According to Ruud on the PCem forum, this is supposed to
                       return 0xFF on the XT. */
                    ret = 0xff;
                else if (kbd->type == KBD_TYPE_ZENITH) {
                    /* Zenith Data Systems Z-151
                     * SW1 switch settings:
                     * bits 6-7: floppy drive number
                     * bits 4-5: video mode
                     * bit 2-3: base memory size
                     * bit 1: fpu enable
                     * bit 0: fdc enable
                     */
                    ret = get_fdd_switch_settings();

                    ret |= get_videomode_switch_settings();

                    /* Base memory size should always be 64k */
                    ret |= 0x0c;

                    if (hasfpu)
                        ret |= 0x02;
                }
            } else
                ret = kbd->pa;
            break;

        case 0x61: /* Keyboard Control Register (aka Port B) */
            ret = kbd->pb;
            break;

        case 0x62: /* Switch Register (aka Port C) */
            if (kbd->type == KBD_TYPE_FE2010) {
                if (kbd->pb & 0x04) /* PB2 */
                    ret = (kbd->pd & 0x0d) | (hasfpu ? 0x02 : 0x00);
                else
                    ret = kbd->pd >> 4;
            } else if ((kbd->type == KBD_TYPE_PC81) || (kbd->type == KBD_TYPE_PC82) ||
                (kbd->type == KBD_TYPE_PRAVETZ)) {
                if (kbd->pb & 0x04) /* PB2 */
                    switch (mem_size + isa_mem_size) {
                        case 64:
                        case 48:
                        case 32:
                        case 16:
                            ret = 0x00;
                            break;
                        default:
                            ret = (((mem_size + isa_mem_size) - 64) / 32) & 0x0f;
                            break;
                    }
                else
                    ret = (((mem_size + isa_mem_size) - 64) / 32) >> 4;
            } else if ((kbd->type == KBD_TYPE_OLIVETTI) ||
                       (kbd->type == KBD_TYPE_ZENITH)) {
                /* Olivetti M19 or Zenith Data Systems Z-151 */
                if (kbd->pb & 0x04) /* PB2 */
                    ret = kbd->pd & 0xbf;
                else
                    ret = kbd->pd >> 4;
            } else {
                if (kbd->pb & 0x08) /* PB3 */
                    ret = kbd->pd >> 4;
                else
                    ret = (kbd->pd & 0x0d) | (hasfpu ? 0x02 : 0x00);
            }
            ret |= (ppispeakon ? 0x20 : 0);

            /* This is needed to avoid error 131 (cassette error).
               This is serial read: bit 5 = clock, bit 4 = data, cassette header is 256 x 0xff. */
            if ((kbd->type == KBD_TYPE_PC81) || (kbd->type == KBD_TYPE_PC82) ||
                (kbd->type == KBD_TYPE_PRAVETZ)) {
                if (cassette == NULL)
                    ret |= (ppispeakon ? 0x10 : 0);
                else
                    ret |= (pc_cas_get_inp(cassette) ? 0x10 : 0);
            }

            if (kbd->type == KBD_TYPE_TANDY)
                ret |= (tandy1k_eeprom_read() ? 0x10 : 0);
            break;

        case 0x63: /* Keyboard Configuration Register (aka Port D) */
            if ((kbd->type == KBD_TYPE_XT82) || (kbd->type == KBD_TYPE_XT86) ||
                (kbd->type == KBD_TYPE_XTCLONE) || (kbd->type == KBD_TYPE_COMPAQ) ||
                (kbd->type == KBD_TYPE_TOSHIBA) || (kbd->type == KBD_TYPE_HYUNDAI) ||
                (kbd->type == KBD_TYPE_VTECH))
                ret = kbd->pd;
            break;

        case 0xc0: /* Pravetz Flags */
            if (kbd->type == KBD_TYPE_PRAVETZ)
                ret = kbd->pravetz_flags;
            kbd_log("XTkbd: Port %02X in : %02X\n", port, ret);
            break;

        case 0x1f0:
            if (kbd->type == KBD_TYPE_VTECH)
                ret = kbd->cpu_speed;
            kbd_log("XTkbd: Port %04X in : %02X\n", port, ret);
            break;

        default:
            break;
    }

    return ret;
}

static void
kbd_reset(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *) priv;

    kbd->want_irq      = 0;
    kbd->blocked       = 0;
    kbd->pa            = 0x00;
    kbd->pb            = 0x00;
    kbd->pravetz_flags = 0x00;

    keyboard_scan   = 1;

    key_queue_start = 0;
    key_queue_end   = 0;
}

void
keyboard_set_is_amstrad(int ams)
{
    is_amstrad = ams;
}

static void *
kbd_init(const device_t *info)
{
    xtkbd_t *kbd;

    kbd = (xtkbd_t *) calloc(1, sizeof(xtkbd_t));

    io_sethandler(0x0060, 4,
                  kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);
    keyboard_send = kbd_adddata_ex;
    kbd->type = info->local;
    if (kbd->type == KBD_TYPE_VTECH)
        kbd->cpu_speed = (!!cpu) << 2;
    kbd_reset(kbd);
    if (kbd->type == KBD_TYPE_PRAVETZ)
        io_sethandler(0x00c0, 16,
                      kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);
    if (kbd->type == KBD_TYPE_VTECH)
        io_sethandler(0x01f0, 1,
                      kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);

    key_queue_start = key_queue_end = 0;

    video_reset(gfxcard[0]);

    if ((kbd->type == KBD_TYPE_PC81) || (kbd->type == KBD_TYPE_PC82) ||
        (kbd->type == KBD_TYPE_PRAVETZ) || (kbd->type == KBD_TYPE_XT82) ||
        (kbd->type <= KBD_TYPE_XT86) || (kbd->type == KBD_TYPE_XTCLONE) ||
        (kbd->type == KBD_TYPE_COMPAQ) || (kbd->type == KBD_TYPE_TOSHIBA) ||
        (kbd->type == KBD_TYPE_OLIVETTI) || (kbd->type == KBD_TYPE_HYUNDAI) ||
        (kbd->type == KBD_TYPE_VTECH) || (kbd->type == KBD_TYPE_FE2010)) {
        /* DIP switch readout: bit set = OFF, clear = ON. */
        if (kbd->type == KBD_TYPE_OLIVETTI)
            /* Olivetti M19
             * Jumpers J1, J2 - monitor type.
             * 01 - mono (high-res)
             * 10 - color (low-res, disables 640x400x2 mode)
             * 00 - autoswitching
             */
            kbd->pd |= 0x00;
        else
            /* Switches 7, 8 - floppy drives. */
            kbd->pd = get_fdd_switch_settings();

        /* Switches 5, 6 - video card type */
        kbd->pd |= get_videomode_switch_settings();

        /* Switches 3, 4 - memory size. */
        if ((kbd->type == KBD_TYPE_XT86) || (kbd->type == KBD_TYPE_XTCLONE) ||
            (kbd->type == KBD_TYPE_HYUNDAI) || (kbd->type == KBD_TYPE_COMPAQ) ||
            (kbd->type == KBD_TYPE_TOSHIBA) || (kbd->type == KBD_TYPE_FE2010)) {
            switch (mem_size) {
                case 256:
                    kbd->pd |= 0x00;
                    break;
                case 512:
                    kbd->pd |= 0x04;
                    break;
                case 576:
                    kbd->pd |= 0x08;
                    break;
                case 640:
                default:
                    kbd->pd |= 0x0c;
                    break;
            }
        } else if ((kbd->type == KBD_TYPE_XT82) || (kbd->type == KBD_TYPE_VTECH)) {
            switch (mem_size) {
                case 64: /* 1x64k */
                    kbd->pd |= 0x00;
                    break;
                case 128: /* 2x64k */
                    kbd->pd |= 0x04;
                    break;
                case 192: /* 3x64k */
                    kbd->pd |= 0x08;
                    break;
                case 256: /* 4x64k */
                default:
                    kbd->pd |= 0x0c;
                    break;
            }
        } else if (kbd->type == KBD_TYPE_PC82) {
            switch (mem_size) {
#ifdef PC82_192K_3BANK
                case 192: /* 3x64k, not supported by stock BIOS due to bugs */
                    kbd->pd |= 0x08;
                    break;
#else
                case 192: /* 2x64k + 2x32k */
#endif
                case 64:  /* 4x16k */
                case 96:  /* 2x32k + 2x16k */
                case 128: /* 4x32k */
                case 160: /* 2x64k + 2x16k */
                case 224: /* 3x64k + 1x32k */
                case 256: /* 4x64k */
                default:
                    kbd->pd |= 0x0c;
                    break;
            }
        } else { /* really just the PC '81 */
            switch (mem_size) {
                case 16: /* 1x16k */
                    kbd->pd |= 0x00;
                    break;
                case 32: /* 2x16k */
                    kbd->pd |= 0x04;
                    break;
                case 48: /* 3x16k */
                    kbd->pd |= 0x08;
                    break;
                case 64: /* 4x16k */
                default:
                    kbd->pd |= 0x0c;
                    break;
            }
        }

        /* Switch 2 - 8087 FPU. */
        if (hasfpu)
            kbd->pd |= 0x02;
    } else if (kbd->type == KBD_TYPE_ZENITH) {
        /* Zenith Data Systems Z-151
         * SW2 switch settings:
         * bit 7: monitor frequency
         * bits 5-6: autoboot (00-11 resident monitor, 10 hdd, 01 fdd)
         * bits 0-4: installed memory
         */
        kbd->pd = 0x20;
        switch (mem_size) {
            case 128:
                kbd->pd |= 0x02;
                break;
            case 192:
                kbd->pd |= 0x04;
                break;
            case 256:
                kbd->pd |= 0x06;
                break;
            case 320:
                kbd->pd |= 0x08;
                break;
            case 384:
                kbd->pd |= 0x0a;
                break;
            case 448:
                kbd->pd |= 0x0c;
                break;
            case 512:
                kbd->pd |= 0x0e;
                break;
            case 576:
                kbd->pd |= 0x10;
                break;
            case 640:
            default:
                kbd->pd |= 0x12;
                break;
        }
    }

    timer_add(&kbd->send_delay_timer, kbd_poll, kbd, 1);

    is_tandy = (kbd->type == KBD_TYPE_TANDY);
    is_t1x00 = (kbd->type == KBD_TYPE_TOSHIBA);

    if (keyboard_type == KEYBOARD_TYPE_INTERNAL)
        keyboard_set_table(scancode_xt);
    else
        keyboard_add_device();

    is_amstrad = 0;

    return kbd;
}

static void
kbd_close(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *) priv;

    /* Stop the timer. */
    timer_disable(&kbd->send_delay_timer);

    /* Disable scanning. */
    keyboard_scan = 0;

    keyboard_send = NULL;

    io_removehandler(0x0060, 4,
                     kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);

    free(kbd);
}

const device_t kbc_pc_device = {
    .name          = "IBM PC Keyboard Controller (1981)",
    .internal_name = "kbc_pc",
    .flags         = 0,
    .local         = KBD_TYPE_PC81,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_pc82_device = {
    .name          = "IBM PC Keyboard Controller (1982)",
    .internal_name = "kbc_pc82",
    .flags         = 0,
    .local         = KBD_TYPE_PC82,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_pravetz_device = {
    .name          = "Pravetz Keyboard Controller",
    .internal_name = "kbc_pravetz",
    .flags         = 0,
    .local         = KBD_TYPE_PRAVETZ,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt_device = {
    .name          = "XT (1982) Keyboard Controller",
    .internal_name = "kbc_xt",
    .flags         = 0,
    .local         = KBD_TYPE_XT82,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt86_device = {
    .name          = "XT (1986) Keyboard Controller",
    .internal_name = "kbc_xt86",
    .flags         = 0,
    .local         = KBD_TYPE_XT86,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt_compaq_device = {
    .name          = "Compaq Portable Keyboard Controller",
    .internal_name = "kbc_xt_compaq",
    .flags         = 0,
    .local         = KBD_TYPE_COMPAQ,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_tandy_device = {
    .name          = "Tandy 1000 Keyboard Controller",
    .internal_name = "kbc_tandy",
    .flags         = 0,
    .local         = KBD_TYPE_TANDY,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt_t1x00_device = {
    .name          = "Toshiba T1x00 Keyboard Controller",
    .internal_name = "kbc_xt_t1x00",
    .flags         = 0,
    .local         = KBD_TYPE_TOSHIBA,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt_lxt3_device = {
    .name          = "VTech Laser Turbo XT Keyboard Controller",
    .internal_name = "kbc_xt_lxt",
    .flags         = 0,
    .local         = KBD_TYPE_VTECH,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt_olivetti_device = {
    .name          = "Olivetti XT Keyboard Controller",
    .internal_name = "kbc_xt_olivetti",
    .flags         = 0,
    .local         = KBD_TYPE_OLIVETTI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt_zenith_device = {
    .name          = "Zenith XT Keyboard Controller",
    .internal_name = "kbc_xt_zenith",
    .flags         = 0,
    .local         = KBD_TYPE_ZENITH,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt_hyundai_device = {
    .name          = "Hyundai XT Keyboard Controller",
    .internal_name = "kbc_xt_hyundai",
    .flags         = 0,
    .local         = KBD_TYPE_HYUNDAI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xt_fe2010_device = {
    .name          = "Faraday FE2010 XT Keyboard Controller",
    .internal_name = "kbc_xt_fe2010",
    .flags         = 0,
    .local         = KBD_TYPE_FE2010,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t kbc_xtclone_device = {
    .name          = "XT (Clone) Keyboard Controller",
    .internal_name = "kbc_xtclone",
    .flags         = 0,
    .local         = KBD_TYPE_XTCLONE,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
