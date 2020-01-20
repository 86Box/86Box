#include "../timer.h"

typedef struct cs423x_t
{
        int index;
	uint8_t regs[32];
        uint8_t status;
        
        int trd;
        int mce;
	int ia4;
	int mode2;
	int initb;
        
        int count;
        
        int16_t out_l, out_r;
                
        int64_t enable;

        int irq, dma;
        
        int64_t freq;
        
        pc_timer_t timer_count;
	uint64_t timer_latch;

        int16_t buffer[SOUNDBUFLEN * 2];
        int pos;
} cs423x_t;

void cs423x_setirq(cs423x_t *cs423x, int irq);
void cs423x_setdma(cs423x_t *cs423x, int dma);

uint8_t cs423x_read(uint16_t addr, void *p);
void cs423x_write(uint16_t addr, uint8_t val, void *p);

void cs423x_update(cs423x_t *cs423x);
void cs423x_speed_changed(cs423x_t *cs423x);

void cs423x_init(cs423x_t *cs423x);
