#include "ibm.h"
#include "io.h"
#include "acerm3a.h"

static int acerm3a_index;

static void acerm3a_out(uint16_t port, uint8_t val, void *p)
{
        if (port == 0xea)
                acerm3a_index = val;
}

static uint8_t acerm3a_in(uint16_t port, void *p)
{
        if (port == 0xeb)
        {
                switch (acerm3a_index)
                {
                        case 2:
                        return 0xfd;
                }
        }
        return 0xff;
}

void acerm3a_io_init()
{
        io_sethandler(0x00ea, 0x0002, acerm3a_in, NULL, NULL, acerm3a_out, NULL, NULL, NULL);
}
