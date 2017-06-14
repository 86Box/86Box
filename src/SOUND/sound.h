/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Sound emulation core.
 *
 * Version:	@(#)sound.h	1.0.1	2017/06/14
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

void sound_add_handler(void (*get_buffer)(int32_t *buffer, int len, void *p), void *p);

extern int sound_card_current;

int sound_card_available(int card);
char *sound_card_getname(int card);
struct device_t *sound_card_getdevice(int card);
int sound_card_has_config(int card);
char *sound_card_get_internal_name(int card);
int sound_card_get_from_internal_name(char *s);
void sound_card_init();
void sound_set_cd_volume(unsigned int vol_l, unsigned int vol_r);

#define CD_FREQ 44100
#define CD_BUFLEN (CD_FREQ / 10)

extern int sound_pos_global;
void sound_speed_changed();

extern int sound_is_float;
void sound_realloc_buffers(void);

void sound_init();
void sound_reset();

void sound_cd_thread_reset();

void closeal(ALvoid);
void initalmain(int argc, char *argv[]);
void inital();
void givealbuffer(float *buf);
void givealbuffer_int16(int16_t *buf);
void givealbuffer_cd(float *buf);
void givealbuffer_cd_int16(int16_t *buf);
