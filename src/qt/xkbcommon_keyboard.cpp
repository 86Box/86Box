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
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2023 RichardG.
 */
extern "C" {
#include <xkbcommon/xkbcommon-x11.h>
};

#include <unordered_map>
#include <QtDebug>

#define IS_HEX_DIGIT(c) ((((c) >= '0') && ((c) <= '9')) || (((c) >= 'A') && ((c) <= 'F')) || (((c) >= 'a') && ((c) <= 'f')))

std::unordered_map<std::string, uint16_t> xkb_keycodes{
    {"ESC",  0x01},
    {"AE01", 0x02},
    {"AE02", 0x03},
    {"AE03", 0x04},
    {"AE04", 0x05},
    {"AE05", 0x06},
    {"AE06", 0x07},
    {"AE07", 0x08},
    {"AE08", 0x09},
    {"AE09", 0x0a},
    {"AE10", 0x0b},
    {"AE11", 0x0c},
    {"AE12", 0x0d},
    {"BKSP", 0x0e},

    {"TAB",  0x0f},
    {"AD01", 0x10},
    {"AD02", 0x11},
    {"AD03", 0x12},
    {"AD04", 0x13},
    {"AD05", 0x14},
    {"AD06", 0x15},
    {"AD07", 0x16},
    {"AD08", 0x17},
    {"AD09", 0x18},
    {"AD10", 0x19},
    {"AD11", 0x1a},
    {"AD12", 0x1b},
    {"RTRN", 0x1c},

    {"LCTL", 0x1d},
    {"AC01", 0x1e},
    {"AC02", 0x1f},
    {"AC03", 0x20},
    {"AC04", 0x21},
    {"AC05", 0x22},
    {"AC06", 0x23},
    {"AC07", 0x24},
    {"AC08", 0x25},
    {"AC09", 0x26},
    {"AC10", 0x27},
    {"AC11", 0x28},

    {"TLDE", 0x29},
    {"LFSH", 0x2a},
    {"BKSL", 0x2b},
    {"AB01", 0x2c},
    {"AB02", 0x2d},
    {"AB03", 0x2e},
    {"AB04", 0x2f},
    {"AB05", 0x30},
    {"AB06", 0x31},
    {"AB07", 0x32},
    {"AB08", 0x33},
    {"AB09", 0x34},
    {"AB10", 0x35},
    {"RTSH", 0x36},

    {"KPMU", 0x37},
    {"LALT", 0x38},
    {"SPCE", 0x39},
    {"CAPS", 0x3a},
    {"FK01", 0x3b},
    {"FK02", 0x3c},
    {"FK03", 0x3d},
    {"FK04", 0x3e},
    {"FK05", 0x3f},
    {"FK06", 0x40},
    {"FK07", 0x41},
    {"FK08", 0x42},
    {"FK09", 0x43},
    {"FK10", 0x44},

    {"NMLK", 0x45},
    {"SCLK", 0x46},
    {"FK14", 0x46}, /* F14 as Scroll Lock */
    {"KP7",  0x47},
    {"KP8",  0x48},
    {"KP9",  0x49},
    {"KPSU", 0x4a},
    {"KP4",  0x4b},
    {"KP5",  0x4c},
    {"KP6",  0x4d},
    {"KPAD", 0x4e},
    {"KP1",  0x4f},
    {"KP2",  0x50},
    {"KP3",  0x51},
    {"KP0",  0x52},
    {"KPDL", 0x53},

    {"LSGT", 0x56},
    {"FK11", 0x57},
    {"FK12", 0x58},

    /* Japanese keys. */
    {"HKTG", 0x70}, /* hiragana-katakana toggle... */
    {"HIRA", 0x70}, /* ...and individual keys */
    {"KATA", 0x70},
    {"AB11", 0x73}, /* \_ and Brazilian /? */
    {"HENK", 0x79},
    {"MUHE", 0x7b},
    {"AE13", 0x7d}, /* \| */
    {"KPPT", 0x7e}, /* Brazilian Num. */
    {"I06",  0x7e}, /* alias of KPPT on keycodes/xfree86 (i.e. X11 forwarding) */
    {"I129", 0x7e}, /* another alias: KPCOMMA */

    /* Korean keys. */
    {"HJCV", 0xf1}, /* hancha toggle */
    {"HNGL", 0xf2}, /* latin toggle */

    {"KPEN", 0x11c},
    {"RCTL", 0x11d},
    {"KPDV", 0x135},
    {"PRSC", 0x137},
    {"SYRQ", 0x137},
    {"FK13", 0x137}, /* F13 as SysRq */
    {"RALT", 0x138},
    {"PAUS", 0x145},
    {"FK15", 0x145}, /* F15 as Pause */
    {"HOME", 0x147},
    {"UP",   0x148},
    {"PGUP", 0x149},
    {"LEFT", 0x14b},
    {"RGHT", 0x14d},
    {"END",  0x14f},
    {"DOWN", 0x150},
    {"PGDN", 0x151},
    {"INS",  0x152},
    {"DELE", 0x153},

    {"LWIN", 0x15b},
    {"RWIN", 0x15c},
    {"COMP", 0x15d},

    /* Multimedia keys, using Linux evdev-specific keycodes where required. Guideline is to try
       and follow the Microsoft standard, then fill in some OEM-specific keys for redundancy sake.
       Keys marked with # are not translated into evdev codes by the standard atkbd driver. */
    {"KPEQ", 0x59},  /* Num= */
    {"FRNT", 0x101}, /* # Logitech Task Select */
    {"I224", 0x105}, /* CHAT# => Messenger/Files */
    {"I190", 0x107}, /* REDO# */
    {"UNDO", 0x108}, /* # */
    {"PAST", 0x10a}, /* # Paste */
    {"I185", 0x10b}, /* SCROLLUP# => normal speed */
    {"I173", 0x110}, /* PREVIOUSSONG */
    {"FIND", 0x112}, /* # Logitech */
    {"I156", 0x113}, /* PROG1# => Word */
    {"I157", 0x114}, /* PROG2# => Excel */
    {"I210", 0x115}, /* PROG3# => Calendar */
    {"I182", 0x116}, /* EXIT# => Log Off */
    {"CUT",  0x117}, /* # */
    {"COPY", 0x118}, /* # */
    {"I171", 0x119}, /* NEXTSONG */
    {"I162", 0x11e}, /* CYCLEWINDOWS => Application Right (no left counterpart) */
    {"MUTE", 0x120},
    {"I148", 0x121}, /* CALC */
    {"I172", 0x122}, /* PLAYPAUSE */
    {"I158", 0x123}, /* WWW# => Compaq online start */
    {"I174", 0x124}, /* STOPCD */
    {"I147", 0x126}, /* MENU# => Shortcut/Menu/Help for a few OEMs */
    {"VOL-", 0x12e},
    {"I168", 0x12f}, /* CLOSECD# => Logitech Eject */
    {"I169", 0x12f}, /* EJECTCD# => Logitech */
    {"I170", 0x12f}, /* EJECTCLOSECD# => Logitech */
    {"VOL+", 0x130},
    {"I180", 0x132}, /* HOMEPAGE */
    {"HELP", 0x13b}, /* # */
    {"I221", 0x13c}, /* SOUND# => My Music */
    {"I212", 0x13d}, /* DASHBOARD# => Task Pane */
    {"I189", 0x13e}, /* NEW# */
    {"OPEN", 0x13f}, /* # */
    {"I214", 0x140}, /* CLOSE# */
    {"I240", 0x141}, /* REPLY# */
    {"I241", 0x142}, /* FORWARDMAIL# */
    {"I239", 0x143}, /* SEND# */
    {"I159", 0x144}, /* MSDOS# */
    {"I120", 0x14c}, /* MACRO */
    {"I187", 0x14c}, /* KPLEFTPAREN# */
    {"I126", 0x14e}, /* KPPLUSMINUS */
    {"I243", 0x155}, /* DOCUMENTS# => Logitech */
    {"I242", 0x157}, /* SAVE# */
    {"I218", 0x158}, /* PRINT# */
    {"POWR", 0x15e},
    {"I150", 0x15f}, /* SLEEP */
    {"I151", 0x163}, /* WAKEUP */
    {"I188", 0x164}, /* KPRIGHTPAREN# */
    {"I220", 0x164}, /* CAMERA# => My Pictures */
    {"I225", 0x165}, /* SEARCH */
    {"I164", 0x166}, /* BOOKMARKS => Favorites */
    {"I181", 0x167}, /* REFRESH */
    {"STOP", 0x168},
    {"I167", 0x169}, /* FORWARD */
    {"I166", 0x16a}, /* BACK */
    {"I165", 0x16b}, /* COMPUTER */
    {"I163", 0x16c}, /* MAIL */
    {"I223", 0x16c}, /* EMAIL# */
    {"I234", 0x16d}, /* MEDIA */
    {"I175", 0x178}, /* RECORD# => Logitech */
    {"I160", 0x17a}, /* COFFEE# */
    {"I186", 0x18b}, /* SCROLLDOWN# => normal speed */
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
    xkbcommon_keymap = NULL;
}

uint16_t
xkbcommon_translate(uint32_t keycode)
{
    const char *key_name = xkb_keymap_key_get_name(xkbcommon_keymap, keycode);
    if (!key_name) {
        qWarning() << "XKB Keyboard: Unknown keycode" << Qt::hex << keycode;
        return 0;
    }

    std::string key_name_s(key_name);
    uint16_t ret = xkb_keycodes[key_name_s];

    /* Observed with multimedia keys on a Windows X11 client. */
    if (!ret && (key_name_s.length() == 3) && (key_name_s[0] == 'I') && IS_HEX_DIGIT(key_name_s[1]) && IS_HEX_DIGIT(key_name_s[2]))
        ret = 0x100 | stoi(key_name_s.substr(1), nullptr, 16);

    if (!ret)
        qWarning() << "XKB Keyboard: Unknown key" << Qt::hex << keycode << "/" << QString::fromStdString(key_name_s);
#if 0
    else
        qInfo() << "XKB Keyboard: Key" << Qt::hex << keycode << "/" << QString::fromStdString(key_name_s) << "scancode" << Qt::hex << ret;
#endif

    return ret;
}
