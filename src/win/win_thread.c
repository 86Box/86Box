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
 * Version:	@(#)win_thread.c	1.0.2	2017/10/10
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
#include "../ibm.h"
#include "../plat.h"


typedef struct {
    HANDLE handle;
} win_event_t;


void *
thread_create(void (*thread_rout)(void *param), void *param)
{
    return((void *)_beginthread(thread_rout, 0, param));
}


void
thread_kill(void *handle)
{
    if (handle == NULL) {
	return;
    }

    TerminateThread(handle, 0);
}


void
thread_sleep(int t)
{
    Sleep(t);
}


event_t *
thread_create_event(void)
{
    win_event_t *event = malloc(sizeof(win_event_t));

    event->handle = CreateEvent(NULL, FALSE, FALSE, NULL);

    return((event_t *)event);
}


void
thread_set_event(event_t *_event)
{
    if (_event == NULL) {
	return;
    }

    win_event_t *event = (win_event_t *)_event;

    SetEvent(event->handle);
}


void
thread_reset_event(event_t *_event)
{
    if (_event == NULL) {
	return;
    }

    win_event_t *event = (win_event_t *)_event;

    ResetEvent(event->handle);
}


int
thread_wait_event(event_t *_event, int timeout)
{
    if (_event == NULL) {
	return 0;
    }

    win_event_t *event = (win_event_t *)_event;

    if (timeout == -1)
	timeout = INFINITE;

    if (WaitForSingleObject(event->handle, timeout)) return(1);

    return(0);
}


void
thread_destroy_event(event_t *_event)
{
    win_event_t *event = (win_event_t *)_event;

    if (_event == NULL) {
	return;
    }

    CloseHandle(event->handle);

    free(event);
}


void *
thread_create_mutex(wchar_t *name)
{
    return((void*)CreateMutex(NULL, FALSE, name));
}


void
thread_close_mutex(void *mutex)
{
    if (mutex == NULL) {
	return;
    }

    CloseHandle((HANDLE)mutex);
}


uint8_t
thread_wait_mutex(void *mutex)
{
    if (mutex == NULL) {
	return 0;
    }

    DWORD dwres = WaitForSingleObject((HANDLE)mutex, INFINITE);

    switch (dwres) {
	case WAIT_OBJECT_0:
		return(1);

	case WAIT_ABANDONED:
	default:
		return(0);
    }
}


uint8_t
thread_release_mutex(void *mutex)
{
    if (mutex == NULL) {
	return 0;
    }

    return(!!ReleaseMutex((HANDLE)mutex));
}
