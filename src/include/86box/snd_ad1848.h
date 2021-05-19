#define AD1848_TYPE_DEFAULT 0
#define AD1848_TYPE_CS4248 1
#define AD1848_TYPE_CS4231 2
#define AD1848_TYPE_CS4236 3

typedef struct ad1848_t
{
        int index;
        uint8_t regs[32]; /* 16 original + 16 CS4231A extensions */
        uint8_t status;
        
        int trd;
        int mce;
        
        int count;
        
        int16_t out_l, out_r;

	double cd_vol_l, cd_vol_r;

        int enable;

        int irq, dma;
        
        int freq;
        
        pc_timer_t timer_count;
	uint64_t timer_latch;

        int16_t buffer[SOUNDBUFLEN * 2];
        int pos;
	
	int type;
} ad1848_t;

void ad1848_setirq(ad1848_t *ad1848, int irq);
void ad1848_setdma(ad1848_t *ad1848, int dma);

uint8_t ad1848_read(uint16_t addr, void *p);
void ad1848_write(uint16_t addr, uint8_t val, void *p);

void ad1848_update(ad1848_t *ad1848);
void ad1848_speed_changed(ad1848_t *ad1848);

void ad1848_init(ad1848_t *ad1848, int type);
