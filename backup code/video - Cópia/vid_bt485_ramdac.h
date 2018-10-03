/* Copyright holders: Tenshi
   see COPYING for more details
*/
typedef struct bt485_ramdac_t
{
        int magic_count;
        uint8_t command;
        int windex, rindex;
        uint16_t regs[256];
        int reg_ff;
        int rs2;
	int rs3;
} bt485_ramdac_t;

void bt485_ramdac_out(uint16_t addr, uint8_t val, bt485_ramdac_t *ramdac, svga_t *svga);
uint8_t bt485_ramdac_in(uint16_t addr, bt485_ramdac_t *ramdac, svga_t *svga);

float bt485_getclock(int clock, void *p);
