/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 * 		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Try to load a support DLL.
 *
 * Version:	@(#)win_dynld.c	1.0.2	2017/05/24
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcap.h>
#include "plat_dynld.h"
#include "../ibm.h"


void *
dynld_module(const char *name, dllimp_t *table)
{
    HMODULE h;
    dllimp_t *imp;
    void *func;
    char **foo;

    /* See if we can load the desired module. */
    if ((h = LoadLibrary(name)) == NULL) {
	pclog("DynLd(\"%s\"): library not found!\n", name);
	return(NULL);
    }

    /* Now load the desired function pointers. */
    for (imp=table; imp->name!=NULL; imp++) {
	func = GetProcAddress(h, imp->name);
	if (func == NULL) {
		pclog("DynLd(\"%s\"): function '%s' not found!\n",
						name, imp->name);
		CloseHandle(h);
		return(NULL);
	}

	/* To overcome typing issues.. */
	*(char **)imp->func = (char *)func;
    }

    /* All good. */
    return((void *)h);
}


void
dynld_close(void *handle)
{
    if (handle != NULL)
	FreeLibrary((HMODULE)handle);
}
