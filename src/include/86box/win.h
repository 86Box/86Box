/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Platform support defintions for Win32.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2021 Laci bá'
 */
#ifndef PLAT_WIN_H
#define PLAT_WIN_H

#ifndef UNICODE
#    define UNICODE
#endif
#include <windows.h>

/* Application-specific window messages.

   A dialog sends 0x8895 with WPARAM = 1 followed by 0x8896 with WPARAM = 1 on open,
   and 0x8895 with WPARAM = <previous pause status> followed by 0x8896 with WPARAM = 0.

   All shutdowns will send an 0x8897. */
#define WM_LEAVEFULLSCREEN WM_USER
#define WM_SAVESETTINGS    0x8888
#define WM_SHOWSETTINGS    0x8889
#define WM_PAUSE           0x8890
#define WM_SENDHWND        0x8891
#define WM_HARDRESET       0x8892
#define WM_SHUTDOWN        0x8893
#define WM_CTRLALTDEL      0x8894
/* Pause/resume status: WPARAM = 1 for paused, 0 for resumed. */
#define WM_SENDSTATUS 0x8895
/* Dialog (Settings or message box) status: WPARAM = 1 for open, 0 for closed. */
#define WM_SENDDLGSTATUS 0x8896
/* The emulator has shut down. */
#define WM_HAS_SHUTDOWN 0x8897

#endif /*PLAT_WIN_H*/
