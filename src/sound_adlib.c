#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "sound.h"

#include "sound_adlib.h"
#include "sound_opl.h"

typedef struct adlib_t
{
        opl_t   opl;
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

void *adlib_init()
{
        adlib_t *adlib = malloc(sizeof(adlib_t));
        memset(adlib, 0, sizeof(adlib_t));
        
        pclog("adlib_init\n");
        opl2_init(&adlib->opl);
        io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &adlib->opl);
        sound_add_handler(adlib_get_buffer, adlib);
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
        adlib_init,
        adlib_close,
        NULL,
        NULL,
        NULL,
        NULL
};
