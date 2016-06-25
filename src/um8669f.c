/*um8669f :
        
  aa to 108 unlocks
  next 108 write is register select (Cx?)
  data read/write to 109
  55 to 108 locks
  
  


C0
bit 3 = LPT1 enable
bit 2 = COM2 enable
bit 1 = COM1 enable
bit 0 = FDC enable

C1
bits 7-6 = LPT1 mode : 11 = ECP/EPP, 01 = EPP, 10 = SPP
bit 3 = clear when LPT1 = 278

C3
bits 7-6 = LPT1 DMA mode : 11 = ECP/EPP DMA1, 10 = ECP/EPP DMA3, 01 = EPP/SPP, 00 = ECP
bits 5-4 = LPT1 addr : 10 = 278/IRQ5, 01 = 3BC/IRQ7, 00 = 378/IRQ7

COM1 :
3f8, IRQ4 - C1 = BF, C3 = 00
2f8, IRQ3 - C1 = BF, C3 = 03
3e8, IRQ4 - C1 = BD, C3 = 00
2e8, IRQ3 - B1 = BD, C3 = 03

COM2 :
3f8, IRQ4 - C1 = BF, C3 = 0C
2f8, IRQ3 - C1 = BF, C3 = 00
3e8, IRQ4 - C1 = BB, C3 = 0C
2e8, IRQ3 - C1 = BB, C3 = 00

  */

#include "ibm.h"

#include "fdc.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "um8669f.h"

static int um8669f_locked;
static int um8669f_curreg;
static uint8_t um8669f_regs[256];

void um8669f_write(uint16_t port, uint8_t val, void *priv)
{
        int temp;
//        pclog("um8669f_write : port=%04x reg %02X = %02X locked=%i\n", port, um8669f_curreg, val, um8669f_locked);
        if (um8669f_locked)
        {
                if (port == 0x108 && val == 0xaa)
                   um8669f_locked = 0;
        }
        else
        {
                if (port == 0x108)
                {
                        if (val == 0x55)
                           um8669f_locked = 1;
                        else
                           um8669f_curreg = val;
                }
                else
                {
                        um8669f_regs[um8669f_curreg] = val;

                        fdc_remove();
                        if (um8669f_regs[0xc0] & 1)
                           fdc_add();
                           
                        if (um8669f_regs[0xc0] & 2)
                        {
                                temp = um8669f_regs[0xc3] & 1; /*might be & 2*/
                                if (!(um8669f_regs[0xc1] & 2))
                                   temp |= 2;
                                switch (temp)
                                {
                                        case 0: serial1_set(0x3f8, 4); break;
                                        case 1: serial1_set(0x2f8, 4); break;
                                        case 2: serial1_set(0x3e8, 4); break;
                                        case 3: serial1_set(0x2e8, 4); break;
                                }
                        }
                        
                        if (um8669f_regs[0xc0] & 4)
                        {
                                temp = (um8669f_regs[0xc3] & 4) ? 0 : 1; /*might be & 8*/
                                if (!(um8669f_regs[0xc1] & 4))
                                   temp |= 2;
                                switch (temp)
                                {
                                        case 0: serial2_set(0x3f8, 3); break;
                                        case 1: serial2_set(0x2f8, 3); break;
                                        case 2: serial2_set(0x3e8, 3); break;
                                        case 3: serial2_set(0x2e8, 3); break;
                                }
                        }
                        
                        mouse_serial_init();
                        
                        lpt1_remove();
                        lpt2_remove();
                        temp = (um8669f_regs[0xc3] >> 4) & 3;
                        switch (temp)
                        {
                                case 0: lpt1_init(0x378); break;
                                case 1: lpt1_init(0x3bc); break;
                                case 2: lpt1_init(0x278); break;
                        }
                }
        }
}

uint8_t um8669f_read(uint16_t port, void *priv)
{
//        pclog("um8669f_read : port=%04x reg %02X locked=%i\n", port, um8669f_curreg, um8669f_locked);
        if (um8669f_locked)
           return 0xff;
        
        if (port == 0x108)
           return um8669f_curreg; /*???*/
        else
           return um8669f_regs[um8669f_curreg];
}

void um8669f_init()
{
        io_sethandler(0x0108, 0x0002, um8669f_read, NULL, NULL, um8669f_write, NULL, NULL,  NULL);
        um8669f_locked = 1;
}
