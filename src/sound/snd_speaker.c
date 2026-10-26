/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the PC speaker.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdint.h>
#include <stdlib.h>
#include <86box/device.h>
#include <86box/snd_speaker.h>
#include <86box/sound.h>

int speaker_mute       = 0;
int speaker_gated      = 0;
int speaker_enable     = 0;
int was_speaker_enable = 0;

int speakon;

typedef struct speaker_t {
    int32_t buffer[MUSICBUFLEN];
    int     pos;

    uint8_t mode;
    double  count;
} speaker_t;

speaker_t *speaker = NULL;

void
speaker_set_count(const uint8_t new_m, const int new_count)
{
    if (speaker != NULL) {
        speaker->mode  = new_m;
        speaker->count = (double) new_count;
    }
}

void
speaker_update(void)
{
    if (speaker != NULL) {
        double amplitude = ((speaker->count / 256.0) * 10240.0) - 5120.0;

        if (amplitude > 5120.0)
            amplitude = 5120.0;

        if (speaker->pos < music_pos_global) {
            int32_t val;

            for (; speaker->pos < music_pos_global; speaker->pos++) {
                if (speaker_gated && was_speaker_enable) {
                    if ((speaker->mode == 0) || (speaker->mode == 4))
                        val = (int32_t) amplitude;
                    else if (speaker->count < 64.0)
                        val = 0xa00;
                    else
                        val = speakon ? 0x1400 : 0;
                } else {
                    if (speaker->mode == 1)
                        val = was_speaker_enable ? (int32_t) amplitude : 0;
                    else
                        val = was_speaker_enable ? 0x1400 : 0;
                }

                if (!speaker_enable)
                    was_speaker_enable = 0;

                speaker->buffer[speaker->pos] = val;
            }
        }
    }
}

void
speaker_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    speaker_t *dev   = (speaker_t *) priv;

    double     val_l;
    double     val_r;

    speaker_update();

    if (!speaker_mute) {
        for (uint16_t c = 0; c < len * 2; c += 2) {
            val_l = val_r = (double) dev->buffer[c >> 1];
            /* Apply PC speaker volume and filters */
            if (filter_pc_speaker != NULL) {
                filter_pc_speaker(0, &val_l, filter_pc_speaker_p);
                filter_pc_speaker(1, &val_r, filter_pc_speaker_p);
            }
            buffer[c] += (int32_t) val_l;
            buffer[c + 1] += (int32_t) val_r;
        }
    }

    dev->pos = 0;
}

static void
speaker_close(void *priv)
{
    speaker_t *dev = (speaker_t *) priv;

    free(dev);

    speaker = NULL;
}

static void *
speaker_init(const device_t *info)
{
    speaker_t *dev     = (speaker_t *) calloc(1, sizeof(speaker_t));

    music_add_handler(speaker_get_buffer, dev);

    speaker_mute       = 0;
    speaker_gated      = 0;
    speaker_enable     = 0;
    was_speaker_enable = 0;

    dev->count         = 65535.0;

    speaker            = dev;

    return dev;
}

const device_t speaker_device = {
    .name          = "PC Speaker",
    .internal_name = "speaker",
    .flags         = 0,
    .local         = 0,
    .init          = speaker_init,
    .close         = speaker_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
