#ifndef SOUND_EMU8K_H
#define SOUND_EMU8K_H

/* All these defines are in samples, not in bytes. */
#define EMU8K_MEM_ADDRESS_MASK  0xFFFFFF
#define EMU8K_RAM_MEM_START     0x200000
#define EMU8K_FM_MEM_ADDRESS    0xFFFFE0
#define EMU8K_RAM_POINTERS_MASK 0x3F
#define EMU8K_LFOCHORUS_SIZE    0x4000
/*
 * Everything in this file assumes little endian
 */
/* used for the increment of oscillator position*/
typedef struct emu8k_mem_internal_t {
    union {
        uint64_t addr;
        struct {
            uint16_t fract_lw_address;
            uint16_t fract_address;
            uint32_t int_address;
        };
    };
} emu8k_mem_internal_t;

/* used for access to ram pointers from oscillator position. */
typedef struct emu8k_mem_pointers_t {
    union {
        uint32_t addr;
        struct {
            uint16_t lw_address;
            uint8_t  hb_address;
            uint8_t  unused_address;
        };
    };
} emu8k_mem_pointers_t;

/*
 * From the Soundfount 2.0 fileformat Spec.:
 *
    An envelope generates a control signal in six phases.
    When key-on occurs, a delay period begins during which the envelope value is zero.
    The envelope then rises in a convex curve to a value of one during the attack phase.
    " Note that the attack is convex; the curve is nominally such that when applied to a
    decibel or semitone parameter, the result is linear in amplitude or Hz respectively"

    When a value of one is reached, the envelope enters a hold phase during which it remains at one.
    When the hold phase ends, the envelope enters a decay phase during which its value decreases linearly to a sustain level.
    " For the Volume Envelope, the decay phase linearly ramps toward the sustain level, causing a constant dB change for each time unit. "
    When the sustain level is reached, the envelope enters sustain phase, during which the envelope stays at the sustain level.

    Whenever a key-off occurs, the envelope immediately enters a release phase during which the value linearly ramps from the current value to zero.
    " For the Volume Envelope, the release phase linearly ramps toward zero from the current level, causing a constant dB change for each time unit"

    When zero is reached, the envelope value remains at zero.

    Modulation of pitch and filter cutoff are in octaves, semitones, and cents.
    These parameters can be modulated to varying degree, either positively or negatively, by the modulation envelope.
    The degree of modulation is specified in cents for the full-scale attack peak.

    The volume envelope operates in dB, with the attack peak providing a full scale output, appropriately scaled by the initial volume.
    The zero value, however, is actually zero gain.
    The implementation in the EMU8000 provides for 96 dB of amplitude control.
    When 96 dB of attenuation is reached in the final gain amplifier, an abrupt jump to zero gain
    (infinite dB of attenuation) occurs. In a 16-bit system, this jump is inaudible
*/
/* It seems that the envelopes don't really have a decay/release stage,
 * but instead they have a volume ramper that can be triggered
 * automatically (after hold period), or manually (by activating release)
 * and the "sustain" value is the target of any of both cases.
 * Some programs like cubic player and AWEAmp use this, and it was
 * described in the following way in Vince Vu/Judge Dredd's awe32p10.txt:
 *    If the MSB (most significant bit or bit 15) of this register is set,
 *    the Decay/Release will begin immediately, overriding the Delay, Attack,
 *    and Hold.  Otherwise the Decay/Release will wait until the Delay, Attack,
 *    and Hold are finished.  If you set the MSB of this register, you can use
 *    it as a volume ramper, as on the GUS.  The upper byte (except the MSB),
 *    contains the destination volume, and the lower byte contains the ramp time.
 */

/* attack_amount is linear amplitude (added directly to value).
 * ramp_amount_db is linear dB (added directly to value too, but needs conversion to get linear amplitude).
 * value range is 21bits for both, linear amplitude being 1<<21 = 0dBFS and 0 = -96dBFS (which is shortcut to silence),
 * and db amplutide being 0 = 0dBFS and -(1<<21) = -96dBFS (which is shortcut to silence).
 * This allows to operate db values by simply adding them.
 */
typedef struct emu8k_envelope_t {
    int     state;
    int32_t delay_samples, hold_samples, attack_samples;
    int32_t value_amp_hz, value_db_oct;
    int32_t sustain_value_db_oct;
    int32_t attack_amount_amp_hz, ramp_amount_db_oct;
} emu8k_envelope_t;

typedef struct emu8k_chorus_eng_t {
    int32_t              write;
    int32_t              feedback;
    int32_t              delay_samples_central;
    double               lfodepth_multip;
    double               delay_offset_samples_right;
    emu8k_mem_internal_t lfo_inc;
    emu8k_mem_internal_t lfo_pos;

    int32_t chorus_left_buffer[EMU8K_LFOCHORUS_SIZE];
    int32_t chorus_right_buffer[EMU8K_LFOCHORUS_SIZE];

} emu8k_chorus_eng_t;

/*  32 * 242. 32 comes from the "right" room resso case.*/
#define MAX_REFL_SIZE 7744

/* Reverb parameters description, extracted from AST sources.
 Mix level
 Decay
 Link return amp
 Link type         Switches between normal or panned
 Room reso (   ms) L&R (Ref 6 +1)
 Ref 1 x2 (11 ms)R
 Ref 2 x4 (22 ms)R
 Ref 3 x8 (44 ms)L
 Ref 4 x13(71 ms)R
 Ref 5 x19(105ms)L
 Ref 6 x  (   ms)R  (multiplier changes with room reso)
 Ref 1-6 filter    L&R
 Ref 1-6 amp       L&R
 Ref 1 feedback    L&R
 Ref 2 feedback    L&R
 Ref 3 feedback    L&R
 Ref 4 feedback    L&R
 Ref 5 feedback    L&R
 Ref 6 feedback    L&R
*/
typedef struct emu8k_reverb_combfilter_t {
    int     read_pos;
    int32_t reflection[MAX_REFL_SIZE];
    float   output_gain;
    float   feedback;
    float   damp1;
    float   damp2;
    int     bufsize;
    int32_t filterstore;
} emu8k_reverb_combfilter_t;

typedef struct emu8k_reverb_eng_t {

    int16_t out_mix;
    int16_t link_return_amp; /* tail part output gain ? */
    int8_t  link_return_type;

    uint8_t refl_in_amp;

    emu8k_reverb_combfilter_t reflections[6];
    emu8k_reverb_combfilter_t allpass[8];
    emu8k_reverb_combfilter_t tailL;
    emu8k_reverb_combfilter_t tailR;

    emu8k_reverb_combfilter_t damper;
} emu8k_reverb_eng_t;

typedef struct emu8k_slide_t {
    int32_t last;
} emu8k_slide_t;

typedef struct emu8k_voice_t {
    union {
        uint32_t cpf;
        struct {
            uint16_t cpf_curr_frac_addr; /* fractional part of the playing cursor. */
            uint16_t cpf_curr_pitch;     /* 0x4000 = no shift. Linear increment */
        };
    };
    union {
        uint32_t ptrx;
        struct {
            uint8_t  ptrx_pan_aux;
            uint8_t  ptrx_revb_send;
            uint16_t ptrx_pit_target; /* target pitch to which slide at curr_pitch speed. */
        };
    };
    union {
        uint32_t cvcf;
        struct {
            uint16_t cvcf_curr_filt_ctoff;
            uint16_t cvcf_curr_volume;
        };
    };
    emu8k_slide_t volumeslide;
    union {
        uint32_t vtft;
        struct {
            uint16_t vtft_filter_target;
            uint16_t vtft_vol_target; /* written to by the envelope engine. */
        };
    };
    /* These registers are used at least by the Windows drivers, and seem to be resetting
     * something, similarly to targets and current, but... of what?
     * what is curious is that if they are already zero, they are not written to, so it really
     * looks like they are information about the status of the channel. (lfo position maybe?) */
    uint32_t unknown_data0_4;
    uint32_t unknown_data0_5;
    union {
        uint32_t psst;
        struct {
            uint16_t psst_lw_address;
            uint8_t  psst_hw_address;
            uint8_t  psst_pan;
        };
#define PSST_LOOP_START_MASK 0x00FFFFFF /* In samples, i.e. uint16_t array[BOARD_RAM/2]; */
    };
    union {
        uint32_t csl;
        struct {
            uint16_t csl_lw_address;
            uint8_t  csl_hw_address;
            uint8_t  csl_chor_send;
        };
#define CSL_LOOP_END_MASK 0x00FFFFFF /* In samples, i.e. uint16_t array[BOARD_RAM/2]; */
    };
    union {
        uint32_t ccca;
        struct {
            uint16_t ccca_lw_addr;
            uint8_t  ccca_hb_addr;
            uint8_t  ccca_qcontrol;
        };
    };
#define CCCA_FILTQ_GET(ccca)    (ccca >> 28)
#define CCCA_FILTQ_SET(ccca, q) ccca = (ccca & 0x0FFFFFFF) | (q << 28)
/* Bit 27 should always be zero */
#define CCCA_DMA_ACTIVE(ccca)      (ccca & 0x04000000)
#define CCCA_DMA_WRITE_MODE(ccca)  (ccca & 0x02000000)
#define CCCA_DMA_WRITE_RIGHT(ccca) (ccca & 0x01000000)

    uint16_t envvol;
#define ENVVOL_NODELAY(envol) (envvol & 0x8000)
/* Verified with a soundfont bank. 7FFF is the minimum delay time, and 0 is the max delay time */
#define ENVVOL_TO_EMU_SAMPLES(envvol) (envvol & 0x8000) ? 0 : ((0x8000 - (envvol & 0x7FFF)) << 5)

    uint16_t dcysusv;
#define DCYSUSV_IS_RELEASE(dcysusv)          (dcysusv & 0x8000)
#define DCYSUSV_GENERATOR_ENGINE_ON(dcysusv) !(dcysusv & 0x0080)
#define DCYSUSV_SUSVALUE_GET(dcysusv)        ((dcysusv >> 8) & 0x7F)
/* Inverting the range compared to documentation because the envelope runs from 0dBFS = 0 to -96dBFS = (1 <<21) */
#define DCYSUSV_SUS_TO_ENV_RANGE(susvalue) (((0x7F - susvalue) << 21) / 0x7F)
#define DCYSUSV_DECAYRELEASE_GET(dcysusv)  (dcysusv & 0x7F)

    uint16_t envval;
#define ENVVAL_NODELAY(enval) (envval & 0x8000)
/* Verified with a soundfont bank. 7FFF is the minimum delay time, and 0 is the max delay time */
#define ENVVAL_TO_EMU_SAMPLES(envval) (envval & 0x8000) ? 0 : ((0x8000 - (envval & 0x7FFF)) << 5)

    uint16_t dcysus;
#define DCYSUS_IS_RELEASE(dcysus)         (dcysus & 0x8000)
#define DCYSUS_SUSVALUE_GET(dcysus)       ((dcysus >> 8) & 0x7F)
#define DCYSUS_SUS_TO_ENV_RANGE(susvalue) ((susvalue << 21) / 0x7F)
#define DCYSUS_DECAYRELEASE_GET(dcysus)   (dcysus & 0x7F)

    uint16_t atkhldv;
#define ATKHLDV_TRIGGER(atkhldv)             !(atkhldv & 0x8000)
#define ATKHLDV_HOLD(atkhldv)                ((atkhldv >> 8) & 0x7F)
#define ATKHLDV_HOLD_TO_EMU_SAMPLES(atkhldv) (4096 * (0x7F - ((atkhldv >> 8) & 0x7F)))
#define ATKHLDV_ATTACK(atkhldv)              (atkhldv & 0x7F)

    uint16_t lfo1val, lfo2val;
#define LFOxVAL_NODELAY(lfoxval)        (lfoxval & 0x8000)
#define LFOxVAL_TO_EMU_SAMPLES(lfoxval) (lfoxval & 0x8000) ? 0 : ((0x8000 - (lfoxval & 0x7FFF)) << 5)

    uint16_t atkhld;
#define ATKHLD_TRIGGER(atkhld)             !(atkhld & 0x8000)
#define ATKHLD_HOLD(atkhld)                ((atkhld >> 8) & 0x7F)
#define ATKHLD_HOLD_TO_EMU_SAMPLES(atkhld) (4096 * (0x7F - ((atkhld >> 8) & 0x7F)))
#define ATKHLD_ATTACK(atkhld)              (atkhld & 0x7F)

    uint16_t ip;
#define INTIAL_PITCH_CENTER 0xE000
#define INTIAL_PITCH_OCTAVE 0x1000

    union {
        uint16_t ifatn;
        struct {
            uint8_t ifatn_attenuation;
            uint8_t ifatn_init_filter;
        };
    };
    union {
        uint16_t pefe;
        struct {
            int8_t pefe_modenv_filter_height;
            int8_t pefe_modenv_pitch_height;
        };
    };
    union {
        uint16_t fmmod;
        struct {
            int8_t fmmod_lfo1_filt_mod;
            int8_t fmmod_lfo1_vibrato;
        };
    };
    union {
        uint16_t tremfrq;
        struct {
            uint8_t tremfrq_lfo1_freq;
            int8_t  tremfrq_lfo1_tremolo;
        };
    };
    union {
        uint16_t fm2frq2;
        struct {
            uint8_t fm2frq2_lfo2_freq;
            int8_t  fm2frq2_lfo2_vibrato;
        };
    };

    int env_engine_on;

    emu8k_mem_internal_t addr, loop_start, loop_end;

    int32_t initial_att;
    int32_t initial_filter;

    emu8k_envelope_t vol_envelope;
    emu8k_envelope_t mod_envelope;

    int64_t              lfo1_speed, lfo2_speed;
    emu8k_mem_internal_t lfo1_count, lfo2_count;
    int32_t              lfo1_delay_samples, lfo2_delay_samples;
    int                  vol_l, vol_r;

    int16_t fixed_modenv_filter_height;
    int16_t fixed_modenv_pitch_height;
    int16_t fixed_lfo1_filt_mod;
    int16_t fixed_lfo1_vibrato;
    int16_t fixed_lfo1_tremolo;
    int16_t fixed_lfo2_vibrato;

    /* filter internal data. */
    int     filterq_idx;
    int32_t filt_att;
    int64_t filt_buffer[5];

} emu8k_voice_t;

typedef struct emu8k_t {
    emu8k_voice_t voice[32];

    uint16_t hwcf1, hwcf2, hwcf3;
    uint32_t hwcf4, hwcf5, hwcf6, hwcf7;

    uint16_t init1[32], init2[32], init3[32], init4[32];

    uint32_t smalr, smarr, smalw, smarw;
    uint16_t smld_buffer, smrd_buffer;

    uint16_t wc;

    uint16_t id;

    /* The empty block is used to act as an unallocated memory returning zero. */
    int16_t *ram, *rom, *empty;

    /* RAM pointers are a way to avoid checking ram boundaries on read */
    int16_t *ram_pointers[0x100];
    uint32_t ram_end_addr;

    int cur_reg, cur_voice;

    int16_t out_l, out_r;

    emu8k_chorus_eng_t chorus_engine;
    int32_t            chorus_in_buffer[SOUNDBUFLEN];
    emu8k_reverb_eng_t reverb_engine;
    int32_t            reverb_in_buffer[SOUNDBUFLEN];

    int     pos;
    int32_t buffer[SOUNDBUFLEN * 2];

    uint16_t addr;
} emu8k_t;

void emu8k_change_addr(emu8k_t *emu8k, uint16_t emu_addr);
void emu8k_init(emu8k_t *emu8k, uint16_t emu_addr, int onboard_ram);
void emu8k_close(emu8k_t *emu8k);

void emu8k_update(emu8k_t *emu8k);

/*

Section E - Introduction to the EMU8000 Chip

     The  EMU8000 has its roots in E-mu's Proteus sample playback
     modules and their renowned Emulator sampler. The EMU8000 has
     32 individual oscillators, each playing back at 44.1 kHz. By
     incorporating sophisticated sample interpolation  algorithms
     and  digital filtering, the EMU8000 is capable of  producing
     high fidelity sample playback.

     The EMU8000 has an extensive modulation capability using two
     sine-wave  LFOs  (Low Frequency Oscillator) and  two  multi-
     stage envelope generators.

     What  exactly  does  modulation mean?  Modulation  means  to
     dynamically  change a parameter of an audio signal,  whether
     it  be  the volume (amplitude modulation, or tremolo), pitch
     (frequency   modulation,  or  vibrato)  or   filter   cutoff
     frequency  (filter  modulation,  or  wah-wah).  To  modulate
     something  we  would  require a  modulation  source,  and  a
     modulation  destination.  In  the  EMU8000,  the  modulation
     sources  are the LFOs and the envelope generators,  and  the
     modulation destinations can be the pitch, the volume or  the
     filter cutoff frequency.

     The EMU8000's LFOs and envelope generators provide a complex
     modulation environment. Each sound producing element of  the
     EMU8000 consists of a resonant low-pass filter, two LFOs, in
     which   one  modulates  the  pitch  (LFO2),  and  the  other
     modulates   pitch,   filter   cutoff   and   volume   (LFO1)
     simultaneously. There are two envelope generators;  envelope
     1  contours both pitch and filter cutoff simultaneously, and
     envelope 2 contours volume. The output stage consists of  an
     effects   engine  that  mixes  the  dry  signals  with   the
     Reverb/chorus level signals to produce the final mix.

     What are the EMU8000 sound elements?

     Each  of  the sound elements in an EMU8000 consists  of  the
     following:

     Oscillator
       An oscillator is the source of an audio signal.

     Low Pass Filter
       The  low  pass  filter is responsible  for  modifying  the
       timbres  of  an  instrument. The low pass filter's  filter
       cutoff  values can be varied from 100 Hz to  8000  Hz.  By
       changing  the  values of the filter cutoff,  a  myriad  of
       analogue  sounding  filter  sweeps  can  be  achieved.  An
       example of a GM instrument that makes use of filter  sweep
       is instrument number 87, Lead 7 (fifths).

     Amplifier
       The amplifier determines the loudness of an audio signal.

     LFO1
       An  LFO, or Low Frequency Oscillator, is normally used  to
       periodically modulate, that is, change a sound  parameter,
       whether   it  be  volume  (amplitude  modulation),   pitch
       (frequency   modulation)   or   filter   cutoff    (filter
       modulation).  It  operates  at  sub-audio  frequency  from
       0.042  Hz  to 10.71 Hz. The LFO1 in the EMU8000  modulates
       the pitch, volume and filter cutoff simultaneously.

     LFO2
       The  LFO2 is similar to the LFO1, except that it modulates
       the pitch of the audio signal only.

     Resonance
       A  filter  alone  would  be like an  equalizer,  making  a
       bright  audio signal duller, but the addition of resonance
       greatly  increases  the creative potential  of  a  filter.
       Increasing  the resonance of a filter makes  it  emphasize
       signals  at the cutoff frequency, giving the audio  signal
       a  subtle wah-wah, that is, imagine a siren sound  going
       from bright to dull to bright again periodically.

     LFO1 to Volume (Tremolo)
       The  LFO1's  output is routed to the amplifier,  with  the
       depth  of  oscillation determined by LFO1 to Volume.  LFO1
       to   Volume   produces  tremolo,  which  is   a   periodic
       fluctuation  of  volume. Lets say you are listening  to  a
       piece  of  music  on  your home stereo  system.  When  you
       rapidly  increase  and decrease the playback  volume,  you
       are  creating tremolo effect, and the speed in  which  you
       increases  and  decreases the volume is the  tremolo  rate
       (which  corresponds  to the speed  at  which  the  LFO  is
       oscillating).  An  example of a GM instrument  that  makes
       use  of  LFO1  to Volume is instrument number 45,  Tremolo
       Strings.

     LFO1 to Filter Cutoff (Wah-Wah)
       The  LFO1's output is routed to the filter, with the depth
       of  oscillation  determined by LFO1  to  Filter.  LFO1  to
       Filter  produces  a  periodic fluctuation  in  the  filter
       cutoff  frequency,  producing an effect  very  similar  to
       that  of a wah-wah guitar (see resonance for a description
       of  wah-wah)  An example of a GM instrument  that  makes
       use  of  LFO1  to Filter Cutoff is instrument  number  19,
       Rock Organ.

     LFO1 to Pitch (Vibrato)
       The  LFO1's output is routed to the oscillator,  with  the
       depth of oscillation determined by LFO1 to Pitch. LFO1  to
       Pitch produces a periodic fluctuation in the pitch of  the
       oscillator,  producing a vibrato effect. An example  of  a
       GM   instrument  that  makes  use  of  LFO1  to  Pitch  is
       instrument number 57, Trumpet.

     LFO2 to Pitch (Vibrato)
       The  LFO1  in  the  EMU8000  can  simultaneously  modulate
       pitch,  volume  and  filter.  LFO2,  on  the  other  hand,
       modulates  only  the pitch, with the depth  of  modulation
       determined  by  LFO2 to Pitch. LFO2 to  Pitch  produces  a
       periodic  fluctuation  in  the pitch  of  the  oscillator,
       producing  a  vibrato effect. When this  is  coupled  with
       LFO1 to Pitch, a complex vibrato effect can be achieved.

     Volume Envelope
       The   character  of  a  musical  instrument   is   largely
       determined  by its volume envelope, the way in  which  the
       level  of  the  sound  changes  with  time.  For  example,
       percussive  sounds  usually start suddenly  and  then  die
       away, whereas a bowed sound might take quite some time  to
       start and then sustain at a more or less fixed level.

       A  six-stage envelope makes up the volume envelope of  the
       EMU8000.  The  six stages are delay, attack, hold,  decay,
       sustain  and  release.  The stages  can  be  described  as
       follows:

       Delay     The  time between when a key is played and  when
                 the attack phase begins
       Attack    The  time  it takes to go from zero to the  peak
                 (full) level.
       Hold      The  time  the envelope will stay  at  the  peak
                 level before starting the decay phase.
       Decay     The  time  it takes the envelope to go from  the
                 peak level to the sustain level.
       Sustain   The  level at which the envelope remains as long
                 as a key is held down.
       Release   The  time it takes the envelope to fall  to  the
                 zero level after the key is released.

       Using  these  six  parameters  can  yield  very  realistic
       reproduction  of  the volume envelope  characteristics  of
       many musical instruments.

     Pitch and Filter Envelope
       The  pitch  and filter envelope is similar to  the  volume
       envelope  in  that  it has the same envelope  stages.  The
       difference  between  them  is  that  whereas  the   volume
       envelope contours the volume of the instrument over  time,
       the  pitch  and  filter envelope contours  the  pitch  and
       filter  values  of  the instrument over  time.  The  pitch
       envelope  is particularly useful in putting the  finishing
       touches  in simulating a natural instrument. For  example,
       some  wind instruments tend to go slightly sharp when they
       are  first blown, and this characteristic can be simulated
       by  setting up a pitch envelope with a fairly fast  attack
       and  decay.  The  filter envelope, on the other  hand,  is
       useful  in  creating synthetic sci-fi sound  textures.  An
       example  of  a GM instrument that makes use of the  filter
       envelope is instrument number 86, Pad 8 (Sweep).

     Pitch/Filter Envelope Modulation
       These  two  parameters determine the modulation  depth  of
       the  pitch  and  filter envelope. In the  wind  instrument
       example   above,   a  small  amount  of   pitch   envelope
       modulation  is  desirable to simulate  its  natural  pitch
       characteristics.

     This  rich  modulation capability of the  EMU8000  is  fully
     exploited  by  the SB AWE32 MIDI drivers.  The  driver  also
     provides  you  with a means to change these parameters  over
     MIDI in real time. Refer to the section "How do I change  an
     instrument's  sound  parameter  in  real  time"   for   more
     information.




     Room 1 - 3
       This  group  of  reverb  variation simulates  the  natural
       ambiance of a room. Room 1 simulates a small room, Room  2
       simulates  a slightly bigger room, and Room 3 simulates  a
       big room.

     Hall 1 - 2
       This  group  of  reverb  variation simulates  the  natural
       ambiance of a concert hall. It has greater depth than  the
       room  variations. Again, Hall 1 simulates  a  small  hall,
       and Hall 2 simulates a larger hall.

     Plate
       Back  in  the  old  days,  reverb effects  were  sometimes
       produced  using  a metal plate, and this  type  of  reverb
       produces  a metallic echo. The SB AWE32's Plate  variation
       simulates this form of reverb.

     Delay
       This reverb produces a delay, that is, echo effect.

     Panning Delay
       This  reverb  variation produces a delay  effect  that  is
       continuously panned left and right.

     Chorus 1 - 4
       Chorus  produces  a "beating" effect. The  chorus  effects
       are more prominent going from chorus 1 to chorus 4.

     Feedback Chorus
       This chorus variation simulates a soft "swishing" effect.

     Flanger
       This  chorus variation produces a more prominent  feedback
       chorus effect.

     Short Delay
       This  chorus  variation simulates a delay  repeated  in  a
       short time.

     Short Delay (feed back)
       This  chorus  variation simulates a short  delay  repeated
       (feedback) many times.



Registers to write the Chorus Parameters to (all are 16-bit, unless noted):
(codified as in register,port,voice. port 0=0x620, 2=0x622, 4=0xA20, 6=0xA22, 8=0xE20)
( 3409 = register 3, port A20, voice 9)

0x3409
0x340C
0x3603
0x1409 (32-Bit)
0x140A (32-Bit)
then write 0x8000 to 0x140D (32-Bit)
and then 0x0000 to 0x140E (32-Bit)

Chorus Parameters:

Chorus 1  Chorus 2  Chorus 3  Chorus 4  Feedback  Flanger

0xE600    0xE608    0xE610    0xE620    0xE680    0xE6E0
0x03F6    0x031A    0x031A    0x0269    0x04D3    0x044E
0xBC2C    0xBC6E    0xBC84    0xBC6E    0xBCA6    0xBC37
0x0000    0x0000    0x0000    0x0000    0x0000    0x0000
0x006D    0x017C    0x0083    0x017C    0x005B    0x0026

Short Delay         Short Delay + Feedback

0xE600              0xE6C0
0x0B06              0x0B06
0xBC00              0xBC00
0xE000              0xE000
0x0083              0x0083

// Chorus Params
typedef struct {
        WORD	FbkLevel;	// Feedback Level (0xE600-0xE6FF)
        WORD	Delay;		// Delay (0-0x0DA3)  [1/44100 sec]
        WORD	LfoDepth;	// LFO Depth (0xBC00-0xBCFF)
        DWORD	DelayR;		// Right Delay (0-0xFFFFFFFF) [1/256/44100 sec]
        DWORD	LfoFreq;	// LFO Frequency (0-0xFFFFFFFF)
        } CHORUS_TYPE;


Registers to write the Reverb Parameters to (they are all 16-bit):
(codified as in register,port,voice. port 0=0x620, 2=0x622, 4=0xA20, 6=0xA22, 8=0xE20)
( 3409 = register 3, port A20, voice 9)

0x2403,0x2405,0x361F,0x2407,0x2614,0x2616,0x240F,0x2417,
0x241F,0x2607,0x260F,0x2617,0x261D,0x261F,0x3401,0x3403,
0x2409,0x240B,0x2411,0x2413,0x2419,0x241B,0x2601,0x2603,
0x2609,0x260B,0x2611,0x2613

Reverb Parameters:

Room 1:

0xB488,0xA450,0x9550,0x84B5,0x383A,0x3EB5,0x72F4,0x72A4,
0x7254,0x7204,0x7204,0x7204,0x4416,0x4516,0xA490,0xA590,
0x842A,0x852A,0x842A,0x852A,0x8429,0x8529,0x8429,0x8529,
0x8428,0x8528,0x8428,0x8528

Room 2:

0xB488,0xA458,0x9558,0x84B5,0x383A,0x3EB5,0x7284,0x7254,
0x7224,0x7224,0x7254,0x7284,0x4448,0x4548,0xA440,0xA540,
0x842A,0x852A,0x842A,0x852A,0x8429,0x8529,0x8429,0x8529,
0x8428,0x8528,0x8428,0x8528

Room 3:

0xB488,0xA460,0x9560,0x84B5,0x383A,0x3EB5,0x7284,0x7254,
0x7224,0x7224,0x7254,0x7284,0x4416,0x4516,0xA490,0xA590,
0x842C,0x852C,0x842C,0x852C,0x842B,0x852B,0x842B,0x852B,
0x842A,0x852A,0x842A,0x852A

Hall 1:

0xB488,0xA470,0x9570,0x84B5,0x383A,0x3EB5,0x7284,0x7254,
0x7224,0x7224,0x7254,0x7284,0x4448,0x4548,0xA440,0xA540,
0x842B,0x852B,0x842B,0x852B,0x842A,0x852A,0x842A,0x852A,
0x8429,0x8529,0x8429,0x8529

Hall 2:

0xB488,0xA470,0x9570,0x84B5,0x383A,0x3EB5,0x7254,0x7234,
0x7224,0x7254,0x7264,0x7294,0x44C3,0x45C3,0xA404,0xA504,
0x842A,0x852A,0x842A,0x852A,0x8429,0x8529,0x8429,0x8529,
0x8428,0x8528,0x8428,0x8528

Plate:

0xB4FF,0xA470,0x9570,0x84B5,0x383A,0x3EB5,0x7234,0x7234,
0x7234,0x7234,0x7234,0x7234,0x4448,0x4548,0xA440,0xA540,
0x842A,0x852A,0x842A,0x852A,0x8429,0x8529,0x8429,0x8529,
0x8428,0x8528,0x8428,0x8528

Delay:

0xB4FF,0xA470,0x9500,0x84B5,0x333A,0x39B5,0x7204,0x7204,
0x7204,0x7204,0x7204,0x72F4,0x4400,0x4500,0xA4FF,0xA5FF,
0x8420,0x8520,0x8420,0x8520,0x8420,0x8520,0x8420,0x8520,
0x8420,0x8520,0x8420,0x8520

Panning Delay:

0xB4FF,0xA490,0x9590,0x8474,0x333A,0x39B5,0x7204,0x7204,
0x7204,0x7204,0x7204,0x72F4,0x4400,0x4500,0xA4FF,0xA5FF,
0x8420,0x8520,0x8420,0x8520,0x8420,0x8520,0x8420,0x8520,
0x8420,0x8520,0x8420,0x8520

Registers to write the EQ Parameters to (16-Bit):
(codified as in register,port,voice. port 0=0x620, 2=0x622, 4=0xA20, 6=0xA22, 8=0xE20)
( 3409 = register 3, port A20, voice 9)

Bass:

0x3601
0x3611

Treble:

0x3411
0x3413
0x341B
0x3607
0x360B
0x360D
0x3617
0x3619

Total:

write the 0x0263 + 3rd parameter of the Bass EQ + 9th parameter of Treble EQ to 0x3615.
write the 0x8363 + 3rd parameter of the Bass EQ + 9th parameter of Treble EQ to 0x3615.


Bass Parameters:

0:      1:      2:      3:      4:      5:      6:      7:      8:      9:      10:     11:

0xD26A  0xD25B  0xD24C  0xD23D  0xD21F  0xC208  0xC219  0xC22A  0xC24C  0xC26E  0xC248  0xC26A
0xD36A  0xD35B  0xD34C  0xD33D  0xC31F  0xC308  0xC308  0xC32A  0xC34C  0xC36E  0xC384  0xC36A
0x0000  0x0000  0x0000  0x0000  0x0000  0x0001  0x0001  0x0001  0x0001  0x0001  0x0002  0x0002

Treble Parameters:

0:      1:      2:      3:      4:      5:      6:      7:      8:      9:      10:     11:
0x821E  0x821E  0x821E  0x821E  0x821E  0x821E  0x821E  0x821E  0x821E  0x821E  0x821D  0x821C
0xC26A  0xC25B  0xC24C  0xC23D  0xC21F  0xD208  0xD208  0xD208  0xD208  0xD208  0xD219  0xD22A
0x031E  0x031E  0x031E  0x031E  0x031E  0x031E  0x031E  0x031E  0x031E  0x031E  0x031D  0x031C
0xC36A  0xC35B  0xC34C  0xC33D  0xC31F  0xD308  0xD308  0xD308  0xD308  0xD308  0xD319  0xD32A
0x021E  0x021E  0x021E  0x021E  0x021E  0x021E  0x021D  0x021C  0x021A  0x0219  0x0219  0x0219
0xD208  0xD208  0xD208  0xD208  0xD208  0xD208  0xD219  0xD22A  0xD24C  0xD26E  0xD26E  0xD26E
0x831E  0x831E  0x831E  0x831E  0x831E  0x831E  0x831D  0x831C  0x831A  0x8319  0x8319  0x8319
0xD308  0xD308  0xD308  0xD308  0xD308  0xD308  0xD3019 0xD32A  0xD34C  0xD36E  0xD36E  0xD36E
0x0001  0x0001  0x0001  0x0001  0x0001  0x0002  0x0002  0x0002  0x0002  0x0002  0x0002  0x0002
*/

#endif /*SOUND_EMU8K_H*/
