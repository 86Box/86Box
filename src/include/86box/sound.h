/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sound emulation core.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 */

#ifndef EMU_SOUND_H
#define EMU_SOUND_H

#define SOUND_CARD_MAX 4 /* currently we support up to 4 sound cards and a standalome MPU401 */

extern int sound_gain;

#define FREQ_44100  44100
#define FREQ_48000  48000
#define FREQ_49716  49716
#define FREQ_88200  88200
#define FREQ_96000  96000

#define SOUND_FREQ  FREQ_48000
#define SOUNDBUFLEN (SOUND_FREQ / 50)

#define MUSIC_FREQ  FREQ_49716
#define MUSICBUFLEN (MUSIC_FREQ / 36)

#define CD_FREQ     FREQ_44100
#define CD_BUFLEN   (CD_FREQ / 10)

#define WT_FREQ     FREQ_44100
#define WTBUFLEN    (MUSIC_FREQ / 45)

enum {
    SOUND_NONE = 0,
    SOUND_INTERNAL
};

extern int ppispeakon;
extern int gated;
extern int speakval;
extern int speakon;

extern int sound_pos_global;

extern int music_pos_global;
extern int wavetable_pos_global;

extern int sound_card_current[SOUND_CARD_MAX];

extern void sound_add_handler(void (*get_buffer)(int32_t *buffer,
                                                 int len, void *priv),
                              void *priv);

extern void music_add_handler(void (*get_buffer)(int32_t *buffer,
                                                 int len, void *priv),
                              void *priv);

extern void wavetable_add_handler(void (*get_buffer)(int32_t *buffer,
                                                     int len, void *priv),
                                  void *priv);

extern void sound_set_cd_audio_filter(void (*filter)(int     channel,
                                                     double *buffer, void *priv),
                                      void *priv);
extern void sound_set_pc_speaker_filter(void (*filter)(int     channel,
                                                       double *buffer, void *priv),
                                        void *priv);

extern void (*filter_pc_speaker)(int channel, double *buffer, void *priv);
extern void *filter_pc_speaker_p;

extern int sound_card_available(int card);
#ifdef EMU_DEVICE_H
extern const device_t *sound_card_getdevice(int card);
#endif
extern int         sound_card_has_config(int card);
extern const char *sound_card_get_internal_name(int card);
extern int         sound_card_get_from_internal_name(const char *s);
extern void        sound_card_init(void);
extern void        sound_set_cd_volume(unsigned int vol_l, unsigned int vol_r);

extern void sound_speed_changed(void);

extern void sound_init(void);
extern void sound_reset(void);

extern void sound_card_reset(void);

extern void sound_cd_thread_end(void);
extern void sound_cd_thread_reset(void);

extern void closeal(void);
extern void inital(void);
extern void givealbuffer(const void *buf);
extern void givealbuffer_music(const void *buf);
extern void givealbuffer_wt(const void *buf);
extern void givealbuffer_cd(const void *buf);

#define sb_vibra16c_onboard_relocate_base sb_vibra16s_onboard_relocate_base
#define sb_vibra16cl_onboard_relocate_base sb_vibra16s_onboard_relocate_base
#define sb_vibra16xv_onboard_relocate_base sb_vibra16s_onboard_relocate_base
extern void sb_vibra16s_onboard_relocate_base(uint16_t new_addr, void *priv);

#ifdef EMU_DEVICE_H
/* AdLib and AdLib Gold */
extern const device_t adlib_device;
extern const device_t adlib_mca_device;
extern const device_t adgold_device;

/* Aztech Sound Galaxy 16 */
extern const device_t azt2316a_device;
extern const device_t acermagic_s20_device;
extern const device_t mirosound_pcm10_device;
extern const device_t azt1605_device;

/* C-Media CMI8x38 */
extern const device_t cmi8338_device;
extern const device_t cmi8338_onboard_device;
extern const device_t cmi8738_device;
extern const device_t cmi8738_onboard_device;
extern const device_t cmi8738_6ch_onboard_device;

/* Creative Labs Game Blaster */
extern const device_t cms_device;

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
extern const device_t sb_vibra16c_onboard_device;
extern const device_t sb_vibra16c_device;
extern const device_t sb_vibra16cl_onboard_device;
extern const device_t sb_vibra16cl_device;
extern const device_t sb_vibra16s_onboard_device;
extern const device_t sb_vibra16s_device;
extern const device_t sb_vibra16xv_onboard_device;
extern const device_t sb_vibra16xv_device;
extern const device_t sb_16_pnp_device;
extern const device_t sb_16_pnp_ide_device;
extern const device_t sb_16_compat_device;
extern const device_t sb_16_compat_nompu_device;
extern const device_t sb_16_reply_mca_device;
extern const device_t sb_goldfinch_device;
extern const device_t sb_32_pnp_device;
extern const device_t sb_awe32_device;
extern const device_t sb_awe32_pnp_device;
extern const device_t sb_awe64_value_device;
extern const device_t sb_awe64_device;
extern const device_t sb_awe64_ide_device;
extern const device_t sb_awe64_gold_device;

/* Crystal CS423x */
extern const device_t cs4235_device;
extern const device_t cs4235_onboard_device;
extern const device_t cs4236_onboard_device;
extern const device_t cs4236b_device;
extern const device_t cs4236b_onboard_device;
extern const device_t cs4237b_device;
extern const device_t cs4238b_device;

/* ESS Technology */
extern const device_t ess_688_device;
extern const device_t ess_ess0100_pnp_device;
extern const device_t ess_1688_device;
extern const device_t ess_ess0102_pnp_device;
extern const device_t ess_ess0968_pnp_device;
extern const device_t ess_soundpiper_16_mca_device;
extern const device_t ess_soundpiper_32_mca_device;
extern const device_t ess_chipchat_16_mca_device;

/* Ensoniq AudioPCI */
extern const device_t es1370_device;
extern const device_t es1371_device;
extern const device_t es1371_onboard_device;
extern const device_t es1373_device;
extern const device_t es1373_onboard_device;
extern const device_t ct5880_device;
extern const device_t ct5880_onboard_device;

/* Gravis UltraSound and UltraSound Max */
extern const device_t gus_device;

/* IBM PS/1 Audio Card */
extern const device_t ps1snd_device;

/* Innovation SSI-2001 */
extern const device_t ssi2001_device;
extern const device_t entertainer_device;

/* Pro Audio Spectrum Plus, 16, and 16D */
extern const device_t pasplus_device;
extern const device_t pas16_device;
extern const device_t pas16d_device;

/* Tandy PSSJ */
extern const device_t pssj_device;
extern const device_t pssj_isa_device;

/* Tandy PSG */
extern const device_t tndy_device;

/* Windows Sound System */
extern const device_t wss_device;
extern const device_t ncr_business_audio_device;

#ifdef USE_LIBSERIALPORT
/* External Audio device OPL2Board (Host Connected hardware)*/
extern const device_t opl2board_device;
#endif 

#endif

#endif /*EMU_SOUND_H*/
