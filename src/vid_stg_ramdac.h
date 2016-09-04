typedef struct stg_ramdac_t
{
        int magic_count;
        uint8_t command;
        int index;
        uint8_t regs[256];
} stg_ramdac_t;

void stg_ramdac_out(uint16_t addr, uint8_t val, stg_ramdac_t *ramdac, svga_t *svga);
uint8_t stg_ramdac_in(uint16_t addr, stg_ramdac_t *ramdac, svga_t *svga);
float stg_getclock(int clock, void *p);
