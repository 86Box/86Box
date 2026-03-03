#ifndef SOUND_SND_SB_DSP_H
#define SOUND_SND_SB_DSP_H

#include <86box/fifo.h>

/*Sound Blaster Clones, for quirks*/
#define SB_SUBTYPE_DEFAULT             0 /* Handle as a Creative card */
#define SB_SUBTYPE_CLONE_AZT2316A_0X11 1 /* Aztech Sound Galaxy Pro 16 AB, DSP 3.1 - SBPRO2 clone */
#define SB_SUBTYPE_CLONE_AZT1605_0X0C  2 /* Aztech Sound Galaxy Nova 16 Extra /
                                            Packard Bell Forte 16, DSP 2.1 - SBPRO2 clone */
#define SB_SUBTYPE_CLONE_AZTPR16_0X09  3 /* Aztech Sound Galaxy Pro 16 Extra */
#define SB_SUBTYPE_MVD201              4 /* Mediavision MVD201, found on the thunderboard and PAS16 */
#define SB_SUBTYPE_ESS_ES688           5 /* ESS Technology ES688 */
#define SB_SUBTYPE_ESS_ES1688          6 /* ESS Technology ES1688 */

/* ESS-related */
#define IS_ESS(dsp) ((dsp)->sb_subtype >= SB_SUBTYPE_ESS_ES688)    /* Check for future ESS cards here */
#define IS_NOT_ESS(dsp) ((dsp)->sb_subtype < SB_SUBTYPE_ESS_ES688) /* Check for future ESS cards here */

/* aztech-related */
#define IS_AZTECH(dsp)     ((dsp)->sb_subtype == SB_SUBTYPE_CLONE_AZT2316A_0X11 || (dsp)->sb_subtype == SB_SUBTYPE_CLONE_AZT1605_0X0C || (dsp)->sb_subtype == SB_SUBTYPE_CLONE_AZTPR16_0X09) /* check for future AZT cards here */
#define AZTECH_EEPROM_SIZE 36

/* MediaVision related */
#define IS_MV201(dsp) ((dsp)->sb_subtype >= SB_SUBTYPE_MVD201)

typedef struct sb_dsp_t {
    int   sb_type;
    int   sb_subtype; /* which clone */
    void *parent;     /* "sb_t *" if default subtype, "azt2316a_t *" if aztech. */

    int sb_8_length;
    int sb_8_origlength;
    int sb_8_format;
    int sb_8_autoinit;
    int sb_8_pause;
    int sb_8_enable;
    int sb_8_autolen;
    int sb_8_output;
    int sb_8_dmanum;
    int sb_16_length;
    int sb_16_origlength;
    int sb_16_format;
    int sb_16_autoinit;
    int sb_16_pause;
    int sb_16_enable;
    int sb_16_autolen;
    int sb_16_output;
    int sb_16_dmanum;
    int sb_16_8_dmanum;
    int sb_16_dma_enabled;
    int sb_16_dma_supported;
    int sb_16_dma_translate;
    int sb_pausetime;
    int (*dma_readb)(void *priv);
    int (*dma_readw)(void *priv);
    int (*dma_writeb)(void *priv, uint8_t val);
    int (*dma_writew)(void *priv, uint16_t val);
    void *dma_priv;

    uint8_t sb_read_data[256];

    uint8_t dma_ff;
    int     dma_data;

    int     sb_read_wp;
    int     sb_read_rp;
    int     sb_speaker;
    int     muted;

    int sb_data_stat;

    int midi_in_sysex;
    int midi_in_poll;
    int uart_midi;
    int uart_irq;
    int onebyte_midi;
    int midi_in_timestamp;

    int sb_irqnum;
    void (*irq_update)(void *priv, int set);
    void  *irq_priv;

    uint8_t sbe2;
    int     sbe2count;

    uint8_t sb_data[8];

    int sb_freq;

    int16_t sbdat;
    int     sbdat2;
    int16_t sbdatl;
    int16_t sbdatr;

    uint8_t sbref;
    int8_t  sbstep;

    uint8_t activity;

    int sbdacpos;

    int sbleftright;
    int sbleftright_default;

    int     sbreset;
    uint8_t sbreaddat;
    uint8_t sb_command;
    uint8_t sb_last_command;
    uint8_t sb_test;
    int     sb_timei;
    int     sb_timeo;

    int sb_irq8;
    int sb_irq16;
    int sb_irq401;
    int sb_irqm8;
    int sb_irqm16;
    int sb_irqm401;

    uint8_t sb_has_real_opl;

    uint8_t sb_asp_regs[256];
    uint8_t sb_asp_mode;

    uint8_t sb_asp_ram[2048];
    int     sb_asp_ram_index;

    uint8_t sb_8051_ram[256];

    int sbenable;
    int sb_enable_i;

    int state;

    pc_timer_t output_timer;
    pc_timer_t input_timer;

    double sblatcho;
    double sblatchi;

    uint16_t sb_addr;

    int stereo;

    int asp_data_len;

    pc_timer_t wb_timer;
    int        wb_full;

    pc_timer_t irq_timer;
    pc_timer_t irq16_timer;

    int busy_count;

    int     record_pos_read;
    int     record_pos_write;
    int16_t record_buffer[0xFFFF];
    int16_t buffer[SOUNDBUFLEN * 2];
    int     pos;

    uint8_t azt_eeprom[AZTECH_EEPROM_SIZE]; /* the eeprom in the Aztech cards is attached to the DSP */

    uint8_t  ess_regs[256]; /* ESS registers. */
    uint8_t  ess_playback_mode;
    uint8_t  ess_extended_mode;
    uint8_t  ess_reload_len;
    uint32_t ess_dma_counter;

    /* IRQ status flags (0x22C) */
    uint8_t  ess_irq_generic;
    uint8_t  ess_irq_dmactr;

    /* ESPCM */
    fifo64_t *espcm_fifo;
    uint8_t   espcm_fifo_reset;
    uint8_t   espcm_mode;              /* see ESPCM in "NON-PCM SAMPLE FORMATS" deflist in snd_sb_dsp.c */
    uint8_t   espcm_sample_idx;
    uint8_t   espcm_range;
    uint8_t   espcm_byte_buffer[4];
    uint8_t   espcm_code_buffer[19];   /* used for ESPCM_3 and for ESPCM_4 recording */
    int8_t    espcm_sample_buffer[19]; /* used for ESPCM_4 recording */
    uint8_t   espcm_table_index;       /* used for ESPCM_3 */
    uint8_t   espcm_last_value;        /* used for ESPCM_3 */

    mpu_t *mpu;
} sb_dsp_t;

extern void sb_dsp_input_msg(void *priv, uint8_t *msg, uint32_t len);

extern int  sb_dsp_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort);

extern void sb_dsp_set_mpu(sb_dsp_t *dsp, mpu_t *src_mpu);

extern void sb_dsp_init(sb_dsp_t *dsp, int type, int subtype, void *parent);
extern void sb_dsp_close(sb_dsp_t *dsp);

extern void sb_dsp_setirq(sb_dsp_t *dsp, int irq);
extern void sb_dsp_setdma8(sb_dsp_t *dsp, int dma);
extern void sb_dsp_setdma16(sb_dsp_t *dsp, int dma);
extern void sb_dsp_setdma16_8(sb_dsp_t *dsp, int dma);
extern void sb_dsp_setdma16_enabled(sb_dsp_t *dsp, int enabled);
extern void sb_dsp_setdma16_supported(sb_dsp_t *dsp, int supported);
extern void sb_dsp_setdma16_translate(sb_dsp_t *dsp, int translate);
extern void sb_dsp_setaddr(sb_dsp_t *dsp, uint16_t addr);

extern void sb_dsp_speed_changed(sb_dsp_t *dsp);

extern void sb_dsp_poll(sb_dsp_t *dsp, int16_t *l, int16_t *r);

extern void sb_dsp_set_real_opl(sb_dsp_t *dsp, uint8_t has_real_opl);

extern void sb_dsp_set_stereo(sb_dsp_t *dsp, int stereo);

extern void sb_dsp_update(sb_dsp_t *dsp);
extern void sb_update_mask(sb_dsp_t *dsp, int irqm8, int irqm16, int irqm401);

extern void sb_dsp_irq_attach(sb_dsp_t *dsp, void (*irq_update)(void *priv, int set), void *priv);
extern void sb_dsp_dma_attach(sb_dsp_t *dsp,
                              int (*dma_readb)(void *priv),
                              int (*dma_readw)(void *priv),
                              int (*dma_writeb)(void *priv, uint8_t val),
                              int (*dma_writew)(void *priv, uint16_t val),
                              void *priv);

#endif /* SOUND_SND_SB_DSP_H */
