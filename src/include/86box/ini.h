/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Configuration file handler header.
 *
 *
 *
 * Authors:	Sarah Walker,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Overdoze,
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *
 */
#ifndef EMU_INI_H
#define EMU_INI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void *ini_t;
typedef void *ini_section_t;

extern ini_t ini_new(void);
extern ini_t ini_read(char *fn);
extern void  ini_write(ini_t ini, char *fn);
extern void  ini_dump(ini_t ini);
extern void  ini_close(ini_t ini);

extern void     ini_section_delete_var(ini_section_t section, char *name);
extern int      ini_section_get_int(ini_section_t section, char *name, int def);
extern double   ini_section_get_double(ini_section_t section, char *name, double def);
extern int      ini_section_get_hex16(ini_section_t section, char *name, int def);
extern int      ini_section_get_hex20(ini_section_t section, char *name, int def);
extern int      ini_section_get_mac(ini_section_t section, char *name, int def);
extern char    *ini_section_get_string(ini_section_t section, char *name, char *def);
extern wchar_t *ini_section_get_wstring(ini_section_t section, char *name, wchar_t *def);
extern void     ini_section_set_int(ini_section_t section, char *name, int val);
extern void     ini_section_set_double(ini_section_t section, char *name, double val);
extern void     ini_section_set_hex16(ini_section_t section, char *name, int val);
extern void     ini_section_set_hex20(ini_section_t section, char *name, int val);
extern void     ini_section_set_mac(ini_section_t section, char *name, int val);
extern void     ini_section_set_string(ini_section_t section, char *name, char *val);
extern void     ini_section_set_wstring(ini_section_t section, char *name, wchar_t *val);

#define ini_delete_var(ini, head, name)       ini_section_delete_var(ini_find_section(ini, head), name)

#define ini_get_int(ini, head, name, def)     ini_section_get_int(ini_find_section(ini, head), name, def)
#define ini_get_double(ini, head, name, def)  ini_section_get_double(ini_find_section(ini, head), name, def)
#define ini_get_hex16(ini, head, name, def)   ini_section_get_hex16(ini_find_section(ini, head), name, def)
#define ini_get_hex20(ini, head, name, def)   ini_section_get_hex20(ini_find_section(ini, head), name, def)
#define ini_get_mac(ini, head, name, def)     ini_section_get_mac(ini_find_section(ini, head), name, def)
#define ini_get_string(ini, head, name, def)  ini_section_get_string(ini_find_section(ini, head), name, def)
#define ini_get_wstring(ini, head, name, def) ini_section_get_wstring(ini_find_section(ini, head), name, def)

#define ini_set_int(ini, head, name, val)     ini_section_set_int(ini_find_or_create_section(ini, head), name, val)
#define ini_set_double(ini, head, name, val)  ini_section_set_double(ini_find_or_create_section(ini, head), name, val)
#define ini_set_hex16(ini, head, name, val)   ini_section_set_hex16(ini_find_or_create_section(ini, head), name, val)
#define ini_set_hex20(ini, head, name, val)   ini_section_set_hex20(ini_find_or_create_section(ini, head), name, val)
#define ini_set_mac(ini, head, name, val)     ini_section_set_mac(ini_find_or_create_section(ini, head), name, val)
#define ini_set_string(ini, head, name, val)  ini_section_set_string(ini_find_or_create_section(ini, head), name, val)
#define ini_set_wstring(ini, head, name, val) ini_section_set_wstring(ini_find_or_create_section(ini, head), name, val)

extern ini_section_t ini_find_section(ini_t ini, char *name);
extern ini_section_t ini_find_or_create_section(ini_t ini, char *name);
extern void          ini_rename_section(ini_section_t section, char *name);
extern void          ini_delete_section_if_empty(ini_t ini, ini_section_t section);

#ifdef __cplusplus
}
#endif

#endif