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
 * Version:	@(#)win_thread.c	1.0.5	2017/10/19
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2017 Fred N. van Kempen.
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
#include "../86box.h"
#include "../plat.h"


typedef struct {
    HANDLE handle;
} win_event_t;


thread_t *
thread_create(void (*func)(void *param), void *param)
{
    return((thread_t *)_beginthread(func, 0, param));
}


void
thread_kill(void *arg)
{
    if (arg == NULL) return;

    TerminateThread(arg, 0);
}


int
thread_wait(thread_t *arg, int timeout)
{
    if (arg == NULL) return(0);

    if (timeout == -1)
	timeout = INFINITE;

    if (WaitForSingleObject(arg, timeout)) return(1);

    return(0);
}


event_t *
thread_create_event(void)
{
    win_event_t *ev = malloc(sizeof(win_event_t));

    ev->handle = CreateEvent(NULL, FALSE, FALSE, NULL);

    return((event_t *)ev);
}


void
thread_set_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *)arg;

    if (arg == NULL) return;

    SetEvent(ev->handle);
}


void
thread_reset_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *)arg;

    if (arg == NULL) return;

    ResetEvent(ev->handle);
}


int
thread_wait_event(event_t *arg, int timeout)
{
    win_event_t *ev = (win_event_t *)arg;

    if (arg == NULL) return(0);

    if (ev->handle == NULL) return(0);

    if (timeout == -1)
	timeout = INFINITE;

    if (WaitForSingleObject(ev->handle, timeout)) return(1);

    return(0);
}


void
thread_destroy_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *)arg;

    if (arg == NULL) return;

    CloseHandle(ev->handle);

    free(ev);
}


mutex_t *
thread_create_mutex(wchar_t *name)
{
    return((mutex_t*)CreateMutex(NULL, FALSE, name));
}


void
thread_close_mutex(mutex_t *mutex)
{
    if (mutex == NULL) return;

    CloseHandle((HANDLE)mutex);
}


int
thread_wait_mutex(mutex_t *mutex)
{
    if (mutex == NULL) return(0);

    DWORD dwres = WaitForSingleObject((HANDLE)mutex, INFINITE);

    if (dwres == WAIT_OBJECT_0) return(1);

    return(0);
}


int
thread_release_mutex(mutex_t *mutex)
{
    if (mutex == NULL) return(0);

    return(!!ReleaseMutex((HANDLE)mutex));
}
