#include "ibm.h"

#include "io.h"
#include "lpt.h"
#include "serial.h"

static void cbm_io_write(uint16_t port, uint8_t val, void *p)
{
        lpt1_remove();
        lpt2_remove();
        switch (val & 3)
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
        switch (val & 0xc)
        {
                case 0x4:
                serial_setup(1, 0x2f8, 3);
                break;
                case 0x8:
                serial_setup(1, 0x3f8, 4);
                break;
        }
}

void cbm_io_init()
{
        io_sethandler(0x0230, 0x0001, NULL,NULL,NULL, cbm_io_write,NULL,NULL, NULL);
}
