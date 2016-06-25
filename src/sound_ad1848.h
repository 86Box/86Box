#include "timer.h"

typedef struct ad1848_t
{
        int index;
        uint8_t regs[16];
        uint8_t status;
        
        int trd;
        int mce;
        
        int count;
        
        int16_t out_l, out_r;
                
        int enable;

        int irq, dma;
        
        int freq;
        
        int timer_count, timer_latch;

        int16_t buffer[SOUNDBUFLEN * 2];
        int pos;
} ad1848_t;

void ad1848_setirq(ad1848_t *ad1848, int irq);
void ad1848_setdma(ad1848_t *ad1848, int dma);

uint8_t ad1848_read(uint16_t addr, void *p);
void ad1848_write(uint16_t addr, uint8_t val, void *p);

void ad1848_update(ad1848_t *ad1848);
void ad1848_speed_changed(ad1848_t *ad1848);
