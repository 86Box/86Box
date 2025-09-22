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
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/snd_speaker.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

int speaker_mute       = 0;
int speaker_gated      = 0;
int speaker_enable     = 0;
int was_speaker_enable = 0;

int gated;
int speakval;
int speakon;

static int32_t speaker_buffer[SOUNDBUFLEN];
static int     speaker_pos = 0;

static uint8_t speaker_mode  = 0;
static double  speaker_count = 65535.0;

void
speaker_set_count(uint8_t new_m, int new_count)
{
    speaker_mode  = new_m;
    speaker_count = (double) new_count;
}

void
speaker_update(void)
{
    int32_t val;
    double  amplitude;

    amplitude = ((speaker_count / 256.0) * 10240.0) - 5120.0;

    if (amplitude > 5120.0)
        amplitude = 5120.0;

    if (speaker_pos < sound_pos_global) {
        for (; speaker_pos < sound_pos_global; speaker_pos++) {
            if (speaker_gated && was_speaker_enable) {
                if ((speaker_mode == 0) || (speaker_mode == 4))
                    val = (int32_t) amplitude;
                else if (speaker_count < 64.0)
                    val = 0xa00;
                else
                    val = speakon ? 0x1400 : 0;
            } else {
                if (speaker_mode == 1)
                    val = was_speaker_enable ? (int32_t) amplitude : 0;
                else
                    val = was_speaker_enable ? 0x1400 : 0;
            }

            if (!speaker_enable)
                was_speaker_enable = 0;

            speaker_buffer[speaker_pos] = val;
        }
    }
}

void
speaker_get_buffer(int32_t *buffer, int len, UNUSED(void *priv))
{
    double val_l, val_r;

    speaker_update();

    if (!speaker_mute) {
        for (int c = 0; c < len * 2; c += 2) {
            val_l = val_r = (double) speaker_buffer[c >> 1];
            /* Apply PC speaker volume and filters */
            if (filter_pc_speaker != NULL) {
                filter_pc_speaker(0, &val_l, filter_pc_speaker_p);
                filter_pc_speaker(1, &val_r, filter_pc_speaker_p);
            }
            buffer[c] += (int32_t) val_l;
            buffer[c + 1] += (int32_t) val_r;
        }
    }

    speaker_pos = 0;
}

void
speaker_init(void)
{
    memset(speaker_buffer, 0, sizeof(speaker_buffer));
    sound_add_handler(speaker_get_buffer, NULL);
    speaker_mute = 0;
}
