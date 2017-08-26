#include "../ibm.h"
#include "../CPU/cpu.h"
#include "../device.h"
#include "../model.h"

#include "hdd.h"

#include "hdd_esdi_at.h"
#include "hdd_esdi_mca.h"
#include "hdd_mfm_at.h"
#include "hdd_mfm_xebec.h"
#include "hdd_ide_xt.h"


char hdd_controller_name[16];
hard_disk_t hdc[HDC_NUM];

static device_t null_hdd_device;
static int hdd_controller_current;


static struct
{
        char name[50];
        char internal_name[16];
        device_t *device;
        int is_mfm;
} hdd_controllers[] = 
{
        {"None",					"none",		&null_hdd_device,	0},
        {"[MFM] AT Fixed Disk Adapter",			"mfm_at",	&mfm_at_device,		1},
        {"[MFM] DTC 5150X",				"dtc5150x",	&dtc_5150x_device,	1},
        {"[MFM] Fixed Disk Adapter (Xebec)",		"mfm_xebec",	&mfm_xebec_device,	1},
        {"[ESDI] IBM ESDI Fixed Disk Adapter",		"esdi_mca",	&hdd_esdi_device,	1},
        {"[ESDI] Western Digital WD1007V-SE1",		"wd1007vse1",	&wd1007vse1_device,	0},
        {"[IDE] XTIDE",					"xtide",	&xtide_device,		0},
        {"[IDE] XTIDE (Acculogic)",			"xtide_ps2",	&xtide_ps2_device,	0},
        {"[IDE] XTIDE (AT)",				"xtide_at",	&xtide_at_device,	0},
        {"[IDE] XTIDE (AT) (1.1.5)",			"xtide_at_ps2",	&xtide_at_ps2_device,	0},
        {"", "", NULL, 0}
};

char *hdd_controller_get_name(int hdd)
{
        return hdd_controllers[hdd].name;
}

char *hdd_controller_get_internal_name(int hdd)
{
        return hdd_controllers[hdd].internal_name;
}

int hdd_controller_get_flags(int hdd)
{
        return hdd_controllers[hdd].device->flags;
}

int hdd_controller_available(int hdd)
{
        return device_available(hdd_controllers[hdd].device);
}

int hdd_controller_current_is_mfm()
{
        return hdd_controllers[hdd_controller_current].is_mfm;
}

void hdd_controller_init(char *internal_name)
{
        int c = 0;

	if (models[model].flags & MODEL_HAS_IDE)
	{
		return;
	}

        while (hdd_controllers[c].device)
        {
                if (!strcmp(internal_name, hdd_controllers[c].internal_name))
                {
                        hdd_controller_current = c;
                        if (strcmp(internal_name, "none"))
                                device_add(hdd_controllers[c].device);
                        return;
                }
                c++;
        }
}


static void *null_hdd_init()
{
        return NULL;
}

static void null_hdd_close(void *p)
{
}

static device_t null_hdd_device =
{
        "Null HDD controller",
        0,
        null_hdd_init,
        null_hdd_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
