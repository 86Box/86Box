typedef struct tkd8001_ramdac_t
{
        int state;
        uint8_t ctrl;
} tkd8001_ramdac_t;

void tkd8001_ramdac_out(uint16_t addr, uint8_t val, tkd8001_ramdac_t *ramdac, svga_t *svga);
uint8_t tkd8001_ramdac_in(uint16_t addr, tkd8001_ramdac_t *ramdac, svga_t *svga);
