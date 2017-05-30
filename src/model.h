/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of the emulated machines.
 *
 * Version:	@(#)model.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
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
