/* All these defines are in samples, not in bytes. */
#define EMU8K_MEM_ADDRESS_MASK 0xFFFFFF
#define EMU8K_RAM_MEM_START 0x200000
#define EMU8K_FM_MEM_ADDRESS 0xFFFFE0
#define EMU8K_RAM_POINTERS_MASK 0x3F 
#define EMU8K_LFOCHORUS_SIZE 0x4000

/*
   Everything in this file assumes little endian
  
   used for the increment of oscillator position */
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
                uint8_t hb_address;
                uint8_t unused_address;
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
    When 96 dB of attenuation is reached in the final gain amplifier, an abrupt jump to zero gain (infinite dB of attenuation) occurs. In a 16-bit system, this jump is inaudible
*/
/* It seems that the envelopes don't really have a decay/release stage,
   but instead they have a volume ramper that can be triggered 
   automatically (after hold period), or manually (by activating release)
   and the "sustain" value is the target of any of both cases.
   Some programs like cubic player and AWEAmp use this, and it was
   described in the following way in Vince Vu/Judge Dredd's awe32p10.txt:
      If the MSB (most significant bit or bit 15) of this register is set,
      the Decay/Release will begin immediately, overriding the Delay, Attack,
      and Hold.  Otherwise the Decay/Release will wait until the Delay, Attack,
      and Hold are finished.  If you set the MSB of this register, you can use
      it as a volume ramper, as on the GUS.  The upper byte (except the MSB),
      contains the destination volume, and the lower byte contains the ramp time. */

/* attack_amount is linear amplitude (added directly to value). ramp_amount_db is linear dB (added directly to value too, but needs conversion to get linear amplitude).
   value range is 21bits for both, linear amplitude being 1<<21 = 0dBFS and 0 = -96dBFS (which is shortcut to silence), and db amplutide being 0 = 0dBFS and -(1<<21) = -96dBFS (which is shortcut to silence). This allows to operate db values by simply adding them. */
typedef struct emu8k_envelope_t {
        int state;
        int32_t delay_samples, hold_samples, attack_samples;
        int32_t value_amp_hz, value_db_oct;
        int32_t sustain_value_db_oct;
        int32_t attack_amount_amp_hz, ramp_amount_db_oct;
} emu8k_envelope_t;



typedef struct emu8k_chorus_eng_t {
        int32_t write;
        int32_t feedback;
        int32_t delay_samples_central;
        double lfodepth_multip;
        double delay_offset_samples_right;
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
        int read_pos;
        int32_t reflection[MAX_REFL_SIZE];
        float output_gain;
        float feedback;
        float damp1;
        float damp2;
        int bufsize;
        int32_t filterstore;
} emu8k_reverb_combfilter_t;

typedef struct emu8k_reverb_eng_t {

        int16_t out_mix;
        int16_t link_return_amp; /* tail part output gain ? */
        int8_t link_return_type;

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


typedef struct emu8k_voice_t
{
        union {
                uint32_t cpf;
                struct {
                        uint16_t cpf_curr_frac_addr; /* fractional part of the playing cursor. */
                        uint16_t cpf_curr_pitch; /* 0x4000 = no shift. Linear increment */
                };
        };
        union {
                uint32_t ptrx;
                struct {
                        uint8_t ptrx_pan_aux;
                        uint8_t ptrx_revb_send;
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
                        uint8_t psst_hw_address;
                        uint8_t psst_pan;
                };
                #define PSST_LOOP_START_MASK 0x00FFFFFF /* In samples, i.e. uint16_t array[BOARD_RAM/2]; */
        };
        union {
                uint32_t csl;
                struct { 
                        uint16_t csl_lw_address;
                        uint8_t csl_hw_address;
                        uint8_t csl_chor_send;
                };
                #define CSL_LOOP_END_MASK 0x00FFFFFF /* In samples, i.e. uint16_t array[BOARD_RAM/2]; */
        };
        union {
                uint32_t ccca;
                struct {
                        uint16_t ccca_lw_addr;
                        uint8_t ccca_hb_addr;
                        uint8_t ccca_qcontrol;
                };
        };
        #define CCCA_FILTQ_GET(ccca) (ccca>>28)
        #define CCCA_FILTQ_SET(ccca,q) ccca = (ccca&0x0FFFFFFF) | (q<<28)
        /* Bit 27 should always be zero */
        #define CCCA_DMA_ACTIVE(ccca) (ccca&0x04000000)
        #define CCCA_DMA_WRITE_MODE(ccca) (ccca&0x02000000)
        #define CCCA_DMA_WRITE_RIGHT(ccca) (ccca&0x01000000)
        
        uint16_t envvol;
        #define ENVVOL_NODELAY(envol) (envvol&0x8000)
        /* Verified with a soundfont bank. 7FFF is the minimum delay time, and 0 is the max delay time */
        #define ENVVOL_TO_EMU_SAMPLES(envvol) (envvol&0x8000) ? 0 : ((0x8000-(envvol&0x7FFF)) <<5)
        
        uint16_t dcysusv;
        #define DCYSUSV_IS_RELEASE(dcysusv) (dcysusv&0x8000)
        #define DCYSUSV_GENERATOR_ENGINE_ON(dcysusv) !(dcysusv&0x0080)
        #define DCYSUSV_SUSVALUE_GET(dcysusv) ((dcysusv>>8)&0x7F)
        /* Inverting the range compared to documentation because the envelope runs from 0dBFS = 0 to -96dBFS = (1 <<21) */
        #define DCYSUSV_SUS_TO_ENV_RANGE(susvalue)  (((0x7F-susvalue) << 21)/0x7F)
        #define DCYSUSV_DECAYRELEASE_GET(dcysusv) (dcysusv&0x7F)
        
        uint16_t envval;
        #define ENVVAL_NODELAY(enval) (envval&0x8000)
        /* Verified with a soundfont bank. 7FFF is the minimum delay time, and 0 is the max delay time */
        #define ENVVAL_TO_EMU_SAMPLES(envval)(envval&0x8000) ? 0 : ((0x8000-(envval&0x7FFF)) <<5)
        
        uint16_t dcysus;
        #define DCYSUS_IS_RELEASE(dcysus) (dcysus&0x8000)
        #define DCYSUS_SUSVALUE_GET(dcysus) ((dcysus>>8)&0x7F)
        #define DCYSUS_SUS_TO_ENV_RANGE(susvalue) ((susvalue << 21)/0x7F)
        #define DCYSUS_DECAYRELEASE_GET(dcysus) (dcysus&0x7F)
        
        uint16_t atkhldv;
        #define ATKHLDV_TRIGGER(atkhldv) !(atkhldv&0x8000)
        #define ATKHLDV_HOLD(atkhldv) ((atkhldv>>8)&0x7F)
        #define ATKHLDV_HOLD_TO_EMU_SAMPLES(atkhldv) (4096*(0x7F-((atkhldv>>8)&0x7F)))
        #define ATKHLDV_ATTACK(atkhldv) (atkhldv&0x7F)
        
        uint16_t lfo1val, lfo2val;
        #define LFOxVAL_NODELAY(lfoxval) (lfoxval&0x8000)
        #define LFOxVAL_TO_EMU_SAMPLES(lfoxval) (lfoxval&0x8000) ? 0 : ((0x8000-(lfoxval&0x7FFF)) <<5)
        
        uint16_t atkhld;
        #define ATKHLD_TRIGGER(atkhld) !(atkhld&0x8000)
        #define ATKHLD_HOLD(atkhld) ((atkhld>>8)&0x7F)
        #define ATKHLD_HOLD_TO_EMU_SAMPLES(atkhld) (4096*(0x7F-((atkhld>>8)&0x7F)))
        #define ATKHLD_ATTACK(atkhld) (atkhld&0x7F)
        
        
        uint16_t ip;
        #define INTIAL_PITCH_CENTER 0xE000
        #define INTIAL_PITCH_OCTAVE 0x1000
        
        union {
                uint16_t ifatn;
                struct{
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
                        int8_t tremfrq_lfo1_tremolo;
                };
        };
        union {
                uint16_t fm2frq2;
                struct {
                        uint8_t fm2frq2_lfo2_freq;
                        int8_t fm2frq2_lfo2_vibrato;
                };
        };
        
        int env_engine_on;
        
        emu8k_mem_internal_t addr, loop_start, loop_end;
        
        int32_t initial_att;
        int32_t initial_filter;

        emu8k_envelope_t vol_envelope;
        emu8k_envelope_t mod_envelope;
        
        int64_t lfo1_speed, lfo2_speed;
        emu8k_mem_internal_t lfo1_count, lfo2_count;
        int32_t lfo1_delay_samples, lfo2_delay_samples;
        int vol_l, vol_r;

        int16_t fixed_modenv_filter_height;
        int16_t fixed_modenv_pitch_height;
        int16_t fixed_lfo1_filt_mod;
        int16_t fixed_lfo1_vibrato;
        int16_t fixed_lfo1_tremolo;
        int16_t fixed_lfo2_vibrato;

        /* filter internal data. */
        int filterq_idx;
        int32_t filt_att;
        int64_t filt_buffer[5];

} emu8k_voice_t;

typedef struct emu8k_t
{
        emu8k_voice_t voice[32];

        uint16_t hwcf1, hwcf2, hwcf3;
        uint32_t hwcf4, hwcf5, hwcf6, hwcf7;

        uint16_t init1[32], init2[32], init3[32], init4[32];
                
        uint32_t smalr, smarr, smalw, smarw;
        uint16_t smld_buffer, smrd_buffer;

        uint16_t wc;
        
        uint16_t c02_read;

        uint16_t id;

        /* The empty block is used to act as an unallocated memory returning zero. */
        int16_t *ram, *rom, *empty;

        /* RAM pointers are a way to avoid checking ram boundaries on read */
        int16_t *ram_pointers[0x100];
        uint32_t ram_end_addr;

        int cur_reg, cur_voice;
        
        int timer_count;
        
        int16_t out_l, out_r;
        
        emu8k_chorus_eng_t chorus_engine;
        int32_t chorus_in_buffer[SOUNDBUFLEN];
        emu8k_reverb_eng_t reverb_engine;
        int32_t reverb_in_buffer[SOUNDBUFLEN];
        
        int pos;
        int32_t buffer[SOUNDBUFLEN * 2];
} emu8k_t;



void emu8k_init(emu8k_t *emu8k, int onboard_ram);
void emu8k_close(emu8k_t *emu8k);

void emu8k_update(emu8k_t *emu8k);
