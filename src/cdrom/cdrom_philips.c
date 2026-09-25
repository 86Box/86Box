/*
 * 86Box: Philips/LMS CM250 ISA adapter and original (non-MS) CM205.
 *
 * Host-visible protocol reference: Kai Petzke's Linux cm205 driver,
 * with updates by Martin Seine (GPL-2.0-or-later), and CM250.MSC 1.00.
 * This behavioral implementation does not execute drive firmware.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/plat.h>
#include <86box/pic.h>
#include <86box/cdrom.h>
#include <86box/cdrom_philips.h>
#include <86box/timer.h>

#ifdef ENABLE_PHILIPS_CDROM_LOG
#    define cm_log pclog
#else
#    define cm_log(...) \
        do {            \
        } while (0)
#endif

typedef struct cm250_t {
    cdrom_t   *cd;
    uint16_t   base;
    int        irq;
    pc_timer_t command_timer, sector_timer;
    uint8_t    control, line, data_status, echo, pending_echo;
    uint8_t    packet[16], response[32];
    unsigned   packet_pos, packet_len, response_pos, response_len;
    uint8_t    drive_error, adapter_error, changed;
    uint8_t    reading, toc_reply, stream_valid;
    uint32_t   lba, remaining;
    uint8_t    sector[2352];
    unsigned   data_pos, data_len;
} cm250_t;

static int
cm_ready(const cm250_t *d)
{
    return d->cd && d->cd->ops && ((d->cd->cd_status & CD_STATUS_MASK) != CD_STATUS_EMPTY) && ((d->cd->cd_status & CD_STATUS_MASK) != CD_STATUS_DVD_REJECTED);
}

static uint8_t
bcd(unsigned n)
{
    return ((n / 10) << 4) | (n % 10);
}

/* Original CM205 has no multisession support. Use only the first session's
   track entries and lead-out, even when the image backend knows later ones. */
static uint32_t
cm_disc_info(const cm250_t *d, uint8_t *first, uint8_t *last)
{
    track_info_t t;
    *first = *last = 1;
    if (cm_ready(d) && d->cd->ops->get_raw_track_info) {
        uint8_t  raw[65536] = { 0 };
        int      num        = 0;
        unsigned session = 256, f = 100, l = 0;
        uint32_t end = 0;
        d->cd->ops->get_raw_track_info(d->cd->local, &num, raw);
        const raw_track_info_t *r = (const raw_track_info_t *) raw;
        if (num < 0 || (unsigned) num > sizeof(raw) / sizeof(*r))
            return 0;
        for (int i = 0; i < num; ++i)
            if (r[i].session && r[i].session < session)
                session = r[i].session;
        for (int i = 0; i < num; ++i) {
            if (r[i].session != session)
                continue;
            if (r[i].point >= 1 && r[i].point <= 99) {
                if (r[i].point < f)
                    f = r[i].point;
                if (r[i].point > l)
                    l = r[i].point;
            } else if (r[i].point == 0xa2) {
                end = ((uint32_t) r[i].pm * 60 + r[i].ps) * 75 + r[i].pf;
            }
        }
        if (l && end >= 150) {
            *first = f;
            *last  = l;
            return end - 150;
        }
    }
    if (!cm_ready(d) || !d->cd->ops->get_track_info || !d->cd->ops->get_track_info(d->cd->local, 0xa2, 0, &t) || t.number != 0xa2)
        return 0;
    uint32_t f = ((uint32_t) t.m * 60 + t.s) * 75 + t.f;
    return f >= 150 ? f - 150 : 0;
}

static uint32_t
cm_capacity(const cm250_t *d)
{
    uint8_t first, last;
    return cm_disc_info(d, &first, &last);
}

static void
cm_fsm(uint32_t lba, uint8_t *p)
{
    lba += 150;
    p[0] = bcd(lba % 75);
    p[1] = bcd((lba / 75) % 60);
    p[2] = bcd(lba / 4500);
}

static int
cm_decode(const uint8_t *p, uint32_t *lba)
{
    unsigned v[3];
    for (int i = 0; i < 3; i++) {
        if ((p[i] & 15) > 9 || (p[i] >> 4) > 9)
            return 0;
        v[i] = (p[i] >> 4) * 10 + (p[i] & 15);
    }
    if (v[0] >= 75 || v[1] >= 60)
        return 0;
    unsigned f = (v[2] * 60 + v[1]) * 75 + v[0];
    if (f < 150)
        return 0;
    *lba = f - 150;
    return 1;
}

static void
cm_stop(cm250_t *d)
{
    timer_disable(&d->sector_timer);
    d->reading      = 0;
    d->stream_valid = 0;
    d->remaining    = 0;
    d->data_len = d->data_pos = 0;
    d->data_status            = 0;
    d->line |= 0x40;
}

static void
cm_command_done(void *priv)
{
    cm250_t *d = priv;
    d->echo    = d->pending_echo;
    d->line |= d->toc_reply ? 0x81 : 3;
    d->toc_reply = 0;
    picint(1 << d->irq);
}

static void
cm_sector(void *priv)
{
    cm250_t *d   = priv;
    int      len = 0;
    if (!d->reading || d->data_len)
        return;
    if (!cm_ready(d) || d->lba >= cm_capacity(d) || cdrom_readsector_raw(d->cd, d->sector, d->lba, 0, 2, 0xf8, &len, 0) <= 0 || len != 2352) {
        cm_stop(d);
        d->drive_error = cm_ready(d) ? 0x16 : 0x1e;
        d->line |= 0x10;
    } else {
        d->cd->seek_pos = d->lba;
        d->data_len     = len;
        d->data_pos     = 0;
        d->data_status |= 0x10;
    }
    picint(1 << d->irq);
}

static void
cm_execute(cm250_t *d)
{
    const uint8_t *p = d->packet;
    uint32_t       lba;
    cm_log("CM205 command %02x (%u): %02x %02x %02x %02x %02x %02x\n",
           p[0], d->packet_pos, p[1], p[2], p[3], p[4], p[5], p[6]);
    d->response_len = d->response_pos = 0;
    memset(d->response, 0, sizeof(d->response));
    switch (p[0]) {
        case 0x2d: /* Original CM205 identity, read one byte per 9Ch. */
            d->response[0]  = 5;
            d->response[1]  = 4;
            d->response[4]  = 10;
            d->response_len = 12;
            break;
        case 0x3a:
            { /* Position, state, errors, end of disc, track range. */
                uint8_t  first, last;
                uint32_t capacity = cm_disc_info(d, &first, &last);
                cm_fsm(d->lba, d->response);
                d->response[5] = cm_ready(d) ? 0 : 0x08;
                if (d->changed)
                    d->response[5] |= 0x10;
                d->response[6] = d->drive_error;
                d->response[7] = d->adapter_error;
                cm_fsm(capacity ? capacity - 1 : 0, d->response + 8);
                d->response[11] = bcd(first);
                d->response[12] = bcd(last);
                d->response_len = 13;
                break;
            }
        case 0x4e: /* Clear latched error. */
            d->drive_error = d->adapter_error = 0;
            d->changed                        = 0;
            d->line &= ~0x10;
            break;
        case 0xe5:
            { /* CM205 TOC range, returned through the adapter data FIFO. */
                cm_stop(d);
                unsigned first = (p[1] >> 4) * 10 + (p[1] & 15);
                unsigned last  = (p[2] >> 4) * 10 + (p[2] & 15);
                uint8_t  disc_first, disc_last;
                cm_disc_info(d, &disc_first, &disc_last);
                if (!cm_ready(d) || (p[1] & 15) > 9 || (p[2] & 15) > 9 || first < disc_first || last < first || last > disc_last) {
                    d->adapter_error = 4;
                    break;
                }
                d->data_len  = 1;
                d->sector[0] = 0;
                for (unsigned track = first; track <= last; track++) {
                    track_info_t t;
                    if (!d->cd->ops->get_track_info || !d->cd->ops->get_track_info(d->cd->local, track, 0, &t) || t.number != track) {
                        d->data_len      = 0;
                        d->adapter_error = 4;
                        break;
                    }
                    uint8_t *q = d->sector + d->data_len;
                    q[0]       = bcd(track);
                    /* Unlike command addresses, FIFO TOC positions are binary. */
                    q[1] = t.f;
                    q[2] = t.s;
                    q[3] = t.m;
                    q[4] = (t.attr & 0x0f) << 4;
                    d->data_len += 5;
                }
                if (d->adapter_error)
                    break;
                d->sector[d->data_len++] = 0;
                d->line &= ~0x40;
                d->toc_reply = 1;
                break;
            }
        case 0x59: /* Seek; over-end seek is also the original driver's stop. */
            cm_stop(d);
            if (!cm_ready(d))
                d->drive_error = 0x1e;
            else if (!cm_decode(p + 1, &lba) || lba >= cm_capacity(d)) {
                d->lba         = cm_capacity(d) ? cm_capacity(d) - 1 : 0;
                d->drive_error = 0x16;
            } else {
                d->lba = lba;
                cdrom_seek(d->cd, lba, 0);
            }
            break;
        case 0xa6: /* Read: BCD FSM, three BCD pairs of sector count. */
            cm_stop(d);
            if (!cm_ready(d)) {
                d->drive_error = 0x1e;
                break;
            }
            if (!cm_decode(p + 1, &lba) || lba >= cm_capacity(d)) {
                d->adapter_error = 4;
                break;
            }
            d->remaining = 0;
            for (int i = 6; i >= 4; --i) {
                if ((p[i] & 15) > 9 || (p[i] >> 4) > 9) {
                    d->adapter_error = 4;
                    break;
                }
                d->remaining = d->remaining * 100 + (p[i] >> 4) * 10 + (p[i] & 15);
            }
            if (!d->adapter_error && d->remaining) {
                d->lba          = lba;
                d->reading      = 1;
                d->stream_valid = 1;
                d->line &= ~0x40;
                cdrom_stop(d->cd);
                cdrom_seek(d->cd, lba, 0);
                timer_on_auto(&d->sector_timer, 1000000.0 / 75.0);
            }
            break;
        case 0xa7:
            { /* Extend an active read by a BCD sector count. */
                if (!cm_ready(d) || !d->stream_valid) {
                    d->adapter_error = 4;
                    break;
                }
                uint32_t count = 0;
                for (int i = 3; i >= 1; --i) {
                    if ((p[i] & 15) > 9 || (p[i] >> 4) > 9) {
                        d->adapter_error = 4;
                        break;
                    }
                    count = count * 100 + (p[i] >> 4) * 10 + (p[i] & 15);
                }
                if (count > UINT32_MAX - d->remaining)
                    d->adapter_error = 4;
                if (!d->adapter_error && count) {
                    d->remaining += count;
                    d->reading = 1;
                    d->line &= ~0x40;
                    if (!d->data_len && !(d->sector_timer.flags & TIMER_ENABLED))
                        timer_on_auto(&d->sector_timer, 1000000.0 / 75.0);
                }
                break;
            }
        default:
            d->adapter_error = 1;
            break;
    }
    if (d->drive_error || d->adapter_error)
        d->line |= 0x10;
}

static void
cm_byte(cm250_t *d, uint8_t v)
{
    picintc(1 << d->irq);
    d->line &= ~3;
    d->pending_echo = v;
    if (!d->packet_pos && v == 0x9c) {
        d->pending_echo = d->response_pos < d->response_len ? d->response[d->response_pos++] : 0;
        cm_log("CM205 response[%u] = %02x\n", d->response_pos, d->pending_echo);
    } else {
        if (!d->packet_pos) {
            memset(d->packet, 0, sizeof(d->packet));
            /* Consume known unsupported audio packets as a whole, so their
               parameters cannot be mistaken for data commands. */
            switch (v) {
                case 0xb1:
                    d->packet_len = 8;
                    break;
                case 0xa6:
                    d->packet_len = 7;
                    break;
                case 0x59:
                case 0xa7:
                    d->packet_len = 4;
                    break;
                case 0xe5:
                    d->packet_len = 3;
                    break;
                case 0xc5:
                    d->packet_len = 2;
                    break;
                default:
                    d->packet_len = 1;
                    break;
            }
        }
        d->packet[d->packet_pos++] = v;
        if (d->packet_pos == d->packet_len) {
            cm_execute(d);
            d->packet_pos = 0;
        }
    }
    timer_on_auto(&d->command_timer, 50.0);
}

static uint8_t
cm_in(uint16_t port, void *priv)
{
    cm250_t *d = priv;
    uint8_t  v = 0xff;
    switch (port - d->base) {
        case 0:
            v = d->data_status;
            d->data_status &= ~0x10;
            picintc(1 << d->irq);
            break;
        case 1:
            v = d->echo;
            d->line &= ~2;
            picintc(1 << d->irq);
            break;
        case 2:
            if (d->data_pos < d->data_len) {
                v = d->sector[d->data_pos++];
                if (d->data_pos == d->data_len) {
                    d->data_pos = d->data_len = 0;
                    d->data_status &= ~0x10;
                    if (d->reading) {
                        ++d->lba;
                        if (d->remaining)
                            --d->remaining;
                    }
                    if (d->reading && d->remaining)
                        timer_on_auto(&d->sector_timer, 1000000.0 / 75.0);
                    else {
                        d->reading = 0;
                        d->line |= 0x40;
                    }
                }
            }
            break;
        case 3:
            v = d->line;
            d->line &= ~0x80;
            picintc(1 << d->irq);
            break;
    }
    return v;
}

static void
cm_out(uint16_t port, uint8_t v, void *priv)
{
    cm250_t *d = priv;
    if (port == d->base + 5) {
        cm_byte(d, v);
    } else if (port == d->base + 4) {
        uint8_t old = d->control;
        d->control  = v;
        cm_log("CM250 control %02x line %02x\n", v, d->line);
        picintc(1 << d->irq);
        if (v & 0x40) {
            cm_stop(d);
            timer_disable(&d->command_timer);
            d->packet_pos = d->response_len = d->response_pos = 0;
            d->toc_reply                                      = 0;
            d->line                                           = 0x41 | ((d->drive_error || d->adapter_error) ? 0x10 : 0);
            picint(1 << d->irq);
        }
        if ((old & 0x80) && !(v & 0x80)) {
            cm_stop(d);
            timer_disable(&d->command_timer);
            d->packet_pos = d->response_len = d->response_pos = 0;
            d->toc_reply                                      = 0;
            d->drive_error                                    = 0x0e;
            d->adapter_error                                  = 0;
            d->line                                           = 0x51;
        }
        if (!(v & 0x20) && (d->line & 1))
            picint(1 << d->irq);
    }
}

static void
cm_insert(void *priv)
{
    cm250_t *d = priv;
    cm_stop(d);
    timer_disable(&d->command_timer);
    picintc(1 << d->irq);
    d->packet_pos = d->response_pos = d->response_len = 0;
    d->toc_reply                                      = 0;
    d->line                                           = 0x41;
    d->drive_error = d->adapter_error = 0;
    d->changed                        = 1;
}

static void
cm_reset(void *priv)
{
    cm250_t *d = priv;
    cm_stop(d);
    timer_disable(&d->command_timer);
    picintc(1 << d->irq);
    d->control     = 0x21;
    d->line        = 0x41;
    d->drive_error = d->adapter_error = 0;
    d->packet_pos = d->response_pos = d->response_len = 0;
    d->changed = d->toc_reply = 0;
}

static void *
cm_init(UNUSED(const device_t *info))
{
    cm250_t *d = calloc(1, sizeof(*d));
    d->base    = device_get_config_hex16("base");
    d->irq     = device_get_config_int("irq");
    timer_add(&d->command_timer, cm_command_done, d, 0);
    timer_add(&d->sector_timer, cm_sector, d, 0);
    for (int i = 0; i < CDROM_NUM; i++) {
        if (cdrom[i].bus_type == CDROM_BUS_PHILIPS) {
            d->cd                = &cdrom[i];
            d->cd->priv          = d;
            d->cd->insert        = cm_insert;
            d->cd->cached_sector = d->cd->subc_sector = -1;
            d->cd->cur_speed                          = 1;
            break;
        }
    }
    cm_reset(d);
    io_sethandler(d->base, 8, cm_in, NULL, NULL, cm_out, NULL, NULL, d);
    return d;
}

static void
cm_close(void *priv)
{
    cm250_t *d = priv;
    cm_reset(d);
    io_removehandler(d->base, 8, cm_in, NULL, NULL, cm_out, NULL, NULL, d);
    if (d->cd && d->cd->priv == d) {
        d->cd->priv   = NULL;
        d->cd->insert = NULL;
    }
    free(d);
}

static const device_config_t cm250_config[] = {
    { .name = "base", .description = "Address", .type = CONFIG_HEX16, .default_int = 0x340, .selection = { { "300H", 0x300 }, { "310H", 0x310 }, { "330H", 0x330 }, { "340H", 0x340 }, { 0 } } },
    { .name = "irq", .description = "IRQ", .type = CONFIG_SELECTION, .default_int = 5, .selection = { { "3", 3 }, { "4", 4 }, { "5", 5 }, { "6", 6 }, { 0 } } },
    { .name = "", .type = CONFIG_END }
};

const device_t philips_cm250_device = {
    .name          = "Philips/LMS CM250",
    .internal_name = "philips_cm250",
    .flags         = DEVICE_ISA,
    .init          = cm_init,
    .close         = cm_close,
    .reset         = cm_reset,
    .config        = cm250_config
};
