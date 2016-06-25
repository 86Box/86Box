/*It is unknown exactly what RAMDAC this is
  It is possibly a Sierra 1502x
  It's addressed by the TLIVESA1 driver for ET4000*/
#include "ibm.h"
#include "mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_unk_ramdac.h"

void unk_ramdac_out(uint16_t addr, uint8_t val, unk_ramdac_t *ramdac, svga_t *svga)
{
        //pclog("OUT RAMDAC %04X %02X\n",addr,val);
        switch (addr)
        {
                case 0x3C6:
                if (ramdac->state == 4)
                {
                        ramdac->state = 0;
                        ramdac->ctrl = val;
                        switch ((val&1)|((val&0xE0)>>4))
                        {
                                case 0: case 1: case 2: case 3:
                                svga->bpp = 8;
                                break;
                                case 6: case 7:
                                svga->bpp = 15;
                                break;
                                case 8: case 9: case 0xA: case 0xB:
                                svga->bpp = 16;
                                break;
                                case 0xC: case 0xD: case 0xE: case 0xF:
                                svga->bpp = 24;
                                break;
                        }
                        return;
                }
                ramdac->state = 0;
                break;
                case 0x3C7: case 0x3C8: case 0x3C9:
                ramdac->state = 0;
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t unk_ramdac_in(uint16_t addr, unk_ramdac_t *ramdac, svga_t *svga)
{
        //pclog("IN RAMDAC %04X\n",addr);
        switch (addr)
        {
                case 0x3C6:
                if (ramdac->state == 4)
                {
                        ramdac->state = 0;
                        return ramdac->ctrl;
                }
                ramdac->state++;
                break;
                case 0x3C7: case 0x3C8: case 0x3C9:
                ramdac->state = 0;
                break;
        }
        return svga_in(addr, svga);
}
