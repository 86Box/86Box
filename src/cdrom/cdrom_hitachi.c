/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Hitachi CD-ROM interfaces: the ISA adapter CD-IFI4-A for the
 *          CDR-1503S drive, and the Micro Channel adapter carrying Adapter
 *          ID 5EEEh.
 *
 *          Both are programmed I/O only: an 8255 in mode 0 in front of a
 *          separate sector-data port. The ISA card keeps its register block
 *          at a jumpered base，and the MCA card answers behind an eight-port
 *          window the POS registers place, its base in POS 2, and puts the
 *          sector stream on its data port, the control lines on the status
 *          port and the command and reply bytes on the register between
 *          them.
 *
 *          Reconstructed from Hitachi's HITACHI.SYS 1.02, HITACHIA.SYS 2.10
 *          and HITACHIB.SYS 2.10, and from the Adapter Description File
 *          @5eee.adf. No drive firmware is executed. DMA, diagnostic
 *          packets and audio playback are not implemented yet.
 *
 * Authors: heavysink, <winstonwu91@gmail.com>
 *          WNT50
 *
 *          Copyright 2026 heavysink.
 *          Copyright 2026 WNT50.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/plat.h>
#include <86box/cdrom.h>
#include <86box/cdrom_hitachi.h>
#include <86box/timer.h>

/* MCA variant (POS adapter ID 5EEEh): the same drive-side engine behind a
   POS-addressed eight-port window, its control, status, command and data
   ports laid out differently from the ISA card's. */
#define HITACHI_MCA_ID_HIGH  0x5e
#define HITACHI_MCA_ID_LOW   0xee
#define HITACHI_MCA_WINDOW   8
#define HITACHI_MCA_BASES    16

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
    uint8_t         mca_enabled;
    uint8_t         pos_regs[8];
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

/* The sector stream as the host takes it, four-byte header first. Both card
   variants feed the same buffer, only through different ports. */
static uint8_t
hitachi_data_byte(hitachi_t *h)
{
    hitachi_drive_t *d = hitachi_selected(h);

    return (d && (d->data_pos < d->data_len)) ? d->data[d->data_pos++] : 0xff;
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
        case 4: /* The sector stream, four-byte header first. */
            return hitachi_data_byte(h);
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
hitachi_isa_init(UNUSED(const device_t *info))
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
hitachi_isa_close(void *priv)
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

/* MCA variant (POS adapter ID 5EEEh): the same drive-side engine behind a
   POS-addressed eight-port window, with the sector stream on its data port. */

static const uint16_t hitachi_mca_bases[HITACHI_MCA_BASES] = {
    0x200, 0x220, 0x240, 0x260, 0x300, 0x320, 0x340, 0x360,
    0x208, 0x228, 0x248, 0x268, 0x308, 0x328, 0x348, 0x368
};

static uint8_t
hitachi_mca_in(uint16_t port, void *priv)
{
    hitachi_t *h   = priv;
    uint16_t   off = (uint16_t) (port - h->base);
    uint8_t    ret;

    /* The MCA card moves the sector stream on +0 and keeps the replies on +2
       (also where the driver writes its command bytes), with the status on
       +1. */
    switch (off) {
        case 0: /* Sector bytes, packet header included, come back here. */
            ret = hitachi_data_byte(h);
            break;

        case 1:
            ret = hitachi_in(h->base + 1, h);
            break;

        case 2: /* Command bytes go out here; replies come back first, then the
                   sector stream once a read has been loaded. */
            ret = hitachi_in(h->base, h);
            if (ret == 0xff)
                ret = hitachi_data_byte(h);
            break;

        default:
            ret = 0xff;
            break;
    }

    return ret;
}

static void
hitachi_mca_out(uint16_t port, uint8_t val, void *priv)
{
    hitachi_t *h   = priv;
    uint16_t   off = (uint16_t) (port - h->base);

    switch (off) {
        case 1: {
            /* Control: bit 0 strobes, bit 1 acknowledges, bit 2 says which way
               the data lines point. The drive-side engine wants the strobe on
               bit 0 and the acknowledge on bit 2, and may only take a command
               byte while the host is the one driving them. */
            uint8_t isa = (uint8_t) ((val & 0x01) | ((val & 0x02) << 1) | (val & 0x18));

            if (val & 0x04)
                h->mode &= ~0x10;
            else
                h->mode |= 0x10;
            hitachi_out(h->base + 2, isa, h);
            h->mode |= 0x10;
            break;
        }

        case 2: /* Command and parameter bytes; the latch holds them until the
                   next strobe. */
            hitachi_out(h->base, val, h);
            break;

        default:
            break;
    }
}

static void
hitachi_mca_window(hitachi_t *h, const uint8_t enable)
{
    if (enable == h->mca_enabled)
        return;

    if (enable)
        io_sethandler(h->base, HITACHI_MCA_WINDOW, hitachi_mca_in, NULL, NULL, hitachi_mca_out, NULL, NULL, h);
    else
        io_removehandler(h->base, HITACHI_MCA_WINDOW, hitachi_mca_in, NULL, NULL, hitachi_mca_out, NULL, NULL, h);

    h->mca_enabled = enable;
}

static uint8_t
hitachi_mca_pos_read(uint16_t port, void *priv)
{
    const hitachi_t *h = priv;

    return h->pos_regs[port & 7];
}

static void
hitachi_mca_pos_write(uint16_t port, uint8_t val, void *priv)
{
    hitachi_t *h = priv;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    /* The bits above POS 2's window and enable read back zero. */
    if ((port & 7) == 2)
        val &= 0x1f;

    hitachi_mca_window(h, 0);
    h->pos_regs[port & 7] = val;

    /* The window is rebuilt from POS 2: bits 4-1 pick it, bit 0 enables it. */
    h->base = hitachi_mca_bases[(h->pos_regs[2] >> 1) & 0x0f];
    hitachi_mca_window(h, h->pos_regs[2] & 0x01);
}

static uint8_t
hitachi_mca_feedb(void *priv)
{
    const hitachi_t *h = priv;

    return h->pos_regs[2] & 0x01;
}

static void *
hitachi_mca_init(UNUSED(const device_t *info))
{
    hitachi_t *h = calloc(1, sizeof(*h));

    h->pos_regs[0] = HITACHI_MCA_ID_LOW;
    h->pos_regs[1] = HITACHI_MCA_ID_HIGH;

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
    hitachi_mca_pos_write(0x102, 0x09, h); /* 300h, enabled, until POS says otherwise */
    mca_add(hitachi_mca_pos_read, hitachi_mca_pos_write, hitachi_mca_feedb, hitachi_reset, h);

    return h;
}

static void
hitachi_mca_close(void *priv)
{
    hitachi_t *h = priv;

    if (!h)
        return;

    hitachi_mca_window(h, 0);
    hitachi_reset(h);

    for (unsigned i = 0; i < 4; ++i) {
        cdrom_t *cd = h->drives[i].cd;
        if (cd && cd->priv == &h->drives[i]) {
            cd->priv   = NULL;
            cd->insert = NULL;
        }
    }

    free(h);
}

static const device_config_t hitachi_isa_config[] = {
    // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "200H", .value = 0x200 },
            { .description = "220H", .value = 0x220 },
            { .description = "240H", .value = 0x240 },
            { .description = "260H", .value = 0x260 },
            { .description = "300H", .value = 0x300 },
            { .description = "320H", .value = 0x320 },
            { .description = "340H", .value = 0x340 },
            { .description = "360H", .value = 0x360 },
            { NULL                                  }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t hitachi_cdrom_isa_device = {
    .name          = "Hitachi CD-IFI4-A",
    .internal_name = "hitachi_ifi4a",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = hitachi_isa_init,
    .close         = hitachi_isa_close,
    .reset         = hitachi_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = hitachi_isa_config
};

const device_t hitachi_cdrom_mca_device = {
    .name          = "Hitachi CD-ROM Adapter",
    .internal_name = "hitachi_cdrom_mca",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = hitachi_mca_init,
    .close         = hitachi_mca_close,
    .reset         = hitachi_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
