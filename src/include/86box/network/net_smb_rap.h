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
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_SMB_RAP_H
#define NETWORK_NET_SMB_RAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define SMB_RAP_PARAMS      8
#define SMB_RAP_INFO_PARAMS 6
#define SMB_RAP_MAX_DATA    1024

/* Runs one call from \PIPE\LANMAN for a session that has or hasn't proved the domain
   account. params has room for SMB_RAP_PARAMS bytes, with the number used set in
   param_length, and data has room for SMB_RAP_MAX_DATA. Returns the length of data. */
int smb_rap_dispatch(void *log, bool authenticated, const uint8_t *in, int in_len, uint8_t *params,
                     int *param_length, uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_SMB_RAP_H */
