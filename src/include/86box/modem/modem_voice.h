/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and IBM PC systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Voice modem audio codec and framing interfaces.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *          Xeon3D
 *
 *          Copyright 2026 Xeon3D.
 *          Copyright 2025-2026 Jasmine Iwanek.
 *
 * Voice modem audio support (char_modem.c).
 *
 * The formats a voice modem's DTE speaks (Rockwell ADPCM, 8-bit linear),
 * the telephone line's own (G.711 mu-law at 8000 Hz, in frames on the
 * connection), and what goes between them: rate conversion,
 * DTMF tones and the detection of silence.  No I/O: char_modem.c moves the
 * bytes.
 *
 * Released under the GNU General Public License version 2 or later.  See
 * COPYING for more information.
 */
#ifndef EMU_MODEM_VOICE_H
#define EMU_MODEM_VOICE_H

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_LINE_RATE 8000 /* the telephone line: G.711 */
#define VOICE_FRAME_MS  20   /* audio frames on the line  */
#define VOICE_FRAME_SAMPLES (VOICE_LINE_RATE * VOICE_FRAME_MS / 1000)

/* Rockwell ADPCM, 2, 3 or 4 bits a sample (AT#VBS=2..4), bit-exact with
   Rockwell's own coder.  Codewords are packed least significant bit first. */
typedef struct {
    int     bps;
    int16_t pz[8];    /* predictor coefficients a1 a2 b1..b6 */
    int16_t qdata[8]; /* the delay line                      */
    int16_t qidx;
    int16_t di;
    int16_t last_nu;
    int16_t dempz;
    int16_t new_qdata;
    int16_t qdelay_mx;
    int16_t adc16z1; /* the coder's X(n-1) */
    const int16_t *mul;
    const int16_t *zeta;
    const int16_t *cd;
    int            cd_len;
    uint32_t       bits; /* packing */
    int            nbits;
} rv_adpcm_t;

void    rv_adpcm_init(rv_adpcm_t *s, int bps);
int16_t rv_adpcm_decode_one(rv_adpcm_t *s, int code);
int     rv_adpcm_encode_one(rv_adpcm_t *s, int16_t x);
/* Whole streams: bytes in, samples out (returns the samples made), and
   samples in, bytes out (returns the bytes made; a partial byte waits for the
   next call, or for rv_adpcm_flush()). */
size_t rv_adpcm_decode(rv_adpcm_t *s, const uint8_t *in, size_t n, int16_t *out, size_t max);
size_t rv_adpcm_encode(rv_adpcm_t *s, const int16_t *in, size_t n, uint8_t *out, size_t max);
size_t rv_adpcm_flush(rv_adpcm_t *s, uint8_t *out, size_t max);

/* G.711 mu-law and A-law. */
uint8_t voice_ulaw_encode(int16_t x);
int16_t voice_ulaw_decode(uint8_t u);
uint8_t voice_alaw_encode(int16_t x);
int16_t voice_alaw_decode(uint8_t a);

/* Linear interpolation from one rate to another, a sample at a time. */
typedef struct {
    uint32_t from, to;
    uint32_t acc;  /* position between prev and next, in units of 1/to */
    int16_t  prev;
    int16_t  next;
    int      primed;
} voice_resampler_t;

void   voice_resampler_init(voice_resampler_t *r, uint32_t from, uint32_t to);
/* Feeds n samples; writes what they make (at most max) to out. */
size_t voice_resample(voice_resampler_t *r, const int16_t *in, size_t n, int16_t *out, size_t max);

/* DTMF and plain tones, at the line's rate. */
typedef struct {
    double   ph1, ph2;
    double   f1, f2;
    uint32_t left; /* samples of tone to go */
    double   amp;
} voice_tone_t;

int  voice_dtmf_freqs(char digit, double *f1, double *f2);
void voice_tone_start(voice_tone_t *t, double f1, double f2, uint32_t ms, double amp);
/* Adds the tone to buf; returns 1 while it lasts. */
int voice_tone_mix(voice_tone_t *t, int16_t *buf, size_t n);

/* Silence: how long the line has been quiet, from 10 ms blocks. */
typedef struct {
    int      threshold; /* mean absolute level under which a block is quiet */
    uint32_t quiet_ms;
    uint32_t heard_ms;  /* voice since the last silence report */
    int32_t  sum;
    int      count;
} voice_silence_t;

void voice_silence_init(voice_silence_t *s, int sensitivity);
void voice_silence_feed(voice_silence_t *s, const int16_t *buf, size_t n);

/* Voice frames sent directly over the call's TCP connection: a type byte,
   a 16-bit little-endian length, then the payload. */
#define VOICE_FRAME_AUDIO 'A' /* mu-law samples at 8000 Hz */
#define VOICE_FRAME_DTMF  'D' /* one digit, as the far end pressed it */
#define VOICE_FRAME_ALAW  'a' /* A-law samples at 8000 Hz */
#define VOICE_FRAME_S8    's' /* signed 8-bit linear PCM */
#define VOICE_FRAME_U8    'u' /* unsigned 8-bit linear PCM */
#define VOICE_FRAME_S16   'S' /* little-endian signed 16-bit linear PCM */
#define VOICE_FRAME_HDR   3
#define VOICE_FRAME_MAX   512

typedef struct {
    uint8_t buf[VOICE_FRAME_HDR + VOICE_FRAME_MAX];
    size_t  have;
} voice_deframer_t;

/* Takes bytes; for each whole frame calls fn(type, payload, len, priv).
   Returns -1 if the stream is not frames. */
int voice_deframe(voice_deframer_t *d, const uint8_t *in, size_t n,
                  void (*fn)(int type, const uint8_t *payload, size_t len, void *priv), void *priv);
size_t voice_frame(uint8_t *out, int type, const uint8_t *payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_VOICE_H */
