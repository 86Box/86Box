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
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_SMB_FS_H
#define NETWORK_NET_SMB_FS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SMB_FS_ATTR_READONLY  0x01
#define SMB_FS_ATTR_VOLUME    0x08
#define SMB_FS_ATTR_DIRECTORY 0x10

#define SMB_FS_NAME_MAX 13 /* 8.3 plus the terminator */

/* Every entry carries the same timestamp, midnight UTC on 1 January 1996. */
#define SMB_FS_UNIX_TIME 820454400u
#define SMB_FS_DOS_DATE  0x2021
#define SMB_FS_DOS_TIME  0x0000

typedef struct smb_fs_entry_t smb_fs_entry_t;

/* The folders behind the shared folder and the logon share. */
const smb_fs_entry_t *smb_fs_root(void);
const smb_fs_entry_t *smb_fs_logon_root(void);

/* Finds a path such as "DOCS\PATTERN.TXT" below a root. An empty path is the root. */
const smb_fs_entry_t *smb_fs_lookup(const smb_fs_entry_t *root, const char *path);

/* The index'th entry in a directory, or NULL past the end. */
const smb_fs_entry_t *smb_fs_child(const smb_fs_entry_t *directory, int index);

const char *smb_fs_name(const smb_fs_entry_t *entry);
bool        smb_fs_is_directory(const smb_fs_entry_t *entry);
uint32_t    smb_fs_size(const smb_fs_entry_t *entry);
uint8_t     smb_fs_attributes(const smb_fs_entry_t *entry);

/* Copies file data, returning the number of bytes read. */
int smb_fs_read(const smb_fs_entry_t *entry, uint32_t offset, uint8_t *data, int count);

/* DOS wildcard matching, including the forms NT clients send (< > "). */
bool smb_fs_match(const char *pattern, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_SMB_FS_H */
