/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs.
 *
 *          This file is part of the 86Box distribution.
 *
 *          A read-only TFTP service for the virtual network server.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/network/net_vserver_config.h>
#include "net_tftp.h"

#define TFTP_PORT          69
#define TFTP_BLOCK_SIZE    512
#define TFTP_MAX_SESSIONS  4
#define TFTP_SERVER_PORT   40000
#define TFTP_RETRY_MS      1000
#define TFTP_MAX_RETRIES   5

typedef struct tftp_session_t {
    bool     active;
    uint32_t peer_ip;
    uint16_t peer_port;
    uint16_t local_port;
    uint16_t block;
    int      offset;
    int      last_length;
    int      retries;
    int      elapsed_ms;
    bool     final_block;
} tftp_session_t;

struct vserver_tftp_t {
    vserver_tftp_output_t output;
    void                 *priv;
    tftp_session_t        sessions[TFTP_MAX_SESSIONS];
};

static uint16_t
tftp_get16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static void
tftp_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) (value >> 8);
    p[1] = (uint8_t) value;
}

static char
tftp_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

static bool
tftp_equal(const uint8_t *left, int left_length, const char *right)
{
    size_t right_length = strlen(right);

    if (left_length != (int) right_length)
        return false;
    for (int i = 0; i < left_length; i++) {
        if (tftp_lower((char) left[i]) != tftp_lower(right[i]))
            return false;
    }
    return true;
}

static void
tftp_send(vserver_tftp_t *tftp, tftp_session_t *session, const uint8_t *data, int length)
{
    tftp->output(tftp->priv, session->peer_ip, session->local_port, session->peer_port, data, length);
    session->elapsed_ms = 0;
}

static void
tftp_send_data(vserver_tftp_t *tftp, tftp_session_t *session)
{
    static const uint8_t file[] = VSERVER_TFTP_TEST_FILE_CONTENT;
    uint8_t              packet[4 + TFTP_BLOCK_SIZE];
    int                  remaining = (int) sizeof(file) - 1 - session->offset;
    int                  length = remaining > TFTP_BLOCK_SIZE ? TFTP_BLOCK_SIZE : remaining;

    if (length < 0)
        length = 0;
    tftp_put16(packet, 3);
    tftp_put16(packet + 2, session->block);
    if (length)
        memcpy(packet + 4, file + session->offset, (size_t) length);
    session->last_length = length;
    session->final_block = length < TFTP_BLOCK_SIZE;
    tftp_send(tftp, session, packet, 4 + length);
}

static void
tftp_error(vserver_tftp_t *tftp, uint32_t destination, uint16_t port, uint16_t source_port, uint16_t code,
           const char *message)
{
    uint8_t packet[128];
    size_t  length = strlen(message);

    if (length + 5 > sizeof(packet))
        return;
    tftp_put16(packet, 5);
    tftp_put16(packet + 2, code);
    memcpy(packet + 4, message, length + 1);
    tftp->output(tftp->priv, destination, source_port, port, packet, (int) length + 5);
}

static bool
tftp_filename(const uint8_t *data, int length)
{
    return tftp_equal(data, length, VSERVER_TFTP_TEST_FILE_NAME);
}

static tftp_session_t *
tftp_find_peer(vserver_tftp_t *tftp, uint32_t ip, uint16_t port)
{
    for (int i = 0; i < TFTP_MAX_SESSIONS; i++) {
        tftp_session_t *session = &tftp->sessions[i];

        if (session->active && session->peer_ip == ip && session->peer_port == port)
            return session;
    }
    return NULL;
}

static tftp_session_t *
tftp_find_local(vserver_tftp_t *tftp, uint16_t port)
{
    for (int i = 0; i < TFTP_MAX_SESSIONS; i++) {
        tftp_session_t *session = &tftp->sessions[i];

        if (session->active && session->local_port == port)
            return session;
    }
    return NULL;
}

vserver_tftp_t *
vserver_tftp_create(vserver_tftp_output_t output, void *priv)
{
    vserver_tftp_t *tftp;

    if (!output)
        return NULL;
    tftp = (vserver_tftp_t *) calloc(1, sizeof(vserver_tftp_t));
    if (tftp) {
        tftp->output = output;
        tftp->priv = priv;
    }
    return tftp;
}

void
vserver_tftp_close(vserver_tftp_t *tftp)
{
    free(tftp);
}

bool
vserver_tftp_input(vserver_tftp_t *tftp, uint32_t source, uint16_t source_port, uint16_t destination_port,
                   const uint8_t *data, int length)
{
    uint16_t opcode;

    if (!tftp || !data)
        return false;
    if (length < 2) {
        if (destination_port == TFTP_PORT)
            tftp_error(tftp, source, TFTP_PORT, source_port, 4, "Malformed request");
        return destination_port == TFTP_PORT;
    }
    opcode = tftp_get16(data);
    if (destination_port == TFTP_PORT) {
        tftp_session_t *session;
        int             filename_end = 2;
        int             mode_end;

        if (opcode == 2) {
            tftp_error(tftp, source, TFTP_PORT, source_port, 2, "Read-only server");
            return true;
        }
        if (opcode != 1) {
            tftp_error(tftp, source, TFTP_PORT, source_port, 4, "Unknown request");
            return true;
        }
        while (filename_end < length && data[filename_end])
            filename_end++;
        if (filename_end == length || filename_end == 2) {
            tftp_error(tftp, source, TFTP_PORT, source_port, 4, "Malformed request");
            return true;
        }
        mode_end = filename_end + 1;
        while (mode_end < length && data[mode_end])
            mode_end++;
        if (mode_end == length || mode_end == filename_end + 1) {
            tftp_error(tftp, source, TFTP_PORT, source_port, 4, "Malformed request");
            return true;
        }
        if (!tftp_filename(data + 2, filename_end - 2)) {
            tftp_error(tftp, source, TFTP_PORT, source_port, 1, "File not found");
            return true;
        }
        if (!tftp_equal(data + filename_end + 1, mode_end - filename_end - 1, "octet")
            && !tftp_equal(data + filename_end + 1, mode_end - filename_end - 1, "netascii")) {
            tftp_error(tftp, source, TFTP_PORT, source_port, 4, "Unsupported transfer mode");
            return true;
        }

        session = tftp_find_peer(tftp, source, source_port);
        if (!session) {
            for (int i = 0; i < TFTP_MAX_SESSIONS && !session; i++) {
                if (!tftp->sessions[i].active) {
                    session = &tftp->sessions[i];
                    break;
                }
            }
        }
        if (!session) {
            tftp_error(tftp, source, TFTP_PORT, source_port, 0, "Server busy");
            return true;
        }
        memset(session, 0, sizeof(*session));
        session->active = true;
        session->peer_ip = source;
        session->peer_port = source_port;
        session->local_port = (uint16_t) (TFTP_SERVER_PORT + (session - tftp->sessions));
        session->block = 1;
        tftp_send_data(tftp, session);
        return true;
    }

    {
        tftp_session_t *session = tftp_find_local(tftp, destination_port);
        if (!session)
            return false;
        if (session->peer_ip != source || session->peer_port != source_port) {
            tftp_error(tftp, source, destination_port, source_port, 5, "Unknown transfer ID");
            return true;
        }
        if (opcode == 4 && length == 4) {
            uint16_t block = tftp_get16(data + 2);

            if (block == session->block) {
                if (session->final_block) {
                    memset(session, 0, sizeof(*session));
                    return true;
                }
                session->offset += session->last_length;
                session->block++;
                session->retries = 0;
                tftp_send_data(tftp, session);
            } else if (block == (uint16_t) (session->block - 1)) {
                tftp_send_data(tftp, session);
            }
            return true;
        }
        tftp_error(tftp, source, destination_port, session->peer_port, 4, "Illegal operation");
        return true;
    }
}

void
vserver_tftp_tick(vserver_tftp_t *tftp)
{
    if (!tftp)
        return;
    for (int i = 0; i < TFTP_MAX_SESSIONS; i++) {
        tftp_session_t *session = &tftp->sessions[i];

        if (!session->active || ++session->elapsed_ms < TFTP_RETRY_MS)
            continue;
        if (++session->retries > TFTP_MAX_RETRIES) {
            memset(session, 0, sizeof(*session));
            continue;
        }
        session->elapsed_ms = 0;
        tftp_send_data(tftp, session);
    }
}
