/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
typedef struct unk_ramdac_t
{
        int state;
        uint8_t ctrl;
} sc1502x_ramdac_t;

void sc1502x_ramdac_out(uint16_t addr, uint8_t val, sc1502x_ramdac_t *ramdac, svga_t *svga);
uint8_t sc1502x_ramdac_in(uint16_t addr, sc1502x_ramdac_t *ramdac, svga_t *svga);
