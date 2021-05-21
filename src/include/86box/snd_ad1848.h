/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for AD1848 / CS4248 / CS4231 (Windows Sound System) codec emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2018-2020 TheCollector1995.
 */

#define AD1848_TYPE_DEFAULT 0
#define AD1848_TYPE_CS4248 1
#define AD1848_TYPE_CS4231 2
#define AD1848_TYPE_CS4236 3


typedef struct {
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


extern void	ad1848_setirq(ad1848_t *ad1848, int irq);
extern void	ad1848_setdma(ad1848_t *ad1848, int dma);

extern uint8_t	ad1848_read(uint16_t addr, void *p);
extern void	ad1848_write(uint16_t addr, uint8_t val, void *p);

extern void	ad1848_update(ad1848_t *ad1848);
extern void	ad1848_speed_changed(ad1848_t *ad1848);

extern void	ad1848_init(ad1848_t *ad1848, int type);
