/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
typedef struct
{
        char name[24];
        int id;
        struct
        {
                char name[16];
                CPU *cpus;
        } cpu[5];
        int fixed_gfxcard;
        int is_at;
        int min_ram, max_ram;
        int ram_granularity;
        void (*init)();
} MODEL;

extern MODEL models[];

extern int model;

int model_count();
int model_getromset();
int model_getmodel(int romset);
char *model_getname();
void model_init();
