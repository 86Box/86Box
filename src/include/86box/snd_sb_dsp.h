#ifndef SOUND_SND_SB_DSP_H
#define SOUND_SND_SB_DSP_H

/*Sound Blaster Clones, for quirks*/
#define SB_SUBTYPE_DEFAULT             0 /*Handle as a Creative card*/
#define SB_SUBTYPE_CLONE_AZT2316A_0X11 1 /*Aztech Sound Galaxy Pro 16 AB, DSP 3.1 - SBPRO2 clone*/
#define SB_SUBTYPE_CLONE_AZT1605_0X0C  2 /*Aztech Sound Galaxy Nova 16 Extra / Packard Bell Forte 16, DSP 2.1 - SBPRO2 clone*/

/* aztech-related */
#define IS_AZTECH(dsp)     ((dsp)->sb_subtype == SB_SUBTYPE_CLONE_AZT2316A_0X11 || (dsp)->sb_subtype == SB_SUBTYPE_CLONE_AZT1605_0X0C) /* check for future AZT cards here */
#define AZTECH_EEPROM_SIZE 16

typedef struct sb_dsp_t {
    int   sb_type;
    int   sb_subtype; /* which clone */
    void *parent;     /* "sb_t *" if default subtype, "azt2316a_t *" if aztech. */

    int sb_8_length, sb_8_origlength, sb_8_format, sb_8_autoinit, sb_8_pause, sb_8_enable, sb_8_autolen, sb_8_output;
    int sb_8_dmanum;
    int sb_16_length, sb_16_origlength, sb_16_format, sb_16_autoinit, sb_16_pause, sb_16_enable, sb_16_autolen, sb_16_output;
    int sb_16_dmanum;
    int sb_pausetime;
    int (*dma_readb)(void *priv),
        (*dma_readw)(void *priv),
        (*dma_writeb)(void *priv, uint8_t val),
        (*dma_writew)(void *priv, uint16_t val);
    void *dma_priv;

    uint8_t sb_read_data[256];
    int     sb_read_wp, sb_read_rp;
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
    void (*irq_update)(void *priv, int set),
        *irq_priv;

    uint8_t sbe2;
    int     sbe2count;

    uint8_t sb_data[8];

    int sb_freq;

    int16_t sbdat;
    int     sbdat2;
    int16_t sbdatl, sbdatr;

    uint8_t sbref;
    int8_t  sbstep;

    int sbdacpos;

    int sbleftright, sbleftright_default;

    int     sbreset;
    uint8_t sbreaddat;
    uint8_t sb_command;
    uint8_t sb_test;
    int     sb_timei, sb_timeo;

    int sb_irq8, sb_irq16, sb_irq401;
    int sb_irqm8, sb_irqm16, sb_irqm401;

    uint8_t sb_asp_regs[256];
    uint8_t sb_asp_mode;

    uint8_t sb_asp_ram[2048];
    int     sb_asp_ram_index;

    uint8_t sb_8051_ram[256];

    int sbenable, sb_enable_i;

    pc_timer_t output_timer, input_timer;

    double sblatcho, sblatchi;

    uint16_t sb_addr;

    int stereo;

    int asp_data_len;

    pc_timer_t wb_timer;
    int        wb_full;

    int busy_count;

    int     record_pos_read;
    int     record_pos_write;
    int16_t record_buffer[0xFFFF];
    int16_t buffer[SOUNDBUFLEN * 2];
    int     pos;

    uint8_t azt_eeprom[AZTECH_EEPROM_SIZE]; /* the eeprom in the Aztech cards is attached to the DSP */

    mpu_t *mpu;
} sb_dsp_t;

void sb_dsp_input_msg(void *p, uint8_t *msg, uint32_t len);

int sb_dsp_input_sysex(void *p, uint8_t *buffer, uint32_t len, int abort);

void sb_dsp_set_mpu(sb_dsp_t *dsp, mpu_t *src_mpu);

void sb_dsp_init(sb_dsp_t *dsp, int type, int subtype, void *parent);
void sb_dsp_close(sb_dsp_t *dsp);

void sb_dsp_setirq(sb_dsp_t *dsp, int irq);
void sb_dsp_setdma8(sb_dsp_t *dsp, int dma);
void sb_dsp_setdma16(sb_dsp_t *dsp, int dma);
void sb_dsp_setaddr(sb_dsp_t *dsp, uint16_t addr);

void sb_dsp_speed_changed(sb_dsp_t *dsp);

void sb_dsp_poll(sb_dsp_t *dsp, int16_t *l, int16_t *r);

void sb_dsp_set_stereo(sb_dsp_t *dsp, int stereo);

void sb_dsp_update(sb_dsp_t *dsp);
void sb_update_mask(sb_dsp_t *dsp, int irqm8, int irqm16, int irqm401);

void sb_dsp_irq_attach(sb_dsp_t *dsp, void (*irq_update)(void *priv, int set), void *priv);
void sb_dsp_dma_attach(sb_dsp_t *dsp,
                       int (*dma_readb)(void *priv),
                       int (*dma_readw)(void *priv),
                       int (*dma_writeb)(void *priv, uint8_t val),
                       int (*dma_writew)(void *priv, uint16_t val),
                       void *priv);

#endif /* SOUND_SND_SB_DSP_H */
