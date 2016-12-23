/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include "ibm.h"

#include "disc.h"
#include "fdc.h"
#include "fdd.h"
#include "io.h"
#include "lpt.h"
#include "serial.h"
#include "fdc37c665.h"

static uint8_t fdc37c665_lock[2];
static int fdc37c665_curreg;
static uint8_t fdc37c665_regs[16];

static void write_lock(uint8_t val)
{
        if (val == 0x55 && fdc37c665_lock[1] == 0x55)
                fdc_3f1_enable(0);
        if (fdc37c665_lock[0] == 0x55 && fdc37c665_lock[1] == 0x55 && val != 0x55)
                fdc_3f1_enable(1);

        fdc37c665_lock[0] = fdc37c665_lock[1];
        fdc37c665_lock[1] = val;
}

void fdc37c665_write(uint16_t port, uint8_t val, void *priv)
{
        // pclog("Write SuperIO %04x %02x\n", port, val);
        if (fdc37c665_lock[0] == 0x55 && fdc37c665_lock[1] == 0x55)
        {
                if (port == 0x3f0)
                {
                        if (val == 0xaa)
                                write_lock(val);
                        else
                                fdc37c665_curreg = val & 0xf;
                }
                else
                {
                        uint16_t com3_addr, com4_addr;
                        fdc37c665_regs[fdc37c665_curreg] = val;
//                        pclog("Write superIO %02x %02x %04x(%08x):%08x\n", fdc37c665_curreg, val, CS, cs, pc);
                        
                        switch (fdc37c665_regs[1] & 0x60)
                        {
                                case 0x00:
                                com3_addr = 0x338;
                                com4_addr = 0x238;
                                break;
                                case 0x20:
                                com3_addr = 0x3e8;
                                com4_addr = 0x2e8;
                                break;
                                case 0x40:
                                com3_addr = 0x3e8;
                                com4_addr = 0x2e0;
                                break;
                                case 0x60:
                                com3_addr = 0x220;
                                com4_addr = 0x228;
                                break;
                        }
                        
                        if (!(fdc37c665_regs[2] & 4))
                                serial1_remove();
                        else switch (fdc37c665_regs[2] & 3)
                        {
                                case 0:
                                serial1_set(0x3f8, 4);
                                break;
                                case 1:
                                serial1_set(0x2f8, 4);
                                break;
                                case 2:
                                serial1_set(com3_addr, 4);
                                break;
                                case 3:
                                serial1_set(com4_addr, 4);
                                break;
                        }

                        if (!(fdc37c665_regs[2] & 0x40))
                                serial2_remove();
                        else switch (fdc37c665_regs[2] & 0x30)
                        {
                                case 0x00:
                                serial2_set(0x3f8, 3);
                                break;
                                case 0x10:
                                serial2_set(0x2f8, 3);
                                break;
                                case 0x20:
                                serial2_set(com3_addr, 3);
                                break;
                                case 0x30:
                                serial2_set(com4_addr, 3);
                                break;
                        }

                        lpt1_remove();
                        lpt2_remove();
                        switch (fdc37c665_regs[1] & 3)
                        {
                                case 1:
                                lpt1_init(0x3bc);
                                break;
                                case 2:
                                lpt1_init(0x378);
                                break;
                                case 3:
                                lpt1_init(0x278);
                                break;
                        }

			fdc_update_enh_mode((fdc37c665_regs[3] & 2) ? 1 : 0);

			fdc_update_densel_force((fdc37c665_regs[5] & 0x18) >> 3);
			fdd_swap = ((fdc37c665_regs[5] & 0x20) >> 5);
                }
        }
        else
        {
                if (port == 0x3f0)
                        write_lock(val);
        }
}

uint8_t fdc37c665_read(uint16_t port, void *priv)
{
        // pclog("Read SuperIO %04x %02x\n", port, fdc37c665_curreg);
        if (fdc37c665_lock[0] == 0x55 && fdc37c665_lock[1] == 0x55)
        {
                if (port == 0x3f1)
                        return fdc37c665_regs[fdc37c665_curreg];
        }
        return 0xff;
}

void fdc37c665_init()
{
        io_sethandler(0x03f0, 0x0002, fdc37c665_read, NULL, NULL, fdc37c665_write, NULL, NULL,  NULL);

        fdc_update_is_nsc(0);
        
        fdc37c665_lock[0] = fdc37c665_lock[1] = 0;
        fdc37c665_regs[0x0] = 0x3b;
        fdc37c665_regs[0x1] = 0x9f;
        fdc37c665_regs[0x2] = 0xdc;
        fdc37c665_regs[0x3] = 0x78;
        fdc37c665_regs[0x4] = 0x00;
        fdc37c665_regs[0x5] = 0x00;
        fdc37c665_regs[0x6] = 0xff;
        fdc37c665_regs[0x7] = 0x00;
        fdc37c665_regs[0x8] = 0x00;
        fdc37c665_regs[0x9] = 0x00;
        fdc37c665_regs[0xa] = 0x00;
        fdc37c665_regs[0xb] = 0x00;
        fdc37c665_regs[0xc] = 0x00;
        fdc37c665_regs[0xd] = 0x65;
        fdc37c665_regs[0xe] = 0x01;
        fdc37c665_regs[0xf] = 0x00;

	fdc_update_densel_polarity(1);
	fdc_update_densel_force(0);
	fdd_swap = 0;
}
