/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Platform-dependent global settings handling
 *
 *		  Windows: HKCU\Software\86Box\Preferences
 *		  Linux: ~/.86Box
 *		  macOS: ~/Library/Application Support/86Box
 *
 *		This file contains the implementation for Windows.
 *
 * Author:	Laci bá'
 *
 *		Copyright 2021 Laci bá'
 */

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/win.h>

/*
   This function will initialize the global-config environment for a one-time
   chain reading/writing session. The return value is an optional platform 
   specific type, or pointer.

   If the return value needs some cleanup, it has to performed in 
   plat_gconf_close();

   The mode parameter definies that the context is read-only (0), or 
   writable (1).

   Under Windows the result value is a HKEY type, a handle to the registry key
   with the specified access mode.

*/
void* plat_gconf_init(int mode)
{
    HKEY hKey = 0;
    if (mode)
    {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\86Box\\Preferences", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL) != ERROR_SUCCESS)
            hKey = 0;
    }
    else if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\86Box\\Preferences", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        hKey = 0;

    return (void*)hKey;
}

/*
   This function will cleans up the returned context initialized by 
   plat_gconf_init();
*/
void plat_gconf_close(void *context)
{
    RegCloseKey((HKEY)context);
}

/*
   This function will write a single string to the global config. 
   
   The context has to be initialized before with plat_gconf_init(1);
   The stored values are in key=value format.
*/
void plat_gconf_set_string(void *context, char *key, char *val)
{
    wchar_t *val_w = calloc(strlen(val) + 1, sizeof(wchar_t)),
            *key_w = calloc(strlen(key) + 1, sizeof(wchar_t));

    mbstoc16s(val_w, val, strlen(val) + 1);
    mbstoc16s(key_w, key, strlen(key) + 1);

    RegSetValueExW((HKEY)context, key_w, 0, REG_SZ, (LPBYTE)val_w, (wcslen(val_w) + 1) * sizeof(wchar_t));

    free(val_w);
    free(key_w);
}

/*
   This function will read a single string from the global config. 
   
   The context has to be initialized before with plat_gconf_init(0);
   The stored values are in key=value format.

   The return value has to be freed.
*/
char* plat_gconf_get_string(void *context, char *key, char *def)
{
    wchar_t *key_w = calloc(strlen(key) + 1, sizeof(wchar_t));
    mbstoc16s(key_w, key, strlen(key) + 1);
	
    DWORD cb = 0, dwType = REG_SZ;
    if (RegQueryValueExW((HKEY)context, key_w, NULL, &dwType, NULL, &cb) != ERROR_SUCCESS) {
       free(key_w);    
       return def ? strdup(def) : NULL;
    }
	
    cb += sizeof(wchar_t);
    dwType = REG_SZ;
    wchar_t* buffer = calloc(cb, 1);
	
    if (RegQueryValueExW((HKEY)context, key_w, NULL, &dwType, (LPBYTE)buffer, &cb) != ERROR_SUCCESS) {
       free(key_w);    
       free(buffer);
       return def ? strdup(def) : NULL;
    }
    else {
       char* retval = calloc(cb, sizeof(wchar_t));
       c16stombs(retval, buffer, cb - 1);

       free(key_w);
       free(buffer);        
       return retval;
    }
}