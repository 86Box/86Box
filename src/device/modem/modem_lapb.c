/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          LAPB (ISO 7776 / X.25 level 2) reliable link layer used by
 *          PPP Numbered-Mode (RFC 1663).
 *
 *          Supported: SABM/SABME, UA, DM, DISC, FRMR (received), I, RR,
 *          RNR, REJ; modulo 8 and modulo 128; go-back-N retransmission;
 *          T1 timer with poll-based recovery and N2 retry limit.
 *          Not supported: SREJ, multi-octet addresses, T3 idle polling.
 *
 *          RFC 1663 leaves the control-octet encodings to ISO 7776; the
 *          standard HDLC/LAPB values are used here.
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
#include <86box/modem/modem_lapb.h>

/* Unnumbered frames (control octet with the P/F bit clear). */
#define LAPB_U_SABM 0x2F
#define LAPB_U_SABME 0x6F
#define LAPB_U_UA   0x63
#define LAPB_U_DM   0x0F
#define LAPB_U_DISC 0x43
#define LAPB_U_FRMR 0x87
#define LAPB_U_PF   0x10

/* Supervisory frames (low four bits of the first control octet). */
#define LAPB_S_RR   0x01
#define LAPB_S_RNR  0x05
#define LAPB_S_REJ  0x09

static void
lapb_log(lapb_t *link, const char *fmt, ...)
{
    char    text[160];
    va_list ap;

    if (!link->ops.log)
        return;

    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    link->ops.log(link->priv, text);
}

static unsigned
lapb_inc(const lapb_t *link, unsigned value)
{
    return (value + 1) % link->modulus;
}

static unsigned
lapb_outstanding(const lapb_t *link)
{
    return (link->vs + link->modulus - link->va) % link->modulus;
}

static void
lapb_start_t1(lapb_t *link)
{
    link->t1_running = true;
    link->t1_elapsed = 0;
}

static void
lapb_stop_t1(lapb_t *link)
{
    link->t1_running = false;
    link->t1_elapsed = 0;
}

static void
lapb_free_slots(lapb_t *link)
{
    for (int i = 0; i < LAPB_MAX_MODULO; i++) {
        free(link->slot[i]);
        link->slot[i]     = NULL;
        link->slot_len[i] = 0;
    }
}

static void
lapb_free_pending(lapb_t *link)
{
    for (unsigned i = 0; i < link->pending_count; i++) {
        unsigned index = (link->pending_head + i) % LAPB_MAX_PENDING;
        free(link->pending[index]);
        link->pending[index]     = NULL;
        link->pending_len[index] = 0;
    }
    link->pending_head  = 0;
    link->pending_count = 0;
}

static void
lapb_reset_variables(lapb_t *link)
{
    lapb_free_slots(link);
    lapb_free_pending(link);
    link->vs = link->vr = link->va = 0;
    link->peer_busy     = false;
    link->rej_exception = false;
    link->poll_pending  = false;
    link->ack_needed    = false;
    link->retries       = 0;
    lapb_stop_t1(link);
}

static void
lapb_emit_u(lapb_t *link, uint8_t control)
{
    link->ops.emit(link->priv, &control, 1, NULL, 0);
}

static void
lapb_emit_s(lapb_t *link, uint8_t type, bool pf)
{
    uint8_t control[2];

    if (link->ext) {
        control[0] = type;
        control[1] = (uint8_t) ((link->vr << 1) | (pf ? 1 : 0));
        link->ops.emit(link->priv, control, 2, NULL, 0);
    } else {
        control[0] = (uint8_t) (type | (pf ? 0x10 : 0) | (link->vr << 5));
        link->ops.emit(link->priv, control, 1, NULL, 0);
    }
    link->ack_needed = false;
}

static void
lapb_emit_i(lapb_t *link, unsigned ns, bool pf)
{
    uint8_t control[2];

    if (!link->slot[ns])
        return;

    if (link->ext) {
        control[0] = (uint8_t) (ns << 1);
        control[1] = (uint8_t) ((link->vr << 1) | (pf ? 1 : 0));
        link->ops.emit(link->priv, control, 2, link->slot[ns], link->slot_len[ns]);
    } else {
        control[0] = (uint8_t) ((ns << 1) | (pf ? 0x10 : 0) | (link->vr << 5));
        link->ops.emit(link->priv, control, 1, link->slot[ns], link->slot_len[ns]);
    }
    link->ack_needed = false;
}

/* Send queued information fields while the window allows. */
static void
lapb_pump(lapb_t *link)
{
    if (link->in_rx)
        return;

    while (link->state == LAPB_UP && !link->peer_busy && !link->poll_pending
           && link->pending_count > 0 && lapb_outstanding(link) < link->window) {
        unsigned index = link->pending_head;

        link->slot[link->vs]     = link->pending[index];
        link->slot_len[link->vs] = link->pending_len[index];
        link->pending[index]     = NULL;
        link->pending_len[index] = 0;
        link->pending_head       = (link->pending_head + 1) % LAPB_MAX_PENDING;
        link->pending_count--;

        lapb_emit_i(link, link->vs, false);
        link->stats.tx_i++;
        link->vs = lapb_inc(link, link->vs);
        if (!link->t1_running)
            lapb_start_t1(link);
    }
}

static void
lapb_retransmit_all(lapb_t *link)
{
    for (unsigned seq = link->va; seq != link->vs; seq = lapb_inc(link, seq)) {
        lapb_emit_i(link, seq, false);
        link->stats.retx_i++;
    }
    if (lapb_outstanding(link) != 0)
        lapb_start_t1(link);
}

/* N(R) must lie in the range V(A) .. V(S) inclusive, modulo the sequence space. */
static bool
lapb_nr_valid(const lapb_t *link, unsigned nr)
{
    unsigned acked = (nr + link->modulus - link->va) % link->modulus;

    return acked <= lapb_outstanding(link);
}

/* Release acknowledged frames. The caller has already checked N(R). */
static void
lapb_process_nr(lapb_t *link, unsigned nr)
{
    unsigned acked = (nr + link->modulus - link->va) % link->modulus;

    while (link->va != nr) {
        free(link->slot[link->va]);
        link->slot[link->va]     = NULL;
        link->slot_len[link->va] = 0;
        link->va                 = lapb_inc(link, link->va);
    }

    if (acked != 0) {
        link->retries = 0;
        if (!link->poll_pending) {
            if (lapb_outstanding(link) == 0)
                lapb_stop_t1(link);
            else
                lapb_start_t1(link);
        }
    }
}

/* The F bit answered our checkpoint poll. */
static void
lapb_poll_response(lapb_t *link)
{
    link->poll_pending = false;
    link->retries      = 0;

    if (!link->peer_busy)
        lapb_retransmit_all(link);

    if (lapb_outstanding(link) != 0 || link->peer_busy)
        lapb_start_t1(link);
    else
        lapb_stop_t1(link);
}

static void
lapb_down(lapb_t *link, lapb_down_reason_t reason)
{
    link->state                = LAPB_DOWN;
    link->connect_delay_active = false;
    lapb_reset_variables(link);
    if (link->ops.link_down)
        link->ops.link_down(link->priv, reason);
}

/* Unrecoverable frame or sequence error: re-run the set-up procedure. */
static void
lapb_reestablish(lapb_t *link, const char *why)
{
    lapb_log(link, "LAPB: %s; re-establishing the link", why);
    link->stats.resets++;
    lapb_connect(link);
}

static void
lapb_rx_i(lapb_t *link, unsigned ns, unsigned nr, bool pf,
          const uint8_t *info, int info_len)
{
    bool respond_f = pf;

    if (!lapb_nr_valid(link, nr)) {
        lapb_reestablish(link, "invalid N(R) in I-frame");
        return;
    }
    lapb_process_nr(link, nr);

    if (pf && link->poll_pending) {
        lapb_poll_response(link);
        respond_f = false;
    }

    if (ns == link->vr) {
        link->vr            = lapb_inc(link, link->vr);
        link->rej_exception = false;
        link->ack_needed    = true;
        link->stats.rx_i++;
        if (link->ops.deliver)
            link->ops.deliver(link->priv, info, info_len);
    } else {
        link->stats.rx_out_of_sequence++;
        if (!link->rej_exception) {
            link->rej_exception = true;
            link->stats.rej_sent++;
            lapb_emit_s(link, LAPB_S_REJ, pf);
            respond_f = false;
        }
        /* Otherwise discard silently until the sequence is repaired. */
    }

    if (respond_f)
        lapb_emit_s(link, LAPB_S_RR, true);
}

static void
lapb_rx_s(lapb_t *link, unsigned type, bool pf, unsigned nr)
{
    bool respond_f = false;
    bool retransmitted = false;

    if (!lapb_nr_valid(link, nr)) {
        lapb_reestablish(link, "invalid N(R) in supervisory frame");
        return;
    }

    switch (type) {
        case LAPB_S_RR:
            link->peer_busy = false;
            break;
        case LAPB_S_RNR:
            link->peer_busy = true;
            break;
        case LAPB_S_REJ:
            link->peer_busy = false;
            link->stats.rej_received++;
            break;
        default:
            return;
    }

    lapb_process_nr(link, nr);

    if (pf) {
        if (link->poll_pending) {
            lapb_poll_response(link);
            retransmitted = true;
        } else {
            respond_f = true;
        }
    }

    if (type == LAPB_S_REJ && !retransmitted)
        lapb_retransmit_all(link);

    /* A busy peer must be polled until it reports ready again. */
    if (link->peer_busy && !link->t1_running)
        lapb_start_t1(link);

    if (respond_f)
        lapb_emit_s(link, LAPB_S_RR, true);
}

static void
lapb_rx_u(lapb_t *link, uint8_t control)
{
    bool    pf   = (control & LAPB_U_PF) != 0;
    uint8_t type = (uint8_t) (control & ~LAPB_U_PF);

    switch (type) {
        case LAPB_U_SABM:
        case LAPB_U_SABME:
            {
                bool want_ext = type == LAPB_U_SABME;
                bool was_up   = link->state == LAPB_UP;

                if (want_ext != link->ext) {
                    lapb_log(link, "LAPB: peer asked for modulo %d but modulo %u is configured",
                             want_ext ? 128 : 8, link->modulus);
                    lapb_emit_u(link, (uint8_t) (LAPB_U_DM | (pf ? LAPB_U_PF : 0)));
                    break;
                }
                lapb_reset_variables(link);
                link->connect_delay_active = false;
                lapb_emit_u(link, (uint8_t) (LAPB_U_UA | (pf ? LAPB_U_PF : 0)));
                link->state = LAPB_UP;
                if (!was_up && link->ops.link_up)
                    link->ops.link_up(link->priv);
            }
            break;

        case LAPB_U_UA:
            if (link->state == LAPB_SETUP) {
                lapb_reset_variables(link);
                link->state = LAPB_UP;
                if (link->ops.link_up)
                    link->ops.link_up(link->priv);
            }
            break;

        case LAPB_U_DM:
            if (link->state != LAPB_DOWN) {
                lapb_log(link, "LAPB: peer answered DM");
                lapb_down(link, LAPB_DOWN_PEER_DM);
            }
            break;

        case LAPB_U_DISC:
            lapb_emit_u(link, (uint8_t) (LAPB_U_UA | (pf ? LAPB_U_PF : 0)));
            if (link->state != LAPB_DOWN)
                lapb_down(link, LAPB_DOWN_PEER_DISC);
            break;

        case LAPB_U_FRMR:
            if (link->state == LAPB_UP)
                lapb_reestablish(link, "peer reported FRMR");
            break;

        default:
            lapb_log(link, "LAPB: ignored unnumbered frame 0x%02X", (unsigned) control);
            break;
    }
}

lapb_t *
lapb_create(const lapb_ops_t *ops, void *priv)
{
    lapb_t *link;

    if (!ops || !ops->emit)
        return NULL;

    link = (lapb_t *) calloc(1, sizeof(lapb_t));
    if (!link)
        return NULL;

    link->ops   = *ops;
    link->priv  = priv;
    link->state = LAPB_DOWN;
    lapb_configure(link, 7, LAPB_DEFAULT_T1_MS, LAPB_DEFAULT_N2);
    return link;
}

void
lapb_destroy(lapb_t *link)
{
    if (!link)
        return;
    lapb_reset_variables(link);
    free(link);
}

void
lapb_configure(lapb_t *link, unsigned window, unsigned t1_ms, unsigned n2)
{
    if (!link)
        return;

    if (window < 1)
        window = 1;
    if (window > 127)
        window = 127;

    link->ext     = window >= 8;
    link->modulus = link->ext ? 128 : 8;
    link->window  = window;
    link->t1_ms   = t1_ms ? t1_ms : LAPB_DEFAULT_T1_MS;
    link->n2      = n2 ? n2 : LAPB_DEFAULT_N2;
}

void
lapb_connect(lapb_t *link)
{
    if (!link)
        return;

    lapb_reset_variables(link);
    link->connect_delay_active = false;
    link->state                = LAPB_SETUP;
    lapb_emit_u(link, (uint8_t) ((link->ext ? LAPB_U_SABME : LAPB_U_SABM) | LAPB_U_PF));
    lapb_start_t1(link);
}

void
lapb_listen(lapb_t *link, unsigned takeover_ms)
{
    if (!link)
        return;

    lapb_reset_variables(link);
    link->state                 = LAPB_DOWN;
    link->connect_delay_ms      = takeover_ms;
    link->connect_delay_elapsed = 0;
    link->connect_delay_active  = takeover_ms != 0;
}

bool
lapb_is_up(const lapb_t *link)
{
    return link && link->state == LAPB_UP;
}

bool
lapb_send(lapb_t *link, const uint8_t *info, int info_len)
{
    uint8_t *copy;
    unsigned index;

    if (!link || link->state != LAPB_UP || info_len < 0 || info_len > LAPB_MAX_INFO
        || (!info && info_len != 0))
        return false;

    if (link->pending_count >= LAPB_MAX_PENDING) {
        link->stats.dropped_queue_full++;
        return false;
    }

    copy = (uint8_t *) malloc(info_len ? (size_t) info_len : 1);
    if (!copy)
        return false;
    if (info_len)
        memcpy(copy, info, (size_t) info_len);

    index                    = (link->pending_head + link->pending_count) % LAPB_MAX_PENDING;
    link->pending[index]     = copy;
    link->pending_len[index] = info_len;
    link->pending_count++;

    lapb_pump(link);
    return true;
}

bool
lapb_rx(lapb_t *link, const uint8_t *frame, int frame_len)
{
    uint8_t first;

    if (!link || !frame || frame_len < 1)
        return false;

    first = frame[0];

    if ((first & 0x01) == 0) {
        /* Information frame. */
        unsigned       ns;
        unsigned       nr;
        bool           pf;
        const uint8_t *info;
        int            info_len;

        if (link->ext) {
            if (frame_len < 2)
                return false;
            ns       = first >> 1;
            nr       = frame[1] >> 1;
            pf       = (frame[1] & 0x01) != 0;
            info     = frame + 2;
            info_len = frame_len - 2;
        } else {
            ns       = (first >> 1) & 0x07;
            pf       = (first & 0x10) != 0;
            nr       = (first >> 5) & 0x07;
            info     = frame + 1;
            info_len = frame_len - 1;
        }

        if (link->state == LAPB_DOWN) {
            lapb_emit_u(link, (uint8_t) (LAPB_U_DM | (pf ? LAPB_U_PF : 0)));
            return true;
        }
        if (link->state != LAPB_UP)
            return true;

        link->in_rx = true;
        lapb_rx_i(link, ns, nr, pf, info, info_len);
        link->in_rx = false;
    } else if ((first & 0x03) == 0x01) {
        /* Supervisory frame. */
        unsigned type = first & 0x0F;
        unsigned nr;
        bool     pf;

        if (link->ext) {
            if (frame_len < 2 || (first & 0xF0) != 0)
                return false;
            nr = frame[1] >> 1;
            pf = (frame[1] & 0x01) != 0;
        } else {
            nr = (first >> 5) & 0x07;
            pf = (first & 0x10) != 0;
        }

        if (type != LAPB_S_RR && type != LAPB_S_RNR && type != LAPB_S_REJ)
            return false;

        if (link->state == LAPB_DOWN) {
            lapb_emit_u(link, (uint8_t) (LAPB_U_DM | (pf ? LAPB_U_PF : 0)));
            return true;
        }
        if (link->state != LAPB_UP)
            return true;

        link->in_rx = true;
        lapb_rx_s(link, type, pf, nr);
        link->in_rx = false;
    } else {
        /* Unnumbered frame. */
        lapb_rx_u(link, first);
        return true;
    }

    if (link->state == LAPB_UP) {
        lapb_pump(link);
        if (link->ack_needed)
            lapb_emit_s(link, LAPB_S_RR, false);
    }
    return true;
}

void
lapb_tick(lapb_t *link)
{
    if (!link)
        return;

    if (link->connect_delay_active) {
        if (++link->connect_delay_elapsed >= link->connect_delay_ms) {
            link->connect_delay_active = false;
            if (link->state == LAPB_DOWN)
                lapb_connect(link);
        }
    }

    if (!link->t1_running)
        return;
    if (++link->t1_elapsed < link->t1_ms)
        return;
    link->t1_elapsed = 0;
    link->stats.t1_expired++;

    if (link->state == LAPB_SETUP) {
        if (link->retries >= link->n2) {
            lapb_log(link, "LAPB: no answer to SABM after %u attempts", link->retries + 1);
            lapb_down(link, LAPB_DOWN_TIMEOUT);
            return;
        }
        link->retries++;
        lapb_emit_u(link, (uint8_t) ((link->ext ? LAPB_U_SABME : LAPB_U_SABM) | LAPB_U_PF));
        return;
    }

    if (link->state == LAPB_UP) {
        if (link->retries >= link->n2) {
            lapb_log(link, "LAPB: peer silent after %u polls", link->retries);
            lapb_down(link, LAPB_DOWN_TIMEOUT);
            return;
        }
        link->retries++;
        link->poll_pending = true;
        lapb_emit_s(link, LAPB_S_RR, true);
    }
}
