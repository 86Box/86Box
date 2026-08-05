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

#include <86box/sound.h>

static const sound_backend_t sound_backend_none = {
    .name               = "None",
    .internal_name      = "none",
    .give_buffer        = NULL,
    .init_source        = NULL,
    .close_source       = NULL,
    .get_output_devices = NULL,
    .init               = NULL,
    .close              = NULL
};

typedef struct {
    const sound_backend_t *backend;
} SOUND_BACKEND;

static const SOUND_BACKEND sound_backends[] = {
    // clang-format off
    { &sound_backend_none                  },
    { &sound_backend_openal                },
#ifdef ANY_BACKENDS
    { &sound_backend_xaudio2               },
#endif
    { NULL                                 }
    // clang-format on
};

int          sound_backend_current = 0;

static void *cur_backend           = NULL;

const char *
sound_backend_get_internal_name(const int backend)
{
    return sound_backends[backend].backend->internal_name;
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
    if ((cur_backend != NULL) &&
        (sound_backends[sound_backend_current].backend) &&
        (sound_backends[sound_backend_current].backend->give_buffer != NULL))
        sound_backends[sound_backend_current].backend->give_buffer(cur_backend, src, buf, size, gain);
}

void
sound_backend_close_source(void *src)
{
    if ((cur_backend != NULL) &&
        (sound_backends[sound_backend_current].backend) &&
        (sound_backends[sound_backend_current].backend->close_source != NULL))
        sound_backends[sound_backend_current].backend->close_source(cur_backend, src);
}

void *
sound_backend_init_source(const int sample_rate, const int buffer_size)
{
    void *new_src = NULL;

    if ((cur_backend != NULL) &&
        (sound_backends[sound_backend_current].backend) &&
        (sound_backends[sound_backend_current].backend->init_source != NULL))
        new_src = sound_backends[sound_backend_current].backend->init_source(cur_backend, sample_rate, buffer_size);

    return new_src;
}

const char *
sound_backend_get_output_devices(void)
{
    const char *output_devices = NULL;

    if ((cur_backend != NULL) &&
        (sound_backends[sound_backend_current].backend) &&
        (sound_backends[sound_backend_current].backend->close != NULL))
        output_devices = sound_backends[sound_backend_current].backend->get_output_devices();

    return output_devices;
}

void
sound_backend_close(void)
{
    sound_source_close_all();

    if ((cur_backend != NULL) &&
        (sound_backends[sound_backend_current].backend) &&
        (sound_backends[sound_backend_current].backend->close != NULL))
        sound_backends[sound_backend_current].backend->close(cur_backend);

    cur_backend = NULL;
}

void
sound_backend_init(void)
{
    if ((sound_backends[sound_backend_current].backend) &&
        (sound_backends[sound_backend_current].backend->init != NULL))
        cur_backend = sound_backends[sound_backend_current].backend->init();

    sound_source_reopen_all();
}
