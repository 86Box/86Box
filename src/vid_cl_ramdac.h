typedef struct cl_ramdac_t
{
        int state;
        uint8_t ctrl;
} cl_ramdac_t;

void cl_ramdac_out(uint16_t addr, uint8_t val, cl_ramdac_t *ramdac, void *clgd, svga_t *svga);
uint8_t cl_ramdac_in(uint16_t addr, cl_ramdac_t *ramdac, void *clgd, svga_t *svga);
