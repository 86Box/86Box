/*PCem v0.8 by Tom Walker

  Windows Sound System emulation*/

#include <math.h>  
#include <stdlib.h>
#include "ibm.h"

#include "device.h"
#include "dma.h"
#include "io.h"
#include "pic.h"
#include "sound_ad1848.h"
#include "sound_opl.h"
#include "sound_wss.h"

/*530, 11, 3 - 530=23*/
/*530, 11, 1 - 530=22*/
/*530, 11, 0 - 530=21*/
/*530, 10, 1 - 530=1a*/
/*530, 9,  1 - 530=12*/
/*530, 7,  1 - 530=0a*/
/*604, 11, 1 - 530=22*/
/*e80, 11, 1 - 530=22*/
/*f40, 11, 1 - 530=22*/


static int wss_dma[4] = {0, 0, 1, 3};
static int wss_irq[8] = {5, 7, 9, 10, 11, 12, 14, 15}; /*W95 only uses 7-9, others may be wrong*/
static uint16_t wss_addr[4] = {0x530, 0x604, 0xe80, 0xf40};

typedef struct wss_t
{
        uint8_t config;

        ad1848_t ad1848;        
        opl_t    opl;
} wss_t;

uint8_t wss_read(uint16_t addr, void *p)
{
        wss_t *wss = (wss_t *)p;
        uint8_t temp;
//        pclog("wss_read - addr %04X %04X(%08X):%08X ", addr, CS, cs, pc);
        temp = 4 | (wss->config & 0x40);
//        pclog("return %02X\n", temp);
        return temp;
}

void wss_write(uint16_t addr, uint8_t val, void *p)
{
        wss_t *wss = (wss_t *)p;
//        pclog("wss_write - addr %04X val %02X  %04X(%08X):%08X\n", addr, val, CS, cs, pc);

        wss->config = val;
        ad1848_setdma(&wss->ad1848, wss_dma[val & 3]);
        ad1848_setirq(&wss->ad1848, wss_irq[(val >> 3) & 7]);
}

static void wss_get_buffer(int32_t *buffer, int len, void *p)
{
        wss_t *wss = (wss_t *)p;
        
        int c;

        opl3_update2(&wss->opl);
        ad1848_update(&wss->ad1848);
        for (c = 0; c < len * 2; c++)
        {
                buffer[c] += wss->opl.buffer[c];
                buffer[c] += (wss->ad1848.buffer[c] / 2);
        }

        wss->opl.pos = 0;
        wss->ad1848.pos = 0;
}

void *wss_init()
{
        wss_t *wss = malloc(sizeof(wss_t));
        int c;
        double attenuation;

        memset(wss, 0, sizeof(wss_t));

        opl3_init(&wss->opl);
        ad1848_init(&wss->ad1848);
        
        ad1848_setirq(&wss->ad1848, 7);
        ad1848_setdma(&wss->ad1848, 3);

        io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL,  &wss->opl);
        io_sethandler(0x0530, 0x0004, wss_read,    NULL, NULL, wss_write,    NULL, NULL,  wss);
        io_sethandler(0x0534, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL,  &wss->ad1848);
                
        sound_add_handler(wss_get_buffer, wss);
        
        return wss;
}

void wss_close(void *p)
{
        wss_t *wss = (wss_t *)p;
        
        free(wss);
}

void wss_speed_changed(void *p)
{
        wss_t *wss = (wss_t *)p;
        
        ad1848_speed_changed(&wss->ad1848);
}

device_t wss_device =
{
        "Windows Sound System",
        0,
        wss_init,
        wss_close,
        NULL,
        wss_speed_changed,
        NULL,
        NULL
};
