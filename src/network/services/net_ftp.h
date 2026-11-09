/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface for the virtual network server's read-only FTP service.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
typedef struct vserver_ftp_session_t vserver_ftp_session_t;

vserver_ftp_session_t *vserver_ftp_session_create(void);
void                   vserver_ftp_session_close(vserver_ftp_session_t *session);
int                    vserver_ftp_greeting(uint8_t *output, int capacity);
int                    vserver_ftp_input(vserver_ftp_session_t *session, uint32_t server_ip, uint32_t client_ip,
                                         uint16_t passive_port, const uint8_t *input, int length, uint8_t *output,
                                         int capacity, bool *close_after_reply);
bool                   vserver_ftp_passive_enabled(const vserver_ftp_session_t *session);
bool                   vserver_ftp_active_target(const vserver_ftp_session_t *session, uint32_t *client_ip,
                                                 uint16_t *client_port);
bool                   vserver_ftp_transfer_pending(const vserver_ftp_session_t *session);
bool                   vserver_ftp_start_transfer(vserver_ftp_session_t *session, const uint8_t **data, int *length);
int                    vserver_ftp_finish_transfer(vserver_ftp_session_t *session, bool success, uint8_t *output,
                                                   int capacity);
void                   vserver_ftp_data_closed(vserver_ftp_session_t *session);
