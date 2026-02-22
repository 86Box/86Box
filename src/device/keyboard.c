/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          General keyboard driver interface.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2015-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/plat.h>

#include "cpu.h"

uint16_t     scancode_map[768]        = { 0 };
uint16_t     scancode_config_map[768] = { 0 };

int          keyboard_scan;

typedef struct keyboard_t {
    const device_t *device;
} keyboard_t;

int          keyboard_type = 0;

static const device_t keyboard_internal_device = {
    .name          = "Internal",
    .internal_name = "internal",
    .flags         = 0,
    .local         = KEYBOARD_TYPE_INTERNAL,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static keyboard_t keyboard_devices[] = {
    // clang-format off
    { &keyboard_internal_device        },
    { &keyboard_pc_xt_device           },
    { &keyboard_at_device              },
    { &keyboard_ax_device              },
    { &keyboard_ps2_device             },
    { &keyboard_ps55_device            },
    { NULL                             }
    // clang-format on
};

#ifdef ENABLE_KBC_AT_LOG
int kbc_at_do_log = ENABLE_KBC_AT_LOG;

static void
kbc_at_log(const char* fmt, ...)
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

void (*keyboard_send)(uint16_t val);

static int recv_key[768] = { 0 }; /* keyboard input buffer */
static int recv_key_ui[768] = { 0 }; /* keyboard input buffer */
static int oldkey[768];
#if 0
static int keydelay[768];
#endif
static scancode *scan_table; /* scancode table for keyboard */

static volatile uint8_t caps_lock    = 0;
static volatile uint8_t num_lock     = 0;
static volatile uint8_t scroll_lock  = 0;
static volatile uint8_t kana_lock    = 0;
static volatile uint8_t kbd_in_reset = 0;
static uint8_t shift                 = 0;

static int key5576mode = 0;

typedef struct {
    const uint16_t sc;
    const uint8_t mk[4];
    const uint8_t brk[4];
} scconvtbl;

static scconvtbl scconv55_8a[18 + 1] =
{
    // clang-format off
      {.sc = 0x02 , .mk = {    0x48 }, .brk = {       0 } }, /* '1' -> 'Clear/ /SysRq' */
      {.sc = 0x03 , .mk = {    0x49 }, .brk = {       0 } }, /* '2' -> '終了 (Exit)' */
      {.sc = 0x04 , .mk = {    0x46 }, .brk = {       0 } }, /* '3' -> 'メッセージ (Message)/ /応答 (Respond)' */
      {.sc = 0x05 , .mk = {    0x44 }, .brk = {       0 } }, /* '4' -> 'サイズ変換 (Change Size)/ /横倍角 (2x Width)' */
      {.sc = 0x06 , .mk = {    0x42 }, .brk = {       0 } }, /* '5' -> '単語登録 (Register Word)/ /再交換 (Re-change)' */
      {.sc = 0x07 , .mk = {    0x43 }, .brk = {       0 } }, /* '6' -> '漢字 (Kanji)/ /番号 (Number)' */
      {.sc = 0x08 , .mk = {    0x40 }, .brk = {       0 } }, /* '7' -> '取消 (Cancel)' */
      {.sc = 0x09 , .mk = {    0x51 }, .brk = {       0 } }, /* '8' -> 'コピー (Copy)/ /移動 (Move)' */
      {.sc = 0x3d , .mk = {    0x76 }, .brk = {       0 } }, /* 'F3' -> 'Cr Bnk/領域呼出 (Call Range)/All Cr/登録 (Register)' */
      {.sc = 0x3e , .mk = {    0x77 }, .brk = {       0 } }, /* 'F4' -> '割込み (Interrupt)' */
      {.sc = 0x3f , .mk = {    0x78 }, .brk = {       0 } }, /* 'F5' -> 'UF1' */
      {.sc = 0x40 , .mk = {    0x79 }, .brk = {       0 } }, /* 'F6' -> 'UF2' */
      {.sc = 0x41 , .mk = {    0x7a }, .brk = {       0 } }, /* 'F7' -> 'UF3' */
      {.sc = 0x42 , .mk = {    0x7b }, .brk = {       0 } }, /* 'F8' -> 'UF4' */
      {.sc = 0x43 , .mk = {    0x7c }, .brk = {       0 } }, /* 'F9' -> 'EOF/Erase/ErInp' */
      {.sc = 0x44 , .mk = {    0x7d }, .brk = {       0 } }, /* 'F10' -> 'Attn/ /CrSel' */
      {.sc = 0x57 , .mk = {    0x7e }, .brk = {       0 } }, /* 'F11' -> 'PA1/ /DvCncl' */
      {.sc = 0x58 , .mk = {    0x7f }, .brk = {       0 } }, /* 'F12' -> 'PA2/ /PA3' */
      {.sc = 0 , .mk = { 0 }, .brk = { 0 } } /* end */
    // clang-format on
};

void
keyboard_init(void)
{
    num_lock     = 0;
    caps_lock    = 0;
    scroll_lock  = 0;
    kana_lock    = 0;
    shift        = 0;
    kbd_in_reset = 0;

    memset(recv_key, 0x00, sizeof(recv_key));
    memset(recv_key_ui, 0x00, sizeof(recv_key));
    memset(oldkey, 0x00, sizeof(recv_key));
#if 0
    memset(key_delay, 0x00, sizeof(recv_key));
#endif

    keyboard_scan = 1;
    scan_table    = NULL;

    memset(keyboard_set3_flags, 0x00, sizeof(keyboard_set3_flags));
    keyboard_set3_all_repeat = 0;
    keyboard_set3_all_break  = 0;
}

void
keyboard_set_table(const scancode *ptr)
{
    scan_table = (scancode *) ptr;
}

static uint8_t
fake_shift_needed(uint16_t scan)
{
    switch (scan) {
        case 0x137:    /* Yes, Print Screen requires the fake shifts. */
        case 0x147:
        case 0x148:
        case 0x149:
        case 0x14a:
        case 0x14b:
        case 0x14d:
        case 0x14f:
        case 0x150:
        case 0x151:
        case 0x152:
        case 0x153:
            return 1;
        default:
            return 0;
    }
}

void
key_process(uint16_t scan, int down)
{
    const scancode *codes = scan_table;
    int             c;

    if (!codes)
        return;

    if (!keyboard_scan || (keyboard_send == NULL))
        return;

    scan = scancode_config_map[scan];

    oldkey[scan] = down;

    kbc_at_log("Key %04X,%d in process\n", scan, down);

    c = 0;
    /* According to Japanese DOS K3.3 manual (N:SC18-2194-1),
      IBM 5576-002, -003 keyboards have the one-time key conversion mode
      that emulates 18 out of 131 keys on IBM 5576-001 keyboard.
      It is triggered by pressing L-Shift (⇧) + L-Ctrl + R-Alt (前面キー)
      when the scancode set is 82h or 8ah.
    */
    if (key5576mode) {
        int i = 0;
        if (down) {
            while (scconv55_8a[i].sc != 0) {
                if (scconv55_8a[i].sc == scan) {
                    while (scconv55_8a[i].mk[c] != 0)
                        keyboard_send(scconv55_8a[i].mk[c++]);
                }
                i++;
            }
        }
        /* Do and exit the 5576-001 emulation when a key is pressed other than trigger keys. */
        if (scan != 0x1d && scan != 0x2a && scan != 0x138) {
            if (!down) {
                key5576mode = 0;
                kbc_at_log("5576-001 key emulation disabled.\n");
            }
            /* If the key is found in the table, the scancode has been sent.
               Or else, do nothing. */
            return;
        }
    }

    if (down && (codes[scan].mk[0] == 0))
        return;

    if (!down && (codes[scan].brk[0] == 0))
        return;

    /* TODO: The keyboard controller needs to report the AT flag to us here. */
    if (is286 && ((keyboard_mode & 3) == 3)) {
        if (!keyboard_set3_all_break && !down && !(keyboard_set3_flags[codes[scan].mk[0]] & 2))
            return;
    }

    c = 0;
    if (down) {
        /* Send the special code indicating an opening fake shift might be needed. */
        if (fake_shift_needed(scan))
            keyboard_send(0x100);
        while (codes[scan].mk[c] != 0)
            keyboard_send(codes[scan].mk[c++]);
    } else {
        while (codes[scan].brk[c] != 0)
            keyboard_send(codes[scan].brk[c++]);
        /* Send the special code indicating a closing fake shift might be needed. */
        if (fake_shift_needed(scan))
            keyboard_send(0x101);
    }

    /* Enter the 5576-001 emulation mode. */
    if (keyboard_mode == 0x8a && down && ((keyboard_get_shift() & 0x43) == 0x43))
    {
        key5576mode = 1;
        kbc_at_log("5576-001 key emulation enabled.\n");
    }
}

/* Handle a keystroke event from the UI layer. */
void
keyboard_input(int down, uint16_t scan)
{
    if (kbd_in_reset)
        return;

    /* Special case for E1 1D, translate it to 0100 - special case. */
    if ((scan >> 8) == 0xe1) {
        if ((scan & 0xff) == 0x1d)
            scan = 0x0100;
    /* Translate E0 xx scan codes to 01xx because we use 512-byte arrays for states
       and scan code sets. */
    } else if ((scan >> 8) == 0xe0) {
        scan &= 0x00ff;
        scan |= 0x0100; /* extended key code */
    } else if ((scan >> 8) != 0x01)
        scan &= 0x00ff; /* we can receive a scan code whose upper byte is 0x01,
                           this means we're the Win32 version running on windows
                           that already sends us preprocessed scan codes, which
                           means we then use the scan code as is, and need to
                           make sure we do not accidentally strip that upper byte */

    if (recv_key[scan & 0x1ff] ^ down) {
        if (down) {
            switch (scan & 0x1ff) {
                case 0x01d: /* Left Ctrl */
                    shift |= 0x01;
                    break;
                case 0x11d: /* Right Ctrl */
                    shift |= 0x10;
                    break;
                case 0x02a: /* Left Shift */
                    shift |= 0x02;
                    break;
                case 0x036: /* Right Shift */
                    shift |= 0x20;
                    break;
                case 0x038: /* Left Alt */
                    shift |= 0x04;
                    break;
                case 0x138: /* Right Alt */
                    shift |= 0x40;
                    break;
                case 0x15b: /* Left Windows */
                    shift |= 0x08;
                    break;
                case 0x15c: /* Right Windows */
                    shift |= 0x80;
                    break;

                default:
                    break;
            }
        } else {
            switch (scan & 0x1ff) {
                case 0x01d: /* Left Ctrl */
                    shift &= ~0x01;
                    break;
                case 0x11d: /* Right Ctrl */
                    shift &= ~0x10;
                    break;
                case 0x02a: /* Left Shift */
                    shift &= ~0x02;
                    break;
                case 0x036: /* Right Shift */
                    shift &= ~0x20;
                    break;
                case 0x038: /* Left Alt */
                    shift &= ~0x04;
                    break;
                case 0x138: /* Right Alt */
                    shift &= ~0x40;
                    break;
                case 0x15b: /* Left Windows */
                    shift &= ~0x08;
                    break;
                case 0x15c: /* Right Windows */
                    shift &= ~0x80;
                    break;
                case 0x03a: /* Caps Lock */
                    if (!(machine_has_bus(machine, MACHINE_AT) > 0))
                        caps_lock ^= 1;
                    break;
                case 0x045:
                    if (!(machine_has_bus(machine, MACHINE_AT) > 0))
                        num_lock ^= 1;
                    break;
                case 0x046:
                    if (!(machine_has_bus(machine, MACHINE_AT) > 0))
                        scroll_lock ^= 1;
                    break;

                default:
                    break;
            }
        }
    }

    /* kbc_at_log("Received scan code: %03X (%s)\n", scan & 0x1ff, down ? "down" : "up"); */
    recv_key_ui[scan & 0x1ff] = down;

    if (mouse_capture || !kbd_req_capture || (video_fullscreen && !fullscreen_ui_visible)) {
        recv_key[scan & 0x1ff] = down;
        key_process(scan & 0x1ff, down);
    }
}

void
keyboard_all_up(void)
{
    for (unsigned short i = 0; i < 0x200; i++) {
        if (recv_key_ui[i])
            recv_key_ui[i] = 0;

        if (recv_key[i]) {
            recv_key[i] = 0;
            key_process(i, 0);
        }
    }
}

void
keyboard_set_in_reset(uint8_t in_reset)
{
    kbd_in_reset = in_reset;
}

uint8_t
keyboard_get_in_reset(void)
{
    return kbd_in_reset;
}

static uint8_t
keyboard_do_break(uint16_t scan)
{
    const scancode *codes = scan_table;

    /* TODO: The keyboard controller needs to report the AT flag to us here. */
    if (is286 && ((keyboard_mode & 3) == 3)) {
        if (!keyboard_set3_all_break && !recv_key[scan] && !(keyboard_set3_flags[codes[scan].mk[0]] & 2))
            return 0;
        else
            return 1;
    } else
        return 1;
}

/* Also called by the emulated keyboard controller to update the states of
   Caps Lock, Num Lock, and Scroll Lock when receving the "Set keyboard LEDs"
   command. */
void
keyboard_update_states(uint8_t cl, uint8_t nl, uint8_t sl, uint8_t kl)
{
    caps_lock   = cl;
    num_lock    = nl;
    scroll_lock = sl;
    kana_lock   = kl;
}

uint8_t
keyboard_get_shift(void)
{
    return shift;
}

void
keyboard_get_states(uint8_t *cl, uint8_t *nl, uint8_t *sl, uint8_t *kl)
{
    if (cl)
        *cl = caps_lock;
    if (nl)
        *nl = num_lock;
    if (sl)
        *sl = scroll_lock;
    if (kl)
        *kl = kana_lock;
}

/* Called by the UI to update the states of Caps Lock, Num Lock, and Scroll Lock. */
void
keyboard_set_states(uint8_t cl, uint8_t nl, uint8_t sl)
{
    const scancode *codes = scan_table;

    int i;

    if (caps_lock != cl) {
        i = 0;
        while (codes[0x03a].mk[i] != 0)
            keyboard_send(codes[0x03a].mk[i++]);
        if (keyboard_do_break(0x03a)) {
            i = 0;
            while (codes[0x03a].brk[i] != 0)
                keyboard_send(codes[0x03a].brk[i++]);
        }
    }

    if (num_lock != nl) {
        i = 0;
        while (codes[0x045].mk[i] != 0)
            keyboard_send(codes[0x045].mk[i++]);
        if (keyboard_do_break(0x045)) {
            i = 0;
            while (codes[0x045].brk[i] != 0)
                keyboard_send(codes[0x045].brk[i++]);
        }
    }

    if (scroll_lock != sl) {
        i = 0;
        while (codes[0x046].mk[i] != 0)
            keyboard_send(codes[0x046].mk[i++]);
        if (keyboard_do_break(0x046)) {
            i = 0;
            while (codes[0x046].brk[i] != 0)
                keyboard_send(codes[0x046].brk[i++]);
        }
    }

    keyboard_update_states(cl, nl, sl, kana_lock);
}

int
keyboard_recv(uint16_t key)
{
    return recv_key[key];
}

int
keyboard_recv_ui(uint16_t key)
{
    return recv_key_ui[key];
}

/* Do we have Control-Alt-PgDn in the keyboard buffer? */
int
keyboard_isfsenter(void)
{
    return ((recv_key_ui[0x01d] || recv_key_ui[0x11d]) && (recv_key_ui[0x038] || recv_key_ui[0x138]) && (recv_key_ui[0x049] || recv_key_ui[0x149]));
}

int
keyboard_isfsenter_up(void)
{
    return (!recv_key_ui[0x01d] && !recv_key_ui[0x11d] && !recv_key_ui[0x038] && !recv_key_ui[0x138] && !recv_key_ui[0x049] && !recv_key_ui[0x149]);
}

/* Do we have Control-Alt-PgDn in the keyboard buffer? */
int
keyboard_isfsexit(void)
{
    return ((recv_key_ui[0x01d] || recv_key_ui[0x11d]) && (recv_key_ui[0x038] || recv_key_ui[0x138]) && (recv_key_ui[0x051] || recv_key_ui[0x151]));
}

int
keyboard_isfsexit_up(void)
{
    return (!recv_key_ui[0x01d] && !recv_key_ui[0x11d] && !recv_key_ui[0x038] && !recv_key_ui[0x138] && !recv_key_ui[0x051] && !recv_key_ui[0x151]);
}

/* This is so we can disambiguate scan codes that would otherwise conflict and get
   passed on incorrectly. */
uint16_t
convert_scan_code(uint16_t scan_code)
{
    if ((scan_code & 0xff00) == 0xe000)
        scan_code = (scan_code & 0xff) | 0x0100;

    if (scan_code == 0xE11D)
        scan_code = 0x0100;
    /* E0 00 is sent by some USB keyboards for their special keys, as it is an
       invalid scan code (it has no untranslated set 2 equivalent), we mark it
       appropriately so it does not get passed through. */
    else if ((scan_code > 0x01FF) || (scan_code == 0x0100))
        scan_code = 0xFFFF;

    return scan_code;
}

const char *
keyboard_get_name(int keyboard)
{
    return (keyboard_devices[keyboard].device->name);
}

const char *
keyboard_get_internal_name(int keyboard)
{
    return device_get_internal_name(keyboard_devices[keyboard].device);
}

int
keyboard_get_from_internal_name(char *s)
{
    int c = 0;

    while (keyboard_devices[c].device != NULL) {
        if (!strcmp((char *) keyboard_devices[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

int
keyboard_has_config(int keyboard)
{
    if (keyboard_devices[keyboard].device == NULL)
        return 0;

    return (keyboard_devices[keyboard].device->config ? 1 : 0);
}

const device_t *
keyboard_get_device(int keyboard)
{
    return (keyboard_devices[keyboard].device);
}

/* Return number of MOUSE types we know about. */
int
keyboard_get_ndev(void)
{
    return ((sizeof(keyboard_devices) / sizeof(keyboard_t)) - 1);
}

void
keyboard_add_device(void)
{
    device_add(keyboard_devices[keyboard_type].device);
}
