#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/cm100.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/ui.h>
#include "cm153_uart.h"

typedef struct cm153 {
    cdrom_t *cd;
    cm100_drive_t *drive;
    cm153_uart_t uart;
    pc_timer_t uart_timer;
    pc_timer_t data_timer;
    uint16_t base;
    uint8_t data_gate;
    uint8_t data_phase;
    uint8_t data_byte;
    uint8_t pending_byte;
    uint8_t pending_valid;
    uint8_t dma_over;
    uint8_t attention_samples;
    uint8_t probe_counter;
    uint8_t probe_status;
} cm153_t;

static uint64_t cm153_now_ns(void);

static void
cm153_medium(cm153_t *card)
{
    int present = card->cd && card->cd->ops && card->cd->local &&
                  ((card->cd->cd_status & CD_STATUS_MASK) != CD_STATUS_EMPTY) &&
                  ((card->cd->cd_status & CD_STATUS_MASK) != CD_STATUS_DVD_REJECTED);
    /* cdrom_capacity is the last LBA, while the drive core needs a count. */
    uint32_t sectors = present ? card->cd->cdrom_capacity + 1 : 0;
    cm100_set_medium(card->drive, sectors, present);
}

static int
cm153_read_sector(void *opaque, uint32_t lba, uint8_t raw[2352],
                  uint8_t unreliable[2352])
{
    cm153_t *card = opaque;
    int len = 0;
    if (!card->cd || !card->cd->ops || lba > card->cd->cdrom_capacity)
        return 0;
    if (card->cd->ops->get_track_type &&
        (card->cd->ops->get_track_type(card->cd->local, lba) & CD_TRACK_AUDIO))
        return 0;
    memset(unreliable, 0, 2352);
    if (cdrom_readsector_raw(card->cd, raw, lba, 0, 0, 0xf8, &len, 0) <= 0 ||
        len != 2352)
        return 0;
    card->cd->seek_pos = lba;
    ui_sb_update_icon(SB_CDROM | card->cd->id, 1);
    return 1;
}

static void
cm153_signal(void *opaque, cm100_signal_t signal, int level,
             uint64_t time_ns)
{
    cm153_t *card = opaque;
    (void) time_ns;
    if (signal == CM100_DATA_CLOCK && level) {
        /* U7118 samples ATTENTION at a byte boundary, not on an ISA
           register read or an ATTENTION transition. */
        if (++card->data_phase == 8) {
            cm100_snapshot_t snapshot;
            cm100_snapshot(card->drive, &snapshot);
            card->attention_samples =
                (uint8_t) ((card->attention_samples << 1) |
                           snapshot.attention);
            card->data_phase = 0;
        }
    }
}

static int
cm153_probe_mode(const cm153_t *card)
{
    /* The original card check enables TX/RX with 05h; normal operation
       enables DTR and RTS as well. Those outputs select the on-card
       counter/ATTENTION loopback through U7109. */
    return (card->uart.command & 0x27) == 0x05;
}

static void
cm153_probe_clock(cm153_t *card)
{
    /* U7115 is a seven-stage 4024. Its Q6 level is looped to the
       ATTENTION shift path during the card check. The driver clocks the
       path by reading base+8 and observes low -> high -> low. */
    card->probe_counter = (card->probe_counter + 1) & 0x7f;
    int q6 = !!(card->probe_counter & 0x40);
    card->probe_status |= q6;
    card->attention_samples =
        (uint8_t) ((card->attention_samples << 1) | q6);
}

static uint64_t
cm153_now_ns(void)
{
    return (uint64_t) ((double) tsc * 4294967296.0 * 1000.0 /
                       (double) TIMER_USEC);
}

static void
cm153_uart_timer(void *opaque)
{
    cm153_t *card = opaque;
    uint64_t now_ns = cm153_now_ns();
    cm100_advance(card->drive, now_ns);
    cm153_uart_tick(&card->uart, card->drive, now_ns);
    timer_on_auto(&card->uart_timer, 1000000.0 / 19200.0);
}

static void
cm153_data_timer(void *opaque)
{
    cm153_t *card = opaque;
    uint64_t now_ns = cm153_now_ns();
    if (!card->data_gate || card->dma_over)
        return;
    if (card->pending_valid) {
        int result;
        dma_set_drq(3, 1);
        result = dma_channel_write(3, card->pending_byte);
        dma_set_drq(3, 0);
        if (result == DMA_NODATA) {
            timer_on_auto(&card->data_timer, 10.0);
            return;
        }
        card->pending_valid = 0;
        if (result & DMA_OVER) {
            card->dma_over = 1;
            return;
        }
    }
    cm100_advance(card->drive, now_ns);
    int transferred = 0;
    for (unsigned phase = 0; phase < 8; phase++) {
        int bit = cm100_data_edge(card->drive, 0, now_ns + phase * 333);
        if (bit < 0)
            break;
        bit = cm100_data_edge(card->drive, 1, now_ns + phase * 333 + 166);
        if (bit < 0)
            break;
        card->data_byte |= bit << phase;
        if (phase == 7) {
            card->pending_byte = card->data_byte;
            card->pending_valid = 1;
            transferred = 1;
        }
    }
    card->data_byte = 0;
    if (transferred) {
        timer_on_auto(&card->data_timer, 8.0 / 3.0);
    } else {
        cm100_snapshot_t state;
        cm100_snapshot(card->drive, &state);
        double delay_us = 1000.0;
        if (state.data_active && state.next_sector_ns > now_ns)
            delay_us = (double) (state.next_sector_ns - now_ns) / 1000.0;
        timer_on_auto(&card->data_timer, delay_us);
    }
}

static uint8_t
cm153_in(uint16_t port, void *opaque)
{
    cm153_t *card = opaque;
    switch ((port - card->base) & 0x0f) {
        case 2:
            return cm153_uart_receive(&card->uart);
        case 3: {
            cm100_snapshot_t snapshot;
            cm100_snapshot(card->drive, &snapshot);
            return cm153_uart_status(&card->uart, snapshot.attention);
        }
        case 4:
            /* The schematic routes a flip-flop through U7111 to MDB7. */
            if (cm153_probe_mode(card))
                return card->probe_status ? 0x80 : 0;
            return card->pending_valid ? 0x80 : 0;
        case 5:
            if (cm153_probe_mode(card))
                return card->probe_status ? 0x80 : 0;
            /* The recovery loop polls this port's bit 7, then advances
               DX and reads the sampled ATTENTION byte at base+6. */
            return card->pending_valid ? 0x80 : 0;
        case 6:
        case 7:
            return card->attention_samples;
        case 8:
        case 9:
            if (cm153_probe_mode(card))
                cm153_probe_clock(card);
            return 0xff;
        default:
            return 0xff;
    }
}

static void
cm153_out(uint16_t port, uint8_t value, void *opaque)
{
    cm153_t *card = opaque;
    switch ((port - card->base) & 0x0f) {
        case 0:
        case 1:
            card->data_gate = 1;
            card->dma_over = 0;
            card->data_phase = 0;
            card->pending_valid = 0;
            card->probe_counter = 0;
            card->probe_status = 0;
            card->attention_samples = 0;
            timer_on_auto(&card->data_timer, 8.0 / 3.0);
            break;
        case 2:
            cm153_uart_transmit(&card->uart, value);
            break;
        case 3:
            cm153_uart_control(&card->uart, card->drive, value,
                               cm153_now_ns());
            break;
        default:
            break;
    }
}

static void
cm153_insert(void *opaque)
{
    cm153_t *card = opaque;
    cm153_medium(card);
}

static void
cm153_reset(void *opaque)
{
    cm153_t *card = opaque;
    timer_disable(&card->data_timer);
    dma_set_drq(3, 0);
    card->data_gate = card->data_phase = card->pending_valid = 0;
    card->dma_over = card->attention_samples = 0;
    card->probe_counter = card->probe_status = 0;
    cm153_uart_reset(&card->uart);
    cm100_reset(card->drive, cm153_now_ns());
    cm153_medium(card);
}

static void *
cm153_init(UNUSED(const device_t *info))
{
    cm153_t *card = calloc(1, sizeof(*card));
    if (!card)
        return NULL;
    for (int i = 0; i < CDROM_NUM; i++)
        if (cdrom[i].bus_type == CDROM_BUS_CM100) {
            card->cd = &cdrom[i];
            break;
        }
    if (!card->cd) {
        free(card);
        return NULL;
    }
    card->drive = cm100_create(cm153_read_sector, cm153_signal, card);
    if (!card->drive) {
        free(card);
        return NULL;
    }
    card->base = device_get_config_hex16("base");
    card->cd->priv = card;
    card->cd->insert = cm153_insert;
    card->cd->cached_sector = card->cd->subc_sector = -1;
    card->cd->cur_speed = 1;
    timer_add(&card->uart_timer, cm153_uart_timer, card, 0);
    timer_add(&card->data_timer, cm153_data_timer, card, 0);
    cm153_reset(card);
    io_sethandler(card->base, 16, cm153_in, NULL, NULL,
                  cm153_out, NULL, NULL, card);
    timer_on_auto(&card->uart_timer, 1000000.0 / 19200.0);
    return card;
}

static void
cm153_close(void *opaque)
{
    cm153_t *card = opaque;
    timer_disable(&card->uart_timer);
    timer_disable(&card->data_timer);
    dma_set_drq(3, 0);
    io_removehandler(card->base, 16, cm153_in, NULL, NULL,
                     cm153_out, NULL, NULL, card);
    if (card->cd && card->cd->priv == card) {
        card->cd->priv = NULL;
        card->cd->insert = NULL;
    }
    cm100_destroy(card->drive);
    free(card);
}

static const device_config_t cm153_config[] = {
    { .name = "base", .description = "Address", .type = CONFIG_HEX16,
      .default_int = 0x340,
      .selection = { { "300H", 0x300 }, { "310H", 0x310 },
                     { "330H", 0x330 }, { "340H", 0x340 }, { 0 } } },
    { .name = "", .type = CONFIG_END }
};

const device_t philips_cm153_device = {
    .name = "Philips CM-153 (CM-100)",
    .internal_name = "philips_cm153",
    .flags = DEVICE_ISA,
    .init = cm153_init,
    .close = cm153_close,
    .reset = cm153_reset,
    .config = cm153_config
};
