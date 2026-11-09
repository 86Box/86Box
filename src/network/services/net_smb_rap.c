/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The RAP calls behind browse and share lists for the virtual
 *          network server.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/network/net_vserver_config.h>
#include <86box/log.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>
#include <86box/network/net_smb_fs.h>
#include <86box/network/net_smb_rap.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
rap_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define rap_log(priv, fmt, ...)
#endif

#define RAP_NetShareEnum     0
#define RAP_NetShareGetInfo  1
#define RAP_NetServerGetInfo 13
#define RAP_NetGroupEnum     47
#define RAP_NetUserEnum      53
#define RAP_NetUserGetGroups 59
#define RAP_NetWkstaGetInfo  63
#define RAP_NetServerEnum2   104
#define RAP_NetWkstaUserLogon 132
#define RAP_NetServerEnum3   215

#define RAP_SUCCESS       0
#define RAP_ACCESS_DENIED 5
#define RAP_NOT_SUPPORTED 50
#define RAP_NET_NAME_GONE 2310 /* NERR_NetNameNotFound */
#define RAP_MORE_DATA     234
#define RAP_INVALID_LEVEL 124

#define SV_TYPE_WORKSTATION       0x00000001u
#define SV_TYPE_SERVER            0x00000002u
#define SV_TYPE_DOMAIN_CTRL       0x00000008u
#define SV_TYPE_NT                0x00001000u
#define SV_TYPE_POTENTIAL_BROWSER 0x00010000u
#define SV_TYPE_MASTER_BROWSER    0x00040000u
#define SV_TYPE_DOMAIN_ENUM       0x80000000u
#define SERVER_TYPE               (SV_TYPE_WORKSTATION | SV_TYPE_SERVER | SV_TYPE_DOMAIN_CTRL | SV_TYPE_NT \
                                   | SV_TYPE_POTENTIAL_BROWSER | SV_TYPE_MASTER_BROWSER)

#define RAP_ACCOUNT_LENGTH 21

#define SHARE_NAME_LENGTH  13
#define SHARE_ENTRY_LENGTH 20
#define SHARE_TYPE_DISK    0
#define SHARE_TYPE_IPC     3

typedef struct rap_share_t {
    const char *name;
    uint16_t    type;
    const char *comment;
} rap_share_t;

static const rap_share_t rap_shares[] = {
    { VSERVER_SHARE_NAME,       SHARE_TYPE_DISK, VSERVER_SHARE_REMARK       },
    { VSERVER_LOGON_SHARE_NAME, SHARE_TYPE_DISK, VSERVER_LOGON_SHARE_REMARK },
    { VSERVER_IPC_SHARE_NAME,  SHARE_TYPE_IPC,  VSERVER_IPC_SHARE_REMARK }
};

#define RAP_SHARE_COUNT ((int) (sizeof(rap_shares) / sizeof(rap_shares[0])))

static uint16_t
rap_get16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static void
rap_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
}

static void
rap_put32(uint8_t *p, uint32_t value)
{
    rap_put16(p, (uint16_t) value);
    rap_put16(p + 2, (uint16_t) (value >> 16));
}

static void
rap_put_comment(uint8_t *data, int *string_pos, const char *text, uint8_t *pointer)
{
    int length = (int) strlen(text) + 1;

    rap_put32(pointer, (uint32_t) *string_pos);
    memcpy(data + *string_pos, text, (size_t) length);
    *string_pos += length;
}

static void
rap_put_name(uint8_t *data, const char *name, int size)
{
    memset(data, 0, (size_t) size);
    memcpy(data, name, strlen(name));
}

/* NetServerEnum2: the server itself, or the workgroup when domains are requested. */
static int
rap_server_enum(int level, int bufsize, uint32_t type, const char *domain, uint8_t *params, uint8_t *data)
{
    /* Windows asks for every type at once, which sets the domain bit as well. */
    bool        domains = (type & SV_TYPE_DOMAIN_ENUM) != 0 && type != 0xFFFFFFFFu;
    const char *name    = domains ? VSERVER_DOMAIN_NAME : VSERVER_SERVER_NAME;
    uint32_t    entry   = domains ? (SV_TYPE_DOMAIN_ENUM | SV_TYPE_NT) : SERVER_TYPE;
    int         length  = 0;
    int         count   = 1;
    int         size    = level == 0 ? 16 : 26;

    if (level != 0 && level != 1) {
        rap_put16(params, RAP_INVALID_LEVEL);
        return 0;
    }
    if (!domains && !(type & SERVER_TYPE) && type != 0xFFFFFFFFu)
        count = 0;
    if (!domains && domain[0] && strcmp(domain, VSERVER_DOMAIN_NAME) != 0)
        count = 0;

    if (count) {
        int comment_length = (int) strlen(domains ? VSERVER_SERVER_NAME : VSERVER_SERVER_COMMENT) + 1;

        if (bufsize < size + (level == 1 ? comment_length : 0)) {
            rap_put16(params, RAP_MORE_DATA);
            rap_put16(params + 4, 0);
            rap_put16(params + 6, 1);
            return 0;
        }
        rap_put_name(data, name, 16);
        length = size;
        if (level == 1) {
            data[16] = VSERVER_SERVER_VERSION_MAJOR;
            data[17] = VSERVER_SERVER_VERSION_MINOR;
            rap_put32(data + 18, entry);
            rap_put_comment(data, &length, domains ? VSERVER_SERVER_NAME : VSERVER_SERVER_COMMENT, data + 22);
        }
    }

    rap_put16(params + 4, (uint16_t) count);
    rap_put16(params + 6, (uint16_t) count);
    return length;
}

static int
rap_share_size(const rap_share_t *share, int level)
{
    return level == 0 ? SHARE_NAME_LENGTH : SHARE_ENTRY_LENGTH + (int) strlen(share->comment) + 1;
}

/* Writes one share's fixed part at entry, with its comment at the end of the strings. */
static void
rap_share_put(const rap_share_t *share, int level, uint8_t *data, uint8_t *entry, int *strings)
{
    rap_put_name(entry, share->name, SHARE_NAME_LENGTH);
    if (level == 1) {
        entry[SHARE_NAME_LENGTH] = 0;
        rap_put16(entry + 14, share->type);
        rap_put_comment(data, strings, share->comment, entry + 16);
    }
}

static int
rap_share_enum(int level, int bufsize, uint8_t *params, uint8_t *data)
{
    int fixed = level == 0 ? SHARE_NAME_LENGTH : SHARE_ENTRY_LENGTH;
    int count = 0;
    int used  = 0;
    int strings;

    if (level != 0 && level != 1) {
        rap_put16(params, RAP_INVALID_LEVEL);
        return 0;
    }

    while (count < RAP_SHARE_COUNT && used + rap_share_size(&rap_shares[count], level) <= bufsize) {
        used += rap_share_size(&rap_shares[count], level);
        count++;
    }

    strings = fixed * count;
    for (int i = 0; i < count; i++)
        rap_share_put(&rap_shares[i], level, data, data + fixed * i, &strings);

    if (count < RAP_SHARE_COUNT)
        rap_put16(params, RAP_MORE_DATA);
    rap_put16(params + 4, (uint16_t) count);
    rap_put16(params + 6, RAP_SHARE_COUNT);
    return strings;
}

static int
rap_share_info(const char *name, int level, int bufsize, uint8_t *params, uint8_t *data)
{
    int fixed = level == 0 ? SHARE_NAME_LENGTH : SHARE_ENTRY_LENGTH;

    if (level != 0 && level != 1) {
        rap_put16(params, RAP_INVALID_LEVEL);
        return 0;
    }
    for (int i = 0; i < RAP_SHARE_COUNT; i++) {
        int strings = fixed;

        if (strcmp(name, rap_shares[i].name) != 0)
            continue;
        if (bufsize < rap_share_size(&rap_shares[i], level)) {
            rap_put16(params, RAP_MORE_DATA);
            rap_put16(params + 4, (uint16_t) rap_share_size(&rap_shares[i], level));
            return 0;
        }
        rap_share_put(&rap_shares[i], level, data, data, &strings);
        rap_put16(params + 4, (uint16_t) strings);
        return strings;
    }
    rap_put16(params, RAP_NET_NAME_GONE);
    return 0;
}

static int
rap_server_info(int level, int bufsize, uint8_t *params, uint8_t *data)
{
    int length = 26;

    if (level == 0) {
        rap_put_name(data, VSERVER_SERVER_NAME, 16);
        rap_put16(params + 4, 16);
        return 16;
    }
    if (level != 1) {
        rap_put16(params, RAP_INVALID_LEVEL);
        return 0;
    }
    if (bufsize < length + (int) strlen(VSERVER_SERVER_COMMENT) + 1) {
        rap_put16(params, RAP_MORE_DATA);
        return 0;
    }
    rap_put_name(data, VSERVER_SERVER_NAME, 16);
    data[16] = VSERVER_SERVER_VERSION_MAJOR;
    data[17] = VSERVER_SERVER_VERSION_MINOR;
    rap_put32(data + 18, SERVER_TYPE);
    rap_put_comment(data, &length, VSERVER_SERVER_COMMENT, data + 22);
    rap_put16(params + 4, (uint16_t) length);
    return length;
}

static int
rap_wksta_info(int level, int bufsize, uint8_t *params, uint8_t *data)
{
    int length = 22;

    if (level != 10) {
        rap_put16(params, RAP_INVALID_LEVEL);
        return 0;
    }
    if (bufsize < 64) {
        rap_put16(params, RAP_MORE_DATA);
        return 0;
    }
    memset(data, 0, (size_t) length);
    rap_put_comment(data, &length, VSERVER_SERVER_NAME, data);
    rap_put_comment(data, &length, "", data + 4);
    rap_put_comment(data, &length, VSERVER_DOMAIN_NAME, data + 8);
    data[12] = VSERVER_SERVER_VERSION_MAJOR;
    data[13] = VSERVER_SERVER_VERSION_MINOR;
    rap_put_comment(data, &length, VSERVER_DOMAIN_NAME, data + 14);
    rap_put_comment(data, &length, VSERVER_OTHER_DOMAINS, data + 18);
    rap_put16(params + 4, (uint16_t) length);
    return length;
}

/* What a Windows 9x or LAN Manager client asks once it has connected as the account. Laid
   out as WB21BWDWWDDDDDDDzzzD. */
static int
rap_user_logon(bool authenticated, int bufsize, uint8_t *params, uint8_t *data)
{
    enum { FIXED_LENGTH = 78, NEVER = -1 };
    int length = FIXED_LENGTH;

    if (!authenticated) {
        rap_put16(params, RAP_ACCESS_DENIED);
        return 0;
    }
    if (bufsize < FIXED_LENGTH + 64) {
        rap_put16(params, RAP_MORE_DATA);
        rap_put16(params + 4, FIXED_LENGTH + 64);
        return 0;
    }

    memset(data, 0, FIXED_LENGTH);
    memcpy(data + 2, VSERVER_LOGON_USERNAME, strlen(VSERVER_LOGON_USERNAME));
    rap_put16(data + 24, 1);          /* an ordinary user */
    for (int at = 38; at <= 46; at += 4)
        rap_put32(data + at, (uint32_t) NEVER); /* last logoff, logoff and kickoff times */
    rap_put32(data + 58, (uint32_t) NEVER);     /* when the password must change */
    rap_put_comment(data, &length, "\\\\" VSERVER_SERVER_NAME, data + 62);
    rap_put_comment(data, &length, VSERVER_DOMAIN_NAME, data + 66);
    rap_put_comment(data, &length, VSERVER_LOGON_SCRIPT, data + 70);
    rap_put16(params + 4, (uint16_t) length);
    return length;
}

/* A list of one account name, in the fixed-width form all three list calls share. */
static int
rap_account_list(int bufsize, const char *name, uint8_t *params, uint8_t *data)
{
    rap_put16(params + 6, 1);
    if (bufsize < RAP_ACCOUNT_LENGTH) {
        rap_put16(params, RAP_MORE_DATA);
        return 0;
    }
    rap_put_name(data, name, RAP_ACCOUNT_LENGTH);
    rap_put16(params + 4, 1);
    return RAP_ACCOUNT_LENGTH;
}

/* The parameter and data descriptors after the call number are never needed. */
static int
rap_skip_descriptors(const uint8_t *in, int in_len)
{
    int pos = 2;

    for (int i = 0; i < 2; i++) {
        while (pos < in_len && in[pos])
            pos++;
        pos++;
    }
    return pos;
}

static const char *
rap_string(const uint8_t *in, int in_len, int pos)
{
    if (pos >= in_len || strnlen((const char *) in + pos, (size_t) (in_len - pos)) == (size_t) (in_len - pos))
        return NULL;
    return (const char *) in + pos;
}

int
smb_rap_dispatch(void *log, bool authenticated, const uint8_t *in, int in_len, uint8_t *params,
                 int *param_length, uint8_t *data)
{
    int pos;
    int api;

    memset(params, 0, SMB_RAP_PARAMS);
    *param_length = SMB_RAP_PARAMS;
    if (in_len < 2) {
        rap_put16(params, RAP_NOT_SUPPORTED);
        return 0;
    }
    api = rap_get16(in);
    pos = rap_skip_descriptors(in, in_len);
    rap_log(log, "SMB: RAP call %d\n", api);

    /* Enumerations report two counts, and calls that return one thing only a size. */
    if (api != RAP_NetShareEnum && api != RAP_NetServerEnum2 && api != RAP_NetServerEnum3
        && api != RAP_NetUserGetGroups && api != RAP_NetGroupEnum && api != RAP_NetUserEnum)
        *param_length = SMB_RAP_INFO_PARAMS;

    switch (api) {
        case RAP_NetShareEnum:
            if (pos + 4 > in_len)
                break;
            return rap_share_enum(rap_get16(in + pos), rap_get16(in + pos + 2), params, data);

        case RAP_NetShareGetInfo:
            {
                const char *name = rap_string(in, in_len, pos);

                if (!name)
                    break;
                pos += (int) strlen(name) + 1;
                if (pos + 4 > in_len)
                    break;
                return rap_share_info(name, rap_get16(in + pos), rap_get16(in + pos + 2), params, data);
            }

        case RAP_NetUserEnum:
            if (pos + 4 > in_len)
                break;
            return rap_account_list(rap_get16(in + pos + 2), VSERVER_LOGON_USERNAME, params, data);

        case RAP_NetGroupEnum:
            if (pos + 4 > in_len)
                break;
            return rap_account_list(rap_get16(in + pos + 2), VSERVER_LOGON_GROUP_NAME, params, data);

        case RAP_NetUserGetGroups:
            {
                const char *name = rap_string(in, in_len, pos);

                if (!name)
                    break;
                pos += (int) strlen(name) + 1;
                if (pos + 4 > in_len)
                    break;
                return rap_account_list(rap_get16(in + pos + 2), VSERVER_LOGON_GROUP_NAME, params, data);
            }

        case RAP_NetWkstaUserLogon:
            {
                /* The level is followed by a 54 byte block of names, then the buffer size. */
                enum { LOGON_BLOCK = 54 };

                if (pos + 2 + LOGON_BLOCK + 2 > in_len)
                    break;
                return rap_user_logon(authenticated, rap_get16(in + pos + 2 + LOGON_BLOCK), params, data);
            }

        case RAP_NetServerGetInfo:
            if (pos + 4 > in_len)
                break;
            return rap_server_info(rap_get16(in + pos), rap_get16(in + pos + 2), params, data);

        case RAP_NetWkstaGetInfo:
            if (pos + 4 > in_len)
                break;
            return rap_wksta_info(rap_get16(in + pos), rap_get16(in + pos + 2), params, data);

        case RAP_NetServerEnum2:
        case RAP_NetServerEnum3:
            if (pos + 8 > in_len)
                break;
            {
                const char *domain = rap_string(in, in_len, pos + 8);

                return rap_server_enum(rap_get16(in + pos), rap_get16(in + pos + 2),
                                       (uint32_t) rap_get16(in + pos + 4)
                                           | ((uint32_t) rap_get16(in + pos + 6) << 16),
                                       domain ? domain : "", params, data);
            }

        default:
            break;
    }
    rap_put16(params, RAP_NOT_SUPPORTED);
    return 0;
}
