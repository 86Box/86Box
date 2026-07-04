/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sound source management.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2026 Miran Grca.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/sound.h>
#include <86box/timer.h>

typedef struct sound_source_t {
    const char *           name;

    void *                 src;
    int32_t *              buf;

    float *                buff;
    int16_t *              buf16;

    int                    pos;
    int                    gain;
    int                    sample_rate;
    int                    buffer_size;

    pc_timer_t             timer;
    uint64_t               latch;

    void                   (*get_buffer)(int32_t *buffer, uint16_t len, void *priv);
    void *                 priv;

    const device_t *       info;

    struct sound_source_t *prev;
    struct sound_source_t *next;
} sound_source_t;

/* Needed for recalculation on backend change. */
static sound_source_t *first_sound_source = NULL;
static sound_source_t *last_sound_source  = NULL;

int
sound_source_get_pos(void *priv)
{
    const sound_source_t *src = (sound_source_t *) priv;

    return src->pos;
}

void
sound_source_give_buffer(void *priv, const void *buf)
{
    const sound_source_t *src = (sound_source_t *) priv;

    sound_backend_give_buffer(src->src, buf, src->buffer_size, src->gain);
}

static void
sound_source_clear_buffers(sound_source_t *src)
{
    if (src->buf16 != NULL)
        memset(src->buf16, 0x00, 2 * src->buffer_size * sizeof(int16_t));

    if (src->buff != NULL)
        memset(src->buff, 0x00, 2 * src->buffer_size * sizeof(float));

    if (src->buf != NULL)
        memset(src->buf, 0x00, 2 * src->buffer_size * sizeof(int32_t));

    src->pos = 0;
}

static void
sound_source_dealloc_buffers(sound_source_t *src)
{
    if (src->buf16 != NULL) {
        free(src->buf16);
        src->buf16 = NULL;
    }

    if (src->buff != NULL) {
        free(src->buff);
        src->buff = NULL;
    }

    if (src->buf != NULL) {
        free(src->buf);
        src->buf = NULL;
    }

    src->pos = 0;
}

static void
sound_source_alloc_buffers(sound_source_t *src)
{
    src->buf = (int32_t *) calloc(sizeof(int32_t), 2 * src->buffer_size);

    if (sound_is_float)
        src->buff  = (float *) calloc(sizeof(float), 2 * src->buffer_size);
    else
        src->buf16 = (int16_t *) calloc(sizeof(int16_t), 2 * src->buffer_size);
}

static void
sound_source_do_close(sound_source_t *src)
{
    timer_disable(&src->timer);

    sound_backend_close_source(src->src);

    src->src = NULL;
}

static void
sound_source_do_init(sound_source_t *src)
{
    src->src = sound_backend_init_source(src->sample_rate, src->buffer_size);
}

void
sound_source_set_sample_rate(void *priv)
{
    sound_source_t *src = (sound_source_t *) priv;

    sound_source_do_close(src);
    sound_source_dealloc_buffers(src);

    src->buffer_size    = src->sample_rate / 50;
    src->latch          = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) src->sample_rate));

    sound_source_alloc_buffers(src);
    sound_source_do_init(src);

    timer_set_delay_u64(&src->timer, src->latch);
}

void
sound_source_poll(void *priv)
{
    sound_source_t *src = (sound_source_t *) priv;

    timer_advance_u64(&src->timer, src->latch);

    src->pos++;
    if (src->pos == src->buffer_size) {
        memset(src->buf, 0x00, src->buffer_size * 2 * sizeof(int32_t));

        if (src->get_buffer != NULL)
            src->get_buffer(src->buf, src->buffer_size, src->priv);

        for (uint32_t c = 0; c < (uint32_t) (src->buffer_size * 2); c++) {
            if (sound_is_float)
                src->buff[c] = ((float) src->buf[c]) / (float) 32768.0;
            else {
                if (src->buf[c] > 32767)
                    src->buf[c] = 32767;
                if (src->buf[c] < -32768)
                    src->buf[c] = -32768;

                src->buf16[c] = (int16_t) src->buf[c];
            }
        }

        if (sound_is_float)
            sound_source_give_buffer(src, src->buff);
        else
            sound_source_give_buffer(src, src->buf16);

        src->pos = 0;
    }
}

void
sound_source_close_all(void)
{
    sound_source_t *src = first_sound_source;

    while (src != NULL) {
        sound_source_do_close(src);
        sound_source_clear_buffers(src);

        src = src->next;
    }
}

void
sound_source_reopen_all(void)
{
    sound_source_t *src = first_sound_source;

    while (src != NULL) {
        sound_source_do_init(src);
        timer_set_delay_u64(&src->timer, src->latch);

        src = src->next;
    }
}

const char *
sound_source_get_name(void *priv)
{
    const sound_source_t *src = (sound_source_t *) priv;

    return src->name;
}

void
sound_source_set_name(void *priv, const char *name)
{
    sound_source_t *src = (sound_source_t *) priv;

    src->name = name;
}

const device_t *
sound_source_get_device(void *priv)
{
    const sound_source_t *src = (sound_source_t *) priv;

    return src->info;
}

static void
sound_source_close(void *priv)
{
    sound_source_t *src = (sound_source_t *) priv;

    if (src->prev != NULL)
        src->prev->next    = src->next;

    if (src->next != NULL)
        src->next->prev    = src->prev;

    if (first_sound_source == src)
        first_sound_source = src->next;

    if (last_sound_source == src)
        last_sound_source  = src->prev;

    sound_source_dealloc_buffers(src);

    free(src);
}

static void *
sound_source_init(const device_t *info)
{
    sound_source_t *src     = (sound_source_t *) calloc(1, sizeof(sound_source_t));

    src->sample_rate        = info->local;
    src->buffer_size        = src->sample_rate / 50;

    timer_add(&src->timer, sound_source_poll, src, 1);

    sound_source_alloc_buffers(src);
    sound_source_do_init(src);

    src->info               = info;

    if (first_sound_source == NULL)
        first_sound_source = src;

    src->prev               = last_sound_source;

    last_sound_source->next = src;
    last_sound_source       = src;

    return src;
}

const device_t sound_source_device = {
    .name          = "Sound source",
    .internal_name = "sound_source",
    .flags         = 0,
    .local         = 0,
    .init          = sound_source_init,
    .close         = sound_source_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
