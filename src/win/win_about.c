/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handle the About dialog.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2021-2023 Jasmine Iwanek.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/win.h>
#include <86box/version.h>

void
AboutDialogCreate(HWND hwnd)
{
    int               i;
    TASKDIALOGCONFIG  tdconfig    = { 0 };
    TASKDIALOG_BUTTON tdbuttons[] = {
        {IDOK,      EMU_SITE_W               },
        { IDCANCEL, MAKEINTRESOURCE(IDS_2128)}
    };

    wchar_t emu_version[256];
    i = swprintf(emu_version, sizeof_w(emu_version), L"%ls v%ls", EMU_NAME_W, EMU_VERSION_FULL_W);
#ifdef EMU_GIT_HASH
    i += swprintf(&emu_version[i], sizeof_w(emu_version) - i, L" [%ls]", EMU_GIT_HASH_W);
#endif

#if defined(__arm__) || defined(__TARGET_ARCH_ARM)
#    define ARCH_STR L"arm"
#elif defined(__aarch64__) || defined(_M_ARM64)
#    define ARCH_STR L"arm64"
#elif defined(__i386) || defined(__i386__) || defined(_M_IX86)
#    define ARCH_STR L"i386"
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
#    define ARCH_STR L"x86_64"
#else
#    define ARCH_STR L"unknown"
#endif
    swprintf(&emu_version[i], sizeof_w(emu_version) - i, L" [%ls, %ls]", ARCH_STR, plat_get_string(IDS_DYNAREC));

    tdconfig.cbSize             = sizeof(tdconfig);
    tdconfig.hwndParent         = hwnd;
    tdconfig.hInstance          = hinstance;
    tdconfig.dwCommonButtons    = 0;
    tdconfig.pszWindowTitle     = MAKEINTRESOURCE(IDS_2125);
    tdconfig.pszMainIcon        = (PCWSTR) 10;
    tdconfig.pszMainInstruction = emu_version;
    tdconfig.pszContent         = MAKEINTRESOURCE(IDS_2127);
    tdconfig.cButtons           = ARRAYSIZE(tdbuttons);
    tdconfig.pButtons           = tdbuttons;
    tdconfig.nDefaultButton     = IDCANCEL;
    TaskDialogIndirect(&tdconfig, &i, NULL, NULL);

    if (i == IDOK)
        ShellExecute(hwnd, L"open", L"https://" EMU_SITE_W, NULL, NULL, SW_SHOW);
}
