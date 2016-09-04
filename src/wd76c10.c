#include "ibm.h"
#include "fdc.h"
#include "io.h"
#include "mem.h"
#include "serial.h"
#include "wd76c10.h"

static uint16_t wd76c10_0092;
static uint16_t wd76c10_2072;
static uint16_t wd76c10_2872;
static uint16_t wd76c10_5872;

uint16_t wd76c10_read(uint16_t port, void *priv)
{
        switch (port)
        {
                case 0x0092:
                return wd76c10_0092;
                
                case 0x2072:
                return wd76c10_2072;

                case 0x2872:
                return wd76c10_2872;
                
                case 0x5872:
                return wd76c10_5872;
        }
        return 0;
}

void wd76c10_write(uint16_t port, uint16_t val, void *priv)
{
        pclog("WD76C10 write %04X %04X\n", port, val);
        switch (port)
        {
                case 0x0092:
                wd76c10_0092 = val;
                        
                mem_a20_alt = val & 2;
                mem_a20_recalc();
                break;
                
                case 0x2072:
                wd76c10_2072 = val;
                
                switch ((val >> 5) & 7)
                {
                        case 1: serial1_set(0x3f8, 4); break;
                        case 2: serial1_set(0x2f8, 4); break;
                        case 3: serial1_set(0x3e8, 4); break;
                        case 4: serial1_set(0x2e8, 4); break;
                }
                switch ((val >> 1) & 7)
                {
                        case 1: serial2_set(0x3f8, 3); break;
                        case 2: serial2_set(0x2f8, 3); break;
                        case 3: serial2_set(0x3e8, 3); break;
                        case 4: serial2_set(0x2e8, 3); break;
                }
                break;

                case 0x2872:
                wd76c10_2872 = val;
                
                fdc_remove();
                if (!(val & 1))
                   fdc_add();
                break;
                
                case 0x5872:
                wd76c10_5872 = val;
                break;
        }
}

uint8_t wd76c10_readb(uint16_t port, void *priv)
{
        if (port & 1)
           return wd76c10_read(port & ~1, priv) >> 8;
        return wd76c10_read(port, priv) & 0xff;
}

void wd76c10_writeb(uint16_t port, uint8_t val, void *priv)
{
        uint16_t temp = wd76c10_read(port, priv);
        if (port & 1)
           wd76c10_write(port & ~1, (temp & 0x00ff) | (val << 8), priv);
        else
           wd76c10_write(port     , (temp & 0xff00) | val, priv);
}

void wd76c10_init()
{
        io_sethandler(0x0092, 0x0002, wd76c10_readb, wd76c10_read, NULL, wd76c10_writeb, wd76c10_write, NULL,  NULL);
        io_sethandler(0x2072, 0x0002, wd76c10_readb, wd76c10_read, NULL, wd76c10_writeb, wd76c10_write, NULL,  NULL);
        io_sethandler(0x2872, 0x0002, wd76c10_readb, wd76c10_read, NULL, wd76c10_writeb, wd76c10_write, NULL,  NULL);
        io_sethandler(0x5872, 0x0002, wd76c10_readb, wd76c10_read, NULL, wd76c10_writeb, wd76c10_write, NULL,  NULL);
}
