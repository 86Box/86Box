/*
 * 86Box: Sony CDU-31A / CDU-33A proprietary CD-ROM interface.
 *
 * Protocol references: Sony CDU31A service manual 9-974-500-11 and
 * Corey Minyard's Linux cdu31a driver (GPL-2.0-or-later).
 * This models the host protocol, not the drive's microcontroller firmware.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/timer.h>
#include <86box/cdrom.h>
#include <86box/cdrom_sony.h>
#include <86box/ui.h>

#ifdef ENABLE_SONY_CDROM_LOG
#    define sony_log pclog
#else
#    define sony_log(...) ((void) 0)
#endif

enum {
    SONY_ATTN        = 1,
    SONY_RESULT      = 2,
    SONY_DATA        = 4,
    SONY_BUSY        = 0x80,
    SONY_BAD_COMMAND = 0x10,
    SONY_BAD_PARAM   = 0x11,
    SONY_NO_DISC     = 0x21,
    SONY_NO_TOC      = 0x60
};

typedef struct sony_cdrom_t {
    cdrom_t   *cd;
    uint16_t   base;
    int        irq, dma;
    pc_timer_t command_timer, sector_timer, dma_timer;
    uint8_t    status, control, command, double_speed;
    uint8_t    params[10], param_count, param_overflow;
    uint8_t    settings[7][2];
    uint8_t    results[1024];
    unsigned   result_pos, result_len, result_end;
    uint8_t    attention[16], attention_head, attention_count, attention_read, attention_ack;
    uint8_t    reading, block_status, block_pending, final_pending;
    uint8_t    spinning, toc_read, audio_active, tray_open;
    uint32_t   lba, remaining;
    uint8_t    sector[2352];
    unsigned   data_pos, data_len;
} sony_cdrom_t;

static void sony_sector(void *priv);
static void sony_reset(void *priv);

static int
sony_ready(const sony_cdrom_t *d)
{
    return d->cd && d->cd->ops && (d->cd->cd_status & CD_STATUS_MASK) != CD_STATUS_EMPTY && (d->cd->cd_status & CD_STATUS_MASK) != CD_STATUS_DVD_REJECTED;
}

static void
sony_irq(sony_cdrom_t *d)
{
    if (d->irq < 0)
        return;
    if (((d->status & 7) << 3) & d->control)
        picint(1 << d->irq);
    else
        picintc(1 << d->irq);
}

static void
sony_attention(sony_cdrom_t *d, uint8_t code)
{
    if (d->attention_count < sizeof(d->attention)) {
        d->attention[(d->attention_head + d->attention_count++) % sizeof(d->attention)] = code;
        if (!d->attention_ack)
            d->status |= SONY_ATTN;
        sony_irq(d);
    }
}

static void
sony_next_attention(sony_cdrom_t *d)
{
    /* DOS acknowledges before reading; NT 3.1 reads before acknowledging. */
    if (d->attention_read && d->attention_ack) {
        d->attention_head = (d->attention_head + 1) % sizeof(d->attention);
        --d->attention_count;
        d->attention_read = d->attention_ack = 0;
        if (d->attention_count)
            d->status |= SONY_ATTN;
    }
}

static void
sony_result(sony_cdrom_t *d, const uint8_t *data, unsigned len)
{
    memcpy(d->results, data, len);
    d->result_pos = 0;
    d->result_len = len;
    d->result_end = MIN(len, 10);
    d->status     = (d->status & ~SONY_BUSY) | SONY_RESULT;
    sony_irq(d);
}

static void
sony_reply(sony_cdrom_t *d, const uint8_t *data, unsigned len)
{
    uint8_t reply[1024];
    reply[0] = len >> 8;
    reply[1] = len;
    if (len)
        memcpy(reply + 2, data, len);
    sony_result(d, reply, len + 2);
}

static void
sony_error(sony_cdrom_t *d, uint8_t error)
{
    const uint8_t reply[2] = { 0x20, error };
    sony_log("Sony: command %02x error %02x\n", d->command, error);
    sony_result(d, reply, sizeof(reply));
}

static void
sony_stop_read(sony_cdrom_t *d)
{
    timer_disable(&d->sector_timer);
    timer_disable(&d->dma_timer);
    if (d->dma >= 0)
        dma_set_drq(d->dma, 0);
    d->reading = d->block_pending = d->final_pending = 0;
    d->data_pos = d->data_len = d->remaining = 0;
    d->status &= ~SONY_DATA;
    if (d->cd)
        ui_sb_update_icon(SB_CDROM | d->cd->id, 0);
}

static uint8_t
sony_bcd(unsigned n)
{
    return ((n / 10) << 4) | (n % 10);
}

static void
sony_msf(uint32_t frames, uint8_t *p)
{
    p[0] = sony_bcd(frames / 4500);
    p[1] = sony_bcd((frames / 75) % 60);
    p[2] = sony_bcd(frames % 75);
}

static int
sony_lba(const uint8_t *p, uint32_t *lba)
{
    unsigned v[3];
    for (int i = 0; i < 3; ++i) {
        if ((p[i] & 15) > 9 || (p[i] >> 4) > 9)
            return 0;
        v[i] = (p[i] >> 4) * 10 + (p[i] & 15);
    }
    if (v[1] >= 60 || v[2] >= 75)
        return 0;
    unsigned frames = (v[0] * 60 + v[1]) * 75 + v[2];
    if (frames < 150)
        return 0;
    *lba = frames - 150;
    return 1;
}

static uint32_t
sony_capacity(const sony_cdrom_t *d)
{
    track_info_t t;
    if (!sony_ready(d) || !d->cd->ops->get_track_info || !d->cd->ops->get_track_info(d->cd->local, 0xa2, 0, &t))
        return 0;
    uint32_t frames = ((uint32_t) t.m * 60 + t.s) * 75 + t.f;
    return frames >= 150 ? frames - 150 : 0;
}

/* Preserve the ten-byte result FIFO boundaries, including the two-byte
   execution status in the first batch. Acknowledging RDY does not drain it. */
static void
sony_next_result(sony_cdrom_t *d)
{
    if (d->block_pending) {
        uint8_t result   = d->block_status;
        d->block_pending = 0;
        sony_result(d, &result, 1);
    } else if (d->final_pending) {
        d->final_pending = 0;
        sony_reply(d, NULL, 0);
    }
}

static void
sony_data_done(sony_cdrom_t *d)
{
    d->data_pos = d->data_len = 0;
    d->status &= ~SONY_DATA;
    if (d->dma >= 0)
        dma_set_drq(d->dma, 0);
    ++d->lba;
    if (d->cd)
        d->cd->seek_pos = d->lba;
    if (!--d->remaining) {
        d->reading       = 0;
        d->final_pending = 1;
        if (d->cd)
            ui_sb_update_icon(SB_CDROM | d->cd->id, 0);
    }
    if (d->command == 0x34)
        d->block_pending = 1;
    sony_next_result(d);
    if (d->reading)
        timer_set_delay_u64(&d->sector_timer, (1000000 / (75 * d->cd->cur_speed)) * TIMER_USEC);
    sony_irq(d);
}

static void
sony_dma(void *priv)
{
    sony_cdrom_t *d = priv;
    if (d->dma < 0 || !d->data_len)
        return;
    /* A bounded burst lets a masked/reprogrammed 8237 keep the unread bytes. */
    for (unsigned n = 0; n < 128 && d->data_pos < d->data_len; ++n) {
        int result = dma_channel_write(d->dma, d->sector[d->data_pos]);
        if (result == DMA_NODATA)
            break;
        ++d->data_pos;
        if (result & DMA_OVER)
            break;
    }
    if (d->data_pos == d->data_len)
        sony_data_done(d);
    else
        timer_set_delay_u64(&d->dma_timer, 16 * TIMER_USEC);
}

static void
sony_sector(void *priv)
{
    sony_cdrom_t *d   = priv;
    int           len = 0;
    if (!d->reading || d->data_len)
        return;
    /* Do not overwrite an unconsumed block status. */
    if (d->result_pos < d->result_len) {
        timer_set_delay_u64(&d->sector_timer, 100 * TIMER_USEC);
        return;
    }
    if (!sony_ready(d)) {
        sony_stop_read(d);
        sony_error(d, SONY_NO_DISC);
        return;
    }
    int raw   = !(d->settings[0][0] & 8);
    int audio = d->cd->ops->get_track_type && d->cd->ops->get_track_type(d->cd->local, d->lba) == CD_TRACK_AUDIO;
    if ((!raw && audio) || !cdrom_readsector_raw(d->cd, d->sector, d->lba, 0, 0, raw ? 0xf8 : 0x10, &len, 0) || len != (raw ? 2352 : 2048)) {
        sony_stop_read(d);
        sony_error(d, audio ? 0x40 : 0x57);
        return;
    }
    if (raw && !audio) {
        memmove(d->sector, d->sector + 12, 2340);
        len = 2340;
    }
    d->data_len     = len;
    d->data_pos     = 0;
    d->block_status = audio ? 0x50 : 0x54;
    d->status |= SONY_DATA;
    sony_irq(d);
    if (d->dma >= 0 && (d->settings[1][0] & 1)) {
        dma_set_drq(d->dma, 1);
        timer_set_delay_u64(&d->dma_timer, 16 * TIMER_USEC);
    }
}

static void
sony_toc(sony_cdrom_t *d, int session)
{
    uint8_t      reply[512] = { 0 };
    track_info_t tracks[99];
    unsigned     first = 0, last = 0, n = 0, pos = 0;
    if (!d->toc_read) {
        sony_error(d, SONY_NO_TOC);
        return;
    }
    if (session && (d->param_count != 1 || d->params[0] != 1)) {
        sony_error(d, SONY_BAD_PARAM);
        return;
    }
    for (unsigned i = 1; i <= 99; ++i) {
        track_info_t t;
        if (d->cd->ops->get_track_info(d->cd->local, i, 0, &t) && t.number == i) {
            if (!first)
                first = i;
            last        = i;
            tracks[n++] = t;
        }
    }
    if (!n) {
        sony_error(d, SONY_NO_TOC);
        return;
    }
    if (session)
        reply[pos++] = 1;
    uint8_t ctl  = (tracks[0].attr << 4) | (tracks[0].attr >> 4);
    reply[pos++] = ctl;
    reply[pos++] = 0xa0;
    reply[pos++] = sony_bcd(first);
    reply[pos++] = 0;
    reply[pos++] = 0;
    reply[pos++] = ctl;
    reply[pos++] = 0xa1;
    reply[pos++] = sony_bcd(last);
    reply[pos++] = 0;
    reply[pos++] = 0;
    reply[pos++] = ctl;
    reply[pos++] = 0xa2;
    sony_msf(sony_capacity(d) + 150, reply + pos);
    pos += 3;
    for (unsigned i = 0; i < n; ++i) {
        reply[pos++] = (tracks[i].attr << 4) | (tracks[i].attr >> 4);
        reply[pos++] = sony_bcd(tracks[i].number);
        reply[pos++] = sony_bcd(tracks[i].m);
        reply[pos++] = sony_bcd(tracks[i].s);
        reply[pos++] = sony_bcd(tracks[i].f);
    }
    sony_reply(d, reply, pos);
}

static void
sony_command_done(void *priv)
{
    sony_cdrom_t *d         = priv;
    uint8_t       reply[40] = { 0 };
    uint32_t      start, end;
    d->status &= ~SONY_BUSY;
    if (d->param_overflow) {
        sony_error(d, SONY_BAD_PARAM);
        return;
    }
    switch (d->command) {
        case 0x00: /* Drive configuration. */
            memset(reply, ' ', 32);
            memcpy(reply, "SONY", 4);
            memcpy(reply + 8, d->double_speed ? "CD-ROM CDU33A" : "CD-ROM CDU31A", 13);
            memcpy(reply + 24, "1.0", 3);
            reply[32] = 0x8d | (d->double_speed ? 0x10 : 0); /* Tray, eject button, LED, 64K. */
            reply[33] = 7;                                   /* Audio, separate electronic volume. */
            sony_reply(d, reply, 34);
            break;
        case 0x02: /* Read one drive parameter. */
            if (d->param_count != 1 || d->params[0] > 6)
                sony_error(d, SONY_BAD_PARAM);
            else
                sony_reply(d, d->settings[d->params[0]], d->params[0] == 4 ? 2 : 1);
            break;
        case 0x03: /* Mechanical status. */
            reply[0] = (!d->tray_open ? 1 : 0) | (sony_ready(d) ? 2 : 0) | (d->spinning ? 8 : 0) | (d->toc_read ? 0x10 : 0);
            /* SLCD.SYS tests bit 4 of the second mechanical-status byte.
               Without it, a status poll discards its saved play/resume state. */
            reply[1] = d->audio_active ? 0x10 : 0;
            sony_reply(d, reply, 3);
            break;
        case 0x10:
            if (d->param_count < 2 || d->params[0] > 6 || d->param_count != (d->params[0] == 4 ? 3 : 2)) {
                sony_error(d, SONY_BAD_PARAM);
                break;
            }
            d->settings[d->params[0]][0] = d->params[1];
            if (d->params[0] == 4)
                d->settings[4][1] = d->params[2];
            if (d->cd && d->params[0] == 5)
                d->cd->cur_speed = d->double_speed && (d->params[1] & 4) ? 2 : 1;
            sony_reply(d, NULL, 0);
            break;
        case 0x35: /* Abort. */
            sony_reply(d, NULL, 0);
            break;
        case 0x50:
            if (d->param_count) {
                sony_error(d, SONY_BAD_PARAM);
                break;
            }
            cdrom_eject(d->cd->id);
            d->tray_open = 1;
            d->spinning = d->toc_read = d->audio_active = 0;
            sony_reply(d, NULL, 0);
            break;
        default:
            if (!sony_ready(d)) {
                sony_error(d, SONY_NO_DISC);
                break;
            }
            switch (d->command) {
                case 0x20:
                case 0x24:
                    sony_toc(d, d->command == 0x24);
                    break;
                case 0x21:
                    { /* Sub-Q address. */
                        uint32_t     pos   = d->cd->seek_pos;
                        track_info_t track = { 0 };
                        for (unsigned i = 1; i <= 99; ++i) {
                            track_info_t t;
                            if (!d->cd->ops->get_track_info(d->cd->local, i, 0, &t) || t.number != i)
                                break;
                            if (MSFtoLBA(t.m, t.s, t.f) > pos + 150)
                                break;
                            track = t;
                        }
                        reply[0] = (track.attr << 4) | (track.attr >> 4);
                        reply[1] = sony_bcd(track.number);
                        reply[2] = 1;
                        sony_msf(pos + 150 - MSFtoLBA(track.m, track.s, track.f), reply + 3);
                        sony_msf(pos + 150, reply + 7);
                        sony_reply(d, reply, 10);
                        break;
                    }
                case 0x30:
                case 0x36:
                    if (d->command == 0x36 && (d->param_count != 1 || d->params[0] != 1))
                        sony_error(d, SONY_BAD_PARAM);
                    else {
                        d->spinning = d->toc_read = 1;
                        sony_reply(d, NULL, 0);
                    }
                    break;
                case 0x31:
                case 0x32:
                case 0x34:
                    if (d->param_count != (d->command == 0x31 ? 3 : 6) || !sony_lba(d->params, &start)) {
                        sony_error(d, SONY_BAD_PARAM);
                        break;
                    }
                    d->remaining = d->command == 0x31 ? 0 : (d->params[3] << 16) | (d->params[4] << 8) | d->params[5];
                    if (start >= sony_capacity(d) || d->remaining > sony_capacity(d) - start) {
                        sony_error(d, 0x46);
                        break;
                    }
                    cdrom_stop(d->cd);
                    d->audio_active = 0;
                    d->spinning     = 1;
                    d->lba = d->cd->seek_pos = start;
                    if (!d->remaining) {
                        sony_reply(d, NULL, 0);
                        break;
                    }
                    d->reading = 1;
                    ui_sb_update_icon(SB_CDROM | d->cd->id, 1);
                    timer_set_delay_u64(&d->sector_timer, (1000000 / (75 * d->cd->cur_speed)) * TIMER_USEC);
                    break;
                case 0x40:
                    if (d->param_count != 7 || d->params[0] != 3 || !sony_lba(d->params + 1, &start) || !sony_lba(d->params + 4, &end) || end < start || end >= sony_capacity(d))
                        sony_error(d, SONY_BAD_PARAM);
                    /* Sony's last-frame address is inclusive; SLCD.SYS sends
                       start + length - 1 for an MSCDEX PLAY AUDIO request. */
                    else if (!cdrom_audio_play(d->cd, start, end - start + 1, 0))
                        sony_error(d, 0x93);
                    else {
                        d->spinning     = 1;
                        d->audio_active = 1;
                        sony_reply(d, NULL, 0);
                    }
                    break;
                case 0x41:
                    cdrom_audio_pause_resume(d->cd, 0);
                    d->audio_active = 0;
                    sony_reply(d, NULL, 0);
                    break;
                case 0x51:
                    d->spinning = 1;
                    if (d->settings[5][0] & 1)
                        d->toc_read = 1;
                    sony_reply(d, NULL, 0);
                    break;
                case 0x52:
                    cdrom_stop(d->cd);
                    d->audio_active = d->spinning = 0;
                    sony_reply(d, NULL, 0);
                    break;
                default:
                    sony_error(d, SONY_BAD_COMMAND);
                    break;
            }
            break;
    }
    d->param_count = 0;
    sony_irq(d);
}

static uint8_t
sony_in(uint16_t port, void *priv)
{
    sony_cdrom_t *d     = priv;
    uint8_t       value = 0xff;
    if (!d->cd)
        return 0xff;
    switch ((port - d->base) & 7) {
        case 0:
            if (d->audio_active && (d->cd->cd_status & CD_STATUS_MASK) == CD_STATUS_PLAYING_COMPLETED) {
                d->audio_active = 0;
                sony_attention(d, 0x90);
            }
            /* The interrupt control bits can be saved even with no pending event. */
            value = d->status | d->control | (d->data_pos < d->data_len ? 0x40 : 0);
            break;
        case 1:
            if (d->attention_count && !d->attention_read) {
                value             = d->attention[d->attention_head];
                d->attention_read = 1;
                sony_next_attention(d);
            } else if (d->result_pos < d->result_end) {
                value = d->results[d->result_pos++];
                if (d->result_pos == d->result_end) {
                    d->status &= ~SONY_RESULT;
                    if (d->result_pos < d->result_len) {
                        d->result_end = MIN(d->result_len, d->result_pos + 10);
                        d->status |= SONY_RESULT;
                    } else
                        sony_next_result(d);
                }
            }
            sony_irq(d);
            break;
        case 2:
            if (d->data_pos < d->data_len) {
                value = d->sector[d->data_pos++];
                if (d->data_pos == d->data_len)
                    sony_data_done(d);
            }
            break;
        case 3:
            value = 0xf0 | (d->param_count < sizeof(d->params) ? 1 : 0) | (!d->param_count ? 2 : 0) | (d->result_pos < d->result_end || (d->attention_count && !d->attention_read) ? 4 : 0) | (d->result_end - d->result_pos == 10 ? 8 : 0);
            break;
        default:
            break;
    }
    return value;
}

static void
sony_out(uint16_t port, uint8_t value, void *priv)
{
    sony_cdrom_t *d = priv;
    if (!d->cd)
        return;
    switch ((port - d->base) & 7) {
        case 0:
            if ((d->status & SONY_BUSY) && value != 0x35)
                return;
            sony_stop_read(d);
            d->command    = value;
            d->result_pos = d->result_len = d->result_end = 0;
            d->status                                     = (d->status & ~SONY_RESULT) | SONY_BUSY;
#ifdef ENABLE_SONY_CDROM_LOG
            char trace[3 * sizeof(d->params) + 1] = { 0 };
            for (unsigned i = 0; i < d->param_count; ++i)
                snprintf(trace + 3 * i, 4, " %02x", d->params[i]);
            sony_log("Sony: command %02x (%u):%s\n", value, d->param_count, trace);
#endif
            timer_set_delay_u64(&d->command_timer, 100 * TIMER_USEC);
            break;
        case 1:
            if (d->param_count < sizeof(d->params))
                d->params[d->param_count++] = value;
            else
                d->param_overflow = 1;
            break;
        case 3:
            if (value & 0x80) {
                sony_reset(d);
                break;
            }
            d->control = value & 0x38;
            if (value & 1) {
                d->status &= ~SONY_ATTN;
                if (d->attention_count) {
                    d->attention_ack = 1;
                    sony_next_attention(d);
                }
            }
            if (value & 2)
                d->status &= ~SONY_RESULT;
            if (value & 4)
                d->status &= ~SONY_DATA;
            if (value & 0x40)
                d->param_count = d->param_overflow = 0;
            break;
        default:
            break;
    }
    sony_irq(d);
}

static uint32_t
sony_volume(void *priv, int channel)
{
    sony_cdrom_t *d = priv;
    return d->settings[4][channel & 1];
}

static uint32_t
sony_channel(void *priv, int channel)
{
    sony_cdrom_t *d = priv;
    return (d->settings[3][0] >> ((channel & 1) * 2)) & 3;
}

static void
sony_insert(void *priv)
{
    sony_cdrom_t *d = priv;
    sony_stop_read(d);
    timer_disable(&d->command_timer);
    cdrom_stop(d->cd);
    d->cd->cd_buflen = 0;
    d->tray_open     = !sony_ready(d);
    d->status &= ~SONY_BUSY;
    d->param_count = d->param_overflow = 0;
    d->result_pos = d->result_len = d->result_end = 0;
    d->status &= ~SONY_RESULT;
    d->toc_read = d->spinning = d->audio_active = 0;
    sony_attention(d, sony_ready(d) ? 0x80 : 0x28);
    if (sony_ready(d) && (d->settings[5][0] & 1)) {
        /* Automatic spin-up also reads the TOC. NT 3.5 waits for the drive
           to become ready after loading, without issuing a spin-up command. */
        d->spinning = d->toc_read = 1;
        sony_attention(d, 0x24);
        sony_attention(d, 0x62);
    }
}

static void
sony_reset(void *priv)
{
    sony_cdrom_t *d = priv;
    sony_stop_read(d);
    timer_disable(&d->command_timer);
    if (d->cd)
        cdrom_stop(d->cd);
    d->control = d->status = 0;
    d->param_count = d->param_overflow = 0;
    d->result_pos = d->result_len = d->result_end = 0;
    d->attention_head = d->attention_count = d->attention_read = d->attention_ack = 0;
    d->toc_read = d->spinning = d->audio_active = 0;
    memset(d->settings, 0, sizeof(d->settings));
    d->settings[0][0] = 0x0f;
    d->settings[3][0] = 9;
    d->settings[4][0] = d->settings[4][1] = 255;
    if (d->cd) {
        d->cd->cur_speed = 1;
        sony_attention(d, 0x80);
    }
    sony_irq(d);
}

static void *
sony_init(const device_t *info)
{
    sony_cdrom_t *d = calloc(1, sizeof(*d));
    d->base         = device_get_config_hex16("base");
    d->irq          = info->local ? -1 : device_get_config_int("irq");
    d->dma          = info->local ? -1 : device_get_config_int("dma");
    if (d->irq < 3 || d->irq > 6)
        d->irq = -1;
    if (d->dma < 1 || d->dma > 3)
        d->dma = -1;
    timer_add(&d->command_timer, sony_command_done, d, 0);
    timer_add(&d->sector_timer, sony_sector, d, 0);
    timer_add(&d->dma_timer, sony_dma, d, 0);
    for (int i = 0; i < CDROM_NUM; ++i) {
        if (cdrom[i].bus_type == CDROM_BUS_SONY) {
            d->cd                = &cdrom[i];
            d->double_speed      = !strcmp(cdrom_get_internal_name(d->cd->type), "sony_cdu33a");
            d->cd->priv          = d;
            d->cd->insert        = sony_insert;
            d->cd->get_volume    = sony_volume;
            d->cd->get_channel   = sony_channel;
            d->cd->cached_sector = d->cd->subc_sector = -1;
            break;
        }
    }
    sony_reset(d);
    sony_log("Sony: init base=%03x irq=%d dma=%d drive=%p\n", d->base, d->irq, d->dma, (void *) d->cd);
    io_sethandler(d->base, 8, sony_in, NULL, NULL, sony_out, NULL, NULL, d);
    return d;
}

static void
sony_close(void *priv)
{
    sony_cdrom_t *d = priv;
    sony_reset(d);
    io_removehandler(d->base, 8, sony_in, NULL, NULL, sony_out, NULL, NULL, d);
    if (d->cd && d->cd->priv == d) {
        d->cd->priv        = NULL;
        d->cd->insert      = NULL;
        d->cd->get_volume  = NULL;
        d->cd->get_channel = NULL;
    }
    free(d);
}

static const device_config_t sony_config[] = {
    { .name = "base", .description = "Address", .type = CONFIG_HEX16, .default_int = 0x340, .selection = { { "320H", 0x320 }, { "330H", 0x330 }, { "340H", 0x340 }, { "360H", 0x360 }, { 0 } } },
    { .name = "irq", .description = "IRQ", .type = CONFIG_SELECTION, .default_int = -1, .selection = { { "Disabled", -1 }, { "3", 3 }, { "4", 4 }, { "5", 5 }, { "6", 6 }, { 0 } } },
    { .name = "dma", .description = "DMA", .type = CONFIG_SELECTION, .default_int = -1, .selection = { { "Disabled", -1 }, { "1", 1 }, { "2", 2 }, { "3", 3 }, { 0 } } },
    { .name = "", .type = CONFIG_END }
};

static const device_config_t sony_creative_config[] = {
    { .name = "base", .description = "Address", .type = CONFIG_HEX16, .default_int = 0x230, .selection = { { "230H", 0x230 }, { "250H", 0x250 }, { "270H", 0x270 }, { "290H", 0x290 }, { 0 } } },
    { .name = "", .type = CONFIG_END }
};

const device_t sony_cdu31a_device = {
    .name = "SONY CDU31A interface", .internal_name = "sony_cdu31a", .flags = DEVICE_ISA, .init = sony_init, .close = sony_close, .reset = sony_reset, .config = sony_config
};

const device_t sony_creative_device = {
    .name = "SONY/Creative interface", .internal_name = "sony_creative", .flags = DEVICE_ISA, .local = 1, .init = sony_init, .close = sony_close, .reset = sony_reset, .config = sony_creative_config
};
