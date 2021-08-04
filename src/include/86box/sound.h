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
 *
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


enum {
    SOUND_NONE = 0,
    SOUND_INTERNAL
};


extern int	ppispeakon;
extern int	gated,
		speakval,
		speakon;

extern int	sound_pos_global;
extern int	sound_card_current;


extern void	sound_add_handler(void (*get_buffer)(int32_t *buffer, \
				  int len, void *p), void *p);
extern void	sound_set_cd_audio_filter(void (*filter)(int channel, \
					  double *buffer, void *p), void *p);

extern int	sound_card_available(int card);
#ifdef EMU_DEVICE_H
extern const device_t	*sound_card_getdevice(int card);
#endif
extern int	sound_card_has_config(int card);
extern char	*sound_card_get_internal_name(int card);
extern int	sound_card_get_from_internal_name(char *s);
extern void	sound_card_init(void);
extern void	sound_set_cd_volume(unsigned int vol_l, unsigned int vol_r);

extern void	sound_speed_changed(void);

extern void	sound_init(void);
extern void	sound_reset(void);

extern void	sound_card_reset(void);

extern void	sound_cd_thread_end(void);
extern void	sound_cd_thread_reset(void);

extern void	closeal(void);
extern void	inital(void);
extern void	givealbuffer(void *buf);
extern void	givealbuffer_cd(void *buf);


#ifdef EMU_DEVICE_H
/* AdLib and AdLib Gold */
extern const device_t adlib_device;
extern const device_t adlib_mca_device;
extern const device_t adgold_device;

/* Aztech Sound Galaxy 16 */
extern const device_t azt2316a_device;
extern const device_t azt1605_device;

/* Ensoniq AudioPCI */
extern const device_t es1371_device;
extern const device_t es1371_onboard_device;

/* Creative Labs Game Blaster */
extern const device_t cms_device;

/* Gravis UltraSound and UltraSound Max */
extern const device_t gus_device;

#if defined(DEV_BRANCH) && defined(USE_PAS16)
/* Pro Audio Spectrum 16 */
extern const device_t pas16_device;
#endif

/* Tandy PSSJ */
extern const device_t pssj_device;

/* Creative Labs Sound Blaster */
extern const device_t sb_1_device;
extern const device_t sb_15_device;
extern const device_t sb_mcv_device;
extern const device_t sb_2_device;
extern const device_t sb_pro_v1_device;
extern const device_t sb_pro_v2_device;
extern const device_t sb_pro_mcv_device;
extern const device_t sb_pro_compat_device;
extern const device_t sb_16_device;
extern const device_t sb_16_pnp_device;
extern const device_t sb_32_pnp_device;
extern const device_t sb_awe32_device;
extern const device_t sb_awe32_pnp_device;
extern const device_t sb_awe64_gold_device;

/* Innovation SSI-2001 */
extern const device_t ssi2001_device;

/* Windows Sound System */
extern const device_t wss_device;
extern const device_t ncr_business_audio_device;

/* Crystal CS423x */
extern const device_t cs4236b_device;
extern const device_t cs4237b_device;
extern const device_t cs4238b_device;
#endif

#endif	/*EMU_SOUND_H*/
