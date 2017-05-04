/* Copyright holders: Sarah Walker, Tohka
   see COPYING for more details
*/
#define MODEL_AT	   1
#define MODEL_PS2	   2
#define MODEL_AMSTRAD	   4
#define MODEL_OLIM24	   8
#define MODEL_HAS_IDE	  16
#define MODEL_MCA	  32
#define MODEL_PCI	  64
#define MODEL_PS2_HDD	 128
#define MODEL_NEC	 256
#define MODEL_FUJITSU	 512
#define MODEL_RM	1024

typedef struct
{
        char name[32];
        int id;
	char internal_name[24];
        struct
        {
                char name[16];
                CPU *cpus;
        } cpu[5];
        int fixed_gfxcard;
        int flags;
        int min_ram, max_ram;
        int ram_granularity;
        void (*init)();
	struct device_t *device;
} MODEL;

extern MODEL models[];

extern int model;

int model_count();
int model_getromset();
int model_getmodel(int romset);
char *model_getname();
char *model_get_internal_name();
int model_get_model_from_internal_name(char *s);
void model_init();
struct device_t *model_getdevice(int model);
int model_getromset_ex(int m);
