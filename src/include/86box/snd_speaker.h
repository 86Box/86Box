/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header of the emulation of the PC speaker.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */

#ifndef SOUND_SPEAKER_H
#define SOUND_SPEAKER_H

extern int speaker_mute;

extern int speaker_gated;
extern int speaker_enable, was_speaker_enable;

extern void speaker_init();

extern void speaker_set_count(uint8_t new_m, int new_count);
extern void speaker_update(void);

#endif /*SOUND_SPEAKER_H*/
