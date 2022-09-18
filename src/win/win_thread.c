/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implement threads and mutexes for the Win32 platform.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#include <process.h>
#undef BITMAP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/thread.h>

typedef struct {
    HANDLE handle;
} win_event_t;

thread_t *
thread_create(void (*func)(void *param), void *param)
{
    uintptr_t bt = _beginthread(func, 0, param);
    return ((thread_t *) bt);
}

int
thread_test_mutex(thread_t *arg)
{
    if (arg == NULL)
        return (0);

    return (WaitForSingleObject(arg, 0) == WAIT_OBJECT_0) ? 1 : 0;
}

int
thread_wait(thread_t *arg)
{
    if (arg == NULL)
        return (0);

    if (WaitForSingleObject(arg, INFINITE))
        return (1);

    return (0);
}

event_t *
thread_create_event(void)
{
    win_event_t *ev = malloc(sizeof(win_event_t));

    ev->handle = CreateEvent(NULL, FALSE, FALSE, NULL);

    return ((event_t *) ev);
}

void
thread_set_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *) arg;

    if (arg == NULL)
        return;

    SetEvent(ev->handle);
}

void
thread_reset_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *) arg;

    if (arg == NULL)
        return;

    ResetEvent(ev->handle);
}

int
thread_wait_event(event_t *arg, int timeout)
{
    win_event_t *ev = (win_event_t *) arg;

    if (arg == NULL)
        return (0);

    if (ev->handle == NULL)
        return (0);

    if (timeout == -1)
        timeout = INFINITE;

    if (WaitForSingleObject(ev->handle, timeout))
        return (1);

    return (0);
}

void
thread_destroy_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *) arg;

    if (arg == NULL)
        return;

    CloseHandle(ev->handle);

    free(ev);
}

mutex_t *
thread_create_mutex(void)
{
    mutex_t *mutex = malloc(sizeof(CRITICAL_SECTION));

    InitializeCriticalSection(mutex);

    return mutex;
}

int
thread_wait_mutex(mutex_t *mutex)
{
    if (mutex == NULL)
        return (0);

    LPCRITICAL_SECTION critsec = (LPCRITICAL_SECTION) mutex;

    EnterCriticalSection(critsec);

    return 1;
}

int
thread_release_mutex(mutex_t *mutex)
{
    if (mutex == NULL)
        return (0);

    LPCRITICAL_SECTION critsec = (LPCRITICAL_SECTION) mutex;

    LeaveCriticalSection(critsec);

    return 1;
}

void
thread_close_mutex(mutex_t *mutex)
{
    if (mutex == NULL)
        return;

    LPCRITICAL_SECTION critsec = (LPCRITICAL_SECTION) mutex;

    DeleteCriticalSection(critsec);

    free(critsec);
}
