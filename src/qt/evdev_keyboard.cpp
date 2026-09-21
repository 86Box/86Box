/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          evdev keyboard input module.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2023 RichardG.
 */
#include <unordered_map>
#include <QtDebug>

static std::unordered_map<uint32_t, uint16_t> evdev_keycodes = {
    { 184, 0x46  }, /* F14 => Scroll Lock (for Apple keyboards) */
    { 86,  0x56  }, /* 102ND */
    { 87,  0x57  }, /* F11 */
    { 88,  0x58  }, /* F12 */
    { 186, 0x5d  }, /* F16 => F13 */
    { 187, 0x5e  }, /* F17 => F14 */
    { 188, 0x5f  }, /* F18 => F15 */

    /* Japanese keys. */
    { 95,  0x5c  }, /* KPJPCOMMA */
    { 93,  0x70  }, /* KATAKANAHIRAGANA */
    { 89,  0x73  }, /* RO */
    { 85,  0x76  }, /* ZENKAKUHANKAKU */
    { 91,  0x77  }, /* HIRAGANA */
    { 90,  0x78  }, /* KATAKANA */
    { 92,  0x79  }, /* HENKAN */
    { 94,  0x7b  }, /* MUHENKAN */
    { 124, 0x7d  }, /* YEN */
    { 121, 0x7e  }, /* KPCOMMA */

    /* Korean keys. */
    { 123, 0xf1  }, /* HANJA */
    { 122, 0xf2  }, /* HANGUL */

    { 96,  0x11c }, /* KPENTER */
    { 97,  0x11d }, /* RIGHTCTRL */
    { 98,  0x135 }, /* KPSLASH */
    { 99,  0x137 }, /* SYSRQ */
    { 183, 0x137 }, /* F13 => SysRq (for Apple keyboards) */
    { 100, 0x138 }, /* RIGHTALT */
    { 119, 0x145 }, /* PAUSE */
    { 411, 0x145 }, /* BREAK */
    { 185, 0x145 }, /* F15 => Pause (for Apple keyboards) */
    { 102, 0x147 }, /* HOME */
    { 103, 0x148 }, /* UP */
    { 104, 0x149 }, /* PAGEUP */
    { 105, 0x14b }, /* LEFT */
    { 106, 0x14d }, /* RIGHT */
    { 107, 0x14f }, /* END */
    { 108, 0x150 }, /* DOWN */
    { 109, 0x151 }, /* PAGEDOWN */
    { 110, 0x152 }, /* INSERT */
    { 111, 0x153 }, /* DELETE */

    { 125, 0x15b }, /* LEFTMETA */
    { 126, 0x15c }, /* RIGHTMETA */
    { 127, 0x15d }, /* COMPOSE => Menu */

    /* Multimedia keys. Guideline is to try and follow the Microsoft standard, then
       fill in remaining scancodes with OEM-specific keys for redundancy sake. Keys
       marked with # are not translated into evdev codes by the standard atkbd driver. */
    { 634, 0x54  }, /* SELECTIVE_SCREENSHOT# => Alt+SysRq */
    { 117, 0x59  }, /* KPEQUAL */
    { 418, 0x6a  }, /* ZOOMIN# => Logitech */
    { 420, 0x6b  }, /* ZOOMRESET# => Logitech */
    { 223, 0x6d  }, /* CANCEL# => Logitech */
    { 132, 0x101 }, /* # Logitech Task Select */
    { 148, 0x102 }, /* PROG1# => Samsung */
    { 149, 0x103 }, /* PROG2# => Samsung */
    { 419, 0x104 }, /* ZOOMOUT# => Logitech */
    { 144, 0x105 }, /* FILE# => Messenger/Files */
    { 216, 0x105 }, /* CHAT# => Messenger/Files */
    { 430, 0x105 }, /* MESSENGER# */
    { 182, 0x107 }, /* REDO# */
    { 131, 0x108 }, /* UNDO# */
    { 135, 0x10a }, /* PASTE# */
    { 177, 0x10b }, /* SCROLLUP# => normal speed */
    { 165, 0x110 }, /* PREVIOUSSONG */
    { 136, 0x112 }, /* FIND# => Logitech */
    { 421, 0x113 }, /* WORDPROCESSOR# => Word */
    { 423, 0x114 }, /* SPREADSHEET# => Excel */
    { 397, 0x115 }, /* CALENDAR# */
    { 433, 0x116 }, /* LOGOFF# */
    { 137, 0x117 }, /* CUT# */
    { 133, 0x118 }, /* COPY# */
    { 163, 0x119 }, /* NEXTSONG */
    { 154, 0x11e }, /* CYCLEWINDOWS => Application Right (no left counterpart) */
    { 113, 0x120 }, /* MUTE */
    { 140, 0x121 }, /* CALC */
    { 164, 0x122 }, /* PLAYPAUSE */
    { 432, 0x123 }, /* SPELLCHECK# */
    { 166, 0x124 }, /* STOPCD */
    { 139, 0x126 }, /* MENU# => Shortcut/Menu/Help for a few OEMs */
    { 114, 0x12e }, /* VOL- */
    { 160, 0x12f }, /* CLOSECD# => Logitech Eject */
    { 161, 0x12f }, /* EJECTCD# => Logitech */
    { 162, 0x12f }, /* EJECTCLOSECD# => Logitech */
    { 115, 0x130 }, /* VOL+ */
    { 150, 0x132 }, /* WWW# */
    { 172, 0x132 }, /* HOMEPAGE */
    { 138, 0x13b }, /* HELP# */
    { 213, 0x13c }, /* SOUND# => My Music/Office Home */
    { 360, 0x13c }, /* VENDOR# => My Music/Office Home */
    { 204, 0x13d }, /* DASHBOARD# => Task Pane */
    { 181, 0x13e }, /* NEW# */
    { 134, 0x13f }, /* OPEN# */
    { 206, 0x140 }, /* CLOSE# */
    { 232, 0x141 }, /* REPLY# */
    { 233, 0x142 }, /* FORWARDMAIL# */
    { 231, 0x143 }, /* SEND# */
    { 151, 0x144 }, /* MSDOS# */
    { 112, 0x14c }, /* MACRO */
    { 179, 0x14c }, /* KPLEFTPAREN# */
    { 118, 0x14e }, /* KPPLUSMINUS */
    { 235, 0x155 }, /* DOCUMENTS# => Logitech */
    { 234, 0x157 }, /* SAVE# */
    { 210, 0x158 }, /* PRINT# */
    { 116, 0x15e }, /* POWER */
    { 142, 0x15f }, /* SLEEP */
    { 143, 0x163 }, /* WAKEUP */
    { 180, 0x164 }, /* KPRIGHTPAREN# */
    { 212, 0x164 }, /* CAMERA# => My Pictures */
    { 217, 0x165 }, /* SEARCH */
    { 156, 0x166 }, /* BOOKMARKS => Favorites */
    { 364, 0x166 }, /* FAVORITES# */
    { 173, 0x167 }, /* REFRESH */
    { 128, 0x168 }, /* STOP */
    { 159, 0x169 }, /* FORWARD */
    { 158, 0x16a }, /* BACK */
    { 157, 0x16b }, /* COMPUTER */
    { 155, 0x16c }, /* MAIL */
    { 215, 0x16c }, /* EMAIL# */
    { 226, 0x16d }, /* MEDIA */
    { 167, 0x178 }, /* RECORD# => Logitech */
    { 152, 0x17a }, /* COFFEE/SCREENLOCK# */
    { 178, 0x18b }, /* SCROLLDOWN# => normal speed */
};

uint16_t
evdev_translate(uint32_t keycode)
{
    /* "for 1-83 (0x01-0x53) scancode equals keycode" */
    auto ret = (keycode <= 0x53) ? keycode : evdev_keycodes[keycode];

    if (!ret)
        qWarning() << "Evdev Keyboard: Unknown key" << keycode;
#if 0
    else
        qInfo() << "Evdev Keyboard: Key" << keycode << "scancode" << QString::number(ret, 16);
#endif

    return ret;
}
