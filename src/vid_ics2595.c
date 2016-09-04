/*ICS2595 clock chip emulation
  Used by ATI Mach64*/

#include "ibm.h"
#include "vid_ics2595.h"

enum
{
        ICS2595_IDLE = 0,
        ICS2595_WRITE,
        ICS2595_READ
};

static int ics2595_div[4] = {8, 4, 2, 1};

void ics2595_write(ics2595_t *ics2595, int strobe, int dat)
{
//        pclog("ics2595_write : %i %i\n", strobe, dat);
        if (strobe)
        {
                if ((dat & 8) && !ics2595->oldfs3) /*Data clock*/
                {
//                        pclog(" - new dat %i\n", dat & 4);
                        switch (ics2595->state)
                        {
                                case ICS2595_IDLE:
                                ics2595->state = (dat & 4) ? ICS2595_WRITE : ICS2595_IDLE;
                                ics2595->pos = 0;
                                break;
                                case ICS2595_WRITE:
                                ics2595->dat = (ics2595->dat >> 1);
                                if (dat & 4)
                                        ics2595->dat |= (1 << 19);
                                ics2595->pos++;
                                if (ics2595->pos == 20)
                                {
                                        int d, n, l;
//                                        pclog("ICS2595_WRITE : dat %08X\n", ics2595->dat);
                                        l = (ics2595->dat >> 2) & 0xf;
                                        n = ((ics2595->dat >> 7) & 255) + 257;
                                        d = ics2595_div[(ics2595->dat >> 16) & 3];

                                        ics2595->clocks[l] = (14318181.8 * ((double)n / 46.0)) / (double)d;
//                                        pclog("ICS2595 clock set - L %i N %i D %i freq = %f\n", l, n, d, (14318181.8 * ((double)n / 46.0)) / (double)d);
                                        ics2595->state = ICS2595_IDLE;
                                }
                                break;                                                
                        }
                }
                        
                ics2595->oldfs2 = dat & 4;
                ics2595->oldfs3 = dat & 8;
        }
        ics2595->output_clock = ics2595->clocks[dat];
}
