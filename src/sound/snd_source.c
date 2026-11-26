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
#include <86box/runctl.h>
#include <86box/device.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/automation.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <stdio.h>
#include <math.h>

typedef struct sound_source_t {
    const char *           name;

    void *                 src;
    int32_t *              buf;

    int16_t *              buf16;

    int                    cls;        /* Stream class + 1, 0 for none. */
    int                    pos;
    int                    gain;       /* Per-source gain in dB. */
    char                   key[64];    /* Configuration key of the gain. */
    char                   label[160]; /* What the user sees. */
    char                   what[32];   /* What the source is of its device (FM, PCM...). */

    /* Sources of the same device, class and rate share one timer: the leader
       runs it and delivers the blocks of its followers as well. */
    struct sound_source_t *leader;
    struct sound_source_t *followers;
    struct sound_source_t *fnext;
    int                    inst;
    int                    sample_rate;
    int                    buffer_size;

    double                 req_rate;   /* Rate the card asked for, 0 for none. */
    int                    req_seen;   /* Rate seen at the previous block. */

    pc_timer_t             timer;
    uint64_t               latch;

    void                   (*get_buffer)(int32_t *buffer, uint16_t len, void *priv);
    void *                 priv;

    const device_t *       info;
    const device_t *       dev;        /* Device that created the source. */

    struct sound_source_t *prev;
    struct sound_source_t *next;
} sound_source_t;

/* Needed for recalculation on backend change. */
static sound_source_t *first_sound_source = NULL;
static sound_source_t *last_sound_source  = NULL;

/* What the sources of a stream class have produced together, for automation.
   The sources of a class run in lockstep, so a block is complete when all of
   them have delivered theirs. */
typedef struct sound_class_mix_t {
    int32_t *buf;
    void *   out;
    int      len;
    int      done;
} sound_class_mix_t;

static sound_class_mix_t class_mix[I_MAX] = { 0 };

static sound_source_t *
sound_source_lead(sound_source_t *src)
{
    return (src->leader != NULL) ? src->leader : src;
}

static void sound_source_identify(sound_source_t *src);

static int
sound_source_class_count(const int cls)
{
    int count = 0;

    for (const sound_source_t *src = first_sound_source; src != NULL; src = src->next)
        count += (src->cls == cls);

    return count;
}

static sound_source_t *
sound_source_class_first(const int cls)
{
    for (sound_source_t *src = first_sound_source; src != NULL; src = src->next) {
        if ((src->cls == cls) && (src->leader == NULL))
            return src;
    }

    return NULL;
}

/* The frames of a block of a stream class as seen by automation: the main
   stream is always presented at the default host rate, no matter which rate
   its sources currently run at. */
static int
sound_source_class_frames(const sound_source_t *src)
{
    return (src->cls == (I_NORMAL + 1)) ? (sound_sample_rate / 50) : src->buffer_size;
}

static void
sound_source_class_tap(const sound_source_t *src)
{
    const uint8_t      stream = (uint8_t) (src->cls - 1);
    sound_class_mix_t *mix    = &class_mix[stream];
    const int          frames = sound_source_class_frames(src);
    const int          len    = frames * 2;

    if (mix->len != len) {
        free(mix->buf);
        free(mix->out);
        mix->buf  = (int32_t *) calloc(len, sizeof(int32_t));
        mix->out  = calloc(len, sizeof(int16_t));
        mix->len  = len;
        mix->done = 0;
    }

    if (frames == src->buffer_size) {
        for (int c = 0; c < len; c++)
            mix->buf[c] += src->buf[c];
    } else {
        for (int c = 0; c < frames; c++) {
            const int ind = (int) (((int64_t) c * src->buffer_size) / frames);

            mix->buf[c * 2]     += src->buf[ind * 2];
            mix->buf[c * 2 + 1] += src->buf[ind * 2 + 1];
        }
    }

    if (++mix->done < sound_source_class_count(src->cls))
        return;

    {
        int16_t *out = (int16_t *) mix->out;

        for (int c = 0; c < len; c++) {
            int32_t v = mix->buf[c];

            out[c] = (int16_t) ((v > 32767) ? 32767 : ((v < -32768) ? -32768 : v));
        }
    }

    automation_audio(stream, mix->out, len);

    memset(mix->buf, 0x00, len * sizeof(int32_t));
    mix->done = 0;
}

/* Samples a stream class has ticked since its last block. */
int
sound_source_class_pending(const uint8_t stream)
{
    const sound_source_t *src = sound_source_class_first(stream + 1);

    return (src != NULL) ? (int) (((int64_t) src->pos * sound_source_class_frames(src)) / src->buffer_size) : 0;
}

/* See sound_poll_offset_delay(): the phase of the main stream's sources. */
uint64_t
sound_source_offset_delay(pc_timer_t *timer, uint64_t latch)
{
    const sound_source_t *ref = sound_source_class_first(I_NORMAL + 1);
    int64_t               period;
    int64_t               diff;

    if ((ref == NULL) || !ref->latch || !timer_is_enabled((pc_timer_t *) &ref->timer))
        return latch;

    period = (int64_t) ref->latch;

    diff = (int64_t) ((ref->timer.ts_integer - timer->ts_integer) << 32) +
           (int64_t) ref->timer.ts_frac - (int64_t) timer->ts_frac;
    diff = (diff + (period / 2)) % period;
    if (diff < 0)
        diff += period;
    if (diff < (period / 2))
        diff += period;

    return (uint64_t) diff;
}

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

    if ((src->src != NULL) && (runctl_mode() != RUN_MODE_FAST))
        sound_backend_give_buffer(src->src, buf, src->buffer_size * 2, src->gain);
}

static void
sound_source_dealloc_buffers(sound_source_t *src)
{
    if (src->buf16 != NULL) {
        free(src->buf16);
        src->buf16 = NULL;
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
    src->src = sound_backend_init_source(src->sample_rate, src->buffer_size * 2);
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

/* A card tells which rate its output is generated at. The change is applied at
   a block boundary, when it has been asked for twice in a row, so that the
   clock the card reads stays consistent and a guest that reprograms its rate
   back and forth does not make the backend reopen its stream every time. */
void
sound_source_set_rate(void *priv, const double rate)
{
    sound_source_t *src = (sound_source_t *) priv;
    double          r   = rate;

    if (src == NULL)
        return;

    src = sound_source_lead(src);

    if ((r <= 0.0) || (r > (double) sound_sample_rate))
        r = (double) sound_sample_rate;
    else if (r < (double) SOUND_MIN_RATE)
        r = (double) SOUND_MIN_RATE;

    src->req_rate = r;
}

/* For cards that only know their clock. */
void
sound_source_set_rate_by_clock(const int *clock, const double rate)
{
    if (clock == NULL)
        return;

    for (sound_source_t *src = first_sound_source; src != NULL; src = src->next) {
        if (&src->pos == clock) {
            sound_source_set_rate(src, rate);
            return;
        }
    }
}

int
sound_source_get_rate(void *priv)
{
    const sound_source_t *src = (sound_source_t *) priv;

    return (src != NULL) ? src->sample_rate : sound_sample_rate;
}

static void
sound_source_apply_rate(sound_source_t *src)
{
    const int      rate  = (int) (src->req_rate + 0.5);
    const uint64_t latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / src->req_rate));
    void *         old;

    if (rate <= 0)
        return;

    if (rate == src->sample_rate) {
        /* Same host rate: only follow the exact period, to stay in step with the card. */
        src->latch    = latch;
        src->req_seen = rate;
        return;
    }

    if (src->req_seen != rate) {
        src->req_seen = rate;
        return;
    }

    for (sound_source_t *s = src; s != NULL; s = (s == src) ? src->followers : s->fnext) {
        old      = s->src;
        s->src   = NULL;
        sound_backend_close_source(old);

        sound_source_dealloc_buffers(s);

        s->sample_rate = rate;
        s->buffer_size = rate / 50;
        s->latch       = latch;

        sound_source_alloc_buffers(s);
        sound_source_do_init(s);
    }
}

/* The gain of a source in dB, as a factor. */
static double
sound_source_gain_factor(const int gain)
{
    return pow(10.0, (double) gain / 20.0);
}

static void
sound_source_block(sound_source_t *src)
{
    memset(src->buf, 0x00, src->buffer_size * 2 * sizeof(int32_t));

    if (src->get_buffer != NULL)
        src->get_buffer(src->buf, src->buffer_size, src->priv);

    if (src->cls != 0)
        sound_source_class_tap(src);

    const int    gained = (src->gain != 0);
    const double factor = gained ? sound_source_gain_factor(src->gain) : 1.0;

    for (uint32_t c = 0; c < (uint32_t) (src->buffer_size * 2); c++) {
        double v = (double) src->buf[c];

        if (gained)
            v *= factor;
        if (v > 32767.0)
            v = 32767.0;
        if (v < -32768.0)
            v = -32768.0;

        src->buf16[c] = (int16_t) v;
    }

    sound_source_give_buffer(src, src->buf16);
}

void
sound_source_poll(void *priv)
{
    sound_source_t *src = (sound_source_t *) priv;

    sound_source_t *f;

    timer_advance_u64(&src->timer, src->latch);

    src->pos++;
    for (f = src->followers; f != NULL; f = f->fnext)
        f->pos = src->pos;

    if (src->pos == src->buffer_size) {
        sound_source_block(src);

        for (f = src->followers; f != NULL; f = f->fnext)
            sound_source_block(f);

        src->pos = 0;
        for (f = src->followers; f != NULL; f = f->fnext)
            f->pos = 0;

        if (src->req_rate != 0.0)
            sound_source_apply_rate(src);
    }
}

void
sound_source_close_all(void)
{
    sound_source_t *src = first_sound_source;

    /* The timer and the position keep running, so that whoever reads the
       position is not disturbed by a backend change. */
    while (src != NULL) {
        void *old = src->src;

        src->src = NULL;
        sound_backend_close_source(old);

        src = src->next;
    }
}

void
sound_source_reopen_all(void)
{
    sound_source_t *src = first_sound_source;

    while (src != NULL) {
        if (src->src == NULL)
            sound_source_do_init(src);

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

    sound_source_do_close(src);

    if (src->leader != NULL) {
        sound_source_t **pp = &src->leader->followers;

        while ((*pp != NULL) && (*pp != src))
            pp = &(*pp)->fnext;
        if (*pp != NULL)
            *pp = src->fnext;
    } else if (src->followers != NULL) {
        /* Hand the timer over to the first follower. */
        sound_source_t *n = src->followers;
        const uint64_t  rem = timer_is_enabled(&src->timer) ? timer_get_remaining_u64(&src->timer) : src->latch;

        n->leader    = NULL;
        n->followers = n->fnext;
        n->fnext     = NULL;
        for (sound_source_t *f = n->followers; f != NULL; f = f->fnext)
            f->leader = n;
        n->req_rate  = src->req_rate;
        n->req_seen  = src->req_seen;
        n->latch     = src->latch;
        n->pos       = src->pos;
        timer_set_delay_u64(&n->timer, rem);
    }

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
    src->latch              = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) src->sample_rate));

    timer_add(&src->timer, sound_source_poll, src, 1);

    sound_source_alloc_buffers(src);
    sound_source_do_init(src);

    src->info               = info;

    if (first_sound_source == NULL)
        first_sound_source = src;

    src->prev               = last_sound_source;

    if (last_sound_source != NULL)
        last_sound_source->next = src;

    last_sound_source       = src;

    sound_source_identify(src);

    return src;
}

/* A source owned by a sound card: it has its own clock, which the card (and the
   FM chip drivers) read through sound_source_get_pos_ptr(), and its own timer,
   which calls get_buffer once per block. */
static void
sound_source_link(sound_source_t *src)
{
    if (first_sound_source == NULL)
        first_sound_source = src;

    src->prev = last_sound_source;

    if (last_sound_source != NULL)
        last_sound_source->next = src;

    last_sound_source = src;
}

static void *sound_source_add_unmerged(int sample_rate, int frames,
                                       void (*get_buffer)(int32_t *buffer, uint16_t len, void *priv),
                                       void *priv);

/* Names the source after the device that creates it and loads its gain. */
static void
sound_source_identify(sound_source_t *src)
{
    int ordinal = 0;

    src->dev  = device_get_current();
    src->inst = (src->dev != NULL) ? device_get_instance() : 0;

    for (const sound_source_t *o = first_sound_source; o != NULL; o = o->next) {
        if ((o != src) && (o->dev == src->dev) && (o->inst == src->inst))
            ordinal++;
    }

    if (src->dev != NULL) {
        char name[128] = { 0 };

        device_get_name(src->dev, 0, name);
        snprintf(src->key, sizeof(src->key), "%s_%i_%i",
                 device_get_internal_name(src->dev), src->inst, ordinal);
        snprintf(src->label, sizeof(src->label), "%s #%i", name, ordinal + 1);
    } else {
        snprintf(src->key, sizeof(src->key), "source_%i", ordinal);
        snprintf(src->label, sizeof(src->label), "Source #%i", ordinal + 1);
    }

    src->gain = config_get_int("Sound gains", src->key, 0);
}

/* Sources of the same device, class and rate do not need a timer each. */
static void
sound_source_merge(sound_source_t *src)
{
    if (src->dev == NULL)
        return;

    for (sound_source_t *o = first_sound_source; o != NULL; o = o->next) {
        if ((o == src) || (o->leader != NULL) || (o->dev != src->dev) || (o->inst != src->inst) ||
            (o->cls != src->cls) || (o->sample_rate != src->sample_rate) || (o->buffer_size != src->buffer_size))
            continue;

        timer_disable(&src->timer);

        src->leader   = o;
        src->fnext    = o->followers;
        o->followers  = src;
        src->pos      = o->pos;
        src->latch    = o->latch;
        return;
    }
}

void *
sound_source_add(const int sample_rate, const int frames,
                 void (*get_buffer)(int32_t *buffer, uint16_t len, void *priv), void *priv)
{
    sound_source_t *src = (sound_source_t *) sound_source_add_unmerged(sample_rate, frames, get_buffer, priv);

    if (src != NULL)
        sound_source_merge(src);

    return src;
}

static void *
sound_source_add_unmerged(const int sample_rate, const int frames,
                          void (*get_buffer)(int32_t *buffer, uint16_t len, void *priv), void *priv)
{
    sound_source_t *src = (sound_source_t *) calloc(1, sizeof(sound_source_t));

    if (src == NULL)
        return NULL;

    src->sample_rate = sample_rate;
    src->buffer_size = frames;
    src->latch       = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) sample_rate));
    src->get_buffer  = get_buffer;
    src->priv        = priv;

    sound_source_alloc_buffers(src);
    sound_source_do_init(src);
    sound_source_link(src);
    sound_source_identify(src);

    timer_add(&src->timer, sound_source_poll, src, 1);

    return src;
}

void *
sound_source_add_stream(const uint8_t stream,
                        void (*get_buffer)(int32_t *buffer, uint16_t len, void *priv), void *priv)
{
    sound_source_t *peer = sound_source_class_first(stream + 1);
    sound_source_t *src  = NULL;

    switch (stream) {
        case I_NORMAL:
            src = (sound_source_t *) sound_source_add_unmerged(sound_sample_rate, sound_sample_rate / 50, get_buffer, priv);
            break;
        case I_MUSIC:
            src = (sound_source_t *) sound_source_add_unmerged(MUSIC_FREQ, MUSICBUFLEN, get_buffer, priv);
            break;
        case I_YM2151:
            src = (sound_source_t *) sound_source_add_unmerged(YM2151_FREQ, YM2151BUFLEN, get_buffer, priv);
            break;
        case I_WT:
            src = (sound_source_t *) sound_source_add_unmerged(WT_FREQ, WTBUFLEN, get_buffer, priv);
            break;
        default:
            break;
    }

    if (src != NULL) {
        src->cls = stream + 1;

        sound_source_merge(src);

        /* Join the phase of the class, so that its blocks complete together. */
        if ((src->leader == NULL) && (peer != NULL) && (peer != src) && timer_is_enabled(&peer->timer)) {
            src->pos = (int) (((int64_t) peer->pos * src->buffer_size) / peer->buffer_size);
            timer_set_delay_u64(&src->timer, timer_get_remaining_u64(&peer->timer));
        }
    }

    return src;
}

const int *
sound_source_get_pos_ptr(void *priv)
{
    const sound_source_t *src = (sound_source_t *) priv;

    return (src == NULL) ? NULL : &sound_source_lead((sound_source_t *) src)->pos;
}

/* Safe to call on a source that has already been removed. */
void
sound_source_remove(void *priv)
{
    for (sound_source_t *src = first_sound_source; src != NULL; src = src->next) {
        if (src == (sound_source_t *) priv) {
            sound_source_close(src);
            return;
        }
    }
}

/* For when the cards are gone: whatever they have left behind. */
void
sound_source_remove_all(void)
{
    while (first_sound_source != NULL)
        sound_source_remove(first_sound_source);
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

/* Per-source gain, for the sound gain dialog. The sources of the cards come
   first, the streams that are fed directly (CD, floppy, hard disk, MIDI) last. */
static sound_source_t *
sound_source_nth(int n)
{
    for (sound_source_t *src = first_sound_source; src != NULL; src = src->next) {
        if (n-- == 0)
            return src;
    }

    return NULL;
}

static int
sound_source_card_count(void)
{
    int n = 0;

    for (const sound_source_t *src = first_sound_source; src != NULL; src = src->next)
        n++;

    return n;
}

int
sound_source_get_count(void)
{
    return sound_source_card_count() + sound_stream_extra_count();
}

/* The part of a device a source is, by what it is when the card did not say. */
static const char *
sound_source_class_what(const sound_source_t *src)
{
    switch (src->cls - 1) {
        case I_MUSIC:
        case I_YM2151:
            return "FM";
        case I_WT:
            return "Wavetable";
        default:
            return NULL;
    }
}

static void
sound_source_relabel(sound_source_t *src)
{
    const char *what = (src->what[0] != '\0') ? src->what : NULL;
    char        name[128] = { 0 };
    int         others    = 0;

    if (src->dev == NULL)
        return;

    for (const sound_source_t *o = first_sound_source; o != NULL; o = o->next) {
        if ((o != src) && (o->dev == src->dev) && (o->inst == src->inst))
            others++;
    }

    /* A device with a single source needs no telling apart. */
    if (others == 0)
        return;

    if (what == NULL)
        what = sound_source_class_what(src);

    if (what == NULL)
        return;

    device_get_name(src->dev, 0, name);
    if (src->inst > 0)
        snprintf(src->label, sizeof(src->label), "%s #%i - %s", name, src->inst + 1, what);
    else
        snprintf(src->label, sizeof(src->label), "%s - %s", name, what);
}

/* Says what part of its device a source is: "FM", "PCM"... */
void
sound_source_set_label(void *priv, const char *what)
{
    sound_source_t *src = (sound_source_t *) priv;

    if ((src == NULL) || (what == NULL))
        return;

    snprintf(src->what, sizeof(src->what), "%s", what);
}

const char *
sound_source_get_label(const int n)
{
    sound_source_t *src = (sound_source_t *) sound_source_nth(n);

    if (src == NULL)
        return sound_stream_extra_label(n - sound_source_card_count());

    sound_source_relabel(src);

    return src->label;
}

int
sound_source_get_gain(const int n)
{
    const sound_source_t *src = sound_source_nth(n);

    return (src != NULL) ? src->gain : sound_stream_extra_get_gain(n - sound_source_card_count());
}

void
sound_source_set_gain(const int n, const int gain)
{
    sound_source_t *src = sound_source_nth(n);

    if (src != NULL)
        src->gain = gain;
    else
        sound_stream_extra_set_gain(n - sound_source_card_count(), gain);
}

/* Puts the gains into the configuration. */
void
sound_source_save_gains(void)
{
    for (const sound_source_t *src = first_sound_source; src != NULL; src = src->next) {
        if (src->gain != 0)
            config_set_int("Sound gains", (char *) src->key, src->gain);
        else
            config_delete_var("Sound gains", (char *) src->key);
    }

    sound_stream_extra_save_gains();
}
