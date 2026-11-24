/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sound backend management.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2026 Miran Grca.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/thread.h>

static const sound_backend_t sound_backend_none = {
    .name               = "None",
    .internal_name      = "none"
};

typedef struct {
    const sound_backend_t *backend;
} SOUND_BACKEND;

static const SOUND_BACKEND sound_backends[] = {
    // clang-format off
    { &sound_backend_none                  },
#ifdef SND_BACKEND_OPENAL
    { &sound_backend_openal                },
#endif
#ifdef SND_BACKEND_XAUDIO2
    { &sound_backend_xaudio2               },
#endif
#ifdef SND_BACKEND_AUDIO4
    { &sound_backend_audio4                },
#endif
#ifdef SND_BACKEND_SNDIO
    { &sound_backend_sndio                 },
#endif
    { NULL                                 }
    // clang-format on
};

int           sound_backend_current = 0;

static void * cur_backend           = NULL;
static void * cur_input             = NULL;
static mutex_t *input_mutex         = NULL;

/* The backend the current handles belong to; sound_backend_current may already
   point at a newly selected one while the old one is still open. */
static int    active_backend        = 0;

/* Input may be provided by another backend than the output one (XAudio2 and
   FAudio can not capture, so OpenAL is used for that). */
static int    input_backend         = -1;
static void * input_handle          = NULL;
static int    input_own_handle      = 0;

#define IN_BACKEND (((input_backend >= 0) ? sound_backends[input_backend].backend : NULL))

#define CUR_BACKEND (sound_backends[active_backend].backend)

int
sound_backend_get_count(void)
{
    int c = 0;

    while (sound_backends[c].backend != NULL)
        c++;

    return c;
}

static int
sound_backend_valid(const int backend)
{
    return (backend >= 0) && (backend < sound_backend_get_count());
}

const char *
sound_backend_get_name(const int backend)
{
    return sound_backend_valid(backend) ? sound_backends[backend].backend->name : "None";
}

/* The first real backend, or None if there is none. */
int
sound_backend_get_default(void)
{
    return (sound_backend_get_count() > 1) ? 1 : 0;
}

const char *
sound_backend_get_output_devices_of(const int backend)
{
    if (sound_backend_valid(backend) && (sound_backends[backend].backend->get_output_devices != NULL))
        return sound_backends[backend].backend->get_output_devices();

    return NULL;
}

static int
sound_backend_input_provider(const int backend)
{
    if (!sound_backend_valid(backend))
        return -1;

    if (sound_backends[backend].backend->init_input != NULL)
        return backend;

    for (int c = 0; sound_backends[c].backend != NULL; c++) {
        if ((sound_backends[c].backend->init_input != NULL) &&
            !strcmp(sound_backends[c].backend->internal_name, "openal"))
            return c;
    }

    return -1;
}

const char *
sound_backend_get_input_devices_of(const int backend)
{
    const int prov = sound_backend_input_provider(backend);

    if ((prov >= 0) && (sound_backends[prov].backend->get_input_devices != NULL))
        return sound_backends[prov].backend->get_input_devices();

    return NULL;
}

int
sound_backend_get_supported_rates_of(const int backend, const char *device_name,
                                     int *rates_out, const int max_rates)
{
    int count = 0;

    if (sound_backend_valid(backend) && (sound_backends[backend].backend->get_device_supported_rates != NULL))
        count = sound_backends[backend].backend->get_device_supported_rates(device_name, rates_out, max_rates);

    if (count == 0) {
        /* The backend can not tell, so offer the usual ones. */
        if (max_rates > 0)
            rates_out[count++] = FREQ_44100;
        if (max_rates > 1)
            rates_out[count++] = FREQ_48000;
    }

    return count;
}

const char *
sound_backend_get_internal_name(const int backend)
{
    return sound_backend_valid(backend) ? sound_backends[backend].backend->internal_name : "none";
}

int
sound_backend_get_from_internal_name(const char *s)
{
    int c = 0;

    while (sound_backends[c].backend != NULL) {
        if (!strcmp(sound_backends[c].backend->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

void
sound_backend_give_buffer(void *src, const void *buf, const int size, const int gain)
{
    if ((src != NULL) && (cur_backend != NULL) &&
        (CUR_BACKEND) &&
        (CUR_BACKEND->give_buffer != NULL))
        CUR_BACKEND->give_buffer(cur_backend, src, buf, size, gain);
}

void
sound_backend_close_source(void *src)
{
    if ((src != NULL) && (cur_backend != NULL) &&
        (CUR_BACKEND) &&
        (CUR_BACKEND->close_source != NULL))
        CUR_BACKEND->close_source(cur_backend, src);
}

void *
sound_backend_init_source(const int sample_rate, const int buffer_size)
{
    void *new_src = NULL;

    if ((cur_backend != NULL) &&
        (CUR_BACKEND) &&
        (CUR_BACKEND->init_source != NULL))
        new_src = CUR_BACKEND->init_source(cur_backend, sample_rate, buffer_size);

    return new_src;
}

void
sound_backend_close(void)
{
    sound_source_close_all();

    sound_backend_input_close();

    if ((cur_backend != NULL) &&
        (CUR_BACKEND) &&
        (CUR_BACKEND->close != NULL))
        CUR_BACKEND->close(cur_backend);

    cur_backend = NULL;
}

void
sound_backend_init(void)
{
    if (!sound_backend_valid(sound_backend_current))
        sound_backend_current = 0;

    if (input_mutex == NULL)
        input_mutex = thread_create_mutex();

    active_backend = sound_backend_current;

    if ((CUR_BACKEND != NULL) && (CUR_BACKEND->init != NULL))
        cur_backend = CUR_BACKEND->init();

    sound_source_reopen_all();
}

/* Input. The capture handle is shared with the sound poll thread, hence the
   mutex. */
static void
sound_backend_input_lock(void)
{
    if (input_mutex != NULL)
        thread_wait_mutex(input_mutex);
}

static void
sound_backend_input_unlock(void)
{
    if (input_mutex != NULL)
        thread_release_mutex(input_mutex);
}

void
sound_backend_input_open(void)
{
    /* Opening a capture device can take a while, so do it without holding
       the lock the emulation thread needs to poll the input. */
    sound_backend_input_lock();
    const int prov = sound_backend_input_provider(active_backend);
    const int need = (cur_input == NULL) && sound_input_enabled && (prov >= 0);
    const int cur_active = active_backend;
    sound_backend_input_unlock();

    if (!need)
        return;

    const sound_backend_t *be     = sound_backends[prov].backend;
    void                  *handle = cur_backend;
    int                    own    = 0;

    if (prov != cur_active) {
        if (be->init != NULL)
            handle = be->init();
        own = 1;
        if (handle == NULL)
            return;
    }

    void *in = be->init_input(handle, (sb_input_rate == FREQ_48000) ? FREQ_48000 : FREQ_44100);

    sound_backend_input_lock();
    if ((in != NULL) && (cur_input == NULL) && (active_backend == cur_active)) {
        cur_input        = in;
        input_backend    = prov;
        input_handle     = handle;
        input_own_handle = own;
        sound_backend_input_unlock();
        return;
    }
    sound_backend_input_unlock();

    if ((in != NULL) && (be->close_input != NULL))
        be->close_input(handle, in);
    if (own && (be->close != NULL))
        be->close(handle);
}

void
sound_backend_input_close(void)
{
    sound_backend_input_lock();

    if (cur_input != NULL) {
        const sound_backend_t *be = IN_BACKEND;

        if ((be != NULL) && (be->close_input != NULL))
            be->close_input(input_handle, cur_input);
        if (input_own_handle && (be != NULL) && (be->close != NULL))
            be->close(input_handle);

        cur_input        = NULL;
        input_handle     = NULL;
        input_backend    = -1;
        input_own_handle = 0;
    }

    sound_backend_input_unlock();
}

int
sound_backend_input_available(void)
{
    sound_backend_input_lock();
    const int available = (cur_input != NULL);
    sound_backend_input_unlock();

    return available;
}

void
sound_backend_input_start(void)
{
    sound_backend_input_lock();

    if ((cur_input != NULL) && (IN_BACKEND->start_input != NULL))
        IN_BACKEND->start_input(input_handle, cur_input);

    sound_backend_input_unlock();
}

void
sound_backend_input_stop(void)
{
    sound_backend_input_lock();

    if ((cur_input != NULL) && (IN_BACKEND->stop_input != NULL))
        IN_BACKEND->stop_input(input_handle, cur_input);

    sound_backend_input_unlock();
}

int
sound_backend_input_get_rate(void)
{
    int rate = 0;

    sound_backend_input_lock();

    if ((cur_input != NULL) && (IN_BACKEND->get_input_rate != NULL))
        rate = IN_BACKEND->get_input_rate(input_handle, cur_input);

    sound_backend_input_unlock();

    return rate;
}

void
sound_backend_input_get_data(int16_t *buf, size_t *len)
{
    if ((buf == NULL) || (len == NULL))
        return;

    sound_backend_input_lock();

    if ((cur_input != NULL) && (IN_BACKEND->get_input_data != NULL))
        IN_BACKEND->get_input_data(input_handle, cur_input, buf, len);
    else
        *len = 0;

    sound_backend_input_unlock();
}
