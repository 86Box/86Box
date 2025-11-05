/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SMB1 server for the virtual server: the NetBIOS
 *          session layer, user-level authentication with guest fallback, IPC$
 *          and read-only shares.
 *
 *          The server negotiates everything from the core protocol to NT LM
 *          0.12 and behaves like Windows 9x at the top: no Unicode, no NT
 *          SMBs and DOS error codes. Clients of every era already fall back
 *          to that, which leaves one set of file commands to serve them all.
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/network/net_vserver_config.h>
#include <86box/log.h>
#include <86box/modem/modem_crypto.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>
#include <86box/network/net_smb_fs.h>
#include <86box/network/net_smb_rap.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
smb_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define smb_log(priv, fmt, ...)
#endif

#define NBT_MESSAGE          0x00
#define NBT_SESSION_REQUEST  0x81
#define NBT_POSITIVE         0x82
#define NBT_NEGATIVE         0x83
#define NBT_KEEPALIVE        0x85
#define NBT_NOT_LISTENING    0x80
#define NBT_NAME_NOT_PRESENT 0x82
#define NBT_HEADER_LENGTH    4
#define NBT_NAME_LENGTH      34

#define SMB_HEADER_LENGTH 32
#define SMB_MAX_INPUT     8192
#define SMB_MAX_BUFFER    4356
#define SMB_MAX_READ      4096
#define SMB_MAX_READ_OVERHEAD 64 /* what a read reply needs besides the data */
#define SMB_MAX_MPX       2
#define SMB_MAX_REPLY     (SMB_MAX_BUFFER + 512)
#define SMB_MAX_CHAIN     256 /* an AndX chain stops growing once its reply is this long */

#define SMB_COM_CREATE_DIRECTORY  0x00
#define SMB_COM_DELETE_DIRECTORY  0x01
#define SMB_COM_OPEN              0x02
#define SMB_COM_CREATE            0x03
#define SMB_COM_CLOSE             0x04
#define SMB_COM_FLUSH             0x05
#define SMB_COM_DELETE            0x06
#define SMB_COM_RENAME            0x07
#define SMB_COM_QUERY_INFORMATION 0x08
#define SMB_COM_SET_INFORMATION   0x09
#define SMB_COM_READ              0x0A
#define SMB_COM_WRITE             0x0B
#define SMB_COM_LOCK              0x0C
#define SMB_COM_UNLOCK            0x0D
#define SMB_COM_CREATE_TEMPORARY  0x0E
#define SMB_COM_CREATE_NEW        0x0F
#define SMB_COM_CHECK_DIRECTORY   0x10
#define SMB_COM_PROCESS_EXIT      0x11
#define SMB_COM_SEEK              0x12
#define SMB_COM_SET_INFORMATION2  0x22
#define SMB_COM_QUERY_INFORMATION2 0x23
#define SMB_COM_LOCKING_ANDX      0x24
#define SMB_COM_TRANSACTION       0x25
#define SMB_COM_ECHO              0x2B
#define SMB_COM_OPEN_ANDX         0x2D
#define SMB_COM_READ_ANDX         0x2E
#define SMB_COM_WRITE_ANDX        0x2F
#define SMB_COM_TRANSACTION2      0x32
#define SMB_COM_FIND_CLOSE2       0x34
#define SMB_COM_TREE_CONNECT      0x70
#define SMB_COM_TREE_DISCONNECT   0x71
#define SMB_COM_NEGOTIATE         0x72
#define SMB_COM_SESSION_SETUP     0x73
#define SMB_COM_LOGOFF            0x74
#define SMB_COM_TREE_CONNECT_ANDX 0x75
#define SMB_COM_QUERY_INFORMATION_DISK 0x80
#define SMB_COM_SEARCH            0x81
#define SMB_COM_FIND              0x82
#define SMB_COM_FIND_UNIQUE       0x83
#define SMB_COM_FIND_CLOSE        0x84
#define SMB_ANDX_NONE             0xFF

#define SMB_FLAGS_REPLY    0x80
#define SMB_FLAGS2_LONG_NAMES 0x0001

#define SMB_SECURITY_USER      0x01
#define SMB_SECURITY_ENCRYPT   0x02
#define SMB_CHALLENGE_LENGTH   8
#define SMB_RESPONSE_LENGTH    24
#define SMB_ACTION_GUEST       0x0001
#define SMB_SESSION_WORDS_LM   10
#define SMB_SESSION_WORDS_NT   13
#define SMB_SUPPORT_SEARCH_BITS 0x0001

/* An error class and code travel back through a handler's return value. */
#define SMB_FAIL(class, code) (-(((class) << 16) | (code)))
#define SMB_ERRDOS 1
#define SMB_ERRSRV 2

#define SMB_ERRerror       1
#define SMB_ERRbadfunc     1
#define SMB_ERRbadfile     2
#define SMB_ERRbadpath     3
#define SMB_ERRnofids      4
#define SMB_ERRnoaccess    5
#define SMB_ERRbadfid      6
#define SMB_ERRnofiles     18
#define SMB_ERRfilexists   80
#define SMB_ERRunknownlevel 124
#define SMB_ERRbadpipe     230
#define SMB_ERRinvnetname  6
#define SMB_ERRinvtid      5
#define SMB_ERRnosupport   0xFFFF

#define SMB_UID 100
#define SMB_MAX_TREES    8
#define SMB_MAX_FILES    16
#define SMB_MAX_FINDS    8
#define SMB_MAX_SEARCHES 4
#define SMB_PATH_MAX     128

#define SMB_ACCESS_MASK     0x0007
#define SMB_ACCESS_READ     0
#define SMB_ACCESS_EXECUTE  3
#define SMB_OPEN_EXISTS_MASK 0x0003
#define SMB_OPEN_FAIL_EXISTS 0
#define SMB_OPEN_OPEN        1
#define SMB_OPEN_CREATE      0x0010
#define SMB_OPEN_RESULT_OPENED 1

#define SMB_RESUME_KEY_LENGTH 21
#define SMB_DIR_ENTRY_LENGTH  43
#define SMB_DIR_POSITION_DOTS 2 /* "." and ".." come before a directory's entries */

#define TRANS2_OPEN2             0x00
#define TRANS2_FIND_FIRST2       0x01
#define TRANS2_FIND_NEXT2        0x02
#define TRANS2_QUERY_FS          0x03
#define TRANS2_SET_FS            0x04
#define TRANS2_QUERY_PATH        0x05
#define TRANS2_SET_PATH          0x06
#define TRANS2_QUERY_FILE        0x07
#define TRANS2_SET_FILE          0x08
#define TRANS2_CREATE_DIRECTORY  0x0D
#define TRANS2_MAX_PARAMS        12
#define TRANS2_MAX_DATA          3072
#define TRANS2_REQUEST_WORDS     15
#define TRANS2_REPLY_WORDS       10

#define FIND_CLOSE_AFTER_REQUEST 0x0001
#define FIND_CLOSE_AT_EOS        0x0002
#define FIND_RETURN_RESUME_KEYS  0x0004
#define FIND_CONTINUE            0x0008

#define INFO_STANDARD        0x0001
#define INFO_QUERY_EA_SIZE   0x0002
#define INFO_QUERY_EAS       0x0003
#define INFO_QUERY_ALL_EAS   0x0004
#define INFO_IS_NAME_VALID   0x0006
#define FIND_FILE_DIRECTORY  0x0101
#define FIND_FILE_FULL       0x0102
#define FIND_FILE_NAMES      0x0103
#define FIND_FILE_BOTH       0x0104
#define QUERY_FILE_BASIC     0x0101
#define QUERY_FILE_STANDARD  0x0102
#define QUERY_FILE_EA        0x0103
#define QUERY_FILE_NAME      0x0104
#define QUERY_FILE_ALL       0x0107
#define QUERY_FILE_ALT_NAME  0x0108
#define FS_INFO_ALLOCATION   0x0001
#define FS_INFO_VOLUME       0x0002
#define FS_QUERY_VOLUME      0x0102
#define FS_QUERY_SIZE        0x0103
#define FS_QUERY_DEVICE      0x0104
#define FS_QUERY_ATTRIBUTE   0x0105

#define FS_DEVICE_DISK        7
#define FS_DEVICE_READ_ONLY   0x00000002u
#define FS_ATTRIBUTE_CASE     0x00000002u
#define FS_ATTRIBUTE_READ_ONLY 0x00080000u
#define FS_SERIAL_NUMBER      0x86B0086Bu
#define FS_NAME               "FAT"
#define FS_SECTOR_SIZE        512
#define FS_SECTORS_PER_UNIT   64
#define FS_TOTAL_UNITS        2048

#define UNIX_TO_FILETIME_SECONDS 11644473600ull
#define FILETIME_PER_SECOND      10000000ull

typedef enum {
    SMB_LEVEL_CORE,
    SMB_LEVEL_LANMAN,
    SMB_LEVEL_NT
} smb_level_t;

typedef struct smb_dialect_t {
    const char *name;
    smb_level_t level;
} smb_dialect_t;

typedef enum {
    SMB_TREE_NONE,
    SMB_TREE_IPC,
    SMB_TREE_SHARE,
    SMB_TREE_LOGON
} smb_tree_t;

typedef struct smb_file_t {
    const smb_fs_entry_t *entry;
    uint32_t              position;
    uint16_t              pid;
    uint16_t              tid;
} smb_file_t;

typedef struct smb_search_t {
    const smb_fs_entry_t *root;
    const smb_fs_entry_t *directory;
    int                   position;
    uint16_t              attributes;
    uint16_t              tid;
    char                  pattern[SMB_PATH_MAX];
} smb_search_t;

struct smb_session_t {
    void        *log;
    bool         established;
    int          max_buffer;
    int          max_read;
    bool         logged_on;
    bool         authenticated; /* this is the domain's account rather than a guest */
    smb_level_t  level;
    smb_tree_t   trees[SMB_MAX_TREES]; /* indexed by tree ID less one */
    uint16_t     chain_fid;
    int          next_search;
    smb_file_t   files[SMB_MAX_FILES];
    smb_search_t finds[SMB_MAX_FINDS];
    smb_search_t searches[SMB_MAX_SEARCHES];
    int          input_len;
    uint8_t      input[SMB_MAX_INPUT + NBT_HEADER_LENGTH];
};

/* One command within a message: the block of it at in, and its reply at pos. */
typedef struct smb_request_t {
    smb_session_t *session;
    const uint8_t *msg;
    int            len;
    int            in;
    uint8_t       *out;
    int            pos;
} smb_request_t;

/* Handlers return the length of the reply block at pos, or an SMB_FAIL value. */
typedef int (*smb_handler_t)(smb_request_t *request);

/* The dialects served, best first. */
static const smb_dialect_t smb_dialects[] = {
    { "NT LM 0.12",                  SMB_LEVEL_NT     },
    { "NT LANMAN 1.0",               SMB_LEVEL_NT     },
    { "LANMAN2.1",                   SMB_LEVEL_LANMAN },
    { "DOS LANMAN2.1",               SMB_LEVEL_LANMAN },
    { "LM1.2X002",                   SMB_LEVEL_LANMAN },
    { "DOS LM1.2X002",               SMB_LEVEL_LANMAN },
    { "Windows for Workgroups 3.1a", SMB_LEVEL_LANMAN },
    { "LANMAN1.0",                   SMB_LEVEL_LANMAN },
    { "MICROSOFT NETWORKS 3.0",      SMB_LEVEL_LANMAN },
    { "MICROSOFT NETWORKS 1.03",     SMB_LEVEL_LANMAN },
    { "PC NETWORK PROGRAM 1.0",      SMB_LEVEL_CORE   },
    { "PCLAN1.0",                    SMB_LEVEL_CORE   }
};

/* Fixed, because the password is a public default and nothing here needs defending. */
static const uint8_t smb_challenge[SMB_CHALLENGE_LENGTH] = { '8', '6', 'B', 'o', 'x', 'S', 'M', 'B' };

static uint16_t
smb_get16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t
smb_get32(const uint8_t *p)
{
    return smb_get16(p) | ((uint32_t) smb_get16(p + 2) << 16);
}

static void
smb_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
}

static void
smb_put32(uint8_t *p, uint32_t value)
{
    smb_put16(p, (uint16_t) value);
    smb_put16(p + 2, (uint16_t) (value >> 16));
}

static void
smb_put64(uint8_t *p, uint64_t value)
{
    smb_put32(p, (uint32_t) value);
    smb_put32(p + 4, (uint32_t) (value >> 32));
}

static int
smb_put_string(uint8_t *p, const char *text)
{
    size_t length = strlen(text) + 1;

    memcpy(p, text, length);
    return (int) length;
}

static uint64_t
smb_filetime(uint32_t unix_time)
{
    return ((uint64_t) unix_time + UNIX_TO_FILETIME_SECONDS) * FILETIME_PER_SECOND;
}

static int
smb_words(const smb_request_t *request)
{
    return request->msg[request->in];
}

static uint16_t
smb_word(const smb_request_t *request, int index)
{
    return index < smb_words(request) ? smb_get16(request->msg + request->in + 1 + index * 2) : 0;
}

static uint32_t
smb_dword(const smb_request_t *request, int index)
{
    return smb_word(request, index) | ((uint32_t) smb_word(request, index + 1) << 16);
}

static int
smb_bytes_at(const smb_request_t *request)
{
    return request->in + 1 + smb_words(request) * 2 + 2;
}

static int
smb_bytes_end(const smb_request_t *request)
{
    int start = smb_bytes_at(request);
    int end   = start + smb_get16(request->msg + start - 2);

    return end < request->len ? end : request->len;
}

static uint8_t *
smb_reply_words(smb_request_t *request, int words)
{
    memset(request->out + request->pos, 0, (size_t) (1 + words * 2 + 2));
    request->out[request->pos] = (uint8_t) words;
    return request->out + request->pos + 1;
}

static int
smb_reply_length(smb_request_t *request, int words, int bytes)
{
    smb_put16(request->out + request->pos + 1 + words * 2, (uint16_t) bytes);
    return 1 + words * 2 + 2 + bytes;
}

static int
smb_error(uint8_t *out, int fail)
{
    fail = -fail;
    out[5] = (uint8_t) (fail >> 16);
    out[6] = 0;
    smb_put16(out + 7, (uint16_t) fail);
    memset(out + SMB_HEADER_LENGTH, 0, 3);
    return SMB_HEADER_LENGTH + 3;
}

/* Copies a path out of a message into the form the folder uses: upper case with single
   backslashes and none at either end. It is read up to end, advancing pos past it. */
static bool
smb_read_path(const smb_request_t *request, int *pos, int end, bool buffer_format, char *path)
{
    const uint8_t *msg = request->msg;
    int            out = 0;
    int            at  = *pos;

    if (buffer_format) {
        if (at >= end || msg[at] != 0x04)
            return false;
        at++;
    }

    for (;; at++) {
        char c;

        if (at >= end)
            return false;
        c = (char) msg[at];
        if (!c)
            break;
        if (c == '/')
            c = '\\';
        else if (c >= 'a' && c <= 'z')
            c = (char) (c - 'a' + 'A');
        if (c == '\\' && (out == 0 || path[out - 1] == '\\'))
            continue;
        if (out >= SMB_PATH_MAX - 1)
            return false;
        path[out++] = c;
    }
    if (out && path[out - 1] == '\\')
        out--;
    path[out] = 0;
    *pos      = at + 1;

    for (const char *part = path; *part;) {
        size_t length = strcspn(part, "\\");

        if (length == 2 && part[0] == '.' && part[1] == '.')
            return false;
        part += length;
        if (*part)
            part++;
    }
    return true;
}

static bool
smb_share_connected(const smb_request_t *request)
{
    uint16_t tid = smb_get16(request->out + 24);

    return tid >= 1 && tid <= SMB_MAX_TREES
        && (request->session->trees[tid - 1] == SMB_TREE_SHARE
            || request->session->trees[tid - 1] == SMB_TREE_LOGON);
}

/* Only valid once smb_share_connected has said the tree holds files. */
static const smb_fs_entry_t *
smb_tree_root(const smb_request_t *request)
{
    uint16_t tid = smb_get16(request->out + 24);

    return request->session->trees[tid - 1] == SMB_TREE_LOGON ? smb_fs_logon_root() : smb_fs_root();
}

static int
smb_share_lookup(const smb_request_t *request, const char *path, const smb_fs_entry_t **entry)
{
    const char *last;

    if (!smb_share_connected(request))
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvtid);

    *entry = smb_fs_lookup(smb_tree_root(request), path);
    if (*entry)
        return 0;

    /* A missing file and a missing directory on the way to it are different errors. */
    last = strrchr(path, '\\');
    if (last) {
        char                  parent[SMB_PATH_MAX];
        const smb_fs_entry_t *directory;

        memcpy(parent, path, (size_t) (last - path));
        parent[last - path] = 0;
        directory = smb_fs_lookup(smb_tree_root(request), parent);
        if (!directory || !smb_fs_is_directory(directory))
            return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);
    }
    return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfile);
}

static smb_file_t *
smb_file_get(smb_session_t *session, uint16_t fid)
{
    if (fid == 0xFFFF)
        fid = session->chain_fid;
    if (fid < 1 || fid > SMB_MAX_FILES || !session->files[fid - 1].entry)
        return NULL;
    return &session->files[fid - 1];
}

static int
smb_file_open(smb_request_t *request, const smb_fs_entry_t *entry)
{
    smb_session_t *session = request->session;

    for (int i = 0; i < SMB_MAX_FILES; i++) {
        if (!session->files[i].entry) {
            session->files[i].entry    = entry;
            session->files[i].position = 0;
            session->files[i].pid      = smb_get16(request->msg + 26);
            session->files[i].tid      = smb_get16(request->out + 24);
            session->chain_fid         = (uint16_t) (i + 1);
            return i + 1;
        }
    }
    return SMB_FAIL(SMB_ERRDOS, SMB_ERRnofids);
}

static void
smb_tree_disconnect(smb_session_t *session, uint16_t tid)
{
    if (tid < 1 || tid > SMB_MAX_TREES)
        return;
    session->trees[tid - 1] = SMB_TREE_NONE;
    for (int i = 0; i < SMB_MAX_FILES; i++) {
        if (session->files[i].tid == tid)
            memset(&session->files[i], 0, sizeof(smb_file_t));
    }
    for (int i = 0; i < SMB_MAX_FINDS; i++) {
        if (session->finds[i].tid == tid)
            memset(&session->finds[i], 0, sizeof(smb_search_t));
    }
    for (int i = 0; i < SMB_MAX_SEARCHES; i++) {
        if (session->searches[i].tid == tid)
            memset(&session->searches[i], 0, sizeof(smb_search_t));
    }
}

/* Takes a new tree ID for the share at the end of a UNC path, or fails with an SMB error. */
static int
smb_tree_connect(smb_session_t *session, const char *path, smb_tree_t *type)
{
    const char *last = strrchr(path, '\\');

    last = last ? last + 1 : path;
    if (!strcmp(last, VSERVER_IPC_SHARE_NAME))
        *type = SMB_TREE_IPC;
    else if (!strcmp(last, VSERVER_SHARE_NAME))
        *type = SMB_TREE_SHARE;
    else if (!strcmp(last, VSERVER_LOGON_SHARE_NAME)) {
        if (!session->authenticated)
            return SMB_FAIL(SMB_ERRDOS, SMB_ERRnoaccess);
        *type = SMB_TREE_LOGON;
    } else
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvnetname);

    for (int i = 0; i < SMB_MAX_TREES; i++) {
        if (session->trees[i] == SMB_TREE_NONE) {
            session->trees[i] = *type;
            return i + 1;
        }
    }
    return SMB_FAIL(SMB_ERRSRV, SMB_ERRerror);
}

static int
smb_cmd_negotiate(smb_request_t *request)
{
    smb_session_t *session = request->session;
    const uint8_t *msg     = request->msg;
    int            offered = 0;
    int            best    = (int) (sizeof(smb_dialects) / sizeof(smb_dialects[0]));
    int            chosen  = -1;
    int            pos     = smb_bytes_at(request);
    uint8_t       *reply;

    while (pos < request->len && msg[pos] == 0x02) {
        const char *name = (const char *) msg + pos + 1;
        size_t      left = (size_t) (request->len - pos - 1);
        size_t      nlen = strnlen(name, left);

        if (nlen == left)
            break;
        for (int i = 0; i < best; i++) {
            if (!strcmp(name, smb_dialects[i].name)) {
                best   = i;
                chosen = offered;
            }
        }
        offered++;
        pos += (int) nlen + 2;
    }

    smb_log(session->log, "SMB: Negotiate: %d dialects offered, chose %d\n", offered, chosen);

    if (chosen < 0) {
        reply = smb_reply_words(request, 1);
        smb_put16(reply, 0xFFFF);
        return smb_reply_length(request, 1, 0);
    }
    session->level = smb_dialects[best].level;
    smb_log(session->log, "SMB: Dialect %s\n", smb_dialects[best].name);

    if (session->level == SMB_LEVEL_CORE) {
        reply = smb_reply_words(request, 1);
        smb_put16(reply, (uint16_t) chosen);
        return smb_reply_length(request, 1, 0);
    }

    if (session->level == SMB_LEVEL_LANMAN) {
        reply = smb_reply_words(request, 13);
        smb_put16(reply, (uint16_t) chosen);
        smb_put16(reply + 2, SMB_SECURITY_USER | SMB_SECURITY_ENCRYPT);
        smb_put16(reply + 4, (uint16_t) session->max_buffer);
        smb_put16(reply + 6, SMB_MAX_MPX);
        smb_put16(reply + 8, 1);                      /* MaxNumberVcs */
        smb_put16(reply + 22, SMB_CHALLENGE_LENGTH);
        memcpy(reply + 13 * 2 + 2, smb_challenge, SMB_CHALLENGE_LENGTH);
        return smb_reply_length(request, 13, SMB_CHALLENGE_LENGTH);
    }

    /* NT LM packs its words differently, and a byte in the security mode throws off all
       that follow it. */
    {
        int bytes;

        reply = smb_reply_words(request, 17);
        smb_put16(reply, (uint16_t) chosen);
        reply[2] = SMB_SECURITY_USER | SMB_SECURITY_ENCRYPT;
        smb_put16(reply + 3, SMB_MAX_MPX);
        smb_put16(reply + 5, 1); /* MaxNumberVcs */
        smb_put32(reply + 7, (uint32_t) session->max_buffer);
        smb_put64(reply + 23, smb_filetime((uint32_t) time(NULL)));
        reply[33] = SMB_CHALLENGE_LENGTH;
        memcpy(reply + 36, smb_challenge, SMB_CHALLENGE_LENGTH);
        bytes = SMB_CHALLENGE_LENGTH + smb_put_string(reply + 36 + SMB_CHALLENGE_LENGTH, VSERVER_DOMAIN_NAME);
        return smb_reply_length(request, 17, bytes);
    }
}

/* Turns a 21 byte padded hash into the 24 byte answer to the challenge. */
static void
smb_challenge_response(const uint8_t hash[21], uint8_t response[SMB_RESPONSE_LENGTH])
{
    for (int i = 0; i < 3; i++)
        modem_des_encrypt_block(hash + i * 7, smb_challenge, response + i * 8);
}

static void
smb_lm_response(const char *password, uint8_t response[SMB_RESPONSE_LENGTH])
{
    static const uint8_t magic[8] = { 'K', 'G', 'S', '!', '@', '#', '$', '%' };
    uint8_t              upper[14] = { 0 };
    uint8_t              hash[21]  = { 0 };

    for (int i = 0; i < 14 && password[i]; i++) {
        char c = password[i];

        upper[i] = (uint8_t) ((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
    }
    modem_des_encrypt_block(upper, magic, hash);
    modem_des_encrypt_block(upper + 7, magic, hash + 8);
    smb_challenge_response(hash, response);
}

static void
smb_nt_response(const char *password, uint8_t response[SMB_RESPONSE_LENGTH])
{
    uint8_t unicode[32];
    uint8_t hash[21] = { 0 };
    int     length   = 0;

    for (; password[length] && length < 16; length++) {
        unicode[length * 2]     = (uint8_t) password[length];
        unicode[length * 2 + 1] = 0;
    }
    modem_md4(unicode, (size_t) length * 2, hash);
    smb_challenge_response(hash, response);
}

static bool
smb_name_equal(const char *left, const char *right)
{
    for (;; left++, right++) {
        char l = *left;
        char r = *right;

        if (l >= 'a' && l <= 'z')
            l = (char) (l - 'a' + 'A');
        if (r >= 'a' && r <= 'z')
            r = (char) (r - 'a' + 'A');
        if (l != r)
            return false;
        if (!l)
            return true;
    }
}

static void
smb_hmac_md5(const uint8_t *key, int key_length, const uint8_t *first, int first_length,
             const uint8_t *second, int second_length, uint8_t digest[MD5_DIGEST_LENGTH])
{
    enum { BLOCK = 64 };
    uint8_t         inner_pad[BLOCK];
    uint8_t         outer_pad[BLOCK];
    uint8_t         inner[MD5_DIGEST_LENGTH];
    modem_md5_ctx_t context;

    for (int i = 0; i < BLOCK; i++) {
        uint8_t byte = i < key_length ? key[i] : 0;

        inner_pad[i] = byte ^ 0x36;
        outer_pad[i] = byte ^ 0x5C;
    }
    modem_md5_init(&context);
    modem_md5_update(&context, inner_pad, BLOCK);
    modem_md5_update(&context, first, (size_t) first_length);
    if (second_length)
        modem_md5_update(&context, second, (size_t) second_length);
    modem_md5_final(&context, inner);

    modem_md5_init(&context);
    modem_md5_update(&context, outer_pad, BLOCK);
    modem_md5_update(&context, inner, MD5_DIGEST_LENGTH);
    modem_md5_final(&context, digest);
}

/* The NTLMv2 key, made from the password and the upper case user name plus the domain. */
static void
smb_ntlmv2_hash(const char *password, const char *user, const char *domain, uint8_t hash[MD5_DIGEST_LENGTH])
{
    enum { IDENTITY_MAX = 128 };
    uint8_t identity[IDENTITY_MAX * 2];
    uint8_t unicode[32];
    uint8_t nt_hash[MD4_DIGEST_LENGTH];
    int     length = 0;
    int     wide   = 0;

    for (; password[wide] && wide < 16; wide++) {
        unicode[wide * 2]     = (uint8_t) password[wide];
        unicode[wide * 2 + 1] = 0;
    }
    modem_md4(unicode, (size_t) wide * 2, nt_hash);

    for (; *user && length < IDENTITY_MAX; user++, length++) {
        char c = *user;

        identity[length * 2]     = (uint8_t) ((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
        identity[length * 2 + 1] = 0;
    }
    for (; *domain && length < IDENTITY_MAX; domain++, length++) {
        identity[length * 2]     = (uint8_t) *domain;
        identity[length * 2 + 1] = 0;
    }
    smb_hmac_md5(nt_hash, MD4_DIGEST_LENGTH, identity, length * 2, NULL, 0, hash);
}

/* Both NTLMv2 answers are a keyed digest of the challenge and a client block, followed by
   that block. For the LM slot the block is an 8 byte nonce. */
static bool
smb_ntlmv2_valid(const uint8_t *hash, const uint8_t *answer, int length)
{
    uint8_t expected[MD5_DIGEST_LENGTH];

    if (length <= MD5_DIGEST_LENGTH)
        return false;
    smb_hmac_md5(hash, MD5_DIGEST_LENGTH, smb_challenge, SMB_CHALLENGE_LENGTH, answer + MD5_DIGEST_LENGTH,
                 length - MD5_DIGEST_LENGTH, expected);
    return modem_constant_time_equal(answer, expected, MD5_DIGEST_LENGTH);
}

/* Windows 9x sends only the LM response, later systems both, and clients set to use NTLMv2
   put its forms in the same two slots. Any one of them proves the password. */
static bool
smb_credentials_valid(const smb_request_t *request)
{
    const uint8_t *msg  = request->msg;
    int            at   = smb_bytes_at(request);
    int            end  = smb_bytes_end(request);
    int            oem  = smb_word(request, 7);
    int            wide = smb_words(request) >= SMB_SESSION_WORDS_NT ? smb_word(request, 8) : 0;
    int            user = at + oem + wide;
    uint8_t        expected[SMB_RESPONSE_LENGTH];
    uint8_t        v2_hash[MD5_DIGEST_LENGTH];
    const char    *name;
    const char    *domain = "";
    int            domain_at;
    bool           valid = false;

    if (user >= end || strnlen((const char *) msg + user, (size_t) (end - user)) == (size_t) (end - user)
        || !smb_name_equal((const char *) msg + user, VSERVER_LOGON_USERNAME))
        return false;
    name = (const char *) msg + user;

    if (oem == SMB_RESPONSE_LENGTH) {
        smb_lm_response(VSERVER_LOGON_PASSWORD, expected);
        valid = modem_constant_time_equal(msg + at, expected, SMB_RESPONSE_LENGTH);
    }
    if (!valid && wide == SMB_RESPONSE_LENGTH) {
        smb_nt_response(VSERVER_LOGON_PASSWORD, expected);
        valid = modem_constant_time_equal(msg + at + oem, expected, SMB_RESPONSE_LENGTH);
    }
    if (valid)
        return true;

    domain_at = user + (int) strlen(name) + 1;
    if (domain_at < end && strnlen((const char *) msg + domain_at, (size_t) (end - domain_at))
                               < (size_t) (end - domain_at))
        domain = (const char *) msg + domain_at;
    smb_ntlmv2_hash(VSERVER_LOGON_PASSWORD, name, domain, v2_hash);
    return (oem == SMB_RESPONSE_LENGTH && smb_ntlmv2_valid(v2_hash, msg + at, oem))
        || (wide > SMB_RESPONSE_LENGTH && smb_ntlmv2_valid(v2_hash, msg + at + oem, wide));
}

static int
smb_cmd_session_setup(smb_request_t *request)
{
    smb_session_t *session = request->session;
    uint8_t       *reply;
    uint8_t       *bytes;
    int            length;

    session->authenticated = smb_words(request) >= SMB_SESSION_WORDS_LM && smb_credentials_valid(request);
    session->logged_on     = true;
    smb_log(session->log, "SMB: Session for %s\n", session->authenticated ? "the domain account" : "a guest");

    reply = smb_reply_words(request, 3);
    bytes = reply + 3 * 2 + 2;
    smb_put16(request->out + 28, SMB_UID);
    smb_put16(reply + 4, session->authenticated ? 0 : SMB_ACTION_GUEST);

    length  = smb_put_string(bytes, VSERVER_NATIVE_OS_NAME);
    length += smb_put_string(bytes + length, VSERVER_NATIVE_LANMAN_NAME);
    length += smb_put_string(bytes + length, VSERVER_DOMAIN_NAME);
    return smb_reply_length(request, 3, length);
}

static int
smb_cmd_tree_connect_andx(smb_request_t *request)
{
    smb_session_t *session = request->session;
    char           path[SMB_PATH_MAX];
    int            pos = smb_bytes_at(request);
    smb_tree_t     type;
    int            tid;
    uint8_t       *reply;
    uint8_t       *bytes;
    int            length;

    if (smb_words(request) < 4)
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport);
    pos += smb_word(request, 3); /* the password, which is never needed */
    if (!smb_read_path(request, &pos, request->len, false, path))
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvnetname);
    tid = smb_tree_connect(session, path, &type);
    if (tid < 0)
        return tid;
    smb_put16(request->out + 24, (uint16_t) tid);

    if (session->level != SMB_LEVEL_NT) {
        reply  = smb_reply_words(request, 2);
        bytes  = reply + 2 * 2 + 2;
        length = smb_put_string(bytes, type == SMB_TREE_IPC ? "IPC" : "A:");
        return smb_reply_length(request, 2, length);
    }

    reply = smb_reply_words(request, 3);
    bytes = reply + 3 * 2 + 2;
    smb_put16(reply + 4, SMB_SUPPORT_SEARCH_BITS);
    length  = smb_put_string(bytes, type == SMB_TREE_IPC ? "IPC" : "A:");
    length += smb_put_string(bytes + length, type == SMB_TREE_IPC ? "" : FS_NAME);
    return smb_reply_length(request, 3, length);
}

static int
smb_cmd_tree_connect(smb_request_t *request)
{
    smb_session_t *session = request->session;
    char           path[SMB_PATH_MAX];
    int            pos = smb_bytes_at(request);
    smb_tree_t     type;
    int            tid;
    uint8_t       *reply;

    if (!smb_read_path(request, &pos, smb_bytes_end(request), true, path))
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvnetname);
    tid = smb_tree_connect(session, path, &type);
    if (tid < 0)
        return tid;
    smb_put16(request->out + 24, (uint16_t) tid);

    reply = smb_reply_words(request, 2);
    smb_put16(reply, (uint16_t) request->session->max_buffer);
    smb_put16(reply + 2, (uint16_t) tid);
    return smb_reply_length(request, 2, 0);
}

static int
smb_cmd_tree_disconnect(smb_request_t *request)
{
    smb_tree_disconnect(request->session, smb_get16(request->msg + 24));
    smb_reply_words(request, 0);
    return smb_reply_length(request, 0, 0);
}

static int
smb_cmd_logoff(smb_request_t *request)
{
    request->session->logged_on     = false;
    request->session->authenticated = false;
    for (int tid = 1; tid <= SMB_MAX_TREES; tid++)
        smb_tree_disconnect(request->session, (uint16_t) tid);
    smb_reply_words(request, 2);
    return smb_reply_length(request, 2, 0);
}

static int
smb_cmd_echo(smb_request_t *request)
{
    int      start  = smb_bytes_at(request);
    int      length = smb_bytes_end(request) - start;
    uint8_t *reply;

    if (length < 0)
        length = 0;
    if (length > request->session->max_read)
        length = request->session->max_read;
    reply = smb_reply_words(request, 1);
    smb_put16(reply, 1);
    memcpy(reply + 2 + 2, request->msg + start, (size_t) length);
    return smb_reply_length(request, 1, length);
}

static int
smb_cmd_process_exit(smb_request_t *request)
{
    uint16_t pid = smb_get16(request->msg + 26);

    for (int i = 0; i < SMB_MAX_FILES; i++) {
        if (request->session->files[i].pid == pid)
            memset(&request->session->files[i], 0, sizeof(smb_file_t));
    }
    smb_reply_words(request, 0);
    return smb_reply_length(request, 0, 0);
}

/* The folder is read-only, so anything that would change it stops here. */
static int
smb_cmd_read_only(smb_request_t *request)
{
    (void) request;
    return SMB_FAIL(SMB_ERRDOS, SMB_ERRnoaccess);
}

/* Nobody else can write, so a lock is always granted and needs no record. */
static int
smb_cmd_lock(smb_request_t *request)
{
    smb_reply_words(request, 0);
    return smb_reply_length(request, 0, 0);
}

static int
smb_cmd_open(smb_request_t *request)
{
    char                  path[SMB_PATH_MAX];
    int                   pos = smb_bytes_at(request);
    const smb_fs_entry_t *entry;
    uint8_t              *reply;
    int                   access = smb_word(request, 0) & SMB_ACCESS_MASK;
    int                   result;

    if (!smb_read_path(request, &pos, smb_bytes_end(request), true, path))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);
    result = smb_share_lookup(request, path, &entry);
    if (result < 0)
        return result;
    if (smb_fs_is_directory(entry) || (access != SMB_ACCESS_READ && access != SMB_ACCESS_EXECUTE))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRnoaccess);
    result = smb_file_open(request, entry);
    if (result < 0)
        return result;

    reply = smb_reply_words(request, 7);
    smb_put16(reply, (uint16_t) result);
    smb_put16(reply + 2, smb_fs_attributes(entry));
    smb_put32(reply + 4, SMB_FS_UNIX_TIME);
    smb_put32(reply + 8, smb_fs_size(entry));
    smb_put16(reply + 12, (uint16_t) access);
    return smb_reply_length(request, 7, 0);
}

static int
smb_cmd_open_andx(smb_request_t *request)
{
    char                  path[SMB_PATH_MAX];
    int                   pos = smb_bytes_at(request);
    const smb_fs_entry_t *entry;
    uint8_t              *reply;
    int                   access;
    int                   open_mode;
    int                   result;

    if (smb_words(request) < 15)
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport);
    access    = smb_word(request, 3) & SMB_ACCESS_MASK;
    open_mode = smb_word(request, 8);
    if (!smb_read_path(request, &pos, smb_bytes_end(request), false, path))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);

    result = smb_share_lookup(request, path, &entry);
    if (result == SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfile) && (open_mode & SMB_OPEN_CREATE))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRnoaccess);
    if (result < 0)
        return result;
    if ((open_mode & SMB_OPEN_EXISTS_MASK) == SMB_OPEN_FAIL_EXISTS)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRfilexists);
    if ((open_mode & SMB_OPEN_EXISTS_MASK) != SMB_OPEN_OPEN || smb_fs_is_directory(entry)
        || (access != SMB_ACCESS_READ && access != SMB_ACCESS_EXECUTE))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRnoaccess);

    result = smb_file_open(request, entry);
    if (result < 0)
        return result;

    reply = smb_reply_words(request, 15);
    smb_put16(reply + 4, (uint16_t) result);
    smb_put16(reply + 6, smb_fs_attributes(entry));
    smb_put32(reply + 8, SMB_FS_UNIX_TIME);
    smb_put32(reply + 12, smb_fs_size(entry));
    smb_put16(reply + 16, (uint16_t) access);
    smb_put16(reply + 22, SMB_OPEN_RESULT_OPENED);
    return smb_reply_length(request, 15, 0);
}

static int
smb_cmd_close(smb_request_t *request)
{
    smb_file_t *file = smb_file_get(request->session, smb_word(request, 0));

    if (!file)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    memset(file, 0, sizeof(smb_file_t));
    smb_reply_words(request, 0);
    return smb_reply_length(request, 0, 0);
}

static int
smb_cmd_flush(smb_request_t *request)
{
    if (smb_word(request, 0) != 0xFFFF && !smb_file_get(request->session, smb_word(request, 0)))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    smb_reply_words(request, 0);
    return smb_reply_length(request, 0, 0);
}

static int
smb_cmd_read(smb_request_t *request)
{
    smb_file_t *file = smb_file_get(request->session, smb_word(request, 0));
    int         count;
    uint8_t    *reply;

    if (!file)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    count = smb_word(request, 1);
    if (count > request->session->max_read)
        count = request->session->max_read;

    reply = smb_reply_words(request, 5);
    count = smb_fs_read(file->entry, smb_dword(request, 2), reply + 5 * 2 + 2 + 3, count);
    file->position = smb_dword(request, 2) + (uint32_t) count;
    smb_put16(reply, (uint16_t) count);
    reply[5 * 2 + 2] = 0x01; /* the data block's buffer format */
    smb_put16(reply + 5 * 2 + 2 + 1, (uint16_t) count);
    return smb_reply_length(request, 5, 3 + count);
}

static int
smb_cmd_read_andx(smb_request_t *request)
{
    smb_file_t *file;
    uint32_t    offset;
    int         count;
    int         data_at;
    uint8_t    *reply;

    if (smb_words(request) < 10)
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport);
    file = smb_file_get(request->session, smb_word(request, 2));
    if (!file)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    offset = smb_dword(request, 3);
    count  = smb_word(request, 5);
    if (count > request->session->max_read)
        count = request->session->max_read;

    reply   = smb_reply_words(request, 12);
    data_at = request->pos + 1 + 12 * 2 + 2 + 1; /* one byte of padding keeps the data even */
    /* The offset's high half only arrives from clients with larger files than these. */
    if (smb_words(request) >= 12 && smb_dword(request, 10) != 0)
        count = 0;
    else
        count = smb_fs_read(file->entry, offset, request->out + data_at, count);
    file->position = offset + (uint32_t) count;

    smb_put16(reply + 10, (uint16_t) count);
    smb_put16(reply + 12, (uint16_t) data_at);
    request->out[data_at - 1] = 0;
    return smb_reply_length(request, 12, 1 + count);
}

static int
smb_cmd_seek(smb_request_t *request)
{
    smb_file_t *file = smb_file_get(request->session, smb_word(request, 0));
    int64_t     position;
    uint8_t    *reply;

    if (!file)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    switch (smb_word(request, 1) & 3) {
        case 1:
            position = file->position;
            break;
        case 2:
            position = smb_fs_size(file->entry);
            break;
        default:
            position = 0;
            break;
    }
    position += (int32_t) smb_dword(request, 2);
    if (position < 0)
        position = 0;
    file->position = (uint32_t) position;

    reply = smb_reply_words(request, 2);
    smb_put32(reply, file->position);
    return smb_reply_length(request, 2, 0);
}

static int
smb_cmd_locking_andx(smb_request_t *request)
{
    if (smb_words(request) < 8)
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport);
    if (!smb_file_get(request->session, smb_word(request, 2)))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    smb_reply_words(request, 2);
    return smb_reply_length(request, 2, 0);
}

static int
smb_cmd_query_information(smb_request_t *request)
{
    char                  path[SMB_PATH_MAX];
    int                   pos = smb_bytes_at(request);
    const smb_fs_entry_t *entry;
    uint8_t              *reply;
    int                   result;

    if (!smb_read_path(request, &pos, smb_bytes_end(request), true, path))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);
    result = smb_share_lookup(request, path, &entry);
    if (result < 0)
        return result;

    reply = smb_reply_words(request, 10);
    smb_put16(reply, smb_fs_attributes(entry));
    smb_put32(reply + 2, SMB_FS_UNIX_TIME);
    smb_put32(reply + 6, smb_fs_size(entry));
    return smb_reply_length(request, 10, 0);
}

static uint32_t
smb_allocation(const smb_fs_entry_t *entry)
{
    return (smb_fs_size(entry) + FS_SECTOR_SIZE - 1) & ~(uint32_t) (FS_SECTOR_SIZE - 1);
}

static int
smb_cmd_query_information2(smb_request_t *request)
{
    smb_file_t *file = smb_file_get(request->session, smb_word(request, 0));
    uint8_t    *reply;

    if (!file)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    reply = smb_reply_words(request, 11);
    for (int i = 0; i < 3; i++) {
        smb_put16(reply + i * 4, SMB_FS_DOS_DATE);
        smb_put16(reply + i * 4 + 2, SMB_FS_DOS_TIME);
    }
    smb_put32(reply + 12, smb_fs_size(file->entry));
    smb_put32(reply + 16, smb_allocation(file->entry));
    smb_put16(reply + 20, smb_fs_attributes(file->entry));
    return smb_reply_length(request, 11, 0);
}

static int
smb_cmd_check_directory(smb_request_t *request)
{
    char                  path[SMB_PATH_MAX];
    int                   pos = smb_bytes_at(request);
    const smb_fs_entry_t *entry;
    int                   result;

    if (!smb_read_path(request, &pos, smb_bytes_end(request), true, path))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);
    result = smb_share_lookup(request, path, &entry);
    if (result == SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfile))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);
    if (result < 0)
        return result;
    if (!smb_fs_is_directory(entry))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);
    smb_reply_words(request, 0);
    return smb_reply_length(request, 0, 0);
}

static int
smb_cmd_query_disk(smb_request_t *request)
{
    uint8_t *reply;

    if (!smb_share_connected(request))
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvtid);
    reply = smb_reply_words(request, 5);
    smb_put16(reply, FS_TOTAL_UNITS);
    smb_put16(reply + 2, FS_SECTORS_PER_UNIT);
    smb_put16(reply + 4, FS_SECTOR_SIZE);
    return smb_reply_length(request, 5, 0);
}

/* Prepares a search for a pattern such as "DOCS\*.*". */
static int
smb_search_start(const smb_request_t *request, const char *path, uint16_t attributes, smb_search_t *search)
{
    char                  directory_path[SMB_PATH_MAX];
    const char           *pattern = strrchr(path, '\\');
    const smb_fs_entry_t *directory;

    if (!smb_share_connected(request))
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvtid);

    directory_path[0] = 0;
    if (pattern) {
        memcpy(directory_path, path, (size_t) (pattern - path));
        directory_path[pattern - path] = 0;
        pattern++;
    } else {
        pattern = path;
    }
    directory = smb_fs_lookup(smb_tree_root(request), directory_path);
    if (!directory || !smb_fs_is_directory(directory))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);

    search->root       = smb_tree_root(request);
    search->directory  = directory;
    search->attributes = attributes;
    search->tid        = smb_get16(request->out + 24);
    search->position   = directory == search->root ? SMB_DIR_POSITION_DOTS : 0;
    strcpy(search->pattern, *pattern ? pattern : "*");
    return 0;
}

/* Finds the next match, giving back its name and the entry to describe it with. */
static bool
smb_search_next(smb_search_t *search, const char **name, const smb_fs_entry_t **entry)
{
    bool directories = (search->attributes & SMB_FS_ATTR_DIRECTORY) != 0;

    for (;;) {
        const smb_fs_entry_t *found;

        if (search->position < SMB_DIR_POSITION_DOTS) {
            bool dots = directories && smb_fs_match(search->pattern, ".");

            *name  = search->position ? ".." : ".";
            *entry = search->directory;
            search->position++;
            if (dots)
                return true;
            continue;
        }

        found = smb_fs_child(search->directory, search->position - SMB_DIR_POSITION_DOTS);
        if (!found)
            return false;
        search->position++;
        if ((directories || !smb_fs_is_directory(found))
            && smb_fs_match(search->pattern, smb_fs_name(found))) {
            *name  = smb_fs_name(found);
            *entry = found;
            return true;
        }
    }
}

static void
smb_dir_entry_put(uint8_t *data, int slot, int position, const uint8_t *client_key, const char *name,
                  uint8_t attributes, uint32_t size)
{
    const char *dot = strchr(name, '.');
    size_t      stem = dot && dot != name ? (size_t) (dot - name) : strlen(name);

    memset(data, 0, SMB_DIR_ENTRY_LENGTH);
    /* The first half of the resume key is a space-padded 8.3 name. */
    memset(data + 1, ' ', 11);
    memcpy(data + 1, name, stem > 8 ? 8 : stem);
    if (dot && dot != name)
        memcpy(data + 9, dot + 1, strlen(dot + 1) > 3 ? 3 : strlen(dot + 1));
    data[12] = (uint8_t) (slot + 1);
    smb_put16(data + 13, (uint16_t) position);
    if (client_key)
        memcpy(data + 17, client_key + 17, 4);
    data[21] = attributes;
    smb_put16(data + 22, SMB_FS_DOS_TIME);
    smb_put16(data + 24, SMB_FS_DOS_DATE);
    smb_put32(data + 26, size);
    strncpy((char *) data + 30, name, SMB_FS_NAME_MAX - 1);
}

/* SEARCH and FIND, which the core and LAN Manager 1.x clients list directories with. They
   never say when they are finished, so slots are reused in turn. */
static int
smb_cmd_search(smb_request_t *request)
{
    smb_session_t  *session = request->session;
    const uint8_t  *msg     = request->msg;
    const uint8_t  *key     = NULL;
    uint8_t         command = msg[4];
    char            path[SMB_PATH_MAX];
    int             max     = smb_word(request, 0);
    uint16_t        attributes = smb_word(request, 1);
    int             pos     = smb_bytes_at(request);
    int             end     = smb_bytes_end(request);
    int             key_length;
    int             slot    = -1;
    int             count   = 0;
    uint8_t        *reply;
    uint8_t        *entries;

    if (!smb_share_connected(request))
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvtid);
    if (!smb_read_path(request, &pos, end, true, path))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);
    if (pos + 3 > end || msg[pos] != 0x05)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRnofiles);
    key_length = smb_get16(msg + pos + 1);
    pos += 3;

    if (key_length) {
        if (key_length < SMB_RESUME_KEY_LENGTH || pos + SMB_RESUME_KEY_LENGTH > end)
            return SMB_FAIL(SMB_ERRDOS, SMB_ERRnofiles);
        key  = msg + pos;
        slot = key[12] - 1;
        if (slot < 0 || slot >= SMB_MAX_SEARCHES || !session->searches[slot].directory)
            return SMB_FAIL(SMB_ERRDOS, SMB_ERRnofiles);
        session->searches[slot].position = smb_get16(key + 13);
    }

    if (command == SMB_COM_FIND_CLOSE) {
        if (slot >= 0)
            memset(&session->searches[slot], 0, sizeof(smb_search_t));
        reply = smb_reply_words(request, 1);
        reply[2 + 2] = 0x05;
        reply[2 + 2 + 1] = 0;
        reply[2 + 2 + 2] = 0;
        return smb_reply_length(request, 1, 3);
    }

    reply   = smb_reply_words(request, 1);
    entries = reply + 2 + 2 + 3;
    if (max > (request->session->max_buffer - 64) / SMB_DIR_ENTRY_LENGTH)
        max = (request->session->max_buffer - 64) / SMB_DIR_ENTRY_LENGTH;

    if (!key && (attributes & 0x1F) == SMB_FS_ATTR_VOLUME) {
        /* Asking for the volume label and nothing else. */
        if (max > 0) {
            smb_dir_entry_put(entries, 0, 0, NULL, VSERVER_SHARE_NAME, SMB_FS_ATTR_VOLUME, 0);
            count = 1;
        }
    } else {
        if (!key) {
            int result;

            slot = session->next_search;
            session->next_search = (slot + 1) % SMB_MAX_SEARCHES;
            result = smb_search_start(request, path, attributes, &session->searches[slot]);
            if (result < 0) {
                session->searches[slot].directory = NULL;
                return result;
            }
        }
        while (count < max) {
            const char           *name;
            const smb_fs_entry_t *entry;

            if (!smb_search_next(&session->searches[slot], &name, &entry))
                break;
            smb_dir_entry_put(entries + count * SMB_DIR_ENTRY_LENGTH, slot,
                              session->searches[slot].position, key, name,
                              smb_fs_attributes(entry), smb_fs_size(entry));
            count++;
        }
        if (command == SMB_COM_FIND_UNIQUE)
            memset(&session->searches[slot], 0, sizeof(smb_search_t));
    }

    if (!count)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRnofiles);

    smb_put16(reply, (uint16_t) count);
    reply[2 + 2] = 0x05;
    smb_put16(reply + 2 + 2 + 1, (uint16_t) (count * SMB_DIR_ENTRY_LENGTH));
    return smb_reply_length(request, 1, 3 + count * SMB_DIR_ENTRY_LENGTH);
}

/* Writes one directory entry for FIND_FIRST2 or FIND_NEXT2. Returns its length, or 0 when
   it doesn't fit in room. name_at receives where the name starts within it. */
static int
smb_find_entry(int level, bool resume_keys, uint32_t resume_key, const char *name,
               const smb_fs_entry_t *entry, uint8_t *data, int room, int *name_at)
{
    int      name_length = (int) strlen(name);
    uint32_t size        = smb_fs_size(entry);
    uint32_t allocation  = smb_allocation(entry);
    uint64_t time        = smb_filetime(SMB_FS_UNIX_TIME);
    int      fixed;
    int      total;

    switch (level) {
        case INFO_STANDARD:
        case INFO_QUERY_EA_SIZE:
            {
                int at = resume_keys ? 4 : 0;

                fixed = at + (level == INFO_STANDARD ? 23 : 27);
                total = fixed + name_length + 1;
                if (total > room)
                    return 0;
                memset(data, 0, (size_t) fixed);
                if (resume_keys)
                    smb_put32(data, resume_key);
                for (int i = 0; i < 3; i++) {
                    smb_put16(data + at + i * 4, SMB_FS_DOS_DATE);
                    smb_put16(data + at + i * 4 + 2, SMB_FS_DOS_TIME);
                }
                smb_put32(data + at + 12, size);
                smb_put32(data + at + 16, allocation);
                smb_put16(data + at + 20, smb_fs_attributes(entry));
                data[fixed - 1] = (uint8_t) name_length;
                memcpy(data + fixed, name, (size_t) name_length + 1);
                *name_at = fixed;
                return total;
            }

        case FIND_FILE_NAMES:
            fixed = 12;
            total = (fixed + name_length + 1 + 3) & ~3;
            if (total > room)
                return 0;
            memset(data, 0, (size_t) total);
            smb_put32(data, (uint32_t) total);
            smb_put32(data + 4, resume_key);
            smb_put32(data + 8, (uint32_t) name_length);
            memcpy(data + fixed, name, (size_t) name_length);
            *name_at = fixed;
            return total;

        case FIND_FILE_DIRECTORY:
        case FIND_FILE_FULL:
        case FIND_FILE_BOTH:
            fixed = level == FIND_FILE_DIRECTORY ? 64 : (level == FIND_FILE_FULL ? 68 : 94);
            total = (fixed + name_length + 1 + 3) & ~3;
            if (total > room)
                return 0;
            memset(data, 0, (size_t) total);
            smb_put32(data, (uint32_t) total);
            smb_put32(data + 4, resume_key);
            for (int i = 0; i < 4; i++)
                smb_put64(data + 8 + i * 8, time);
            smb_put64(data + 40, size);
            smb_put64(data + 48, allocation);
            smb_put32(data + 56, smb_fs_attributes(entry));
            smb_put32(data + 60, (uint32_t) name_length);
            memcpy(data + fixed, name, (size_t) name_length);
            *name_at = fixed;
            return total;

        default:
            return 0;
    }
}

static bool
smb_find_level_known(int level)
{
    return level == INFO_STANDARD || level == INFO_QUERY_EA_SIZE || level == FIND_FILE_DIRECTORY
        || level == FIND_FILE_FULL || level == FIND_FILE_NAMES || level == FIND_FILE_BOTH;
}

/* Fills data from a search and writes how it went to the last four parameters. */
static int
smb_find_entries(smb_search_t *search, int level, int max, int flags, uint8_t *params, uint8_t *data, int cap)
{
    bool resume_keys = (flags & FIND_RETURN_RESUME_KEYS) != 0;
    int  count       = 0;
    int  length      = 0;
    int  last_name   = 0;
    int  last_entry  = 0;
    bool finished    = false;

    while (count < max) {
        smb_search_t          before = *search;
        const char           *name;
        const smb_fs_entry_t *entry;
        int                   name_at;
        int                   used;

        if (!smb_search_next(search, &name, &entry)) {
            finished = true;
            break;
        }
        used = smb_find_entry(level, resume_keys, (uint32_t) search->position, name, entry,
                              data + length, cap - length, &name_at);
        if (!used) {
            *search = before; /* out of room: it goes in the next reply */
            break;
        }
        last_entry = length;
        last_name  = length + name_at;
        length += used;
        count++;
    }

    if (!finished) {
        smb_search_t peek = *search;
        const char           *name;
        const smb_fs_entry_t *entry;

        finished = !smb_search_next(&peek, &name, &entry);
    }
    if (count && level >= FIND_FILE_DIRECTORY)
        smb_put32(data + last_entry, 0); /* the last entry has nothing after it */

    smb_put16(params, (uint16_t) count);
    smb_put16(params + 2, finished ? 1 : 0);
    smb_put16(params + 4, 0);
    smb_put16(params + 6, (uint16_t) last_name);
    return (count || finished) ? length : SMB_FAIL(SMB_ERRDOS, SMB_ERRnofiles);
}

static int
smb_trans2_find_first(smb_request_t *request, const uint8_t *in, int in_len,
                      uint8_t *params, int *param_count, uint8_t *data, int cap)
{
    smb_session_t *session = request->session;
    smb_request_t  name_request = *request;
    char           path[SMB_PATH_MAX];
    int            pos;
    int            level;
    int            flags;
    int            slot;
    int            result;

    if (in_len < 12)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfunc);
    flags = smb_get16(in + 4);
    level = smb_get16(in + 6);
    if (!smb_find_level_known(level))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRunknownlevel);

    name_request.msg = in;
    pos = 12;
    if (!smb_read_path(&name_request, &pos, in_len, false, path))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);

    for (slot = 0; slot < SMB_MAX_FINDS; slot++) {
        if (!session->finds[slot].directory)
            break;
    }
    if (slot == SMB_MAX_FINDS)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRnofids);
    result = smb_search_start(request, path, smb_get16(in), &session->finds[slot]);
    if (result < 0) {
        session->finds[slot].directory = NULL;
        return result;
    }

    smb_put16(params, (uint16_t) (slot + 1));
    *param_count = 10;
    result = smb_find_entries(&session->finds[slot], level, smb_get16(in + 2), flags,
                              params + 2, data, cap);
    if (result <= 0 || (flags & FIND_CLOSE_AFTER_REQUEST)
        || ((flags & FIND_CLOSE_AT_EOS) && smb_get16(params + 4))) {
        memset(&session->finds[slot], 0, sizeof(smb_search_t));
        if (result == 0)
            return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfile);
    }
    return result;
}

static int
smb_trans2_find_next(smb_request_t *request, const uint8_t *in, int in_len,
                     uint8_t *params, int *param_count, uint8_t *data, int cap)
{
    smb_session_t *session = request->session;
    smb_search_t  *search;
    uint32_t       key;
    int            level;
    int            flags;
    int            slot;
    int            result;

    if (in_len < 12)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfunc);
    slot = smb_get16(in) - 1;
    if (slot < 0 || slot >= SMB_MAX_FINDS || !session->finds[slot].directory)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    search = &session->finds[slot];
    level  = smb_get16(in + 4);
    key    = smb_get32(in + 6);
    flags  = smb_get16(in + 10);
    if (!smb_find_level_known(level))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRunknownlevel);

    if (!(flags & FIND_CONTINUE)) {
        if (key >= SMB_DIR_POSITION_DOTS && key < 0x10000u) {
            search->position = (int) key;
        } else {
            /* Without a usable key the client names the last entry it received. */
            const uint8_t        *name  = in + 12;
            int                   left  = in_len - 12;
            smb_search_t          scan  = *search;
            const char           *found;
            const smb_fs_entry_t *entry;

            if (left > 0 && strnlen((const char *) name, (size_t) left) < (size_t) left && name[0]) {
                scan.position = search->directory == search->root ? SMB_DIR_POSITION_DOTS : 0;
                while (smb_search_next(&scan, &found, &entry)) {
                    if (smb_fs_match((const char *) name, found)) {
                        search->position = scan.position;
                        break;
                    }
                }
            }
        }
    }

    *param_count = 8;
    result = smb_find_entries(search, level, smb_get16(in + 2), flags, params, data, cap);
    if (result < 0 || (flags & FIND_CLOSE_AFTER_REQUEST)
        || ((flags & FIND_CLOSE_AT_EOS) && smb_get16(params + 2)))
        memset(search, 0, sizeof(smb_search_t));
    return result;
}

static int
smb_trans2_query_fs(smb_request_t *request, const uint8_t *in, int in_len, uint8_t *data, int cap)
{
    int length;

    if (in_len < 2)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfunc);
    if (!smb_share_connected(request))
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvtid);
    if (cap < 128)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfunc);

    length = (int) strlen(VSERVER_SHARE_NAME);
    switch (smb_get16(in)) {
        case FS_INFO_ALLOCATION:
            memset(data, 0, 18);
            smb_put32(data + 4, FS_SECTORS_PER_UNIT);
            smb_put32(data + 8, FS_TOTAL_UNITS);
            smb_put16(data + 16, FS_SECTOR_SIZE);
            return 18;

        case FS_INFO_VOLUME:
            smb_put32(data, FS_SERIAL_NUMBER);
            data[4] = (uint8_t) length;
            memcpy(data + 5, VSERVER_SHARE_NAME, (size_t) length + 1);
            return 5 + length + 1;

        case FS_QUERY_VOLUME:
            memset(data, 0, 18);
            smb_put64(data, smb_filetime(SMB_FS_UNIX_TIME));
            smb_put32(data + 8, FS_SERIAL_NUMBER);
            smb_put32(data + 12, (uint32_t) length);
            memcpy(data + 18, VSERVER_SHARE_NAME, (size_t) length);
            return 18 + length;

        case FS_QUERY_SIZE:
            memset(data, 0, 24);
            smb_put64(data, FS_TOTAL_UNITS);
            smb_put32(data + 16, FS_SECTORS_PER_UNIT);
            smb_put32(data + 20, FS_SECTOR_SIZE);
            return 24;

        case FS_QUERY_DEVICE:
            smb_put32(data, FS_DEVICE_DISK);
            smb_put32(data + 4, FS_DEVICE_READ_ONLY);
            return 8;

        case FS_QUERY_ATTRIBUTE:
            smb_put32(data, FS_ATTRIBUTE_CASE | FS_ATTRIBUTE_READ_ONLY);
            smb_put32(data + 4, 12);
            smb_put32(data + 8, (uint32_t) strlen(FS_NAME));
            memcpy(data + 12, FS_NAME, strlen(FS_NAME));
            return 12 + (int) strlen(FS_NAME);

        default:
            return SMB_FAIL(SMB_ERRDOS, SMB_ERRunknownlevel);
    }
}

/* The answers shared by QUERY_PATH_INFORMATION and QUERY_FILE_INFORMATION. */
static int
smb_info_put(const smb_fs_entry_t *entry, int level, uint8_t *data, int cap)
{
    const char *name        = smb_fs_name(entry);
    int         name_length = (int) strlen(name);
    bool        directory   = smb_fs_is_directory(entry);
    uint64_t    time        = smb_filetime(SMB_FS_UNIX_TIME);

    if (cap < 128)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfunc);

    switch (level) {
        case INFO_STANDARD:
        case INFO_QUERY_EA_SIZE:
            memset(data, 0, 26);
            for (int i = 0; i < 3; i++) {
                smb_put16(data + i * 4, SMB_FS_DOS_DATE);
                smb_put16(data + i * 4 + 2, SMB_FS_DOS_TIME);
            }
            smb_put32(data + 12, smb_fs_size(entry));
            smb_put32(data + 16, smb_allocation(entry));
            smb_put16(data + 20, smb_fs_attributes(entry));
            return level == INFO_STANDARD ? 22 : 26;

        case INFO_QUERY_EAS:
        case INFO_QUERY_ALL_EAS:
            smb_put32(data, 4); /* an empty extended attribute list holds only its own size */
            return 4;

        case INFO_IS_NAME_VALID:
            return 0;

        case QUERY_FILE_EA:
            smb_put32(data, 0);
            return 4;

        case QUERY_FILE_NAME:
        case QUERY_FILE_ALT_NAME:
            smb_put32(data, (uint32_t) name_length);
            memcpy(data + 4, name, (size_t) name_length);
            return 4 + name_length;

        case QUERY_FILE_BASIC:
        case QUERY_FILE_STANDARD:
        case QUERY_FILE_ALL:
            memset(data, 0, 72);
            if (level != QUERY_FILE_STANDARD) {
                for (int i = 0; i < 4; i++)
                    smb_put64(data + i * 8, time);
                smb_put32(data + 32, smb_fs_attributes(entry));
                if (level == QUERY_FILE_BASIC)
                    return 40;
                data += 40;
            }
            smb_put64(data, smb_allocation(entry));
            smb_put64(data + 8, smb_fs_size(entry));
            smb_put32(data + 16, 1);
            data[21] = directory ? 1 : 0;
            if (level == QUERY_FILE_STANDARD)
                return 24;
            smb_put32(data + 28, (uint32_t) name_length);
            memcpy(data + 32, name, (size_t) name_length);
            return 40 + 32 + name_length;

        default:
            return SMB_FAIL(SMB_ERRDOS, SMB_ERRunknownlevel);
    }
}

static int
smb_trans2_query_path(smb_request_t *request, const uint8_t *in, int in_len,
                      uint8_t *params, int *param_count, uint8_t *data, int cap)
{
    smb_request_t         name_request = *request;
    char                  path[SMB_PATH_MAX];
    int                   pos = 6;
    const smb_fs_entry_t *entry;
    int                   result;

    if (in_len < 6)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfunc);
    name_request.msg = in;
    if (!smb_read_path(&name_request, &pos, in_len, false, path))
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpath);
    result = smb_share_lookup(request, path, &entry);
    if (result < 0)
        return result;
    smb_put16(params, 0);
    *param_count = 2;
    return smb_info_put(entry, smb_get16(in), data, cap);
}

static int
smb_trans2_query_file(smb_request_t *request, const uint8_t *in, int in_len,
                      uint8_t *params, int *param_count, uint8_t *data, int cap)
{
    smb_file_t *file;

    if (in_len < 4)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfunc);
    file = smb_file_get(request->session, smb_get16(in));
    if (!file)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    smb_put16(params, 0);
    *param_count = 2;
    return smb_info_put(file->entry, smb_get16(in + 2), data, cap);
}

static int
smb_cmd_transaction2(smb_request_t *request)
{
    uint8_t        params[TRANS2_MAX_PARAMS];
    uint8_t        data[TRANS2_MAX_DATA];
    const uint8_t *in;
    int            in_len;
    int            in_at;
    int            cap;
    int            param_count = 0;
    int            result;
    int            params_at;
    int            data_at;
    uint8_t       *reply;

    if (smb_words(request) < TRANS2_REQUEST_WORDS)
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport);

    in_len = smb_word(request, 9);
    in_at  = smb_word(request, 10);
    if (in_at < SMB_HEADER_LENGTH || in_at + in_len > request->len)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfunc);
    in  = request->msg + in_at;
    cap = smb_word(request, 3);
    if (cap > TRANS2_MAX_DATA)
        cap = TRANS2_MAX_DATA;

    smb_log(request->session->log, "SMB: TRANS2 subcommand 0x%02X\n", smb_word(request, 14));

    switch (smb_word(request, 14)) {
        case TRANS2_FIND_FIRST2:
            result = smb_trans2_find_first(request, in, in_len, params, &param_count, data, cap);
            break;
        case TRANS2_FIND_NEXT2:
            result = smb_trans2_find_next(request, in, in_len, params, &param_count, data, cap);
            break;
        case TRANS2_QUERY_FS:
            result = smb_trans2_query_fs(request, in, in_len, data, cap);
            break;
        case TRANS2_QUERY_PATH:
            result = smb_trans2_query_path(request, in, in_len, params, &param_count, data, cap);
            break;
        case TRANS2_QUERY_FILE:
            result = smb_trans2_query_file(request, in, in_len, params, &param_count, data, cap);
            break;
        case TRANS2_OPEN2:
        case TRANS2_SET_FS:
        case TRANS2_SET_PATH:
        case TRANS2_SET_FILE:
        case TRANS2_CREATE_DIRECTORY:
            result = SMB_FAIL(SMB_ERRDOS, SMB_ERRnoaccess);
            break;
        default:
            result = SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport);
            break;
    }
    if (result < 0)
        return result;

    reply     = smb_reply_words(request, TRANS2_REPLY_WORDS);
    params_at = request->pos + 1 + TRANS2_REPLY_WORDS * 2 + 2 + 1;
    data_at   = (params_at + param_count + 3) & ~3;
    smb_put16(reply, (uint16_t) param_count);
    smb_put16(reply + 2, (uint16_t) result);
    smb_put16(reply + 6, (uint16_t) param_count);
    smb_put16(reply + 8, (uint16_t) params_at);
    smb_put16(reply + 12, (uint16_t) result);
    smb_put16(reply + 14, (uint16_t) data_at);
    memset(request->out + params_at - 1, 0, (size_t) (data_at - params_at + 1));
    memcpy(request->out + params_at, params, (size_t) param_count);
    memcpy(request->out + data_at, data, (size_t) result);
    return smb_reply_length(request, TRANS2_REPLY_WORDS, data_at + result - (params_at - 1));
}

static int
smb_cmd_find_close2(smb_request_t *request)
{
    int slot = smb_word(request, 0) - 1;

    if (slot < 0 || slot >= SMB_MAX_FINDS || !request->session->finds[slot].directory)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadfid);
    memset(&request->session->finds[slot], 0, sizeof(smb_search_t));
    smb_reply_words(request, 0);
    return smb_reply_length(request, 0, 0);
}

static int
smb_cmd_transaction(smb_request_t *request)
{
    enum { REQUEST_WORDS = 14, REPLY_WORDS = 10 };
    const uint8_t *msg = request->msg;
    int            len = request->len;
    int            tid;
    int            param_count;
    int            param_offset;
    int            name_pos;
    const char    *name;
    uint8_t        params[SMB_RAP_PARAMS];
    uint8_t        data[SMB_RAP_MAX_DATA];
    int            param_length;
    int            data_length;
    int            params_at;
    int            data_at;
    uint8_t       *reply;

    tid = smb_get16(request->out + 24);
    if (tid < 1 || tid > SMB_MAX_TREES || request->session->trees[tid - 1] == SMB_TREE_NONE)
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRinvtid);
    if (smb_words(request) < REQUEST_WORDS)
        return SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport);

    param_count  = smb_word(request, 9);
    param_offset = smb_word(request, 10);
    name_pos     = smb_bytes_at(request);
    if (name_pos >= len || strnlen((const char *) msg + name_pos, (size_t) (len - name_pos))
                               == (size_t) (len - name_pos)
        || param_offset + param_count > len)
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpipe);

    name = (const char *) msg + name_pos;
    if (strcmp(name, "\\PIPE\\LANMAN") != 0) {
        smb_log(request->session->log, "SMB: Transaction on unknown name %s\n", name);
        return SMB_FAIL(SMB_ERRDOS, SMB_ERRbadpipe);
    }

    data_length = smb_rap_dispatch(request->session->log, request->session->authenticated, msg + param_offset,
                                   param_count, params, &param_length, data);

    reply     = smb_reply_words(request, REPLY_WORDS);
    params_at = request->pos + 1 + REPLY_WORDS * 2 + 2 + 1;
    data_at   = params_at + param_length;
    smb_put16(reply, (uint16_t) param_length);
    smb_put16(reply + 2, (uint16_t) data_length);
    smb_put16(reply + 6, (uint16_t) param_length);
    smb_put16(reply + 8, (uint16_t) params_at);
    smb_put16(reply + 12, (uint16_t) data_length);
    smb_put16(reply + 14, (uint16_t) data_at);
    request->out[params_at - 1] = 0;
    memcpy(request->out + params_at, params, (size_t) param_length);
    memcpy(request->out + data_at, data, (size_t) data_length);
    return smb_reply_length(request, REPLY_WORDS, 1 + param_length + data_length);
}

typedef struct smb_command_t {
    uint8_t       command;
    bool          andx;
    smb_handler_t handler;
} smb_command_t;

static const smb_command_t smb_commands[] = {
    { SMB_COM_CREATE_DIRECTORY,       false, smb_cmd_read_only         },
    { SMB_COM_DELETE_DIRECTORY,       false, smb_cmd_read_only         },
    { SMB_COM_OPEN,                   false, smb_cmd_open              },
    { SMB_COM_CREATE,                 false, smb_cmd_read_only         },
    { SMB_COM_CLOSE,                  false, smb_cmd_close             },
    { SMB_COM_FLUSH,                  false, smb_cmd_flush             },
    { SMB_COM_DELETE,                 false, smb_cmd_read_only         },
    { SMB_COM_RENAME,                 false, smb_cmd_read_only         },
    { SMB_COM_QUERY_INFORMATION,      false, smb_cmd_query_information },
    { SMB_COM_SET_INFORMATION,        false, smb_cmd_read_only         },
    { SMB_COM_READ,                   false, smb_cmd_read              },
    { SMB_COM_WRITE,                  false, smb_cmd_read_only         },
    { SMB_COM_LOCK,                   false, smb_cmd_lock   },
    { SMB_COM_UNLOCK,                 false, smb_cmd_lock   },
    { SMB_COM_CREATE_TEMPORARY,       false, smb_cmd_read_only         },
    { SMB_COM_CREATE_NEW,             false, smb_cmd_read_only         },
    { SMB_COM_CHECK_DIRECTORY,        false, smb_cmd_check_directory   },
    { SMB_COM_PROCESS_EXIT,           false, smb_cmd_process_exit      },
    { SMB_COM_SEEK,                   false, smb_cmd_seek              },
    { SMB_COM_SET_INFORMATION2,       false, smb_cmd_read_only         },
    { SMB_COM_QUERY_INFORMATION2,     false, smb_cmd_query_information2 },
    { SMB_COM_LOCKING_ANDX,           true,  smb_cmd_locking_andx      },
    { SMB_COM_TRANSACTION,            false, smb_cmd_transaction       },
    { SMB_COM_ECHO,                   false, smb_cmd_echo              },
    { SMB_COM_OPEN_ANDX,              true,  smb_cmd_open_andx         },
    { SMB_COM_READ_ANDX,              true,  smb_cmd_read_andx         },
    { SMB_COM_WRITE_ANDX,             false, smb_cmd_read_only         },
    { SMB_COM_TRANSACTION2,           false, smb_cmd_transaction2      },
    { SMB_COM_FIND_CLOSE2,            false, smb_cmd_find_close2       },
    { SMB_COM_TREE_CONNECT,           false, smb_cmd_tree_connect      },
    { SMB_COM_TREE_DISCONNECT,        false, smb_cmd_tree_disconnect   },
    { SMB_COM_NEGOTIATE,              false, smb_cmd_negotiate         },
    { SMB_COM_SESSION_SETUP,          true,  smb_cmd_session_setup     },
    { SMB_COM_LOGOFF,                 true,  smb_cmd_logoff            },
    { SMB_COM_TREE_CONNECT_ANDX,      true,  smb_cmd_tree_connect_andx },
    { SMB_COM_QUERY_INFORMATION_DISK, false, smb_cmd_query_disk        },
    { SMB_COM_SEARCH,                 false, smb_cmd_search            },
    { SMB_COM_FIND,                   false, smb_cmd_search            },
    { SMB_COM_FIND_UNIQUE,            false, smb_cmd_search            },
    { SMB_COM_FIND_CLOSE,             false, smb_cmd_search            }
};

static const smb_command_t *
smb_command_find(uint8_t command)
{
    for (size_t i = 0; i < sizeof(smb_commands) / sizeof(smb_commands[0]); i++) {
        if (smb_commands[i].command == command)
            return &smb_commands[i];
    }
    return NULL;
}

/* Runs a message's first command, then whatever an AndX chain has after it. A failure
   anywhere in a chain replaces the whole reply with the error. */
static int
smb_run_commands(smb_session_t *session, const uint8_t *msg, int len, uint8_t *out)
{
    smb_request_t request = { session, msg, len, SMB_HEADER_LENGTH, out, SMB_HEADER_LENGTH };
    uint8_t       command = msg[4];

    for (int links = 0; links < 6; links++) {
        const smb_command_t *entry = smb_command_find(command);
        int                  used;
        int                  next_at;
        uint8_t              next;

        if (!entry)
            return smb_error(out, SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport));
        if (request.in + 1 + msg[request.in] * 2 + 2 > len || (entry->andx && msg[request.in] < 2))
            return smb_error(out, SMB_FAIL(SMB_ERRSRV, SMB_ERRnosupport));

        used = entry->handler(&request);
        if (used < 0)
            return smb_error(out, used);
        if (!entry->andx)
            return request.pos + used;

        next    = msg[request.in + 1];
        next_at = smb_get16(msg + request.in + 3);
        if (next != SMB_ANDX_NONE && (next_at <= request.in || next_at >= len || !smb_command_find(next)))
            next = SMB_ANDX_NONE;

        out[request.pos + 1] = next;
        out[request.pos + 2] = 0;
        smb_put16(out + request.pos + 3, next == SMB_ANDX_NONE ? 0 : (uint16_t) (request.pos + used));
        request.pos += used;
        if (next == SMB_ANDX_NONE || request.pos > SMB_MAX_CHAIN)
            break;
        command    = next;
        request.in = next_at;
    }
    return request.pos;
}

/* Handles one SMB message and returns the reply's length, or 0 for none. */
static int
smb_process_message(smb_session_t *session, const uint8_t *msg, int len, uint8_t *out, int cap)
{
    int words;

    if (len < SMB_HEADER_LENGTH + 3 || memcmp(msg, "\xFFSMB", 4) != 0 || cap < SMB_MAX_REPLY)
        return 0;
    words = msg[SMB_HEADER_LENGTH];
    if (SMB_HEADER_LENGTH + 1 + words * 2 + 2 > len)
        return 0;

    memcpy(out, msg, SMB_HEADER_LENGTH);
    memset(out + 5, 0, 4);
    out[9] = (uint8_t) (out[9] | SMB_FLAGS_REPLY);
    /* Whatever a client asked for, the reply is in OEM text with DOS errors. */
    smb_put16(out + 10, (uint16_t) (smb_get16(msg + 10) & SMB_FLAGS2_LONG_NAMES));
    smb_log(session->log, "SMB: Command 0x%02X\n", msg[4]);

    return smb_run_commands(session, msg, len, out);
}

static bool
smb_called_name_ok(const uint8_t *name, int len)
{
    char decoded[17];

    if (len < NBT_NAME_LENGTH || name[0] != 32)
        return false;
    for (int i = 0; i < 16; i++)
        decoded[i] = (char) (((name[1 + i * 2] - 'A') << 4) | ((name[2 + i * 2] - 'A') & 0x0F));
    for (int i = 14; i >= 0 && decoded[i] == ' '; i--)
        decoded[i] = 0;
    decoded[15] = 0;
    return !strcmp(decoded, VSERVER_SERVER_NAME) || !strcmp(decoded, VSERVER_DOMAIN_NAME)
        || !strcmp(decoded, "*SMBSERVER");
}

int
smb_session_input(smb_session_t *session, const uint8_t *data, int len, uint8_t *out, int cap)
{
    int written = 0;

    if (!session || !data || len < 0)
        return -1;

    while (len > 0) {
        int copy = (int) sizeof(session->input) - session->input_len;

        if (copy > len)
            copy = len;
        memcpy(session->input + session->input_len, data, (size_t) copy);
        session->input_len += copy;
        data += copy;
        len -= copy;

        while (session->input_len >= NBT_HEADER_LENGTH) {
            const uint8_t *packet = session->input;
            int            size   = ((packet[1] & 1) << 16) | (packet[2] << 8) | packet[3];
            int            total  = NBT_HEADER_LENGTH + size;

            if (size > SMB_MAX_INPUT)
                return -1;
            if (session->input_len < total)
                break;
            if (cap - written < SMB_MAX_REPLY + NBT_HEADER_LENGTH)
                return -1;

            if (packet[0] == NBT_SESSION_REQUEST) {
                bool ok = smb_called_name_ok(packet + NBT_HEADER_LENGTH, size);

                memset(out + written, 0, NBT_HEADER_LENGTH);
                if (ok) {
                    out[written] = NBT_POSITIVE;
                    written += NBT_HEADER_LENGTH;
                    session->established = true;
                } else {
                    out[written]     = NBT_NEGATIVE;
                    out[written + 3] = 1;
                    out[written + 4] = NBT_NAME_NOT_PRESENT;
                    written += NBT_HEADER_LENGTH + 1;
                }
                smb_log(session->log, "SMB: NetBIOS session request %s\n", ok ? "accepted" : "refused");
            } else if (packet[0] == NBT_KEEPALIVE) {
                /* no reply */
            } else if (packet[0] == NBT_MESSAGE && session->established) {
                int reply = smb_process_message(session, packet + NBT_HEADER_LENGTH, size,
                                                out + written + NBT_HEADER_LENGTH,
                                                cap - written - NBT_HEADER_LENGTH);
                if (reply > 0) {
                    out[written]     = NBT_MESSAGE;
                    out[written + 1] = (uint8_t) (reply >> 16);
                    out[written + 2] = (uint8_t) (reply >> 8);
                    out[written + 3] = (uint8_t) reply;
                    written += NBT_HEADER_LENGTH + reply;
                }
            } else {
                return -1;
            }

            session->input_len -= total;
            memmove(session->input, session->input + total, (size_t) session->input_len);
        }
    }
    return written;
}

smb_session_t *
smb_session_create(void *log)
{
    smb_session_t *session = (smb_session_t *) calloc(1, sizeof(smb_session_t));

    if (session) {
        session->log        = log;
        session->max_buffer = SMB_MAX_BUFFER;
        session->max_read   = SMB_MAX_READ;
    }
    return session;
}

void
smb_session_attach(smb_session_t *session, int max_message)
{
    if (!session)
        return;
    session->established = true;
    if (max_message > SMB_MAX_READ_OVERHEAD && max_message < session->max_buffer) {
        session->max_buffer = max_message;
        session->max_read   = max_message - SMB_MAX_READ_OVERHEAD;
    }
}

void
smb_session_close(smb_session_t *session)
{
    free(session);
}
