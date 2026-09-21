/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Configuration file handler header.
 *
 * Authors: Sarah Walker,
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Overdoze,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2017 Sarah Walker.
 *          Copyright 2016-2017 Miran Grca.
 *          Copyright 2017 Fred N. van Kempen.
 *          Copyright 2021-2025 Jasmine Iwanek.
 */
#ifndef EMU_CONFIG_H
#define EMU_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

extern void config_load_global(void);
extern void config_load(void);
extern void config_save_global(void);
extern void config_save(void);

#ifdef EMU_INI_H
extern ini_t config_get_ini(void);
#else
extern void *config_get_ini(void);
#endif

#define config_delete_var(head, name)       ini_delete_var(config_get_ini(), head, name)

#define config_get_int(head, name, def)     ini_get_int(config_get_ini(), head, name, def)
#define config_get_double(head, name, def)  ini_get_double(config_get_ini(), head, name, def)
#define config_get_hex16(head, name, def)   ini_get_hex16(config_get_ini(), head, name, def)
#define config_get_hex20(head, name, def)   ini_get_hex20(config_get_ini(), head, name, def)
#define config_get_mac(head, name, def)     ini_get_mac(config_get_ini(), head, name, def)
#define config_get_string(head, name, def)  ini_get_string(config_get_ini(), head, name, def)
#define config_get_wstring(head, name, def) ini_get_wstring(config_get_ini(), head, name, def)

#define config_set_int(head, name, val)     ini_set_int(config_get_ini(), head, name, val)
#define config_set_double(head, name, val)  ini_set_double(config_get_ini(), head, name, val)
#define config_set_hex16(head, name, val)   ini_set_hex16(config_get_ini(), head, name, val)
#define config_set_hex20(head, name, val)   ini_set_hex20(config_get_ini(), head, name, val)
#define config_set_mac(head, name, val)     ini_set_mac(config_get_ini(), head, name, val)
#define config_set_string(head, name, val)  ini_set_string(config_get_ini(), head, name, val)
#define config_set_wstring(head, name, val) ini_set_wstring(config_get_ini(), head, name, val)

#define config_find_section(name)           ini_find_section(config_get_ini(), name)
#define config_create_section(name)         ini_find_or_create_section(config_get_ini(), name)
#define config_rename_section               ini_rename_section

#ifdef __cplusplus
}
#endif

#endif /*EMU_CONFIG_H*/
