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
 *		This file contains the implementation for Linux/macOS.
 *
 */

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
	return NULL;
}

/*
   This function will cleans up the returned context initialized by 
   plat_gconf_init();
*/
void plat_gconf_close(void *context)
{
	return;
}

/*
   This function will write a single string to the global config. 
   
   The context has to be initialized before with plat_gconf_init(1);
   The stored values are in key=value format.
*/
void plat_gconf_set_string(void *context, char *key, char *val)
{
    return;
}

/*
   This function will read a single string from the global config. 
   
   The context has to be initialized before with plat_gconf_init(0);
   The stored values are in key=value format.

   The return value has to be freed.
*/
char* plat_gconf_get_string(void *context, char *key, char *def)
{
    return strdup("none");
}