#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/timer.h>

typedef struct {
    sn76489_t  sn76489;
    uint8_t    status, ctrl;
    uint64_t   timer_latch;
    pc_timer_t timer_count;
    int        timer_enable;
    uint8_t    fifo[2048];
    int        fifo_read_idx, fifo_write_idx;
    int        fifo_threshold;
    uint8_t    dac_val;
    int16_t    buffer[SOUNDBUFLEN];
    int        pos;
} ps1snd_t;

static void
ps1snd_update_irq_status(ps1snd_t *snd)
{
    if (((snd->status & snd->ctrl) & 0x12) && (snd->ctrl & 0x01))
        picint(1 << 7);
    else
        picintc(1 << 7);
}

static uint8_t
ps1snd_read(uint16_t port, void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;
    uint8_t   ret    = 0xff;

    switch (port & 7) {
        case 0: /* ADC data */
            ps1snd->status &= ~0x10;
            ps1snd_update_irq_status(ps1snd);
            ret = 0;
            break;

        case 2: /* status */
            ret = ps1snd->status;
            ret |= (ps1snd->ctrl & 0x01);
            if ((ps1snd->fifo_write_idx - ps1snd->fifo_read_idx) >= 2048)
                ret |= 0x08; /* FIFO full */
            if (ps1snd->fifo_read_idx == ps1snd->fifo_write_idx)
                ret |= 0x04; /* FIFO empty */
            break;

        case 3: /* FIFO timer */
            /*
             * The PS/1 Technical Reference says this should return
             * thecurrent value, but the PS/1 BIOS and Stunt Island
             * expect it not to change.
             */
            ret = ps1snd->timer_latch;
            break;

        case 4:
        case 5:
        case 6:
        case 7:
            ret = 0;
    }

    return (ret);
}

static void
ps1snd_write(uint16_t port, uint8_t val, void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;

    switch (port & 7) {
        case 0: /* DAC output */
            if ((ps1snd->fifo_write_idx - ps1snd->fifo_read_idx) < 2048) {
                ps1snd->fifo[ps1snd->fifo_write_idx & 2047] = val;
                ps1snd->fifo_write_idx++;
            }
            break;

        case 2: /* control */
            ps1snd->ctrl = val;
            if (!(val & 0x02))
                ps1snd->status &= ~0x02;
            ps1snd_update_irq_status(ps1snd);
            break;

        case 3: /* timer reload value */
            ps1snd->timer_latch = val;
            if (val)
                timer_set_delay_u64(&ps1snd->timer_count, ((0xff - val) * TIMER_USEC));
            else
                timer_disable(&ps1snd->timer_count);
            break;

        case 4: /* almost empty */
            ps1snd->fifo_threshold = val * 4;
            break;
    }
}

static void
ps1snd_update(ps1snd_t *ps1snd)
{
    for (; ps1snd->pos < sound_pos_global; ps1snd->pos++)
        ps1snd->buffer[ps1snd->pos] = (int8_t) (ps1snd->dac_val ^ 0x80) * 0x20;
}

static void
ps1snd_callback(void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;

    ps1snd_update(ps1snd);

    if (ps1snd->fifo_read_idx != ps1snd->fifo_write_idx) {
        ps1snd->dac_val = ps1snd->fifo[ps1snd->fifo_read_idx & 2047];
        ps1snd->fifo_read_idx++;
    }

    if ((ps1snd->fifo_write_idx - ps1snd->fifo_read_idx) == ps1snd->fifo_threshold)
        ps1snd->status |= 0x02; /*FIFO almost empty*/

    ps1snd->status |= 0x10; /*ADC data ready*/
    ps1snd_update_irq_status(ps1snd);

    timer_advance_u64(&ps1snd->timer_count, ps1snd->timer_latch * TIMER_USEC);
}

static void
ps1snd_get_buffer(int32_t *buffer, int len, void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;
    int       c;

    ps1snd_update(ps1snd);

    for (c = 0; c < len * 2; c++)
        buffer[c] += ps1snd->buffer[c >> 1];

    ps1snd->pos = 0;
}

static void *
ps1snd_init(const device_t *info)
{
    ps1snd_t *ps1snd = malloc(sizeof(ps1snd_t));
    memset(ps1snd, 0x00, sizeof(ps1snd_t));

    sn76489_init(&ps1snd->sn76489, 0x0205, 0x0001, SN76496, 4000000);

    io_sethandler(0x0200, 1,
                  ps1snd_read, NULL, NULL,
                  ps1snd_write, NULL, NULL,
                  ps1snd);
    io_sethandler(0x0202, 6,
                  ps1snd_read, NULL, NULL,
                  ps1snd_write, NULL, NULL,
                  ps1snd);

    timer_add(&ps1snd->timer_count, ps1snd_callback, ps1snd, 0);

    sound_add_handler(ps1snd_get_buffer, ps1snd);

    return (ps1snd);
}

static void
ps1snd_close(void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;

    free(ps1snd);
}

const device_t ps1snd_device = {
    .name          = "IBM PS/1 Audio Card",
    .internal_name = "ps1snd",
    .flags         = 0,
    .local         = 0,
    .init          = ps1snd_init,
    .close         = ps1snd_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
