/*
 * 86Box: Hitachi CD-IFI4-A ISA interface / CDR-1503S.
 *
 * Initial high-level PIO implementation, reconstructed from Hitachi's
 * HITACHI.SYS 1.02 and HITACHIA.SYS 2.10. No drive firmware is executed.
 * The interface uses an 8255 in mode 0 plus a separate sector-data port.
 * DMA, diagnostic packets and audio playback are not implemented yet.
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
#include <86box/cdrom.h>
#include <86box/cdrom_hitachi.h>
#include <86box/timer.h>

typedef struct hitachi_drive_t {
    cdrom_t   *cd;
    pc_timer_t timer;
    uint32_t   lba;
    uint8_t    data[2340];
    unsigned   data_pos, data_len;
    uint8_t    reply[192];
    unsigned   reply_pos, reply_len;
    uint8_t    packet[16];
    unsigned   packet_pos;
    uint8_t    reading, error, changed, locked;
    int        toc;
} hitachi_drive_t;

typedef struct hitachi_t {
    uint16_t        base;
    uint8_t         mode, control, latch;
    hitachi_drive_t drives[4];
} hitachi_t;

#ifdef ENABLE_HITACHI_LOG
#    define hitachi_log pclog
#else
#    define hitachi_log(...) \
        do {                 \
        } while (0)
#endif

static int
hitachi_ready(const hitachi_drive_t *d)
{
    return d->cd && d->cd->ops && ((d->cd->cd_status & CD_STATUS_MASK) != CD_STATUS_EMPTY) && ((d->cd->cd_status & CD_STATUS_MASK) != CD_STATUS_DVD_REJECTED);
}

static void
hitachi_stop(hitachi_drive_t *d)
{
    timer_disable(&d->timer);
    d->reading  = 0;
    d->data_pos = d->data_len = 0;
}

static uint32_t
hitachi_sector_count(const hitachi_drive_t *d)
{
    track_info_t leadout;

    /* The image backend's cached capacity includes the 150-frame pregap.
       Obtain the exclusive logical end from the absolute lead-out MSF. */
    if (!hitachi_ready(d) || !d->cd->ops->get_track_info || !d->cd->ops->get_track_info(d->cd->local, 0xa2, 0, &leadout) || leadout.number != 0xa2)
        return 0;
    uint32_t frames = ((uint32_t) leadout.m * 60 + leadout.s) * 75 + leadout.f;
    return frames >= 150 ? frames - 150 : 0;
}

static void
hitachi_read_sector(void *priv)
{
    hitachi_drive_t *d   = priv;
    int              len = 0;

    if (!d->reading)
        return;
    if (!hitachi_ready(d) || d->lba >= hitachi_sector_count(d) || cdrom_readsector_raw(d->cd, d->data, d->lba, 0, 2, 0x78, &len, 0) <= 0 || len != sizeof(d->data)) {
        hitachi_stop(d);
        d->error = 0x02;
        return;
    }
    d->cd->seek_pos = d->lba;
    d->data_pos     = 0;
    d->data_len     = len;
}

static void
hitachi_insert(void *priv)
{
    hitachi_drive_t *d = priv;
    hitachi_stop(d);
    d->packet_pos = d->reply_pos = d->reply_len = 0;
    d->error                                    = 0;
    d->changed                                  = 1;
    d->toc                                      = 0;
}

static int
hitachi_msf(const uint8_t *p, uint32_t *lba)
{
    unsigned msf[3];
    for (unsigned i = 0; i < 3; ++i) {
        if ((p[i] & 15) > 9 || (p[i] >> 4) > 9)
            return 0;
        msf[i] = (p[i] >> 4) * 10 + (p[i] & 15);
    }
    if (msf[1] >= 60 || msf[2] >= 75)
        return 0;
    unsigned frame = (msf[0] * 60 + msf[1]) * 75 + msf[2];
    if (frame < 150)
        return 0;
    *lba = frame - 150;
    return 1;
}

static void
hitachi_execute(hitachi_drive_t *d)
{
    const uint8_t *p = d->packet;
    uint32_t       lba;
    hitachi_log("Hitachi command %02x %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3], p[4]);
    d->reply_pos = d->reply_len = 0;
    memset(d->reply, 0, sizeof(d->reply));

    switch (p[0]) {
        case 0x60: /* Device/media state; the driver caches the change indication. */
            d->reply[0] = hitachi_ready(d) ? 0x04 : 0x80;
            if (d->changed) {
                d->reply[0] |= 0x20;
                d->changed = 0;
            }
            d->reply_len = 1;
            break;
        case 0x70: /* Operation state: complete, error, sector ready. */
            d->reply[0]  = d->error ? d->error : d->data_len ? 0x04
                : d->reading                                 ? 0
                                                             : 1;
            d->reply_len = 1;
            break;
        case 0xa0:
        case 0xa8:
        case 0xa9:
            d->reply[0]  = d->locked;
            d->reply_len = 1;
            break;
        case 0x50:
            if (hitachi_ready(d) && d->cd->ops->get_raw_track_info) {
                d->toc       = cdrom_get_q(d->cd, d->reply, d->toc, 1);
                d->reply_len = 10;
            } else {
                memset(d->reply, 0xff, 10);
                d->reply_len = 10;
                d->error     = 0x02;
            }
            break;
        case 0xff:
            d->error = 0;
            switch (p[1]) {
                case 0x00:
                    hitachi_stop(d);
                    d->locked = 0;
                    d->toc    = 0;
                    break;
                case 0x18:
                    hitachi_stop(d);
                    break;
                case 0x10: /* Seek */
                case 0x22: /* Read from BCD MSF until stopped */
                    hitachi_stop(d);
                    if (!hitachi_ready(d) || !hitachi_msf(p + 2, &lba) || lba >= hitachi_sector_count(d)) {
                        d->error = 0x02;
                        break;
                    }
                    d->lba = lba;
                    cdrom_stop(d->cd);
                    cdrom_seek(d->cd, lba, 0);
                    if (p[1] == 0x22) {
                        d->reading = 1;
                        timer_on_auto(&d->timer, 1000000.0 / 75.0);
                    }
                    break;
                case 0x90: /* Power-save interval, accepted by CDRD8A during init. */
                    break;
                case 0xc0: /* Seek lead-in for the driver's TOC scan. */
                    hitachi_stop(d);
                    d->toc = 0;
                    if (!hitachi_ready(d))
                        d->error = 0x02;
                    break;
                case 0x30:
                    if (p[2] == 0x80 || p[2] == 0x81) {
                        d->locked = p[2] & 1;
                    } else if (p[2] == 0x90 && p[3] == 0x90 && p[4] == 0x82) {
                        /* CDRD8A function 7: volume length is a big-endian
                           seconds count plus frames at offsets 15..17. */
                        d->reply_len = 192;
                        if (hitachi_ready(d)) {
                            unsigned sectors = hitachi_sector_count(d);
                            d->reply[15]     = (sectors / 75) >> 8;
                            d->reply[16]     = sectors / 75;
                            d->reply[17]     = sectors % 75;
                        } else
                            d->error = 0x02;
                    } else {
                        /* Keep known response lengths on unsupported diagnostics. */
                        d->reply_len = p[4] == 0x85 ? 52 : p[4] == 0x83 ? 30
                                                                        : 0;
                        memset(d->reply, 0xff, d->reply_len);
                        d->error = 0x40;
                    }
                    break;
                default:
                    d->error = 0x40;
                    break;
            }
            break;
        default:
            d->error = 0x40;
            break;
    }
}

static void
hitachi_command_byte(hitachi_drive_t *d, uint8_t val)
{
    unsigned len = 1;
    if (!d->packet_pos)
        memset(d->packet, 0, sizeof(d->packet));
    d->packet[d->packet_pos++] = val;
    if (d->packet[0] == 0xff) {
        if (d->packet_pos < 2)
            return;
        switch (d->packet[1]) {
            case 0x10:
            case 0x22:
                len = 5;
                break;
            case 0x30:
                if (d->packet_pos < 3)
                    return;
                len = d->packet[2] == 0x90 ? 5 : 3;
                break;
            case 0x90:
                len = 3;
                break;
            default:
                len = (d->packet[1] & 0xfc) == 0xe0 ? 8 : (d->packet[1] & 0xfc) == 0xe8 ? 4
                                                                                        : 2;
                break;
        }
    }
    if (d->packet_pos == len) {
        hitachi_execute(d);
        d->packet_pos = 0;
    }
}

static hitachi_drive_t *
hitachi_selected(hitachi_t *h)
{
    hitachi_drive_t *d = &h->drives[(h->control >> 3) & 3];
    return d->cd ? d : NULL;
}

static uint8_t
hitachi_in(uint16_t port, void *priv)
{
    hitachi_t       *h = priv;
    hitachi_drive_t *d = hitachi_selected(h);
    if (!d)
        return 0xff;
    switch (port - h->base) {
        case 0:
            if (!(h->mode & 0x10))
                return h->latch;
            return d->reply_pos < d->reply_len ? d->reply[d->reply_pos++] : 0xff;
        case 1:
            return ((h->control & 1) ? 2 : 0) | ((d->data_len && !(h->control & 4)) ? 1 : 0);
        case 2:
            return h->control;
        case 4:
            return d->data_pos < d->data_len ? d->data[d->data_pos++] : 0xff;
        default:
            return 0xff;
    }
}

static void
hitachi_out(uint16_t port, uint8_t val, void *priv)
{
    hitachi_t *h   = priv;
    uint8_t    old = h->control;
    switch (port - h->base) {
        case 0:
            h->latch = val;
            break;
        case 2:
            {
                h->control         = val;
                hitachi_drive_t *d = hitachi_selected(h);
                if (!d)
                    break;
                if (!(h->mode & 0x10) && (val & 1) && !(old & 1))
                    hitachi_command_byte(d, h->latch);
                /* Acknowledge the consumed sector, discarding ECC after a cooked
                   read. A command handshake before any data is read must not
                   advance the sector. Never overwrite unread sector data. */
                if ((val & 4) && !(old & 4) && d->data_pos) {
                    d->data_pos = d->data_len = 0;
                    if (d->reading) {
                        ++d->lba;
                        timer_on_auto(&d->timer, 1000000.0 / 75.0);
                    }
                }
                break;
            }
        case 3:
            if (val & 0x80)
                h->mode = val;
            else { /* 8255 bit set/reset affects port C. */
                uint8_t bit = 1 << ((val >> 1) & 7);
                hitachi_out(h->base + 2, (h->control & ~bit) | ((val & 1) ? bit : 0), h);
            }
            break;
        default:
            break;
    }
}

static void
hitachi_reset(void *priv)
{
    hitachi_t *h = priv;
    h->mode      = 0x92;
    h->control   = 0x82;
    h->latch     = 0;
    for (unsigned i = 0; i < 4; ++i) {
        hitachi_drive_t *d = &h->drives[i];
        hitachi_stop(d);
        d->packet_pos = d->reply_pos = d->reply_len = 0;
        d->error = d->locked = d->toc = 0;
    }
}

static void *
hitachi_init(UNUSED(const device_t *info))
{
    hitachi_t *h = calloc(1, sizeof(*h));
    h->base      = device_get_config_hex16("base");
    for (unsigned i = 0; i < 4; ++i)
        timer_add(&h->drives[i].timer, hitachi_read_sector, &h->drives[i], 0);
    for (unsigned i = 0; i < CDROM_NUM; ++i) {
        cdrom_t *cd = &cdrom[i];
        if (cd->bus_type != CDROM_BUS_HITACHI || cd->hitachi_channel >= 4)
            continue;
        hitachi_drive_t *d = &h->drives[cd->hitachi_channel];
        if (d->cd)
            continue;
        d->cd             = cd;
        cd->priv          = d;
        cd->insert        = hitachi_insert;
        cd->cached_sector = cd->subc_sector = -1;
        cd->cur_speed                       = 1;
    }
    hitachi_reset(h);
    io_sethandler(h->base, 16, hitachi_in, NULL, NULL, hitachi_out, NULL, NULL, h);
    return h;
}

static void
hitachi_close(void *priv)
{
    hitachi_t *h = priv;
    if (!h)
        return;
    hitachi_reset(h);
    io_removehandler(h->base, 16, hitachi_in, NULL, NULL, hitachi_out, NULL, NULL, h);
    for (unsigned i = 0; i < 4; ++i) {
        cdrom_t *cd = h->drives[i].cd;
        if (cd && cd->priv == &h->drives[i]) {
            cd->priv   = NULL;
            cd->insert = NULL;
        }
    }
    free(h);
}

static const device_config_t hitachi_config[] = {
    { .name = "base", .description = "Address", .type = CONFIG_HEX16, .default_int = 0x300, .selection = { { .description = "200H", .value = 0x200 }, { .description = "220H", .value = 0x220 }, { .description = "240H", .value = 0x240 }, { .description = "260H", .value = 0x260 }, { .description = "300H", .value = 0x300 }, { .description = "320H", .value = 0x320 }, { .description = "340H", .value = 0x340 }, { .description = "360H", .value = 0x360 }, { 0 } } },
    { .name = "", .type = CONFIG_END }
};

const device_t hitachi_cdrom_device = {
    .name          = "Hitachi CD-IFI4-A",
    .internal_name = "hitachi_ifi4a",
    .flags         = DEVICE_ISA,
    .init          = hitachi_init,
    .close         = hitachi_close,
    .reset         = hitachi_reset,
    .config        = hitachi_config
};
