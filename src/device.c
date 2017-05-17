#include "ibm.h"
#include "cpu/cpu.h"
#include "config.h"
#include "device.h"
#include "model.h"
#include "sound/sound.h"


static void *device_priv[256];
static device_t *devices[256];

static device_t *current_device;

void device_init()
{
        memset(devices, 0, sizeof(devices));
}

void device_add(device_t *d)
{
        int c = 0;
        void *priv = NULL;
        
        while (devices[c] != NULL && c < 256)
                c++;
        
        if (c >= 256)
                fatal("device_add : too many devices\n");
        
        current_device = d;
        
        if (d->init != NULL)
        {
                priv = d->init();
                if (priv == NULL)
                        fatal("device_add : device init failed\n");
        }
        
        devices[c] = d;
        device_priv[c] = priv;        
}

void device_close_all()
{
        int c;
        
        for (c = 0; c < 256; c++)
        {
                if (devices[c] != NULL)
                {
                        if (devices[c]->close != NULL)
                                devices[c]->close(device_priv[c]);
                        devices[c] = device_priv[c] = NULL;
                }
        }
}

int device_available(device_t *d)
{
#ifdef RELEASE_BUILD
        if (d->flags & DEVICE_NOT_WORKING)
                return 0;
#endif
        if (d->available)
                return d->available();
                
        return 1;        
}

void device_speed_changed()
{
        int c;
        
        for (c = 0; c < 256; c++)
        {
                if (devices[c] != NULL)
                {
                        if (devices[c]->speed_changed != NULL)
                        {
                                devices[c]->speed_changed(device_priv[c]);
                        }
                }
        }
        
        sound_speed_changed();
}

void device_force_redraw()
{
        int c;
        
        for (c = 0; c < 256; c++)
        {
                if (devices[c] != NULL)
                {
                        if (devices[c]->force_redraw != NULL)
                        {
                                devices[c]->force_redraw(device_priv[c]);
                        }
                }
        }
}

char *device_add_status_info(char *s, int max_len)
{
        int c;
        
        for (c = 0; c < 256; c++)
        {
                if (devices[c] != NULL)
                {
                        if (devices[c]->add_status_info != NULL)
                                devices[c]->add_status_info(s, max_len, device_priv[c]);
                }
        }

	return NULL;
}

int device_get_config_int(char *s)
{
        device_config_t *config = current_device->config;
        
        while (config->type != -1)
        {
                if (!strcmp(s, config->name))
                        return config_get_int(current_device->name, s, config->default_int);

                config++;
        }
        return 0;
}

int device_get_config_int_ex(char *s, int default_int)
{
        device_config_t *config = current_device->config;
        
        while (config->type != -1)
        {
                if (!strcmp(s, config->name))
                        return config_get_int(current_device->name, s, default_int);

                config++;
        }
        return default_int;
}

void device_set_config_int(char *s, int val)
{
        device_config_t *config = current_device->config;
        
        while (config->type != -1)
        {
                if (!strcmp(s, config->name))
		{
                        config_set_int(current_device->name, s, val);
			return;
		}

                config++;
        }
        return;
}

char *device_get_config_string(char *s)
{
        device_config_t *config = current_device->config;
        
        while (config->type != -1)
        {
                if (!strcmp(s, config->name))
                        return config_get_string(current_device->name, s, config->default_string);

                config++;
        }
        return NULL;
}

int model_get_config_int(char *s)
{
        device_t *device = model_getdevice(model);
        device_config_t *config;

        if (!device)
                return 0;                

        config = device->config;
        
        while (config->type != -1)
        {
                if (!strcmp(s, config->name))
                        return config_get_int(device->name, s, config->default_int);

                config++;
        }
        return 0;
}

char *model_get_config_string(char *s)
{
        device_t *device = model_getdevice(model);
        device_config_t *config;
        
        if (!device)
                return 0;                

        config = device->config;
        
        while (config->type != -1)
        {
                if (!strcmp(s, config->name))
                        return config_get_string(device->name, s, config->default_string);

                config++;
        }
        return NULL;
}
