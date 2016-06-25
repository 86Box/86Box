typedef struct ati68860_ramdac_t
{
        uint8_t regs[16];
        void (*render)(struct svga_t *svga);
        
        int dac_write, dac_pos;
        int dac_r, dac_g;
        PALETTE pal;
        uint32_t pallook[2];
        
        int ramdac_type;
} ati68860_ramdac_t;

void ati68860_ramdac_out(uint16_t addr, uint8_t val, ati68860_ramdac_t *ramdac, svga_t *svga);
uint8_t ati68860_ramdac_in(uint16_t addr, ati68860_ramdac_t *ramdac, svga_t *svga);
void ati68860_ramdac_init(ati68860_ramdac_t *ramdac);
void ati68860_set_ramdac_type(ati68860_ramdac_t *ramdac, int type);
