#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "../io.h"
#include "../mca.h"
#include "../device.h"
#include "sound.h"
#include "snd_adlib.h"
#include "snd_opl.h"


typedef struct adlib_t
{
        opl_t   opl;
        
        uint8_t pos_regs[8];
} adlib_t;


static void adlib_get_buffer(int32_t *buffer, int len, void *p)
{
        adlib_t *adlib = (adlib_t *)p;
        int c;

        opl2_update2(&adlib->opl);
        
        for (c = 0; c < len * 2; c++)
                buffer[c] += (int32_t)adlib->opl.buffer[c];

        adlib->opl.pos = 0;
}

uint8_t adlib_mca_read(int port, void *p)
{
        adlib_t *adlib = (adlib_t *)p;
        
        pclog("adlib_mca_read: port=%04x\n", port);
        
        return adlib->pos_regs[port & 7];
}

void adlib_mca_write(int port, uint8_t val, void *p)
{
        adlib_t *adlib = (adlib_t *)p;

        if (port < 0x102)
                return;
        
        pclog("adlib_mca_write: port=%04x val=%02x\n", port, val);
        
        switch (port)
        {
                case 0x102:
                if ((adlib->pos_regs[2] & 1) && !(val & 1))
                        io_removehandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &adlib->opl);
                if (!(adlib->pos_regs[2] & 1) && (val & 1))
                        io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &adlib->opl);
                break;
        }
        adlib->pos_regs[port & 7] = val;
}

void *adlib_init(device_t *info)
{
        adlib_t *adlib = malloc(sizeof(adlib_t));
        memset(adlib, 0, sizeof(adlib_t));
        
        pclog("adlib_init\n");
        opl2_init(&adlib->opl);
        io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &adlib->opl);
        sound_add_handler(adlib_get_buffer, adlib);
        return adlib;
}

void *adlib_mca_init(device_t *info)
{
        adlib_t *adlib = adlib_init(info);
        
        io_removehandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &adlib->opl);
        mca_add(adlib_mca_read, adlib_mca_write, adlib);
        adlib->pos_regs[0] = 0xd7;
        adlib->pos_regs[1] = 0x70;

        return adlib;
}

void adlib_close(void *p)
{
        adlib_t *adlib = (adlib_t *)p;

        free(adlib);
}

device_t adlib_device =
{
        "AdLib",
        0,
	0,
        adlib_init, adlib_close, NULL,
        NULL, NULL, NULL, NULL,
        NULL
};

device_t adlib_mca_device =
{
        "AdLib (MCA)",
        DEVICE_MCA,
	0,
        adlib_init, adlib_close, NULL,
        NULL, NULL, NULL, NULL,
        NULL
};
