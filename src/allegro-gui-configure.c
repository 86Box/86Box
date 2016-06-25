#include "ibm.h"
#include "device.h"
#include "allegro-main.h"
#include "allegro-gui.h"
#include "cpu.h"
#include "fdd.h"
#include "gameport.h"
#include "model.h"
#include "sound.h"
#include "video.h"
#include "vid_voodoo.h"

static int romstolist[ROM_MAX], listtomodel[ROM_MAX], romstomodel[ROM_MAX], modeltolist[ROM_MAX];
static int settings_sound_to_list[20], settings_list_to_sound[20];

typedef struct allegro_list_t
{
        char name[256];
        int num;
} allegro_list_t;

static allegro_list_t model_list[ROM_MAX+1];
static allegro_list_t video_list[GFX_MAX+1];
static allegro_list_t sound_list[GFX_MAX+1];
static allegro_list_t cpumanu_list[4];
static allegro_list_t cpu_list[32];
static allegro_list_t joystick_list[32];

static char mem_size_str[10], mem_size_units[3];

static allegro_list_t cache_list[] =
{
        {"A little", 0},
        {"A bit", 1},
        {"Some", 2},
        {"A lot", 3},
        {"Infinite", 4},
        {"", -1}
};

static allegro_list_t vidspeed_list[] =
{
        {"8-bit", 0},
        {"Slow 16-bit", 1},
        {"Fast 16-bit", 2},
        {"Slow VLB/PCI", 3},
        {"Mid  VLB/PCI", 4},
        {"Fast VLB/PCI", 5},
        {"", -1}
};                

static allegro_list_t fdd_list[] =
{
	{"None", 0},
	{"5.25\" 360k", 1},
	{"5.25\" 1.2M", 2},
	{"5.25\" 1.2M Dual RPM", 3},
	{"3.5\" 720k", 4},
	{"3.5\" 1.44M", 5},
	{"3.5\" 1.44M 3-Mode", 6},
	{"3.5\" 2.88M", 7},
        {"", -1}
};

static void reset_list();

static char *list_proc_model(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (model_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return model_list[index].name;
}

static char *list_proc_video(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (video_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return video_list[index].name;
}

static char *list_proc_cache(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (cache_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return cache_list[index].name;
}

static char *list_proc_vidspeed(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (vidspeed_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return vidspeed_list[index].name;
}

static char *list_proc_sound(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (sound_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return sound_list[index].name;
}

static char *list_proc_cpumanu(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (cpumanu_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return cpumanu_list[index].name;
}

static char *list_proc_cpu(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (cpu_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return cpu_list[index].name;
}

static char *list_proc_fdd(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (fdd_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return fdd_list[index].name;
}

static char *list_proc_joystick(int index, int *list_size)
{
        if (index < 0)
        {
                int c = 0;
                
                while (joystick_list[c].name[0])
                        c++;

                *list_size = c;
                return NULL;
        }
        
        return joystick_list[index].name;
}

static int voodoo_config_proc(int msg, DIALOG *d, int c)
{
        int ret = d_button_proc(msg, d, c);
        
        if (ret == D_CLOSE)
        {
                deviceconfig_open(&voodoo_device);
                return D_O_K;
        }
        
        return ret;
}


static int video_config_proc(int msg, DIALOG *d, int c);
static int sound_config_proc(int msg, DIALOG *d, int c);
static int list_proc(int msg, DIALOG *d, int c);

static DIALOG configure_dialog[] =
{
        {d_shadow_box_proc, 0, 0, 568,352,0,0xffffff,0,0,     0,0,0,0,0}, // 0

        {d_button_proc, 226,  328, 50, 16, 0, 0xffffff, 0, D_EXIT, 0, 0, "OK",     0, 0}, // 1
        {d_button_proc, 296,  328, 50, 16, 0, 0xffffff, 0, D_EXIT, 0, 0, "Cancel", 0, 0}, // 2

        {list_proc,      70*2, 12,  152*2, 20, 0, 0xffffff, 0, 0,      0, 0, list_proc_model, 0, 0},

        {list_proc,      70*2, 32,  152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_video, 0, 0},

        {list_proc,      70*2,  52, 152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_cpumanu, 0, 0}, //5
        {list_proc,      70*2,  72, 152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_cpu, 0, 0},
        {d_list_proc,    70*2, 112, 152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_cache, 0, 0},
        {d_list_proc,    70*2, 132, 152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_vidspeed, 0, 0},
        {list_proc,      70*2, 152, 152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_sound, 0, 0}, //9
        
        {d_edit_proc,    70*2, 236,    32, 14, 0, 0xffffff, 0, 0, 3, 0, mem_size_str, 0, 0},
                        
        {d_text_proc,    98*2, 236,  40, 10, 0, 0xffffff, 0, 0, 0, 0, mem_size_units, 0, 0},
        
        {d_check_proc,   14*2, 252, 118*2, 10, 0, 0xffffff, 0, 0, 0, 0, "CMS / Game Blaster", 0, 0},
        {d_check_proc,   14*2, 268, 118*2, 10, 0, 0xffffff, 0, 0, 0, 0, "Gravis Ultrasound", 0, 0},
        {d_check_proc,   14*2, 284, 118*2, 10, 0, 0xffffff, 0, 0, 0, 0, "Innovation SSI-2001", 0, 0},
        {d_check_proc,   14*2, 300, 118*2, 10, 0, 0xffffff, 0, 0, 0, 0, "Composite CGA", 0, 0},
        {d_check_proc,   14*2, 316, 118*2, 10, 0, 0xffffff, 0, 0, 0, 0, "Voodoo Graphics", 0, 0},

        {d_text_proc,    16*2,  16,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Machine :", 0, 0},
        {d_text_proc,    16*2,  36,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Video :", 0, 0},
        {d_text_proc,    16*2,  56,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "CPU type :", 0, 0},
        {d_text_proc,    16*2,  76,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "CPU :", 0, 0},
        {d_text_proc,    16*2, 116,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Cache :", 0, 0},
        {d_text_proc,    16*2, 136,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Video speed :", 0, 0},
        {d_text_proc,    16*2, 156,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Soundcard :", 0, 0},
        {d_text_proc,    16*2, 236,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Memory :", 0, 0},

        {d_check_proc,   14*2,  92, 118*2, 10, 0, 0xffffff, 0, 0, 0, 0, "Dynamic Recompiler", 0, 0},
        
        {d_text_proc,    16*2, 176,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Drive A: :", 0, 0},
        {d_text_proc,    16*2, 196,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Drive B: :", 0, 0},
        {d_list_proc,    70*2, 172, 152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_fdd, 0, 0},
        {d_list_proc,    70*2, 192, 152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_fdd, 0, 0},

        {video_config_proc, 452,  32+4, 100, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "Configure...",     0, 0}, //30
        {sound_config_proc, 452, 152+4, 100, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "Configure...",     0, 0},
        {voodoo_config_proc, 452,   316, 100, 14, 0, 0xffffff, 0, D_EXIT, 0, 0, "Configure...",     0, 0},

        {d_text_proc,    16*2, 216,  40, 10, 0, 0xffffff, 0, 0, 0, 0, "Joystick :", 0, 0},
        {d_list_proc,    70*2, 212, 152*2, 20, 0, 0xffffff, 0, 0, 0, 0, list_proc_joystick, 0, 0}, //34
                
        {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

static int list_proc(int msg, DIALOG *d, int c)
{
        int old = d->d1;
        int ret = d_list_proc(msg, d, c);
        
        if (d->d1 != old)
        {
                int new_model = model_list[configure_dialog[3].d1].num;
                int new_cpu_m = configure_dialog[5].d1;
                int new_cpu = configure_dialog[6].d1;
                int new_dynarec = configure_dialog[25].flags & D_SELECTED;
                int new_gfxcard = video_old_to_new(video_list[configure_dialog[4].d1].num);
		int new_mem_size;
                int cpu_flags;

                reset_list();

                if (models[new_model].fixed_gfxcard)
                        configure_dialog[4].flags |= D_DISABLED;
                else
                        configure_dialog[4].flags &= ~D_DISABLED;

                cpu_flags = models[new_model].cpu[new_cpu_m].cpus[new_cpu].cpu_flags;
                configure_dialog[25].flags = (((cpu_flags & CPU_SUPPORTS_DYNAREC) && new_dynarec) || (cpu_flags & CPU_REQUIRES_DYNAREC)) ? D_SELECTED : 0;
                if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC))
                        configure_dialog[25].flags |= D_DISABLED;

                sscanf(mem_size_str, "%i", &new_mem_size);
                new_mem_size &= ~(models[new_model].ram_granularity - 1);
                if (new_mem_size < models[new_model].min_ram)
                	new_mem_size = models[new_model].min_ram;
                else if (new_mem_size > models[new_model].max_ram)
			new_mem_size = models[new_model].max_ram;
		sprintf(mem_size_str, "%i", new_mem_size);

		if (models[new_model].is_at)
			sprintf(mem_size_units, "MB");
		else
			sprintf(mem_size_units, "kB");

                if (!video_card_has_config(new_gfxcard))
                        configure_dialog[30].flags |= D_DISABLED;
                else
                        configure_dialog[30].flags &= ~D_DISABLED;

                if (!sound_card_has_config(configure_dialog[9].d1))
                        configure_dialog[31].flags |= D_DISABLED;
                else
                        configure_dialog[31].flags &= ~D_DISABLED;

                return D_REDRAW;
        }

        return ret;
}

static int video_config_proc(int msg, DIALOG *d, int c)
{
        int ret = d_button_proc(msg, d, c);
        
        if (ret == D_CLOSE)
        {
                int new_gfxcard = video_old_to_new(video_list[configure_dialog[4].d1].num);
                
                deviceconfig_open(video_card_getdevice(new_gfxcard));
                return D_O_K;
        }
        
        return ret;
}
static int sound_config_proc(int msg, DIALOG *d, int c)
{
        int ret = d_button_proc(msg, d, c);
        
        if (ret == D_CLOSE)
        {
                int new_sndcard = sound_list[configure_dialog[9].d1].num;

                deviceconfig_open(sound_card_getdevice(new_sndcard));
                return D_O_K;
        }
        
        return ret;
}

static void reset_list()
{
        int model = model_list[configure_dialog[3].d1].num;
        int cpumanu = configure_dialog[5].d1;
        int cpu = configure_dialog[6].d1;
        int c;
        
        memset(cpumanu_list, 0, sizeof(cpumanu_list));
        memset(cpu_list, 0, sizeof(cpu_list));

        c = 0;
        while (models[model].cpu[c].cpus != NULL && c < 3)
        {
                strcpy(cpumanu_list[c].name, models[model].cpu[c].name);
                cpumanu_list[c].num = c;
                c++;
        }
        
        if (cpumanu >= c)
                cpumanu = configure_dialog[6].d1 = c-1;

        c = 0;
        while (models[model].cpu[cpumanu].cpus[c].cpu_type != -1)
        {
                strcpy(cpu_list[c].name, models[model].cpu[cpumanu].cpus[c].name);
                cpu_list[c].num = c;
                c++;
        }

        if (cpu >= c)
                cpu = configure_dialog[7].d1 = c-1;
}

int settings_configure()
{
        int c, d;
        int cpu_flags;
        
        memset(model_list, 0, sizeof(model_list));
        memset(video_list, 0, sizeof(video_list));
        memset(sound_list, 0, sizeof(sound_list));

        for (c = 0; c < ROM_MAX; c++)
                romstolist[c] = 0;
        c = d = 0;
        while (models[c].id != -1)
        {
                pclog("INITDIALOG : %i %i %i\n",c,models[c].id,romspresent[models[c].id]);
                if (romspresent[models[c].id])
                {
                        strcpy(model_list[d].name, models[c].name);
                        model_list[d].num = c;
                        if (c == model)
                                configure_dialog[3].d1 = d;
                        d++;
                }
                c++;
        }

        if (models[model].fixed_gfxcard)
                configure_dialog[4].flags |= D_DISABLED;
        else
                configure_dialog[4].flags &= ~D_DISABLED;

        c = d = 0;
        while (1)
        {
                char *s = video_card_getname(c);

                if (!s[0])
                        break;
pclog("video_card_available : %i\n", c);
                if (video_card_available(c))
                {
                        strcpy(video_list[d].name, video_card_getname(c));
                        video_list[d].num = video_new_to_old(c);
                        if (video_new_to_old(c) == gfxcard)
                                configure_dialog[4].d1 = d;
                        d++;
                }

                c++;
        }

        if (!video_card_has_config(video_old_to_new(gfxcard)))
                configure_dialog[30].flags |= D_DISABLED;
        else
                configure_dialog[30].flags &= ~D_DISABLED;

        c = d = 0;
        while (1)
        {
                char *s = sound_card_getname(c);

                if (!s[0])
                        break;

                if (sound_card_available(c))
                {
                        strcpy(sound_list[d].name, sound_card_getname(c));
                        sound_list[d].num = c;
                        if (c == sound_card_current)
                                configure_dialog[9].d1 = d;
                        d++;
                }

                c++;
        }

        c = 0;
        while (joystick_get_name(c))
        {
                strcpy(joystick_list[c].name, joystick_get_name(c));
                if (c == joystick_type)
                        configure_dialog[34].d1 = c;

                c++;
        }

        if (!sound_card_has_config(configure_dialog[9].d1))
                configure_dialog[31].flags |= D_DISABLED;
        else
                configure_dialog[31].flags &= ~D_DISABLED;

        configure_dialog[5].d1 = cpu_manufacturer;
        configure_dialog[6].d1 = cpu;
        configure_dialog[7].d1 = cache;
        configure_dialog[8].d1 = video_speed;
        reset_list();
//        strcpy(cpumanu_str, models[romstomodel[romset]].cpu[cpu_manufacturer].name);
//        strcpy(cpu_str, models[romstomodel[romset]].cpu[cpu_manufacturer].cpus[cpu].name);
//        strcpy(cache_str, cache_str_list[cache]);
//        strcpy(vidspeed_str, vidspeed_str_list[video_speed]);

//        strcpy(soundcard_str, sound_card_getname(sound_card_current));

        if (GAMEBLASTER)
                configure_dialog[12].flags |= D_SELECTED;
        else
                configure_dialog[12].flags &= ~D_SELECTED;

        if (GUS)
                configure_dialog[13].flags |= D_SELECTED;
        else
                configure_dialog[13].flags &= ~D_SELECTED;

        if (SSI2001)
                configure_dialog[14].flags |= D_SELECTED;
        else
                configure_dialog[14].flags &= ~D_SELECTED;

        if (cga_comp)
                configure_dialog[15].flags |= D_SELECTED;
        else
                configure_dialog[15].flags &= ~D_SELECTED;

        if (voodoo_enabled)
                configure_dialog[16].flags |= D_SELECTED;
        else
                configure_dialog[16].flags &= ~D_SELECTED;

	if (models[model].is_at)
	        sprintf(mem_size_str, "%i", mem_size / 1024);
	else
	        sprintf(mem_size_str, "%i", mem_size);

	if (models[model].is_at)
		sprintf(mem_size_units, "MB");
	else
		sprintf(mem_size_units, "kB");

        cpu_flags = models[model].cpu[cpu_manufacturer].cpus[cpu].cpu_flags;
        configure_dialog[25].flags = (((cpu_flags & CPU_SUPPORTS_DYNAREC) && cpu_use_dynarec) || (cpu_flags & CPU_REQUIRES_DYNAREC)) ? D_SELECTED : 0;
        if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC))
                configure_dialog[25].flags |= D_DISABLED;

        configure_dialog[28].d1 = fdd_get_type(0);
        configure_dialog[29].d1 = fdd_get_type(1);

        while (1)
        {
                position_dialog(configure_dialog, SCREEN_W/2 - configure_dialog[0].w/2, SCREEN_H/2 - configure_dialog[0].h/2);
        
                c = popup_dialog(configure_dialog, 1);

                position_dialog(configure_dialog, -(SCREEN_W/2 - configure_dialog[0].w/2), -(SCREEN_H/2 - configure_dialog[0].h/2));
                
                if (c == 1)
                {
                        int new_model = model_list[configure_dialog[3].d1].num;
                        int new_gfxcard = video_list[configure_dialog[4].d1].num;
                        int new_sndcard = sound_list[configure_dialog[9].d1].num;
                        int new_cpu_m = configure_dialog[5].d1;
                        int new_cpu = configure_dialog[6].d1;
                        int new_mem_size;
                        int new_has_fpu = (models[new_model].cpu[new_cpu_m].cpus[new_cpu].cpu_type >= CPU_i486DX) ? 1 : 0;
                        int new_GAMEBLASTER = (configure_dialog[12].flags & D_SELECTED) ? 1 : 0;
                        int new_GUS = (configure_dialog[13].flags & D_SELECTED) ? 1 : 0;
                        int new_SSI2001 = (configure_dialog[14].flags & D_SELECTED) ? 1 : 0;
                        int new_voodoo = (configure_dialog[16].flags & D_SELECTED) ? 1 : 0;
                        int new_dynarec = (configure_dialog[25].flags & D_SELECTED) ? 1 : 0;
			int new_fda = configure_dialog[28].d1;
			int new_fdb = configure_dialog[29].d1;
                        
                        sscanf(mem_size_str, "%i", &new_mem_size);
                        new_mem_size &= ~(models[new_model].ram_granularity - 1);
                        if (new_mem_size < models[new_model].min_ram)
                                new_mem_size = models[new_model].min_ram;
                        else if (new_mem_size > models[new_model].max_ram)
                                new_mem_size = models[new_model].max_ram;
			if (models[new_model].is_at)
				new_mem_size *= 1024;
                        
                        if (new_model != model || new_gfxcard != gfxcard || new_mem_size != mem_size || 
                            new_has_fpu != hasfpu || new_GAMEBLASTER != GAMEBLASTER || new_GUS != GUS ||
                            new_SSI2001 != SSI2001 || new_sndcard != sound_card_current || new_voodoo != voodoo_enabled ||
                            new_dynarec != cpu_use_dynarec || new_fda != fdd_get_type(0) || new_fdb != fdd_get_type(1))
                        {
                                if (alert("This will reset PCem!", "Okay to continue?", NULL, "OK", "Cancel", 0, 0) != 1)
                                        continue;

                                model = new_model;
                                romset = model_getromset();
                                gfxcard = new_gfxcard;
                                mem_size = new_mem_size;
                                cpu_manufacturer = new_cpu_m;
                                cpu = new_cpu;
                                GAMEBLASTER = new_GAMEBLASTER;
                                GUS = new_GUS;
                                SSI2001 = new_SSI2001;
                                sound_card_current = new_sndcard;
				voodoo_enabled = new_voodoo;
				cpu_use_dynarec = new_dynarec;
                                        
                                mem_resize();
                                loadbios();
                                resetpchard();

				fdd_set_type(0, new_fda);
				fdd_set_type(1, new_fdb);
                        }

                        video_speed = configure_dialog[8].d1;

                        cga_comp = (configure_dialog[15].flags & D_SELECTED) ? 1 : 0;

                        cpu_manufacturer = new_cpu_m;
                        cpu = new_cpu;
                        cpu_set();
                        
                        cache = configure_dialog[7].d1;
                        mem_updatecache();
                        
                        joystick_type = configure_dialog[34].d1;
                        gameport_update_joystick_type();
                        
                        saveconfig();

                        speedchanged();

                        return D_O_K;
                }
                
                if (c == 2)
                        return D_O_K;
        }
        
        return D_O_K;
}

