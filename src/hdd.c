#include "ibm.h"
#include "device.h"
#include "hdd.h"

#include "hdd_esdi.h"
#include "mfm_at.h"
#include "mfm_xebec.h"
#include "xtide.h"

char hdd_controller_name[16];

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
        {"None",                  "none",     &null_hdd_device, 0},
        {"AT Fixed Disk Adapter", "mfm_at",   &mfm_at_device,   1},
        {"DTC 5150X",                   "dtc5150x",  &dtc_5150x_device, 1},
        {"Fixed Disk Adapter (Xebec)",  "mfm_xebec", &mfm_xebec_device, 1},
        {"IBM ESDI Fixed Disk Adapter (MCA)", "esdi_mca",  &hdd_esdi_device,  1},
        {"XTIDE",                 "xtide",    &xtide_device,    0},
        {"XTIDE (AT)",            "xtide_at", &xtide_at_device, 0},
        {"XTIDE (PS/2)",          "xtide_ps2",&xtide_ps2_device,0},
        {"XTIDE (AT) (PS/2)",     "xtide_at_ps2",&xtide_at_ps2_device,0},
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
