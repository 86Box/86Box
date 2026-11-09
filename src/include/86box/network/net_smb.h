/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SMB1 server for the virtual server: core, LAN
 *          Manager and NT LM dialects, a browse list and one read-only folder.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_SMB_H
#define NETWORK_NET_SMB_H

#ifdef __cplusplus
extern "C" {
#endif

#define SMB_MAX_OUTPUT 16384

typedef struct smb_session_t smb_session_t;

smb_session_t *smb_session_create(void *log);
void           smb_session_close(smb_session_t *session);

/* For a transport with sessions of its own, where no NetBIOS session request will come.
   Max_message is the largest SMB message it can carry, or 0 when it can carry any. */
void smb_session_attach(smb_session_t *session, int max_message);

/* Feeds bytes from one TCP stream. Whatever should go back is appended to out, which
   holds cap bytes. Returns the number of bytes written, or -1 to close the connection. */
int smb_session_input(smb_session_t *session, const uint8_t *data, int len, uint8_t *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_SMB_H */
