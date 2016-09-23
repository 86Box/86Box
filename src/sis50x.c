#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "pci.h"

#include "sis50x.h"

typedef struct sis501_t
{
        uint8_t pci_conf[256];
	uint8_t turbo_reg;
} sis501_t;

typedef struct sis503_t
{
        uint8_t pci_conf[256];
} sis503_t;

typedef struct sis50x_t
{
        uint8_t isa_conf[12];
	uint8_t reg;
} sis50x_t;

void sis501_recalcmapping(sis501_t *sis501)
{
        int c, d;
        
	for (c = 0; c < 1; c++)
	{
	        for (d = 0; d < 4; d++)
        	{
			// uint32_t base = (((2 - c) << 16) + 0xc0000) + (d << 14);
			uint32_t base = 0xe0000 + (d << 14);
	                if (sis501->pci_conf[0x54 + c] & (1 << (d + 4)))
	                {
        	                switch (sis501->pci_conf[0x53] & 0x60)
	                        {
                	                case 0x00:
        	                        mem_set_mem_state(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
	                                break;
                	                case 0x20:
        	                        mem_set_mem_state(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
	                                break;
                        	        case 0x40:
                	                mem_set_mem_state(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        	                        break;
	                                case 0x60:
                                	mem_set_mem_state(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                        	        break;
                	        }
        	        }
	                else
        	                mem_set_mem_state(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
	        }
	}

        flushmmucache();
        shadowbios = 1;
}

void sis501_write(int func, int addr, uint8_t val, void *p)
{
        sis501_t *sis501 = (sis501_t *)p;
        //pclog("sis501_write : addr=%02x val=%02x\n", addr, val);
        switch (addr)
        {
                case 0x54: /*Shadow configure*/
                if ((sis501->pci_conf[0x54] & val) ^ 0xf0)
                {
                        sis501->pci_conf[0x54] = val;
                        sis501_recalcmapping(sis501);
                }
                break;
        }
                
        if ((addr >= 4 && addr < 8) || addr >= 0x40)
           sis501->pci_conf[addr] = val;
}

void sis501_turbo_write(uint16_t port, uint8_t val, void *priv)
{
	sis501_t *sis501 = (sis501_t *)priv;

	uint8_t valxor = val ^ sis501->turbo_reg;

	sis501->turbo_reg = val;

	if ((val & 4) && (valxor & 4))
	{
		if (sis501->turbo_reg & 2)
			resetpchard();
		else
			softresetx86();
	}
}

void sis503_write(int func, int addr, uint8_t val, void *p)
{
        sis503_t *sis503 = (sis503_t *)p;
        //pclog("sis503_write : addr=%02x val=%02x\n", addr, val);

        if ((addr >= 4 && addr < 8) || addr >= 0x0f)
           sis503->pci_conf[addr] = val;
}

void sis50x_write(uint16_t port, uint8_t val, void *priv)
{
	sis50x_t *sis50x = (sis50x_t *)priv;

	if (port & 1)
	{
		if (sis50x->reg <= 0xB)  sis50x->isa_conf[sis50x->reg] = val;
	}
	else
	{
		sis50x->reg = val;
	}
}

uint8_t sis501_read(int func, int addr, void *p)
{
        sis501_t *sis501 = (sis501_t *)p;
        
        return sis501->pci_conf[addr];
}

uint8_t sis501_turbo_read(uint16_t port, void *priv)
{
        sis501_t *sis501 = (sis501_t *)priv;
        
        return sis501->turbo_reg;
}
 
uint8_t sis503_read(int func, int addr, void *p)
{
        sis503_t *sis503 = (sis503_t *)p;
        
        return sis503->pci_conf[addr];
}
 
uint8_t sis50x_read(uint16_t port, void *priv)
{
	sis50x_t *sis50x = (sis50x_t *)priv;

	if (port & 1)
	{
		if (sis50x->reg <= 0xB)
			return sis50x->isa_conf[sis50x->reg];
		else
			return 0xff;
	}
	else
	{
		return sis50x->reg;
	}
}

void *sis501_init()
{
        sis501_t *sis501 = malloc(sizeof(sis501_t));
        memset(sis501, 0, sizeof(sis501_t));

	// io_sethandler(0x0cf9, 0x0001, sis501_turbo_read, NULL, NULL, sis501_turbo_write, NULL, NULL,  sis501);
        
        // pci_add_specific(5, sis501_read, sis501_write, sis501);
        pci_add_specific(0, sis501_read, sis501_write, sis501);
        
        sis501->pci_conf[0x00] = 0x39; /*SiS*/
        sis501->pci_conf[0x01] = 0x10; 
        sis501->pci_conf[0x02] = 0x06; /*501/502*/
        sis501->pci_conf[0x03] = 0x04; 

        sis501->pci_conf[0x04] = 7;
        sis501->pci_conf[0x05] = 0;

        sis501->pci_conf[0x06] = 0x80;
        sis501->pci_conf[0x07] = 0x02;
        
        sis501->pci_conf[0x08] = 0; /*Device revision*/

        sis501->pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
        sis501->pci_conf[0x0a] = 0x00;
        sis501->pci_conf[0x0b] = 0x06;
        
        sis501->pci_conf[0x0e] = 0x00; /*Single function device*/

	shadowbios = 1;

	return sis501;
}

void *sis503_init()
{
        sis503_t *sis503 = malloc(sizeof(sis503_t));
        memset(sis503, 0, sizeof(sis503_t));
        
        // pci_add_specific(6, sis503_read, sis503_write, sis503);
        pci_add_specific(1, sis503_read, sis503_write, sis503);
        
        sis503->pci_conf[0x00] = 0x39; /*SiS*/
        sis503->pci_conf[0x01] = 0x10; 
        sis503->pci_conf[0x02] = 0x08; /*503*/
        sis503->pci_conf[0x03] = 0x00; 

        sis503->pci_conf[0x04] = 7;
        sis503->pci_conf[0x05] = 0;

        sis503->pci_conf[0x06] = 0x80;
        sis503->pci_conf[0x07] = 0x02;
        
        sis503->pci_conf[0x08] = 0; /*Device revision*/

        sis503->pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
        sis503->pci_conf[0x0a] = 0x01;
        sis503->pci_conf[0x0b] = 0x06;
        
        sis503->pci_conf[0x0e] = 0x00; /*Single function device*/

	return sis503;
}

void *sis50x_init()
{
        sis50x_t *sis50x = malloc(sizeof(sis50x_t));
        memset(sis50x, 0, sizeof(sis50x_t));

	io_sethandler(0x22, 0x0002, sis50x_read, NULL, NULL, sis50x_write, NULL, NULL,  sis50x);
}

void sis501_close(void *p)
{
        sis501_t *sis501 = (sis501_t *)p;

        free(sis501);
}

void sis503_close(void *p)
{
        sis503_t *sis503 = (sis503_t *)p;

        free(sis503);
}

void sis50x_close(void *p)
{
        sis50x_t *sis50x = (sis50x_t *)p;

        free(sis50x);
}

device_t sis501_device =
{
        "SiS 501/502",
        0,
        sis501_init,
        sis501_close,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t sis503_device =
{
        "SiS 503",
        0,
        sis503_init,
        sis503_close,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t sis50x_device =
{
        "SiS 50x ISA",
        0,
        sis50x_init,
        sis50x_close,
        NULL,
        NULL,
        NULL,
        NULL
};
