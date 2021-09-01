#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/sound.h>
#include <86box/snd_resid.h>


typedef struct ssi2001_t
{
        void    *psid;
        int16_t buffer[SOUNDBUFLEN * 2];
        int     pos;
} ssi2001_t;

static void ssi2001_update(ssi2001_t *ssi2001)
{
        if (ssi2001->pos >= sound_pos_global)
                return;
        
        sid_fillbuf(&ssi2001->buffer[ssi2001->pos], sound_pos_global - ssi2001->pos, ssi2001->psid);
        ssi2001->pos = sound_pos_global;
}

static void ssi2001_get_buffer(int32_t *buffer, int len, void *p)
{
        ssi2001_t *ssi2001 = (ssi2001_t *)p;
        int c;

        ssi2001_update(ssi2001);
        
        for (c = 0; c < len * 2; c++)
                buffer[c] += ssi2001->buffer[c >> 1] / 2;

        ssi2001->pos = 0;
}

static uint8_t ssi2001_read(uint16_t addr, void *p)
{
        ssi2001_t *ssi2001 = (ssi2001_t *)p;
        
        ssi2001_update(ssi2001);
        
        return sid_read(addr, p);
}

static void ssi2001_write(uint16_t addr, uint8_t val, void *p)
{
        ssi2001_t *ssi2001 = (ssi2001_t *)p;
        
        ssi2001_update(ssi2001);        
        sid_write(addr, val, p);
}

void *ssi2001_init(const device_t *info)
{
        ssi2001_t *ssi2001 = malloc(sizeof(ssi2001_t));
        memset(ssi2001, 0, sizeof(ssi2001_t));

        ssi2001->psid = sid_init();
        sid_reset(ssi2001->psid);
        uint16_t addr = device_get_config_hex16("base");
        io_sethandler(addr, 0x0020, ssi2001_read, NULL, NULL, ssi2001_write, NULL, NULL, ssi2001);
        sound_add_handler(ssi2001_get_buffer, ssi2001);
        return ssi2001;
}

void ssi2001_close(void *p)
{
        ssi2001_t *ssi2001 = (ssi2001_t *)p;
        
        sid_close(ssi2001->psid);

        free(ssi2001);
}

static const device_config_t ssi2001_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x280, "", { 0 },
                {
                        {
                                "0x280", 0x280
                        },
                        {
                                "0x2A0", 0x2A0
                        },
                        {
                                "0x2C0", 0x2C0
                        },
                        {
                                "0x2E0", 0x2E0
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

const device_t ssi2001_device =
{
        "Innovation SSI-2001",
        0, 0,
        ssi2001_init, ssi2001_close, NULL,
	{ NULL }, NULL, NULL,
        ssi2001_config
};
