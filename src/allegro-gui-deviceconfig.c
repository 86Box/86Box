/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "device.h"
#include "allegro-main.h"
#include "allegro-gui.h"
#include "config.h"

static device_t *config_device;

#define MAX_CONFIG_SIZE 64
#define MAX_CONFIG_SELECTIONS 8

static device_config_selection_t *config_selections[MAX_CONFIG_SELECTIONS];

#define list_proc_device_func(i)                                                \
        static char *list_proc_device_ ## i(int index, int *list_size)          \
        {                                                                       \
                device_config_selection_t *config = config_selections[i];       \
                                                                                \
                if (index < 0)                                                  \
                {                                                               \
                        int c = 0;                                              \
                                                                                \
                        while (config[c].description[0])                        \
                                c++;                                            \
                                                                                \
                        *list_size = c;                                         \
                        return NULL;                                            \
                }                                                               \
                                                                                \
                return config[index].description;                               \
        }

list_proc_device_func(0)
list_proc_device_func(1)
list_proc_device_func(2)
list_proc_device_func(3)
list_proc_device_func(4)
list_proc_device_func(5)
list_proc_device_func(6)
list_proc_device_func(7)

static DIALOG deviceconfig_dialog[MAX_CONFIG_SIZE] =
{
        {d_shadow_box_proc, 0, 0, 568,332,0,0xffffff,0,0,     0,0,0,0,0} // 0
};

void deviceconfig_open(device_t *device)
{
        DIALOG *d;
        device_config_t *config = device->config;
        int y = 10;
        int dialog_pos = 1;
        int list_pos = 0;
        int c;
        int id_ok, id_cancel;

        memset((void *)((uintptr_t)deviceconfig_dialog) + sizeof(DIALOG), 0, sizeof(deviceconfig_dialog) - sizeof(DIALOG));
        deviceconfig_dialog[0].x = deviceconfig_dialog[0].y = 0;
        
        while (config->type != -1)
        {
                d = &deviceconfig_dialog[dialog_pos];
                
                switch (config->type)
                {
                        case CONFIG_BINARY:
                        d->x = 32;
                        d->y = y;
                
                        d->w = 118*2;
                        d->h = 15;
                        
                        d->dp = config->description;
                        d->proc = d_check_proc;
                        
                        d->flags = config_get_int(device->name, config->name, config->default_int) ? D_SELECTED : 0;
                        d->bg = 0xffffff;
                        d->fg = 0;
                        
                        dialog_pos++;

                        y += 20;
                        break;

                        case CONFIG_SELECTION:
                        if (list_pos >= MAX_CONFIG_SELECTIONS)
                                break;
                                
                        d->x = 32;
                        d->y = y;
                
                        d->w = 80;
                        d->h = 15;
                        
                        d->dp = config->description;
                        d->proc = d_text_proc;
                        
                        d->flags = 0;
                        d->bg = 0xffffff;
                        d->fg = 0;
                        
                        d++;

                        d->x = 250;
                        d->y = y;
                
                        d->w = 304;
                        d->h = 20;
                        
                        switch (list_pos)
                        {
                                case 0 : d->dp = list_proc_device_0; break;
                                case 1 : d->dp = list_proc_device_1; break;
                                case 2 : d->dp = list_proc_device_2; break;
                                case 3 : d->dp = list_proc_device_3; break;
                                case 4 : d->dp = list_proc_device_4; break;
                                case 5 : d->dp = list_proc_device_5; break;
                                case 6 : d->dp = list_proc_device_6; break;
                                case 7 : d->dp = list_proc_device_7; break;
                        }
                        d->proc = d_list_proc;
                        
                        d->flags = 0;
                        d->bg = 0xffffff;
                        d->fg = 0;
                        
                        config_selections[list_pos++] = config->selection;
                        
                        c = 0;
                        while (config->selection[c].description[0])
                        {
                                if (config_get_int(device->name, config->name, config->default_int) == config->selection[c].value)
                                        d->d1 = c;
                                c++;
                        }
                        
                        dialog_pos += 2;

                        y += 20;
                        break;

                        case CONFIG_MIDI:
                        break;
                }
 
                config++;
                
                if (dialog_pos >= MAX_CONFIG_SIZE-3)
                        break;
        }
        
        d = &deviceconfig_dialog[dialog_pos];

        id_ok = dialog_pos;
        id_cancel = dialog_pos + 1;
        
        d->x = 226;
        d->y = y+8;
                
        d->w = 50;
        d->h = 16;
                        
        d->dp = "OK";
        d->proc = d_button_proc;
                        
        d->flags = D_EXIT;
        d->bg = 0xffffff;
        d->fg = 0;

        d++;
        
        d->x = 296;
        d->y = y+8;

        d->w = 50;
        d->h = 16;

        d->dp = "Cancel";
        d->proc = d_button_proc;
                        
        d->flags = D_EXIT;
        d->bg = 0xffffff;
        d->fg = 0;

        deviceconfig_dialog[0].h = y + 28;
                
        config_device = device;

        while (1)
        {
                position_dialog(deviceconfig_dialog, SCREEN_W/2 - deviceconfig_dialog[0].w/2, SCREEN_H/2 - deviceconfig_dialog[0].h/2);
        
                c = popup_dialog(deviceconfig_dialog, 1);

                position_dialog(deviceconfig_dialog, -(SCREEN_W/2 - deviceconfig_dialog[0].w/2), -(SCREEN_H/2 - deviceconfig_dialog[0].h/2));

                if (c == id_ok)
                {
                        int changed = 0;
                        
                        dialog_pos = 1;
                        config = device->config;
                        
                        while (config->type != -1)
                        {
                                int val;
                                
                                d = &deviceconfig_dialog[dialog_pos];

                                switch (config->type)
                                {
                                        case CONFIG_BINARY:
                                        val = (d->flags & D_SELECTED) ? 1 : 0;
                                        
                                        if (val != config_get_int(device->name, config->name, config->default_int))
                                                changed = 1;
                                        
                                        dialog_pos++;
                                        break;

                                        case CONFIG_SELECTION:
                                        if (list_pos >= MAX_CONFIG_SELECTIONS)
                                                break;

                                        d++;
                                        
                                        val = config->selection[d->d1].value;

                                        if (val != config_get_int(device->name, config->name, config->default_int))
                                                changed = 1;

                                        dialog_pos += 2;
                                        break;

                                        case CONFIG_MIDI:
                                        break;
                                }

                                config++;
                
                                if (dialog_pos >= MAX_CONFIG_SIZE-3)
                                        break;
                        }

                        if (!changed)
                                return;

                        if (alert("This will reset 86Box!", "Okay to continue?", NULL, "OK", "Cancel", 0, 0) != 1)
                                continue;

                        dialog_pos = 1;
                        config = device->config;

                        while (config->type != -1)
                        {
                                int val;
                                
                                d = &deviceconfig_dialog[dialog_pos];
                                                                
                                switch (config->type)
                                {
                                        case CONFIG_BINARY:
                                        val = (d->flags & D_SELECTED) ? 1 : 0;
                                        
                                        config_set_int(config_device->name, config->name, val);
                                                
                                        dialog_pos++;
                                        break;
                                     
                                        case CONFIG_SELECTION:
                                        if (list_pos >= MAX_CONFIG_SELECTIONS)
                                                break;

                                        d++;
                                        
                                        val = config->selection[d->d1].value;

                                        config_set_int(config_device->name, config->name, val);
                                        
                                        dialog_pos += 2;
                                        break;

                                        case CONFIG_MIDI:
                                        break;
                                }

                                config++;

                                if (dialog_pos >= MAX_CONFIG_SIZE-3)
                                        break;
                        }

                        saveconfig();
                        
                        resetpchard();
                        
                        return;
                }
                
                if (c == id_cancel)
                        break;
        }
}
