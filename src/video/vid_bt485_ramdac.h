/* Copyright holders: Tenshi
   see COPYING for more details
*/
typedef struct bt485_ramdac_t
{
	PALETTE extpal;
	uint32_t extpallook[256];
	uint8_t cursor32_data[256];
	uint8_t cursor64_data[1024];
        int set_reg0a;
	int hwc_y, hwc_x;
	uint8_t cr0;
        uint8_t cr1;
        uint8_t cr2;
	uint8_t cr3;
} bt485_ramdac_t;

void bt485_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, bt485_ramdac_t *ramdac, svga_t *svga);
uint8_t bt485_ramdac_in(uint16_t addr, int rs2, int rs3, bt485_ramdac_t *ramdac, svga_t *svga);

