/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
/*PCem v8.1 by Tom Walker

  ICD2061 clock generator emulation
  Used by ET4000w32/p (Diamond Stealth 32)*/
#include "ibm.h"
#include "vid_icd2061.h"

void icd2061_write(icd2061_t *icd2061, int val)
{
        int q, p, m, i, a;
        if ((val & 1) && !(icd2061->state & 1))
        {
                pclog("ICD2061 write %02X %i %08X %i\n", val, icd2061->unlock, icd2061->data, icd2061->pos);
                if (!icd2061->status)
                {
                        if (val & 2) 
                                icd2061->unlock++;
                        else         
                        {
                                if (icd2061->unlock >= 5)
                                {
                                        icd2061->status = 1;
                                        icd2061->pos = 0;
                                }
                                else
                                   icd2061->unlock = 0;
                        }
                }
                else if (val & 1)
                {
                        icd2061->data = (icd2061->data >> 1) | (((val & 2) ? 1 : 0) << 24);
                        icd2061->pos++;
                        if (icd2061->pos == 26)
                        {
                                pclog("ICD2061 data - %08X\n", icd2061->data);
                                a = (icd2061->data >> 21) & 0x7;
                                if (!(a & 4))
                                {
                                        q = (icd2061->data & 0x7f) - 2;
                                        m = 1 << ((icd2061->data >> 7) & 0x7);
                                        p = ((icd2061->data >> 10) & 0x7f) - 3;
                                        i = (icd2061->data >> 17) & 0xf;
                                        pclog("p %i q %i m %i\n", p, q, m);
                                        if (icd2061->ctrl & (1 << a))
                                           p <<= 1;
                                        icd2061->freq[a] = ((double)p / (double)q) * 2.0 * 14318184.0 / (double)m;
                                        pclog("ICD2061 freq %i = %f\n", a, icd2061->freq[a]);
                                }
                                else if (a == 6)
                                {
                                        icd2061->ctrl = val;
                                        pclog("ICD2061 ctrl = %08X\n", val);
                                }
                                icd2061->unlock = icd2061->data = 0;
                                icd2061->status = 0;
                        }
                }
        }
        icd2061->state = val;
}

double icd2061_getfreq(icd2061_t *icd2061, int i)
{
        pclog("Return freq %f\n", icd2061->freq[i]);
        return icd2061->freq[i];
}
