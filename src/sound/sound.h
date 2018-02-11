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
 * Version:	@(#)sound.h	1.0.4	2018/02/11
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#ifndef EMU_SOUND_H
# define EMU_SOUND_H


extern int sound_gain;

#define SOUNDBUFLEN	(48000/50)

#define CD_FREQ		44100
#define CD_BUFLEN	(CD_FREQ / 10)


extern int	ppispeakon;
extern int	gated,
		speakval,
		speakon;

extern int	sound_pos_global;
extern int	sound_card_current;


extern void	sound_add_handler(void (*get_buffer)(int32_t *buffer, \
				  int len, void *p), void *p);

extern int	sound_card_available(int card);
extern char	*sound_card_getname(int card);
#ifdef EMU_DEVICE_H
extern device_t	*sound_card_getdevice(int card);
#endif
extern int	sound_card_has_config(int card);
extern char	*sound_card_get_internal_name(int card);
extern int	sound_card_get_from_internal_name(char *s);
extern void	sound_card_init(void);
extern void	sound_set_cd_volume(unsigned int vol_l, unsigned int vol_r);

extern void	sound_speed_changed(void);

extern void	sound_realloc_buffers(void);

extern void	sound_init(void);
extern void	sound_reset(void);

extern void	sound_cd_thread_end(void);
extern void	sound_cd_thread_reset(void);

extern void	closeal(void);
extern void	initalmain(int argc, char *argv[]);
extern void	inital(void);
extern void	givealbuffer(void *buf);
extern void	givealbuffer_cd(void *buf);


#endif	/*EMU_SOUND_H*/
