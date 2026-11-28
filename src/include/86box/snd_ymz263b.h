/*
 * A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Yamaha YMZ263B (MMA) audio helpers, shared by the
 *          AdLib Gold and the Tandy Sensation/VIS audio: PCM and ADPCM
 *          playback and recording, with host input resampling.
 *          Copyright 2026 Xeon3D.
 */
#ifndef SOUND_YMZ263B_H
#define SOUND_YMZ263B_H

#define MMA_REC_RING 16384 /* host input frames held for the ADC */

/* Register 9 (playback and recording control). */
#define MMA_R9_GO   0x01
#define MMA_R9_PLAY 0x02 /* P/R: set to play, clear to record */
#define MMA_R9_PCM  0x04 /* clear: ADPCM */
#define MMA_R9_RST  0x80

/* Register 0Ch (sampling format and control). */
#define MMA_RC_ENB 0x01
#define MMA_RC_MSK 0x02
#define MMA_RC_ILV 0x80

typedef struct mma_play_adpcm_t {
    int32_t predictor;
    int32_t step;
    int     low_nibble;
} mma_play_adpcm_t;

typedef struct mma_rec_t {
    /* The host's input frames (at in_rate) until the ADC takes them. */
    int16_t  ring[MMA_REC_RING * 2];
    uint32_t wr;
    uint32_t rd;
    double   frac;
    int      in_rate;
    int      started;
    int      primed;

    /* Format 0 (8-bit) is offset binary on this card (80h is 0), else signed. */
    int      offset8;

    /* The frame the ADC sampled last (both channels see the same one). */
    int l;
    int r;

    /* ADPCM encoders, one per channel. */
    int32_t adpcm_x[2];
    int32_t adpcm_step[2];
    uint8_t adpcm_byte[2];
    int     adpcm_half[2];
} mma_rec_t;

/* Open (on != 0) or close the host's input. */
extern void mma_rec_input(mma_rec_t *rec, int on);

/* sound_in_add_handler()'s callback: queue the host's input frames. */
extern void mma_rec_put_buffer(int16_t *buffer, int len, void *priv);

/* Take the next input frame at the given ADC rate into rec->l / rec->r. */
extern void mma_rec_next_frame(mma_rec_t *rec, double rate);

/* The sample period in ticks of the MMA's 1.88964 us clock. */
extern int mma_sample_period(uint8_t reg9);

/* The FIFO fill above which a recording channel raises its FIFO flag. */
extern int mma_rec_fifo_level(uint8_t regc);

/* Bytes per DMA/interleave unit for this channel's data format. */
extern int mma_rec_unit(uint8_t reg9, uint8_t regc);

/* Reset a channel's ADPCM encoder (on channel reset or record start). */
extern void mma_rec_adpcm_reset(mma_rec_t *rec, int ch);

/* Encode one 16-bit sample in the channel's format; returns the number of
   bytes put in out (0 to 2: ADPCM gives a byte every second sample). */
extern int mma_rec_encode(mma_rec_t *rec, int ch, int sample, uint8_t reg9, uint8_t regc, uint8_t *out);

/* Playback DMA unit size and PCM sample decoding. */
extern int     mma_play_bytes(uint8_t reg9, uint8_t regc);
extern int16_t mma_play_decode(uint8_t regc, int offset8, uint8_t b0, uint8_t b1);

extern void mma_play_adpcm_reset(mma_play_adpcm_t *state);

/* Decode one nibble. Return the number of FIFO bytes consumed (0 or 1). */
extern int mma_play_adpcm_decode(mma_play_adpcm_t *state, uint8_t byte, int16_t *sample);

#endif /*SOUND_YMZ263B_H*/
