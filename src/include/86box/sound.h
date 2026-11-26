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
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2024-2026 Jasmine Iwanek.
 */
#ifndef EMU_SOUND_H
#define EMU_SOUND_H

#include <86box/timer.h>

#define SOUND_CARD_MAX 4 /* currently we support up to 4 sound cards and a standalone MPU401 */

typedef struct _sound_backend_ {
    const char *name;
    const char *internal_name;

    /* Output. Sizes are in values (2 per stereo frame). */
    void        (*give_buffer)(void *priv, void *src, const void *buf, int size, int gain);
    void *      (*init_source)(void *priv, int sample_rate, int buffer_size);
    void        (*close_source)(void *priv, void *src);
    const char *(*get_output_devices)(void);
    void       *(*init)(void);
    void        (*close)(void *priv);

    /* Input. Captured data is always interleaved 16-bit stereo. */
    const char *(*get_input_devices)(void);
    void *      (*init_input)(void *priv, int sample_rate);
    void        (*close_input)(void *priv, void *in);
    void        (*start_input)(void *priv, void *in);
    void        (*stop_input)(void *priv, void *in);
    int         (*get_input_rate)(void *priv, void *in);
    void        (*get_input_data)(void *priv, void *in, int16_t *buf, size_t *len);

    /* Probes which of the candidate rates a device supports; returns count. */
    int         (*get_device_supported_rates)(const char *device_name, int *rates_out, int max);
} sound_backend_t;

extern const sound_backend_t sound_backend_audio4;
extern const sound_backend_t sound_backend_openal;
extern const sound_backend_t sound_backend_xaudio2;
extern const sound_backend_t sound_backend_sndio;

extern int                   sound_backend_current;

extern int                   sound_backend_get_count(void);
extern const char *          sound_backend_get_name(int backend);
extern const char *          sound_backend_get_internal_name(int backend);
extern int                   sound_backend_get_from_internal_name(const char *s);
extern int                   sound_backend_get_default(void);
extern const char *          sound_backend_get_output_devices_of(int backend);
extern const char *          sound_backend_get_input_devices_of(int backend);
extern int                   sound_backend_get_supported_rates_of(int backend, const char *device_name,
                                                                  int *rates_out, int max_rates);

extern void                  sound_backend_give_buffer(void *src, const void *buf,
                                                       int size, int gain);
extern void                  sound_backend_close_source(void *src);
extern void *                sound_backend_init_source(int sample_rate, int buffer_size);
extern void                  sound_backend_close(void);
extern void                  sound_backend_init(void);

extern void                  sound_backend_input_open(void);
extern void                  sound_backend_input_close(void);
extern int                   sound_backend_input_available(void);
extern void                  sound_backend_input_start(void);
extern void                  sound_backend_input_stop(void);
extern int                   sound_backend_input_get_rate(void);
extern void                  sound_backend_input_get_data(int16_t *buf, size_t *len);

/* Sources owned by sound cards: each has its own clock and timer. get_buffer
   is called once per block, when the clock reaches the block's length. */
extern void *          sound_source_add(int sample_rate, int frames,
                                        void (*get_buffer)(int32_t *buffer, uint16_t len, void *priv),
                                        void *priv);
extern void *          sound_source_add_stream(uint8_t stream,
                                               void (*get_buffer)(int32_t *buffer, uint16_t len, void *priv),
                                               void *priv);
extern const int *     sound_source_get_pos_ptr(void *src);
extern void            sound_source_set_rate(void *src, double rate);
extern void            sound_source_set_rate_by_clock(const int *clock, double rate);
extern int             sound_source_get_rate(void *src);
extern int             sound_source_class_pending(uint8_t stream);
extern uint64_t        sound_source_offset_delay(pc_timer_t *timer, uint64_t latch);

/* The position of a card-owned source's clock, or fallback (a shared clock)
   for a card that has none. */
#define SOUND_CLOCK(clk, fallback) (((clk) != NULL) ? *(clk) : (fallback))
extern void            sound_source_remove(void *src);
extern int             sound_source_get_count(void);
extern const char *    sound_source_get_label(int n);
extern int             sound_source_get_gain(int n);
extern void            sound_source_set_gain(int n, int gain);
extern void            sound_source_save_gains(void);
extern int             sound_stream_extra_count(void);
extern const char *    sound_stream_extra_label(int n);
extern int             sound_stream_extra_get_gain(int n);
extern void            sound_stream_extra_set_gain(int n, int gain);
extern void            sound_stream_extra_save_gains(void);
extern void            sound_source_remove_all(void);

extern void            sound_source_close_all(void);
extern void            sound_source_reopen_all(void);

extern int  sound_gain;
extern char sound_output_device[512]; /* selected audio output device name, empty = system default */

enum {
    I_NORMAL = 0,
    I_MUSIC,
    I_WT,
    I_CD,
    I_FDD,
    I_HDD,
    I_YM2151,
    I_MIDI,
    I_MAX
};

#define FREQ_44100  44100
#define FREQ_48000  48000
#define FREQ_49716  49716
#define FREQ_55930  55930
#define FREQ_88200  88200
#define FREQ_96000  96000

#define SOUND_FREQ   FREQ_48000
#define SOUNDBUFLEN  (SOUND_FREQ / 50)
#define SOUND_MIN_RATE 4000

#define MUSIC_FREQ   FREQ_49716
#define MUSICBUFLEN  (MUSIC_FREQ / 36)

#define YM2151_FREQ  FREQ_55930
#define YM2151BUFLEN (YM2151_FREQ / 70)

#define CD_FREQ      FREQ_44100
#define CD_BUFLEN    (CD_FREQ / 10)

#define WT_FREQ      FREQ_44100
#define WTBUFLEN     (WT_FREQ / 45)

enum {
    SOUND_NONE = 0,
    SOUND_INTERNAL
};

extern int ppispeakon;
extern int speakon;

extern int midi_freq;
extern int midi_buf_size;

extern int sound_pos_global;
extern int music_pos_global;
extern int ym2151_pos_global;
extern int wavetable_pos_global;

extern int sound_card_current[SOUND_CARD_MAX];


extern void sound_in_add_handler(void (*put_buffer)(int16_t *buffer,
                                                     int len, void *priv),
                                 void *priv);
extern void sound_in_remove_handler(void (*put_buffer)(int16_t *buffer,
                                                        int len, void *priv),
                                    void *priv);

struct pc_timer_t;
extern uint64_t sound_poll_offset_delay(struct pc_timer_t *timer, uint64_t latch);

extern void sound_in_start_input(void);
extern void sound_in_stop_input(void);
extern void sound_in_set_test_tone(double hz, double dbfs); /* automation "audio tone" */




extern void sound_set_cd_audio_filter(void (*filter)(int     channel,
                                                     double *buffer, void *priv),
                                      void *priv);
extern void sound_set_pc_speaker_filter(void (*filter)(int     channel,
                                                       double *buffer, void *priv),
                                        void *priv);
extern void sound_set_midi_filter(void (*filter)(int     channel,
                                                 double *buffer, void *priv),
                                  void *priv);

extern void (*filter_pc_speaker)(int channel, double *buffer, void *priv);
extern void *filter_pc_speaker_p;

extern void (*filter_midi)(int channel, double *buffer, void *priv);
extern void *filter_midi_p;

extern int sound_card_available(int card);
#ifdef EMU_DEVICE_H
extern const device_t *sound_card_getdevice(int card);
#endif
extern int         sound_card_has_config(int card);
extern int         sound_card_has_input(int card);
extern const char *sound_card_get_internal_name(int card);
extern int         sound_card_get_from_internal_name(const char *s);
extern void        sound_card_init(void);

extern void sound_speed_changed(void);

extern void sound_init(void);
extern void sound_reset(void);

extern void sound_card_reset(void);

extern void sound_recalc_timers(void);
extern void sound_close(void);

extern void sound_cd_thread_end(void);
extern void sound_cd_thread_reset(void);

extern void sound_fdd_thread_init(void);
extern void sound_fdd_thread_end(void);

extern void sound_hdd_thread_init(void);
extern void sound_hdd_thread_end(void);

extern void        sound_output_init(void);
extern void        sound_output_close(void);
extern void        al_set_midi(int freq, int buf_size);
extern void        sound_reopen_input(void);
extern void        sound_reopen_output(void);

extern unsigned long long src_freqs[I_MAX];

/* Hands a block to the host backend, and first to the automation socket's
   audio capture when it is built in. size counts values (2 per frame). */
extern void        sound_give(const void *buf, const uint8_t src, const int size);
extern int         sound_pending_frames(const uint8_t src);
extern int         sound_stream_rate(const uint8_t src);

#define givealbuffer(b)         sound_give(b, I_NORMAL, (sound_sample_rate / 50) << 1)
#define givealbuffer_music(b)   sound_give(b, I_MUSIC, MUSICBUFLEN << 1)
#define givealbuffer_ym2151(b)  sound_give(b, I_YM2151, YM2151BUFLEN << 1)
#define givealbuffer_wt(b)      sound_give(b, I_WT, WTBUFLEN << 1)
#define givealbuffer_cd(b)      sound_give(b, I_CD, CD_BUFLEN << 1)
#define givealbuffer_fdd(b, s)  sound_give(b, I_FDD, s)
#define givealbuffer_hdd(b, s)  sound_give(b, I_HDD, s)
#define givealbuffer_midi(b, s) sound_give(b, I_MIDI, s)

#define sb_vibra16c_onboard_relocate_base sb_vibra16s_onboard_relocate_base
#define sb_vibra16cl_onboard_relocate_base sb_vibra16s_onboard_relocate_base
#define sb_vibra16xv_onboard_relocate_base sb_vibra16s_onboard_relocate_base
extern void sb_vibra16s_onboard_relocate_base(uint16_t new_addr, void *priv);

#ifdef EMU_DEVICE_H
/* AdLib and AdLib Gold */
extern const device_t adlib_device;
extern const device_t adlib_mca_device;
extern const device_t adgold_device;

/* Analog Devices AD1816 */
extern const device_t ad1816_device;

/* Aztech Sound Galaxy 16 */
extern const device_t azt2316a_device;
extern const device_t azt1605_device;
extern const device_t aztpr16_device;
extern const device_t azt2316r_device;
extern const device_t azt2320_device;

/* C-Media CMI8x38 */
extern const device_t cmi8338_device;
extern const device_t cmi8338_onboard_device;
extern const device_t cmi8738_device;
extern const device_t cmi8738_onboard_device;
extern const device_t cmi8738_6ch_onboard_device;

/* Covox ISA */
extern const device_t voicemasterkey_device;
extern const device_t soundmaster_device;
extern const device_t soundmasterplus_device;
extern const device_t isadacr0_device;
extern const device_t isadacr1_device;

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
extern const device_t sb_awe32_ide_pnp_device;
extern const device_t sb_awe64_value_device;
extern const device_t sb_awe64_device;
extern const device_t sb_awe64_ide_device;
extern const device_t sb_awe64_gold_device;
extern const device_t sb_awe64d_device;

/* Crystal CS423x */
extern const device_t cs4232_device;
extern const device_t cs4232_onboard_device;
extern const device_t cs4235_device;
extern const device_t cs4235_onboard_device;
extern const device_t cs4236_onboard_device;
extern const device_t cs4236b_device;
extern const device_t cs4236b_onboard_device;
extern const device_t cs4237b_device;
extern const device_t cs4238b_device;

/* ESS Technology */
extern const device_t ess_488_device;
extern const device_t ess_1488_device;
extern const device_t ess_688_device;
extern const device_t ess_ess0100_pnp_device;
extern const device_t ess_ess0968_pnp_688_device;
extern const device_t ess_1688_device;
extern const device_t ess_1688_compaq_device;
extern const device_t ess_ess0102_pnp_device;
extern const device_t ess_ess0968_pnp_device;
extern const device_t ess_soundpiper_16_mca_device;
extern const device_t ess_soundpiper_32_mca_device;
extern const device_t ess_chipchat_16_mca_device;
extern const device_t ess_1788_device;
extern const device_t ess_1888_device;
extern const device_t ess_1888_compaq_device;
extern const device_t ess_1887_device;
extern const device_t ess_1868_device;
extern const device_t ess_1869_device;
extern const device_t ess_1869_onboard_device;
extern const device_t ess_solo1_device;
extern const device_t ess_solo1_onboard_device;

/* Ensoniq AudioPCI */
extern const device_t es1370_device;
/* Aureal Vortex 2 */
extern const device_t vortex2_device;

extern const device_t es1371_device;
extern const device_t es1371_onboard_device;
extern const device_t es1373_device;
extern const device_t es1373_onboard_device;
extern const device_t ct5880_device;
extern const device_t ct5880_onboard_device;

/* Ensoniq Soundscape */
extern const device_t sscape_device;

/* Gravis UltraSound family */
extern const device_t gus_device;
extern const device_t gus_v34_device;
extern const device_t gus_v37_device;
extern const device_t gus_max_device;
extern const device_t gus_ace_device;
extern const device_t gus_extreme_device;
extern const device_t gus_vipermax_device;
extern const device_t gus_pnp_device;
extern const device_t gus_pnp_new_device;
extern const device_t gus_pnp_nocd_device;
extern const device_t gus_pnp_compaq_device;

/* IBM M-ACPA/A (Audio Capture & Playback Adapter) */
extern const device_t acpa_mca_device;

/* IBM PS/2 Speech Adapter */
extern const device_t ibm_speech_device;

/* IBM Music Feature Card */
extern const device_t imfc_device;

/* IBM PS/1 Audio Card */
extern const device_t ps1snd_device;
extern const device_t ps1midi_device;

/* Innovation SSI-2001 */
extern const device_t ssi2001_device;
extern const device_t entertainer_device;

/* Mindscape Music Board */
extern const device_t mmb_device;

/* Media Vision */
extern const device_t thunderboard_device;
extern const device_t jazz16_device;

/* Oak Technology OTI-601 */
extern const device_t mozart_device;

/* OPTi 82c93x */
extern const device_t acermagic_s20_device;
extern const device_t mirosound_pcm10_device;
extern const device_t opti_82c930_device;
extern const device_t opti_82c931_device;

/* PC Speaker */
extern const device_t speaker_device;

/* Pro Audio Spectrum, Plus, 16, and 16D */
extern const device_t pas_device;
extern const device_t pasplus_device;
extern const device_t pas16_device;
extern const device_t pas16d_device;

/* Rainbow Arts PC-Soundman */
extern const device_t soundman_device;

/* Tandy PSSJ */
extern const device_t pssj_device;
extern const device_t pssj_isa_device;
extern const device_t pssj_1e0_device;

/* Tandy PSG */
extern const device_t tndy_device;

/* Tandy Sensation */
#define SENSATION_TANDY_VIS 0x01 /* VIS wiring: VIS DAC on IRQ 7 */

extern const device_t sensationaud_device;

/* TexElec SAAYM */
extern const device_t saaym_device;

/* Windows Sound System */
extern const device_t wss_device;
extern const device_t ncr_business_audio_device;

/* Yamaha YMF-7xx */
extern const device_t ymf701_device;
extern const device_t ymf715_onboard_device;
extern const device_t ymf718_device;
extern const device_t ymf719_device;

#ifdef USE_LIBSERIALPORT
/* External Audio device OPL2Board (Host Connected hardware)*/
extern const device_t opl2board_device;
#endif 

#endif

#endif /*EMU_SOUND_H*/
