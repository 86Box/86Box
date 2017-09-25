/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "ibm.h"
#include "io.h"
#include "lpt.h"
#include "sound/snd_lpt_dac.h"
#include "sound/snd_lpt_dss.h"


char lpt1_device_name[16];


static struct
{
        char name[64];
        char internal_name[16];
        lpt_device_t *device;
} lpt_devices[] =
{
        {"None",                         "none",           NULL},
        {"Disney Sound Source",          "dss",            &dss_device},
        {"LPT DAC / Covox Speech Thing", "lpt_dac",        &lpt_dac_device},
        {"Stereo LPT DAC",               "lpt_dac_stereo", &lpt_dac_stereo_device},
        {"", "", NULL}
};

char *lpt_device_get_name(int id)
{
        if (strlen(lpt_devices[id].name) == 0)
                return NULL;
        return lpt_devices[id].name;
}
char *lpt_device_get_internal_name(int id)
{
        if (strlen(lpt_devices[id].internal_name) == 0)
                return NULL;
        return lpt_devices[id].internal_name;
}

static lpt_device_t *lpt1_device;
static void *lpt1_device_p;

void lpt1_device_init()
{
        int c = 0;

        while (strcmp(lpt_devices[c].internal_name, lpt1_device_name) && strlen(lpt_devices[c].internal_name) != 0)
                c++;

        if (strlen(lpt_devices[c].internal_name) == 0)
                lpt1_device = NULL;
        else
        {
                lpt1_device = lpt_devices[c].device;
                if (lpt1_device)
                        lpt1_device_p = lpt1_device->init();
        }
}

void lpt1_device_close()
{
        if (lpt1_device)
                lpt1_device->close(lpt1_device_p);
        lpt1_device = NULL;
}

static uint8_t lpt1_dat, lpt2_dat, lpt3_dat;
static uint8_t lpt1_ctrl, lpt2_ctrl, lpt3_ctrl;

void lpt1_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port & 3)
        {
                case 0:
                if (lpt1_device)
                        lpt1_device->write_data(val, lpt1_device_p);
                lpt1_dat = val;
                break;
                case 2:
                if (lpt1_device)
                        lpt1_device->write_ctrl(val, lpt1_device_p);
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
                if (lpt1_device)
                        return lpt1_device->read_status(lpt1_device_p);
                return 0;
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
                lpt2_dat = val;
                break;
                case 2:
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
                return 0;
                case 2:
                return lpt2_ctrl;
        }
        return 0xff;
}

void lpt3_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port & 3)
        {
                case 0:
                lpt3_dat = val;
                break;
                case 2:
                lpt3_ctrl = val;
                break;
        }
}
uint8_t lpt3_read(uint16_t port, void *priv)
{
        switch (port & 3)
        {
                case 0:
                return lpt3_dat;
                case 1:
                return 0;
                case 2:
                return lpt3_ctrl;
        }
        return 0xff;
}

uint16_t lpt_addr[3] = { 0x378, 0x278, 0x3bc };

void lpt_init(void)
{
	if (lpt_enabled)
	{
	        io_sethandler(0x0378, 0x0003, lpt1_read, NULL, NULL, lpt1_write, NULL, NULL,  NULL);
        	io_sethandler(0x0278, 0x0003, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
		lpt_addr[0] = 0x378;
		lpt_addr[1] = 0x278;
	}
}

void lpt1_init(uint16_t port)
{
	if (lpt_enabled)
	{
	        io_sethandler(port, 0x0003, lpt1_read, NULL, NULL, lpt1_write, NULL, NULL,  NULL);
		lpt_addr[0] = port;
	}
}
void lpt1_remove(void)
{
	if (lpt_enabled)
	{
	        io_removehandler(lpt_addr[0], 0x0003, lpt1_read, NULL, NULL, lpt1_write, NULL, NULL,  NULL);
	}
}
void lpt2_init(uint16_t port)
{
	if (lpt_enabled)
	{
	        io_sethandler(port, 0x0003, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
		lpt_addr[1] = port;
	}
}
void lpt2_remove(void)
{
	if (lpt_enabled)
	{
	        io_removehandler(lpt_addr[1], 0x0003, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
	}
}

void lpt2_remove_ams(void)
{
	if (lpt_enabled)
	{
	        io_removehandler(0x0379, 0x0002, lpt2_read, NULL, NULL, lpt2_write, NULL, NULL,  NULL);
	}
}

void lpt3_init(uint16_t port)
{
	if (lpt_enabled)
	{
	        io_sethandler(port, 0x0003, lpt3_read, NULL, NULL, lpt3_write, NULL, NULL,  NULL);
		lpt_addr[2] = port;
	}
}
void lpt3_remove(void)
{
	if (lpt_enabled)
	{
	        io_removehandler(lpt_addr[2], 0x0003, lpt3_read, NULL, NULL, lpt3_write, NULL, NULL,  NULL);
	}
}
