/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Windows raw keyboard input handler.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#define UNICODE
#define _WIN32_WINNT 0x0501
#define BITMAP       WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/plat.h>
#include <86box/win.h>

static uint16_t scancode_map[768];

/* This is so we can disambiguate scan codes that would otherwise conflict and get
   passed on incorrectly. */
static UINT16
convert_scan_code(UINT16 scan_code)
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

void
keyboard_getkeymap(void)
{
    WCHAR        *keyName   = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout";
    WCHAR        *valueName = L"Scancode Map";
    unsigned char buf[32768];
    DWORD         bufSize;
    HKEY          hKey;
    int           j;
    UINT32       *bufEx2;
    int           scMapCount;
    UINT16       *bufEx;
    int           scancode_unmapped;
    int           scancode_mapped;

    /* First, prepare the default scan code map list which is 1:1.
     * Remappings will be inserted directly into it.
     * 512 bytes so this takes less memory, bit 9 set means E0
     * prefix.
     */
    for (j = 0; j < 512; j++)
        scancode_map[j] = j;

    /* Get the scan code remappings from:
    HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Keyboard Layout */
    bufSize = 32768;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyName, 0, 1, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueEx(hKey, valueName, NULL, NULL, buf, &bufSize) == ERROR_SUCCESS) {
            bufEx2     = (UINT32 *) buf;
            scMapCount = bufEx2[2];
            if ((bufSize != 0) && (scMapCount != 0)) {
                bufEx = (UINT16 *) (buf + 12);
                for (j = 0; j < scMapCount * 2; j += 2) {
                    /* Each scan code is 32-bit: 16 bits of remapped scan code,
                       and 16 bits of original scan code. */
                    scancode_unmapped = bufEx[j + 1];
                    scancode_mapped   = bufEx[j];

                    scancode_unmapped = convert_scan_code(scancode_unmapped);
                    scancode_mapped   = convert_scan_code(scancode_mapped);

                    /* Ignore source scan codes with prefixes other than E1
                       that are not E1 1D. */
                    if (scancode_unmapped != 0xFFFF)
                        scancode_map[scancode_unmapped] = scancode_mapped;
                }
            }
        }
        RegCloseKey(hKey);
    }
}

void
keyboard_handle(PRAWINPUT raw)
{
    USHORT     scancode;
    static int recv_lalt = 0, recv_ralt = 0, recv_tab = 0;

    RAWKEYBOARD rawKB = raw->data.keyboard;
    scancode          = rawKB.MakeCode;

    if (kbd_req_capture && !mouse_capture && !video_fullscreen)
        return;

    /* If it's not a scan code that starts with 0xE1 */
    if (!(rawKB.Flags & RI_KEY_E1)) {
        if (rawKB.Flags & RI_KEY_E0)
            scancode |= 0x100;

        /* Translate the scan code to 9-bit */
        scancode = convert_scan_code(scancode);

        /* Remap it according to the list from the Registry */
        if (scancode != scancode_map[scancode])
            pclog("Scan code remap: %03X -> %03X\n", scancode, scancode);
        scancode = scancode_map[scancode];

        /* If it's not 0xFFFF, send it to the emulated
           keyboard.
           We use scan code 0xFFFF to mean a mapping that
           has a prefix other than E0 and that is not E1 1D,
           which is, for our purposes, invalid. */
        if ((scancode == 0x00F) && !(rawKB.Flags & RI_KEY_BREAK) && (recv_lalt || recv_ralt) && !mouse_capture) {
            /* We received a TAB while ALT was pressed, while the mouse
               is not captured, suppress the TAB and send an ALT key up. */
            if (recv_lalt) {
                keyboard_input(0, 0x038);
                /* Extra key press and release so the guest is not stuck in the
                   menu bar. */
                keyboard_input(1, 0x038);
                keyboard_input(0, 0x038);
                recv_lalt = 0;
            }
            if (recv_ralt) {
                keyboard_input(0, 0x138);
                /* Extra key press and release so the guest is not stuck in the
                   menu bar. */
                keyboard_input(1, 0x138);
                keyboard_input(0, 0x138);
                recv_ralt = 0;
            }
        } else if (((scancode == 0x038) || (scancode == 0x138)) && !(rawKB.Flags & RI_KEY_BREAK) && recv_tab && !mouse_capture) {
            /* We received an ALT while TAB was pressed, while the mouse
               is not captured, suppress the ALT and send a TAB key up. */
            keyboard_input(0, 0x00F);
            recv_tab = 0;
        } else {
            switch (scancode) {
                case 0x00F:
                    recv_tab = !(rawKB.Flags & RI_KEY_BREAK);
                    break;
                case 0x038:
                    recv_lalt = !(rawKB.Flags & RI_KEY_BREAK);
                    break;
                case 0x138:
                    recv_ralt = !(rawKB.Flags & RI_KEY_BREAK);
                    break;
            }

            /* Translate right CTRL to left ALT if the user has so
               chosen. */
            if ((scancode == 0x11D) && rctrl_is_lalt)
                scancode = 0x038;

            /* Normal scan code pass through, pass it through as is if
               it's not an invalid scan code. */
            if (scancode != 0xFFFF)
                keyboard_input(!(rawKB.Flags & RI_KEY_BREAK), scancode);
        }
    } else {
        if (rawKB.MakeCode == 0x1D) {
            scancode = scancode_map[0x100]; /* Translate E1 1D to 0x100 (which would
                                               otherwise be E0 00 but that is invalid
                                               anyway).
                                               Also, take a potential mapping into
                                               account. */
        } else
            scancode = 0xFFFF;
        if (scancode != 0xFFFF)
            keyboard_input(!(rawKB.Flags & RI_KEY_BREAK), scancode);
    }
}
