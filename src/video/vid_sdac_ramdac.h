/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
typedef struct sdac_ramdac_t
{
        int magic_count;
        uint8_t command;
        int windex, rindex;
        uint16_t regs[256];
        int reg_ff;
        int rs2;
} sdac_ramdac_t;

void sdac_init(sdac_ramdac_t *ramdac);

void sdac_ramdac_out(uint16_t addr, int rs2, uint8_t val, sdac_ramdac_t *ramdac, svga_t *svga);
uint8_t sdac_ramdac_in(uint16_t addr, int rs2, sdac_ramdac_t *ramdac, svga_t *svga);

float sdac_getclock(int clock, void *p);
