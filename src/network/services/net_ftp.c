/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs.
 *
 *          This file is part of the 86Box distribution.
 *
 *          A read-only FTP service for the virtual network server.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/network/net_vserver_config.h>
#include "net_ftp.h"

#define FTP_LINE_LENGTH 512

typedef enum ftp_user_t {
    FTP_USER_NONE = 0,
    FTP_USER_CONFIGURED,
    FTP_USER_ANONYMOUS
} ftp_user_t;

typedef enum ftp_transfer_t {
    FTP_TRANSFER_NONE = 0,
    FTP_TRANSFER_FILE,
    FTP_TRANSFER_LIST,
    FTP_TRANSFER_NAMES
} ftp_transfer_t;

struct vserver_ftp_session_t {
    char           line[FTP_LINE_LENGTH];
    int            line_length;
    bool           discard_line;
    ftp_user_t     pending_user;
    bool           authenticated;
    bool           anonymous;
    bool           passive;
    bool           active;
    uint32_t       active_ip;
    uint16_t       active_port;
    ftp_transfer_t transfer;
    bool           transfer_started;
    char           listing[256];
};

static bool
ftp_append(uint8_t *output, int capacity, int *used, const char *text, ...)
{
    va_list ap;
    int     length;

    if (*used >= capacity)
        return false;
    va_start(ap, text);
    length = vsnprintf((char *) output + *used, (size_t) (capacity - *used), text, ap);
    va_end(ap);
    if (length < 0 || length >= capacity - *used)
        return false;
    *used += length;
    return true;
}

static char
ftp_upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char) (c - 'a' + 'A') : c;
}

static bool
ftp_equal(const char *left, const char *right)
{
    while (*left && *right && ftp_upper(*left) == ftp_upper(*right)) {
        left++;
        right++;
    }
    return *left == *right;
}

static bool
ftp_file_name(const char *name)
{
    if (*name == '/')
        name++;
    return ftp_equal(name, VSERVER_FTP_TEST_FILE_NAME);
}

static bool
ftp_parse_decimal(const char **cursor, unsigned maximum, unsigned *value)
{
    unsigned result = 0;

    if (**cursor < '0' || **cursor > '9')
        return false;
    do {
        unsigned digit = (unsigned) (**cursor - '0');

        if (result > (maximum - digit) / 10)
            return false;
        result = result * 10 + digit;
        (*cursor)++;
    } while (**cursor >= '0' && **cursor <= '9');
    *value = result;
    return true;
}

static bool
ftp_parse_ipv4(const char *text, uint32_t *address)
{
    unsigned    octets[4];
    const char *cursor = text;

    for (int i = 0; i < 4; i++) {
        if (!ftp_parse_decimal(&cursor, 255, &octets[i]))
            return false;
        if (i < 3) {
            if (*cursor++ != '.')
                return false;
        } else if (*cursor) {
            return false;
        }
    }
    *address = ((uint32_t) octets[0] << 24) | ((uint32_t) octets[1] << 16)
             | ((uint32_t) octets[2] << 8) | octets[3];
    return true;
}

static bool
ftp_parse_port(const char *text, uint32_t *address, uint16_t *port)
{
    unsigned    values[6];
    const char *cursor = text;

    for (int i = 0; i < 6; i++) {
        if (!ftp_parse_decimal(&cursor, 255, &values[i]))
            return false;
        if (i < 5) {
            if (*cursor++ != ',')
                return false;
        } else if (*cursor) {
            return false;
        }
    }
    *address = ((uint32_t) values[0] << 24) | ((uint32_t) values[1] << 16)
             | ((uint32_t) values[2] << 8) | values[3];
    *port = (uint16_t) ((values[4] << 8) | values[5]);
    return true;
}

static int
ftp_parse_eprt(const char *text, uint32_t *address, uint16_t *port)
{
    char        delimiter = *text++;
    char        address_text[16];
    const char *end;
    const char *cursor;
    unsigned    parsed_port;
    size_t      address_length;

    if (delimiter <= ' ' || delimiter >= 127)
        return 0;
    end = strchr(text, delimiter);
    if (!end || end - text != 1 || text[0] != '1')
        return -1;
    text = end + 1;
    end = strchr(text, delimiter);
    if (!end)
        return 0;
    address_length = (size_t) (end - text);
    if (!address_length || address_length >= sizeof(address_text))
        return 0;
    memcpy(address_text, text, address_length);
    address_text[address_length] = '\0';
    if (!ftp_parse_ipv4(address_text, address))
        return 0;

    text = end + 1;
    end = strchr(text, delimiter);
    if (!end || end[1] || end == text)
        return 0;
    cursor = text;
    if (!ftp_parse_decimal(&cursor, UINT16_MAX, &parsed_port) || cursor != end)
        return 0;
    *port = (uint16_t) parsed_port;
    return 1;
}

static int
ftp_command(vserver_ftp_session_t *session, uint32_t server_ip, uint32_t client_ip, uint16_t passive_port, char *line,
            uint8_t *output, int capacity, int *used, bool *close_after_reply)
{
    char    *argument = line;
    char    *space;
    char     command[8];
    size_t   command_length;
    uint32_t ip = server_ip;

    space = strchr(line, ' ');
    if (space) {
        *space++ = '\0';
        while (*space == ' ')
            space++;
        argument = space;
    } else {
        argument = line + strlen(line);
    }

    command_length = strlen(line);
    if (!command_length || command_length >= sizeof(command))
        return ftp_append(output, capacity, used, "500 Invalid command.\r\n");
    for (size_t i = 0; i <= command_length; i++)
        command[i] = ftp_upper(line[i]);

    if (!strcmp(command, "USER")) {
        session->authenticated = false;
        session->anonymous = false;
        session->passive = false;
        session->active = false;
        session->transfer = FTP_TRANSFER_NONE;
        session->transfer_started = false;
        if (ftp_equal(argument, "anonymous") || ftp_equal(argument, "ftp")) {
            session->pending_user = FTP_USER_ANONYMOUS;
            return ftp_append(output, capacity, used, "331 Anonymous access accepted; send any password.\r\n");
        }
        if (ftp_equal(argument, VSERVER_LOGON_USERNAME)) {
            session->pending_user = FTP_USER_CONFIGURED;
            return ftp_append(output, capacity, used, "331 Password required.\r\n");
        }
        session->pending_user = FTP_USER_NONE;
        session->authenticated = false;
        return ftp_append(output, capacity, used, "530 Login incorrect.\r\n");
    }
    if (!strcmp(command, "PASS")) {
        if (session->pending_user == FTP_USER_ANONYMOUS
            || (session->pending_user == FTP_USER_CONFIGURED && !strcmp(argument, VSERVER_LOGON_PASSWORD))) {
            session->authenticated = true;
            session->anonymous = session->pending_user == FTP_USER_ANONYMOUS;
            return ftp_append(output, capacity, used, session->anonymous
                                                           ? "230 Anonymous login accepted; access is read-only.\r\n"
                                                           : "230 Login successful; access is read-only.\r\n");
        }
        session->authenticated = false;
        session->anonymous = false;
        return ftp_append(output, capacity, used, "530 Login incorrect.\r\n");
    }
    if (!strcmp(command, "QUIT")) {
        *close_after_reply = true;
        return ftp_append(output, capacity, used, "221 Goodbye.\r\n");
    }
    if (!strcmp(command, "NOOP"))
        return ftp_append(output, capacity, used, "200 OK.\r\n");
    if (!strcmp(command, "SYST"))
        return ftp_append(output, capacity, used, "215 UNIX Type: L8\r\n");
    if (!strcmp(command, "FEAT"))
        return ftp_append(output, capacity, used, "211-Features:\r\n PORT\r\n EPRT\r\n PASV\r\n EPSV\r\n SIZE\r\n211 End\r\n");
    if (!strcmp(command, "PWD"))
        return session->authenticated
                   ? ftp_append(output, capacity, used, "257 \"/\" is the current directory.\r\n")
                   : ftp_append(output, capacity, used, "530 Please login first.\r\n");
    if (!strcmp(command, "CWD"))
        return session->authenticated && (!*argument || !strcmp(argument, "/") || !strcmp(argument, "."))
                   ? ftp_append(output, capacity, used, "250 Directory changed to /.\r\n")
                   : ftp_append(output, capacity, used, session->authenticated ? "550 Directory not found.\r\n"
                                                                                 : "530 Please login first.\r\n");
    if (!strcmp(command, "TYPE"))
        return session->authenticated && (ftp_equal(argument, "I") || ftp_equal(argument, "A"))
                   ? ftp_append(output, capacity, used, "200 Type set.\r\n")
                   : ftp_append(output, capacity, used, session->authenticated ? "504 Type not supported.\r\n"
                                                                                 : "530 Please login first.\r\n");
    if (!strcmp(command, "PASV")) {
        if (!session->authenticated)
            return ftp_append(output, capacity, used, "530 Please login first.\r\n");
        if (!passive_port)
            return ftp_append(output, capacity, used, "425 No passive ports are available.\r\n");
        session->passive = true;
        session->active = false;
        return ftp_append(output, capacity, used, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n",
                          (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
                          passive_port >> 8, passive_port & 0xFF);
    }
    if (!strcmp(command, "EPSV")) {
        if (!session->authenticated)
            return ftp_append(output, capacity, used, "530 Please login first.\r\n");
        if (!passive_port)
            return ftp_append(output, capacity, used, "425 No passive ports are available.\r\n");
        session->passive = true;
        session->active = false;
        return ftp_append(output, capacity, used, "229 Entering Extended Passive Mode (|||%u|).\r\n", passive_port);
    }
    if (!strcmp(command, "PORT") || !strcmp(command, "EPRT")) {
        uint32_t target_ip;
        uint16_t target_port;
        int      parsed = 1;

        if (!session->authenticated)
            return ftp_append(output, capacity, used, "530 Please login first.\r\n");
        if (!strcmp(command, "PORT")) {
            if (!ftp_parse_port(argument, &target_ip, &target_port))
                parsed = 0;
        } else {
            parsed = ftp_parse_eprt(argument, &target_ip, &target_port);
        }
        if (parsed < 0)
            return ftp_append(output, capacity, used, "522 Network protocol not supported.\r\n");
        if (!parsed || target_ip != client_ip || target_port < 1024)
            return ftp_append(output, capacity, used, "501 Invalid active data endpoint.\r\n");
        session->passive = false;
        session->active = true;
        session->active_ip = target_ip;
        session->active_port = target_port;
        return ftp_append(output, capacity, used, "200 Active data endpoint accepted.\r\n");
    }
    if (!strcmp(command, "SIZE")) {
        if (!session->authenticated)
            return ftp_append(output, capacity, used, "530 Please login first.\r\n");
        if (!ftp_file_name(argument))
            return ftp_append(output, capacity, used, "550 File not found.\r\n");
        return ftp_append(output, capacity, used, "213 %u\r\n", (unsigned) strlen(VSERVER_FTP_TEST_FILE_CONTENT));
    }
    if (!strcmp(command, "RETR") || !strcmp(command, "LIST") || !strcmp(command, "NLST")) {
        if (!session->authenticated)
            return ftp_append(output, capacity, used, "530 Please login first.\r\n");
        if (!session->passive && !session->active)
            return ftp_append(output, capacity, used, "425 Select PORT, EPRT, PASV, or EPSV first.\r\n");
        if (session->transfer != FTP_TRANSFER_NONE)
            return ftp_append(output, capacity, used, "450 A transfer is already in progress.\r\n");
        if (!strcmp(command, "RETR") && !ftp_file_name(argument))
            return ftp_append(output, capacity, used, "550 File not found.\r\n");
        session->transfer = !strcmp(command, "RETR") ? FTP_TRANSFER_FILE
                            : !strcmp(command, "NLST") ? FTP_TRANSFER_NAMES : FTP_TRANSFER_LIST;
        session->transfer_started = false;
        return ftp_append(output, capacity, used, session->passive ? "150 Opening passive data connection.\r\n"
                                                                    : "150 Opening active data connection.\r\n");
    }
    if (!strcmp(command, "STOR") || !strcmp(command, "APPE") || !strcmp(command, "DELE")
        || !strcmp(command, "MKD") || !strcmp(command, "RMD") || !strcmp(command, "RNFR")
        || !strcmp(command, "RNTO"))
        return ftp_append(output, capacity, used, "550 This server is read-only.\r\n");
    return ftp_append(output, capacity, used, "502 Command not implemented.\r\n");
}

vserver_ftp_session_t *
vserver_ftp_session_create(void)
{
    return (vserver_ftp_session_t *) calloc(1, sizeof(vserver_ftp_session_t));
}

void
vserver_ftp_session_close(vserver_ftp_session_t *session)
{
    free(session);
}

int
vserver_ftp_greeting(uint8_t *output, int capacity)
{
    static const char greeting[] = "220 86Box read-only FTP service ready.\r\n";

    if (!output || capacity < (int) sizeof(greeting))
        return -1;
    memcpy(output, greeting, sizeof(greeting) - 1);
    return (int) sizeof(greeting) - 1;
}

int
vserver_ftp_input(vserver_ftp_session_t *session, uint32_t server_ip, uint32_t client_ip, uint16_t passive_port,
                  const uint8_t *input, int length, uint8_t *output, int capacity, bool *close_after_reply)
{
    int used = 0;

    if (!session || !input || length < 0 || !output || capacity < 0 || !close_after_reply)
        return -1;
    *close_after_reply = false;
    for (int i = 0; i < length; i++) {
        char c = (char) input[i];

        if (c == '\n') {
            if (session->discard_line) {
                if (!ftp_append(output, capacity, &used, "500 Command line too long.\r\n"))
                    return -1;
                session->discard_line = false;
                session->line_length = 0;
                continue;
            }
            if (session->line_length && session->line[session->line_length - 1] == '\r')
                session->line_length--;
            session->line[session->line_length] = '\0';
            if (session->line_length
                && !ftp_command(session, server_ip, client_ip, passive_port, session->line, output, capacity, &used,
                                close_after_reply))
                return -1;
            session->line_length = 0;
        } else if (!session->discard_line) {
            if (session->line_length >= FTP_LINE_LENGTH - 1) {
                session->discard_line = true;
                session->line_length = 0;
            } else {
                session->line[session->line_length++] = c;
            }
        }
    }
    return used;
}

bool
vserver_ftp_passive_enabled(const vserver_ftp_session_t *session)
{
    return session && session->passive;
}

bool
vserver_ftp_active_target(const vserver_ftp_session_t *session, uint32_t *client_ip, uint16_t *client_port)
{
    if (!session || !session->active || !client_ip || !client_port)
        return false;
    *client_ip = session->active_ip;
    *client_port = session->active_port;
    return true;
}

bool
vserver_ftp_transfer_pending(const vserver_ftp_session_t *session)
{
    return session && session->transfer != FTP_TRANSFER_NONE;
}

bool
vserver_ftp_start_transfer(vserver_ftp_session_t *session, const uint8_t **data, int *length)
{
    if (!session || !data || !length || session->transfer == FTP_TRANSFER_NONE || session->transfer_started)
        return false;

    if (session->transfer == FTP_TRANSFER_FILE) {
        *data = (const uint8_t *) VSERVER_FTP_TEST_FILE_CONTENT;
        *length = (int) strlen(VSERVER_FTP_TEST_FILE_CONTENT);
    } else if (session->transfer == FTP_TRANSFER_NAMES) {
        int written = snprintf(session->listing, sizeof(session->listing), "%s\r\n", VSERVER_FTP_TEST_FILE_NAME);

        if (written < 0 || written >= (int) sizeof(session->listing))
            return false;
        *data = (const uint8_t *) session->listing;
        *length = written;
    } else {
        int written = snprintf(session->listing, sizeof(session->listing),
                               "-r--r--r-- 1 %s %s %u Jan 01 00:00 %s\r\n", VSERVER_LOGON_USERNAME,
                               VSERVER_LOGON_GROUP_NAME, (unsigned) strlen(VSERVER_FTP_TEST_FILE_CONTENT),
                               VSERVER_FTP_TEST_FILE_NAME);

        if (written < 0 || written >= (int) sizeof(session->listing))
            return false;
        *data = (const uint8_t *) session->listing;
        *length = written;
    }
    session->transfer_started = true;
    return true;
}

int
vserver_ftp_finish_transfer(vserver_ftp_session_t *session, bool success, uint8_t *output, int capacity)
{
    static const char *const replies[] = { "426 Transfer aborted.\r\n", "226 Transfer complete.\r\n" };
    int                       length;

    if (!session || !output || capacity < 0)
        return -1;
    session->transfer = FTP_TRANSFER_NONE;
    session->transfer_started = false;
    session->passive = false;
    session->active = false;
    session->active_ip = 0;
    session->active_port = 0;
    length = (int) strlen(replies[success ? 1 : 0]);
    if (length > capacity)
        return -1;
    memcpy(output, replies[success ? 1 : 0], (size_t) length);
    return length;
}

void
vserver_ftp_data_closed(vserver_ftp_session_t *session)
{
    if (session) {
        session->transfer = FTP_TRANSFER_NONE;
        session->transfer_started = false;
        session->passive = false;
        session->active = false;
        session->active_ip = 0;
        session->active_port = 0;
    }
}
