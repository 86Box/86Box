/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          LAPB (ISO 7776 / X.25 level 2) reliable link layer used by
 *          PPP Numbered-Mode (RFC 1663). Modulo-8 and modulo-128 operation.
 *
 *          The module is independent of PPP: it exchanges (control, info)
 *          pairs with its owner through callbacks. The owner supplies the
 *          Address octet, the FCS and the HDLC byte stuffing.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_LAPB_H
#define EMU_MODEM_LAPB_H

#define LAPB_MAX_MODULO    128
#define LAPB_MAX_PENDING   256 /* Frames queued while the window is closed */
#define LAPB_MAX_INFO      4096

#define LAPB_DEFAULT_T1_MS 3000
#define LAPB_DEFAULT_N2    10

typedef enum {
    LAPB_DOWN = 0, /* Disconnected */
    LAPB_SETUP,    /* SABM(E) sent, waiting for UA */
    LAPB_UP        /* Information transfer */
} lapb_state_t;

typedef enum {
    LAPB_DOWN_PEER_DM,   /* Peer answered DM or sent DM while up */
    LAPB_DOWN_PEER_DISC, /* Peer sent DISC */
    LAPB_DOWN_TIMEOUT    /* N2 retransmissions of SABM or poll went unanswered */
} lapb_down_reason_t;

typedef struct lapb_ops_t {
    /* Transmit one frame: control octet(s) followed by optional information. */
    void (*emit)(void *priv, const uint8_t *control, int control_len,
                 const uint8_t *info, int info_len);
    /* An in-sequence I-frame information field. */
    void (*deliver)(void *priv, const uint8_t *info, int info_len);
    void (*link_up)(void *priv);
    void (*link_down)(void *priv, lapb_down_reason_t reason);
    /* Optional diagnostic text. */
    void (*log)(void *priv, const char *message);
} lapb_ops_t;

typedef struct lapb_stats_t {
    uint32_t tx_i;
    uint32_t retx_i;
    uint32_t rx_i;
    uint32_t rx_out_of_sequence;
    uint32_t rej_sent;
    uint32_t rej_received;
    uint32_t t1_expired;
    uint32_t resets;
    uint32_t dropped_queue_full;
} lapb_stats_t;

typedef struct lapb_t {
    lapb_ops_t   ops;
    void        *priv;
    lapb_state_t state;

    bool         ext;      /* modulo 128 */
    unsigned     modulus;
    unsigned     window;   /* k: maximum outstanding I-frames we may send */
    unsigned     t1_ms;
    unsigned     n2;

    unsigned     vs; /* V(S) next sequence number to send */
    unsigned     vr; /* V(R) next sequence number expected */
    unsigned     va; /* V(A) oldest unacknowledged sequence number */

    bool         peer_busy;
    bool         rej_exception;
    bool         poll_pending;  /* RR(P=1) sent, waiting for the F response */
    bool         ack_needed;
    bool         in_rx;
    bool         t1_running;
    unsigned     t1_elapsed;
    unsigned     retries;

    bool         connect_delay_active;
    unsigned     connect_delay_ms;
    unsigned     connect_delay_elapsed;

    uint8_t     *slot[LAPB_MAX_MODULO];
    int          slot_len[LAPB_MAX_MODULO];

    uint8_t     *pending[LAPB_MAX_PENDING];
    int          pending_len[LAPB_MAX_PENDING];
    unsigned     pending_head;
    unsigned     pending_count;

    lapb_stats_t stats;
} lapb_t;

#ifdef __cplusplus
extern "C" {
#endif

lapb_t  *lapb_create(const lapb_ops_t *ops, void *priv);
void     lapb_destroy(lapb_t *link);

/* window 1-7 selects modulo 8; 8-127 selects modulo 128. */
void     lapb_configure(lapb_t *link, unsigned window, unsigned t1_ms, unsigned n2);

/* Actively establish the link (send SABM or SABME). */
void     lapb_connect(lapb_t *link);

/* Wait for the peer to establish the link. If takeover_ms is non-zero and no
   SABM arrives in that time, establish it ourselves. */
void     lapb_listen(lapb_t *link, unsigned takeover_ms);

bool     lapb_is_up(const lapb_t *link);

/* Queue one information field. Returns false when the link is down or the
   pending queue is full. */
bool     lapb_send(lapb_t *link, const uint8_t *info, int info_len);

/* Process one received frame (control octet(s) + information, address and FCS
   already removed). Returns false if the frame was not a valid LAPB frame. */
bool     lapb_rx(lapb_t *link, const uint8_t *frame, int frame_len);

/* Advance timers by one millisecond. */
void     lapb_tick(lapb_t *link);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_LAPB_H */
