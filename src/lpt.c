#include "ibm.h"
#include "io.h"

#include "lpt.h"

static uint8_t lpt1_dat, lpt2_dat;
static uint8_t lpt1_ctrl, lpt2_ctrl;

void lpt1_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port & 3)
        {
                case 0:
                writedac(val);
                lpt1_dat = val;
                break;
                case 2:
                writedacctrl(val);
                lpt1_ctrl = val;
                break;
        }
}
uint8_t lpt1_read(uint16_t port, void *priv)
{
        switch (port & 3)
        {
                case 0:
                return lpt1_dat;
                case 1:
                return readdacfifo();
                case 2:
                return lpt1_ctrl;
        }
        return 0xff;
}

void lpt2_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port & 3)
        {
                case 0:
                writedac(val);
                lpt2_dat = val;
                break;
                case 2:
                writedacctrl(val);
                lpt2_ctrl = val;
                break;
        }
}
uint8_t lpt2_read(uint16_t port, void *priv)
{
        switch (port & 3)
        {
                case 0:
                return lpt2_dat;
                case 1:
                return readdacfifo();
                case 2:
                return lpt2_ctrl;
        }
        return 0xff;
}

void lpt_init()
{
        io_sethandler(0x0378, 0x0003, lpt1_read, NULL, NULL, lpt1_write, NULL, NULL,  NULL);
        io_sethandler(0x0278, 0x0003, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
}

void lpt1_init(uint16_t port)
{
        io_sethandler(port, 0x0003, lpt1_read, NULL, NULL, lpt1_write, NULL, NULL,  NULL);
}
void lpt1_remove()
{
        io_removehandler(0x0278, 0x0003, lpt1_read, NULL, NULL, lpt1_write, NULL, NULL,  NULL);
        io_removehandler(0x0378, 0x0003, lpt1_read, NULL, NULL, lpt1_write, NULL, NULL,  NULL);
        io_removehandler(0x03bc, 0x0003, lpt1_read, NULL, NULL, lpt1_write, NULL, NULL,  NULL);
}
void lpt2_init(uint16_t port)
{
        io_sethandler(port, 0x0003, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
}
void lpt2_remove()
{
        io_removehandler(0x0278, 0x0003, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
        io_removehandler(0x0378, 0x0003, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
        io_removehandler(0x03bc, 0x0003, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
}

void lpt2_remove_ams()
{
        io_removehandler(0x0379, 0x0002, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
}
