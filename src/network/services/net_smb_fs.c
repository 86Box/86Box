/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The read-only folder served by the virtual network server.
 *          Everything here is built in, so nothing touches the host.
 *
 *          PATTERN.TXT is generated, and every line carries its own number,
 *          so a copy made by a guest can be checked for dropped or reordered
 *          data across the link.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_smb_fs.h>

#define PATTERN_LINE_LENGTH 35 /* "86Box SMB test pattern line 00000\r\n" */
#define PATTERN_LINES       1024
#define NO_PARENT           (-1)

typedef enum {
    KIND_DIRECTORY,
    KIND_TEXT,
    KIND_PATTERN
} entry_kind_t;

struct smb_fs_entry_t {
    const char  *name;
    int          parent;
    entry_kind_t kind;
    const char  *text;
};

static const char readme[] =
    "This is the " VSERVER_SERVER_COMMENT " virtual server shared folder.\r\n"
    "\r\n"
    "It is read-only and built into the emulator. No host files are shared.\r\n"
    "DOCS\\PATTERN.TXT is a generated file for checking copies over the link.\r\n";

static const smb_fs_entry_t entries[] = {
    { "",            NO_PARENT, KIND_DIRECTORY, NULL   },
    { "README.TXT",  0,         KIND_TEXT,      readme },
    { "DOCS",        0,         KIND_DIRECTORY, NULL   },
    { "PATTERN.TXT", 2,         KIND_PATTERN,   NULL   },
    { "",            NO_PARENT, KIND_DIRECTORY, NULL   } /* NETLOGON, which needs nothing in it */
};

#define ENTRY_COUNT ((int) (sizeof(entries) / sizeof(entries[0])))

const smb_fs_entry_t *
smb_fs_root(void)
{
    return &entries[0];
}

const smb_fs_entry_t *
smb_fs_logon_root(void)
{
    return &entries[4];
}

const smb_fs_entry_t *
smb_fs_child(const smb_fs_entry_t *directory, int index)
{
    int parent = (int) (directory - entries);

    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (entries[i].parent == parent && index-- == 0)
            return &entries[i];
    }
    return NULL;
}

const smb_fs_entry_t *
smb_fs_lookup(const smb_fs_entry_t *root, const char *path)
{
    const smb_fs_entry_t *entry = root;

    while (*path) {
        const char *end    = strchr(path, '\\');
        size_t      length = end ? (size_t) (end - path) : strlen(path);
        const smb_fs_entry_t *found = NULL;

        if (entry->kind != KIND_DIRECTORY)
            return NULL;
        for (int i = 0; (found = smb_fs_child(entry, i)) != NULL; i++) {
            if (strlen(found->name) == length && !memcmp(found->name, path, length))
                break;
        }
        if (!found)
            return NULL;
        entry = found;
        path += length;
        if (*path)
            path++;
    }
    return entry;
}

const char *
smb_fs_name(const smb_fs_entry_t *entry)
{
    return entry->name;
}

bool
smb_fs_is_directory(const smb_fs_entry_t *entry)
{
    return entry->kind == KIND_DIRECTORY;
}

uint32_t
smb_fs_size(const smb_fs_entry_t *entry)
{
    switch (entry->kind) {
        case KIND_TEXT:
            return (uint32_t) strlen(entry->text);
        case KIND_PATTERN:
            return PATTERN_LINE_LENGTH * PATTERN_LINES;
        default:
            return 0;
    }
}

uint8_t
smb_fs_attributes(const smb_fs_entry_t *entry)
{
    return smb_fs_is_directory(entry) ? SMB_FS_ATTR_DIRECTORY : SMB_FS_ATTR_READONLY;
}

int
smb_fs_read(const smb_fs_entry_t *entry, uint32_t offset, uint8_t *data, int count)
{
    uint32_t size = smb_fs_size(entry);

    if (count <= 0 || offset >= size)
        return 0;
    if ((uint32_t) count > size - offset)
        count = (int) (size - offset);

    if (entry->kind == KIND_TEXT) {
        memcpy(data, entry->text + offset, (size_t) count);
    } else if (entry->kind == KIND_PATTERN) {
        char line[PATTERN_LINE_LENGTH + 1] = "";

        for (int i = 0; i < count; i++) {
            uint32_t position = offset + (uint32_t) i;

            if (i == 0 || position % PATTERN_LINE_LENGTH == 0)
                snprintf(line, sizeof(line), "86Box SMB test pattern line %05u\r\n",
                         (unsigned) (position / PATTERN_LINE_LENGTH) % PATTERN_LINES);
            data[i] = (uint8_t) line[position % PATTERN_LINE_LENGTH];
        }
    } else {
        return 0;
    }
    return count;
}

static char
fs_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
}

bool
smb_fs_match(const char *pattern, const char *name)
{
    for (;;) {
        char p = *pattern;

        switch (p) {
            case 0:
                return *name == 0;

            case '*':
            case '<':
                while (*pattern == '*' || *pattern == '<')
                    pattern++;
                for (;; name++) {
                    if (smb_fs_match(pattern, name))
                        return true;
                    if (!*name)
                        return false;
                }

            case '?':
            case '>':
                /* A DOS question mark also matches nothing, at a dot or the end of a name. */
                if (*name && *name != '.')
                    name++;
                pattern++;
                break;

            case '.':
            case '"':
                /* "NAME." and "NAME.*" both match a name with no extension. */
                if (*name == '.')
                    name++;
                else if (*name)
                    return false;
                pattern++;
                break;

            default:
                if (fs_upper(p) != fs_upper(*name))
                    return false;
                pattern++;
                name++;
                break;
        }
    }
}
