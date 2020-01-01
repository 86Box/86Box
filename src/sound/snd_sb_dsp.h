typedef struct sb_dsp_t
{
        int sb_type;

        int sb_8_length,  sb_8_format,  sb_8_autoinit,  sb_8_pause,  sb_8_enable,  sb_8_autolen,  sb_8_output;
        int sb_8_dmanum;
        int sb_16_length, sb_16_format, sb_16_autoinit, sb_16_pause, sb_16_enable, sb_16_autolen, sb_16_output;
        int sb_16_dmanum;
        int sb_pausetime;

        uint8_t sb_read_data[256];
		int sb_read_wp, sb_read_rp;
        int sb_speaker;
        int muted;

        int sb_data_stat;
		
		int midi_in_sysex;
		int midi_in_poll;
		int uart_midi;
		int uart_irq;
		int onebyte_midi;
		int midi_in_timestamp;

        int sb_irqnum;

        uint8_t sbe2;
        int sbe2count;

        uint8_t sb_data[8];

        int sb_freq;
        
        int16_t sbdat;
        int sbdat2;
        int16_t sbdatl, sbdatr;

        uint8_t sbref;
        int8_t sbstep;

        int sbdacpos;

        int sbleftright;

        int sbreset;
        uint8_t sbreaddat;
        uint8_t sb_command;
        uint8_t sb_test;
        int sb_timei, sb_timeo;

        int sb_irq8, sb_irq16;

        uint8_t sb_asp_regs[256];
        
        int sbenable, sb_enable_i;
        
        pc_timer_t output_timer, input_timer;
        
        uint64_t sblatcho, sblatchi;
        
        uint16_t sb_addr;
        
        int stereo;
        
        int asp_data_len;
        
        pc_timer_t wb_timer;
		int wb_full;

	int busy_count;

        int record_pos_read;
        int record_pos_write;
        int16_t record_buffer[0xFFFF];
        int16_t buffer[SOUNDBUFLEN * 2];
        int pos;
} sb_dsp_t;

extern sb_dsp_t *dspin;

void sb_dsp_input_msg(uint8_t *msg);

int sb_dsp_input_sysex(uint8_t *buffer, uint32_t len, int abort);

void sb_dsp_set_mpu(mpu_t *src_mpu);

void sb_dsp_init(sb_dsp_t *dsp, int type);
void sb_dsp_close(sb_dsp_t *dsp);

void sb_dsp_setirq(sb_dsp_t *dsp, int irq);
void sb_dsp_setdma8(sb_dsp_t *dsp, int dma);
void sb_dsp_setdma16(sb_dsp_t *dsp, int dma);
void sb_dsp_setaddr(sb_dsp_t *dsp, uint16_t addr);

void sb_dsp_speed_changed(sb_dsp_t *dsp);

void sb_dsp_poll(sb_dsp_t *dsp, int16_t *l, int16_t *r);

void sb_dsp_set_stereo(sb_dsp_t *dsp, int stereo);

void sb_dsp_update(sb_dsp_t *dsp);
