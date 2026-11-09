/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NetBEUI for the virtual server: the NetBIOS
 *          Frames protocol and the IEEE 802.2 LLC connection beneath it.
 *          It works on LLC frames, so it doesn't mind what carries them.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_NBF_H
#define NETWORK_NET_NBF_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBF_MAX_FRAME 1500 /* the largest LLC frame sent or received */

typedef struct nbf_t nbf_t;

/* Takes one LLC frame headed for the guest. */
typedef void (*nbf_output_t)(void *priv, const uint8_t *frame, int len);

nbf_t *nbf_init(void *log, nbf_output_t output, void *priv);
void   nbf_close(nbf_t *nbf);

/* Sets the largest LLC frame the link can carry towards the guest. */
void nbf_set_max_frame(nbf_t *nbf, int length);

/* Sets the credentials checked by legacy RAS sessions. */
bool nbf_set_ras_credentials(nbf_t *nbf, const char *username, const char *password);

/* Forgets every connection, for when the link carrying them starts again. */
void nbf_reset(nbf_t *nbf);

/* Handles one LLC frame from the guest. */
void nbf_input(nbf_t *nbf, const uint8_t *frame, int len);

/* Runs retransmission and announcements; call once per millisecond of emulated time. */
void nbf_tick(nbf_t *nbf);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_NBF_H */
