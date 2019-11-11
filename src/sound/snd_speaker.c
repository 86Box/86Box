/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the PC speaker.
 *
 * Version:	@(#)snd_speaker.c	1.0.0	2019/11/11
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../timer.h"
#include "../pit.h"
#include "sound.h"
#include "snd_speaker.h"


int speaker_mute = 0, speaker_gated = 0;
int speaker_enable = 0, was_speaker_enable = 0;

int gated,speakval,speakon;


static int32_t speaker_buffer[SOUNDBUFLEN];
static int speaker_pos = 0;


void
speaker_update(void)
{
    int32_t val;
    double timer2_count, amplitude;

    timer2_count = pit.l[2] ? ((double) pit.l[2]) : 65536.0;
    amplitude = ((timer2_count / 64.0) * 10240.0) - 5120.0;

    if (amplitude > 5120.0)
	amplitude = 5120.0;

    if (speaker_pos >= sound_pos_global)
	return;

    for (; speaker_pos < sound_pos_global; speaker_pos++) {
	if (speaker_gated && was_speaker_enable) {
		if ((pit.m[2] == 0) || (pit.m[2] == 4))
			val = (int32_t) amplitude;
		else if (pit.l[2] < 0x40)
			val = 0xa00;
		else
			val = speakon ? 0x1400 : 0;
	} else {
		if (pit.m[2] == 1)
			val = was_speaker_enable ? (int32_t) amplitude : 0;
		else
			val = was_speaker_enable ? 0x1400 : 0;
	}

	if (!speaker_enable)
		was_speaker_enable = 0;

	speaker_buffer[speaker_pos] = val;
    }
}


void
speaker_get_buffer(int32_t *buffer, int len, void *p)
{
    int32_t c, val;

    speaker_update();

    if (!speaker_mute) {
	for (c = 0; c < len * 2; c += 2) {
		val = speaker_buffer[c >> 1];
		buffer[c] += val;
		buffer[c + 1] += val;
	}
    }

    speaker_pos = 0;
}


void
speaker_init(void)
{
    sound_add_handler(speaker_get_buffer, NULL);
    speaker_mute = 0;
}
