typedef struct unk_ramdac_t
{
        int state;
        uint8_t ctrl;
} unk_ramdac_t;

void unk_ramdac_out(uint16_t addr, uint8_t val, unk_ramdac_t *ramdac, svga_t *svga);
uint8_t unk_ramdac_in(uint16_t addr, unk_ramdac_t *ramdac, svga_t *svga);
