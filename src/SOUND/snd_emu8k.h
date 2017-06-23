#define EMU8K_MEM_ADDRESS_MASK 0xFFFFFF
#define EMU8K_RAM_MEM_START 0x200000
#define EMU8K_ROM_MEM_1MB_END 0x80000 

typedef struct emu8k_t
{
        struct
        {
                uint32_t cpf;
                uint32_t ptrx;
                uint32_t cvcf;
                uint32_t vtft;
                uint32_t psst;
                uint32_t csl;
                
                uint32_t ccca;

                uint16_t init1, init2, init3, init4;
                
                uint16_t envvol;
                uint16_t dcysusv;
                uint16_t envval;
                uint16_t dcysus;
                uint16_t atkhldv;
                uint16_t lfo1val, lfo2val;
                uint16_t atkhld;
                uint16_t ip;
                uint16_t ifatn;
                uint16_t pefe;
                uint16_t fmmod;
                uint16_t tremfrq;
                uint16_t fm2frq2;
                
                int voice_on;
                
                uint64_t addr;
                uint64_t loop_start, loop_end;
                
                uint16_t pitch;
                int attenuation;
                int env_state, env_vol;
                int env_attack, env_decay, env_sustain, env_release;

                int menv_state, menv_vol;
                int menv_attack, menv_decay, menv_sustain, menv_release;
                
                int lfo1_count, lfo2_count;
                int8_t lfo1_fmmod, lfo2_fmmod;
                int8_t lfo1_trem;
                int vol_l, vol_r;
                
                int8_t fe_height;
                
                int64_t vlp, vbp, vhp;
                int32_t q;
                
                int filter_offset;
                
/*                float vlp, vbp, vhp;
                float q;*/
                
                int cutoff;
        } voice[32];

        uint32_t hwcf1, hwcf2, hwcf3;
        uint32_t hwcf4, hwcf5, hwcf6;
                
        uint32_t smalr, smarr, smalw, smarw;
        uint16_t smld_buffer, smrd_buffer;

        uint16_t wc;
        
        uint16_t c02_read;

	uint16_t id;
        
        int16_t *ram, *rom;
        
        uint32_t ram_end_addr;
        
        int cur_reg, cur_voice;
        
        int timer_count;
        
        int16_t out_l, out_r;
        
        int pos;
        int32_t buffer[SOUNDBUFLEN * 2];
} emu8k_t;

void emu8k_init(emu8k_t *emu8k, int onboard_ram);
void emu8k_close(emu8k_t *emu8k);

void emu8k_update(emu8k_t *emu8k);
