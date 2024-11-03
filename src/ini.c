/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Configuration file handler.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Overdoze,
 *          David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2018-2019 David Hrdlička.
 *
 * NOTE:    Forcing config files to be in Unicode encoding breaks
 *          it on Windows XP, and possibly also Vista. Use the
 *          -DANSI_CFG for use on these systems.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/plat.h>

typedef struct _list_ {
    struct _list_ *next;
} list_t;

typedef struct section_t {
    list_t list;

    char name[128];

    list_t entry_head;
} section_t;

typedef struct entry_t {
    list_t list;

    char    name[128];
    char    data[512];
    wchar_t wdata[512];
} entry_t;

#define list_add(new, head)        \
    {                              \
        list_t *next = head;       \
                                   \
        while (next->next != NULL) \
            next = next->next;     \
                                   \
        (next)->next = new;        \
        (new)->next  = NULL;       \
    }

#define list_delete(old, head)          \
    {                                   \
        list_t *next = head;            \
                                        \
        while ((next)->next != old) {   \
            next = (next)->next;        \
        }                               \
                                        \
        (next)->next = (old)->next;     \
        if ((next) == (head))           \
            (head)->next = (old)->next; \
    }

#ifdef ENABLE_INI_LOG
int ini_do_log = ENABLE_INI_LOG;

static void
ini_log(const char *fmt, ...)
{
    va_list ap;

    if (ini_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ini_log(fmt, ...)
#endif

static section_t *
find_section(list_t *head, const char *name)
{
    section_t *sec     = (section_t *) head->next;
    const char blank[] = "";

    if (name == NULL)
        name = blank;

    while (sec != NULL) {
        if (!strncmp(sec->name, name, sizeof(sec->name)))
            return sec;

        sec = (section_t *) sec->list.next;
    }

    return NULL;
}

ini_section_t
ini_find_section(ini_t ini, const char *name)
{
    if (ini == NULL)
        return NULL;

    return (ini_section_t) find_section((list_t *) ini, name);
}

void
ini_rename_section(ini_section_t section, const char *name)
{
    section_t *sec = (section_t *) section;

    if (sec == NULL)
        return;

    memset(sec->name, 0x00, sizeof(sec->name));
    memcpy(sec->name, name, MIN(128, strlen(name) + 1));
}

static entry_t *
find_entry(section_t *section, const char *name)
{
    entry_t *ent;

    ent = (entry_t *) section->entry_head.next;

    while (ent != NULL) {
        if (!strncmp(ent->name, name, sizeof(ent->name)))
            return ent;

        ent = (entry_t *) ent->list.next;
    }

    return (NULL);
}

static int
entries_num(section_t *section)
{
    entry_t *ent;
    int      i = 0;

    ent = (entry_t *) section->entry_head.next;

    while (ent != NULL) {
        if (strlen(ent->name) > 0)
            i++;

        ent = (entry_t *) ent->list.next;
    }

    return i;
}

static void
delete_section_if_empty(list_t *head, section_t *section)
{
    if (section == NULL)
        return;

    int     n = entries_num(section);

    if (n > 0) {
        int      i      = 0;
        entry_t *i_ent = (entry_t *) section->entry_head.next;

        while (i_ent != NULL) {
            int      i_nlen = strlen(i_ent->name);
            entry_t* i_next = (entry_t *) i_ent->list.next;

            if (i_nlen > 0) {
                int      j      = 0;
                entry_t *j_ent = (entry_t *) section->entry_head.next;

                while (j_ent != NULL) {
                    int      j_nlen = strlen(j_ent->name);
                    entry_t* j_next = (entry_t *) j_ent->list.next;
                    if (j_nlen > 0) {
                        if ((j != i) && (strcmp(j_ent->name, i_ent->name) > 0)) {
                            entry_t t_ent = { 0 };
                            memcpy(&t_ent, j_ent, sizeof(entry_t));
                            /* J: Contents of I, list of J */
                            memcpy(j_ent->name, i_ent->name, sizeof(entry_t) - sizeof(i_ent->list));
                            /* I: Contents of J, list of I */
                            memcpy(i_ent->name, t_ent.name, sizeof(entry_t) - sizeof(i_ent->list));
                        }

                        j++;
                    }

                    j_ent = (entry_t *) j_next;
                }

                i++;
            }

            i_ent = (entry_t *) i_next;
        }
    } else {
        list_delete(&section->list, head);
        free(section);
    }
}

void
ini_delete_section_if_empty(ini_t ini, ini_section_t section)
{
    if (ini == NULL || section == NULL)
        return;

    delete_section_if_empty((list_t *) ini, (section_t *) section);
}

static section_t *
create_section(list_t *head, const char *name)
{
    section_t *ns = malloc(sizeof(section_t));

    memset(ns, 0x00, sizeof(section_t));
    memcpy(ns->name, name, strlen(name) + 1);
    list_add(&ns->list, head);

    return ns;
}

ini_section_t
ini_find_or_create_section(ini_t ini, const char *name)
{
    if (ini == NULL)
        return NULL;

    section_t *section = find_section((list_t *) ini, name);
    if (section == NULL)
        section = create_section((list_t *) ini, name);

    return (ini_section_t) section;
}

static entry_t *
create_entry(section_t *section, const char *name)
{
    entry_t *ne = malloc(sizeof(entry_t));

    memset(ne, 0x00, sizeof(entry_t));
    memcpy(ne->name, name, strlen(name) + 1);
    list_add(&ne->list, &section->entry_head);

    return ne;
}

void
ini_close(ini_t ini)
{
    section_t *sec;
    section_t *ns;
    entry_t   *ent;
    list_t    *list = (list_t *) ini;

    if (list == NULL)
        return;

    sec = (section_t *) list->next;
    while (sec != NULL) {
        ns  = (section_t *) sec->list.next;
        ent = (entry_t *) sec->entry_head.next;

        while (ent != NULL) {
            entry_t *nent = (entry_t *) ent->list.next;

            free(ent);
            ent = nent;
        }

        free(sec);
        sec = ns;
    }

    free(list);
}

static int
ini_detect_bom(const char *fn)
{
    FILE         *fp;
    unsigned char bom[4] = { 0, 0, 0, 0 };

#if defined(ANSI_CFG) || !defined(_WIN32)
    fp = plat_fopen(fn, "rt");
#else
    fp = plat_fopen(fn, "rt, ccs=UTF-8");
#endif
    if (fp == NULL)
        return 0;
    (void) !fread(bom, 1, 3, fp);
    if (bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}

#ifdef __HAIKU__
/* Local version of fgetws to avoid a crash */
static wchar_t *
ini_fgetws(wchar_t *str, int count, FILE *stream)
{
    int i = 0;
    if (feof(stream))
        return NULL;
    for (i = 0; i < count; i++) {
        wint_t curChar = fgetwc(stream);
        if (curChar == WEOF) {
            if (i + 1 < count)
                str[i + 1] = 0;
            return feof(stream) ? str : NULL;
        }
        str[i] = curChar;
        if (curChar == '\n')
            break;
    }
    if (i + 1 < count)
        str[i + 1] = 0;
    return str;
}
#endif

/* Read and parse the configuration file into memory. */
ini_t
ini_read(const char *fn)
{
    char       sname[128];
    char       ename[128];
    wchar_t    buff[1024];
    section_t *sec;
    section_t *ns;
    entry_t   *ne;
    int        c;
    int        d;
    int        bom;
    FILE      *fp;
    list_t    *head;

    bom = ini_detect_bom(fn);
#if defined(ANSI_CFG) || !defined(_WIN32)
    fp = plat_fopen(fn, "rt");
#else
    fp = plat_fopen(fn, "rt, ccs=UTF-8");
#endif
    if (fp == NULL)
        return NULL;

    head = malloc(sizeof(list_t));
    memset(head, 0x00, sizeof(list_t));

    sec = malloc(sizeof(section_t));
    memset(sec, 0x00, sizeof(section_t));

    list_add(&sec->list, head);
    if (bom)
        fseek(fp, 3, SEEK_SET);

    while (1) {
        memset(buff, 0x00, sizeof(buff));
#ifdef __HAIKU__
        ini_fgetws(buff, sizeof_w(buff), fp);
#else
        (void) !fgetws(buff, sizeof_w(buff), fp);
#endif
        if (feof(fp))
            break;

        /* Make sure there are no stray newlines or hard-returns in there. */
        if (wcslen(buff) > 0)
            if (buff[wcslen(buff) - 1] == L'\n')
                buff[wcslen(buff) - 1] = L'\0';
        if (wcslen(buff) > 0)
            if (buff[wcslen(buff) - 1] == L'\r')
                buff[wcslen(buff) - 1] = L'\0';

        /* Skip any leading whitespace. */
        c = 0;
        while ((buff[c] == L' ') || (buff[c] == L'\t'))
            c++;

        /* Skip empty lines. */
        if (buff[c] == L'\0')
            continue;

        /* Skip lines that (only) have a comment. */
        if ((buff[c] == L'#') || (buff[c] == L';'))
            continue;

        if (buff[c] == L'[') { /*Section*/
            c++;
            d = 0;
            while (buff[c] != L']' && buff[c])
                (void) !wctomb(&(sname[d++]), buff[c++]);
            sname[d] = L'\0';

            /* Is the section name properly terminated? */
            if (buff[c] != L']')
                continue;

            /* Create a new section and insert it. */
            ns = malloc(sizeof(section_t));
            memset(ns, 0x00, sizeof(section_t));
            memcpy(ns->name, sname, 128);
            list_add(&ns->list, head);

            /* New section is now the current one. */
            sec = ns;
            continue;
        }

        /* Get the variable name. */
        d = 0;
        while ((buff[c] != L'=') && (buff[c] != L' ') && buff[c])
            (void) !wctomb(&(ename[d++]), buff[c++]);
        ename[d] = L'\0';

        /* Skip incomplete lines. */
        if (buff[c] == L'\0')
            continue;

        /* Look for =, skip whitespace. */
        while ((buff[c] == L'=' || buff[c] == L' ') && buff[c])
            c++;

        /* Skip incomplete lines. */
        if (buff[c] == L'\0')
            continue;

        /* This is where the value part starts. */
        d = c;

        /* Allocate a new variable entry.. */
        ne = malloc(sizeof(entry_t));
        memset(ne, 0x00, sizeof(entry_t));
        memcpy(ne->name, ename, 128);
        wcsncpy(ne->wdata, &buff[d], sizeof_w(ne->wdata) - 1);
        ne->wdata[sizeof_w(ne->wdata) - 1] = L'\0';
#ifdef _WIN32 /* Make sure the string is converted to UTF-8 rather than a legacy codepage */
        c16stombs(ne->data, ne->wdata, sizeof(ne->data));
#else
        wcstombs(ne->data, ne->wdata, sizeof(ne->data));
#endif
        ne->data[sizeof(ne->data) - 1] = '\0';

        /* .. and insert it. */
        list_add(&ne->list, &sec->entry_head);
    }

    (void) fclose(fp);

    return (ini_t) head;
}

/* Write the in-memory configuration to disk. */
void
ini_write(ini_t ini, const char *fn)
{
    wchar_t    wtemp[512];
    list_t    *list = (list_t *) ini;
    section_t *sec;
    FILE      *fp;
    int        fl = 0;

    if (list == NULL)
        return;

    sec = (section_t *) list->next;

#if defined(ANSI_CFG) || !defined(_WIN32)
    fp = plat_fopen(fn, "wt");
#else
    fp = plat_fopen(fn, "wt, ccs=UTF-8");
#endif
    if (fp == NULL)
        return;

    while (sec != NULL) {
        entry_t *ent;

        if (sec->name[0]) {
            mbstowcs(wtemp, sec->name, strlen(sec->name) + 1);
            if (fl)
                fwprintf(fp, L"\n[%ls]\n", wtemp);
            else
                fwprintf(fp, L"[%ls]\n", wtemp);
            fl++;
        }

        ent = (entry_t *) sec->entry_head.next;
        while (ent != NULL) {
            if (ent->name[0] != '\0') {
                mbstowcs(wtemp, ent->name, 128);
                if (ent->wdata[0] == L'\0')
                    fwprintf(fp, L"%ls = \n", wtemp);
                else
                    fwprintf(fp, L"%ls = %ls\n", wtemp, ent->wdata);
                fl++;
            }

            ent = (entry_t *) ent->list.next;
        }

        sec = (section_t *) sec->list.next;
    }

    (void) fclose(fp);
}

ini_t
ini_new(void)
{
    ini_t ini = malloc(sizeof(list_t));
    memset(ini, 0, sizeof(list_t));
    return ini;
}

void
ini_dump(ini_t ini)
{
    section_t *sec = (section_t *) ini;
    while (sec != NULL) {
        entry_t *ent;

        if (sec->name[0])
            ini_log("[%s]\n", sec->name);

        ent = (entry_t *) sec->entry_head.next;
        while (ent != NULL) {
            ini_log("%s = %s\n", ent->name, ent->data);

            ent = (entry_t *) ent->list.next;
        }

        sec = (section_t *) sec->list.next;
    }
}

void
ini_section_delete_var(ini_section_t self, const char *name)
{
    section_t *section = (section_t *) self;
    entry_t   *entry;

    if (section == NULL)
        return;

    entry = find_entry(section, name);
    if (entry != NULL) {
        list_delete(&entry->list, &section->entry_head);
        free(entry);
    }
}

int
ini_section_get_int(ini_section_t self, const char *name, int def)
{
    section_t     *section = (section_t *) self;
    const entry_t *entry;
    int            value = 0;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    sscanf(entry->data, "%i", &value);

    return value;
}

uint32_t
ini_section_get_uint(ini_section_t self, const char *name, uint32_t def)
{
    section_t     *section = (section_t *) self;
    const entry_t *entry;
    uint32_t       value = 0;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    sscanf(entry->data, "%u", &value);

    return value;
}

#if 0
float
ini_section_get_float(ini_section_t self, const char *name, float def)
{
    section_t     *section = (section_t *) self;
    const entry_t *entry;
    float         value = 0;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    sscanf(entry->data, "%g", &value);

    return value;
}
#endif

double
ini_section_get_double(ini_section_t self, const char *name, double def)
{
    section_t     *section = (section_t *) self;
    const entry_t *entry;
    double         value = 0;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    sscanf(entry->data, "%lg", &value);

    return value;
}

int
ini_section_get_hex16(ini_section_t self, const char *name, int def)
{
    section_t     *section = (section_t *) self;
    const entry_t *entry;
    unsigned int   value = 0;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    sscanf(entry->data, "%04X", &value);

    return value;
}

int
ini_section_get_hex20(ini_section_t self, const char *name, int def)
{
    section_t     *section = (section_t *) self;
    const entry_t *entry;
    unsigned int   value = 0;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    sscanf(entry->data, "%05X", &value);

    return value;
}

int
ini_section_get_mac(ini_section_t self, const char *name, int def)
{
    section_t     *section = (section_t *) self;
    const entry_t *entry;
    unsigned int   val0 = 0;
    unsigned int   val1 = 0;
    unsigned int   val2 = 0;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    sscanf(entry->data, "%02x:%02x:%02x", &val0, &val1, &val2);

    return ((val0 << 16) + (val1 << 8) + val2);
}

char *
ini_section_get_string(ini_section_t self, const char *name, char *def)
{
    section_t *section = (section_t *) self;
    entry_t   *entry;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    return (entry->data);
}

wchar_t *
ini_section_get_wstring(ini_section_t self, const char *name, wchar_t *def)
{
    section_t *section = (section_t *) self;
    entry_t   *entry;

    if (section == NULL)
        return def;

    entry = find_entry(section, name);
    if (entry == NULL)
        return def;

    return (entry->wdata);
}

void
ini_section_set_int(ini_section_t self, const char *name, int val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    sprintf(ent->data, "%i", val);
    mbstowcs(ent->wdata, ent->data, 512);
}

void
ini_section_set_uint(ini_section_t self, const char *name, uint32_t val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    sprintf(ent->data, "%i", val);
    mbstowcs(ent->wdata, ent->data, 512);
}

#if 0
void
ini_section_set_float(ini_section_t self, const char *name, float val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    sprintf(ent->data, "%g", val);
    mbstowcs(ent->wdata, ent->data, 512);
}
#endif

void
ini_section_set_double(ini_section_t self, const char *name, double val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    sprintf(ent->data, "%lg", val);
    mbstowcs(ent->wdata, ent->data, 512);
}

void
ini_section_set_hex16(ini_section_t self, const char *name, int val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    sprintf(ent->data, "%04X", val);
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
}

void
ini_section_set_hex20(ini_section_t self, const char *name, int val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    sprintf(ent->data, "%05X", val);
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
}

void
ini_section_set_mac(ini_section_t self, const char *name, int val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    sprintf(ent->data, "%02x:%02x:%02x",
            (val >> 16) & 0xff, (val >> 8) & 0xff, val & 0xff);
    mbstowcs(ent->wdata, ent->data, 512);
}

void
ini_section_set_string(ini_section_t self, const char *name, const char *val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    if ((strlen(val) + 1) <= sizeof(ent->data))
        memcpy(ent->data, val, strlen(val) + 1);
    else
        memcpy(ent->data, val, sizeof(ent->data));
#ifdef _WIN32 /* Make sure the string is converted from UTF-8 rather than a legacy codepage */
    mbstoc16s(ent->wdata, ent->data, sizeof_w(ent->wdata));
#else
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
#endif
}

void
ini_section_set_wstring(ini_section_t self, const char *name, wchar_t *val)
{
    section_t *section = (section_t *) self;
    entry_t   *ent;

    if (section == NULL)
        return;

    ent = find_entry(section, name);
    if (ent == NULL)
        ent = create_entry(section, name);

    memcpy(ent->wdata, val, sizeof_w(ent->wdata));
#ifdef _WIN32 /* Make sure the string is converted to UTF-8 rather than a legacy codepage */
    c16stombs(ent->data, ent->wdata, sizeof(ent->data));
#else
    wcstombs(ent->data, ent->wdata, sizeof(ent->data));
#endif
}
