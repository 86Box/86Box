/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          xkbcommon keyboard input module.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2023 RichardG.
 */
extern "C" {
#include <xkbcommon/xkbcommon.h>
};

#include <unordered_map>
#include <QtDebug>
#include "evdev_keyboard.hpp"

#define IS_DEC_DIGIT(c) (((c) >= '0') && ((c) <= '9'))
#define IS_HEX_DIGIT(c) (IS_DEC_DIGIT(c) || (((c) >= 'A') && ((c) <= 'F')) || (((c) >= 'a') && ((c) <= 'f')))

static std::unordered_map<std::string, uint16_t> xkb_keycodes = {
    { "ESC",  0x01  },
    { "AE01", 0x02  },
    { "AE02", 0x03  },
    { "AE03", 0x04  },
    { "AE04", 0x05  },
    { "AE05", 0x06  },
    { "AE06", 0x07  },
    { "AE07", 0x08  },
    { "AE08", 0x09  },
    { "AE09", 0x0a  },
    { "AE10", 0x0b  },
    { "AE11", 0x0c  },
    { "AE12", 0x0d  },
    { "BKSP", 0x0e  },

    { "TAB",  0x0f  },
    { "AD01", 0x10  },
    { "AD02", 0x11  },
    { "AD03", 0x12  },
    { "AD04", 0x13  },
    { "AD05", 0x14  },
    { "AD06", 0x15  },
    { "AD07", 0x16  },
    { "AD08", 0x17  },
    { "AD09", 0x18  },
    { "AD10", 0x19  },
    { "AD11", 0x1a  },
    { "AD12", 0x1b  },
    { "RTRN", 0x1c  },
    { "LNFD", 0x1c  }, /* linefeed => Enter */

    { "LCTL", 0x1d  },
    { "CTRL", 0x1d  },
    { "AC01", 0x1e  },
    { "AC02", 0x1f  },
    { "AC03", 0x20  },
    { "AC04", 0x21  },
    { "AC05", 0x22  },
    { "AC06", 0x23  },
    { "AC07", 0x24  },
    { "AC08", 0x25  },
    { "AC09", 0x26  },
    { "AC10", 0x27  },
    { "AC11", 0x28  },

    { "TLDE", 0x29  },
    { "AE00", 0x29  }, /* alias of TLDE on keycodes/xfree86 (i.e. X11 forwarding) */
    { "LFSH", 0x2a  },
    { "BKSL", 0x2b  },
    { "AC12", 0x2b  },
    { "AB01", 0x2c  },
    { "AB02", 0x2d  },
    { "AB03", 0x2e  },
    { "AB04", 0x2f  },
    { "AB05", 0x30  },
    { "AB06", 0x31  },
    { "AB07", 0x32  },
    { "AB08", 0x33  },
    { "AB09", 0x34  },
    { "AB10", 0x35  },
    { "RTSH", 0x36  },

    { "KPMU", 0x37  },
    { "LALT", 0x38  },
    { "ALT",  0x38  },
    { "SPCE", 0x39  },
    { "CAPS", 0x3a  },
    { "FK01", 0x3b  },
    { "FK02", 0x3c  },
    { "FK03", 0x3d  },
    { "FK04", 0x3e  },
    { "FK05", 0x3f  },
    { "FK06", 0x40  },
    { "FK07", 0x41  },
    { "FK08", 0x42  },
    { "FK09", 0x43  },
    { "FK10", 0x44  },

    { "NMLK", 0x45  },
    { "SCLK", 0x46  },
    { "FK14", 0x46  }, /* F14 => Scroll Lock (for Apple keyboards) */
    { "KP7",  0x47  },
    { "KP8",  0x48  },
    { "KP9",  0x49  },
    { "KPSU", 0x4a  },
    { "KP4",  0x4b  },
    { "KP5",  0x4c  },
    { "KP6",  0x4d  },
    { "KPAD", 0x4e  },
    { "KP1",  0x4f  },
    { "KP2",  0x50  },
    { "KP3",  0x51  },
    { "KP0",  0x52  },
    { "KPDL", 0x53  },

    { "LSGT", 0x56  },
    { "FK11", 0x57  },
    { "FK12", 0x58  },
    { "FK16", 0x5d  }, /* F16 => F13 */
    { "FK17", 0x5e  }, /* F17 => F14 */
    { "FK18", 0x5f  }, /* F18 => F15 */

    /* Japanese keys. */
    { "JPCM", 0x5c  }, /* Num, */
    { "KPDC", 0x5c  },
    { "HKTG", 0x70  }, /* hiragana-katakana toggle */
    { "AB11", 0x73  }, /* \_ and Brazilian /? */
    { "HZTG", 0x76  }, /* hankaku-zenkaku toggle */
    { "HIRA", 0x77  },
    { "KATA", 0x78  },
    { "HENK", 0x79  },
    { "KANA", 0x79  }, /* kana => henkan (for Apple keyboards) */
    { "MUHE", 0x7b  },
    { "EISU", 0x7b  }, /* eisu => muhenkan (for Apple keyboards) */
    { "AE13", 0x7d  }, /* \| */
    { "KPPT", 0x7e  }, /* Brazilian Num. */
    { "I06",  0x7e  }, /* alias of KPPT on keycodes/xfree86 (i.e. X11 forwarding) */

    /* Korean keys. */
    { "HJCV", 0xf1  }, /* hancha toggle */
    { "HNGL", 0xf2  }, /* latin toggle */

    { "KPEN", 0x11c },
    { "RCTL", 0x11d },
    { "KPDV", 0x135 },
    { "PRSC", 0x137 },
    { "SYRQ", 0x137 },
    { "FK13", 0x137 }, /* F13 => SysRq (for Apple keyboards) */
    { "RALT", 0x138 },
    { "ALGR", 0x138 },
    { "LVL3", 0x138 }, /* observed on TigerVNC with AltGr-enabled layout */
    { "PAUS", 0x145 },
    { "FK15", 0x145 }, /* F15 => Pause (for Apple keyboards) */
    { "BRK",  0x145 },
    { "HOME", 0x147 },
    { "UP",   0x148 },
    { "PGUP", 0x149 },
    { "LEFT", 0x14b },
    { "RGHT", 0x14d },
    { "END",  0x14f },
    { "DOWN", 0x150 },
    { "PGDN", 0x151 },
    { "INS",  0x152 },
    { "DELE", 0x153 },

    { "LWIN", 0x15b },
    { "WIN",  0x15b },
    { "LMTA", 0x15b },
    { "META", 0x15b },
    { "RWIN", 0x15c },
    { "RMTA", 0x15c },
    { "MENU", 0x15d },
    { "COMP", 0x15d }, /* Compose as Menu */

    /* Multimedia keys. Same notes as evdev_keyboard apply here. */
    { "KPEQ", 0x59  }, /* Num= */
    { "FRNT", 0x101 }, /* # Logitech Task Select */
    { "UNDO", 0x108 }, /* # */
    { "PAST", 0x10a }, /* # Paste */
    { "FIND", 0x112 }, /* # Logitech */
    { "CUT",  0x117 }, /* # */
    { "COPY", 0x118 }, /* # */
    { "MUTE", 0x120 },
    { "VOL-", 0x12e },
    { "VOL+", 0x130 },
    { "HELP", 0x13b },
    { "OPEN", 0x13f },
    { "POWR", 0x15e },
    { "STOP", 0x168 },
};
struct xkb_keymap *xkbcommon_keymap = nullptr;

void
xkbcommon_init(struct xkb_keymap *keymap)
{
    if (keymap)
        xkbcommon_keymap = keymap;
}

void
xkbcommon_close()
{
    xkbcommon_keymap = nullptr;
}

uint16_t
xkbcommon_translate(uint32_t keycode)
{
    const char *key_name = xkb_keymap_key_get_name(xkbcommon_keymap, keycode);

    /* If XKB doesn't know the key name for this keycode, assume an unnamed Ixxx key.
       This is useful for older XKB versions with an incomplete evdev keycode map. */
    auto key_name_s = key_name ? std::string(key_name) : QString("I%1").arg(keycode).toStdString();
    auto ret        = xkb_keycodes[key_name_s];

    /* Observed with multimedia keys on Windows VcXsrv. */
    if (!ret && (key_name_s.length() == 3) && (key_name_s[0] == 'I') && IS_HEX_DIGIT(key_name_s[1]) && IS_HEX_DIGIT(key_name_s[2]))
        ret = 0x100 | stoi(key_name_s.substr(1), nullptr, 16);

    /* Translate unnamed evdev-specific keycodes. */
    if (!ret && (key_name_s.length() >= 2) && (key_name_s[0] == 'I') && IS_DEC_DIGIT(key_name_s[1]))
        ret = evdev_translate(stoi(key_name_s.substr(1)) - 8);

    if (!ret)
        qWarning() << "XKB Keyboard: Unknown key" << QString::number(keycode, 16) << QString::fromStdString(key_name_s);
#if 0
    else
        qInfo() << "XKB Keyboard: Key" << QString::number(keycode, 16) << QString::fromStdString(key_name_s) << "scancode" << QString::number(ret, 16);
#endif

    return ret;
}
