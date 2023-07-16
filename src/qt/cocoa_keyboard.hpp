static std::array<uint32_t, 127> cocoa_keycodes = { /* key names in parentheses are not declared by Apple headers */
    0x1e,  /* ANSI_A */
    0x1f,  /* ANSI_S */
    0x20,  /* ANSI_D */
    0x21,  /* ANSI_F */
    0x23,  /* ANSI_H */
    0x22,  /* ANSI_G */
    0x2c,  /* ANSI_Z */
    0x2d,  /* ANSI_X */
    0x2e,  /* ANSI_C */
    0x2f,  /* ANSI_V */
    0x56,  /* ISO_Section */
    0x30,  /* ANSI_B */
    0x10,  /* ANSI_Q */
    0x11,  /* ANSI_W */
    0x12,  /* ANSI_E */
    0x13,  /* ANSI_R */
    0x15,  /* ANSI_Y */
    0x14,  /* ANSI_T */
    0x02,  /* ANSI_1 */
    0x03,  /* ANSI_2 */
    0x04,  /* ANSI_3 */
    0x05,  /* ANSI_4 */
    0x07,  /* ANSI_6 */
    0x06,  /* ANSI_5 */
    0x0d,  /* ANSI_Equal */
    0x0a,  /* ANSI_9 */
    0x08,  /* ANSI_7 */
    0x0c,  /* ANSI_Minus */
    0x09,  /* ANSI_8 */
    0x0b,  /* ANSI_0 */
    0x1b,  /* ANSI_RightBracket */
    0x18,  /* ANSI_O */
    0x16,  /* ANSI_U */
    0x1a,  /* ANSI_LeftBracket */
    0x17,  /* ANSI_I */
    0x19,  /* ANSI_P */
    0x1c,  /* Return */
    0x26,  /* ANSI_L */
    0x24,  /* ANSI_J */
    0x28,  /* ANSI_Quote */
    0x25,  /* ANSI_K */
    0x27,  /* ANSI_Semicolon */
    0x2b,  /* ANSI_Backslash */
    0x33,  /* ANSI_Comma */
    0x35,  /* ANSI_Slash */
    0x31,  /* ANSI_N */
    0x32,  /* ANSI_M */
    0x34,  /* ANSI_Period */
    0x0f,  /* Tab */
    0x39,  /* Space */
    0x29,  /* ANSI_Grave */
    0x0e,  /* Delete => Backspace */
    0x11c, /* (ANSI_KeypadEnter) */
    0x01,  /* Escape */
    0x15c, /* (RightCommand) => Right Windows */
    0x15b, /* (Left)Command => Left Windows */
    0x2a,  /* Shift */
    0x3a,  /* CapsLock */
    0x38,  /* Option */
    0x1d,  /* Control */
    0x36,  /* RightShift */
    0x138, /* RightOption */
    0x11d, /* RightControl */
    0x15c, /* Function */
    0x5e,  /* F17 => F14 */
    0x53,  /* ANSI_KeypadDecimal */
    0,
    0x37,  /* ANSI_KeypadMultiply */
    0,
    0x4e,  /* ANSI_KeypadPlus */
    0,
    0x45,  /* ANSI_KeypadClear => Num Lock (location equivalent) */
    0x130, /* VolumeUp */
    0x12e, /* VolumeDown */
    0x120, /* Mute */
    0x135, /* ANSI_KeypadDivide */
    0x11c, /* ANSI_KeypadEnter */
    0,
    0x4a,  /* ANSI_KeypadMinus */
    0x5f,  /* F18 => F15 */
    0,     /* F19 */
    0x59,  /* ANSI_KeypadEquals */
    0x52,  /* ANSI_Keypad0 */
    0x4f,  /* ANSI_Keypad1 */
    0x50,  /* ANSI_Keypad2 */
    0x51,  /* ANSI_Keypad3 */
    0x4b,  /* ANSI_Keypad4 */
    0x4c,  /* ANSI_Keypad5 */
    0x4d,  /* ANSI_Keypad6 */
    0x47,  /* ANSI_Keypad7 */
    0,     /* F20 */
    0x48,  /* ANSI_Keypad8 */
    0x49,  /* ANSI_Keypad9 */
    0x7d,  /* JIS_Yen */
    0x73,  /* JIS_Underscore */
    0x5c,  /* JIS_KeypadComma */
    0x3f,  /* F5 */
    0x40,  /* F6 */
    0x41,  /* F7 */
    0x3d,  /* F3 */
    0x42,  /* F8 */
    0x43,  /* F9 */
    0x7b,  /* JIS_Eisu => muhenkan (location equivalent) */
    0x57,  /* F11 */
    0x79,  /* JIS_Kana => henkan (location equivalent) */
    0x137, /* F13 => SysRq (location equivalent) */
    0x5d,  /* F16 => F13 */
    0x46,  /* F14 => Scroll Lock (location equivalent) */
    0,
    0x44,  /* F10 */
    0x15d, /* (Menu) */
    0x58,  /* F12 */
    0,
    0x145, /* F15 => Pause (location equivalent) */
    0x152, /* Help => Insert (location equivalent) */
    0x147, /* Home */
    0x149, /* PageUp */
    0x153, /* ForwardDelete */
    0x3e,  /* F4 */
    0x14f, /* End */
    0x3c,  /* F2 */
    0x151, /* PageDown */
    0x3b,  /* F1 */
    0x14b, /* LeftArrow */
    0x14d, /* RightArrow */
    0x150, /* DownArrow */
    0x148, /* UpArrow */
};

// https://developer.apple.com/documentation/appkit/nseventmodifierflags/
qint32 NSEventModifierFlagCommand = 1 << 20;

qint32 nvk_Delete = 0x75;
qint32 nvk_Insert = 0x72;