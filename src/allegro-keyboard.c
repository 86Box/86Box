#include "allegro-main.h"
#include "plat-keyboard.h"

int recv_key[272];
int rawinputkey[272];

static int key_convert[128] =
{
          -1, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, /*   ,   A,   B,   C,  D,  E,  F,  G*/
        0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, /*  H,   I,   J,   K,  L,  M,  N,  O*/
        0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, /*  P,   Q,   R,   S,  T,  U,  V,  W*/
        0x2d, 0x15, 0x2c, 0x0b, 0x02, 0x03, 0x04, 0x05, /*  X,   Y,   Z,   0,  1,  2,  3,  4*/
        0x06, 0x07, 0x08, 0x09, 0x0a, 0x52, 0x4f, 0x50, /*  5,   6,   7,   8,  9, p0, p1, p2*/
        0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49, 0x3b, /* p3,  p4,  p5,  p6, p7, p8, p9, F1*/
        0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, /* F2,  F3,  F4,  F5, F6, F7, F8, F9*/
        0x44, 0x57, 0x58, 0x01, 0x29, 0x0c, 0x0d, 0x0e, /*F10, F11, F12, ESC, `ª, -_, =+, backspace*/
        0x0f, 0x1a, 0x1b, 0x1c, 0x27, 0x28, 0x2b, 0x56, /*TAB,  [{,  ]}, ENT, ;:, '@, \|, #~*/
        0x33, 0x34, 0x35, 0x39, 0xd2, 0xd3, 0xc7, 0xcf, /* ,<,  .>,  /?, SPC, INS, DEL, HOME, END*/
        0xc9, 0xd1, 0xcb, 0xcd, 0xc8, 0xd0, 0xb5, 0x37, /*PGU, PGD, LFT, RHT,  UP,  DN,  /, * */
        0x4a, 0x4e, 0x53, 0x9c, 0xff,   -1,   -1,   -1, /* p-,  p+, pDL, pEN, psc, pse, abnt, yen*/
          -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1, /*kana, convert, noconvert, at, circumflex, colon2, kanji, pad equals*/
          -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
          -1,   -1,   -1, 0x2a, 0x36, 0x1d, 0x9d, 0x38, /*, , lshift, rshift, lctrl, rctrl, alt*/
        0xb8, 0xdb, 0xdc, 0xdd, 0x46, 0x45, 0x3a,   -1  /*altgr, lwin, rwin, menu, scrlock, numlock, capslock*/
};

void keyboard_init()
{
        install_keyboard();
}

void keyboard_close()
{
}

void keyboard_poll_host()
{
        int c;
        
        for (c = 0; c < 128; c++)
        {
                int key_idx = key_convert[c];
                if (key_idx == -1)
                        continue;
                
                if (key[c] != recv_key[key_idx])
                        recv_key[key_idx] = key[c];
        }
}
