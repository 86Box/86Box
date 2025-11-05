/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NetBIOS datagrams and the browser protocol for the virtual
 *          network server. RFC 1002 section 4.4, [MS-BRWS].
 *
 *          The server stays the master browser of its workgroup, so a guest
 *          always has somebody to ask for the browse list.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_browser.h>

#define DGM_DIRECT_UNIQUE 0x10
#define DGM_DIRECT_GROUP  0x11
#define DGM_BROADCAST     0x12
#define DGM_HEADER_LENGTH 14
#define DGM_NAME_LENGTH   34
#define DGM_FIRST_FLAGS   0x02

#define SMB_HEADER_LENGTH  32
#define SMB_COM_TRANSACTION 0x25
#define BROWSE_FRAME_HEADER_LENGTH 32
#define BROWSE_COMMENT_MAX 43

#define BROWSE_HOST_ANNOUNCEMENT    1
#define BROWSE_ANNOUNCEMENT_REQUEST 2
#define BROWSE_REQUEST_ELECTION     8
#define BROWSE_BACKUP_LIST_REQUEST  9
#define BROWSE_BACKUP_LIST_RESPONSE 10
#define BROWSE_LOCAL_MASTER_ANNOUNCEMENT 15

#define BROWSE_MAILSLOT     "\\MAILSLOT\\BROWSE"
#define NETLOGON_MAILSLOT   "\\MAILSLOT\\NET\\NETLOGON"
#define NETLOGON_REQUEST    0
#define NETLOGON_RESPONSE   6
#define NETLOGON_NO_TOKEN   0xFFFF
#define BROWSE_PERIOD_MS    60000u
#define BROWSE_SERVER_TYPE  0x0005100Bu
#define ELECTION_VERSION    1
#define ELECTION_CRITERIA   0x20010F2Eu /* Preferred master, domain controller, and NetBIOS server */

static void
dgm_put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t) (value >> 24);
    p[1] = (uint8_t) (value >> 16);
    p[2] = (uint8_t) (value >> 8);
    p[3] = (uint8_t) value;
}

static void
dgm_put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
}

static void
dgm_put_le32(uint8_t *p, uint32_t value)
{
    dgm_put_le16(p, (uint16_t) value);
    dgm_put_le16(p + 2, (uint16_t) (value >> 16));
}

static uint16_t
dgm_get_le16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static void
dgm_encode_name(uint8_t *out, const char *text, uint8_t suffix)
{
    uint8_t name[16];
    size_t  length = strlen(text);

    memset(name, ' ', sizeof(name));
    memcpy(name, text, length < 15 ? length : 15);
    name[15] = suffix;
    out[0]   = 32;
    for (int i = 0; i < 16; i++) {
        out[1 + i * 2] = (uint8_t) ('A' + (name[i] >> 4));
        out[2 + i * 2] = (uint8_t) ('A' + (name[i] & 0x0F));
    }
    out[33] = 0;
}

/* Wraps a browser frame in a mailslot write and a NetBIOS datagram. */
static int
dgm_build(uint32_t server_ip, uint8_t type, const char *mailslot, const uint8_t *destination,
          const uint8_t *frame, int frame_length, uint8_t *reply, int cap)
{
    enum { TRANSACTION_WORDS = 17 };
    const int name_size  = (int) strlen(mailslot) + 1;
    const int smb_offset = DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2;
    const int name_at    = SMB_HEADER_LENGTH + 1 + TRANSACTION_WORDS * 2 + 2;
    const int data_at    = name_at + name_size;
    const int total      = smb_offset + data_at + frame_length;
    uint8_t  *smb        = reply + smb_offset;
    uint8_t  *words      = smb + SMB_HEADER_LENGTH + 1;

    if (total > cap)
        return 0;

    memset(reply, 0, (size_t) (smb_offset + data_at));
    reply[0] = type;
    reply[1] = DGM_FIRST_FLAGS;
    dgm_put_be32(reply + 4, server_ip);
    reply[8]  = (uint8_t) (BROWSER_PORT >> 8);
    reply[9]  = (uint8_t) BROWSER_PORT;
    reply[10] = (uint8_t) ((total - DGM_HEADER_LENGTH) >> 8);
    reply[11] = (uint8_t) (total - DGM_HEADER_LENGTH);
    dgm_encode_name(reply + DGM_HEADER_LENGTH, VSERVER_SERVER_NAME, 0x00);
    memcpy(reply + DGM_HEADER_LENGTH + DGM_NAME_LENGTH, destination, DGM_NAME_LENGTH);

    memcpy(smb, "\xFFSMB", 4);
    smb[4]                = SMB_COM_TRANSACTION;
    smb[SMB_HEADER_LENGTH] = TRANSACTION_WORDS;
    dgm_put_le16(words + 2, (uint16_t) frame_length);  /* TotalDataCount */
    dgm_put_le16(words + 20, (uint16_t) data_at);      /* ParameterOffset */
    dgm_put_le16(words + 22, (uint16_t) frame_length); /* DataCount */
    dgm_put_le16(words + 24, (uint16_t) data_at);      /* DataOffset */
    words[26] = 3;                                     /* SetupCount */
    dgm_put_le16(words + 28, 1);                       /* mailslot write */
    dgm_put_le16(words + 30, 1);                       /* priority */
    dgm_put_le16(words + 32, 2);                       /* second-class mailslot */
    dgm_put_le16(words + TRANSACTION_WORDS * 2,
                 (uint16_t) (name_size + frame_length));
    memcpy(smb + name_at, mailslot, (size_t) name_size);
    memcpy(smb + data_at, frame, (size_t) frame_length);
    return total;
}

static int
browser_announcement_frame(uint8_t opcode, uint32_t type, uint8_t *frame)
{
    static const char comment[] = VSERVER_SERVER_COMMENT;
    size_t            name_length = strlen(VSERVER_SERVER_NAME);
    size_t            comment_length = sizeof(comment);

    if (name_length > 15)
        name_length = 15;
    if (comment_length > BROWSE_COMMENT_MAX)
        comment_length = BROWSE_COMMENT_MAX;

    memset(frame, 0, BROWSE_FRAME_HEADER_LENGTH);
    frame[0] = opcode;
    dgm_put_le32(frame + 2, BROWSE_PERIOD_MS);
    memcpy(frame + 6, VSERVER_SERVER_NAME, name_length);
    frame[22] = 4;
    frame[23] = 0;
    dgm_put_le32(frame + 24, type);
    frame[28] = 15;
    frame[29] = 1;
    dgm_put_le16(frame + 30, 0xAA55);
    memcpy(frame + BROWSE_FRAME_HEADER_LENGTH, comment, comment_length);
    frame[BROWSE_FRAME_HEADER_LENGTH + comment_length - 1] = 0;
    return BROWSE_FRAME_HEADER_LENGTH + (int) comment_length;
}

static int
browser_announcement_response(uint32_t server_ip, const uint8_t *request, uint8_t *reply, int reply_cap)
{
    uint8_t frame[BROWSE_FRAME_HEADER_LENGTH + BROWSE_COMMENT_MAX];
    int     length = browser_announcement_frame(BROWSE_HOST_ANNOUNCEMENT, BROWSE_SERVER_TYPE, frame);

    return dgm_build(server_ip, DGM_DIRECT_UNIQUE, BROWSE_MAILSLOT, request + DGM_HEADER_LENGTH, frame, length,
                     reply, reply_cap);
}

int
browser_announcement(uint32_t server_ip, int which, uint8_t *reply, int reply_cap)
{
    uint8_t frame[BROWSE_FRAME_HEADER_LENGTH + BROWSE_COMMENT_MAX];
    uint8_t destination[DGM_NAME_LENGTH];
    int     length;

    if (which) {
        length = browser_announcement_frame(BROWSE_LOCAL_MASTER_ANNOUNCEMENT, BROWSE_SERVER_TYPE, frame);
        dgm_encode_name(destination, VSERVER_WORKGROUP_NAME, 0x1E);
    } else {
        length = browser_announcement_frame(BROWSE_HOST_ANNOUNCEMENT, BROWSE_SERVER_TYPE, frame);
        dgm_encode_name(destination, VSERVER_WORKGROUP_NAME, 0x1D);
    }
    return dgm_build(server_ip, DGM_DIRECT_GROUP, BROWSE_MAILSLOT, destination, frame, length, reply, reply_cap);
}

/* Finds the frame inside a mailslot write, and which mailslot it was written to. */
static const uint8_t *
browser_find_frame(const uint8_t *msg, int len, int *frame_length, bool *netlogon)
{
    const int      smb_offset = DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2;
    const uint8_t *smb        = msg + smb_offset;
    int            smb_length = len - smb_offset;
    int            words;
    int            count;
    int            offset;
    int            name_at;

    if (smb_length < SMB_HEADER_LENGTH + 3 || memcmp(smb, "\xFFSMB", 4) != 0
        || smb[4] != SMB_COM_TRANSACTION)
        return NULL;
    words = smb[SMB_HEADER_LENGTH];
    if (words < 14 || SMB_HEADER_LENGTH + 1 + words * 2 + 2 > smb_length)
        return NULL;
    count   = dgm_get_le16(smb + SMB_HEADER_LENGTH + 1 + 22);
    offset  = dgm_get_le16(smb + SMB_HEADER_LENGTH + 1 + 24);
    name_at = SMB_HEADER_LENGTH + 1 + words * 2 + 2;
    if (name_at + (int) sizeof(NETLOGON_MAILSLOT) <= smb_length
        && !memcmp(smb + name_at, NETLOGON_MAILSLOT, sizeof(NETLOGON_MAILSLOT)))
        *netlogon = true;
    else if (name_at + (int) sizeof(BROWSE_MAILSLOT) <= smb_length
             && !memcmp(smb + name_at, BROWSE_MAILSLOT, sizeof(BROWSE_MAILSLOT)))
        *netlogon = false;
    else
        return NULL;
    if (count < 1 || offset + count > smb_length)
        return NULL;
    *frame_length = count;
    return smb + offset;
}

/* A Windows 9x or LAN Manager client looking for its domain's logon server. The request
   names the computer, the user and the mailslot to answer on. */
static int
netlogon_process(uint32_t server_ip, const uint8_t *msg, const uint8_t *frame, int frame_length,
                 uint8_t *reply, int cap)
{
    const char *strings[3];
    int         at = 2;
    uint8_t     response[32];
    int         length;

    if (frame_length < 2 || dgm_get_le16(frame) != NETLOGON_REQUEST)
        return 0;
    for (int i = 0; i < 3; i++) {
        size_t size;

        if (at >= frame_length)
            return 0;
        strings[i] = (const char *) frame + at;
        size       = strnlen(strings[i], (size_t) (frame_length - at));
        if (size == (size_t) (frame_length - at))
            return 0;
        at += (int) size + 1;
    }
    if (strncmp(strings[2], "\\MAILSLOT\\", 10) != 0)
        return 0;

    dgm_put_le16(response, NETLOGON_RESPONSE);
    length  = 2 + (int) sizeof("\\\\" VSERVER_SERVER_NAME);
    memcpy(response + 2, "\\\\" VSERVER_SERVER_NAME, sizeof("\\\\" VSERVER_SERVER_NAME));
    dgm_put_le16(response + length, NETLOGON_NO_TOKEN);
    return dgm_build(server_ip, DGM_DIRECT_UNIQUE, strings[2], msg + DGM_HEADER_LENGTH, response, length + 2,
                     reply, cap);
}

int
browser_process(uint32_t server_ip, const uint8_t *msg, int len, uint8_t *reply, int reply_cap)
{
    const uint8_t *frame;
    int            frame_length = 0;
    bool           netlogon     = false;

    if (!msg || !reply || len < DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2
        || (msg[0] != DGM_DIRECT_UNIQUE && msg[0] != DGM_DIRECT_GROUP && msg[0] != DGM_BROADCAST)
        || msg[DGM_HEADER_LENGTH] != 32 || msg[DGM_HEADER_LENGTH + DGM_NAME_LENGTH] != 32)
        return 0;

    frame = browser_find_frame(msg, len, &frame_length, &netlogon);
    if (!frame)
        return 0;
    if (netlogon)
        return netlogon_process(server_ip, msg, frame, frame_length, reply, reply_cap);

    if (frame[0] == BROWSE_ANNOUNCEMENT_REQUEST) {
        if (frame_length < 2 || frame[1] != 0)
            return 0;
        return browser_announcement_response(server_ip, msg, reply, reply_cap);
    }

    if (frame[0] == BROWSE_BACKUP_LIST_REQUEST && frame_length >= 6) {
        uint8_t response[32];

        response[0] = BROWSE_BACKUP_LIST_RESPONSE;
        response[1] = 0;
        memcpy(response + 2, frame + 2, 4); /* the requester's token */
        return dgm_build(server_ip, DGM_DIRECT_UNIQUE, BROWSE_MAILSLOT, msg + DGM_HEADER_LENGTH, response,
                         6, reply, reply_cap);
    }

    if (frame[0] == BROWSE_REQUEST_ELECTION) {
        uint8_t election[32];
        uint8_t destination[DGM_NAME_LENGTH];
        size_t  name_length = strlen(VSERVER_SERVER_NAME);
        size_t  request_name_length;
        size_t  available = frame_length > 14 ? (size_t) frame_length - 14 : 0;

        if (frame_length < 15 || frame[1] != ELECTION_VERSION)
            return 0;
        if (available > 16)
            available = 16;
        request_name_length = strnlen((const char *) frame + 14, available);
        if (request_name_length == available)
            return 0;

        if (name_length > 15)
            name_length = 15;
        memset(election, 0, 14);
        election[0] = BROWSE_REQUEST_ELECTION;
        election[1] = ELECTION_VERSION;
        dgm_put_le32(election + 2, ELECTION_CRITERIA);
        dgm_put_le32(election + 6, 0);
        memcpy(election + 14, VSERVER_SERVER_NAME, name_length);
        dgm_encode_name(destination, VSERVER_WORKGROUP_NAME, 0x1E);
        return dgm_build(server_ip, DGM_DIRECT_GROUP, BROWSE_MAILSLOT, destination, election,
                         14 + (int) name_length + 1, reply, reply_cap);
    }

    return 0;
}
